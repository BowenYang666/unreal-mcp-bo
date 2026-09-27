#include "Tests/MCPAssetPropertyTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Commands/UnrealMCPAssetMigration.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialEditingLibrary.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetMigrationRenamedSnapshotTest, "UnrealMCP.AssetMigration.RenamedReferenceSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetMigrationRenamedSnapshotTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/__Dev/AssetMigration_Rename_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Source = NewObject<UMaterialInstanceConstant>(CreatePackage(*(Root + TEXT("/Source/MI_BulletHole"))), TEXT("MI_BulletHole"));
    auto Copy = NewObject<UMaterialInstanceConstant>(CreatePackage(*(Root + TEXT("/Copy/MI_LaserImpact_BulletHole"))), TEXT("MI_LaserImpact_BulletHole"));
    auto System = NewObject<UNiagaraSystem>(CreatePackage(*(Root + TEXT("/NS_Existing"))), TEXT("NS_Existing"));
    auto Renderer = NewObject<UNiagaraSpriteRendererProperties>(System, TEXT("Renderer"));
    Renderer->Material = Source;
    auto SourceChild = NewObject<UTexture2D>(Source, TEXT("Child"));
    auto CopyChild = NewObject<UTexture2D>(Copy, TEXT("RenamedChild"));
    auto References = NewObject<UMCPAssetPropertyTestAsset>(System, TEXT("References"));
    References->Texture = SourceChild;
    References->SoftTexture = SourceChild;
    TMap<FString, FString> Packages{{Source->GetPackage()->GetName(), Copy->GetPackage()->GetName()}};
    TMap<FString, FString> ObjectPaths{{Source->GetPathName(), Copy->GetPathName()}, {Source->GetPackage()->GetName(), Copy->GetPackage()->GetName()}};
    ObjectPaths.Add(SourceChild->GetPathName(), CopyChild->GetPathName());
    ObjectPaths.Add(Copy->GetPathName(), TEXT("/Game/DoNotCascade.DoNotCascade"));
    const FString Suffix = TEXT(":UnloadedChild.Nested");
    const FString SimilarName = Source->GetPathName() + TEXT("_Other");
    References->Label = Source->GetPathName() + Suffix + TEXT(" ") + SimilarName + TEXT(" ") + Source->GetPackage()->GetName();
    const auto Expected = UnrealMCPAssetMigration::SnapshotForTests(System->GetPackage(), ObjectPaths);
    TMap<UObject*, UObject*> Objects{{Source, Copy}, {SourceChild, CopyChild}};
    TestTrue(TEXT("Existing NS reference is rebound"), UnrealMCPAssetMigration::RemapPackage(System->GetPackage(), Objects, Packages) > 0);
    TestEqual(TEXT("Renderer uses moved and renamed MI"), Renderer->Material.Get(), static_cast<UMaterialInterface*>(Copy));
    TestEqual(TEXT("Hard reference uses complete renamed child mapping"), References->Texture.Get(), CopyChild);
    TestEqual(TEXT("Soft reference uses complete renamed child mapping"), References->SoftTexture.ToSoftObjectPath(), FSoftObjectPath(CopyChild));
    References->Label = Copy->GetPathName() + Suffix + TEXT(" ") + SimilarName + TEXT(" ") + Copy->GetPackage()->GetName();
    TestTrue(TEXT("Moved and renamed MI is a reference-only change"), Expected.OrderIndependentCompareEqual(UnrealMCPAssetMigration::SnapshotForTests(System->GetPackage())));
    auto Changed = UnrealMCPAssetMigration::SnapshotForTests(System->GetPackage());
    Changed.Add(TEXT("NS_Existing:Renderer.Material"), TEXT("Unexpected material"));
    Changed.Add(TEXT("AddedField"), TEXT("new"));
    auto Before = Expected;
    Before.Add(TEXT("RemovedField"), TEXT("old"));
    auto Receipt = MakeShared<FJsonObject>();
    TestFalse(TEXT("First comparison rejects real differences"), UnrealMCPAssetMigration::CheckSnapshotForTests(Before, Changed, Receipt));
    TestEqual(TEXT("First comparison stage"), Receipt->GetStringField(TEXT("comparison_stage")), FString(TEXT("rebind")));
    TestEqual(TEXT("Changed added removed fields reported"), Receipt->GetArrayField(TEXT("state_differences")).Num(), 3);
    for (const auto& Value : Receipt->GetArrayField(TEXT("state_differences")))
        TestTrue(TEXT("Every difference has field before after"), Value->AsObject()->HasField(TEXT("field"))
            && Value->AsObject()->HasField(TEXT("before")) && Value->AsObject()->HasField(TEXT("after")));
    System->GetPackage()->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetMigrationMoveRenameTest, "UnrealMCP.AssetMigration.MoveRenameRebind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetMigrationMoveRenameTest::RunTest(const FString& Parameters)
{
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPMigrationRenameRoot="), Root))
    { AddInfo(TEXT("MCPMigrationRenameRoot not supplied; saved-fixture acceptance not requested")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/AssetMigration_Rename_")))
    { AddError(TEXT("Rename acceptance requires a unique __Dev/AssetMigration_Rename_ root")); return false; }
    const FString SourcePath = Root + TEXT("/Source/MI_BulletHole");
    const FString DestinationPath = Root + TEXT("/Copy/MI_LaserImpact_BulletHole");
    const FString SystemPath = Root + TEXT("/NS_Existing");
    if (FParse::Param(FCommandLine::Get(), TEXT("MCPMigrationRenameReload")))
    {
        auto System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
        auto Copy = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(DestinationPath));
        if (!TestNotNull(TEXT("Reload existing NS"), System) || !TestNotNull(TEXT("Reload renamed MI"), Copy)) return false;
        System->WaitForCompilationComplete(true, false);
        TestTrue(TEXT("Reloaded NS compilation valid"), System->IsValid() && System->IsReadyToRun());
        int32 Renderers = 0;
        for (const auto& Handle : System->GetEmitterHandles())
            for (auto Renderer : Handle.GetInstance().GetEmitterData()->GetRenderers())
                if (auto Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
                {
                    if (Handle.GetName() == TEXT("Sparks"))
                    {
                        ++Renderers;
                        TestEqual(TEXT("Reloaded Sparks references renamed copy"), GetPathNameSafe(Sprite->Material.Get()), Copy->GetPathName());
                    }
                    else TestEqual(TEXT("Unselected template renderer unchanged"), GetPathNameSafe(Sprite->Material.Get()),
                        FString(TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial")));
                }
        TestTrue(TEXT("At least one real renderer reloaded"), Renderers > 0);
        TestFalse(TEXT("Reload leaves NS clean"), System->GetPackage()->IsDirty());
        return true;
    }
    if (FPackageName::DoesPackageExist(SourcePath) || FPackageName::DoesPackageExist(SystemPath) || FPackageName::DoesPackageExist(DestinationPath))
    { AddError(TEXT("Rename test root already occupied; nothing overwritten")); return false; }
    auto Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
    auto Emitter = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst"));
    if (!TestNotNull(TEXT("Engine material template"), Parent) || !TestNotNull(TEXT("Engine emitter template"), Emitter)) return false;
    auto Source = NewObject<UMaterialInstanceConstant>(CreatePackage(*SourcePath), TEXT("MI_BulletHole"), RF_Public | RF_Standalone | RF_Transactional);
    UMaterialEditingLibrary::SetMaterialInstanceParent(Source, Parent);
    FAssetRegistryModule::AssetCreated(Source);
    if (!TestTrue(TEXT("Save original test MI"), UEditorAssetLibrary::SaveLoadedAsset(Source, false))) return false;
    auto System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), SystemPath));
    if (!TestNotNull(TEXT("Initialize test NS from native template"), System)) return false;
    const auto Handle = System->AddEmitterHandle(*Emitter, TEXT("Sparks"), Emitter->GetExposedVersion().VersionGuid);
    int32 Renderers = 0;
    for (auto Renderer : Handle.GetInstance().GetEmitterData()->GetRenderers())
        if (auto Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
        { Sprite->Modify(); Sprite->Material = Source; Sprite->PostEditChange(); ++Renderers; }
    Handle.GetInstance().Emitter->PostEditChange();
    if (!TestTrue(TEXT("Real sprite renderer configured"), Renderers > 0)) return false;
    System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(true, false);
    if (!TestTrue(TEXT("Save existing test NS"), UEditorAssetLibrary::SaveLoadedAsset(System, false))) return false;
    FString SourceFile; FPackageName::DoesPackageExist(SourcePath, &SourceFile);
    const FString SourceHash = LexToString(FMD5Hash::HashFile(*SourceFile));
    auto Request = MakeShared<FJsonObject>();
    Request->SetArrayField(TEXT("roots"), {MakeShared<FJsonValueString>(SystemPath)});
    Request->SetArrayField(TEXT("rebind_assets"), {MakeShared<FJsonValueString>(SystemPath)});
    auto Rule = MakeShared<FJsonObject>(); Rule->SetStringField(TEXT("source_root"), SourcePath); Rule->SetStringField(TEXT("target_root"), DestinationPath);
    Request->SetArrayField(TEXT("path_rules"), {MakeShared<FJsonValueObject>(Rule)});
    const auto Plan = UnrealMCPAssetMigration::Plan(Request);
    if (!TestTrue(TEXT("Move and rename plan executable"), Plan->GetBoolField(TEXT("executable"))))
    { for (const auto& Blocker : Plan->GetArrayField(TEXT("blockers"))) AddError(Blocker->AsString()); return false; }
    auto Execute = MakeShared<FJsonObject>(); Execute->SetStringField(TEXT("plan_id"), Plan->GetStringField(TEXT("plan_id")));
    Execute->SetStringField(TEXT("confirmation_token"), Plan->GetStringField(TEXT("confirmation_token")));
    const auto Result = UnrealMCPAssetMigration::Execute(Execute);
    if (!TestTrue(TEXT("Move rename and existing NS rebind saved"), Result->GetBoolField(TEXT("success"))))
    { AddError(Result->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Existing NS not rebuilt"), UEditorAssetLibrary::LoadAsset(SystemPath), static_cast<UObject*>(System));
    auto Copy = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(DestinationPath));
    for (auto Renderer : Handle.GetInstance().GetEmitterData()->GetRenderers())
        if (auto Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer)) TestEqual(TEXT("Actual material points at renamed copy"), Sprite->Material.Get(), static_cast<UMaterialInterface*>(Copy));
    TestEqual(TEXT("Original MI file unchanged"), LexToString(FMD5Hash::HashFile(*SourceFile)), SourceHash);
    TestEqual(TEXT("Repeated completed plan does not copy again"), UnrealMCPAssetMigration::Execute(Execute)->GetStringField(TEXT("state")), FString(TEXT("completed")));
    for (const auto& Item : Result->GetArrayField(TEXT("items"))) TestTrue(TEXT("Every planned target saved"), Item->AsObject()->GetBoolField(TEXT("saved")));
    AddInfo(TEXT("Move-rename acceptance root: ") + Root);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetMigrationSnapshotTest, "UnrealMCP.AssetMigration.AuthoredSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetMigrationSnapshotTest::RunTest(const FString& Parameters)
{
    auto Package = CreatePackage(*(TEXT("/Game/__Dev/AssetMigration_Snapshot_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto Renderer = NewObject<UNiagaraSpriteRendererProperties>(Package, TEXT("Renderer"));
    auto Curve = NewObject<UNiagaraDataInterfaceCurve>(Package, TEXT("AuthoredCurve"));
    auto Script = NewObject<UNiagaraScript>(Package, TEXT("Script"));
    const FNiagaraVariable Speed(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Constants.Test.Speed"));
    Script->RapidIterationParameters.SetParameterValue<float>(2.5f, Speed, true);
    auto Baseline = UnrealMCPAssetMigration::SnapshotForTests(Package);
    auto BindingProperty = FindFProperty<FStructProperty>(Renderer->GetClass(), TEXT("PositionBinding"));
    auto Binding = BindingProperty->ContainerPtrToValuePtr<void>(Renderer);
    auto CachedFlag = FindFProperty<FBoolProperty>(BindingProperty->Struct, TEXT("bBindingExistsOnSource"));
    CachedFlag->SetPropertyValue_InContainer(Binding, !CachedFlag->GetPropertyValue_InContainer(Binding));
    auto Generated = NewObject<UNiagaraDataInterfaceCurve>(Script, TEXT("GeneratedCurve"));
    FNiagaraScriptDataInterfaceInfo GeneratedInfo;
    GeneratedInfo.DataInterface = Generated;
    Script->GetCachedDefaultDataInterfaces().Add(GeneratedInfo);
    TestTrue(TEXT("Compiler cache changes do not change authored snapshot"), Baseline.OrderIndependentCompareEqual(UnrealMCPAssetMigration::SnapshotForTests(Package)));
    auto RootName = FindFProperty<FNameProperty>(BindingProperty->Struct, TEXT("RootName"));
    const FName OriginalName = RootName->GetPropertyValue_InContainer(Binding);
    RootName->SetPropertyValue_InContainer(Binding, TEXT("User.CustomPosition"));
    TestFalse(TEXT("Real renderer binding change is detected"), Baseline.OrderIndependentCompareEqual(UnrealMCPAssetMigration::SnapshotForTests(Package)));
    RootName->SetPropertyValue_InContainer(Binding, OriginalName);
    Curve->Curve.AddKey(0.25f, 3.0f);
    TestFalse(TEXT("Authored curve edits are still detected"), Baseline.OrderIndependentCompareEqual(UnrealMCPAssetMigration::SnapshotForTests(Package)));
    Curve->Curve.Reset();
    Baseline = UnrealMCPAssetMigration::SnapshotForTests(Package);
    Script->RapidIterationParameters.SetParameterValue<float>(9.0f, Speed, false);
    TestFalse(TEXT("Real rapid parameter edit is detected"), Baseline.OrderIndependentCompareEqual(UnrealMCPAssetMigration::SnapshotForTests(Package)));
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetMigrationReferenceTest, "UnrealMCP.AssetMigration.ScopedReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetMigrationReferenceTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/__Dev/AssetMigration_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* SourcePackage = CreatePackage(*(Root + TEXT("/Source")));
    UPackage* TargetPackage = CreatePackage(*(Root + TEXT("/Copy")));
    auto Source = NewObject<UTexture2D>(SourcePackage, TEXT("Source"), RF_Public | RF_Standalone);
    auto Copy = NewObject<UTexture2D>(TargetPackage, TEXT("Copy"), RF_Public | RF_Standalone);
    auto Unrelated = NewObject<UMCPAssetPropertyTestAsset>(CreatePackage(*(Root + TEXT("/Unrelated"))), TEXT("Unrelated"));
    auto Selected = NewObject<UMCPAssetPropertyTestAsset>(CreatePackage(*(Root + TEXT("/Selected"))), TEXT("Selected"));
    for (auto Asset : {Unrelated, Selected})
    {
        Asset->Texture = Source;
        Asset->SoftTexture = Source;
        Asset->Scale = 3.25f;
    }
    TMap<UObject*, UObject*> Objects{{Source, Copy}};
    TMap<FString, FString> Packages{{SourcePackage->GetName(), TargetPackage->GetName()}};
    TestTrue(TEXT("Scoped references changed"), UnrealMCPAssetMigration::RemapPackage(Selected->GetPackage(), Objects, Packages) >= 2);
    TestEqual(TEXT("Selected hard reference"), Selected->Texture.Get(), Copy);
    TestEqual(TEXT("Selected soft path"), Selected->SoftTexture.ToSoftObjectPath(), FSoftObjectPath(Copy));
    TestEqual(TEXT("Unrelated hard reference preserved"), Unrelated->Texture.Get(), Source);
    TestEqual(TEXT("Unrelated soft reference preserved"), Unrelated->SoftTexture.ToSoftObjectPath(), FSoftObjectPath(Source));
    TestEqual(TEXT("Authored scalar preserved"), Selected->Scale, 3.25f);
    Selected->GetPackage()->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetMigrationPlanTest, "UnrealMCP.AssetMigration.PlanAndExecute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetMigrationPlanTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/__Dev/AssetMigration_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Path = Root + TEXT("/Source/DA_Test");
    auto Asset = NewObject<UMCPAssetPropertyTestAsset>(CreatePackage(*Path), TEXT("DA_Test"), RF_Public | RF_Standalone);
    Asset->Scale = 7.5f;
    FAssetRegistryModule::AssetCreated(Asset);
    if (!TestTrue(TEXT("Save fixture"), UEditorAssetLibrary::SaveLoadedAsset(Asset, false))) return false;
    auto Params = MakeShared<FJsonObject>();
    Params->SetArrayField(TEXT("roots"), {MakeShared<FJsonValueString>(Path)});
    auto Rule = MakeShared<FJsonObject>(); Rule->SetStringField(TEXT("source_root"), Root + TEXT("/Source")); Rule->SetStringField(TEXT("target_root"), Root + TEXT("/Copy"));
    Params->SetArrayField(TEXT("path_rules"), {MakeShared<FJsonValueObject>(Rule)});
    auto Plan = UnrealMCPAssetMigration::Plan(Params);
    if (!TestTrue(TEXT("Executable plan"), Plan->GetBoolField(TEXT("executable")))) return false;
    TestFalse(TEXT("Plan creates no destination"), FindPackage(nullptr, *(Root + TEXT("/Copy/DA_Test"))) != nullptr);
    auto Execute = MakeShared<FJsonObject>(); Execute->SetStringField(TEXT("plan_id"), Plan->GetStringField(TEXT("plan_id")));
    Execute->SetStringField(TEXT("confirmation_token"), TEXT("wrong"));
    TestFalse(TEXT("Explicit confirmation required"), UnrealMCPAssetMigration::Execute(Execute)->GetBoolField(TEXT("success")));
    Execute->SetStringField(TEXT("confirmation_token"), Plan->GetStringField(TEXT("confirmation_token")));
    auto Result = UnrealMCPAssetMigration::Execute(Execute);
    TestTrue(TEXT("Copy saved and verified"), Result->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Repeated execution returns receipt"), UnrealMCPAssetMigration::Execute(Execute)->GetStringField(TEXT("state")), Result->GetStringField(TEXT("state")));
    auto Copy = Cast<UMCPAssetPropertyTestAsset>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/Copy/DA_Test")));
    if (TestNotNull(TEXT("Saved copy exists"), Copy)) TestEqual(TEXT("Copied setting unchanged"), Copy->Scale, 7.5f);
    TestFalse(TEXT("Collision blocks new plan"), UnrealMCPAssetMigration::Plan(Params)->GetBoolField(TEXT("executable")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetMigrationContentTest, "UnrealMCP.AssetMigration.ImpactContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetMigrationContentTest::RunTest(const FString& Parameters)
{
    FString VerifyId;
    if (FParse::Value(FCommandLine::Get(), TEXT("MCPMigrationVerify="), VerifyId))
    {
        auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("plan_id"), VerifyId);
        auto Result = UnrealMCPAssetMigration::Verify(Request);
        if (!TestTrue(TEXT("Fresh process dependencies and compilation valid"), Result->GetBoolField(TEXT("success"))))
        { if (Result->HasField(TEXT("issues"))) for (const auto& Issue : Result->GetArrayField(TEXT("issues"))) AddError(Issue->AsString()); }
        AddInfo(FString::Printf(TEXT("Separate process verification; %d packages were already loaded during editor startup"), Result->GetArrayField(TEXT("already_loaded")).Num()));
        FString Json; FJsonSerializer::Serialize(Result.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("AssetMigration-ImpactVerify.json")));
        return !HasAnyErrors();
    }
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPMigrationTestRoot="), Root))
    { AddInfo(TEXT("No MCPMigrationTestRoot supplied; real-content acceptance not requested")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/AssetMigration_"))) { AddError(TEXT("Test destination must be isolated under __Dev/AssetMigration_")); return false; }
    FString RebindSource;
    const bool Rebinding = FParse::Value(FCommandLine::Get(), TEXT("MCPMigrationRebindSource="), RebindSource);
    if (Rebinding && !RebindSource.StartsWith(TEXT("/Game/__Dev/AssetMigration_"))) { AddError(TEXT("Rebind acceptance may only modify isolated test NS")); return false; }
    TArray<TSharedPtr<FJsonValue>> Roots;
    TMap<FString, UObject*> ExistingRoots;
    for (const FString& Suffix : {FString(TEXT("Robot")), FString(TEXT("Metal")), FString(TEXT("Concrete"))})
    {
        const FString Path = (Rebinding ? RebindSource : FString(TEXT("/Game"))) + TEXT("/Features/Weapons/LaserRifle/VFX/Impacts/NS_LaserImpact_") + Suffix;
        Roots.Add(MakeShared<FJsonValueString>(Path));
        if (Rebinding) ExistingRoots.Add(Path, UEditorAssetLibrary::LoadAsset(Path));
    }
    if (!Rebinding) Roots.Add(MakeShared<FJsonValueString>(TEXT("/Game/MarketPlugins/NiagaraExamples/Materials/MI_BulletHole")));
    auto Params = MakeShared<FJsonObject>(); Params->SetArrayField(TEXT("roots"), Roots);
    if (Rebinding) Params->SetArrayField(TEXT("rebind_assets"), Roots);
    auto Rule = MakeShared<FJsonObject>(); Rule->SetStringField(TEXT("source_root"), Rebinding ? RebindSource : FString(TEXT("/Game"))); Rule->SetStringField(TEXT("target_root"), Root);
    Params->SetArrayField(TEXT("path_rules"), {MakeShared<FJsonValueObject>(Rule)});
    auto Plan = UnrealMCPAssetMigration::Plan(Params);
    FString Json;
    FJsonSerializer::Serialize(Plan.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("AssetMigration-ImpactPlan.json")));
    if (!TestTrue(TEXT("Real impact dependency plan has no blockers"), Plan->GetBoolField(TEXT("executable"))))
    {
        if (Plan->HasField(TEXT("blockers"))) for (const auto& Blocker : Plan->GetArrayField(TEXT("blockers"))) AddError(Blocker->AsString());
        return false;
    }
    AddInfo(FString::Printf(TEXT("Plan contains %d assets and %d dependency edges"), Plan->GetArrayField(TEXT("mapping")).Num(), Plan->GetArrayField(TEXT("dependencies")).Num()));
    if (FParse::Param(FCommandLine::Get(), TEXT("MCPMigrationExecute")))
    {
        auto Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("plan_id"), Plan->GetStringField(TEXT("plan_id")));
        Request->SetStringField(TEXT("confirmation_token"), Plan->GetStringField(TEXT("confirmation_token")));
        auto Result = UnrealMCPAssetMigration::Execute(Request);
        if (!TestTrue(TEXT("Real impact assets copied and remapped"), Result->GetBoolField(TEXT("success"))))
        { AddError(Result->GetStringField(TEXT("error"))); return false; }
        for (const auto& Existing : ExistingRoots)
            TestEqual(TEXT("Existing NS identity preserved"), UEditorAssetLibrary::LoadAsset(Existing.Key), Existing.Value);
        TestEqual(TEXT("Repeat execution returns completed receipt"), UnrealMCPAssetMigration::Execute(Request)->GetStringField(TEXT("state")), FString(TEXT("completed")));
        AddInfo(TEXT("Migration completed: ") + Plan->GetStringField(TEXT("plan_id")));
    }
    return true;
}
#endif