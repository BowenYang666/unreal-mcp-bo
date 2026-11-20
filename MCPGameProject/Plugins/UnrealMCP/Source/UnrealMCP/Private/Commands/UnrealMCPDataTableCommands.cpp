#include "Commands/UnrealMCPDataTableCommands.h"
#include "Commands/UnrealMCPAssetProperties.h"
#include "Engine/DataTable.h"
#include "EditorAssetLibrary.h"
#include "EditorFramework/AssetImportData.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "Editor.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/DataTableFactory.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/DataValidation.h"
#include "ScopedTransaction.h"
#include "Serialization/Csv/CsvParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StructOnScope.h"
#include "DataTableUtils.h"

namespace UnrealMCPDataTableCommands
{
namespace
{
constexpr int32 MaxRows = 4096;
constexpr int32 MaxBytes = 8 * 1024 * 1024;

class FBoundedWriter : public FMemoryWriter
{
public:
    explicit FBoundedWriter(TArray<uint8>& Bytes) : FMemoryWriter(Bytes) {}
    virtual void Serialize(void* Data, int64 Count) override
    {
        if (Count < 0 || Count > MaxBytes || Tell() > MaxBytes - Count) { SetError(); return; }
        FMemoryWriter::Serialize(Data, Count);
    }
};

bool Busy()
{
    return !IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor ||
        GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask || FAssetCompilingManager::Get().GetNumRemainingAssets() > 0 ||
        (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid());
}

bool Bool(const TSharedPtr<FJsonObject>& Params, const TCHAR* Name, bool& Value)
{
    return !Params->HasField(Name) || Params->TryGetBoolField(Name, Value);
}

bool Only(const TSharedPtr<FJsonObject>& Params, const TSet<FString>& Allowed)
{
    if (!Params) return false;
    for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) return false;
    return true;
}

FString Hash(const uint8* Data, int32 Count)
{
    uint8 Digest[FSHA1::DigestSize]; FSHA1::HashBuffer(Data, Count, Digest); return BytesToHex(Digest, FSHA1::DigestSize);
}

TSharedPtr<FJsonObject> Failure(const FString& Error)
{
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), false);
    Result->SetNumberField(TEXT("data_table_contract"), 1); Result->SetStringField(TEXT("stage"), TEXT("preflight"));
    Result->SetStringField(TEXT("error"), Error); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
    return Result;
}

bool ExactPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/")) && Path.Len() < NAME_SIZE && !Path.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(Path);
}

bool RowType(const UScriptStruct* Struct)
{
    return Struct && Struct != FTableRowBase::StaticStruct() && Struct->IsChildOf(FTableRowBase::StaticStruct()) &&
        Struct->GetOutermost()->GetName().StartsWith(TEXT("/Script/")) && (Struct->StructFlags & STRUCT_Native) &&
        !(Struct->StructFlags & STRUCT_SerializeNative);
}

TSharedPtr<FJsonValue> Encode(FProperty* Property, const void* Address, FString& Error, int32& Budget, int32 Depth = 0)
{
    if (--Budget < 0 || Depth > 8 || Property->ArrayDim != 1 || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_InstancedReference | CPF_ContainsInstancedReference))
    { Error = TEXT("Unsupported property flags/static array or traversal bounds"); return MakeShared<FJsonValueNull>(); }
    if (const auto* Text = CastField<FTextProperty>(Property))
    {
        const FText Value = Text->GetPropertyValue(Address); auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("display"), Value.ToString()); FString Exported; FTextStringHelper::WriteToBuffer(Exported, Value);
        Result->SetStringField(TEXT("history"), Exported);
        const FString* Source = FTextInspector::GetSourceString(Value); const auto Namespace = FTextInspector::GetNamespace(Value); const auto Key = FTextInspector::GetKey(Value);
        if (Source) Result->SetStringField(TEXT("source"), *Source); else Result->SetField(TEXT("source"), MakeShared<FJsonValueNull>());
        if (Namespace.IsSet()) Result->SetStringField(TEXT("namespace"), Namespace.GetValue()); else Result->SetField(TEXT("namespace"), MakeShared<FJsonValueNull>());
        if (Key.IsSet()) Result->SetStringField(TEXT("key"), Key.GetValue()); else Result->SetField(TEXT("key"), MakeShared<FJsonValueNull>());
        FName TableId; FString TableKey;
        if (FTextInspector::GetTableIdAndKey(Value, TableId, TableKey)) { Result->SetStringField(TEXT("string_table_id"), TableId.ToString()); Result->SetStringField(TEXT("string_table_key"), TableKey); }
        return MakeShared<FJsonValueObject>(Result);
    }
    if (const auto* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Address); TArray<TSharedPtr<FJsonValue>> Values;
        if (Helper.Num() > 1024) { Error = TEXT("Array exceeds 1024 values"); return MakeShared<FJsonValueNull>(); }
        for (int32 Index = 0; Index < Helper.Num() && Error.IsEmpty(); ++Index) Values.Add(Encode(Array->Inner, Helper.GetRawPtr(Index), Error, Budget, Depth + 1));
        return MakeShared<FJsonValueArray>(Values);
    }
    if (const auto* Struct = CastField<FStructProperty>(Property))
    {
        auto Result = MakeShared<FJsonObject>();
        if (Struct->Struct->StructFlags & STRUCT_SerializeNative) return UnrealMCPAssetProperties::EncodeValue(Property, Address, Error);
        for (TFieldIterator<FProperty> Field(Struct->Struct); Field && Error.IsEmpty(); ++Field)
            Result->SetField(Field->GetName(), Encode(*Field, Field->ContainerPtrToValuePtr<void>(Address), Error, Budget, Depth + 1));
        return MakeShared<FJsonValueObject>(Result);
    }
    if (CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property)) { Error = TEXT("Map/Set row values are outside this finite table contract"); return MakeShared<FJsonValueNull>(); }
    auto Value = UnrealMCPAssetProperties::EncodeValue(Property, Address, Error);
    if (const auto* String = CastField<FStrProperty>(Property)) if (String->GetPropertyValue(Address).Len() > 65536) Error = TEXT("String exceeds 65536 characters");
    return Value.IsValid() ? Value : MakeShared<FJsonValueNull>();
}

