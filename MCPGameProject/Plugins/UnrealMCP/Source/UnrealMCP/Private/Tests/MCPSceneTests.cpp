#if WITH_DEV_AUTOMATION_TESTS
#include "Commands/UnrealMCPSceneCommands.h"
#include "Commands/UnrealMCPSceneAssets.h"
#include "Engine/Texture2D.h"
#include "EditorAssetLibrary.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "AssetExportTask.h"
#include "Exporters/StaticMeshExporterFBX.h"
#include "Misc/Base64.h"
#include "Components/RectLightComponent.h"
#include "Engine/RectLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "FileHelpers.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"
#include "LevelEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SViewport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneVisibilityTest, "UnrealMCP.Scene.TemporaryEditorVisibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneVisibilityTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Blueprint = FKismetEditorUtilities::CreateBlueprint(AStaticMeshActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("VisibilityBP")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("BP fixture"), Blueprint)) return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    auto Actor = World->SpawnActor<AStaticMeshActor>(Blueprint->GeneratedClass);
    auto Child = World->SpawnActor<AStaticMeshActor>();
    auto OtherLight = World->SpawnActor<ARectLight>();
    if (!TestNotNull(TEXT("BP instance"), Actor) || !TestNotNull(TEXT("Child fixture"), Child) || !TestNotNull(TEXT("Unselected light"), OtherLight)) return false;
    Actor->SetActorLabel(TEXT("VisibilityFixture"));
    TestTrue(TEXT("Child attachment"), Child->AttachToActor(Actor, FAttachmentTransformRules::KeepWorldTransform));
    auto Component = Actor->GetStaticMeshComponent();
    Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Component->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
    const FTransform Transform = Actor->GetActorTransform();
    const auto Tags = Actor->Tags; const auto Mesh = Component->GetStaticMesh(); const auto Material = Component->GetMaterial(0);
    const bool Visible = Component->IsVisible(), HiddenInGame = Actor->IsHidden(), Tick = Actor->IsActorTickEnabled();
    const auto Collision = Component->GetCollisionEnabled(); const float Intensity = OtherLight->GetLightComponent()->Intensity;
    const bool BlueprintDirty = Blueprint->GetPackage()->IsDirty();
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    Request->SetStringField(TEXT("actor_path"), Actor->GetPathName()); Request->SetBoolField(TEXT("hidden_in_editor"), true);
    Request->SetBoolField(TEXT("expected_hidden_in_editor"), false);
    World->GetPackage()->SetDirtyFlag(false);
    auto Preview = UnrealMCPScene::SetActorVisibility(Request);
    TestTrue(TEXT("Dry-run default"), Preview->GetBoolField(TEXT("dry_run")));
    TestTrue(TEXT("Preview reports change"), Preview->GetBoolField(TEXT("would_modify")));
    TestFalse(TEXT("Preview does not modify"), Preview->GetBoolField(TEXT("modified")));
    TestFalse(TEXT("Preview keeps actor shown"), Actor->IsTemporarilyHiddenInEditor(false));
    TestFalse(TEXT("Preview does not dirty map"), World->GetPackage()->IsDirty());
    Request->SetBoolField(TEXT("dry_run"), false);
    for (const FString Field : {TEXT("project_path"), TEXT("level_path"), TEXT("actor_path")})
    {
        auto Wrong = MakeShared<FJsonObject>(*Request); Wrong->SetStringField(Field, TEXT("VisibilityFixture"));
        TestFalse(TEXT("Exact identity required: ") + Field, UnrealMCPScene::SetActorVisibility(Wrong)->GetBoolField(TEXT("success")));
    }
    auto Invalid = MakeShared<FJsonObject>(*Request); Invalid->RemoveField(TEXT("hidden_in_editor"));
    TestFalse(TEXT("Missing requested state rejected"), UnrealMCPScene::SetActorVisibility(Invalid)->GetBoolField(TEXT("success")));
    Invalid->SetField(TEXT("hidden_in_editor"), MakeShared<FJsonValueNull>());
    TestFalse(TEXT("Null state rejected"), UnrealMCPScene::SetActorVisibility(Invalid)->GetBoolField(TEXT("success")));
    Invalid = MakeShared<FJsonObject>(*Request); Invalid->SetBoolField(TEXT("save"), true);
    TestFalse(TEXT("No save option"), UnrealMCPScene::SetActorVisibility(Invalid)->GetBoolField(TEXT("success")));
    Invalid = MakeShared<FJsonObject>(*Request); Invalid->SetStringField(TEXT("component_name"), Component->GetName());
    TestFalse(TEXT("Not a component setter"), UnrealMCPScene::SetActorVisibility(Invalid)->GetBoolField(TEXT("success")));
    Invalid = MakeShared<FJsonObject>(*Request); Invalid->SetBoolField(TEXT("expected_hidden_in_editor"), true);
    TestFalse(TEXT("Stale expected state rejected"), UnrealMCPScene::SetActorVisibility(Invalid)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Rejected requests do not dirty map"), World->GetPackage()->IsDirty());
    TestFalse(TEXT("Rejected requests do not hide actor"), Actor->IsTemporarilyHiddenInEditor(false));
    for (bool InitiallyDirty : {false, true})
    {
        World->GetPackage()->SetDirtyFlag(InitiallyDirty);
        const FString UndoBefore = UnrealMCPScene::UndoToken();
        Request->SetBoolField(TEXT("hidden_in_editor"), true); Request->SetBoolField(TEXT("expected_hidden_in_editor"), false);
        auto Receipt = UnrealMCPScene::SetActorVisibility(Request);
        TestTrue(TEXT("Unmanaged BP instance can hide"), Receipt->GetBoolField(TEXT("success")));
        TestTrue(TEXT("Temporary flag set"), Actor->IsTemporarilyHiddenInEditor(false));
        TestTrue(TEXT("Hidden readback"), Receipt->GetObjectField(TEXT("after"))->GetBoolField(TEXT("temporary_hidden")));
        TestFalse(TEXT("Never saved"), Receipt->GetBoolField(TEXT("saved")));
        TestTrue(TEXT("Session-only receipt"), Receipt->GetBoolField(TEXT("session_only")));
        TestFalse(TEXT("No map undo promised"), Receipt->GetBoolField(TEXT("undo_supported")));
        TestEqual(TEXT("Map dirty state preserved"), World->GetPackage()->IsDirty(), InitiallyDirty);
        TestEqual(TEXT("Blueprint dirty state preserved"), Blueprint->GetPackage()->IsDirty(), BlueprintDirty);
        TestEqual(TEXT("User undo stack unchanged"), UnrealMCPScene::UndoToken(), UndoBefore);
        TestFalse(TEXT("Attached actor own hidden flag untouched"), Child->IsTemporarilyHiddenInEditor(false));
        TestFalse(TEXT("Unselected light visibility untouched"), OtherLight->IsTemporarilyHiddenInEditor(false));
        TestEqual(TEXT("Light intensity unchanged"), OtherLight->GetLightComponent()->Intensity, Intensity);
        TestEqual(TEXT("BP component not reconstructed"), Actor->GetStaticMeshComponent(), Component);
        TestEqual(TEXT("Gameplay hidden unchanged"), Actor->IsHidden(), HiddenInGame);
        TestEqual(TEXT("Component visibility unchanged"), Component->IsVisible(), Visible);
        TestEqual(TEXT("Tick unchanged"), Actor->IsActorTickEnabled(), Tick);
        TestEqual(TEXT("Collision unchanged"), Component->GetCollisionEnabled(), Collision);
        TestTrue(TEXT("Transform mesh material and ownership unchanged"), Actor->GetActorTransform().Equals(Transform)
            && Component->GetStaticMesh() == Mesh && Component->GetMaterial(0) == Material && Actor->Tags == Tags);
        TestEqual(TEXT("Attachment unchanged"), Child->GetAttachParentActor(), static_cast<AActor*>(Actor));
        auto Inspection = UnrealMCPScene::Inspect(Request);
        TestTrue(TEXT("Inspection exposes temporary state"), Inspection->GetObjectField(TEXT("editor_visibility"))->GetBoolField(TEXT("temporary_hidden")));
        Request->SetStringField(TEXT("filter"), Actor->GetActorLabel());
        auto Listing = UnrealMCPScene::List(Request); Request->RemoveField(TEXT("filter"));
        if (TestEqual(TEXT("Fixture found by label"), Listing->GetArrayField(TEXT("actors")).Num(), 1))
            TestTrue(TEXT("Listing exposes temporary state"), Listing->GetArrayField(TEXT("actors"))[0]->AsObject()->GetObjectField(TEXT("editor_visibility"))->GetBoolField(TEXT("temporary_hidden")));
        Request->SetBoolField(TEXT("expected_hidden_in_editor"), true);
        TestFalse(TEXT("Repeated requested state is no-op"), UnrealMCPScene::SetActorVisibility(Request)->GetBoolField(TEXT("modified")));
        Actor->SetIsHiddenEdLayer(true);
        Request->SetBoolField(TEXT("hidden_in_editor"), false);
        Receipt = UnrealMCPScene::SetActorVisibility(Request);
        TestTrue(TEXT("Restore succeeds"), Receipt->GetBoolField(TEXT("success")));
        TestFalse(TEXT("Own temporary flag restored"), Actor->IsTemporarilyHiddenInEditor(false));
        TestTrue(TEXT("Layer hiding still reported honestly"), Receipt->GetObjectField(TEXT("after"))->GetBoolField(TEXT("editor_hidden")));
        Actor->SetIsHiddenEdLayer(false);
        TestEqual(TEXT("Restore keeps map dirty state"), World->GetPackage()->IsDirty(), InitiallyDirty);
        TestEqual(TEXT("Restore does not add undo"), UnrealMCPScene::UndoToken(), UndoBefore);
    }
    World->GetPackage()->SetDirtyFlag(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPScenePatchTest, "UnrealMCP.Scene.InstancePatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPScenePatchTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Light = World->SpawnActor<ARectLight>();
    auto First = World->SpawnActor<APostProcessVolume>();
    auto Other = World->SpawnActor<APostProcessVolume>();
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    Request->SetStringField(TEXT("actor_path"), Light->GetPathName());
    Request->SetStringField(TEXT("component_name"), Light->GetLightComponent()->GetName());
    auto Apply = [&](const FString& Json)
    {
        TSharedPtr<FJsonObject> Changes;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"changes\":") + Json + TEXT("}")), Changes);
        Request->SetArrayField(TEXT("changes"), Changes->GetArrayField(TEXT("changes")));
        return UnrealMCPScene::Patch(Request);
    };
    const float Before = Light->GetLightComponent()->Intensity;
    auto Result = Apply(TEXT("[{\"path\":\"Intensity\",\"value\":1234},{\"path\":\"Bogus\",\"value\":1}]"));
    TestFalse(TEXT("Invalid batch refused"), Result->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Earlier field remains unchanged"), Light->GetLightComponent()->Intensity, Before);
    Result = Apply(TEXT("[{\"path\":\"Intensity\",\"value\":1234},{\"path\":\"SourceWidth\",\"value\":80}]"));
    TestTrue(TEXT("Patch real level component"), Result->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Intensity applied"), Light->GetLightComponent()->Intensity, 1234.0f);
    TestFalse(TEXT("No automatic save"), Result->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("Stale expected refused"), Apply(TEXT("[{\"path\":\"Intensity\",\"value\":9,\"expected_value\":-1}]"))->GetBoolField(TEXT("success")));
    Request->SetStringField(TEXT("component_name"), TEXT(""));
    Request->SetStringField(TEXT("actor_path"), First->GetPathName());
    const float OtherBloom = Other->Settings.BloomIntensity;
    Result = Apply(TEXT("[{\"path\":\"Settings.BloomIntensity\",\"value\":2.5},{\"path\":\"Settings.bOverride_BloomIntensity\",\"value\":true}]"));
    TestTrue(TEXT("Patch PPV field and override"), Result->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Bloom override enabled"), First->Settings.bOverride_BloomIntensity);
    TestEqual(TEXT("Other volume untouched"), Other->Settings.BloomIntensity, OtherBloom);
    GEditor->UndoTransaction();
    TestFalse(TEXT("Undo restores override"), First->Settings.bOverride_BloomIntensity);
    Request->SetStringField(TEXT("actor_path"), Light->GetPathName()); Request->SetStringField(TEXT("component_name"), Light->GetLightComponent()->GetName());
    const FString PreviousToken = UnrealMCPScene::UndoToken();
    Result = Apply(TEXT("[{\"path\":\"Intensity\",\"value\":2000}]"));
    UnrealMCPScene::RecordTransaction(Request, Result, PreviousToken);
    FString Token; Result->TryGetStringField(TEXT("transaction_id"), Token);
    TestFalse(TEXT("Undo receipt has exact ID"), Token.IsEmpty()); Request->SetStringField(TEXT("transaction_id"), Token);
    {
        const FScopedTransaction UserEdit(FText::FromString(TEXT("Unrelated user edit")));
        Other->Modify(); Other->BlendWeight = 0.25;
    }
    TestFalse(TEXT("Cannot undo over user edit"), UnrealMCPScene::Undo(Request)->GetBoolField(TEXT("success")));
    GEditor->UndoTransaction();
    TestTrue(TEXT("Exact MCP transaction undo"), UnrealMCPScene::Undo(Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Controlled undo restored intensity"), Light->GetLightComponent()->Intensity, 1234.f);
    TestFalse(TEXT("Undo ticket cannot be reused"), UnrealMCPScene::Undo(Request)->GetBoolField(TEXT("success")));
    Result = Apply(TEXT("[{\"path\":\"LightColor\",\"value\":{\"R\":128,\"G\":64,\"B\":32,\"A\":255}}]"));
    TestTrue(TEXT("Light color patch succeeds"), Result->GetBoolField(TEXT("success")));
    TestEqual(TEXT("sRGB byte color preserved"), Light->GetLightComponent()->LightColor, FColor(128, 64, 32, 255));
    Request->SetStringField(TEXT("project_path"), TEXT("C:/Wrong/Other.uproject"));
    TestFalse(TEXT("Wrong project rejected"), Apply(TEXT("[{\"path\":\"bUnbound\",\"value\":true}]"))->GetBoolField(TEXT("success")));
    World->GetPackage()->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneLifecycleTest, "UnrealMCP.Scene.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneLifecycleTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    for (const FString Type : {TEXT("RectLight"), TEXT("PointLight"), TEXT("SpotLight"), TEXT("DirectionalLight"), TEXT("SkyLight"), TEXT("SkyAtmosphere"), TEXT("HeightFog"), TEXT("PostProcessVolume"), TEXT("CameraActor"), TEXT("CineCameraActor")})
    {
        Request->SetStringField(TEXT("operation"), TEXT("create")); Request->SetStringField(TEXT("actor_type"), Type); Request->SetStringField(TEXT("managed_id"), Type);
        auto Created = UnrealMCPScene::ManageActor(Request);
        if (!TestTrue(Type + TEXT(" created"), Created->GetBoolField(TEXT("success")))) continue;
        TestFalse(TEXT("Creation never saves"), Created->GetBoolField(TEXT("saved")));
        TestFalse(TEXT("Duplicate managed id refused"), UnrealMCPScene::ManageActor(Request)->GetBoolField(TEXT("success")));
        Request->SetStringField(TEXT("actor_path"), Created->GetStringField(TEXT("actor_path"))); Request->RemoveField(TEXT("component_name"));
        auto Inspection = UnrealMCPScene::Inspect(Request);
        TestTrue(Type + TEXT(" actor inspection"), Inspection->GetBoolField(TEXT("success")));
        const TArray<TSharedPtr<FJsonValue>>* Components;
        if (Inspection->TryGetArrayField(TEXT("components"), Components))
        {
            for (auto Component : *Components)
            {
                Request->SetStringField(TEXT("component_name"), Component->AsObject()->GetStringField(TEXT("name")));
                auto Fields = UnrealMCPScene::Inspect(Request);
                FString Error; Fields->TryGetStringField(TEXT("error"), Error);
                TestTrue(Type + TEXT(" fields available: ") + Error, Fields->GetBoolField(TEXT("success")));
            }
        }
        Request->RemoveField(TEXT("component_name")); Request->SetStringField(TEXT("operation"), TEXT("delete"));
        TestTrue(TEXT("Managed actor deletable"), UnrealMCPScene::ManageActor(Request)->GetBoolField(TEXT("success")));
    }
    auto Actor = World->SpawnActor<AStaticMeshActor>();
    auto Mesh = DuplicateObject<UStaticMesh>(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")), GetTransientPackage());
    FStaticMaterial SecondSlot = Mesh->GetStaticMaterials()[0];
    SecondSlot.MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
    SecondSlot.MaterialSlotName = TEXT("Second");
    Mesh->GetStaticMaterials().Add(SecondSlot);
    Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    Request->SetStringField(TEXT("actor_path"), Actor->GetPathName()); Request->RemoveField(TEXT("component_name"));
    TestFalse(TEXT("Unmanaged actor deletion refused"), UnrealMCPScene::ManageActor(Request)->GetBoolField(TEXT("success")));
    Request->SetStringField(TEXT("component_name"), Actor->GetStaticMeshComponent()->GetName());
    const FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
    auto Slot = MakeShared<FJsonObject>(); Slot->SetNumberField(TEXT("slot_index"), 1); Slot->SetStringField(TEXT("material_path"), MaterialPath);
    auto Invalid = MakeShared<FJsonObject>(); Invalid->SetNumberField(TEXT("slot_index"), 100); Invalid->SetStringField(TEXT("material_path"), MaterialPath);
    Request->SetArrayField(TEXT("materials"), {MakeShared<FJsonValueObject>(Slot), MakeShared<FJsonValueObject>(Invalid)});
    auto Before = Actor->GetStaticMeshComponent()->GetMaterial(1);
    TestFalse(TEXT("Invalid slot batch rejected"), UnrealMCPScene::Mesh(Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Valid earlier slot unchanged"), Actor->GetStaticMeshComponent()->GetMaterial(1), Before);
    Request->SetArrayField(TEXT("materials"), {MakeShared<FJsonValueObject>(Slot)});
    TestTrue(TEXT("Nonzero slot applied"), UnrealMCPScene::Mesh(Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Slot one actual material"), Actor->GetStaticMeshComponent()->GetMaterial(1)->GetPathName(), MaterialPath);
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo restores slot one"), Actor->GetStaticMeshComponent()->GetMaterial(1), Before);
    TestFalse(TEXT("Save requires explicit confirmation"), UnrealMCPScene::Save(Request)->GetBoolField(TEXT("success")));
    World->GetPackage()->SetDirtyFlag(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneManifestTest, "UnrealMCP.Scene.PlacementManifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneManifestTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    TSharedPtr<FJsonObject> Data;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"version":1,"namespace":"Fixture","source_hash":"hashA","units":"cm","up_axis":"Z","handedness":"left","objects":[{"id":"Child","parent_id":"Root","mesh":"/Engine/BasicShapes/Cube.Cube","location":[0,200,0],"rotation":[0,0,0],"scale":[1,1,1]},{"id":"Root","mesh":"/Engine/BasicShapes/Cube.Cube","location":[100,0,0],"rotation":[0,0,0],"scale":[1,1,1]}]})")), Data);
    Request->SetObjectField(TEXT("manifest"), Data);
    TestTrue(TEXT("Dry run valid"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")));
    TestNull(TEXT("Dry run creates nothing"), FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("MCPScene_Fixture_Root")));
    Request->SetBoolField(TEXT("dry_run"), false);
    const FString Before = UnrealMCPScene::UndoToken();
    auto Result = UnrealMCPScene::Manifest(Request); UnrealMCPScene::RecordTransaction(Request, Result, Before);
    TestTrue(TEXT("Manifest applied"), Result->GetBoolField(TEXT("success")));
    auto Root = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("MCPScene_Fixture_Root"));
    auto Child = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("MCPScene_Fixture_Child"));
    if (!TestNotNull(TEXT("Root created"), Root) || !TestNotNull(TEXT("Child created"), Child)) return false;
    TestEqual(TEXT("Hierarchy preserved"), Child->GetAttachParentActor(), static_cast<AActor*>(Root));
    TestTrue(TEXT("Local cm transform converted to world"), Child->GetActorLocation().Equals(FVector(100, 200, 0), 0.01));
    TestFalse(TEXT("Repeat manifest makes no changes"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("modified")));
    Data->SetStringField(TEXT("expected_source_hash"), TEXT("stale"));
    TestFalse(TEXT("Stale source hash refused"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")));
    Data->RemoveField(TEXT("expected_source_hash"));
    Data->GetArrayField(TEXT("objects"))[1]->AsObject()->SetStringField(TEXT("parent_id"), TEXT("Child"));
    TestFalse(TEXT("Cyclic hierarchy refused"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Rejected batch preserves transform"), Root->GetActorLocation().Equals(FVector(100, 0, 0), 0.01));
    Request->SetStringField(TEXT("transaction_id"), Result->GetStringField(TEXT("transaction_id")));
    TestTrue(TEXT("Whole manifest undo"), UnrealMCPScene::Undo(Request)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Undo removed managed actors"), !IsValid(Root) || Root->IsActorBeingDestroyed());
    World->GetPackage()->SetDirtyFlag(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneFolderTest, "UnrealMCP.Scene.OutlinerFolders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneFolderTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    const FFolder DefaultFolder = FActorFolders::Get().GetActorEditorContextFolder(*World);
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    Request->SetStringField(TEXT("operation"), TEXT("create")); Request->SetStringField(TEXT("actor_type"), TEXT("StaticMeshActor"));
    Request->SetStringField(TEXT("managed_id"), TEXT("FolderFixture"));
    for (const FString Invalid : {TEXT("/Absolute"), TEXT("Parent/../Other"), TEXT("A//B"), TEXT("A\\B"), TEXT("A/"), TEXT(" Padded"), TEXT("None"), TEXT("A:Bad"), TEXT("A/./B")})
    {
        Request->SetStringField(TEXT("folder_path"), Invalid);
        TestFalse(TEXT("Invalid creation folder rejected: ") + Invalid, UnrealMCPScene::ManageActor(Request)->GetBoolField(TEXT("success")));
        TestNull(TEXT("Invalid folder never spawns actor"), FindObject<AActor>(World->PersistentLevel, TEXT("MCPScene_FolderFixture")));
    }
    Request->SetStringField(TEXT("folder_path"), TEXT("OpeningEnvironment/Architecture"));
    auto Receipt = UnrealMCPScene::ManageActor(Request);
    if (!TestTrue(TEXT("Create with folder"), Receipt->GetBoolField(TEXT("success")))) return false;
    auto Actor = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("MCPScene_FolderFixture"));
    if (!TestNotNull(TEXT("Managed actor exists"), Actor)) return false;
    Request->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    TestEqual(TEXT("Creation receipt folder"), Receipt->GetStringField(TEXT("folder_path")), FString(TEXT("OpeningEnvironment/Architecture")));
    TestEqual(TEXT("Actor inspection folder"), UnrealMCPScene::Inspect(Request)->GetStringField(TEXT("folder_path")), FString(TEXT("OpeningEnvironment/Architecture")));
    TestTrue(TEXT("Creation receipt exposes independent ownership"), Receipt->GetBoolField(TEXT("scene_managed")));
    Request->SetStringField(TEXT("filter"), Actor->GetActorLabel());
    auto Listing = UnrealMCPScene::List(Request);
    if (!TestTrue(TEXT("List finds created actor by label"), Listing->GetBoolField(TEXT("success")))
        || !TestEqual(TEXT("One actor matches fixture label"), Listing->GetArrayField(TEXT("actors")).Num(), 1)) return false;
    TestEqual(TEXT("List folder"), Listing->GetArrayField(TEXT("actors"))[0]->AsObject()->GetStringField(TEXT("folder_path")), FString(TEXT("OpeningEnvironment/Architecture")));
    auto Blueprint = FKismetEditorUtilities::CreateBlueprint(AStaticMeshActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("FolderManualBP")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Manual BP fixture"), Blueprint)) return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    auto Manual = World->SpawnActor<AStaticMeshActor>(Blueprint->GeneratedClass);
    if (!TestNotNull(TEXT("Manual BP instance"), Manual)) return false;
    Manual->SetFolderPath(TEXT("ManualGameplay"));
    TestTrue(TEXT("Manual BP attached"), Manual->AttachToActor(Actor, FAttachmentTransformRules::KeepWorldTransform));
    auto Component = Actor->GetStaticMeshComponent();
    Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Component->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
    const FTransform ActorTransform = Actor->GetActorTransform(), ManualTransform = Manual->GetActorTransform();
    const auto Mesh = Component->GetStaticMesh(); const auto Material = Component->GetMaterial(0);
    const auto Collision = Component->GetCollisionEnabled(); const auto Profile = Component->GetCollisionProfileName(); const auto Tags = Actor->Tags;
    auto Change = MakeShared<FJsonObject>(); Change->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Change->SetStringField(TEXT("folder_path"), TEXT("OpeningEnvironment/Props")); Change->SetStringField(TEXT("expected_folder_path"), TEXT("OpeningEnvironment/Architecture"));
    auto OtherChange = MakeShared<FJsonObject>(); OtherChange->SetStringField(TEXT("actor_path"), Manual->GetPathName()); OtherChange->SetStringField(TEXT("folder_path"), TEXT("OpeningEnvironment/Props"));
    Request->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change), MakeShared<FJsonValueObject>(OtherChange)});
    Request->SetBoolField(TEXT("dry_run"), false); World->GetPackage()->SetDirtyFlag(false);
    TestFalse(TEXT("Unmanaged BP rejected by default"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Full preflight leaves earlier actor unchanged"), Actor->GetFolderPath(), FName(TEXT("OpeningEnvironment/Architecture")));
    TestFalse(TEXT("Rejected batch leaves map clean"), World->GetPackage()->IsDirty());
    OtherChange->SetStringField(TEXT("actor_path"), TEXT("MissingExactPath"));
    TestFalse(TEXT("Unknown actor aborts batch"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    OtherChange->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    TestFalse(TEXT("Duplicate actor aborts batch"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    Request->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
    Change->SetStringField(TEXT("expected_folder_path"), TEXT("Stale"));
    TestFalse(TEXT("Stale folder aborts before mutation"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    Change->SetStringField(TEXT("expected_folder_path"), TEXT("OpeningEnvironment/Architecture")); Request->SetBoolField(TEXT("dry_run"), true);
    TestFalse(TEXT("Dry-run reports unmodified"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("modified")));
    TestFalse(TEXT("Dry-run leaves clean map"), World->GetPackage()->IsDirty());
    const FString BeforeToken = UnrealMCPScene::UndoToken(); Request->SetBoolField(TEXT("dry_run"), false);
    Receipt = UnrealMCPScene::SetActorFolders(Request); UnrealMCPScene::RecordTransaction(Request, Receipt, BeforeToken);
    TestTrue(TEXT("Folder assigned"), Receipt->GetBoolField(TEXT("success"))); TestFalse(TEXT("No automatic save"), Receipt->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("New folder readback"), Actor->GetFolderPath(), FName(TEXT("OpeningEnvironment/Props")));
    TestEqual(TEXT("Attached manual BP folder untouched"), Manual->GetFolderPath(), FName(TEXT("ManualGameplay")));
    TestEqual(TEXT("Attachment untouched"), Manual->GetAttachParentActor(), static_cast<AActor*>(Actor));
    TestTrue(TEXT("Both transforms untouched"), ActorTransform.Equals(Actor->GetActorTransform()) && ManualTransform.Equals(Manual->GetActorTransform()));
    TestTrue(TEXT("Mesh, material, collision and tags untouched"), Component->GetStaticMesh() == Mesh && Component->GetMaterial(0) == Material
        && Component->GetCollisionEnabled() == Collision && Component->GetCollisionProfileName() == Profile && Actor->Tags == Tags);
    TestTrue(TEXT("Default creation folder untouched"), FActorFolders::Get().GetActorEditorContextFolder(*World) == DefaultFolder);
    Change->RemoveField(TEXT("expected_folder_path")); World->GetPackage()->SetDirtyFlag(false);
    TestFalse(TEXT("Repeat is no-op"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("modified")));
    TestFalse(TEXT("No-op leaves map clean"), World->GetPackage()->IsDirty());
    Request->SetStringField(TEXT("transaction_id"), Receipt->GetStringField(TEXT("transaction_id")));
    TestTrue(TEXT("Guarded folder undo"), UnrealMCPScene::Undo(Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Undo restores folder"), Actor->GetFolderPath(), FName(TEXT("OpeningEnvironment/Architecture")));
    Change->SetStringField(TEXT("actor_path"), Manual->GetPathName()); Change->SetStringField(TEXT("folder_path"), TEXT(""));
    Request->SetBoolField(TEXT("managed_only"), false);
    TestTrue(TEXT("Explicitly selected BP can clear folder"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Empty folder means world root"), Manual->GetFolderPath().IsNone());
    TestFalse(TEXT("Folder change does not claim BP ownership"), Manual->ActorHasTag(TEXT("UnrealMCP.SceneManaged")));
    TestEqual(TEXT("Selected BP still attached"), Manual->GetAttachParentActor(), static_cast<AActor*>(Actor));
    auto LaterManual = World->SpawnActor<AStaticMeshActor>(Blueprint->GeneratedClass);
    TestTrue(TEXT("Later manual BP not put under tool root"), LaterManual && LaterManual->GetFolderPath().IsNone());
    TArray<TSharedPtr<FJsonValue>> Oversized; Oversized.Init(MakeShared<FJsonValueObject>(Change), 201);
    Request->SetArrayField(TEXT("changes"), Oversized);
    TestFalse(TEXT("Over 200 assignments refused"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    Request->SetArrayField(TEXT("changes"), {});
    TestFalse(TEXT("Empty batch refused"), UnrealMCPScene::SetActorFolders(Request)->GetBoolField(TEXT("success")));
    World->GetPackage()->SetDirtyFlag(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneFolderManifestTest, "UnrealMCP.Scene.FolderManifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneFolderManifestTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    TSharedPtr<FJsonObject> Manifest;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"version":1,"namespace":"FolderTest","source_hash":"hashA","units":"cm","up_axis":"Z","handedness":"left","folder_root":"OpeningEnvironment","objects":[{"id":"Wall","mesh":"/Engine/BasicShapes/Cube.Cube","location":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1],"folder":"Architecture/MainCourtyard"},{"id":"Prop","parent_id":"Wall","mesh":"/Engine/BasicShapes/Cube.Cube","location":[0,100,0],"rotation":[0,0,0],"scale":[1,1,1],"folder":"Props"}]})")), Manifest);
    Request->SetObjectField(TEXT("manifest"), Manifest); World->GetPackage()->SetDirtyFlag(false);
    TestTrue(TEXT("Folder manifest preview succeeds"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Preview leaves map clean"), World->GetPackage()->IsDirty());
    TestNull(TEXT("Preview creates nothing"), FindObject<AActor>(World->PersistentLevel, TEXT("MCPScene_FolderTest_Wall")));
    Request->SetBoolField(TEXT("dry_run"), false);
    auto Objects = Manifest->GetArrayField(TEXT("objects")); Objects[1]->AsObject()->SetStringField(TEXT("folder"), TEXT("../Escape"));
    TestFalse(TEXT("Relative folder escape aborts batch"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")));
    TestNull(TEXT("Invalid later object prevents earlier spawn"), FindObject<AActor>(World->PersistentLevel, TEXT("MCPScene_FolderTest_Wall")));
    Objects[1]->AsObject()->SetStringField(TEXT("folder"), TEXT("Props"));
    if (!TestTrue(TEXT("Folder manifest applied"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")))) return false;
    auto Wall = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("MCPScene_FolderTest_Wall"));
    auto Prop = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("MCPScene_FolderTest_Prop"));
    if (!TestNotNull(TEXT("Wall exists"), Wall) || !TestNotNull(TEXT("Prop exists"), Prop)) return false;
    TestEqual(TEXT("Root plus relative folder"), Wall->GetFolderPath(), FName(TEXT("OpeningEnvironment/Architecture/MainCourtyard")));
    TestEqual(TEXT("Child directory independent of parent directory"), Prop->GetFolderPath(), FName(TEXT("OpeningEnvironment/Props")));
    TestEqual(TEXT("Actor attachment retained"), Prop->GetAttachParentActor(), static_cast<AActor*>(Wall));
    TestFalse(TEXT("Identical folder manifest is no-op"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("modified")));
    const FTransform BeforeTransform = Prop->GetActorTransform(); const auto BeforeMesh = Prop->GetStaticMeshComponent()->GetStaticMesh();
    const FString BeforeToken = UnrealMCPScene::UndoToken(); Objects[1]->AsObject()->SetStringField(TEXT("folder"), TEXT(""));
    auto Receipt = UnrealMCPScene::Manifest(Request); UnrealMCPScene::RecordTransaction(Request, Receipt, BeforeToken);
    TestEqual(TEXT("Empty relative folder selects manifest root"), Prop->GetFolderPath(), FName(TEXT("OpeningEnvironment")));
    TestTrue(TEXT("Folder-only update preserves transform and mesh"), BeforeTransform.Equals(Prop->GetActorTransform()) && Prop->GetStaticMeshComponent()->GetStaticMesh() == BeforeMesh);
    TestEqual(TEXT("Folder-only update preserves attachment"), Prop->GetAttachParentActor(), static_cast<AActor*>(Wall));
    Request->SetStringField(TEXT("transaction_id"), Receipt->GetStringField(TEXT("transaction_id")));
    TestTrue(TEXT("Manifest folder update undo"), UnrealMCPScene::Undo(Request)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Undo restores child folder"), Prop->GetFolderPath(), FName(TEXT("OpeningEnvironment/Props")));
    Manifest->RemoveField(TEXT("folder_root"));
    TestFalse(TEXT("Relative folder without root rejected"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("success")));
    for (const auto& Object : Objects) Object->AsObject()->RemoveField(TEXT("folder"));
    TestFalse(TEXT("Omitted folders preserve existing folders without modification"), UnrealMCPScene::Manifest(Request)->GetBoolField(TEXT("modified")));
    TestEqual(TEXT("Legacy manifest keeps existing folder"), Prop->GetFolderPath(), FName(TEXT("OpeningEnvironment/Props")));
    World->GetPackage()->SetDirtyFlag(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneFolderPersistenceTest, "UnrealMCP.Scene.FolderPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneFolderPersistenceTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    const FString MapName = TEXT("/Game/__Dev/SceneTools/FolderPersistence_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Filename = FPackageName::LongPackageNameToFilename(MapName, FPackageName::GetMapPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!TestTrue(TEXT("Create isolated persistence fixture map"), FEditorFileUtils::SaveLevel(World->PersistentLevel, Filename))) return false;
    auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    Request->SetStringField(TEXT("operation"), TEXT("create")); Request->SetStringField(TEXT("actor_type"), TEXT("StaticMeshActor")); Request->SetStringField(TEXT("managed_id"), TEXT("FolderSaved"));
    Request->SetStringField(TEXT("folder_path"), TEXT("OpeningEnvironment/Architecture/MainCourtyard"));
    auto Receipt = UnrealMCPScene::ManageActor(Request);
    TestTrue(TEXT("Create persistable folder actor"), Receipt->GetBoolField(TEXT("success")));
    Request->SetBoolField(TEXT("confirm_all_changes_in_map"), true);
    TestTrue(TEXT("Explicit map-only save"), UnrealMCPScene::Save(Request)->GetBoolField(TEXT("saved")));
    World->GetPackage()->SetDirtyFlag(false); FAutomationEditorCommonUtils::CreateNewMap();
    if (TestTrue(TEXT("Reopen folder fixture map"), FEditorFileUtils::LoadMap(Filename, false, false)))
    {
        World = GEditor->GetEditorWorldContext().World();
        auto Actor = FindObject<AActor>(World->PersistentLevel, TEXT("MCPScene_FolderSaved"));
        if (TestNotNull(TEXT("Saved actor survived reopen"), Actor))
        {
            TestEqual(TEXT("Folder membership survived reopen"), Actor->GetFolderPath(), FName(TEXT("OpeningEnvironment/Architecture/MainCourtyard")));
            TestTrue(TEXT("Ownership tag survived independently"), Actor->ActorHasTag(TEXT("UnrealMCP.SceneManaged")));
        }
        TestFalse(TEXT("Reopened map clean"), World->GetPackage()->IsDirty());
        World->GetPackage()->SetDirtyFlag(false);
    }
    FAutomationEditorCommonUtils::CreateNewMap();
    TestTrue(TEXT("Remove only unique fixture map"), UEditorAssetLibrary::DeleteAsset(MapName));
    return !HasAnyErrors();
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FMCPWaitForSceneImport, FAutomationTestBase*, Test, TSharedPtr<FJsonObject>, Request);

bool FMCPWaitForSceneImport::Update()
{
    auto Receipt = UnrealMCPSceneAssets::Status(Request);
    const FString State = Receipt->GetStringField(TEXT("state"));
    if (State == TEXT("pending") || State == TEXT("running")) return false;
    FString Error; Receipt->TryGetStringField(TEXT("error"), Error);
    if (!Test->TestTrue(TEXT("PNG import completed: ") + Error, State == TEXT("completed"))) return true;
    Test->TestFalse(TEXT("Default import never saves"), Receipt->GetBoolField(TEXT("saved")));
    Test->TestFalse(TEXT("Import lock released"), UnrealMCPSceneAssets::IsBusy());
    const auto Assets = Receipt->GetArrayField(TEXT("assets"));
    if (Request->HasField(TEXT("mesh_fixture")))
    {
        UStaticMesh* Mesh = nullptr;
        for (const auto& Entry : Assets)
        {
            auto Asset = UEditorAssetLibrary::LoadAsset(Entry->AsObject()->GetStringField(TEXT("asset_path")));
            if (auto Candidate = Cast<UStaticMesh>(Asset)) Mesh = Candidate;
            if (Asset) Asset->GetPackage()->SetDirtyFlag(false);
        }
        if (!Test->TestNotNull(TEXT("Imported StaticMesh exists"), Mesh)) return true;
        if (Mesh->IsCompiling()) return false;
        Request->SetStringField(TEXT("asset_path"), Mesh->GetPathName());
        auto Inspection = UnrealMCPSceneAssets::Inspect(Request);
        if (!Test->TestTrue(TEXT("Mesh inspection succeeds"), Inspection->GetBoolField(TEXT("success")))) return true;
        const auto Lods = Inspection->GetArrayField(TEXT("lods"));
        if (Test->TestTrue(TEXT("Imported mesh has render LOD"), !Lods.IsEmpty()))
            Test->TestTrue(TEXT("Imported triangles available"), Lods[0]->AsObject()->GetNumberField(TEXT("triangles")) >= 12);
        Test->TestTrue(TEXT("Imported material slots available"), !Inspection->GetArrayField(TEXT("material_slots")).IsEmpty());
        Test->TestTrue(TEXT("Imported mesh has nonzero cm extent"), Mesh->GetBounds().BoxExtent.GetMin() > 40);
        const FString Filename = FPackageName::LongPackageNameToFilename(Mesh->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
        Test->TestFalse(TEXT("Mesh preview does not save uasset"), IFileManager::Get().FileExists(*Filename));
        return true;
    }
    if (!Test->TestEqual(TEXT("One PNG asset"), Assets.Num(), 1)) return true;
    const FString AssetPath = Assets[0]->AsObject()->GetStringField(TEXT("asset_path"));
    Request->SetStringField(TEXT("asset_path"), AssetPath);
    auto Inspection = UnrealMCPSceneAssets::Inspect(Request);
    Test->TestTrue(TEXT("Texture inspection succeeds"), Inspection->GetBoolField(TEXT("success")));
    Test->TestFalse(TEXT("Linear mask texture"), Inspection->GetBoolField(TEXT("srgb")));
    Test->TestEqual(TEXT("Mask compression"), Inspection->GetStringField(TEXT("compression")), FString(TEXT("TC_Masks")));
    auto Texture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!Test->TestNotNull(TEXT("Imported texture exists in memory"), Texture)) return true;
    const FString Filename = FPackageName::LongPackageNameToFilename(Texture->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
    Test->TestFalse(TEXT("Preview import produces no uasset on disk"), IFileManager::Get().FileExists(*Filename));
    Test->TestEqual(TEXT("Same id retains receipt"), UnrealMCPSceneAssets::Import(Request)->GetStringField(TEXT("state")), State);
    Texture->GetPackage()->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSceneImportTest, "UnrealMCP.Scene.AssetImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSceneImportTest::RunTest(const FString& Parameters)
{
    const FString Id = TEXT("ImportTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SceneTools") / (Id + TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    TArray<FColor> Pixels; Pixels.Init(FColor(120, 80, 40, 255), 16);
    TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(4, 4, MakeArrayView(Pixels), Png);
    if (!TestTrue(TEXT("Create source PNG"), FFileHelper::SaveArrayToFile(Png, *Filename))) return false;
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("request_id"), Id); Request->SetStringField(TEXT("source_file"), Filename);
    Request->SetStringField(TEXT("destination_path"), TEXT("/Engine/Forbidden"));
    TestFalse(TEXT("Engine destination rejected"), UnrealMCPSceneAssets::Import(Request)->GetBoolField(TEXT("success")));
    Request->SetStringField(TEXT("destination_path"), TEXT("/Game/__Dev/SceneTools/") + Id);
    Request->SetStringField(TEXT("compression"), TEXT("masks"));
    TestFalse(TEXT("sRGB mask conflict rejected"), UnrealMCPSceneAssets::Import(Request)->GetBoolField(TEXT("success")));
    Request->SetBoolField(TEXT("srgb"), false); Request->SetNumberField(TEXT("timeout_seconds"), 30);
    auto Started = UnrealMCPSceneAssets::Import(Request);
    FString Error; Started->TryGetStringField(TEXT("error"), Error);
    if (!TestTrue(TEXT("Import accepted: ") + Error, Started->GetBoolField(TEXT("success")))) return false;
    TestTrue(TEXT("Import lock held"), UnrealMCPSceneAssets::IsBusy());
    auto Other = MakeShared<FJsonObject>(*Request); Other->SetStringField(TEXT("request_id"), Id + TEXT("_Other"));
    TestFalse(TEXT("Concurrent import rejected"), UnrealMCPSceneAssets::Import(Other)->GetBoolField(TEXT("success")));
    ADD_LATENT_AUTOMATION_COMMAND(FMCPWaitForSceneImport(this, Request));
    return true;
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCPSceneMeshImportTest, "UnrealMCP.Scene.MeshImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FMCPSceneMeshImportTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const FString Extension : {TEXT("fbx"), TEXT("gltf"), TEXT("glb")}) { Names.Add(Extension); Commands.Add(Extension); }
}

bool FMCPSceneMeshImportTest::RunTest(const FString& Extension)
{
    const FString Id = TEXT("MeshImport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SceneTools") / (Id + TEXT(".") + Extension));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (Extension == TEXT("fbx"))
    {
        auto Task = NewObject<UAssetExportTask>(); Task->Object = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        Task->Exporter = NewObject<UStaticMeshExporterFBX>(); Task->Filename = Filename; Task->bAutomated = true; Task->bPrompt = false;
        Task->bReplaceIdentical = false; Task->bSelected = false;
        if (!TestTrue(TEXT("Export isolated cube FBX fixture"), UExporter::RunAssetExportTask(Task))) return false;
    }
    else
    {
        const TArray<float> Positions = {-0.5f,-1,-1.5f, 0.5f,-1,-1.5f, 0.5f,1,-1.5f, -0.5f,1,-1.5f, -0.5f,-1,1.5f, 0.5f,-1,1.5f, 0.5f,1,1.5f, -0.5f,1,1.5f};
        const TArray<uint16> Indices = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
        TArray<uint8> Binary; Binary.Append(reinterpret_cast<const uint8*>(Positions.GetData()), Positions.Num() * sizeof(float));
        Binary.Append(reinterpret_cast<const uint8*>(Indices.GetData()), Indices.Num() * sizeof(uint16));
        TSharedPtr<FJsonObject> Document;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"name":"Fixture"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"buffers":[{"byteLength":168}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96,"target":34962},{"buffer":0,"byteOffset":96,"byteLength":72,"target":34963}],"accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[-0.5,-1,-1.5],"max":[0.5,1,1.5]},{"bufferView":1,"componentType":5123,"count":36,"type":"SCALAR"}]})")), Document);
        if (Extension == TEXT("gltf")) Document->GetArrayField(TEXT("buffers"))[0]->AsObject()->SetStringField(TEXT("uri"), TEXT("data:application/octet-stream;base64,") + FBase64::Encode(Binary));
        FString Json; FJsonSerializer::Serialize(Document.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        if (Extension == TEXT("gltf"))
        {
            if (!TestTrue(TEXT("Write glTF fixture"), FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
        }
        else
        {
            FTCHARToUTF8 Utf8(*Json); TArray<uint8> JsonBytes; JsonBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
            while (JsonBytes.Num() % 4) JsonBytes.Add(' ');
            TArray<uint8> Glb;
            auto AppendWord = [&](uint32 Value) { for (uint32 Shift = 0; Shift < 32; Shift += 8) Glb.Add(static_cast<uint8>(Value >> Shift)); };
            AppendWord(0x46546c67); AppendWord(2); AppendWord(28 + JsonBytes.Num() + Binary.Num());
            AppendWord(JsonBytes.Num()); AppendWord(0x4e4f534a); Glb.Append(JsonBytes);
            AppendWord(Binary.Num()); AppendWord(0x004e4942); Glb.Append(Binary);
            if (!TestTrue(TEXT("Write GLB fixture"), FFileHelper::SaveArrayToFile(Glb, *Filename))) return false;
        }
    }
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("request_id"), Id); Request->SetStringField(TEXT("source_file"), Filename);
    Request->SetStringField(TEXT("destination_path"), TEXT("/Game/__Dev/SceneTools/") + Id);
    Request->SetBoolField(TEXT("mesh_fixture"), true); Request->SetNumberField(TEXT("timeout_seconds"), 60);
    auto Result = UnrealMCPSceneAssets::Import(Request); FString Error; Result->TryGetStringField(TEXT("error"), Error);
    if (!TestTrue(TEXT("Mesh import accepted: ") + Error, Result->GetBoolField(TEXT("success")))) return false;
    ADD_LATENT_AUTOMATION_COMMAND(FMCPWaitForSceneImport(this, Request));
    return true;
}
class FMCPImportReceiptBatch : public IAutomationLatentCommand
{
public:
    FMCPImportReceiptBatch(FAutomationTestBase* InTest, TSharedPtr<FJsonObject> InBase, int32 InStart, int32 InEnd)
        : Test(InTest), Base(InBase), Next(InStart), End(InEnd) {}

    bool Update() override
    {
        if (Next >= End) return true;
        if (!Current)
        {
            Current = MakeShared<FJsonObject>(*Base);
            Current->SetStringField(TEXT("request_id"), Base->GetStringField(TEXT("request_id")) + FString::Printf(TEXT("_%d"), Next));
            Current->SetStringField(TEXT("destination_path"), Base->GetStringField(TEXT("destination_path")) + FString::Printf(TEXT("/Item_%d"), Next));
            const auto Started = UnrealMCPSceneAssets::Import(Current);
            FString Error; Started->TryGetStringField(TEXT("error"), Error);
            if (!Test->TestTrue(FString::Printf(TEXT("Import %d accepted: %s"), Next + 1, *Error), Started->GetBoolField(TEXT("success")))) return true;
            Deadline = FPlatformTime::Seconds() + 120;
        }
        const auto Receipt = UnrealMCPSceneAssets::Status(Current);
        const FString State = Receipt->GetStringField(TEXT("state"));
        if (State == TEXT("pending") || State == TEXT("running") || State == TEXT("overdue"))
        {
            if (FPlatformTime::Seconds() < Deadline) return false;
            Test->AddError(TEXT("Import receipt regression timed out; no retry")); return true;
        }
        if (!Test->TestEqual(TEXT("Sequential import completed"), State, FString(TEXT("completed")))) return true;
        Test->TestTrue(TEXT("Requested asset save completed"), Receipt->GetBoolField(TEXT("saved")));
        Test->TestTrue(TEXT("Terminal receipt persisted"), Receipt->GetBoolField(TEXT("receipt_persisted")));
        Test->TestTrue(TEXT("In-memory receipt cache remains bounded"), UnrealMCPSceneAssets::CachedReceiptCountForTests() <= 32);
        Test->TestFalse(TEXT("Completed import releases busy state"), UnrealMCPSceneAssets::IsBusy());
        ++Next; Current.Reset();
        if (Next == 40)
        {
            auto OldRequest = MakeShared<FJsonObject>();
            OldRequest->SetStringField(TEXT("project_path"), Base->GetStringField(TEXT("project_path")));
            OldRequest->SetStringField(TEXT("request_id"), Base->GetStringField(TEXT("request_id")) + TEXT("_0"));
            Test->TestFalse(TEXT("Old terminal receipt evicted from memory"), UnrealMCPSceneAssets::HasCachedReceiptForTests(OldRequest->GetStringField(TEXT("request_id"))));
            const int32 DispatchesBefore = UnrealMCPSceneAssets::ImportDispatchCountForTests();
            Test->TestEqual(TEXT("Old receipt still queryable"), UnrealMCPSceneAssets::Status(OldRequest)->GetStringField(TEXT("state")), FString(TEXT("completed")));
            Test->TestEqual(TEXT("Old ID replays without source/destination or reimport"), UnrealMCPSceneAssets::Import(OldRequest)->GetStringField(TEXT("state")), FString(TEXT("completed")));
            Test->TestTrue(TEXT("Disk receipt contains persistence marker"), UnrealMCPSceneAssets::Status(OldRequest)->GetBoolField(TEXT("receipt_persisted")));
            Test->TestEqual(TEXT("Replay never dispatches another import"), UnrealMCPSceneAssets::ImportDispatchCountForTests(), DispatchesBefore);
            Test->TestFalse(TEXT("Disk reads do not refill cache"), UnrealMCPSceneAssets::HasCachedReceiptForTests(OldRequest->GetStringField(TEXT("request_id"))));
        }
        return Next >= End;
    }

private:
    FAutomationTestBase* Test;
    TSharedPtr<FJsonObject> Base, Current;
    int32 Next, End;
    double Deadline = 0;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCPImportReceiptCapacityTest, "UnrealMCP.Scene.ImportReceiptCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FMCPImportReceiptCapacityTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    Names.Add(TEXT("Continuous40")); Commands.Add(TEXT("continuous"));
    Names.Add(TEXT("TwoBatches20")); Commands.Add(TEXT("batches"));
}

bool FMCPImportReceiptCapacityTest::RunTest(const FString& Parameters)
{
    const FString Id = TEXT("ReceiptCapacity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SceneTools") / (Id + TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    TArray<FColor> Pixels; Pixels.Init(FColor(100, 140, 180, 255), 16);
    TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(4, 4, MakeArrayView(Pixels), Png);
    if (!TestTrue(TEXT("Create capacity test PNG"), FFileHelper::SaveArrayToFile(Png, *Filename))) return false;
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Request->SetStringField(TEXT("request_id"), Id); Request->SetStringField(TEXT("source_file"), Filename);
    Request->SetStringField(TEXT("destination_path"), TEXT("/Game/__Dev/SceneTools/") + Id);
    Request->SetBoolField(TEXT("save"), true);
    if (Parameters == TEXT("batches"))
    {
        ADD_LATENT_AUTOMATION_COMMAND(FMCPImportReceiptBatch(this, Request, 0, 20));
        ADD_LATENT_AUTOMATION_COMMAND(FMCPImportReceiptBatch(this, Request, 20, 40));
    }
    else ADD_LATENT_AUTOMATION_COMMAND(FMCPImportReceiptBatch(this, Request, 0, 40));
    return true;
}
struct FMCPCaptureFixture
{
    TUniquePtr<FLevelEditorViewportClient> Client;
    TUniquePtr<FSceneViewport> Viewport;
    TSharedPtr<SViewport> Widget;
    TSharedPtr<FJsonObject> Request;
    FString Filename;
    FIntPoint SourceSize = FIntPoint(640, 480);
    FVector Location;
    FRotator Rotation;
    TWeakObjectPtr<ACameraActor> Camera;
    bool FixedBefore = false;
    bool ExpectedGameView = true;
    bool ExpectFailure = false;
    bool ExpectDetached = false;
    bool CreatedOutputDirectory = false;
    bool DirtyBefore = false;
    double Deadline = 0;
    ~FMCPCaptureFixture()
    {
        if (Client) Client->Viewport = nullptr;
        Viewport.Reset(); Client.Reset();
        if (CreatedOutputDirectory) IFileManager::Get().DeleteDirectory(*Filename, false, false);
    }
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FMCPCheckCapture, FAutomationTestBase*, Test, TSharedPtr<FMCPCaptureFixture>, Fixture);

bool FMCPCheckCapture::Update()
{
    const auto Receipt = UnrealMCPScene::Task(Fixture->Request);
    const FString State = Receipt->GetStringField(TEXT("state"));
    if (State == TEXT("pending") && FPlatformTime::Seconds() < Fixture->Deadline) return false;
    FString Error; Receipt->TryGetStringField(TEXT("error"), Error);
    Test->TestEqual(TEXT("Source viewport dimensions unchanged"), Fixture->Viewport->GetSizeXY(), Fixture->SourceSize);
    Test->TestEqual(TEXT("Source fixed-size mode unchanged"), Fixture->Viewport->HasFixedSize(), Fixture->FixedBefore);
    Test->TestTrue(TEXT("Source client viewport restored"), Fixture->Client->Viewport == (Fixture->ExpectDetached ? nullptr : Fixture->Viewport.Get()));
    Test->TestTrue(TEXT("Source camera position unchanged"), Fixture->Client->GetViewLocation().Equals(Fixture->Location));
    Test->TestTrue(TEXT("Source camera rotation unchanged"), Fixture->Client->GetViewRotation().Equals(Fixture->Rotation));
    Test->TestTrue(TEXT("Camera lock unchanged"), Fixture->Client->GetActiveActorLock().Get() == Fixture->Camera.Get());
    Test->TestEqual(TEXT("Game View unchanged"), Fixture->Client->IsInGameView(), Fixture->ExpectedGameView);
    Test->TestEqual(TEXT("FOV unchanged"), Fixture->Client->ViewFOV, 58.f);
    Test->TestFalse(TEXT("Fixed exposure mode unchanged"), Fixture->Client->ExposureSettings.bFixed);
    Test->TestEqual(TEXT("World not dirtied by capture"), Fixture->Client->GetWorld()->GetPackage()->IsDirty(), Fixture->DirtyBefore);
    Test->TestFalse(TEXT("Capture lock released"), UnrealMCPScene::IsBusy());
    if (Fixture->ExpectFailure)
    {
        Test->TestEqual(TEXT("Changed source causes explicit task failure"), State, FString(TEXT("failed")));
        Test->TestFalse(TEXT("Failure never reports file saved"), Receipt->GetBoolField(TEXT("saved")));
        Test->TestFalse(TEXT("Failure produces no PNG"), IFileManager::Get().FileExists(*Fixture->Filename));
        Test->TestEqual(TEXT("Failed receipt retains request width"), Receipt->GetIntegerField(TEXT("requested_width")), Fixture->Request->GetIntegerField(TEXT("width")));
        Test->TestEqual(TEXT("Failed receipt retains actual target width"), Receipt->GetIntegerField(TEXT("actual_width")), Fixture->Request->GetIntegerField(TEXT("width")));
        return true;
    }
    if (!Test->TestEqual(TEXT("Custom resolution capture completed: ") + Error, State, FString(TEXT("completed")))) return true;
    const int32 Width = Fixture->Request->GetIntegerField(TEXT("width")), Height = Fixture->Request->GetIntegerField(TEXT("height"));
    Test->TestEqual(TEXT("Requested width reported"), Receipt->GetIntegerField(TEXT("requested_width")), Width);
    Test->TestEqual(TEXT("Requested height reported"), Receipt->GetIntegerField(TEXT("requested_height")), Height);
    Test->TestEqual(TEXT("Actual width reported"), Receipt->GetIntegerField(TEXT("actual_width")), Width);
    Test->TestEqual(TEXT("Actual height reported"), Receipt->GetIntegerField(TEXT("actual_height")), Height);
    Test->TestTrue(TEXT("Actual PNG saved"), Receipt->GetBoolField(TEXT("saved")));
    TArray<uint8> Png;
    if (!Test->TestTrue(TEXT("Read real PNG"), FFileHelper::LoadFileToArray(Png, *Receipt->GetStringField(TEXT("file_path"))))) return true;
    auto Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper")).CreateImageWrapper(EImageFormat::PNG);
    if (!Test->TestTrue(TEXT("Parse real PNG"), Wrapper->SetCompressed(Png.GetData(), Png.Num()))) return true;
    Test->TestEqual(TEXT("PNG width"), Wrapper->GetWidth(), static_cast<int64>(Width)); Test->TestEqual(TEXT("PNG height"), Wrapper->GetHeight(), static_cast<int64>(Height));
    TArray64<uint8> Pixels;
    if (!Test->TestTrue(TEXT("Decode PNG pixels"), Wrapper->GetRaw(ERGBFormat::RGBA, 8, Pixels))) return true;
    int32 NonBlack = 0;
    for (int64 Index = 0; Index + 3 < Pixels.Num(); Index += 4) if (Pixels[Index] > 8 || Pixels[Index + 1] > 8 || Pixels[Index + 2] > 8) ++NonBlack;
    Test->TestTrue(TEXT("Scene is not an empty/black render"), NonBlack > Width * Height / 100);
    if (Fixture->Camera.IsValid())
    {
        Test->TestEqual(TEXT("Camera aspect constraint unchanged"), Fixture->Camera->GetCameraComponent()->AspectRatio, 16.f / 9.f);
        Test->TestTrue(TEXT("Camera aspect constraint remains enabled"), Fixture->Camera->GetCameraComponent()->bConstrainAspectRatio);
        Test->TestEqual(TEXT("Letterbox policy reported"), Receipt->GetStringField(TEXT("aspect_ratio_policy")), FString(TEXT("camera_letterbox")));
        const bool HorizontalBars = static_cast<float>(Width) / Height < 16.f / 9.f - 0.01;
        const bool VerticalBars = static_cast<float>(Width) / Height > 16.f / 9.f + 0.01;
        bool BlackEdge = true;
        if (HorizontalBars) for (int32 Column = 0; Column < Width; ++Column)
        {
            const int64 Index = (static_cast<int64>(2) * Width + Column) * 4;
            BlackEdge &= Pixels[Index] <= 2 && Pixels[Index + 1] <= 2 && Pixels[Index + 2] <= 2;
        }
        if (VerticalBars) for (int32 Row = 0; Row < Height; ++Row)
        {
            const int64 Index = (static_cast<int64>(Row) * Width + 2) * 4;
            BlackEdge &= Pixels[Index] <= 2 && Pixels[Index + 1] <= 2 && Pixels[Index + 2] <= 2;
        }
        Test->TestTrue(TEXT("Constrained camera fills unused aspect area with black bars"), BlackEdge);
    }
    return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCPCaptureSizeTest, "UnrealMCP.Scene.CaptureResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FMCPCaptureSizeTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const FString Name : {TEXT("LockedSmaller"), TEXT("LockedLarger"), TEXT("LockedPortrait"), TEXT("LockedWide"),
        TEXT("FreeSmaller"), TEXT("FreeLarger"), TEXT("FreePortrait"), TEXT("FreeNativeSize"), TEXT("Reported1440x1000"), TEXT("FixedSource"), TEXT("FailureGameView"), TEXT("FailureDetached"), TEXT("FailureWrite")})
    { Names.Add(Name); Commands.Add(Name); }
}

bool FMCPCaptureSizeTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("UnrealMCPNoServer"))) { AddError(TEXT("Capture regression requires an isolated -UnrealMCPNoServer editor")); return false; }
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto Cube = World->SpawnActor<AStaticMeshActor>(); Cube->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    auto Fixture = MakeShared<FMCPCaptureFixture>();
    const bool Free = Parameters.StartsWith(TEXT("Free"));
    int32 Width = Parameters.Contains(TEXT("Smaller")) ? 320 : 1440;
    int32 Height = Parameters.Contains(TEXT("Smaller")) ? 240 : 1000;
    if (Parameters.Contains(TEXT("Portrait"))) { Width = 360; Height = 640; }
    if (Parameters.Contains(TEXT("Wide"))) { Width = 1024; Height = 360; }
    if (Parameters == TEXT("FreeNativeSize")) { Width = 640; Height = 480; }
    if (Parameters == TEXT("Reported1440x1000")) Fixture->SourceSize = FIntPoint(2345, 1833);
    if (Parameters == TEXT("FixedSource")) Fixture->Widget = SNew(SViewport);
    Fixture->Client = MakeUnique<FLevelEditorViewportClient>(nullptr);
    Fixture->Viewport = MakeUnique<FSceneViewport>(Fixture->Client.Get(), Fixture->Widget);
    Fixture->Viewport->UpdateViewportRHI(false, Fixture->SourceSize.X, Fixture->SourceSize.Y, EWindowMode::Windowed, PF_B8G8R8A8);
    if (Fixture->Widget) Fixture->Viewport->SetFixedViewportSize(Fixture->SourceSize.X, Fixture->SourceSize.Y);
    Fixture->Client->Viewport = Fixture->Viewport.Get();
    Fixture->Client->SetViewportType(LVT_Perspective); Fixture->Client->SetViewMode(VMI_Unlit); Fixture->Client->SetGameView(true);
    Fixture->Location = FVector(-300, 0, 100); Fixture->Rotation = FRotator(-15, 0, 0);
    if (Free)
    {
        Fixture->Client->SetViewLocation(Fixture->Location); Fixture->Client->SetViewRotation(Fixture->Rotation);
        Fixture->Client->ViewFOV = Fixture->Client->FOVAngle = 58;
    }
    else
    {
        auto Camera = World->SpawnActor<ACameraActor>(Fixture->Location, Fixture->Rotation);
        Camera->GetCameraComponent()->FieldOfView = 58; Camera->GetCameraComponent()->AspectRatio = 16.f / 9.f; Camera->GetCameraComponent()->bConstrainAspectRatio = true;
        Fixture->Camera = Camera; Fixture->Client->SetActorLock(Camera); Fixture->Client->UpdateViewForLockedActor();
    }
    Fixture->FixedBefore = Fixture->Viewport->HasFixedSize();
    const FString Id = TEXT("CaptureResolution_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Fixture->Request = MakeShared<FJsonObject>();
    Fixture->Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Fixture->Request->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    Fixture->Request->SetNumberField(TEXT("viewport_id"), GEditor->GetLevelViewportClients().IndexOfByKey(Fixture->Client.Get()));
    Fixture->Request->SetStringField(TEXT("request_id"), Id); Fixture->Request->SetNumberField(TEXT("width"), Width); Fixture->Request->SetNumberField(TEXT("height"), Height);
    Fixture->Request->SetNumberField(TEXT("warmup_frames"), Parameters == TEXT("Reported1440x1000") ? 48 : 4); Fixture->Request->SetNumberField(TEXT("timeout_seconds"), 60);
    Fixture->Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/UnrealMCP") / (Id + TEXT(".png")));
    const auto Result = UnrealMCPScene::Capture(Fixture->Request);
    FString Error; Result->TryGetStringField(TEXT("error"), Error);
    if (!TestTrue(TEXT("Capture request accepted: ") + Error, Result->GetBoolField(TEXT("success")))) return false;
    Fixture->Deadline = FPlatformTime::Seconds() + 65;
    World->GetPackage()->SetDirtyFlag(false);
    Fixture->DirtyBefore = World->GetPackage()->IsDirty();
    if (Parameters == TEXT("FailureGameView"))
    {
        Fixture->ExpectFailure = true; Fixture->ExpectedGameView = false; Fixture->Client->SetGameView(false);
    }
    if (Parameters == TEXT("FailureDetached"))
    {
        Fixture->ExpectFailure = true; Fixture->ExpectDetached = true; Fixture->Client->Viewport = nullptr;
    }
    if (Parameters == TEXT("FailureWrite"))
    {
        Fixture->ExpectFailure = true;
        Fixture->CreatedOutputDirectory = IFileManager::Get().MakeDirectory(*Fixture->Filename, true);
        TestTrue(TEXT("Create output collision after capture acceptance"), Fixture->CreatedOutputDirectory);
    }
    ADD_LATENT_AUTOMATION_COMMAND(FMCPCheckCapture(this, Fixture));
    return true;
}
#endif