#include "Commands/UnrealMCPStringTableCommands.h"
#include "EditorAssetLibrary.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/Csv/CsvParser.h"

namespace
{
    struct FEntry
    {
        FString Source;
        TMap<FName, FString> Metadata;
        bool operator==(const FEntry& Other) const { return Source == Other.Source && Metadata.OrderIndependentCompareEqual(Other.Metadata); }
    };
    using FEntries = TMap<FString, FEntry>;

    FEntries ReadEntries(const FStringTableConstRef& Table)
    {
        FEntries Entries;
        Table->EnumerateSourceStrings([&](const FString& Key, const FString& Source)
        {
            FEntry Entry; Entry.Source = Source;
            Table->EnumerateMetaData(FTextKey(Key), [&](FName Name, const FString& Value) { Entry.Metadata.Add(Name, Value); return true; });
            Entries.Add(Key, MoveTemp(Entry));
            return true;
        });
        return Entries;
    }

    void ApplyEntries(const FStringTableRef& Table, const FEntries& Entries)
    {
        const auto Before = ReadEntries(Table);
        for (const auto& Pair : Before) if (!Entries.Contains(Pair.Key)) Table->RemoveSourceString(FTextKey(Pair.Key));
        for (const auto& Pair : Entries)
        {
            const FEntry* Old = Before.Find(Pair.Key);
            if (!Old || Old->Source != Pair.Value.Source) Table->SetSourceString(FTextKey(Pair.Key), Pair.Value.Source);
            if (Old) for (const auto& Meta : Old->Metadata)
                if (!Pair.Value.Metadata.Contains(Meta.Key)) Table->RemoveMetaData(FTextKey(Pair.Key), Meta.Key);
            for (const auto& Meta : Pair.Value.Metadata)
                if (!Old || !Old->Metadata.Contains(Meta.Key) || Old->Metadata[Meta.Key] != Meta.Value)
                    Table->SetMetaData(FTextKey(Pair.Key), Meta.Key, Meta.Value);
        }
    }

    TSharedPtr<FJsonObject> Failure(const FString& Error)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetBoolField(TEXT("modified"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("stage"), TEXT("preflight"));
        Result->SetStringField(TEXT("error"), Error);
        return Result;
    }

    TSharedPtr<FJsonObject> Snapshot(UStringTable* Asset)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
        Result->SetStringField(TEXT("object_path"), Asset->GetPathName());
        Result->SetStringField(TEXT("table_id"), Asset->GetStringTableId().ToString());
        const auto Table = Asset->GetStringTable();
        Result->SetStringField(TEXT("namespace"), Table->GetNamespace());
        TMap<FString, FString> Strings;
        Table->EnumerateSourceStrings([&](const FString& Key, const FString& Source)
        {
            Strings.Add(Key, Source);
            return true;
        });
        TArray<FString> Keys;
        Strings.GetKeys(Keys);
        Keys.Sort();
        TArray<TSharedPtr<FJsonValue>> Entries;
        for (const FString& Key : Keys)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("key"), Key);
            Entry->SetStringField(TEXT("source_string"), Strings[Key]);
            auto Metadata = MakeShared<FJsonObject>();
            Table->EnumerateMetaData(FTextKey(Key), [&](FName Name, const FString& Value)
            {
                Metadata->SetStringField(Name.ToString(), Value);
                return true;
            });
            Entry->SetObjectField(TEXT("metadata"), Metadata);
            Entries.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("entries"), Entries);
        Result->SetNumberField(TEXT("entry_count"), Entries.Num());
        Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
        return Result;
    }

    bool ValidKey(const FString& Key)
    {
        for (int32 Index = 0; Index < Key.Len(); ++Index) if (!Key[Index]) return false;
        return !Key.TrimStartAndEnd().IsEmpty() && Key.Len() <= 1024
            && !Key.Contains(TEXT("\n")) && !Key.Contains(TEXT("\r"));
    }

    bool ValidCsvQuoting(const FString& Text)
    {
        enum class ECell { Start, Bare, Quoted, Closed };
        ECell Cell = ECell::Start;
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            const TCHAR Character = Text[Index];
            if (!Character) return false;
            if (Cell == ECell::Quoted)
            {
                if (Character == TEXT('"'))
                {
                    if (Index + 1 < Text.Len() && Text[Index + 1] == TEXT('"')) ++Index;
                    else Cell = ECell::Closed;
                }
            }
            else if (Character == TEXT(',') || Character == TEXT('\n') || Character == TEXT('\r')) Cell = ECell::Start;
            else if (Cell == ECell::Closed) return false;
            else if (Character == TEXT('"'))
            {
                if (Cell != ECell::Start) return false;
                Cell = ECell::Quoted;
            }
            else Cell = ECell::Bare;
        }
        return Cell != ECell::Quoted;
    }