TSharedPtr<FJsonObject> Snapshot(UDataTable* Table)
{
    auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("data_table_contract"), 1);
    Result->SetStringField(TEXT("asset_path"), Table->GetPackage()->GetName());
    Result->SetStringField(TEXT("row_struct_path"), Table->GetRowStruct() ? Table->GetRowStruct()->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("package_dirty"), Table->GetPackage()->IsDirty());
    TArray<FName> Names; Table->GetRowMap().GetKeys(Names); Names.Sort(FNameLexicalLess());
    TArray<TSharedPtr<FJsonValue>> Rows, Unsupported, Fields, Sources;
    bool Complete = RowType(Table->GetRowStruct()) && Names.Num() <= MaxRows; int32 Budget = 65536;
    if (!Complete) Unsupported.Add(MakeShared<FJsonValueString>(TEXT("Require supported compiled native row struct and at most 4096 rows")));
    TArray<uint8> Bytes; FBoundedWriter Memory(Bytes); FObjectAndNameAsStringProxyArchive Archive(Memory, false);
    FString Identity = Table->GetPathName() + TEXT("|") + Result->GetStringField(TEXT("row_struct_path")); Archive << Identity;
    FString ImportKey = Table->ImportKeyField; Archive << ImportKey;
    for (TFieldIterator<FProperty> Property(Table->GetClass()); Property; ++Property)
        if (!Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) Property->SerializeItem(FStructuredArchiveFromArchive(Archive).GetSlot(), Property->ContainerPtrToValuePtr<void>(Table), nullptr);
    if (Table->AssetImportData) { TArray<FString> Files; Table->AssetImportData->ExtractFilenames(Files); for (const auto& File : Files) Sources.Add(MakeShared<FJsonValueString>(File)); }
    Result->SetArrayField(TEXT("import_sources"), Sources); Result->SetStringField(TEXT("import_key_field"), ImportKey);
    if (Table->GetRowStruct())
        for (TFieldIterator<FProperty> Field(Table->GetRowStruct()); Field; ++Field)
        {
            auto Schema = UnrealMCPAssetProperties::DescribeValue(*Field); Schema->SetStringField(TEXT("name"), Field->GetName()); Fields.Add(MakeShared<FJsonValueObject>(Schema));
        }
    const int32 RowsStart = Bytes.Num();
    if (Complete)
        for (FName Name : Names)
        {
            const uint8* Data = Table->FindRowUnchecked(Name); auto Row = MakeShared<FJsonObject>(); auto Values = MakeShared<FJsonObject>();
            if (!Data) { Complete = false; Unsupported.Add(MakeShared<FJsonValueString>(TEXT("Null row memory"))); break; }
            Row->SetStringField(TEXT("row_name"), Name.ToString()); FString RowName = Name.ToString(); Archive << RowName;
            Table->RowStruct->SerializeItem(FStructuredArchiveFromArchive(Archive).GetSlot(), const_cast<uint8*>(Data), nullptr);
            for (TFieldIterator<FProperty> Field(Table->GetRowStruct()); Field; ++Field)
            {
                FString Error; auto Value = Encode(*Field, Field->ContainerPtrToValuePtr<void>(Data), Error, Budget);
                Values->SetField(Field->GetName(), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
                if (!Error.IsEmpty()) { Complete = false; if (Unsupported.Num() < 128) Unsupported.Add(MakeShared<FJsonValueString>(RowName + TEXT(".") + Field->GetName() + TEXT(": ") + Error)); }
            }
            Row->SetObjectField(TEXT("values"), Values); Rows.Add(MakeShared<FJsonValueObject>(Row));
                if (Archive.IsError() || Memory.IsError() || Budget < 0) { Complete = false; Unsupported.Add(MakeShared<FJsonValueString>(TEXT("Whole-table binary/value budget exceeded"))); break; }
        }
            Result->SetStringField(TEXT("rows_hash"), Hash(Bytes.GetData() + RowsStart, Bytes.Num() - RowsStart));
    Result->SetBoolField(TEXT("complete"), Complete); Result->SetArrayField(TEXT("unsupported"), Unsupported);
    Result->SetArrayField(TEXT("fields"), Fields); Result->SetArrayField(TEXT("rows"), Rows); Result->SetNumberField(TEXT("row_count"), Names.Num());
    if (Complete)
    {
        FString File; FString Disk = FPackageName::DoesPackageExist(Table->GetPackage()->GetName(), &File) ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("unsaved"); Archive << Disk;
        for (const auto& Source : Sources) { FString Value = Source->AsString(); Archive << Value; }
        FString Schema; auto Writer = TJsonWriterFactory<>::Create(&Schema); FJsonSerializer::Serialize(Fields, Writer); Archive << Schema;
        if (Archive.IsError() || Memory.IsError()) { Result->SetBoolField(TEXT("complete"), false); Result->SetField(TEXT("baseline"), MakeShared<FJsonValueNull>()); }
        else Result->SetStringField(TEXT("baseline"), Hash(Bytes.GetData(), Bytes.Num()));
    }
    else Result->SetField(TEXT("baseline"), MakeShared<FJsonValueNull>());
    return Result;
}

UDataTable* Load(const TSharedPtr<FJsonObject>& Params, FString& Error)
{
    FString Path;
    if (!Params || !Params->TryGetStringField(TEXT("asset_path"), Path) || !ExactPath(Path)) { Error = TEXT("Require exact /Game DataTable package path"); return nullptr; }
    if (!UEditorAssetLibrary::DoesAssetExist(Path)) { Error = TEXT("DataTable not found"); return nullptr; }
    auto* Table = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(Path));
    if (!Table || Table->GetClass() != UDataTable::StaticClass() || Table->GetPackage()->GetName() != Path) { Error = TEXT("Require exact ordinary UDataTable; no redirector/composite/mirror fallback"); return nullptr; }
    return Table;
}

void CopyRows(UDataTable* Target, const UDataTable* Source)
{
    Target->EmptyTable();
    for (const auto& Pair : Source->GetRowMap()) Target->AddRow(Pair.Key, Pair.Value, Source->GetRowStruct());
}

bool ValidateField(FProperty* Property, void* Address, FString& Error, int32& Budget, int32 Depth = 0)
{
    const auto Encoded = Encode(Property, Address, Error, Budget, Depth);
    if (!Error.IsEmpty()) return false;
    if (CastField<FTextProperty>(Property)) return true;
    if (auto* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Address);
        for (int32 Index = 0; Index < Helper.Num(); ++Index) if (!ValidateField(Array->Inner, Helper.GetRawPtr(Index), Error, Budget, Depth + 1)) return false;
        return true;
    }
    if (auto* Struct = CastField<FStructProperty>(Property))
    {
        if (!(Struct->Struct->StructFlags & STRUCT_SerializeNative))
        {
            for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
                if (!ValidateField(*Field, Field->ContainerPtrToValuePtr<void>(Address), Error, Budget, Depth + 1)) return false;
            return true;
        }
    }
    void* Copy = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()); Property->InitializeValue(Copy); Property->CopyCompleteValue(Copy, Address);
    const bool Valid = UnrealMCPAssetProperties::DecodeValue(Property, Copy, Encoded, Error);
    Property->DestroyValue(Copy); FMemory::Free(Copy); return Valid;
}

bool ValidateTable(UDataTable* Table, FString& Error)
{
    int32 Budget = 65536;
    for (const auto& Pair : Table->GetRowMap())
    {
        for (TFieldIterator<FProperty> Field(Table->GetRowStruct()); Field; ++Field)
            if (!ValidateField(*Field, Field->ContainerPtrToValuePtr<void>(Pair.Value), Error, Budget)) { Error = Pair.Key.ToString() + TEXT(".") + Field->GetName() + TEXT(": ") + Error; return false; }
        FDataValidationContext Context;
        const auto Status = reinterpret_cast<const FTableRowBase*>(Pair.Value)->IsDataValid(Context);
        if (Status == EDataValidationResult::Invalid || Context.GetNumErrors() > 0) { Error = TEXT("Native row validation failed: ") + Pair.Key.ToString(); return false; }
    }
    return true;
}

struct FImportDocument
{
    FString Format, Text;
    TMap<FName, TSet<FName>> Rows;
};

