#include "Commands/UnrealMCPAssetMigration.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraDataInterface.h"
#include "NiagaraScript.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace UnrealMCPAssetMigration
{
namespace
{
    constexpr int32 MaxPackages = 512;

    struct FPlan
    {
        TMap<FString, FString> Copies;
        TMap<FString, FString> Hashes;
        TMap<FString, FString> Namespaces;
        TMap<FString, FString> CollectionNamespaces;
        TArray<FString> Rebind;
        TSharedPtr<FJsonObject> Receipt;
        bool Started = false;
    };

    TMap<FString, TSharedPtr<FPlan>> Plans;

    TSharedPtr<FJsonObject> Failure(const FString& Message)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), Message);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FString& Value : Values) Result.Add(MakeShared<FJsonValueString>(Value));
        return Result;
    }

    bool ReadStrings(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, TArray<FString>& Values, bool Required)
    {
        if (!Params->HasField(Field)) return !Required;
        const TArray<TSharedPtr<FJsonValue>>* Items;
        if (!Params->TryGetArrayField(Field, Items) || Items->Num() > MaxPackages) return false;
        for (const auto& Item : *Items)
        {
            FString Value;
            if (!Item->TryGetString(Value) || Values.Contains(Value)) return false;
            Values.Add(Value);
        }
        return !Required || !Values.IsEmpty();
    }

    bool GamePath(const FString& Path)
    {
        return Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive) && !Path.Contains(TEXT("."))
            && !Path.Contains(TEXT("//")) && !Path.EndsWith(TEXT("/")) && FPackageName::IsValidLongPackageName(Path);
    }

    bool Under(const FString& Path, const FString& Prefix)
    {
        return Path == Prefix || Path.StartsWith(Prefix + TEXT("/"), ESearchCase::CaseSensitive);
    }

    FString FileHash(const FString& Package)
    {
        FString Filename;
        return FPackageName::DoesPackageExist(Package, &Filename) ? LexToString(FMD5Hash::HashFile(*Filename)) : FString();
    }

    bool SupportedAsset(UObject* Asset)
    {
        static const TSet<FName> Classes = {TEXT("NiagaraSystem"), TEXT("NiagaraEmitter"), TEXT("NiagaraScript"),
            TEXT("NiagaraEffectType"), TEXT("NiagaraParameterCollection"), TEXT("NiagaraParameterCollectionInstance"),
            TEXT("Material"), TEXT("MaterialInstanceConstant"), TEXT("MaterialFunction"), TEXT("MaterialFunctionInstance"),
            TEXT("MaterialParameterCollection"), TEXT("StaticMesh"), TEXT("Texture2D"), TEXT("TextureCube"),
            TEXT("CurveLinearColorAtlas"), TEXT("CurveLinearColor"), TEXT("CurveFloat"), TEXT("CurveVector"), TEXT("PhysicalMaterial")};
        return Asset && (Classes.Contains(Asset->GetClass()->GetFName()) || Asset->IsA<UDataAsset>())
            && Asset->IsAsset() && !Asset->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
    }

    class FReferenceScan : public FArchiveUObject
    {
        FString Package;
    public:
        TSet<FString> Dependencies;
        FReferenceScan(UPackage* Root) : Package(Root->GetName())
        {
            ArIsObjectReferenceCollector = true;
            ArIgnoreOuterRef = true;
            ArIgnoreArchetypeRef = true;
            TArray<UObject*> Exports;
            GetObjectsWithOuter(Root, Exports, true);
            for (UObject* Export : Exports) Export->Serialize(*this);
        }
        using FArchiveUObject::operator<<;
        virtual FArchive& operator<<(UObject*& Object) override
        {
            if (Object && Object->GetOutermost()->GetName() != Package)
                Dependencies.Add(Object->GetOutermost()->GetName());
            return *this;
        }
        virtual FArchive& operator<<(FSoftObjectPath& Path) override
        {
            if (!Path.IsNull() && Path.GetLongPackageName() != Package) Dependencies.Add(Path.GetLongPackageName());
            return *this;
        }
    };

    TSet<FString> Dependencies(UPackage* Package)
    {
        FReferenceScan Scan(Package);
        TArray<FName> Registered;
        FAssetRegistryModule::GetRegistry().GetDependencies(Package->GetFName(), Registered, UE::AssetRegistry::EDependencyCategory::Package);
        for (FName Dependency : Registered) if (Dependency != Package->GetFName()) Scan.Dependencies.Add(Dependency.ToString());
        return Scan.Dependencies;
    }

    FString NormalizeObjectPaths(const FString& Text, const TMap<FString, FString>& ObjectPaths)
    {
        FString Result;
        int32 Cursor = 0;
        while (Cursor < Text.Len())
        {
            if (Text[Cursor] != TEXT('/'))
            {
                Result.AppendChar(Text[Cursor++]);
                continue;
            }
            int32 End = Cursor;
            while (End < Text.Len() && !FChar::IsWhitespace(Text[End])
                && Text[End] != TEXT('\'') && Text[End] != TEXT('"') && Text[End] != TEXT(',')
                && Text[End] != TEXT('(') && Text[End] != TEXT(')') && Text[End] != TEXT(';')
                && Text[End] != TEXT('=') && Text[End] != TEXT('\\')) ++End;
            const FString Token = Text.Mid(Cursor, End - Cursor);
            const FString* Destination = ObjectPaths.Find(Token);
            FString Prefix = Token;
            while (!Destination)
            {
                int32 Dot = INDEX_NONE, Colon = INDEX_NONE;
                Prefix.FindLastChar(TEXT('.'), Dot);
                Prefix.FindLastChar(TEXT(':'), Colon);
                const int32 Boundary = FMath::Max(Dot, Colon);
                if (Boundary == INDEX_NONE) break;
                Prefix.LeftInline(Boundary);
                if (!Prefix.Contains(TEXT("."))) break;
                Destination = ObjectPaths.Find(Prefix);
            }
            Result += Destination ? *Destination + Token.Mid(Prefix.Len()) : Token;
            Cursor = End;
        }
        return Result;
    }

    TMap<FString, FString> NormalizeSnapshot(const TMap<FString, FString>& Snapshot,
        const TMap<FString, FString>& ObjectPaths, const TMap<FString, FString>& Namespaces)
    {
        TMap<FString, FString> Result;
        for (const auto& Field : Snapshot)
        {
            FString Key = Field.Key;
            FString Value = NormalizeObjectPaths(Field.Value, ObjectPaths);
            for (const auto& Namespace : Namespaces)
            {
                Key.ReplaceInline(*Namespace.Key, *Namespace.Value, ESearchCase::CaseSensitive);
                Value.ReplaceInline(*Namespace.Key, *Namespace.Value, ESearchCase::CaseSensitive);
            }
            Result.Add(Key, Value);
        }
        return Result;
    }

    bool CheckSnapshot(const TMap<FString, FString>& Expected, const TMap<FString, FString>& Actual,
        const TSharedPtr<FJsonObject>& Result, const FString& AssetPath, const FString& Phase)
    {
        if (Expected.OrderIndependentCompareEqual(Actual)) return true;
        TArray<FString> Fields;
        Expected.GetKeys(Fields);
        for (const auto& Field : Actual) Fields.AddUnique(Field.Key);
        Fields.Sort();
        TArray<TSharedPtr<FJsonValue>> Differences;
        for (const FString& Field : Fields)
        {
            const FString* Before = Expected.Find(Field);
            const FString* After = Actual.Find(Field);
            if (Before && After && *Before == *After) continue;
            auto Difference = MakeShared<FJsonObject>();
            Difference->SetStringField(TEXT("field"), Field);
            if (Before) Difference->SetStringField(TEXT("before"), *Before);
            else Difference->SetField(TEXT("before"), MakeShared<FJsonValueNull>());
            if (After) Difference->SetStringField(TEXT("after"), *After);
            else Difference->SetField(TEXT("after"), MakeShared<FJsonValueNull>());
            Differences.Add(MakeShared<FJsonValueObject>(Difference));
        }
        Result->SetStringField(TEXT("comparison_stage"), Phase);
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetArrayField(TEXT("state_differences"), Differences);
        return false;
    }

    TMap<FString, FString> EditableSnapshot(UPackage* Package)
    {
        TArray<UObject*> Exports;
        GetObjectsWithOuter(Package, Exports, true);
        TMap<FString, FString> Values;
        TSet<const UObject*> CompiledInterfaces;
        for (UObject* Export : Exports)
            if (const auto Script = Cast<UNiagaraScript>(Export))
                for (const auto& Entry : Script->GetCachedDefaultDataInterfaces())
                    if (Entry.DataInterface && Entry.DataInterface->GetOuter() == Script) CompiledInterfaces.Add(Entry.DataInterface);
        for (UObject* Export : Exports)
        {
            if (Export->HasAnyFlags(RF_Transient)) continue;
            bool CompiledInterface = false;
            for (const UObject* Interface : CompiledInterfaces) CompiledInterface |= Export == Interface || Export->IsIn(Interface);
            if (CompiledInterface) continue;
            if (Export->IsA<UNiagaraDataInterface>() && Export->GetOuter()->IsA<UNiagaraScript>()
                && !Export->HasAnyFlags(RF_Public) && Export->GetName().StartsWith(TEXT("Invalidated_"))) continue;
            if (const auto Script = Cast<UNiagaraScript>(Export))
            {
                TArray<FNiagaraVariable> Parameters;
                Script->RapidIterationParameters.GetParameters(Parameters);
                for (const FNiagaraVariable& Parameter : Parameters)
                {
                    FString Value;
                    if (Parameter.GetType().IsDataInterface())
                        Value = GetPathNameSafe(Script->RapidIterationParameters.GetDataInterface(Parameter));
                    else if (Parameter.GetType().IsUObject())
                        Value = GetPathNameSafe(Script->RapidIterationParameters.GetUObject(Parameter));
                    else if (const uint8* Data = Script->RapidIterationParameters.GetParameterData(Parameter))
                        Value = BytesToHex(Data, Parameter.GetSizeInBytes());
                    else Value = TEXT("<missing>");
                    Values.Add(Export->GetPathName(Package) + TEXT(".RapidIterationParameters.")
                        + Parameter.GetType().GetNameText().ToString() + TEXT(":") + Parameter.GetName().ToString(), Value);
                }
            }
            for (TFieldIterator<FProperty> Field(Export->GetClass()); Field; ++Field)
            {
                if (Field->HasAnyPropertyFlags(CPF_Transient) || !Field->HasAnyPropertyFlags(CPF_Edit)
                    || (Export->IsA<UNiagaraScript>() && Field->GetName() == TEXT("RapidIterationParameters"))) continue;
                FString Text;
                const auto Struct = CastField<FStructProperty>(*Field);
                if (Struct && Struct->Struct == FNiagaraVariableAttributeBinding::StaticStruct())
                {
                    const auto Binding = Struct->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Export);
                    const auto RootName = FindFProperty<FNameProperty>(Struct->Struct, TEXT("RootName"));
                    check(RootName);
                    Text = FString::Printf(TEXT("Root=%s;Type=%s;SourceMode=%d"), *RootName->GetPropertyValue_InContainer(Binding).ToString(),
                        *Binding->GetType().GetNameText().ToString(), static_cast<int32>(Binding->GetBindingSourceMode()));
                }
                else Field->ExportTextItem_Direct(Text, Field->ContainerPtrToValuePtr<void>(Export), nullptr, Export, PPF_None);
                Values.Add(Export->GetPathName(Package) + TEXT(".") + Field->GetName(), Text);
            }
        }
        return Values;
    }

    TSharedPtr<FJsonObject> Compilation(UObject* Asset)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset"), Asset->GetPathName());
        Result->SetBoolField(TEXT("available"), true);
        Result->SetBoolField(TEXT("ok"), true);
        if (auto System = Cast<UNiagaraSystem>(Asset))
        {
            Result->SetStringField(TEXT("kind"), TEXT("niagara"));
            Result->SetBoolField(TEXT("ok"), System->IsValid() && System->IsReadyToRun());
        }
        else if (auto Material = Cast<UMaterialInterface>(Asset))
        {
            Result->SetStringField(TEXT("kind"), TEXT("material"));
            const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
            Result->SetBoolField(TEXT("available"), Resource && Resource->GetGameThreadShaderMap());
            Result->SetBoolField(TEXT("ok"), Resource && Resource->GetGameThreadShaderMap() && Resource->GetCompileErrors().IsEmpty());
            if (Resource) Result->SetArrayField(TEXT("errors"), Strings(Resource->GetCompileErrors()));
        }
        else Result->SetStringField(TEXT("kind"), TEXT("not_applicable"));
        return Result;
    }

    FString JournalPath(const FString& Id)
    {
        return FPaths::ProjectSavedDir() / TEXT("UnrealMCP/AssetMigration") / (Id + TEXT(".json"));
    }

    bool SaveReceipt(const FString& Id, const TSharedPtr<FJsonObject>& Receipt)
    {
        FString Json;
        FJsonSerializer::Serialize(Receipt.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        const FString Filename = JournalPath(Id);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        const FString Temporary = Filename + TEXT(".tmp");
        return FFileHelper::SaveStringToFile(Json, *Temporary)
            && IFileManager::Get().Move(*Filename, *Temporary, true, true);
    }

    class FScopedReferenceRemap : public FArchiveReplaceObjectRef<UObject>
    {
        const TMap<FString, FString>& Packages;
        const TMap<FString, FString>& Namespaces;
    public:
        int64 SoftChanges = 0;

        FScopedReferenceRemap(UObject* Root, const TMap<UObject*, UObject*>& Objects, const TMap<FString, FString>& InPackages,
            const TMap<FString, FString>& InNamespaces)
            : FArchiveReplaceObjectRef<UObject>(Root, Objects, EArchiveReplaceObjectFlags::DelayStart
                | EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef), Packages(InPackages), Namespaces(InNamespaces)
        {
            if (!Namespaces.IsEmpty())
            {
                ArIsObjectReferenceCollector = false;
                SetIsSaving(true);
            }
            SerializeSearchObject();
        }

        using FArchiveReplaceObjectRef<UObject>::operator<<;

        virtual FArchive& operator<<(FName& Name) override
        {
            const FString Text = Name.ToString();
            for (const auto& Namespace : Namespaces)
                if (Text.StartsWith(Namespace.Key, ESearchCase::CaseSensitive))
                { Name = FName(*(Namespace.Value + Text.Mid(Namespace.Key.Len()))); ++SoftChanges; break; }
            return *this;
        }

        virtual FArchive& operator<<(FSoftObjectPath& Path) override
        {
            if (UObject* Resolved = Path.ResolveObject())
                if (UObject* const* Replacement = ReplacementMap.Find(Resolved))
                {
                    Path = FSoftObjectPath(*Replacement);
                    ++SoftChanges;
                    return *this;
                }
            if (const FString* Destination = Packages.Find(Path.GetLongPackageName()))
            {
                const FString AssetName = Path.GetAssetName() == FPackageName::GetLongPackageAssetName(Path.GetLongPackageName())
                    ? FPackageName::GetLongPackageAssetName(*Destination) : Path.GetAssetName();
                const FString ObjectPath = *Destination + TEXT(".") + AssetName;
                Path = FSoftObjectPath(ObjectPath + (Path.GetSubPathString().IsEmpty() ? FString() : TEXT(":") + Path.GetSubPathString()));
                ++SoftChanges;
            }
            return *this;
        }
    };
}

