#include "Tests/MCPAssetPropertyTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Commands/UnrealMCPAssetProperties.h"
#include "MCPRequestBuffer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Editor.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetTextReadTest, "UnrealMCP.AssetProperties.TextRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetTextReadTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/__Dev/AssetTextRead_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Asset = NewObject<UMCPAssetPropertyTestAsset>(CreatePackage(*(Root + TEXT("/DA_Test"))), TEXT("DA_Test"), RF_Public | RF_Standalone | RF_Transactional);
    auto* Table = NewObject<UStringTable>(CreatePackage(*(Root + TEXT("/ST_Test"))), TEXT("ST_Test"), RF_Public | RF_Standalone);
    auto* Other = NewObject<UStringTable>(CreatePackage(*(Root + TEXT("/ST_Other"))), TEXT("ST_Other"), RF_Public | RF_Standalone);
    Table->GetMutableStringTable()->SetNamespace(TEXT("MCPTextRead")); Other->GetMutableStringTable()->SetNamespace(TEXT("MCPTextReadOther"));
    Table->GetMutableStringTable()->SetSourceString(FTextKey(TEXT("One")), TEXT("Same")); Table->GetMutableStringTable()->SetSourceString(FTextKey(TEXT("Two")), TEXT("Same"));
    Other->GetMutableStringTable()->SetSourceString(FTextKey(TEXT("One")), TEXT("Same"));
    FAssetRegistryModule::AssetCreated(Asset); FAssetRegistryModule::AssetCreated(Table); FAssetRegistryModule::AssetCreated(Other);
    Asset->DisplayName = FText::FromStringTable(Table->GetStringTableId(), TEXT("One")); Asset->TextBranch.Name = FText::FromString(TEXT("LOCTABLE(literal)"));
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
    Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(TEXT("DisplayName")), MakeShared<FJsonValueString>(TEXT("TextBranch.Name"))});
    const bool Dirty = Asset->GetPackage()->IsDirty(); const auto Before = UnrealMCPAssetProperties::ReadText(Params);
    if (!TestTrue(TEXT("Root/nested FText identity reader"), Before->GetBoolField(TEXT("success")))) { AddError(Before->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Two selected fields"), Before->GetArrayField(TEXT("fields")).Num(), 2);
    const auto Identity = Before->GetArrayField(TEXT("fields"))[0]->AsObject()->GetObjectField(TEXT("identity"));
    TestEqual(TEXT("Real StringTable identity"), Identity->GetStringField(TEXT("kind")), FString(TEXT("string_table")));
    TestEqual(TEXT("Exact native Table ID"), Identity->GetStringField(TEXT("table_id")), Table->GetStringTableId().ToString());
    TestEqual(TEXT("Exact key"), Identity->GetStringField(TEXT("key")), FString(TEXT("One")));
    TestEqual(TEXT("Read preserves dirty"), Asset->GetPackage()->IsDirty(), Dirty);
    TestEqual(TEXT("Read baseline stable"), UnrealMCPAssetProperties::ReadText(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Asset->DisplayName = FText::FromStringTable(Table->GetStringTableId(), TEXT("Two"));
    TestEqual(TEXT("Same visible text"), Asset->DisplayName.ToString(), FString(TEXT("Same")));
    TestNotEqual(TEXT("Different key differs despite same display"), UnrealMCPAssetProperties::ReadText(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Asset->DisplayName = FText::FromStringTable(Other->GetStringTableId(), TEXT("One"));
    TestNotEqual(TEXT("Different table differs despite same key/display"), UnrealMCPAssetProperties::ReadText(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    FString Error; auto* Property = FindFProperty<FTextProperty>(Asset->GetClass(), TEXT("DisplayName"));
    const auto Legacy = UnrealMCPAssetProperties::EncodeValue(Property, Property->ContainerPtrToValuePtr<void>(Asset), Error);
    TestEqual(TEXT("Legacy plain display encoding unchanged"), Legacy->AsString(), FString(TEXT("Same")));
    Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(TEXT("Stats.Health"))});
    TestFalse(TEXT("Non FText rejected"), UnrealMCPAssetProperties::ReadText(Params)->GetBoolField(TEXT("success")));
    Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(TEXT("Texture.Name"))});
    TestFalse(TEXT("Object boundary retained"), UnrealMCPAssetProperties::ReadText(Params)->GetBoolField(TEXT("success")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetTextBindTest, "UnrealMCP.AssetProperties.TextBind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetTextBindTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/__Dev/AssetText_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Asset = NewObject<UMCPAssetPropertyTestAsset>(CreatePackage(*(Root / TEXT("DA_Test"))), TEXT("DA_Test"), RF_Public | RF_Standalone | RF_Transactional);
    auto* Table = NewObject<UStringTable>(CreatePackage(*(Root / TEXT("ST_Test"))), TEXT("ST_Test"), RF_Public | RF_Standalone | RF_Transactional);
    auto* Other = NewObject<UStringTable>(CreatePackage(*(Root / TEXT("ST_Other"))), TEXT("ST_Other"), RF_Public | RF_Standalone);
    Table->GetMutableStringTable()->SetNamespace(TEXT("MCPAssetText")); Other->GetMutableStringTable()->SetNamespace(TEXT("MCPAssetTextOther"));
    for (const TCHAR* Key : {TEXT("One"), TEXT("Two")}) { Table->GetMutableStringTable()->SetSourceString(FTextKey(Key), TEXT("Slowed")); Other->GetMutableStringTable()->SetSourceString(FTextKey(Key), TEXT("Slowed")); }
    Table->GetMutableStringTable()->SetSourceString(FTextKey(TEXT("Description")), TEXT("{Status} x{Count}"));
    FAssetRegistryModule::AssetCreated(Asset); FAssetRegistryModule::AssetCreated(Table); FAssetRegistryModule::AssetCreated(Other);
    Asset->TextBranch.Cost = 17; Asset->Stats.Health = 123; Asset->DisplayName = FText::FromString(TEXT("LOCTABLE(literal)"));
    for (UObject* Target : {static_cast<UObject*>(Asset), static_cast<UObject*>(Table), static_cast<UObject*>(Other)})
    { Target->MarkPackageDirty(); if (!TestTrue(TEXT("Saved isolated fixture"), UEditorAssetLibrary::SaveLoadedAsset(Target, false))) return false; }
    const FString File = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString TableFile = FPackageName::LongPackageNameToFilename(Table->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
    const auto TableHash = FMD5Hash::HashFile(*TableFile);
    auto ReadParams = MakeShared<FJsonObject>(); ReadParams->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
    ReadParams->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(TEXT("DisplayName")), MakeShared<FJsonValueString>(TEXT("TextBranch.Name")), MakeShared<FJsonValueString>(TEXT("TextBranch.Description"))});
    auto Read = [&]() { return UnrealMCPAssetProperties::ReadText(ReadParams); };
    const auto Before = Read();
    auto Binding = [&](const FString& Path, const FString& Key)
    {
        auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("path"), Path); Row->SetStringField(TEXT("table_path"), Table->GetPackage()->GetName());
        Row->SetStringField(TEXT("key"), Key); Row->SetStringField(TEXT("expected_table_id"), Table->GetStringTableId().ToString()); Row->SetStringField(TEXT("expected_namespace"), TEXT("MCPAssetText")); return Row;
    };
    auto Name = Binding(TEXT("DisplayName"), TEXT("One")); auto Branch = Binding(TEXT("TextBranch.Name"), TEXT("Two")); auto Description = Binding(TEXT("TextBranch.Description"), TEXT("Description"));
    auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName()); Request->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    Request->SetArrayField(TEXT("bindings"), {MakeShared<FJsonValueObject>(Name), MakeShared<FJsonValueObject>(Branch), MakeShared<FJsonValueObject>(Description)});
    auto Apply = [&]() { return UnrealMCPAssetProperties::BindText(Request); };
    auto Preview = Apply();
    if (!TestTrue(TEXT("Default preview accepted"), Preview->GetBoolField(TEXT("success")))) { AddError(Preview->GetStringField(TEXT("error"))); return false; }
    TestTrue(TEXT("Default dry run"), Preview->GetBoolField(TEXT("dry_run"))); TestTrue(TEXT("Would bind three fields"), Preview->GetBoolField(TEXT("would_modify")));
    TestFalse(TEXT("Preview no mutation"), Preview->GetBoolField(TEXT("modified"))); TestFalse(TEXT("Preview no save"), Preview->GetBoolField(TEXT("saved")));
    Request->SetBoolField(TEXT("dry_run"), false);
    auto Reject = [&](const FString& Label)
    {
        auto Result = Apply(); TestFalse(Label, Result->GetBoolField(TEXT("success"))); TestFalse(Label + TEXT(" no mutation"), Result->GetBoolField(TEXT("modified")));
        TestFalse(Label + TEXT(" no save"), Result->GetBoolField(TEXT("saved"))); TestEqual(Label + TEXT(" target unchanged"), Asset->DisplayName.ToString(), FString(TEXT("LOCTABLE(literal)")));
    };
    Description->SetStringField(TEXT("key"), TEXT("Missing")); Reject(TEXT("Invalid last row atomic")); Description->SetStringField(TEXT("key"), TEXT("Description"));
    Name->SetStringField(TEXT("expected_namespace"), TEXT("Wrong")); Reject(TEXT("Namespace")); Name->SetStringField(TEXT("expected_namespace"), TEXT("MCPAssetText"));
    Name->SetStringField(TEXT("expected_table_id"), TEXT("Wrong")); Reject(TEXT("Table ID")); Name->SetStringField(TEXT("expected_table_id"), Table->GetStringTableId().ToString());
    Name->SetStringField(TEXT("table_path"), Asset->GetPackage()->GetName()); Reject(TEXT("Wrong table class")); Name->SetStringField(TEXT("table_path"), Root / TEXT("ST_Missing")); Reject(TEXT("Missing table")); Name->SetStringField(TEXT("table_path"), Table->GetPackage()->GetName());
    for (const TCHAR* Path : {TEXT("Stats.Health"), TEXT("Texture.Name"), TEXT("Missing"), TEXT("TextBranch.Name")})
    { Name->SetStringField(TEXT("path"), Path); Reject(TEXT("Invalid/duplicate path")); } Name->SetStringField(TEXT("path"), TEXT("DisplayName"));
    Table->GetPackage()->SetDirtyFlag(true); Reject(TEXT("Dirty table")); Table->GetPackage()->SetDirtyFlag(false);
    Asset->GetPackage()->SetDirtyFlag(true); Reject(TEXT("Dirty target even no-save")); Asset->GetPackage()->SetDirtyFlag(false);
    Request->SetStringField(TEXT("expected_baseline"), FString::ChrN(40, '0')); Reject(TEXT("Stale")); Request->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    auto Applied = Apply(); if (!TestTrue(TEXT("Three native references applied"), Applied->GetBoolField(TEXT("success")))) { AddError(Applied->GetStringField(TEXT("error"))); return false; }
    TestTrue(TEXT("Modified true"), Applied->GetBoolField(TEXT("modified"))); TestFalse(TEXT("Default save false"), Applied->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Sibling cost"), Asset->TextBranch.Cost, 17); TestEqual(TEXT("Unrelated root"), Asset->Stats.Health, 123);
    FName Id; FString Key;
    TestTrue(TEXT("Root native history"), FTextInspector::GetTableIdAndKey(Asset->DisplayName, Id, Key)); TestEqual(TEXT("Root key"), Key, FString(TEXT("One")));
    TestTrue(TEXT("Nested name native history"), FTextInspector::GetTableIdAndKey(Asset->TextBranch.Name, Id, Key)); TestEqual(TEXT("Nested key"), Key, FString(TEXT("Two")));
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo full batch"), Asset->DisplayName.ToString(), FString(TEXT("LOCTABLE(literal)"))); TestTrue(TEXT("Undo nested"), Asset->TextBranch.Name.IsEmpty());
    GEditor->RedoTransaction(); TestTrue(TEXT("Redo native history"), FTextInspector::GetTableIdAndKey(Asset->TextBranch.Description, Id, Key)); TestEqual(TEXT("Redo key"), Key, FString(TEXT("Description")));
    TestTrue(TEXT("Save only target fixture"), UEditorAssetLibrary::SaveLoadedAsset(Asset, false));
    Request->SetStringField(TEXT("expected_baseline"), Read()->GetStringField(TEXT("baseline"))); Request->SetBoolField(TEXT("save"), true);
    auto NoOp = Apply(); TestTrue(TEXT("Same native identity succeeds"), NoOp->GetBoolField(TEXT("success"))); TestFalse(TEXT("No-op no mutation"), NoOp->GetBoolField(TEXT("modified"))); TestFalse(TEXT("No-op no save"), NoOp->GetBoolField(TEXT("saved")));
    const FString OriginalBaseline = Read()->GetStringField(TEXT("baseline"));
    Asset->DisplayName = FText::FromStringTable(Table->GetStringTableId(), TEXT("Two")); TestEqual(TEXT("Different key same display"), Asset->DisplayName.ToString(), FString(TEXT("Slowed")));
    auto StaleKey = Apply(); TestFalse(TEXT("Same display different key stale"), StaleKey->GetBoolField(TEXT("success")));
    Request->SetStringField(TEXT("expected_baseline"), Read()->GetStringField(TEXT("baseline"))); TestTrue(TEXT("Different identity would modify"), Apply()->GetBoolField(TEXT("would_modify")));
    Asset->DisplayName = FText::FromStringTable(Other->GetStringTableId(), TEXT("One")); Request->SetStringField(TEXT("expected_baseline"), OriginalBaseline);
    TestFalse(TEXT("Same key/display different table stale"), Apply()->GetBoolField(TEXT("success")));
    Request->SetStringField(TEXT("expected_baseline"), Read()->GetStringField(TEXT("baseline")));
    const auto SavedHash = FMD5Hash::HashFile(*File);
    UnrealMCPAssetProperties::SetTextSaveFailureForTests(true); ON_SCOPE_EXIT { UnrealMCPAssetProperties::SetTextSaveFailureForTests(false); };
    auto Failed = Apply(); TestFalse(TEXT("Save failure truthful"), Failed->GetBoolField(TEXT("success"))); TestTrue(TEXT("Save failure applied"), Failed->GetBoolField(TEXT("modified"))); TestFalse(TEXT("Save failure unsaved"), Failed->GetBoolField(TEXT("saved"))); TestEqual(TEXT("Save stage"), Failed->GetStringField(TEXT("stage")), FString(TEXT("save")));
    TestTrue(TEXT("Failure disk unchanged"), SavedHash == FMD5Hash::HashFile(*File)); TestTrue(TEXT("Failure includes after"), Failed->HasTypedField<EJson::Object>(TEXT("after")));
    UnrealMCPAssetProperties::SetTextSaveFailureForTests(false); TestTrue(TEXT("Explicit recovery target save"), UEditorAssetLibrary::SaveLoadedAsset(Asset, false));
    FString Error; auto* Property = FindFProperty<FTextProperty>(Asset->GetClass(), TEXT("DisplayName"));
    FText Literal; TestTrue(TEXT("Legacy string decode"), UnrealMCPAssetProperties::DecodeValue(Property, &Literal, MakeShared<FJsonValueString>(TEXT("LOCTABLE(literal)")), Error));
    TestEqual(TEXT("Literal not interpreted"), Literal.ToString(), FString(TEXT("LOCTABLE(literal)"))); TestFalse(TEXT("Literal no table history"), FTextInspector::GetTableIdAndKey(Literal, Id, Key));
    TestTrue(TEXT("Tables untouched on disk"), TableHash == FMD5Hash::HashFile(*TableFile)); TestFalse(TEXT("Table remains clean"), Table->GetPackage()->IsDirty());
    FString Json; FJsonSerializer::Serialize(Read().ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("AssetText") / FPackageName::GetLongPackageAssetName(Root) / TEXT("snapshot.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt), true);
    TestTrue(TEXT("Snapshot evidence"), FFileHelper::SaveStringToFile(Json, *Receipt));
    AddInfo(TEXT("AssetText fixture: ") + Root); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetTextReloadTest, "UnrealMCP.AssetProperties.TextReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetTextReloadTest::RunTest(const FString& Parameters)
{
    FString Root; if (!FParse::Value(FCommandLine::Get(), TEXT("MCPAssetTextRoot="), Root)) { AddInfo(TEXT("Requires explicit saved TextBind root; skipped")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/AssetText_")) || !FPackageName::IsValidLongPackageName(Root)) return false;
    TestNull(TEXT("Independent process not already loaded"), FindObject<UObject>(nullptr, *(Root / TEXT("DA_Test.DA_Test"))));
    auto* Asset = Cast<UMCPAssetPropertyTestAsset>(UEditorAssetLibrary::LoadAsset(Root / TEXT("DA_Test")));
    auto* Table = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(Root / TEXT("ST_Test")));
    if (!TestNotNull(TEXT("Cold DataAsset"), Asset) || !TestNotNull(TEXT("Cold table"), Table)) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
    Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(TEXT("DisplayName")), MakeShared<FJsonValueString>(TEXT("TextBranch.Name")), MakeShared<FJsonValueString>(TEXT("TextBranch.Description"))});
    FString PreviousJson; TSharedPtr<FJsonObject> Previous;
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("AssetText") / FPackageName::GetLongPackageAssetName(Root) / TEXT("snapshot.json");
    if (!TestTrue(TEXT("Read hot evidence"), FFileHelper::LoadFileToString(PreviousJson, *Receipt))) return false;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PreviousJson), Previous);
    const auto Current = UnrealMCPAssetProperties::ReadText(Params);
    TestEqual(TEXT("Cold baseline exact"), Current->GetStringField(TEXT("baseline")), Previous->GetStringField(TEXT("baseline")));
    TestEqual(TEXT("Cold unrelated sibling"), Asset->TextBranch.Cost, 17); TestEqual(TEXT("Cold unrelated root"), Asset->Stats.Health, 123);
    FString Directory;
    if (FParse::Value(FCommandLine::Get(), TEXT("MCPAssetTextLocalization="), Directory))
    {
        auto& Internationalization = FInternationalization::Get(); const FString Original = Internationalization.GetCurrentCulture()->GetName();
        ON_SCOPE_EXIT { Internationalization.SetCurrentCulture(Original); };
        for (const FString Culture : {TEXT("en"), TEXT("zh-Hans"), TEXT("en")})
        {
            TestTrue(TEXT("Native culture change"), Internationalization.SetCurrentCulture(Culture));
            const FString Resource = Directory / Culture / TEXT("MCPStringTable.locres"); if (!TestTrue(TEXT("Compiled locres"), FPaths::FileExists(Resource))) return false;
            FTextLocalizationManager::Get().UpdateFromLocalizationResource(Resource);
            const FString Expected = Culture == TEXT("zh-Hans") ? TEXT("\u51cf\u901f") : TEXT("Slowed");
            TestEqual(TEXT("Existing root switches language"), Asset->DisplayName.ToString(), Expected); TestEqual(TEXT("Existing nested switches language"), Asset->TextBranch.Name.ToString(), Expected);
            TestEqual(TEXT("Culture-independent identity baseline"), UnrealMCPAssetProperties::ReadText(Params)->GetStringField(TEXT("baseline")), Previous->GetStringField(TEXT("baseline")));
        }
    }
    TestFalse(TEXT("Cold target clean"), Asset->GetPackage()->IsDirty()); TestFalse(TEXT("Cold table clean"), Table->GetPackage()->IsDirty()); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetPropertyTest, "UnrealMCP.AssetProperties.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetPropertyTest::RunTest(const FString& Parameters)
{
    const FString Path = TEXT("/Game/__Dev/AssetProperties_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/DA_Test");
    UPackage* Package = CreatePackage(*Path);
    UMCPAssetPropertyTestAsset* Asset = NewObject<UMCPAssetPropertyTestAsset>(Package, TEXT("DA_Test"), RF_Public | RF_Standalone | RF_Transactional);
    Asset->Attacks.AddDefaulted();
    Asset->Numbers = {1, 2};
    FAssetRegistryModule::AssetCreated(Asset);
    Package->SetDirtyFlag(false);
    auto Apply = [&](const FString& Changes, bool Save = false)
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        const FString Json = TEXT("{\"changes\":") + Changes + TEXT("}");
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Params);
        Params->SetStringField(TEXT("asset_path"), Path);
        Params->SetBoolField(TEXT("save"), Save);
        return UnrealMCPAssetProperties::Write(Params);
    };
    auto ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), Path);
    TestTrue(TEXT("Structured reader"), UnrealMCPAssetProperties::Read(ReadParams)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Reader preserves clean package"), Package->IsDirty());
    const TArray<FString> Invalid = {
        TEXT("[{\"path\":\"Stats.Health\",\"value\":200},{\"path\":\"Byte\",\"value\":256}]"),
        TEXT("[{\"path\":\"Stats.Health\",\"value\":200,\"expected_value\":99}]"),
        TEXT("[{\"path\":\"Stats.Health\",\"value\":1.5}]"),
        TEXT("[{\"path\":\"Stats.Health\",\"value\":-1}]"),
        TEXT("[{\"path\":\"Stats.Health\",\"value\":true}]"),
        TEXT("[{\"path\":\"Stats.Health\",\"value\":101},{\"path\":\"Stats\",\"value\":{}}]"),
        TEXT("[{\"path\":\"Attacks[1].Health\",\"value\":1}]"),
        TEXT("[{\"path\":\"Attacks[00].Health\",\"value\":1}]"),
        TEXT("[{\"path\":\"ReadOnly\",\"value\":1}]"),
        TEXT("[{\"path\":\"UnsupportedMap\",\"value\":{}}]"),
        TEXT("[{\"path\":\"ActorClass\",\"value\":\"/Script/Engine.Texture2D\"}]"),
        TEXT("[{\"path\":\"SoftActorClass\",\"value\":\"/Script/Engine.Texture2D\"}]"),
        TEXT("[{\"path\":\"Texture\",\"value\":\"/Script/Engine.Actor\"}]"),
        TEXT("[{\"path\":\"Texture.SRGB\",\"value\":true}]"),
        TEXT("[{\"path\":\"Tag\",\"value\":\"UnrealMCP.DoesNotExist\"}]"),
        TEXT("[{\"path\":\"Input\",\"value\":\"InvalidEnum\"}]"),
        TEXT("[{\"path\":\"Scale\",\"value\":1e100}]")
    };
    for (const FString& Changes : Invalid)
    {
        const auto Result = Apply(Changes);
        TestFalse(*Changes, Result->GetBoolField(TEXT("success")));
        TestFalse(TEXT("Rejected patch did not modify"), Result->GetBoolField(TEXT("modified")));
        TestEqual(TEXT("Batch prevalidation preserves first property"), Asset->Stats.Health, 100);
        TestFalse(TEXT("Rejection preserves dirty state"), Package->IsDirty());
    }
    auto Result = Apply(TEXT("[{\"path\":\"Stats.Health\",\"value\":150,\"expected_value\":100},"
        "{\"path\":\"Stats.Offset.X\",\"value\":10},{\"path\":\"Attacks[0].Health\",\"value\":30},"
        "{\"path\":\"Numbers\",\"value\":[3,4,5]},{\"path\":\"ActorClass\",\"value\":\"/Script/Engine.Actor\"},"
        "{\"path\":\"SoftActorClass\",\"value\":\"/Script/Engine.Actor\"},"
        "{\"path\":\"Texture\",\"value\":\"/Engine/EngineResources/DefaultTexture.DefaultTexture\"},"
        "{\"path\":\"SoftTexture\",\"value\":\"/Engine/EngineResources/DefaultTexture.DefaultTexture\"},"
        "{\"path\":\"Tag\",\"value\":\"UnrealMCP.Automation.AssetProperty\"},"
        "{\"path\":\"Tags\",\"value\":[\"UnrealMCP.Automation.AssetProperty\"]},"
        "{\"path\":\"Input\",\"value\":\"Player0\"},{\"path\":\"Enabled\",\"value\":false}]"));
    TestTrue(TEXT("Supported batch"), Result->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Nested health"), Asset->Stats.Health, 150);
    TestEqual(TEXT("Sibling preserved"), Asset->Stats.Offset.Y, 0.0);
    TestEqual(TEXT("Nested array element"), Asset->Attacks[0].Health, 30);
    TestEqual(TEXT("Array replaced"), Asset->Numbers.Num(), 3);
    TestEqual(TEXT("Class checked"), Asset->ActorClass.Get(), AActor::StaticClass());
    TestTrue(TEXT("Hard asset reference"), Asset->Texture != nullptr);
    TestTrue(TEXT("Soft asset reference"), !Asset->SoftTexture.IsNull());
    TestEqual(TEXT("Tag set"), Asset->Tag.ToString(), FString(TEXT("UnrealMCP.Automation.AssetProperty")));
    TestTrue(TEXT("Unsaved edits dirty"), Package->IsDirty());
    TestFalse(TEXT("Dirty package save refused"), Apply(TEXT("[{\"path\":\"Stats.Health\",\"value\":160}]"), true)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Dirty rejection before mutation"), Asset->Stats.Health, 150);
    TestTrue(TEXT("Clear references"), Apply(TEXT("[{\"path\":\"Texture\",\"value\":null},{\"path\":\"SoftTexture\",\"value\":null}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Hard ref cleared"), Asset->Texture == nullptr);
    TestTrue(TEXT("Soft ref cleared"), Asset->SoftTexture.IsNull());
    TestFalse(TEXT("No-op is not modified"), Apply(TEXT("[{\"path\":\"Stats.Health\",\"value\":150}]"))->GetBoolField(TEXT("modified")));
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetEditConditionTest, "UnrealMCP.AssetProperties.EditConditions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetEditConditionTest::RunTest(const FString& Parameters)
{
    const FString Path = TEXT("/Game/__Dev/AssetProperties_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/DA_Conditions");
    UPackage* Package = CreatePackage(*Path);
    auto Asset = NewObject<UMCPAssetPropertyTestAsset>(Package, TEXT("DA_Conditions"), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Asset);
    Package->SetDirtyFlag(false);
    auto ReadField = [&](const FString& FieldPath = TEXT("BulletHits.HeadRootBones"))
    {
        auto Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), Path);
        Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(FieldPath)});
        return UnrealMCPAssetProperties::Read(Params)->GetArrayField(TEXT("properties"))[0]->AsObject();
    };
    auto Apply = [&](const FString& Changes)
    {
        TSharedPtr<FJsonObject> Params;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"save\":false,\"changes\":") + Changes + TEXT("}")), Params);
        Params->SetStringField(TEXT("asset_path"), Path);
        return UnrealMCPAssetProperties::Write(Params);
    };
    auto WriteBones = [&]() { return Apply(TEXT("[{\"path\":\"BulletHits.HeadRootBones\",\"value\":[\"head\"]}]")); };
    TestFalse(TEXT("Disabled condition is not writable"), ReadField()->GetBoolField(TEXT("writable")));
    TestTrue(TEXT("Disabled value remains readable"), ReadField()->HasField(TEXT("value")));
    TestTrue(TEXT("Disabled reason names its controller"), ReadField()->GetStringField(TEXT("reason")).Contains(TEXT("bEnabled")));
    TestEqual(TEXT("Condition metadata exposed"), ReadField()->GetObjectField(TEXT("schema"))->GetStringField(TEXT("EditCondition")), FString(TEXT("bEnabled")));
    TestTrue(TEXT("Asset default-only metadata exposed"), ReadField(TEXT("BulletHits"))->GetObjectField(TEXT("schema"))->GetBoolField(TEXT("edit_defaults_only")));
    TestFalse(TEXT("Disabled condition rejects write"), WriteBones()->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Rejected write preserves dirty state"), Package->IsDirty());
    Asset->BulletHits.bEnabled = true;
    TestTrue(TEXT("Enabled condition under EditDefaultsOnly asset root is writable"), ReadField()->GetBoolField(TEXT("writable")));
    TestTrue(TEXT("Enabled condition writes array"), WriteBones()->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Head root set"), Asset->BulletHits.HeadRootBones == TArray<FName>{TEXT("head")});
    TestFalse(TEXT("VisibleAnywhere remains read-only"), Apply(TEXT("[{\"path\":\"ReadOnly\",\"value\":10}]"))->GetBoolField(TEXT("success")));

    for (bool ToggleFirst : {true, false})
    {
        Asset->BulletHits = FMCPAssetPropertyTestBulletHits();
        Package->SetDirtyFlag(false);
        const FString Toggle = TEXT("{\"path\":\"BulletHits.bEnabled\",\"value\":true}");
        const FString Bones = TEXT("{\"path\":\"BulletHits.HeadRootBones\",\"value\":[\"head\"],\"expected_value\":[]}");
        TestTrue(TEXT("Explicit enable plus edit is order independent"), Apply(TEXT("[") + (ToggleFirst ? Toggle + TEXT(",") + Bones : Bones + TEXT(",") + Toggle) + TEXT("]"))->GetBoolField(TEXT("success")));
        TestTrue(TEXT("Enabled and populated atomically"), Asset->BulletHits.bEnabled && Asset->BulletHits.HeadRootBones == TArray<FName>{TEXT("head")});
        Package->SetDirtyFlag(false);
        const auto Rejected = Apply(TEXT("[{\"path\":\"BulletHits.HeadRootBones\",\"value\":[\"other\"]},{\"path\":\"BulletHits.bEnabled\",\"value\":false}]"));
        TestFalse(TEXT("Final disabled state cannot admit conditional write"), Rejected->GetBoolField(TEXT("success")));
        TestEqual(TEXT("Condition failure stage"), Rejected->GetStringField(TEXT("stage")), FString(TEXT("edit_condition")));
        TestTrue(TEXT("Rejected batch preserves toggle and bones"), Asset->BulletHits.bEnabled && Asset->BulletHits.HeadRootBones == TArray<FName>{TEXT("head")});
        TestFalse(TEXT("Condition rejection leaves package clean"), Package->IsDirty());
    }

    TestTrue(TEXT("Whole struct may disable without altering locked child"), Apply(TEXT("[{\"path\":\"BulletHits\",\"value\":{\"bEnabled\":false,\"HeadRootBones\":[\"head\"]}}]"))->GetBoolField(TEXT("success")));
    Package->SetDirtyFlag(false);
    TestFalse(TEXT("Whole struct cannot bypass a false child condition"), Apply(TEXT("[{\"path\":\"BulletHits\",\"value\":{\"bEnabled\":false,\"HeadRootBones\":[\"other\"]}}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Whole struct can explicitly enable and edit"), Apply(TEXT("[{\"path\":\"BulletHits\",\"value\":{\"bEnabled\":true,\"HeadRootBones\":[\"other\"]}}]"))->GetBoolField(TEXT("success")));

    TestFalse(TEXT("New array element cannot bypass child condition"), Apply(TEXT("[{\"path\":\"HitGroups\",\"value\":[{\"bEnabled\":false,\"HeadRootBones\":[\"head\"]}]}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Enabled array element can be authored"), Apply(TEXT("[{\"path\":\"HitGroups\",\"value\":[{\"bEnabled\":true,\"HeadRootBones\":[\"head\"]}]}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Nested array element uses its own container"), ReadField(TEXT("HitGroups[0].HeadRootBones"))->GetBoolField(TEXT("writable")));
    Asset->HitGroups[0].bEnabled = false;
    TestFalse(TEXT("Array element own toggle enforced"), Apply(TEXT("[{\"path\":\"HitGroups[0].HeadRootBones\",\"value\":[\"other\"]}]"))->GetBoolField(TEXT("success")));

    TestFalse(TEXT("Negated condition initially false"), ReadField(TEXT("NegatedValue"))->GetBoolField(TEXT("writable")));
    TestTrue(TEXT("Cross-root pending controller and negation"), Apply(TEXT("[{\"path\":\"NegatedValue\",\"value\":42},{\"path\":\"Enabled\",\"value\":false}]"))->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Negated value changed"), Asset->NegatedValue, 42);
    TestFalse(TEXT("False parent condition blocks nested path"), Apply(TEXT("[{\"path\":\"ConditionalStats.Health\",\"value\":200}]"))->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Cross-root enable permits nested write"), Apply(TEXT("[{\"path\":\"ConditionalStats.Health\",\"value\":200},{\"path\":\"Enabled\",\"value\":true}]"))->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Parent-gated value set"), Asset->ConditionalStats.Health, 200);

    for (const FString& FieldPath : {FString(TEXT("ComplexValue")), FString(TEXT("UnknownConditionValue"))})
    {
        TestFalse(TEXT("Unsupported conditions fail closed on read"), ReadField(FieldPath)->GetBoolField(TEXT("writable")));
        TestTrue(TEXT("Unsupported condition reason is explicit"), ReadField(FieldPath)->GetStringField(TEXT("reason")).Contains(TEXT("unsupported EditCondition")));
        TestFalse(TEXT("Unsupported conditions fail closed on write"), Apply(TEXT("[{\"path\":\"") + FieldPath + TEXT("\",\"value\":1}]"))->GetBoolField(TEXT("success")));
    }
    TestTrue(TEXT("Bool controller not silently changed"), Asset->Enabled);
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetPropertyPersistenceTest, "UnrealMCP.AssetProperties.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetPropertyPersistenceTest::RunTest(const FString& Parameters)
{
    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPAssetTestPath="), Path)
        || !Path.StartsWith(TEXT("/Game/__Dev/AssetProperties_")))
    {
        AddError(TEXT("Provide a unique MCPAssetTestPath under /Game/__Dev/AssetProperties_"));
        return false;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("MCPAssetVerify")))
    {
        const auto Asset = Cast<UMCPAssetPropertyTestAsset>(UEditorAssetLibrary::LoadAsset(Path));
        if (!TestNotNull(TEXT("Reload persisted asset"), Asset)) return false;
        TestEqual(TEXT("Saved health after fresh process"), Asset->Stats.Health, 321);
        TestEqual(TEXT("Saved label after fresh process"), Asset->Label, FString(TEXT("Persisted")));
        TestFalse(TEXT("Reload is clean"), Asset->GetOutermost()->IsDirty());
        return true;
    }
    if (UEditorAssetLibrary::DoesAssetExist(Path)) { AddError(TEXT("Refusing to overwrite existing test asset")); return false; }
    UPackage* Package = CreatePackage(*Path);
    auto Asset = NewObject<UMCPAssetPropertyTestAsset>(Package, *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Asset);
    Package->SetDirtyFlag(false);
    TSharedPtr<FJsonObject> Params;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"save\":true,\"changes\":[{\"path\":\"Stats.Health\",\"value\":321},{\"path\":\"Label\",\"value\":\"Persisted\"}]}")), Params);
    Params->SetStringField(TEXT("asset_path"), Path);
    auto Result = UnrealMCPAssetProperties::Write(Params);
    TestTrue(TEXT("Save succeeded"), Result->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Saved reported truthfully"), Result->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("Saved package clean"), Package->IsDirty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPRequestBufferTest, "UnrealMCP.Transport.RequestBuffer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPRequestBufferTest::RunTest(const FString& Parameters)
{
    const FString Label = FString::ChrN(24000, TEXT('x')) + FString::Chr(0x4e2d);
    TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("type"), TEXT("set_asset_properties"));
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("label"), Label);
    Request->SetObjectField(TEXT("params"), Params);
    FString Json;
    FJsonSerializer::Serialize(Request.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
    FTCHARToUTF8 Encoded(*Json);
    for (int32 ChunkSize : {1, 8192, Encoded.Length()})
    {
        FMCPRequestBuffer Buffer;
        TSharedPtr<FJsonObject> Parsed;
        for (int32 Offset = 0; Offset < Encoded.Length(); Offset += ChunkSize)
        {
            const int32 Count = FMath::Min(ChunkSize, Encoded.Length() - Offset);
            auto Status = Buffer.Append(reinterpret_cast<const uint8*>(Encoded.Get()) + Offset, Count, Parsed);
            if (Offset + Count < Encoded.Length()) TestTrue(TEXT("Incomplete fragments never dispatch"), Status == FMCPRequestBuffer::EResult::Incomplete);
            else TestTrue(TEXT("Final fragment completes exactly once"), Status == FMCPRequestBuffer::EResult::Complete);
        }
        if (!TestTrue(TEXT("Parsed request"), Parsed.IsValid())) return false;
        TestEqual(TEXT("UTF8 and large payload preserved"), Parsed->GetObjectField(TEXT("params"))->GetStringField(TEXT("label")), Label);
    }
    FMCPRequestBuffer Buffer;
    TSharedPtr<FJsonObject> Parsed;
    TestTrue(TEXT("Oversized request rejected before reading bytes"),
        Buffer.Append(nullptr, FMCPRequestBuffer::MaxBytes + 1, Parsed) == FMCPRequestBuffer::EResult::TooLarge);
    const FString EscapedJson = TEXT("{\"type\":\"ping\",\"params\":{\"unicode\":\"\\u4e2d\\ud83d\\ude00\",\"escaped\":\"braces {}[] slash \\\\ quote \\\"\"}}");
    FTCHARToUTF8 EscapedBytes(*EscapedJson);
    for (int32 Split = 1; Split < EscapedBytes.Length(); ++Split)
    {
        FMCPRequestBuffer SplitBuffer;
        TSharedPtr<FJsonObject> SplitRequest;
        const uint8* Data = reinterpret_cast<const uint8*>(EscapedBytes.Get());
        TestTrue(TEXT("Every escaped-string prefix stays unparsed"),
            SplitBuffer.Append(Data, Split, SplitRequest) == FMCPRequestBuffer::EResult::Incomplete);
        TestFalse(TEXT("No partial request escapes"), SplitRequest.IsValid());
        TestTrue(TEXT("Remaining escaped bytes complete the request"),
            SplitBuffer.Append(Data + Split, EscapedBytes.Length() - Split, SplitRequest) == FMCPRequestBuffer::EResult::Complete);
        if (!TestTrue(TEXT("Escaped request parsed"), SplitRequest.IsValid())) return false;
        TestTrue(TEXT("Unicode escape decoded"), SplitRequest->GetObjectField(TEXT("params"))->GetStringField(TEXT("unicode")).StartsWith(FString::Chr(0x4e2d)));
    }
    for (const FString& Invalid : {FString(TEXT("{\"params\":[]]}")), FString(TEXT("{}{}")),
                                   FString(TEXT("{\"params\":not_json}")), FString(TEXT("[]"))})
    {
        FMCPRequestBuffer InvalidBuffer;
        FTCHARToUTF8 InvalidBytes(*Invalid);
        TestTrue(TEXT("Malformed envelopes rejected"), InvalidBuffer.Append(
            reinterpret_cast<const uint8*>(InvalidBytes.Get()), InvalidBytes.Length(), Parsed) == FMCPRequestBuffer::EResult::Invalid);
        TestFalse(TEXT("Malformed request not exposed"), Parsed.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetBlueprintPersistenceTest, "UnrealMCP.AssetProperties.BlueprintPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetBlueprintPersistenceTest::RunTest(const FString& Parameters)
{
    FString BasePath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPAssetTestPath="), BasePath)
        || !BasePath.StartsWith(TEXT("/Game/__Dev/AssetProperties_")))
    { AddError(TEXT("Provide a unique MCPAssetTestPath under /Game/__Dev/AssetProperties_")); return false; }
    const FString Path = BasePath + TEXT("_BPInstance");
    if (FParse::Param(FCommandLine::Get(), TEXT("MCPAssetVerify")))
    {
        auto Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), Path);
        Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(TEXT("Health"))});
        const auto Result = UnrealMCPAssetProperties::Read(Params);
        if (!TestTrue(TEXT("Read saved BP DataAsset"), Result->GetBoolField(TEXT("success")))) return false;
        const auto Field = Result->GetArrayField(TEXT("properties"))[0]->AsObject();
        TestEqual(TEXT("BP instance field survived restart"), Field->GetNumberField(TEXT("value")), 456.0);
        TestFalse(TEXT("Reload preserves clean package"), Result->GetBoolField(TEXT("package_dirty")));
        return true;
    }
    const FString ClassPath = BasePath + TEXT("_BPClass");
    if (UEditorAssetLibrary::DoesAssetExist(Path) || UEditorAssetLibrary::DoesAssetExist(ClassPath))
    { AddError(TEXT("Refusing to replace BP test assets")); return false; }
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UPrimaryDataAsset::StaticClass(), CreatePackage(*ClassPath),
        *FPackageName::GetLongPackageAssetName(ClassPath), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Blueprint DataAsset class"), Blueprint)) return false;
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Int;
    FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Health"), Type);
    FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, TEXT("Health"), false);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (!TestTrue(TEXT("BP compile succeeded"), Blueprint->Status != BS_Error)) return false;
    FAssetRegistryModule::AssetCreated(Blueprint);
    if (!TestTrue(TEXT("Save BP class"), UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false))) return false;
    UObject* Asset = NewObject<UObject>(CreatePackage(*Path), Blueprint->GeneratedClass,
        *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->GetOutermost()->SetDirtyFlag(false);
    TSharedPtr<FJsonObject> Params;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"save\":true,\"changes\":[{\"path\":\"Health\",\"value\":456,\"expected_value\":0}]}")), Params);
    Params->SetStringField(TEXT("asset_path"), Path);
    const auto Result = UnrealMCPAssetProperties::Write(Params);
    TestTrue(TEXT("Write BP DataAsset instance"), Result->GetBoolField(TEXT("success")));
    TestTrue(TEXT("BP instance saved"), Result->GetBoolField(TEXT("saved")));
    const FIntProperty* Health = FindFProperty<FIntProperty>(Blueprint->GeneratedClass, TEXT("Health"));
    TestEqual(TEXT("Class defaults untouched"), Health->GetPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject()), 0);
    return true;
}
#endif