UDataTable* NewStage(UScriptStruct* Struct, const UDataTable* Original)
{
    auto* Package = CreatePackage(*(TEXT("/Temp/MCPDataTable_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))); Package->SetFlags(RF_Transient);
    TextNamespaceUtil::ForcePackageNamespace(Package, Original ? TextNamespaceUtil::GetPackageNamespace(Original->GetPackage()) : FGuid::NewGuid().ToString(EGuidFormats::Digits));
    auto* Table = NewObject<UDataTable>(Package, NAME_None, RF_Transient); Table->RowStruct = Struct; return Table;
}

bool RehomeText(FProperty* Property, void* Address, UPackage* Package, FString& Error, int32 Depth = 0)
{
    if (Depth > 8) { Error = TEXT("Text ownership traversal exceeds bounds"); return false; }
    if (auto* Text = CastField<FTextProperty>(Property))
    {
        const FText Value = Text->GetPropertyValue(Address);
        if (!Value.IsEmpty() && !Value.IsCultureInvariant() && !Value.IsFromStringTable())
        {
            if (TextNamespaceUtil::GetPackageNamespace(Package).IsEmpty()) { Error = TEXT("Target needs a stable package text namespace; save through the editor before import"); return false; }
            Text->SetPropertyValue(Address, TextNamespaceUtil::CopyTextToPackage(Value, Package, TextNamespaceUtil::ETextCopyMethod::PreserveKey, true));
        }
    }
    else if (auto* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Address); if (Helper.Num() > 1024) { Error = TEXT("Text array exceeds bounds"); return false; }
        for (int32 Index = 0; Index < Helper.Num(); ++Index) if (!RehomeText(Array->Inner, Helper.GetRawPtr(Index), Package, Error, Depth + 1)) return false;
    }
    else if (auto* Struct = CastField<FStructProperty>(Property))
        for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
            if (!RehomeText(*Field, Field->ContainerPtrToValuePtr<void>(Address), Package, Error, Depth + 1)) return false;
    return true;
}

bool ExactJsonInteger(const FString& Token, int64& Value, FString& Reason)
{
    constexpr int64 MaxExactInteger = 9007199254740991LL;
    const auto IsDigit = [](TCHAR Character) { return Character >= TEXT('0') && Character <= TEXT('9'); };
    const bool Negative = Token.StartsWith(TEXT("-"));
    int32 Index = Negative || Token.StartsWith(TEXT("+")) ? 1 : 0;
    FString Digits;
    int32 FractionDigits = 0;
    bool Fraction = false, HasDigit = false;
    for (; Index < Token.Len(); ++Index)
    {
        const TCHAR Character = Token[Index];
        if (Character == TEXT('.') && !Fraction) { Fraction = true; continue; }
        if (!IsDigit(Character)) break;
        HasDigit = true;
        if (!Digits.IsEmpty() || Character != TEXT('0')) Digits.AppendChar(Character);
        if (Fraction) ++FractionDigits;
    }
    Reason = TEXT("expected an exact integer without rounding");
    if (!HasDigit) return false;

    int32 Exponent = 0;
    if (Index < Token.Len() && (Token[Index] == TEXT('e') || Token[Index] == TEXT('E')))
    {
        ++Index;
        const bool NegativeExponent = Index < Token.Len() && Token[Index] == TEXT('-');
        if (Index < Token.Len() && (NegativeExponent || Token[Index] == TEXT('+'))) ++Index;
        if (Index == Token.Len() || !IsDigit(Token[Index])) return false;
        const int32 ExponentLimit = Token.Len() + 16;
        for (; Index < Token.Len() && IsDigit(Token[Index]); ++Index)
            Exponent = FMath::Min(ExponentLimit, Exponent * 10 + Token[Index] - TEXT('0'));
        if (NegativeExponent) Exponent = -Exponent;
    }
    if (Index != Token.Len()) return false;
    if (Digits.IsEmpty()) { Value = 0; return true; }

    int32 Scale = Exponent - FractionDigits;
    int32 SignificantDigits = Digits.Len();
    while (Scale < 0 && SignificantDigits > 0 && Digits[SignificantDigits - 1] == TEXT('0'))
    { --SignificantDigits; ++Scale; }
    if (Scale < 0) return false;
    Reason = TEXT("integer exceeds the exact JSON range (+/-9007199254740991)");
    if (SignificantDigits + Scale > 16) return false;
    int64 Magnitude = 0;
    for (int32 Digit = 0; Digit < SignificantDigits; ++Digit) Magnitude = Magnitude * 10 + Digits[Digit] - TEXT('0');
    for (int32 Zero = 0; Zero < Scale; ++Zero) Magnitude *= 10;
    if (Magnitude > MaxExactInteger) return false;
    Value = Negative ? -Magnitude : Magnitude;
    return true;
}

bool ValidateJsonIntegers(const FJsonObject& Object, const UStruct* Struct, const FString& Path, FString& Error);

bool ValidateJsonIntegerValue(const FJsonValue& Value, FProperty* Property, const FString& Path, FString& Error, bool StaticElement = false)
{
    if (const auto* Array = CastField<FArrayProperty>(Property); Array || (Property->ArrayDim > 1 && !StaticElement))
    {
        const TArray<TSharedPtr<FJsonValue>>* Elements;
        if (Value.TryGetArray(Elements))
            for (int32 Index = 0; Index < Elements->Num(); ++Index)
                if (!ValidateJsonIntegerValue(*(*Elements)[Index], Array ? Array->Inner : Property,
                    FString::Printf(TEXT("%s[%d]"), *Path, Index), Error, !Array)) return false;
        return true;
    }
    if (const auto* Struct = CastField<FStructProperty>(Property))
    {
        const TSharedPtr<FJsonObject>* Object;
        return !Value.TryGetObject(Object) || ValidateJsonIntegers(**Object, Struct->Struct, Path, Error);
    }

    const auto* Enum = CastField<FEnumProperty>(Property);
    const auto* Number = Enum ? Enum->GetUnderlyingProperty() : CastField<FNumericProperty>(Property);
    if (!Number || !Number->IsInteger() || (Value.Type == EJson::String && (Enum || Number->IsEnum()))) return true;

    FString Token, Reason;
    int64 Exact = 0;
    if ((Value.Type != EJson::Number && Value.Type != EJson::String) || !Value.TryGetString(Token)) Reason = TEXT("expected an exact integer");
    else if (ExactJsonInteger(Token, Exact, Reason))
    {
        if (!Number->CanHoldValue(Exact)) Reason = TEXT("integer is out of range for ") + Number->GetCPPType();
        else if (Value.Type == EJson::String)
        {
            int64 NativeInteger = 0;
            if (Value.TryGetNumber(NativeInteger) && NativeInteger == Exact) return true;
            Reason = TEXT("native JSON string conversion would change the integer; use a JSON number or a lossless integer string");
        }
        else if (FPlatformString::Atod(*Token) != static_cast<double>(Exact)) Reason = TEXT("native JSON conversion would change the integer");
        else return true;
    }
    Error = Path + TEXT(": ") + Reason;
    return false;
}

bool ValidateJsonIntegers(const FJsonObject& Object, const UStruct* Struct, const FString& Path, FString& Error)
{
    TArray<FString> ImportNames;
    for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
    {
        DataTableUtils::GetPropertyImportNames(*Field, ImportNames);
        for (const FString& Name : ImportNames)
        {
            const auto Value = Object.TryGetField(Name);
            if (!Value.IsValid()) continue;
            if (!ValidateJsonIntegerValue(*Value, *Field, Path + TEXT(".") + Field->GetName(), Error)) return false;
            break;
        }
    }
    return true;
}

bool Document(const TSharedPtr<FJsonObject>& Source, UDataTable* Stage, FImportDocument& Out, bool Missing, FString& Error)
{
    if (!Only(Source, {TEXT("format"), TEXT("text")}) || !Source->TryGetStringField(TEXT("format"), Out.Format) || !Source->TryGetStringField(TEXT("text"), Out.Text) || Out.Text.Len() > 262144)
    { Error = TEXT("source must contain only format csv/json and bounded text"); return false; }
    for (TCHAR Character : Out.Text) if (Character == 0) { Error = TEXT("Source contains an embedded null character"); return false; }
    const FString Key = Stage->ImportKeyField.IsEmpty() ? TEXT("Name") : Stage->ImportKeyField;
    TMap<FString, FName> Fields;
    for (TFieldIterator<FProperty> Field(Stage->GetRowStruct()); Field; ++Field) Fields.Add(Field->GetName(), Field->GetFName());
    auto Add = [&](const FString& Name, const TArray<FString>& Columns)
    {
        if (Name.IsEmpty() || Name.Len() >= NAME_SIZE || DataTableUtils::MakeValidName(Name).ToString() != Name || Out.Rows.Contains(FName(*Name))) { Error = TEXT("Invalid/duplicate RowName; native name normalization is not implicit"); return false; }
        TSet<FName> Present;
        for (const auto& Column : Columns)
        {
            if (Column == Key) continue;
            const FName* Field = Fields.Find(Column);
            if (!Field || Present.Contains(*Field)) { Error = TEXT("Unknown/duplicate column: ") + Column; return false; }
            Present.Add(*Field);
        }
        if (!Missing && Present.Num() != Fields.Num()) { Error = TEXT("Missing required row columns"); return false; }
        Out.Rows.Add(FName(*Name), Present); return Out.Rows.Num() <= MaxRows;
    };
    if (Out.Format == TEXT("csv"))
    {
        bool Quoted = false, Start = true, Closed = false;
        for (int32 Index = 0; Index < Out.Text.Len(); ++Index)
        {
            const TCHAR Character = Out.Text[Index];
            if (Quoted)
            {
                if (Character == TEXT('"'))
                {
                    if (Index + 1 < Out.Text.Len() && Out.Text[Index + 1] == TEXT('"')) ++Index;
                    else { Quoted = false; Closed = true; }
                }
            }
            else if (Character == TEXT(',') || Character == TEXT('\n') || Character == TEXT('\r')) { Start = true; Closed = false; }
            else if (Character == TEXT('"')) { if (!Start || Closed) { Error = TEXT("Malformed CSV quoting"); return false; } Quoted = true; Start = false; }
            else { if (Closed) { Error = TEXT("Characters after closed CSV quote"); return false; } Start = false; }
        }
        if (Quoted) { Error = TEXT("Unclosed CSV quote"); return false; }
        const FCsvParser Parser(Out.Text); const auto& Rows = Parser.GetRows();
        if (Rows.IsEmpty() || Rows[0].IsEmpty() || Rows.Num() > MaxRows + 1) { Error = TEXT("CSV requires header and bounded rows"); return false; }
        TArray<FString> Columns; TSet<FString> Unique;
        for (const TCHAR* Cell : Rows[0]) { FString Name(Cell); if (Unique.Contains(Name)) { Error = TEXT("Duplicate CSV column"); return false; } Unique.Add(Name); Columns.Add(Name); }
        const int32 KeyIndex = Columns.IndexOfByKey(Key);
        if (KeyIndex == INDEX_NONE) { Error = TEXT("CSV must contain exact row key column: ") + Key; return false; }
        for (int32 Index = 1; Index < Rows.Num(); ++Index)
        {
            if (Rows[Index].Num() != Columns.Num()) { Error = TEXT("CSV cells must match every declared column"); return false; }
            if (!Add(Rows[Index][KeyIndex], Columns)) return false;
        }
    }
    else if (Out.Format == TEXT("json"))
    {
        auto Reader = TJsonReaderFactory<>::Create(Out.Text); EJsonNotation Notation; TArray<bool> Objects; TArray<TSet<FString>> Keys;
        while (Reader->ReadNext(Notation))
        {
            const bool End = Notation == EJsonNotation::ObjectEnd || Notation == EJsonNotation::ArrayEnd;
            if (!End && !Objects.IsEmpty() && Objects.Last())
            {
                const FString Name = Reader->GetIdentifier(); if (Keys.Last().Contains(Name)) { Error = TEXT("Duplicate JSON property"); return false; } Keys.Last().Add(Name);
            }
            if (End) { if (!Objects.IsEmpty()) { Objects.Pop(); Keys.Pop(); } }
            else if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
            { if (Objects.Num() >= 16) { Error = TEXT("JSON nesting exceeds 16 levels"); return false; } Objects.Add(Notation == EJsonNotation::ObjectStart); Keys.Add({}); }
        }
        TArray<TSharedPtr<FJsonValue>> Rows; auto JsonReader = TJsonReaderFactory<>::Create(Out.Text);
        if (!FJsonSerializer::Deserialize(JsonReader, Rows, FJsonSerializer::EFlags::StoreNumbersAsStrings) || Rows.Num() > MaxRows) { Error = TEXT("JSON must be an array of bounded native row objects"); return false; }
        for (const auto& Row : Rows)
        {
            const auto Object = Row->Type == EJson::Object ? Row->AsObject() : nullptr; FString Name;
            const auto RowKey = Object ? Object->TryGetField(Key) : nullptr;
            if (!RowKey || RowKey->Type != EJson::String || !RowKey->TryGetString(Name)) { Error = TEXT("JSON row requires string Name/key field"); return false; }
            TArray<FString> Columns; Object->Values.GetKeys(Columns); if (!Add(Name, Columns)) return false;
            if (!ValidateJsonIntegers(*Object, Stage->GetRowStruct(), Name, Error)) return false;
        }
    }
    else { Error = TEXT("source.format must be csv or json"); return false; }
    if (Out.Rows.Num() > MaxRows) { Error = TEXT("More than 4096 input rows"); return false; }
    return true;
}

UDataTable* StageImport(UDataTable* Original, UScriptStruct* Struct, const TSharedPtr<FJsonObject>& Source, bool Merge, bool Missing,
    const TSet<FName>& Delete, bool AllowDelete, FString& Error, FName NewTableName = NAME_None)
{
    auto* Stage = NewStage(Struct, Original);
    if (Original) { Stage->ImportKeyField = Original->ImportKeyField; if (Merge) CopyRows(Stage, Original); }
    FImportDocument Input;
    if (!Document(Source, Stage, Input, Missing, Error)) return nullptr;
    if (Original && !Merge && !AllowDelete)
        for (const auto& Pair : Original->GetRowMap()) if (!Input.Rows.Contains(Pair.Key)) { Error = TEXT("Replace would delete rows; allow_delete is required"); return nullptr; }
    for (FName Name : Delete)
        if (!Original || !Original->GetRowMap().Contains(Name) || Input.Rows.Contains(Name)) { Error = TEXT("Delete rows must exist and must not also be imported"); return nullptr; }
    auto* Parsed = NewObject<UDataTable>(Stage->GetPackage(), Original ? Original->GetFName() : NewTableName, RF_Transient); Parsed->RowStruct = Struct; Parsed->ImportKeyField = Stage->ImportKeyField;
    Parsed->bIgnoreExtraFields = false; Parsed->bIgnoreMissingFields = Missing; Parsed->bPreserveExistingValues = false;
    TArray<FString> Problems;
    if (!Input.Rows.IsEmpty()) Problems = Input.Format == TEXT("csv") ? Parsed->CreateTableFromCSVString(Input.Text) : Parsed->CreateTableFromJSONString(Input.Text);
    if (!Problems.IsEmpty()) { Error = FString::Join(Problems, TEXT("; ")); return nullptr; }
    for (const auto& Pair : Parsed->GetRowMap())
        for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
            if (!RehomeText(*Field, Field->ContainerPtrToValuePtr<void>(Pair.Value), Stage->GetPackage(), Error)) return nullptr;
    for (const auto& Pair : Input.Rows)
    {
        const uint8* Incoming = Parsed->FindRowUnchecked(Pair.Key);
        if (!Incoming) { Error = TEXT("Native import omitted a requested row"); return nullptr; }
        uint8* Existing = Stage->FindRowUnchecked(Pair.Key);
        if (!Existing) Stage->AddRow(Pair.Key, Incoming, Struct);
        else for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
            if (Pair.Value.Contains(Field->GetFName())) Field->CopyCompleteValue_InContainer(Existing, Incoming);
    }
    if (Stage->GetRowMap().Num() > MaxRows) { Error = TEXT("Result exceeds 4096 rows"); return nullptr; }
    if (Original && Merge)
        for (const auto& Pair : Original->GetRowMap())
        {
            const uint8* Proposed = Stage->FindRowUnchecked(Pair.Key); if (!Proposed) { Error = TEXT("Merge unexpectedly removed a row"); return nullptr; }
            const TSet<FName>* Supplied = Input.Rows.Find(Pair.Key);
            for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
                if ((!Supplied || !Supplied->Contains(Field->GetFName())) && !Field->Identical_InContainer(Pair.Value, Proposed))
                { Error = TEXT("Native import changed an unspecified row/field"); return nullptr; }
        }
    for (FName Name : Delete) Stage->RemoveRow(Name);
    if (!ValidateTable(Stage, Error)) return nullptr;
    if (!Snapshot(Stage)->GetBoolField(TEXT("complete"))) { Error = TEXT("Proposed rows are not completely supported"); return nullptr; }
    return Stage;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FailSaveForTests = false;
#endif

bool Save(UDataTable* Table, bool Requested)
{
    if (!Requested) return false;
#if WITH_DEV_AUTOMATION_TESTS
    if (FailSaveForTests) return false;
#endif
    return UEditorAssetLibrary::SaveLoadedAsset(Table, false) && !Table->GetPackage()->IsDirty() && FPackageName::DoesPackageExist(Table->GetPackage()->GetName());
}
}

TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params)
{
    if (!Only(Params, {TEXT("asset_path"), TEXT("row_names"), TEXT("offset"), TEXT("limit")})) return Failure(TEXT("Unknown/missing read arguments"));
    FString Error; UDataTable* Table = Load(Params, Error); if (!Table) return Failure(Error);
    double OffsetValue = 0, LimitValue = 128;
    if ((Params->HasField(TEXT("offset")) && !Params->TryGetNumberField(TEXT("offset"), OffsetValue)) || (Params->HasField(TEXT("limit")) && !Params->TryGetNumberField(TEXT("limit"), LimitValue)) ||
        !FMath::IsFinite(OffsetValue) || !FMath::IsFinite(LimitValue) || OffsetValue < 0 || OffsetValue > MaxRows || LimitValue < 1 || LimitValue > 256 || FMath::FloorToDouble(OffsetValue) != OffsetValue || FMath::FloorToDouble(LimitValue) != LimitValue) return Failure(TEXT("Require integer offset 0..4096 and limit 1..256"));
    const int32 Offset = static_cast<int32>(OffsetValue), Limit = static_cast<int32>(LimitValue);
    const TArray<TSharedPtr<FJsonValue>>* Requested; TSet<FName> Selection;
    if (Params->HasField(TEXT("row_names")))
    {
        if (!Params->TryGetArrayField(TEXT("row_names"), Requested) || Requested->Num() > MaxRows) return Failure(TEXT("Invalid row_names"));
        for (const auto& Value : *Requested) { FString Name; if (!Value->TryGetString(Name) || Name.IsEmpty() || Name.Len() >= NAME_SIZE || Selection.Contains(FName(*Name)) || !Table->GetRowMap().Contains(FName(*Name))) return Failure(TEXT("Unknown/duplicate/invalid RowName")); Selection.Add(FName(*Name)); }
    }
    auto Result = Snapshot(Table); TArray<TSharedPtr<FJsonValue>> Selected, Page;
    for (const auto& Row : Result->GetArrayField(TEXT("rows"))) if (Selection.IsEmpty() || Selection.Contains(FName(*Row->AsObject()->GetStringField(TEXT("row_name"))))) Selected.Add(Row);
    for (int32 Index = Offset; Index < FMath::Min(Selected.Num(), Offset + Limit); ++Index) Page.Add(Selected[Index]);
    Result->SetBoolField(TEXT("success"), true); Result->SetArrayField(TEXT("rows"), Page); Result->SetNumberField(TEXT("selected_count"), Selection.IsEmpty() ? Table->GetRowMap().Num() : Selection.Num());
    Result->SetNumberField(TEXT("returned_count"), Page.Num()); Result->SetNumberField(TEXT("offset"), Offset); Result->SetNumberField(TEXT("limit"), Limit);
    Result->SetBoolField(TEXT("has_more"), Offset + Page.Num() < Selected.Num()); Result->SetNumberField(TEXT("next_offset"), Offset + Page.Num());
    Result->SetBoolField(TEXT("selection_complete"), Result->GetBoolField(TEXT("complete")) && Offset + Page.Num() >= Selected.Num()); return Result;
}

TSharedPtr<FJsonObject> Create(const TSharedPtr<FJsonObject>& Params)
{
    if (Busy()) return Failure(TEXT("DataTable creation requires idle editor mode"));
    if (!Only(Params, {TEXT("asset_path"), TEXT("row_struct_path"), TEXT("source"), TEXT("dry_run"), TEXT("save")})) return Failure(TEXT("Unknown/missing create arguments"));
    FString Path, StructPath; bool Dry = true, Persist = false;
    if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !ExactPath(Path) || !Params->TryGetStringField(TEXT("row_struct_path"), StructPath) || StructPath.Len() >= NAME_SIZE || !StructPath.StartsWith(TEXT("/Script/"))) return Failure(TEXT("Require exact new /Game package and compiled /Script/module.row structure"));
    if (!Bool(Params, TEXT("dry_run"), Dry) || !Bool(Params, TEXT("save"), Persist)) return Failure(TEXT("Invalid boolean option"));
    if (FindPackage(nullptr, *Path) || FPackageName::DoesPackageExist(Path) || UEditorAssetLibrary::DoesAssetExist(Path)) return Failure(TEXT("Destination exists; creation never overwrites"));
    auto* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
    if (!RowType(Struct)) return Failure(TEXT("RowStruct must be an already compiled/loaded native FTableRowBase child without custom native serialization"));
    const TSharedPtr<FJsonObject>* Source; auto* Stage = NewStage(Struct, nullptr);
    if (Params->HasField(TEXT("source")))
    {
        if (!Params->TryGetObjectField(TEXT("source"), Source)) return Failure(TEXT("source must be an object"));
        if (!(*Source)->Values.IsEmpty()) { FString Error; Stage = StageImport(nullptr, Struct, *Source, false, false, {}, false, Error, FName(*FPackageName::GetLongPackageAssetName(Path))); if (!Stage) return Failure(Error); }
    }
    auto Proposed = Snapshot(Stage); if (!Proposed->GetBoolField(TEXT("complete"))) return Failure(TEXT("Unsupported row structure"));
    auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("data_table_contract"), 1); Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Path); Result->SetStringField(TEXT("row_struct_path"), StructPath); Result->SetBoolField(TEXT("dry_run"), Dry); Result->SetBoolField(TEXT("would_modify"), true);
    Result->SetBoolField(TEXT("created"), false); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetObjectField(TEXT("proposed"), Proposed);
    if (Dry) return Result;
    if (Busy() || FindPackage(nullptr, *Path) || FPackageName::DoesPackageExist(Path)) return Failure(TEXT("Editor busy or destination changed after staging"));
    UPackage* Package = CreatePackage(*Path); TextNamespaceUtil::ForcePackageNamespace(Package, TextNamespaceUtil::GetPackageNamespace(Stage->GetPackage()));
    auto* Factory = NewObject<UDataTableFactory>(); Factory->Struct = Struct;
    auto* Table = Cast<UDataTable>(Factory->FactoryCreateNew(UDataTable::StaticClass(), Package, FName(*FPackageName::GetLongPackageAssetName(Path)), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!Table) return Failure(TEXT("Native DataTable factory failed"));
    const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "CreateDataTable", "Create DataTable"));
    Table->Modify(); CopyRows(Table, Stage); FAssetRegistryModule::AssetCreated(Table); Table->MarkPackageDirty();
    Result->SetBoolField(TEXT("created"), true); Result->SetBoolField(TEXT("modified"), true);
    if (Snapshot(Table)->GetStringField(TEXT("rows_hash")) != Proposed->GetStringField(TEXT("rows_hash"))) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback")); Result->SetStringField(TEXT("error"), TEXT("Factory/copy readback changed row data; not saved")); }
    else if (Persist && !Save(Table, true)) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save")); Result->SetStringField(TEXT("error"), TEXT("Created in memory but save failed; never retry creation blindly")); }
    else Result->SetBoolField(TEXT("saved"), Persist);
    auto Final = Snapshot(Table);
    if (Result->GetBoolField(TEXT("success")) && (!Final->GetBoolField(TEXT("complete")) || Final->GetStringField(TEXT("rows_hash")) != Proposed->GetStringField(TEXT("rows_hash"))))
    { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback")); Result->SetStringField(TEXT("error"), TEXT("Post-save row verification failed; inspect actual after/disk")); }
    Result->SetObjectField(TEXT("after"), Final); Result->SetBoolField(TEXT("package_dirty"), Package->IsDirty()); return Result;
}