#if WITH_DEV_AUTOMATION_TESTS
TMap<FString, FString> SnapshotForTests(UPackage* Package, const TMap<FString, FString>& ObjectPaths)
{
    return NormalizeSnapshot(EditableSnapshot(Package), ObjectPaths, {});
}

bool CheckSnapshotForTests(const TMap<FString, FString>& Expected, const TMap<FString, FString>& Actual,
    const TSharedPtr<FJsonObject>& Result)
{
    return CheckSnapshot(Expected, Actual, Result, TEXT("/Game/__Dev/NS_Test"), TEXT("rebind"));
}
#endif

int64 RemapPackage(UPackage* Package, const TMap<UObject*, UObject*>& Objects, const TMap<FString, FString>& Packages,
    const TMap<FString, FString>& Namespaces)
{
    TArray<UObject*> Exports;
    GetObjectsWithOuter(Package, Exports, true);
    int64 Changes = 0;
    for (UObject* Export : Exports)
    {
        FScopedReferenceRemap Archive(Export, Objects, Packages, Namespaces);
        Changes += Archive.GetCount() + Archive.SoftChanges;
    }
    if (Changes) Package->MarkPackageDirty();
    return Changes;
}

TSharedPtr<FJsonObject> Plan(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor || GEditor->PlayWorld) return Failure(TEXT("Planning requires the editor outside PIE"));
    if (Plans.Num() >= 32) return Failure(TEXT("Plan cache full; restart editor after resolving existing operations"));
    TArray<FString> Roots, Rebind, Retain;
    if (!ReadStrings(Params, TEXT("roots"), Roots, true) || !ReadStrings(Params, TEXT("rebind_assets"), Rebind, false)
        || !ReadStrings(Params, TEXT("retain_roots"), Retain, false)) return Failure(TEXT("Expected unique roots/rebind_assets/retain_roots arrays"));
    const TArray<TSharedPtr<FJsonValue>>* RuleValues;
    if (!Params->TryGetArrayField(TEXT("path_rules"), RuleValues) || RuleValues->IsEmpty() || RuleValues->Num() > 32)
        return Failure(TEXT("path_rules requires 1..32 {source_root,target_root} objects"));
    TMap<FString, FString> Rules;
    for (const auto& Value : *RuleValues)
    {
        const TSharedPtr<FJsonObject>* Rule;
        FString Source, Target;
        if (!Value->TryGetObject(Rule) || !(*Rule)->TryGetStringField(TEXT("source_root"), Source)
            || !(*Rule)->TryGetStringField(TEXT("target_root"), Target) || (Source != TEXT("/Game") && !GamePath(Source))
            || !GamePath(Target) || Source == Target || Rules.Contains(Source)) return Failure(TEXT("Invalid or duplicate /Game path rule"));
        Rules.Add(Source, Target);
    }
    for (const FString& Path : Roots) if (!GamePath(Path)) return Failure(TEXT("Roots require full /Game package paths"));
    for (const FString& Path : Rebind) if (!Roots.Contains(Path)) return Failure(TEXT("Every rebind asset must be an explicitly selected root"));
    for (const FString& Path : Retain) if (!GamePath(Path)) return Failure(TEXT("retain_roots must name explicit /Game packages or folders"));

    auto Stored = MakeShared<FPlan>();
    Stored->Rebind = Rebind;
    auto Result = MakeShared<FJsonObject>();
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<FString> Pending = Roots, Blockers;
    TSet<FString> Visited, External, Destinations;
    TArray<TSharedPtr<FJsonValue>> Edges, Mapping;
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        const FString Path = Pending[Index];
        if (Visited.Contains(Path)) continue;
        if (Visited.Num() >= MaxPackages) { Blockers.Add(TEXT("Dependency graph exceeds 512 packages; no executable plan")); break; }
        Visited.Add(Path);
        if (!Path.StartsWith(TEXT("/Game/")))
        {
            External.Add(Path);
            if (Path != GetTransientPackage()->GetName() && !Path.StartsWith(TEXT("/Script/")) && !FPackageName::DoesPackageExist(Path))
                Blockers.Add(TEXT("Missing external dependency: ") + Path);
            continue;
        }
        bool Retained = false;
        for (const FString& Prefix : Retain) Retained |= Under(Path, Prefix);
        if (Retained && !Roots.Contains(Path)) { External.Add(Path); continue; }
        FString Destination;
        if (!Rebind.Contains(Path))
        {
            int32 BestLength = -1;
            for (const auto& Rule : Rules) if (Under(Path, Rule.Key) && Rule.Key.Len() > BestLength)
            { Destination = Rule.Value + Path.Mid(Rule.Key.Len()); BestLength = Rule.Key.Len(); }
            if (Destination.IsEmpty()) { Blockers.Add(TEXT("No path rule or explicit retain rule for: ") + Path); continue; }
            if (!GamePath(Destination) || Destination == Path || Destinations.Contains(Destination.ToLower())
                || FindPackage(nullptr, *Destination) || FPackageName::DoesPackageExist(Destination)
                || UEditorAssetLibrary::DoesAssetExist(Destination)) Blockers.Add(TEXT("Invalid or occupied destination: ") + Destination);
            Destinations.Add(Destination.ToLower());
            Stored->Copies.Add(Path, Destination);
        }
        const FString Hash = FileHash(Path);
        if (Hash.IsEmpty()) { Blockers.Add(TEXT("Missing or unsaved source: ") + Path); continue; }
        if (UPackage* Loaded = FindPackage(nullptr, *Path)) if (Loaded->IsDirty()) Blockers.Add(TEXT("Dirty source/rebind asset: ") + Path);
        const FAssetData Data = UEditorAssetLibrary::FindAssetData(Path);
        if (!Data.IsValid() || Data.IsRedirector()) { Blockers.Add(TEXT("Missing registry asset or redirector: ") + Path); continue; }
        UObject* Asset = Data.GetAsset();
        if (!SupportedAsset(Asset) || Asset->GetPackage()->GetName() != Path)
        { Blockers.Add(TEXT("Unsupported asset type or redirected package: ") + Path); continue; }
        if (Rebind.Contains(Path) && !Asset->IsA<UNiagaraSystem>()) Blockers.Add(TEXT("Existing-root rebinding currently allows only NiagaraSystem: ") + Path);
        Stored->Hashes.Add(Path, Hash);
        if (auto Collection = Cast<UNiagaraParameterCollection>(Asset))
        {
            const FString NewNamespace = TEXT("MCP_") + Id.Left(12) + TEXT("_") + Collection->GetNamespace().ToString();
            Stored->CollectionNamespaces.Add(Path, NewNamespace);
            Stored->Namespaces.Add(Collection->GetFullNamespaceName().ToString(), TEXT("NPC.") + NewNamespace + TEXT("."));
        }
        auto Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("source"), Path);
        Node->SetStringField(TEXT("destination"), Rebind.Contains(Path) ? Path : Destination);
        Node->SetStringField(TEXT("action"), Rebind.Contains(Path) ? TEXT("rebind_only") : TEXT("copy"));
        Node->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        Node->SetStringField(TEXT("source_hash"), Hash);
        Mapping.Add(MakeShared<FJsonValueObject>(Node));
        for (const FString& Dependency : Dependencies(Asset->GetPackage()))
        {
            auto Edge = MakeShared<FJsonObject>();
            Edge->SetStringField(TEXT("from"), Path); Edge->SetStringField(TEXT("to"), Dependency);
            Edges.Add(MakeShared<FJsonValueObject>(Edge));
            Pending.AddUnique(Dependency);
        }
        if (Asset->GetPackage()->IsDirty()) Blockers.AddUnique(TEXT("Source became dirty during load/inspection: ") + Path);
    }
    for (const auto& Pair : Stored->Hashes)
        if (UPackage* Loaded = FindPackage(nullptr, *Pair.Key)) if (Loaded->IsDirty()) Blockers.AddUnique(TEXT("Dirty source/rebind asset: ") + Pair.Key);
    for (const auto& Copy : Stored->Copies) if (Visited.Contains(Copy.Value)) Blockers.Add(TEXT("Destination overlaps dependency graph: ") + Copy.Value);
    if (Stored->Copies.IsEmpty()) Blockers.Add(TEXT("Plan has no assets to copy"));
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("executable"), Blockers.IsEmpty());
    Result->SetStringField(TEXT("plan_id"), Id);
    Result->SetStringField(TEXT("state"), TEXT("planned"));
    Result->SetStringField(TEXT("confirmation_token"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Result->SetArrayField(TEXT("roots"), Strings(Roots));
    Result->SetArrayField(TEXT("rebind_assets"), Strings(Rebind));
    Result->SetArrayField(TEXT("mapping"), Mapping);
    TArray<TSharedPtr<FJsonValue>> NamespaceMapping;
    for (const auto& Pair : Stored->Namespaces)
    {
        auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("source"), Pair.Key); Entry->SetStringField(TEXT("destination"), Pair.Value);
        NamespaceMapping.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("namespace_mapping"), NamespaceMapping);
    Result->SetArrayField(TEXT("dependencies"), Edges);
    Result->SetArrayField(TEXT("retained_external"), Strings(External.Array()));
    Result->SetArrayField(TEXT("blockers"), Strings(Blockers));
    Result->SetStringField(TEXT("coverage"), TEXT("AssetRegistry all package dependencies plus serialized hard/soft references, including editor/preview/cache objects; string-encoded paths are not generally discoverable"));
    Stored->Receipt = Result;
    Plans.Add(Id, Stored);
    return Result;
}