#if WITH_DEV_AUTOMATION_TESTS
    bool FailStringTableSaveForTest = false;
#endif

    bool SaveTable(UStringTable* Asset)
    {
#if WITH_DEV_AUTOMATION_TESTS
        if (FailStringTableSaveForTest) return false;
#endif
        return UEditorAssetLibrary::SaveLoadedAsset(Asset, false) && !Asset->GetPackage()->IsDirty();
    }

    bool ApplyPatches(const TSharedPtr<FJsonObject>& Params, FEntries& Entries, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Patches = nullptr;
        if (!Params->TryGetArrayField(TEXT("entries"), Patches) || Patches->Num() > 4096)
        { Error = TEXT("entries must be an array of at most 4096 entry objects"); return false; }
        TSet<FString> Seen;
        for (const auto& Value : *Patches)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) { Error = TEXT("Each entry must be an object"); return false; }
            auto Patch = Value->AsObject(); FString Key;
            if (!Patch->TryGetStringField(TEXT("key"), Key) || !ValidKey(Key) || Seen.Contains(Key))
            { Error = TEXT("Entry keys must be nonempty and unique"); return false; }
            Seen.Add(Key);
            for (const auto& Pair : Patch->Values)
                if (Pair.Key != TEXT("key") && Pair.Key != TEXT("source_string") && Pair.Key != TEXT("metadata"))
                { Error = TEXT("Unknown entry field: ") + Pair.Key; return false; }
            FEntry Next;
            if (const auto* Old = Entries.Find(Key)) Next = *Old;
            if (Patch->HasField(TEXT("source_string")))
            {
                if (!Patch->TryGetStringField(TEXT("source_string"), Next.Source)) { Error = TEXT("source_string must be a string"); return false; }
            }
            else if (!Entries.Contains(Key)) { Error = TEXT("New entries require source_string"); return false; }
            if (Next.Source.Len() > 65536) { Error = TEXT("Source string too large"); return false; }
            if (Patch->HasField(TEXT("metadata")))
            {
                const TSharedPtr<FJsonObject>* Metadata = nullptr;
                if (!Patch->TryGetObjectField(TEXT("metadata"), Metadata)) { Error = TEXT("metadata must be an object"); return false; }
                TSet<FName> Names;
                for (const auto& Meta : (*Metadata)->Values)
                {
                    if (!ValidKey(Meta.Key) || Meta.Key.Len() >= NAME_SIZE) { Error = TEXT("Invalid metadata name"); return false; }
                    const FName Name(*Meta.Key);
                    if (!ValidKey(Meta.Key) || Name.IsNone() || Names.Contains(Name)) { Error = TEXT("Invalid or case-duplicate metadata name"); return false; }
                    Names.Add(Name);
                    if (Meta.Value->IsNull()) Next.Metadata.Remove(Name);
                    else if (Meta.Value->Type == EJson::String && Meta.Value->AsString().Len() <= 65536) Next.Metadata.Add(Name, Meta.Value->AsString());
                    else { Error = TEXT("Metadata values must be strings or null to remove"); return false; }
                }
            }
            Entries.Add(Key, MoveTemp(Next));
        }
        const TArray<TSharedPtr<FJsonValue>>* Deletes = nullptr;
        if (Params->HasField(TEXT("delete_keys")))
        {
            if (!Params->TryGetArrayField(TEXT("delete_keys"), Deletes) || Deletes->Num() > 4096) { Error = TEXT("delete_keys must be an array"); return false; }
            for (const auto& Value : *Deletes)
            {
                FString Key;
                if (!Value->TryGetString(Key) || !ValidKey(Key) || Seen.Contains(Key)) { Error = TEXT("Deleted keys must be unique and disjoint from entries"); return false; }
                Seen.Add(Key); Entries.Remove(Key);
            }
        }
        return true;
    }

    bool ImportCsv(const FString& File, FEntries& Imported, FString& Error)
    {
        if (FPaths::IsRelative(File) || !File.EndsWith(TEXT(".csv"), ESearchCase::IgnoreCase)
            || IFileManager::Get().FileSize(*File) < 0 || IFileManager::Get().FileSize(*File) > 1024 * 1024)
        { Error = TEXT("csv_path must be an existing absolute .csv file, at most 1 MiB"); return false; }
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *File)) { Error = TEXT("Cannot read CSV"); return false; }
        if (!ValidCsvQuoting(Text)) { Error = TEXT("CSV contains malformed quoting or null characters"); return false; }
        const FCsvParser Parser(Text);
        const auto& Rows = Parser.GetRows();
        if (Rows.Num() < 2 || Rows.Num() > 4097) { Error = TEXT("CSV requires header and 1..4096 rows"); return false; }
        TSet<FName> Headers; int32 KeyColumn = INDEX_NONE; bool HasSource = false;
        for (int32 Column = 0; Column < Rows[0].Num(); ++Column)
        {
            const FString Header(Rows[0][Column]);
            if (!ValidKey(Header) || Header.Len() >= NAME_SIZE) { Error = TEXT("Invalid CSV header name"); return false; }
            const FName Name(*Header);
            if (!ValidKey(Header) || Name.IsNone() || Headers.Contains(Name)) { Error = TEXT("CSV headers must be nonempty and unique"); return false; }
            Headers.Add(Name);
            if (Header.Equals(TEXT("Key"), ESearchCase::IgnoreCase)) KeyColumn = Column;
            if (Header.Equals(TEXT("SourceString"), ESearchCase::IgnoreCase)) HasSource = true;
        }
        if (KeyColumn == INDEX_NONE || !HasSource) { Error = TEXT("CSV requires Key and SourceString headers; additional columns are metadata, not translations"); return false; }
        TSet<FString> Keys;
        for (int32 Row = 1; Row < Rows.Num(); ++Row)
        {
            if (Rows[Row].Num() != Rows[0].Num()) { Error = TEXT("CSV rows must have the same column count as the header"); return false; }
            for (const TCHAR* Cell : Rows[Row]) if (FCString::Strlen(Cell) > 65536) { Error = TEXT("CSV cell exceeds 65536 characters"); return false; }
            const FString Key = FString(Rows[Row][KeyColumn]).ReplaceEscapedCharWithChar();
            if (!ValidKey(Key) || Keys.Contains(Key)) { Error = TEXT("CSV keys must be nonempty and unique after native unescaping"); return false; }
            Keys.Add(Key);
        }
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("UnrealMCP/StringTables");
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Temp = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".csv"));
        ON_SCOPE_EXIT { IFileManager::Get().Delete(*Temp); };
        if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Error = TEXT("Cannot stage CSV snapshot"); return false; }
        auto Table = FStringTable::NewStringTable();
        if (!Table->ImportStrings(Temp)) { Error = TEXT("Native String Table CSV import failed"); return false; }
        Imported = ReadEntries(Table);
        if (Imported.Num() != Keys.Num()) { Error = TEXT("Native imported keys differ from validation"); return false; }
        return true;
    }
}