TSharedPtr<FJsonObject> Import(const TSharedPtr<FJsonObject>& Params)
{
    if (Busy()) return Failure(TEXT("DataTable import requires idle editor mode"));
    if (!Only(Params, {TEXT("asset_path"), TEXT("expected_baseline"), TEXT("source"), TEXT("mode"), TEXT("allow_missing_fields"), TEXT("delete_rows"), TEXT("allow_delete"), TEXT("dry_run"), TEXT("save")})) return Failure(TEXT("Unknown/missing import arguments"));
    FString Error, Expected, Mode = TEXT("merge"); bool Dry = true, Persist = false, Missing = false, AllowDelete = false;
    UDataTable* Table = Load(Params, Error); if (!Table) return Failure(Error);
    if (!RowType(Table->GetRowStruct()) || Table->GetPackage()->IsDirty()) return Failure(TEXT("Require supported native row struct and clean target; drafts are never overwritten"));
    if (!Params->TryGetStringField(TEXT("expected_baseline"), Expected) || Expected.IsEmpty() || (Params->HasField(TEXT("mode")) && !Params->TryGetStringField(TEXT("mode"), Mode)) || (Mode != TEXT("merge") && Mode != TEXT("replace"))) return Failure(TEXT("Require fresh whole-table baseline and mode merge/replace"));
    if (!Bool(Params, TEXT("dry_run"), Dry) || !Bool(Params, TEXT("save"), Persist) || !Bool(Params, TEXT("allow_missing_fields"), Missing) || !Bool(Params, TEXT("allow_delete"), AllowDelete)) return Failure(TEXT("Invalid boolean option"));
    auto Before = Snapshot(Table); if (!Before->GetBoolField(TEXT("complete")) || Before->GetStringField(TEXT("baseline")) != Expected) return Failure(TEXT("Incomplete or stale DataTable baseline"));
    const TSharedPtr<FJsonObject>* Source; if (!Params->TryGetObjectField(TEXT("source"), Source)) return Failure(TEXT("Require structured source format/text"));
    TSet<FName> Delete; const TArray<TSharedPtr<FJsonValue>>* Deletes;
    if (Params->HasField(TEXT("delete_rows")))
    {
        if (!Params->TryGetArrayField(TEXT("delete_rows"), Deletes) || Deletes->Num() > MaxRows) return Failure(TEXT("Invalid delete_rows"));
        for (const auto& Value : *Deletes) { FString Name; if (!Value->TryGetString(Name) || Name.IsEmpty() || Name.Len() >= NAME_SIZE || Delete.Contains(FName(*Name))) return Failure(TEXT("Invalid/duplicate delete RowName")); Delete.Add(FName(*Name)); }
    }
    if (!Delete.IsEmpty() && !AllowDelete) return Failure(TEXT("Explicit deletion requires allow_delete=true"));
    UDataTable* Stage = StageImport(Table, Table->RowStruct.Get(), *Source, Mode == TEXT("merge"), Missing, Delete, AllowDelete, Error); if (!Stage) return Failure(Error);
    auto Proposed = Snapshot(Stage); const FString DesiredHash = Proposed->GetStringField(TEXT("rows_hash")); const bool Changed = DesiredHash != Before->GetStringField(TEXT("rows_hash"));
    auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("data_table_contract"), 1); Result->SetBoolField(TEXT("success"), true); Result->SetStringField(TEXT("asset_path"), Table->GetPackage()->GetName());
    Result->SetBoolField(TEXT("dry_run"), Dry); Result->SetBoolField(TEXT("would_modify"), Changed); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
    Result->SetObjectField(TEXT("before"), Before); Result->SetObjectField(TEXT("proposed"), Proposed); Result->SetObjectField(TEXT("after"), Before);
    if (Dry || !Changed) return Result;
    if (Busy() || Table->GetPackage()->IsDirty() || Snapshot(Table)->GetStringField(TEXT("baseline")) != Expected) return Failure(TEXT("Target changed/became busy during staging; not modified"));
    { const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "ImportDataTable", "Import DataTable")); Table->Modify(); Table->PreEditChange(nullptr); CopyRows(Table, Stage); Table->PostEditChange(); Table->MarkPackageDirty(); }
    Result->SetBoolField(TEXT("modified"), true);
    auto After = Snapshot(Table);
    if (!After->GetBoolField(TEXT("complete")) || After->GetStringField(TEXT("rows_hash")) != DesiredHash) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback")); Result->SetStringField(TEXT("error"), TEXT("Post-edit row verification failed; memory may be modified, not saved")); }
    else if (Persist && !Save(Table, true)) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save")); Result->SetStringField(TEXT("error"), TEXT("Applied in memory but save failed; do not retry blindly")); }
    else Result->SetBoolField(TEXT("saved"), Persist);
    auto Final = Snapshot(Table);
    if (Result->GetBoolField(TEXT("success")) && (!Final->GetBoolField(TEXT("complete")) || Final->GetStringField(TEXT("rows_hash")) != DesiredHash))
    { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback")); Result->SetStringField(TEXT("error"), TEXT("Post-save row verification failed; inspect actual after/disk")); }
    Result->SetObjectField(TEXT("after"), Final); Result->SetBoolField(TEXT("package_dirty"), Table->GetPackage()->IsDirty()); return Result;
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/MCPAssetPropertyTestTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPDataTableAuthoringTest, "UnrealMCP.DataTable.Authoring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPDataTableAuthoringTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPDataTableCommands;
    const FString Root = TEXT("/Game/__Dev/DataTable_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Path = Root / TEXT("DT_Waves");
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path);
    Params->SetStringField(TEXT("row_struct_path"), FMCPDataTableTestRow::StaticStruct()->GetPathName());
    auto Source = MakeShared<FJsonObject>(); Source->SetStringField(TEXT("format"), TEXT("csv"));
    Source->SetStringField(TEXT("text"), TEXT("Name,Count,Delay,Enemy,Label\nWave1,3,0,None,Hello\nWave2,2,1,None,World\n")); Params->SetObjectField(TEXT("source"), Source);
    auto Preview = Create(Params);
    if (!TestTrue(TEXT("Creation preview"), Preview->GetBoolField(TEXT("success")))) { AddError(Preview->GetStringField(TEXT("error"))); return false; }
    TestFalse(TEXT("Preview creates no package"), FindPackage(nullptr, *Path) != nullptr);
    Params->SetBoolField(TEXT("dry_run"), false); Params->SetBoolField(TEXT("save"), true);
    auto Created = Create(Params);
    if (!TestTrue(TEXT("Create native table and save"), Created->GetBoolField(TEXT("success"))))
    {
        FString Evidence; auto Writer = TJsonWriterFactory<>::Create(&Evidence); FJsonSerializer::Serialize(Created, Writer);
        FFileHelper::SaveStringToFile(Evidence, *(FPaths::ProjectSavedDir() / TEXT("DataTableAcceptance/create-failure.json")));
        AddError(Created->GetStringField(TEXT("error"))); return false;
    }
    auto ReadParams = MakeShared<FJsonObject>(); ReadParams->SetStringField(TEXT("asset_path"), Path);
    auto Full = Read(ReadParams); TestTrue(TEXT("Read actual rows complete"), Full->GetBoolField(TEXT("complete")));
    TestEqual(TEXT("Two actual rows"), Full->GetArrayField(TEXT("rows")).Num(), 2);
    auto* Table = CastChecked<UDataTable>(UEditorAssetLibrary::LoadAsset(Path));
    TestEqual(TEXT("Runtime typed lookup"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 3);
    TestTrue(TEXT("FText identity retained in reader"), Full->GetArrayField(TEXT("rows"))[0]->AsObject()->GetObjectField(TEXT("values"))->GetObjectField(TEXT("Label"))->HasField(TEXT("history")));
    ReadParams->SetNumberField(TEXT("limit"), 1); const auto Page = Read(ReadParams);
    TestEqual(TEXT("Page bounded"), Page->GetArrayField(TEXT("rows")).Num(), 1); TestTrue(TEXT("More rows explicit"), Page->GetBoolField(TEXT("has_more")));
    TestEqual(TEXT("Same table baseline across pages"), Page->GetStringField(TEXT("baseline")), Full->GetStringField(TEXT("baseline")));
    auto Write = MakeShared<FJsonObject>(); Write->SetStringField(TEXT("asset_path"), Path); Write->SetStringField(TEXT("expected_baseline"), Full->GetStringField(TEXT("baseline")));
    auto Json = MakeShared<FJsonObject>(); Json->SetStringField(TEXT("format"), TEXT("json")); Json->SetStringField(TEXT("text"), TEXT("[{\"Name\":\"Wave1\",\"Count\":5}]"));
    Write->SetObjectField(TEXT("source"), Json); Write->SetBoolField(TEXT("allow_missing_fields"), true);
    const auto ImportPreview = Import(Write);
    if (!TestTrue(TEXT("Native JSON merge preview"), ImportPreview->GetBoolField(TEXT("success")))) { AddError(ImportPreview->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Preview leaves target count"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 3);
    Write->SetBoolField(TEXT("dry_run"), false); Write->SetBoolField(TEXT("save"), true);
    const auto Applied = Import(Write);
    if (!TestTrue(TEXT("Native JSON merge apply/save"), Applied->GetBoolField(TEXT("success")))) { AddError(Applied->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Runtime lookup sees imported count"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 5);
    TestEqual(TEXT("Unspecified row preserved"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave2"), TEXT("Test"))->Count, 2);
    TestEqual(TEXT("Unspecified text preserved"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Label.ToString(), FString(TEXT("Hello")));
    TestFalse(TEXT("Target clean after save"), Table->GetPackage()->IsDirty());
    TestFalse(TEXT("Creation never overwrites"), Create(Params)->GetBoolField(TEXT("success")));
    const FString InitialSavedBaseline = Snapshot(Table)->GetStringField(TEXT("baseline"));
    Write->SetStringField(TEXT("expected_baseline"), InitialSavedBaseline); Write->SetObjectField(TEXT("source"), Source);
    Source->SetStringField(TEXT("text"), TEXT("Name,Count,Delay,Enemy,Label\nWave1,5,0,None,Hello\nWave2,2,1,None,World\n"));
    const auto TextNoop = Import(Write); TestTrue(TEXT("Full CSV reimport succeeds"), TextNoop->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Same native text identities are no-op"), TextNoop->GetBoolField(TEXT("modified")));
    Source->SetStringField(TEXT("text"), TEXT("Name,Count,Delay,Enemy,Label\nWave1,5,0,None,\"Unclosed\n"));
    TestFalse(TEXT("Malformed CSV quoting refuses"), Import(Write)->GetBoolField(TEXT("success")));
    Write->SetObjectField(TEXT("source"), Json);
    ReadParams->SetNumberField(TEXT("limit"), 256); ReadParams->SetNumberField(TEXT("offset"), 0.5);
    TestFalse(TEXT("Fractional pagination refuses"), Read(ReadParams)->GetBoolField(TEXT("success"))); ReadParams->SetNumberField(TEXT("offset"), 0);
    const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    const auto DiskBefore = FMD5Hash::HashFile(*File);
    auto Reject = [&](const FString& Text, const FString& Label)
    {
        Write->SetStringField(TEXT("expected_baseline"), Snapshot(Table)->GetStringField(TEXT("baseline"))); Json->SetStringField(TEXT("text"), Text);
        auto Result = Import(Write); TestFalse(Label, Result->GetBoolField(TEXT("success")));
        TestFalse(TEXT("Rejected input never modifies"), Result->GetBoolField(TEXT("modified")));
        TestTrue(TEXT("Rejected input retains disk"), FMD5Hash::HashFile(*File) == DiskBefore);
        TestEqual(TEXT("Rejected input retains rows"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 5);
    };
    Reject(TEXT("[{\"Name\":\"Wave1\",\"Count\":6,\"Count\":7}]"), TEXT("Duplicate JSON property refuses"));
    Reject(TEXT("[{\"Name\":\"Wave1\",\"Count\":6},{\"Name\":\"wave1\",\"Count\":7}]"), TEXT("Duplicate native RowName refuses"));
    Reject(TEXT("[{\"Name\":\"Wave1\",\"WrongColumn\":6}]"), TEXT("Unknown field refuses"));
    Reject(TEXT("[{\"Name\":\"Wave1\",\"Count\":0}]"), TEXT("Invalid count refuses"));
    auto InvalidCreate = MakeShared<FJsonObject>(*Params);
    const FString InvalidPath = Root / TEXT("DT_InvalidInteger");
    InvalidCreate->SetStringField(TEXT("asset_path"), InvalidPath);
    InvalidCreate->SetObjectField(TEXT("source"), Json);
    Json->SetStringField(TEXT("format"), TEXT("json"));
    const auto CheckIntegerFailure = [&](const TSharedPtr<FJsonObject>& Result, const TCHAR* Reason)
    {
        TestFalse(TEXT("Bad integer rejected"), Result->GetBoolField(TEXT("success")));
        TestFalse(TEXT("Bad integer never modifies or saves"), Result->GetBoolField(TEXT("modified")) || Result->GetBoolField(TEXT("saved")));
        TestTrue(TEXT("Error identifies original row, field and reason"), Result->GetStringField(TEXT("error")).StartsWith(TEXT("Wave1.Count: ")) && Result->GetStringField(TEXT("error")).Contains(Reason));
    };
    for (const bool DryRun : {true, false})
    {
        Write->SetBoolField(TEXT("dry_run"), DryRun);
        InvalidCreate->SetBoolField(TEXT("dry_run"), DryRun);
        for (const auto& Case : {TPair<const TCHAR*, const TCHAR*>(TEXT("1.4"), TEXT("without rounding")),
            {TEXT("4294967297"), TEXT("out of range")}, {TEXT("1.00000000000000001"), TEXT("without rounding")},
            {TEXT("1e-400"), TEXT("without rounding")}, {TEXT("9007199254740993"), TEXT("exact JSON range")},
            {TEXT("\"1e2\""), TEXT("string conversion would change")}, {TEXT("\"1.2e2\""), TEXT("string conversion would change")}})
        {
            Json->SetStringField(TEXT("text"), FString::Printf(TEXT("[{\"Name\":\"Wave1\",\"Count\":%s,\"Delay\":0,\"Enemy\":\"None\",\"Label\":\"Test\"}]"), Case.Key));
            CheckIntegerFailure(Import(Write), Case.Value);
            CheckIntegerFailure(Create(InvalidCreate), Case.Value);
        }
    }
    TestFalse(TEXT("Invalid creation leaves no package"), FindPackage(nullptr, *InvalidPath) != nullptr || FPackageName::DoesPackageExist(InvalidPath));
    TestFalse(TEXT("Integer rejection preserves clean target"), Table->GetPackage()->IsDirty());
    TestTrue(TEXT("Integer rejection preserves target disk bytes"), FMD5Hash::HashFile(*File) == DiskBefore);
    TestEqual(TEXT("Integer rejection preserves all existing values"), Snapshot(Table)->GetStringField(TEXT("baseline")), InitialSavedBaseline);

    auto NumericCases = MakeShared<FJsonObject>();
    FString IntegerError;
    NumericCases->SetField(TEXT("Byte"), MakeShared<FJsonValueNumberString>(FString(TEXT("255"))));
    TestTrue(TEXT("uint8 max accepted"), ValidateJsonIntegers(*NumericCases, UMCPAssetPropertyTestAsset::StaticClass(), TEXT("Bounds"), IntegerError));
    for (const TCHAR* InvalidByte : {TEXT("256"), TEXT("-1")})
    {
        NumericCases->SetField(TEXT("Byte"), MakeShared<FJsonValueNumberString>(FString(InvalidByte)));
        TestFalse(TEXT("Unsigned bounds checked before conversion"), ValidateJsonIntegers(*NumericCases, UMCPAssetPropertyTestAsset::StaticClass(), TEXT("Bounds"), IntegerError));
        TestTrue(TEXT("Unsigned error identifies field"), IntegerError.StartsWith(TEXT("Bounds.Byte: ")));
    }
    NumericCases->RemoveField(TEXT("Byte"));
    auto Nested = MakeShared<FJsonObject>(); Nested->SetField(TEXT("Health"), MakeShared<FJsonValueNumberString>(FString(TEXT("1.25"))));
    NumericCases->SetArrayField(TEXT("Attacks"), {MakeShared<FJsonValueObject>(Nested)});
    TestFalse(TEXT("Nested array integer rejects fractional raw input"), ValidateJsonIntegers(*NumericCases, UMCPAssetPropertyTestAsset::StaticClass(), TEXT("Bounds"), IntegerError));
    TestTrue(TEXT("Nested error identifies indexed field"), IntegerError.StartsWith(TEXT("Bounds.Attacks[0].Health: ")));
    int64 Exact = 0;
    TestTrue(TEXT("Upper exact JSON integer preserved"), ExactJsonInteger(TEXT("9007199254740991"), Exact, IntegerError));
    TestEqual(TEXT("Upper boundary value"), Exact, 9007199254740991LL);
    TestFalse(TEXT("Beyond exact JSON integer range refused"), ExactJsonInteger(TEXT("9007199254740992"), Exact, IntegerError));
    Write->SetBoolField(TEXT("dry_run"), false);
    Reject(TEXT("[{\"Name\":\"Wave1\",\"Enemy\":\"/Script/Engine.Texture2D\"}]"), TEXT("Wrong soft class constraint refuses"));
    Write->SetBoolField(TEXT("allow_missing_fields"), false);
    Reject(TEXT("[{\"Name\":\"Wave1\",\"Count\":6}]"), TEXT("Missing required field refuses")); Write->SetBoolField(TEXT("allow_missing_fields"), true);
    Json->SetStringField(TEXT("text"), TEXT("[]")); Write->SetStringField(TEXT("mode"), TEXT("replace"));
    TestFalse(TEXT("Replace deletion needs explicit permission"), Import(Write)->GetBoolField(TEXT("success"))); Write->SetStringField(TEXT("mode"), TEXT("merge"));
    Json->SetStringField(TEXT("text"), TEXT("[{\"Name\":\"Wave1\",\"Count\":\"8\"}]")); Write->SetBoolField(TEXT("save"), false);
    const auto UndoEdit = Import(Write); TestTrue(TEXT("Draft import applies"), UndoEdit->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Draft runtime lookup"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 8);
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo restores row"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 5);
    GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores edited row"), Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Count, 8);
    TestFalse(TEXT("Dirty target refuses further import"), Import(Write)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Explicit fixture save"), UEditorAssetLibrary::SaveLoadedAsset(Table, false));
    TestFalse(TEXT("Old baseline refuses"), Import(Write)->GetBoolField(TEXT("success")));
    Write->SetStringField(TEXT("expected_baseline"), Snapshot(Table)->GetStringField(TEXT("baseline"))); Write->SetBoolField(TEXT("save"), true);
    const auto Noop = Import(Write); TestFalse(TEXT("Same native row no-op"), Noop->GetBoolField(TEXT("modified"))); TestFalse(TEXT("No-op never saves"), Noop->GetBoolField(TEXT("saved")));
    const auto SavedHash = FMD5Hash::HashFile(*File); Json->SetStringField(TEXT("text"), TEXT("[{\"Name\":\"Wave1\",\"Count\":9}]"));
    FailSaveForTests = true; const auto FailedSave = Import(Write); FailSaveForTests = false;
    TestFalse(TEXT("Save failure truthful"), FailedSave->GetBoolField(TEXT("success"))); TestTrue(TEXT("Save failure retains modified memory"), FailedSave->GetBoolField(TEXT("modified")));
    TestFalse(TEXT("Save failure not saved"), FailedSave->GetBoolField(TEXT("saved"))); TestTrue(TEXT("Save failure disk unchanged"), SavedHash == FMD5Hash::HashFile(*File));
    TestTrue(TEXT("Recovery fixture save"), UEditorAssetLibrary::SaveLoadedAsset(Table, false));
    Table->FindRow<FMCPDataTableTestRow>(TEXT("Wave1"), TEXT("Test"))->Label = NSLOCTEXT("MCPDataTableTest", "Wave1", "Shared display"); Table->MarkPackageDirty();
    TestTrue(TEXT("Save true FText identity fixture"), UEditorAssetLibrary::SaveLoadedAsset(Table, false));
    Write->SetStringField(TEXT("expected_baseline"), Snapshot(Table)->GetStringField(TEXT("baseline"))); Json->SetStringField(TEXT("text"), TEXT("[]"));
    Write->SetArrayField(TEXT("delete_rows"), {MakeShared<FJsonValueString>(TEXT("Wave2"))});
    TestFalse(TEXT("Explicit delete needs permission"), Import(Write)->GetBoolField(TEXT("success"))); Write->SetBoolField(TEXT("allow_delete"), true);
    TestTrue(TEXT("Explicit delete applies"), Import(Write)->GetBoolField(TEXT("success"))); TestEqual(TEXT("Only named row removed"), Table->GetRowMap().Num(), 1);
    const FString SpawnsPath = Root / TEXT("DT_Spawns"); Params->SetStringField(TEXT("asset_path"), SpawnsPath);
    Source->SetStringField(TEXT("text"), TEXT("Name,Count,Delay,Enemy,Label\nSpawn1,4,2,/Script/Engine.Actor,Spawn\n"));
    const auto Second = Create(Params); TestTrue(TEXT("Second table with native soft class"), Second->GetBoolField(TEXT("success")));
    auto* Spawns = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(SpawnsPath));
    if (!TestNotNull(TEXT("Second table loaded"), Spawns)) return false;
    TestEqual(TEXT("Soft class type retained"), Spawns->FindRow<FMCPDataTableTestRow>(TEXT("Spawn1"), TEXT("Test"))->Enemy.Get(), AActor::StaticClass());
    Write->SetStringField(TEXT("asset_path"), SpawnsPath);
    Write->SetStringField(TEXT("expected_baseline"), Snapshot(Spawns)->GetStringField(TEXT("baseline")));
    Write->RemoveField(TEXT("delete_rows"));
    Json->SetStringField(TEXT("text"), TEXT("[{\"Name\":\"Spawn1\",\"Count\":2.147483647e9,\"Delay\":2.25}]"));
    Write->SetBoolField(TEXT("dry_run"), true);
    const auto UpperPreview = Import(Write);
    if (!TestTrue(TEXT("Exact int32 upper bound and fractional float preview"), UpperPreview->GetBoolField(TEXT("success")))) { AddError(UpperPreview->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Preview retains original runtime value"), Spawns->FindRow<FMCPDataTableTestRow>(TEXT("Spawn1"), TEXT("Test"))->Count, 4);
    const auto UpperValues = UpperPreview->GetObjectField(TEXT("proposed"))->GetArrayField(TEXT("rows"))[0]->AsObject()->GetObjectField(TEXT("values"));
    TestEqual(TEXT("Preview reports exact upper bound"), UpperValues->GetNumberField(TEXT("Count")), static_cast<double>(MAX_int32));
    Write->SetBoolField(TEXT("dry_run"), false);
    const auto UpperApplied = Import(Write);
    if (!TestTrue(TEXT("Exact int32 upper bound applies and saves"), UpperApplied->GetBoolField(TEXT("success")) && UpperApplied->GetBoolField(TEXT("saved")))) { AddError(UpperApplied->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Runtime int32 upper bound"), Spawns->FindRow<FMCPDataTableTestRow>(TEXT("Spawn1"), TEXT("Test"))->Count, MAX_int32);
    TestEqual(TEXT("Fractional float unchanged"), Spawns->FindRow<FMCPDataTableTestRow>(TEXT("Spawn1"), TEXT("Test"))->Delay, 2.25f);
    auto Oracle = MakeShared<FJsonObject>(); Oracle->SetObjectField(TEXT("waves"), Snapshot(Table)); Oracle->SetObjectField(TEXT("spawns"), Snapshot(Spawns));
    FString Serialized; auto Writer = TJsonWriterFactory<>::Create(&Serialized); FJsonSerializer::Serialize(Oracle, Writer);
    const FString OracleFile = FPaths::ProjectSavedDir() / TEXT("DataTableAcceptance") / (FPackageName::GetShortName(Root) + TEXT(".json"));
    TestTrue(TEXT("Cold oracle saved"), FFileHelper::SaveStringToFile(Serialized, *OracleFile));
    AddInfo(TEXT("DataTable fixture: ") + Root); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPDataTableColdTest, "UnrealMCP.DataTable.Cold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPDataTableColdTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPDataTableCommands;
    FString Root; if (!FParse::Value(FCommandLine::Get(), TEXT("MCPDataTableReload="), Root) || !Root.StartsWith(TEXT("/Game/__Dev/DataTable_"))) { AddWarning(TEXT("Explicit owned reload root required; skipped")); return true; }
    FString Text; if (!TestTrue(TEXT("Stored oracle exists"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("DataTableAcceptance") / (FPackageName::GetShortName(Root) + TEXT(".json")))))) return false;
    TSharedPtr<FJsonObject> Oracle; auto Reader = TJsonReaderFactory<>::Create(Text); if (!TestTrue(TEXT("Oracle parses"), FJsonSerializer::Deserialize(Reader, Oracle))) return false;
    for (const auto& Pair : TArray<TPair<FString, FString>>{{TEXT("waves"), TEXT("DT_Waves")}, {TEXT("spawns"), TEXT("DT_Spawns")}})
    {
        auto* Table = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(Root / Pair.Value)); if (!TestNotNull(TEXT("Independent table cold load"), Table)) return false;
        const auto Actual = Snapshot(Table); TestTrue(TEXT("Cold rows complete"), Actual->GetBoolField(TEXT("complete")));
        TestEqual(TEXT("Cold baseline exact"), Actual->GetStringField(TEXT("baseline")), Oracle->GetObjectField(Pair.Key)->GetStringField(TEXT("baseline")));
        TestEqual(TEXT("Cold row bytes exact"), Actual->GetStringField(TEXT("rows_hash")), Oracle->GetObjectField(Pair.Key)->GetStringField(TEXT("rows_hash")));
        TestFalse(TEXT("Cold load did not dirty"), Table->GetPackage()->IsDirty());
        const auto* Row = Table->FindRow<FMCPDataTableTestRow>(Pair.Key == TEXT("waves") ? TEXT("Wave1") : TEXT("Spawn1"), TEXT("ColdTest"));
        const auto ExpectedValues = Oracle->GetObjectField(Pair.Key)->GetArrayField(TEXT("rows"))[0]->AsObject()->GetObjectField(TEXT("values"));
        TestEqual(TEXT("Runtime cold count"), Row->Count, static_cast<int32>(ExpectedValues->GetNumberField(TEXT("Count"))));
        TestEqual(TEXT("Runtime cold delay"), Row->Delay, static_cast<float>(ExpectedValues->GetNumberField(TEXT("Delay"))));
        if (Pair.Key == TEXT("waves")) TestEqual(TEXT("FText namespace survives"), TextNamespaceUtil::StripPackageNamespace(FTextInspector::GetNamespace(Row->Label).GetValue()), FString(TEXT("MCPDataTableTest")));
        else TestEqual(TEXT("Soft class cold lookup"), Row->Enemy.LoadSynchronous(), AActor::StaticClass());
    }
    return !HasAnyErrors();
}
#endif