TSharedPtr<FJsonObject> Status(const TSharedPtr<FJsonObject>& Params)
{
    FString Id;
    FGuid Guid;
    if (!Params->TryGetStringField(TEXT("plan_id"), Id) || !FGuid::ParseExact(Id, EGuidFormats::Digits, Guid)) return Failure(TEXT("Invalid plan_id"));
    if (const auto* Stored = Plans.Find(Id)) return (*Stored)->Receipt;
    FString Json;
    TSharedPtr<FJsonObject> Result;
    if (FFileHelper::LoadFileToString(Json, *JournalPath(Id)) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Result))
    {
        if (Result->GetStringField(TEXT("state")) == TEXT("running"))
        { Result->SetStringField(TEXT("state"), TEXT("interrupted_unknown")); Result->SetBoolField(TEXT("success"), false); }
        return Result;
    }
    return Failure(TEXT("Unknown/expired plan; do not blindly retry a timed-out mutation. Inspect destinations first"));
}

TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params)
{
    FString Id, Token;
    if (!Params->TryGetStringField(TEXT("plan_id"), Id) || !Params->TryGetStringField(TEXT("confirmation_token"), Token))
        return Failure(TEXT("plan_id and confirmation_token from the reviewed plan are required"));
    auto* Found = Plans.Find(Id);
    if (!Found) return Status(Params);
    TSharedPtr<FPlan> Stored = *Found;
    auto Result = Stored->Receipt;
    if (Token != Result->GetStringField(TEXT("confirmation_token"))) return Failure(TEXT("Plan confirmation mismatch"));
    if (Stored->Started) return Result;
    if (!Result->GetBoolField(TEXT("executable")) || !GEditor || GEditor->PlayWorld) return Failure(TEXT("Plan is blocked or editor is in PIE"));
    TArray<TStrongObjectPtr<UObject>> KeepAlive;
    TMap<FString, UObject*> Sources;
    for (const auto& Pair : Stored->Hashes)
    {
        if (FileHash(Pair.Key) != Pair.Value) return Failure(TEXT("Source changed since plan: ") + Pair.Key);
        UObject* Asset = UEditorAssetLibrary::LoadAsset(Pair.Key);
        if (!SupportedAsset(Asset) || Asset->GetPackage()->IsDirty()) return Failure(TEXT("Dirty/unavailable source since plan: ") + Pair.Key);
        Sources.Add(Pair.Key, Asset); KeepAlive.Emplace(Asset);
    }
    for (const auto& Pair : Stored->Copies)
        if (FindPackage(nullptr, *Pair.Value) || FPackageName::DoesPackageExist(Pair.Value) || UEditorAssetLibrary::DoesAssetExist(Pair.Value))
            return Failure(TEXT("Destination now occupied; overwrite forbidden: ") + Pair.Value);
    for (const auto& Pair : Sources) if (Pair.Value->GetPackage()->IsDirty()) return Failure(TEXT("Source became dirty during preflight: ") + Pair.Key);
    Stored->Started = true;
    Result->SetBoolField(TEXT("success"), false);
    Result->SetStringField(TEXT("state"), TEXT("running"));
    TArray<TSharedPtr<FJsonValue>> Items;
    TMap<FString, TSharedPtr<FJsonObject>> ItemBySource;
    for (const auto& Value : Result->GetArrayField(TEXT("mapping")))
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("source"), Value->AsObject()->GetStringField(TEXT("source")));
        Item->SetStringField(TEXT("destination"), Value->AsObject()->GetStringField(TEXT("destination")));
        Item->SetStringField(TEXT("action"), Value->AsObject()->GetStringField(TEXT("action")));
        Item->SetBoolField(TEXT("created"), false); Item->SetBoolField(TEXT("modified"), false); Item->SetBoolField(TEXT("saved"), false);
        Items.Add(MakeShared<FJsonValueObject>(Item)); ItemBySource.Add(Item->GetStringField(TEXT("source")), Item);
    }
    Result->SetArrayField(TEXT("items"), Items);
    if (!SaveReceipt(Id, Result)) { Result->SetStringField(TEXT("state"), TEXT("failed")); Result->SetStringField(TEXT("error"), TEXT("Could not persist execution intent; nothing copied")); return Result; }
    auto Fail = [&](const FString& Message)
    {
        Result->SetStringField(TEXT("state"), TEXT("failed")); Result->SetStringField(TEXT("error"), Message);
        Result->SetBoolField(TEXT("success"), false); SaveReceipt(Id, Result); return Result;
    };
    TMap<UObject*, UObject*> ObjectMap;
    TArray<UObject*> Targets;
    TMap<FString, TMap<FString, FString>> RebindSnapshots;
    for (const FString& Path : Stored->Rebind)
    {
        auto System = CastChecked<UNiagaraSystem>(Sources[Path]);
        System->RequestCompile(false);
        System->WaitForCompilationComplete(true, false);
        if (System->GetPackage()->IsDirty())
        {
            ItemBySource[Path]->SetBoolField(TEXT("modified"), true);
            ItemBySource[Path]->SetBoolField(TEXT("package_dirty"), true);
            return Fail(TEXT("Baseline compilation left unsaved changes; review before rebinding: ") + Path);
        }
        if (!System->IsValid() || !System->IsReadyToRun()) return Fail(TEXT("Existing NS compilation is not valid: ") + Path);
    }
    for (const FString& Path : Stored->Rebind) RebindSnapshots.Add(Path, EditableSnapshot(Sources[Path]->GetPackage()));
    for (const auto& Pair : Stored->Copies)
    {
        if (FindPackage(nullptr, *Pair.Value) || FPackageName::DoesPackageExist(Pair.Value)) return Fail(TEXT("Destination became occupied: ") + Pair.Value);
        UObject* Source = Sources.FindRef(Pair.Key);
        FObjectDuplicationParameters Duplicate(Source, CreatePackage(*Pair.Value));
        Duplicate.DestName = *FPackageName::GetLongPackageAssetName(Pair.Value);
        TMap<UObject*, UObject*> Created;
        Duplicate.CreatedObjects = &Created;
        UObject* Copy = StaticDuplicateObjectEx(Duplicate);
        if (!Copy) return Fail(TEXT("Native duplication failed: ") + Pair.Key);
        Copy->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        if (auto Texture = Cast<UTexture>(Copy)) Texture->OodleTextureSdkVersion = CastChecked<UTexture>(Source)->OodleTextureSdkVersion;
        if (auto Collection = Cast<UNiagaraParameterCollection>(Copy))
        {
            FNameProperty* Namespace = FindFProperty<FNameProperty>(Collection->GetClass(), TEXT("Namespace"));
            if (!Namespace) return Fail(TEXT("Niagara collection namespace property unavailable"));
            Collection->PreEditChange(Namespace);
            Namespace->SetPropertyValue_InContainer(Collection, FName(*Stored->CollectionNamespaces[Pair.Key]));
            FPropertyChangedEvent Event(Namespace, EPropertyChangeType::ValueSet); Collection->PostEditChangeProperty(Event);
            if (Collection->GetNamespace().ToString() != Stored->CollectionNamespaces[Pair.Key]) return Fail(TEXT("Planned NPC namespace became occupied"));
        }
        ObjectMap.Append(Created); ObjectMap.Add(Source, Copy);
        ObjectMap.Add(Source->GetPackage(), Copy->GetPackage());
        FAssetRegistryModule::AssetCreated(Copy); Copy->MarkPackageDirty();
        Targets.Add(Copy); KeepAlive.Emplace(Copy);
        ItemBySource[Pair.Key]->SetBoolField(TEXT("created"), true);
        ItemBySource[Pair.Key]->SetBoolField(TEXT("modified"), true);
        if (!SaveReceipt(Id, Result)) return Fail(TEXT("Execution journal write failed after copy; inspect item state"));
    }
    TMap<FString, FString> ObjectPaths;
    for (const auto& Pair : ObjectMap)
        if (Pair.Key && Pair.Value) ObjectPaths.Add(Pair.Key->GetPathName(), Pair.Value->GetPathName());
    for (const FString& Path : Stored->Rebind)
    {
        RebindSnapshots[Path] = NormalizeSnapshot(RebindSnapshots[Path], ObjectPaths, Stored->Namespaces);
        Targets.Add(Sources[Path]);
    }
    for (UObject* Target : Targets)
    {
        Target->Modify();
        const int64 Changed = RemapPackage(Target->GetPackage(), ObjectMap, Stored->Copies, Stored->Namespaces);
        for (const auto& Item : Items) if (Item->AsObject()->GetStringField(TEXT("destination")) == Target->GetPackage()->GetName())
        { Item->AsObject()->SetNumberField(TEXT("references_remapped"), Changed); if (Changed) Item->AsObject()->SetBoolField(TEXT("modified"), true); }
        if (!SaveReceipt(Id, Result)) return Fail(TEXT("Execution journal write failed after reference update"));
    }
    for (const FString& Path : Stored->Rebind)
        if (!CheckSnapshot(RebindSnapshots[Path], EditableSnapshot(Sources[Path]->GetPackage()), Result, Path, TEXT("rebind")))
            return Fail(TEXT("Editable non-reference state changed during rebinding; not saved: ") + Path);
    for (UObject* Target : Targets)
    {
        Target->PostEditChange();
        if (auto System = Cast<UNiagaraSystem>(Target)) { System->RequestCompile(true); System->WaitForCompilationComplete(true, false); }
    }
    FAssetCompilingManager::Get().FinishCompilationForObjects(Targets);
    TArray<TSharedPtr<FJsonValue>> Compilations;
    bool CompileOk = true;
    for (UObject* Target : Targets)
    {
        auto State = Compilation(Target); Compilations.Add(MakeShared<FJsonValueObject>(State));
        CompileOk &= State->GetBoolField(TEXT("ok"));
    }
    Result->SetArrayField(TEXT("compilation"), Compilations);
    if (!CompileOk) return Fail(TEXT("Compilation is invalid or unavailable; no automatic save. Inspect compilation entries"));
    for (const FString& Path : Stored->Rebind)
        if (!CheckSnapshot(RebindSnapshots[Path], EditableSnapshot(Sources[Path]->GetPackage()), Result, Path, TEXT("post_compile")))
            return Fail(TEXT("Editable state changed during post-edit/compile; not saved: ") + Path);
    TArray<FString> SavedFiles;
    for (UObject* Target : Targets)
    {
        if (!UEditorAssetLibrary::SaveLoadedAsset(Target, false) || Target->GetPackage()->IsDirty() || FileHash(Target->GetPackage()->GetName()).IsEmpty())
            return Fail(TEXT("Save failed; in-memory changes remain: ") + Target->GetPathName());
        FString Filename; FPackageName::DoesPackageExist(Target->GetPackage()->GetName(), &Filename); SavedFiles.Add(Filename);
        for (const auto& Item : Items) if (Item->AsObject()->GetStringField(TEXT("destination")) == Target->GetPackage()->GetName())
        {
            Item->AsObject()->SetBoolField(TEXT("saved"), true); Item->AsObject()->SetBoolField(TEXT("package_dirty"), false);
            Item->AsObject()->SetStringField(TEXT("destination_hash"), FileHash(Target->GetPackage()->GetName()));
        }
        if (!SaveReceipt(Id, Result)) return Fail(TEXT("Journal failed after saving; inspect destination files"));
    }
    FAssetRegistryModule::GetRegistry().ScanModifiedAssetFiles(SavedFiles);
    TArray<FString> Leaks;
    for (UObject* Target : Targets)
        for (const FString& Dependency : Dependencies(Target->GetPackage()))
            if (Stored->Copies.Contains(Dependency)) Leaks.AddUnique(Target->GetPackage()->GetName() + TEXT(" -> ") + Dependency);
    Result->SetArrayField(TEXT("unresolved_references"), Strings(Leaks));
    if (!Leaks.IsEmpty()) return Fail(TEXT("Saved targets still reference the original migration set; inspect unresolved_references"));
    for (const auto& Pair : Stored->Hashes)
        if (!Stored->Rebind.Contains(Pair.Key) && FileHash(Pair.Key) != Pair.Value) return Fail(TEXT("Source file changed during operation: ") + Pair.Key);
    Result->SetStringField(TEXT("state"), TEXT("completed"));
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("fresh_reload_verified"), false);
    if (!SaveReceipt(Id, Result)) return Fail(TEXT("Final receipt persistence failed"));
    return Result;
}