TSharedPtr<FJsonObject> UnrealMCPStringTables::Handle(const FString& Command, const TSharedPtr<FJsonObject>& Params)
{
    FString Path;
    if (!Params || !Params->TryGetStringField(TEXT("asset_path"), Path)
        || !Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Path))
        return Failure(TEXT("asset_path must be a /Game/... package path, without an object suffix"));
    const bool Read = Command == TEXT("read_string_table");
    const bool Create = Command == TEXT("create_string_table");
    const bool Import = Command == TEXT("import_string_table_csv");
    if (!Read && !Create && !Import && Command != TEXT("update_string_table_entries")) return Failure(TEXT("Unsupported String Table operation"));
    TSet<FString> Allowed = {TEXT("asset_path")};
    if (!Read) Allowed.Append({TEXT("dry_run"), TEXT("save")});
    if (Create) Allowed.Add(TEXT("namespace"));
    else if (!Read) Allowed.Add(TEXT("expected_namespace"));
    if (Import) Allowed.Append({TEXT("csv_path"), TEXT("mode"), TEXT("allow_delete")});
    else if (!Read && !Create) Allowed.Append({TEXT("entries"), TEXT("delete_keys")});
    for (const auto& Pair : Params->Values)
        if (!Allowed.Contains(Pair.Key)) return Failure(TEXT("Unknown parameter: ") + Pair.Key);
    const FString ObjectPath = Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    UObject* Loaded = FindObject<UObject>(nullptr, *ObjectPath);
    const bool Exists = Loaded || UEditorAssetLibrary::DoesAssetExist(Path) || FPackageName::DoesPackageExist(Path);
    if (Create && (Exists || FindPackage(nullptr, *Path))) return Failure(TEXT("Destination already exists; overwrite is not supported"));
    if (!Create && !Exists) return Failure(TEXT("String Table does not exist"));
    auto Asset = Create ? nullptr : Cast<UStringTable>(Loaded ? Loaded : UEditorAssetLibrary::LoadAsset(Path));
    if (!Create && !Asset) return Failure(TEXT("Asset is not a String Table"));
    if (Read)
    {
        auto Result = Snapshot(Asset); Result->SetBoolField(TEXT("success"), true);
        Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
    }
    if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || GIsSavingPackage || IsGarbageCollecting())
        return Failure(TEXT("String Table writes require an idle editor, outside PIE/Simulate/save/GC"));
    bool DryRun = true, Save = false, AllowDelete = false;
    for (const auto& Flag : TArray<TPair<FString, bool*>>{{TEXT("dry_run"), &DryRun}, {TEXT("save"), &Save}, {TEXT("allow_delete"), &AllowDelete}})
        if (Params->HasField(Flag.Key) && !Params->TryGetBoolField(Flag.Key, *Flag.Value)) return Failure(Flag.Key + TEXT(" must be boolean"));
    FString Namespace;
    if (!Params->TryGetStringField(Create ? TEXT("namespace") : TEXT("expected_namespace"), Namespace) || !ValidKey(Namespace))
        return Failure(TEXT("A nonempty namespace (create) or expected_namespace (edit) is required"));
    if (!Create && Namespace != Asset->GetStringTable()->GetNamespace()) return Failure(TEXT("Namespace conflict; no identity migration is performed"));
    if (!DryRun && Asset && Asset->GetPackage()->IsDirty()) return Failure(TEXT("Asset has pre-existing unsaved edits; inspect/save them explicitly before editing"));
    const FEntries Before = Asset ? ReadEntries(Asset->GetStringTable()) : FEntries();
    FEntries Next = Before; FString Error;
    if (Import)
    {
        FString File, Mode;
        if (!Params->TryGetStringField(TEXT("csv_path"), File) || !Params->TryGetStringField(TEXT("mode"), Mode)
            || (Mode != TEXT("merge") && Mode != TEXT("replace"))) return Failure(TEXT("Require csv_path and explicit mode merge or replace"));
        FEntries Imported;
        if (!ImportCsv(File, Imported, Error)) return Failure(Error);
        if (Mode == TEXT("replace")) Next = Imported;
        else for (const auto& Pair : Imported)
        {
            auto& Entry = Next.FindOrAdd(Pair.Key); Entry.Source = Pair.Value.Source;
            for (const auto& Meta : Pair.Value.Metadata) Entry.Metadata.Add(Meta.Key, Meta.Value);
        }
    }
    else if (!Create && !ApplyPatches(Params, Next, Error)) return Failure(Error);
    if (Next.Num() > 4096) return Failure(TEXT("Table exceeds the 4096-entry write limit"));
    TArray<TSharedPtr<FJsonValue>> Added, Changed, Deleted;
    for (const auto& Pair : Next)
    {
        if (!Before.Contains(Pair.Key)) Added.Add(MakeShared<FJsonValueString>(Pair.Key));
        else if (!(Before[Pair.Key] == Pair.Value)) Changed.Add(MakeShared<FJsonValueString>(Pair.Key));
    }
    for (const auto& Pair : Before) if (!Next.Contains(Pair.Key)) Deleted.Add(MakeShared<FJsonValueString>(Pair.Key));
    if (Import && !DryRun && Deleted.Num() && !AllowDelete) return Failure(TEXT("Replace would delete keys; review dry_run and explicitly set allow_delete=true"));
    const bool Modify = Create || Added.Num() || Changed.Num() || Deleted.Num();
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Path);
    if (Asset) Result->SetObjectField(TEXT("before"), Snapshot(Asset));
    Result->SetArrayField(TEXT("added_keys"), Added); Result->SetArrayField(TEXT("updated_keys"), Changed); Result->SetArrayField(TEXT("deleted_keys"), Deleted);
    Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), Modify);
    Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("created"), false);
    Result->SetStringField(TEXT("namespace"), Namespace);
    if (!DryRun && Modify)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "StringTableEdit", "Edit String Table"));
        if (Create)
        {
            Asset = NewObject<UStringTable>(CreatePackage(*Path), FName(*FPackageName::GetLongPackageAssetName(Path)), RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Asset);
            Asset->GetMutableStringTable()->SetNamespace(FTextKey(Namespace));
        }
        Asset->SetFlags(RF_Transactional); Asset->Modify(); Asset->PreEditChange(nullptr);
        ApplyEntries(Asset->GetMutableStringTable(), Next);
        Asset->PostEditChange(); Asset->MarkPackageDirty();
        Result->SetBoolField(TEXT("modified"), true); Result->SetBoolField(TEXT("created"), Create);
        const bool Matches = ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Next)
            && Asset->GetStringTable()->GetNamespace() == Namespace;
        const bool Saved = Matches && Save && SaveTable(Asset);
        Result->SetBoolField(TEXT("saved"), Saved);
        if (!Matches || (Save && !Saved))
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), Matches ? TEXT("save") : TEXT("readback"));
            Result->SetStringField(TEXT("error"), TEXT("Edit applied but readback/save failed; inspect actual after state before retrying"));
            Result->SetObjectField(TEXT("after"), Snapshot(Asset)); return Result;
        }
    }
    Result->SetBoolField(TEXT("success"), true);
    if (Asset) Result->SetObjectField(TEXT("after"), Snapshot(Asset));
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStringTableReadTest, "UnrealMCP.StringTables.Read",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStringTableReadTest::RunTest(const FString& Parameters)
{
    const FString Path = TEXT("/Game/__Dev/StringTable_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/ST_Status");
    auto Asset = NewObject<UStringTable>(CreatePackage(*Path), TEXT("ST_Status"), RF_Public | RF_Standalone | RF_Transactional);
    Asset->GetMutableStringTable()->SetNamespace(TEXT("ST_UI_Status"));
    Asset->GetMutableStringTable()->SetSourceString(TEXT("Status.Slowed"), TEXT("Slowed"));
    Asset->GetMutableStringTable()->SetSourceString(TEXT("Status.Stunned"), TEXT("Stunned"));
    Asset->GetMutableStringTable()->SetSourceString(TEXT("Format.ContributionCount"), TEXT("{Status} x{Count}"));
    Asset->GetMutableStringTable()->SetMetaData(TEXT("Status.Slowed"), TEXT("Comment"), TEXT("Status label"));
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->GetPackage()->SetDirtyFlag(false);
    auto Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), Path);
    auto Result = UnrealMCPStringTables::Handle(TEXT("read_string_table"), Params);
    if (!TestTrue(TEXT("Read succeeded"), Result->GetBoolField(TEXT("success")))) return false;
    TestEqual(TEXT("Returned package path can be used as input"), Result->GetStringField(TEXT("asset_path")), Path);
    TestEqual(TEXT("Full object path returned separately"), Result->GetStringField(TEXT("object_path")), Asset->GetPathName());
    TestEqual(TEXT("Actual registered table ID"), Result->GetStringField(TEXT("table_id")), Asset->GetStringTableId().ToString());
    TestEqual(TEXT("Namespace"), Result->GetStringField(TEXT("namespace")), FString(TEXT("ST_UI_Status")));
    const auto Entries = Result->GetArrayField(TEXT("entries"));
    TestEqual(TEXT("Every source entry returned"), Entries.Num(), 3);
    TestEqual(TEXT("Format placeholders preserved"), Entries[0]->AsObject()->GetStringField(TEXT("source_string")), FString(TEXT("{Status} x{Count}")));
    TestEqual(TEXT("Metadata returned"), Entries[1]->AsObject()->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("Comment")), FString(TEXT("Status label")));
    TestFalse(TEXT("Read does not dirty asset"), Asset->GetPackage()->IsDirty());
    Params->SetBoolField(TEXT("unknown"), true);
    TestFalse(TEXT("Unknown arguments refused"), UnrealMCPStringTables::Handle(TEXT("read_string_table"), Params)->GetBoolField(TEXT("success")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStringTableWriteTest, "UnrealMCP.StringTables.Write",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStringTableWriteTest::RunTest(const FString& Parameters)
{
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPStringTableRoot="), Root))
        Root = TEXT("/Game/__Dev/StringTables_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("Only isolated fixtures"), Root.StartsWith(TEXT("/Game/__Dev/StringTables_")) && FPackageName::IsValidLongPackageName(Root))) return false;
    const FString Path = Root / TEXT("ST_Status");
    auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("asset_path"), Path);
    Request->SetStringField(TEXT("namespace"), TEXT("ST_UI_Status"));
    auto Preview = UnrealMCPStringTables::Handle(TEXT("create_string_table"), Request);
    if (!TestTrue(TEXT("Create preview"), Preview->GetBoolField(TEXT("success")))) return false;
    TestFalse(TEXT("Preview creates no package"), FindPackage(nullptr, *Path) != nullptr);
    Request->SetBoolField(TEXT("dry_run"), false); Request->SetBoolField(TEXT("save"), true);
    auto Created = UnrealMCPStringTables::Handle(TEXT("create_string_table"), Request);
    if (!TestTrue(TEXT("Create actual table"), Created->GetBoolField(TEXT("success")))) return false;
    TestTrue(TEXT("Created saved receipt"), Created->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("Refuse overwrite"), UnrealMCPStringTables::Handle(TEXT("create_string_table"), Request)->GetBoolField(TEXT("success")));
    Request->RemoveField(TEXT("namespace")); Request->SetStringField(TEXT("expected_namespace"), TEXT("ST_UI_Status"));
    auto SetEntries = [&](const FString& Json)
    {
        TArray<TSharedPtr<FJsonValue>> Entries;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Entries);
        Request->SetArrayField(TEXT("entries"), Entries);
        return UnrealMCPStringTables::Handle(TEXT("update_string_table_entries"), Request);
    };
    auto Updated = SetEntries(TEXT("[{\"key\":\"Status.Slowed\",\"source_string\":\"Slowed\",\"metadata\":{\"Comment\":\"Status label\",\"Context\":\"HUD\"}},")
        TEXT("{\"key\":\"Status.Stunned\",\"source_string\":\"Stunned\"},{\"key\":\"Format.ContributionCount\",\"source_string\":\"{Status} x{Count}\"}]"));
    if (!TestTrue(TEXT("Populate three source strings"), Updated->GetBoolField(TEXT("success")))) return false;
    auto Asset = CastChecked<UStringTable>(UEditorAssetLibrary::LoadAsset(Path));
    const auto Original = ReadEntries(Asset->GetStringTable());
    TestFalse(TEXT("Duplicate keys rejected as a whole"), SetEntries(TEXT("[{\"key\":\"Status.Slowed\",\"source_string\":\"Wrong\"},{\"key\":\"Status.Slowed\",\"source_string\":\"Duplicate\"}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Duplicate failure leaves all sources unchanged"), ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Original));
    Request->SetStringField(TEXT("expected_namespace"), TEXT("WrongNamespace"));
    TestFalse(TEXT("Identity conflict rejected"), SetEntries(TEXT("[]"))->GetBoolField(TEXT("success")));
    Request->SetStringField(TEXT("expected_namespace"), TEXT("ST_UI_Status"));
    Asset->MarkPackageDirty();
    TestFalse(TEXT("Preexisting unsaved edits protected even with save=false"), SetEntries(TEXT("[{\"key\":\"Status.Slowed\",\"source_string\":\"Wrong\"}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Dirty rejection does not replace content"), ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Original));
    Asset->GetPackage()->SetDirtyFlag(false);
    Request->SetBoolField(TEXT("save"), false);
    TestTrue(TEXT("Metadata patch succeeds"), SetEntries(TEXT("[{\"key\":\"Status.Slowed\",\"metadata\":{\"Comment\":null}}]"))->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Unspecified metadata preserved"), Asset->GetStringTable()->GetMetaData(TEXT("Status.Slowed"), TEXT("Context")), FString(TEXT("HUD")));
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores original sources and metadata"), ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Original));
    Asset->GetPackage()->SetDirtyFlag(false);
    Request->RemoveField(TEXT("entries"));
    const FString Csv = FPaths::ProjectSavedDir() / TEXT("StringTable_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".csv");
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*Csv); };
    Request->SetStringField(TEXT("csv_path"), FPaths::ConvertRelativePathToFull(Csv)); Request->SetStringField(TEXT("mode"), TEXT("merge"));
    FFileHelper::SaveStringToFile(TEXT("Key,SourceString,Comment\nStatus.Slowed,Wrong,x\nStatus.Slowed,Duplicate,y\n"), *Csv);
    TestFalse(TEXT("Duplicate CSV keys rejected before native import"), UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request)->GetBoolField(TEXT("success")));
    FFileHelper::SaveStringToFile(TEXT("Key,SourceString\nStatus.Slowed\n"), *Csv);
    TestFalse(TEXT("Malformed CSV row rejected as a whole"), UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request)->GetBoolField(TEXT("success")));
    FFileHelper::SaveStringToFile(TEXT("Key,SourceString\nStatus.Slowed,\"Unclosed\n"), *Csv);
    TestFalse(TEXT("Unclosed CSV quotes rejected"), UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request)->GetBoolField(TEXT("success")));
    FFileHelper::SaveStringToFile(TEXT("Key,SourceString,comment,Comment\nStatus.Slowed,Wrong,x,y\n"), *Csv);
    TestFalse(TEXT("Duplicate metadata CSV headers rejected"), UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Failed CSV does not dirty or partially edit"), !Asset->GetPackage()->IsDirty() && ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Original));
    FFileHelper::SaveStringToFile(TEXT("Key,SourceString,Comment\nStatus.Slowed,Slowed,Updated context\n"), *Csv);
    auto Imported = UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request);
    if (!TestTrue(TEXT("Native CSV merge"), Imported->GetBoolField(TEXT("success")))) return false;
    TestEqual(TEXT("Merge retains unspecified keys"), ReadEntries(Asset->GetStringTable()).Num(), 3);
    TestEqual(TEXT("Merge retains unspecified metadata"), Asset->GetStringTable()->GetMetaData(TEXT("Status.Slowed"), TEXT("Context")), FString(TEXT("HUD")));
    GEditor->UndoTransaction(); Asset->GetPackage()->SetDirtyFlag(false);
    Request->SetStringField(TEXT("mode"), TEXT("replace")); Request->SetBoolField(TEXT("dry_run"), true);
    Preview = UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request);
    TestEqual(TEXT("Replace previews exact deletions"), Preview->GetArrayField(TEXT("deleted_keys")).Num(), 2);
    Request->SetBoolField(TEXT("dry_run"), false);
    TestFalse(TEXT("Replace requires explicit deletion confirmation"), UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request)->GetBoolField(TEXT("success")));
    Request->SetBoolField(TEXT("allow_delete"), true);
    TestTrue(TEXT("Explicit replacement succeeds"), UnrealMCPStringTables::Handle(TEXT("import_string_table_csv"), Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Replacement removes unspecified keys"), ReadEntries(Asset->GetStringTable()).Num(), 1);
    GEditor->UndoTransaction(); Asset->GetPackage()->SetDirtyFlag(false);
    TestTrue(TEXT("Fixture final table unchanged"), ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Original));
    Request->RemoveField(TEXT("csv_path")); Request->RemoveField(TEXT("mode")); Request->RemoveField(TEXT("allow_delete"));
    Request->SetBoolField(TEXT("save"), true);
    {
        TGuardValue<bool> FailSave(FailStringTableSaveForTest, true);
        auto Failed = SetEntries(TEXT("[{\"key\":\"Status.Slowed\",\"source_string\":\"Edited\"}]"));
        TestFalse(TEXT("Save failure cannot report success"), Failed->GetBoolField(TEXT("success")));
        TestTrue(TEXT("Save failure truthfully reports applied changes"), Failed->GetBoolField(TEXT("modified")));
        TestFalse(TEXT("Save failure truthfully reports unsaved"), Failed->GetBoolField(TEXT("saved")));
        TestEqual(TEXT("Save failure stage"), Failed->GetStringField(TEXT("stage")), FString(TEXT("save")));
    }
    GEditor->UndoTransaction(); Asset->GetPackage()->SetDirtyFlag(false);
    TestTrue(TEXT("Recovery preserves initial fixture"), ReadEntries(Asset->GetStringTable()).OrderIndependentCompareEqual(Original));
    TestTrue(TEXT("Explicit final fixture save"), UEditorAssetLibrary::SaveLoadedAsset(Asset, false));
    const FString ReferencePath = Root / TEXT("WBP_Reference");
    auto Factory = NewObject<UWidgetBlueprintFactory>();
    auto Reference = CastChecked<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(*ReferencePath), TEXT("WBP_Reference"), RF_Public | RF_Standalone, nullptr, GWarn));
    auto Label = Reference->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusLabel"));
    Reference->WidgetTree->RootWidget = Label;
    Label->SetText(FText::FromStringTable(Asset->GetStringTableId(), TEXT("Status.Slowed")));
    FKismetEditorUtilities::CompileBlueprint(Reference);
    FAssetRegistryModule::AssetCreated(Reference); Reference->MarkPackageDirty();
    TestEqual(TEXT("Real String Table FText resolves source"), Label->GetText().ToString(), FString(TEXT("Slowed")));
    TestTrue(TEXT("Reference asset saved for actual Gather"), UEditorAssetLibrary::SaveLoadedAsset(Reference, false));
    AddInfo(TEXT("String Table localization fixture: ") + Root);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStringTableReloadTest, "UnrealMCP.StringTables.Reload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStringTableReloadTest::RunTest(const FString& Parameters)
{
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPStringTableRoot="), Root)) { AddInfo(TEXT("Requires the Write fixture's -MCPStringTableRoot")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/StringTables_")) || !FPackageName::IsValidLongPackageName(Root)) return false;
    auto Asset = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(Root / TEXT("ST_Status")));
    auto Reference = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(Root / TEXT("WBP_Reference")));
    if (!TestNotNull(TEXT("Cold String Table"), Asset) || !TestNotNull(TEXT("Cold FText reference"), Reference)) return false;
    const auto Entries = ReadEntries(Asset->GetStringTable());
    TestEqual(TEXT("Cold Namespace"), Asset->GetStringTable()->GetNamespace(), FString(TEXT("ST_UI_Status")));
    if (!TestEqual(TEXT("Cold full entry count"), Entries.Num(), 3)) return false;
    TestEqual(TEXT("Cold Slowed"), Entries[TEXT("Status.Slowed")].Source, FString(TEXT("Slowed")));
    TestEqual(TEXT("Cold Stunned"), Entries[TEXT("Status.Stunned")].Source, FString(TEXT("Stunned")));
    TestEqual(TEXT("Cold placeholders"), Entries[TEXT("Format.ContributionCount")].Source, FString(TEXT("{Status} x{Count}")));
    TestEqual(TEXT("Cold metadata"), Entries[TEXT("Status.Slowed")].Metadata[TEXT("Comment")], FString(TEXT("Status label")));
    auto Label = Cast<UTextBlock>(Reference->WidgetTree->RootWidget);
    if (!TestNotNull(TEXT("Actual TextBlock reference"), Label)) return false;
    TestEqual(TEXT("Cold FText reference resolves"), Label->GetText().ToString(), FString(TEXT("Slowed")));
    TestFalse(TEXT("Cold read leaves table clean"), Asset->GetPackage()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStringTableLocalizationTest, "UnrealMCP.StringTables.Localization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStringTableLocalizationTest::RunTest(const FString& Parameters)
{
    FString Root, Directory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPStringTableRoot="), Root)
        || !FParse::Value(FCommandLine::Get(), TEXT("MCPStringTableLocalization="), Directory))
    { AddInfo(TEXT("Requires saved fixture and compiled en/zh-Hans resources")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/StringTables_")) || !FPackageName::IsValidLongPackageName(Root)) return false;
    auto Asset = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(Root / TEXT("ST_Status")));
    auto Reference = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(Root / TEXT("WBP_Reference")));
    if (!TestNotNull(TEXT("Localized table"), Asset) || !TestNotNull(TEXT("Localized Widget Blueprint"), Reference)) return false;
    auto Label = Cast<UTextBlock>(Reference->WidgetTree->RootWidget);
    if (!TestNotNull(TEXT("TextBlock contains real FText reference"), Label)) return false;
    auto& Internationalization = FInternationalization::Get();
    const FString OriginalCulture = Internationalization.GetCurrentCulture()->GetName();
    ON_SCOPE_EXIT { Internationalization.SetCurrentCulture(OriginalCulture); };
    for (const FString Culture : {TEXT("en"), TEXT("zh-Hans"), TEXT("en")})
    {
        const FString Resource = Directory / Culture / TEXT("MCPStringTable.locres");
        if (!TestTrue(TEXT("Compiled resource exists: ") + Culture, FPaths::FileExists(Resource))) return false;
        TestTrue(TEXT("Culture change: ") + Culture, Internationalization.SetCurrentCulture(Culture));
        FTextLocalizationManager::Get().UpdateFromLocalizationResource(Resource);
        const bool Chinese = Culture == TEXT("zh-Hans");
        const FString Expected = Chinese ? TEXT("\u51cf\u901f") : TEXT("Slowed");
        TestEqual(TEXT("Existing Widget FText switches language: ") + Culture, Label->GetText().ToString(), Expected);
        TestEqual(TEXT("Stunned translation: ") + Culture, FText::FromStringTable(Asset->GetStringTableId(), TEXT("Status.Stunned")).ToString(),
            Chinese ? FString(TEXT("\u7729\u6655")) : FString(TEXT("Stunned")));
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Status"), Label->GetText()); Arguments.Add(TEXT("Count"), FText::AsNumber(2));
        TestEqual(TEXT("Translated format retains named placeholders: ") + Culture,
            FText::Format(FText::FromStringTable(Asset->GetStringTableId(), TEXT("Format.ContributionCount")), Arguments).ToString(), Expected + TEXT(" x2"));
    }
    TestEqual(TEXT("Localization never rewrites English source"), ReadEntries(Asset->GetStringTable())[TEXT("Status.Slowed")].Source, FString(TEXT("Slowed")));
    TestFalse(TEXT("Language switching leaves table clean"), Asset->GetPackage()->IsDirty());
    return !HasAnyErrors();
}
#endif