TSharedPtr<FJsonObject> Verify(const TSharedPtr<FJsonObject>& Params)
{
    auto Receipt = Status(Params);
    if (!Receipt->HasField(TEXT("items"))) return Receipt;
    TSet<FString> Original, Targets, Allowed;
    for (const auto& Item : Receipt->GetArrayField(TEXT("items")))
    {
        if (Item->AsObject()->GetStringField(TEXT("action")) == TEXT("copy")) Original.Add(Item->AsObject()->GetStringField(TEXT("source")));
        Targets.Add(Item->AsObject()->GetStringField(TEXT("destination")));
    }
    for (const auto& Value : Receipt->GetArrayField(TEXT("retained_external"))) Allowed.Add(Value->AsString());
    TArray<FString> Pending = Targets.Array(), Issues;
    TSet<FString> Visited, External;
    TArray<TSharedPtr<FJsonValue>> CompileStates;
    TArray<FString> Preloaded;
    for (const FString& Target : Targets) if (FindPackage(nullptr, *Target)) Preloaded.Add(Target);
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        const FString Path = Pending[Index];
        if (Visited.Contains(Path) || Path.StartsWith(TEXT("/Script/")) || Path == GetTransientPackage()->GetName()) continue;
        if (Visited.Num() >= MaxPackages * 4) { Issues.Add(TEXT("Verification dependency budget exceeded")); break; }
        Visited.Add(Path);
        if (Original.Contains(Path)) { Issues.Add(TEXT("Original migration package still reachable: ") + Path); continue; }
        UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
        if (!Asset) { Issues.Add(TEXT("Dependency missing after reload: ") + Path); continue; }
        if (Targets.Contains(Path))
        {
            if (auto System = Cast<UNiagaraSystem>(Asset)) System->WaitForCompilationComplete(true, false);
            TArray<UObject*> CompilationTargets{Asset};
            FAssetCompilingManager::Get().FinishCompilationForObjects(CompilationTargets);
            if (Asset->GetPackage()->IsDirty()) Issues.Add(TEXT("Destination has unsaved changes: ") + Path);
            auto State = Compilation(Asset); CompileStates.Add(MakeShared<FJsonValueObject>(State));
            if (!State->GetBoolField(TEXT("ok"))) Issues.Add(TEXT("Compilation invalid/unavailable: ") + Path);
        }
        else
        {
            External.Add(Path);
            if (Path.StartsWith(TEXT("/Game/")) && !Allowed.Contains(Path)) Issues.Add(TEXT("Unplanned /Game external dependency: ") + Path);
        }
        for (const FString& Dependency : Dependencies(Asset->GetPackage())) Pending.AddUnique(Dependency);
    }
    for (const auto& Value : Receipt->GetArrayField(TEXT("items")))
    {
        auto Item = Value->AsObject();
        FString Expected;
        if (!Item->TryGetStringField(TEXT("destination_hash"), Expected) || Expected != FileHash(Item->GetStringField(TEXT("destination"))))
            Issues.Add(TEXT("Destination absent, unsaved or changed since execution: ") + Item->GetStringField(TEXT("destination")));
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), Issues.IsEmpty());
    Result->SetBoolField(TEXT("fresh_reload"), Preloaded.IsEmpty());
    Result->SetArrayField(TEXT("already_loaded"), Strings(Preloaded));
    Result->SetArrayField(TEXT("issues"), Strings(Issues));
    Result->SetArrayField(TEXT("retained_external"), Strings(External.Array()));
    Result->SetArrayField(TEXT("compilation"), CompileStates);
    Result->SetStringField(TEXT("plan_id"), Receipt->GetStringField(TEXT("plan_id")));
    return Result;
}
}