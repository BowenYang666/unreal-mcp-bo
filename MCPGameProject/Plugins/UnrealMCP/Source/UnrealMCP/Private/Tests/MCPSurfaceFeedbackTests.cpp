#include "Tests/MCPAssetPropertyTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Commands/UnrealMCPAssetProperties.h"
#include "Commands/UnrealMCPPhysicalMaterialCommands.h"
#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPNiagaraCommands.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EditorAssetLibrary.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundCue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
    TSharedPtr<FJsonObject> Json(const FString& Text)
    {
        TSharedPtr<FJsonObject> Result;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
        return Result;
    }

    TSharedPtr<FJsonObject> Inspect(const FString& Path, const FString& Field)
    {
        auto Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), Path);
        Params->SetArrayField(TEXT("property_paths"), {MakeShared<FJsonValueString>(Field)});
        auto Result = UnrealMCPAssetProperties::Read(Params);
        return Result->GetBoolField(TEXT("success")) ? Result->GetArrayField(TEXT("properties"))[0]->AsObject() : Result;
    }

    TSharedPtr<FJsonObject> Patch(const FString& Path, const FString& Field, TSharedPtr<FJsonValue> Value,
        TSharedPtr<FJsonValue> Expected = nullptr, bool Save = false)
    {
        auto Change = MakeShared<FJsonObject>();
        Change->SetStringField(TEXT("path"), Field);
        Change->SetField(TEXT("value"), Value);
        if (Expected) Change->SetField(TEXT("expected_value"), Expected);
        auto Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), Path);
        Params->SetBoolField(TEXT("save"), Save);
        Params->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
        return UnrealMCPAssetProperties::Write(Params);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPSurfaceFeedbackTest, "UnrealMCP.SurfaceFeedback.EndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSurfaceFeedbackTest::RunTest(const FString& Parameters)
{
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPSurfaceTestRoot="), Root))
        Root = TEXT("/Game/__Dev/SurfaceFeedback_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!Root.StartsWith(TEXT("/Game/__Dev/SurfaceFeedback_"))) { AddError(TEXT("Test root must be isolated under /Game/__Dev/SurfaceFeedback_")); return false; }
    FString ClassPath = UMCPImpactTestProfile::StaticClass()->GetPathName();
    FParse::Value(FCommandLine::Get(), TEXT("MCPImpactProfileClass="), ClassPath);
    const FString ProfilePath = Root + TEXT("/DA_Impact");
    if (FParse::Param(FCommandLine::Get(), TEXT("MCPSurfaceReload")))
    {
        UObject* Profile = UEditorAssetLibrary::LoadAsset(ProfilePath);
        if (!TestNotNull(TEXT("Reload persisted profile"), Profile)) return false;
        TestEqual(TEXT("Reloaded class identity"), Profile->GetClass()->GetPathName(), ClassPath);
        auto Surfaces = Inspect(ProfilePath, TEXT("Surfaces"));
        if (!TestTrue(TEXT("Reloaded map is readable"), Surfaces->HasField(TEXT("value")))) return false;
        TestEqual(TEXT("Reloaded map has two entries"), Surfaces->GetArrayField(TEXT("value")).Num(), 2);
        for (const auto& EntryValue : Surfaces->GetArrayField(TEXT("value")))
        {
            auto Entry = EntryValue->AsObject();
            auto Value = Entry->GetObjectField(TEXT("value"));
            if (Entry->GetStringField(TEXT("key")) == TEXT("SurfaceType1"))
            {
                TestEqual(TEXT("Persisted inherited Niagara reference"), Value->GetStringField(TEXT("System")), Root + TEXT("/NS_Reference.NS_Reference"));
                TestEqual(TEXT("Persisted inherited sound reference"), Value->GetStringField(TEXT("Sound")), Root + TEXT("/S_Reference.S_Reference"));
                TestEqual(TEXT("Persisted inherited Scale"), Value->GetNumberField(TEXT("Scale")), 2.5);
                auto Decal = Value->GetObjectField(TEXT("Decal"));
                TestEqual(TEXT("Persisted nested material"), Decal->GetStringField(TEXT("Material")), Root + TEXT("/M_Decal.M_Decal"));
                TestEqual(TEXT("Persisted nested hold time"), Decal->GetNumberField(TEXT("HoldTime")), 4.0);
                TestEqual(TEXT("Persisted nested size"), Decal->GetObjectField(TEXT("Size"))->GetNumberField(TEXT("Y")), 8.0);
            }
            else
            {
                TestEqual(TEXT("Other persisted key"), Entry->GetStringField(TEXT("key")), FString(TEXT("SurfaceType2")));
                TestTrue(TEXT("Persisted null Niagara"), Value->Values[TEXT("System")]->IsNull());
                TestTrue(TEXT("Persisted null sound"), Value->Values[TEXT("Sound")]->IsNull());
                TestTrue(TEXT("Persisted nested null material"), Value->GetObjectField(TEXT("Decal"))->Values[TEXT("Material")]->IsNull());
            }
        }
        auto Metal = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/PM_Metal")));
        auto Concrete = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/PM_Concrete")));
        if (!TestNotNull(TEXT("Reload Metal PM"), Metal) || !TestNotNull(TEXT("Reload Concrete PM"), Concrete)) return false;
        TestTrue(TEXT("Persisted Metal surface"), Metal->SurfaceType == SurfaceType1);
        TestTrue(TEXT("Persisted Concrete surface"), Concrete->SurfaceType == SurfaceType2);
        auto Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/M_Surface")));
        auto Instance = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/MI_Surface")));
        auto Decal = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/M_Decal")));
        if (!TestNotNull(TEXT("Reload Material"), Material) || !TestNotNull(TEXT("Reload MI"), Instance) || !TestNotNull(TEXT("Reload decal"), Decal)) return false;
        TestEqual(TEXT("Persisted Material physical reference"), Material->GetPhysicalMaterial(), Metal);
        TestEqual(TEXT("Persisted MI physical reference"), Instance->GetPhysicalMaterial(), Concrete);
        TestTrue(TEXT("Persisted decal domain"), Decal->MaterialDomain == MD_DeferredDecal);
        auto Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/BP_Surface")));
        if (!TestNotNull(TEXT("Reload Blueprint"), Blueprint)) return false;
        UStaticMeshComponent* Template = nullptr;
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            if (Node->GetVariableName() == TEXT("Collider")) Template = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
        if (TestNotNull(TEXT("Reload SCS template"), Template))
            TestEqual(TEXT("Persisted BP override"), Template->BodyInstance.GetPhysMaterialOverride(), Metal);
        TestFalse(TEXT("Read-only profile reload remains clean"), Profile->GetPackage()->IsDirty());
        AddInfo(TEXT("Fresh-process surface feedback persistence verified: ") + Root);
        return true;
    }
    auto Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), ProfilePath);
    Params->SetStringField(TEXT("class_path"), ClassPath);
    if (!TestTrue(TEXT("Create actual requested DataAsset class"), UnrealMCPAssetProperties::CreateDataAsset(Params)->GetBoolField(TEXT("success")))) return false;
    UObject* Profile = UEditorAssetLibrary::LoadAsset(ProfilePath);
    TestEqual(TEXT("Created class identity"), Profile->GetClass()->GetPathName(), ClassPath);
    TestFalse(TEXT("Create refuses overwrite"), UnrealMCPAssetProperties::CreateDataAsset(Params)->GetBoolField(TEXT("success")));
    Params->SetStringField(TEXT("asset_path"), Root + TEXT("/Invalid"));
    Params->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Actor"));
    TestFalse(TEXT("Create refuses arbitrary UObject type"), UnrealMCPAssetProperties::CreateDataAsset(Params)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Invalid class creates no package"), UEditorAssetLibrary::DoesAssetExist(Root + TEXT("/Invalid")));

    FUnrealMCPMaterialCommands Materials;
    auto MaterialParams = Json(TEXT("{\"material_domain\":\"DeferredDecal\",\"blend_mode\":\"Translucent\",\"shading_model\":\"DefaultLit\"}"));
    const FString DecalPath = Root + TEXT("/M_Decal");
    MaterialParams->SetStringField(TEXT("asset_path"), DecalPath);
    Materials.HandleCommand(TEXT("create_material"), MaterialParams);
    auto DecalMaterial = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(DecalPath));
    if (!TestNotNull(TEXT("Existing material tool creates decal"), DecalMaterial)) return false;
    TestTrue(TEXT("DeferredDecal domain honored"), DecalMaterial->MaterialDomain == MD_DeferredDecal);
    if (!TestTrue(TEXT("Save test-owned decal material"), UEditorAssetLibrary::SaveLoadedAsset(DecalMaterial, false))) return false;
    const FString SystemPath = Root + TEXT("/NS_Reference");
    FUnrealMCPNiagaraCommands Niagara;
    auto SystemParams = MakeShared<FJsonObject>();
    SystemParams->SetStringField(TEXT("asset_path"), SystemPath);
    Niagara.HandleCommand(TEXT("create_niagara_system"), SystemParams);
    auto System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!TestNotNull(TEXT("Factory initialized Niagara fixture"), System)) return false;
    if (!TestTrue(TEXT("Save Niagara fixture"), UEditorAssetLibrary::SaveLoadedAsset(System, false))) return false;
    const FString SoundPath = Root + TEXT("/S_Reference");
    auto Sound = NewObject<USoundCue>(CreatePackage(*SoundPath), TEXT("S_Reference"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Sound);
    if (!TestTrue(TEXT("Save sound reference fixture"), UEditorAssetLibrary::SaveLoadedAsset(Sound, false))) return false;

    auto DefaultField = Inspect(ProfilePath, TEXT("Default"));
    if (!TestTrue(TEXT("Inherited Default structure readable"), DefaultField->HasField(TEXT("value")))) return false;
    FString DefaultJson;
    FJsonSerializer::Serialize(DefaultField->GetObjectField(TEXT("value")).ToSharedRef(), TJsonWriterFactory<>::Create(&DefaultJson));
    auto MetalValue = Json(DefaultJson);
    TestTrue(TEXT("Inherited fields present"), MetalValue->HasField(TEXT("System")) && MetalValue->HasField(TEXT("Sound")) && MetalValue->HasField(TEXT("Scale")));
    MetalValue->SetStringField(TEXT("System"), System->GetPathName());
    MetalValue->SetStringField(TEXT("Sound"), Sound->GetPathName());
    MetalValue->SetNumberField(TEXT("Scale"), 2.5);
    MetalValue->GetObjectField(TEXT("Decal"))->SetStringField(TEXT("Material"), DecalMaterial->GetPathName());
    MetalValue->GetObjectField(TEXT("Decal"))->SetNumberField(TEXT("HoldTime"), 4);
    MetalValue->GetObjectField(TEXT("Decal"))->SetObjectField(TEXT("Size"), Json(TEXT("{\"X\":2,\"Y\":8,\"Z\":9}")));
    auto ConcreteValue = Json(DefaultJson);
    ConcreteValue->SetField(TEXT("System"), MakeShared<FJsonValueNull>());
    ConcreteValue->SetField(TEXT("Sound"), MakeShared<FJsonValueNull>());
    ConcreteValue->GetObjectField(TEXT("Decal"))->SetField(TEXT("Material"), MakeShared<FJsonValueNull>());
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (const auto& Pair : TArray<TPair<FString, TSharedPtr<FJsonObject>>>{{TEXT("SurfaceType1"), MetalValue}, {TEXT("SurfaceType2"), ConcreteValue}})
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("key"), Pair.Key); Entry->SetObjectField(TEXT("value"), Pair.Value);
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    auto Empty = MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>());
    auto MapValue = MakeShared<FJsonValueArray>(Entries);
    if (!TestTrue(TEXT("Enum-key inherited map writes and saves"), Patch(ProfilePath, TEXT("Surfaces"), MapValue, Empty, true)->GetBoolField(TEXT("success")))) return false;
    auto MapRead = Inspect(ProfilePath, TEXT("Surfaces"));
    TestTrue(TEXT("Map schema describes key and inherited value"), MapRead->GetObjectField(TEXT("schema"))->GetObjectField(TEXT("value"))->GetObjectField(TEXT("fields"))->HasField(TEXT("Scale")));
    TestEqual(TEXT("Two surface entries"), MapRead->GetArrayField(TEXT("value")).Num(), 2);
    TestFalse(TEXT("Stale expected map refused"), Patch(ProfilePath, TEXT("Surfaces"), Empty, Empty)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Conflict leaves clean package"), Profile->GetPackage()->IsDirty());
    auto Reversed = Entries; Algo::Reverse(Reversed);
    auto Same = Patch(ProfilePath, TEXT("Surfaces"), MapValue, MakeShared<FJsonValueArray>(Reversed));
    TestTrue(TEXT("Expected map equality ignores entry order"), Same->GetBoolField(TEXT("success")));
    TestFalse(TEXT("No-op map not modified"), Same->GetBoolField(TEXT("modified")));
    auto Duplicate = Entries; Duplicate.Add(Entries[0]);
    TestFalse(TEXT("Duplicate key rejected"), Patch(ProfilePath, TEXT("Surfaces"), MakeShared<FJsonValueArray>(Duplicate))->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Rejected map remains clean"), Profile->GetPackage()->IsDirty());
    auto BadValue = Json(DefaultJson); BadValue->SetStringField(TEXT("System"), Sound->GetPathName());
    auto BadEntry = MakeShared<FJsonObject>(); BadEntry->SetStringField(TEXT("key"), TEXT("SurfaceType1")); BadEntry->SetObjectField(TEXT("value"), BadValue);
    TestFalse(TEXT("Wrong nested reference type rejected"), Patch(ProfilePath, TEXT("Surfaces"), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueObject>(BadEntry)}))->GetBoolField(TEXT("success")));
    BadEntry->SetObjectField(TEXT("value"), MetalValue);
    BadEntry->SetStringField(TEXT("key"), TEXT("SurfaceType_NotValid"));
    TestFalse(TEXT("Invalid enum map key rejected"), Patch(ProfilePath, TEXT("Surfaces"), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueObject>(BadEntry)}))->GetBoolField(TEXT("success")));
    TestFalse(TEXT("All rejected map writes preserve clean package"), Profile->GetPackage()->IsDirty());
    TestTrue(TEXT("Clear whole map with expected entries"), Patch(ProfilePath, TEXT("Surfaces"), Empty, MapValue)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Empty map readback"), Inspect(ProfilePath, TEXT("Surfaces"))->GetArrayField(TEXT("value")).Num(), 0);
    TestTrue(TEXT("Restore whole map without saving unrelated changes"), Patch(ProfilePath, TEXT("Surfaces"), MapValue, Empty)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Explicitly save restored test profile"), UEditorAssetLibrary::SaveLoadedAsset(Profile, false));
    FString DefaultAfter;
    FJsonSerializer::Serialize(Inspect(ProfilePath, TEXT("Default"))->GetObjectField(TEXT("value")).ToSharedRef(), TJsonWriterFactory<>::Create(&DefaultAfter));
    TestEqual(TEXT("Unspecified Default untouched"), DefaultAfter, DefaultJson);

    const FString MetalPath = Root + TEXT("/PM_Metal");
    const FString ConcretePath = Root + TEXT("/PM_Concrete");
    for (const auto& Pair : TArray<TPair<FString, FString>>{{MetalPath, TEXT("SurfaceType1")}, {ConcretePath, TEXT("SurfaceType2")}})
    {
        auto Create = MakeShared<FJsonObject>(); Create->SetStringField(TEXT("asset_path"), Pair.Key);
        TestTrue(TEXT("Create PhysicalMaterial"), UnrealMCPAssetProperties::CreatePhysicalMaterial(Create)->GetBoolField(TEXT("success")));
        TestTrue(TEXT("Set PhysicalMaterial surface"), Patch(Pair.Key, TEXT("SurfaceType"), MakeShared<FJsonValueString>(Pair.Value), MakeShared<FJsonValueString>(TEXT("SurfaceType_Default")), true)->GetBoolField(TEXT("success")));
        TestFalse(TEXT("PhysicalMaterial overwrite refused"), UnrealMCPAssetProperties::CreatePhysicalMaterial(Create)->GetBoolField(TEXT("success")));
        TestFalse(TEXT("PhysicalMaterial nonallowlisted field refused"), Patch(Pair.Key, TEXT("DebugColor"), MakeShared<FJsonValueString>(TEXT("red")))->GetBoolField(TEXT("success")));
    }

    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->GetPackage()->Rename(*(Root + TEXT("/L_Collision")));
    const FString LevelPath = World->GetPackage()->GetName();
    auto Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto Wall = World->SpawnActor<AStaticMeshActor>(FVector(0,0,100), FRotator::ZeroRotator);
    auto Floor = World->SpawnActor<AStaticMeshActor>(FVector(500,0,0), FRotator::ZeroRotator);
    for (auto Actor : {Wall, Floor})
    {
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    }
    Wall->SetActorScale3D(FVector(.2,2,2)); Floor->SetActorScale3D(FVector(3,3,.1));
    auto Assign = [&](AStaticMeshActor* Actor, const FString& MaterialPath)
    {
        auto Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("level_path"), LevelPath); Request->SetStringField(TEXT("actor_name"), Actor->GetName());
        Request->SetStringField(TEXT("component_name"), Actor->GetStaticMeshComponent()->GetName());
        Request->SetStringField(TEXT("physical_material_path"), MaterialPath);
        return UnrealMCPPhysicalMaterial::AssignLevelComponent(Request);
    };
    auto Ray = [&](bool Ground, bool Complex)
    {
        auto Request = Json(Ground ? TEXT("{\"start\":[500,0,200],\"end\":[500,0,-100]}") : TEXT("{\"start\":[-200,0,100],\"end\":[200,0,100]}"));
        Request->SetStringField(TEXT("level_path"), LevelPath); Request->SetBoolField(TEXT("trace_complex"), Complex);
        return UnrealMCPPhysicalMaterial::Trace(Request);
    };
    TestTrue(TEXT("Assign wall override"), Assign(Wall, MetalPath)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Assign floor override"), Assign(Floor, ConcretePath)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Wall ray really blocks"), Ray(false, false)->GetBoolField(TEXT("blocking_hit")));
    TestTrue(TEXT("Floor ray really blocks"), Ray(true, false)->GetBoolField(TEXT("blocking_hit")));
    TestEqual(TEXT("Wall actual PM reference"), Ray(false, false)->GetStringField(TEXT("physical_material")), MetalPath + TEXT(".PM_Metal"));
    TestEqual(TEXT("Floor actual PM reference"), Ray(true, false)->GetStringField(TEXT("physical_material")), ConcretePath + TEXT(".PM_Concrete"));
    TestEqual(TEXT("Wall actual simple hit surface"), Ray(false, false)->GetStringField(TEXT("surface_type")), FString(TEXT("SurfaceType1")));
    TestEqual(TEXT("Floor actual simple hit surface"), Ray(true, false)->GetStringField(TEXT("surface_type")), FString(TEXT("SurfaceType2")));
    TestTrue(TEXT("Change live wall override"), Assign(Wall, ConcretePath)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Existing wall physics updated immediately"), Ray(false, false)->GetStringField(TEXT("surface_type")), FString(TEXT("SurfaceType2")));

    MaterialParams = Json(TEXT("{}")); MaterialParams->SetStringField(TEXT("asset_path"), Root + TEXT("/M_Surface"));
    Materials.HandleCommand(TEXT("create_material"), MaterialParams);
    auto Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/M_Surface")));
    auto InstanceParams = MakeShared<FJsonObject>();
    InstanceParams->SetStringField(TEXT("asset_path"), Root + TEXT("/MI_Surface"));
    InstanceParams->SetStringField(TEXT("parent_material_path"), Root + TEXT("/M_Surface"));
    Materials.HandleCommand(TEXT("create_material_instance"), InstanceParams);
    auto Instance = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/MI_Surface")));
    Wall->GetStaticMeshComponent()->SetMaterial(0, Material); Floor->GetStaticMeshComponent()->SetMaterial(0, Instance);
    Assign(Wall, TEXT("")); Assign(Floor, TEXT(""));
    auto AssignAsset = [&](const FString& Path, const FString& PM) -> TSharedPtr<FJsonObject>
    {
        UObject* Target = UEditorAssetLibrary::LoadAsset(Path);
        if (!TestNotNull(TEXT("Test-owned material exists"), Target)) return MakeShared<FJsonObject>();
        TestTrue(TEXT("Explicitly save test-owned material before guarded assignment"), UEditorAssetLibrary::SaveLoadedAsset(Target, false));
        auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("asset_path"), Path);
        Request->SetStringField(TEXT("physical_material_path"), PM);
        auto Result = UnrealMCPPhysicalMaterial::AssignMaterial(Request);
        if (!Result->GetBoolField(TEXT("success"))) AddError(Path + TEXT(": ") + Result->GetStringField(TEXT("error")));
        return Result;
    };
    TestTrue(TEXT("Assign material PM"), AssignAsset(Root + TEXT("/M_Surface"), MetalPath)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Assign instance PM"), AssignAsset(Root + TEXT("/MI_Surface"), ConcretePath)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Wall actual complex material surface"), Ray(false, true)->GetStringField(TEXT("surface_type")), FString(TEXT("SurfaceType1")));
    TestEqual(TEXT("Floor actual complex MI surface"), Ray(true, true)->GetStringField(TEXT("surface_type")), FString(TEXT("SurfaceType2")));
    TestTrue(TEXT("Clear MI override"), AssignAsset(Root + TEXT("/MI_Surface"), TEXT(""))->GetBoolField(TEXT("success")));
    TestEqual(TEXT("MI collision falls back to parent immediately"), Ray(true, true)->GetStringField(TEXT("surface_type")), FString(TEXT("SurfaceType1")));
    TestEqual(TEXT("Actual hit returns parent PM"), Ray(true, true)->GetStringField(TEXT("physical_material")), UEditorAssetLibrary::LoadAsset(MetalPath)->GetPathName());
    TestTrue(TEXT("Restore floor material surface"), AssignAsset(Root + TEXT("/MI_Surface"), ConcretePath)->GetBoolField(TEXT("success")));

    FUnrealMCPBlueprintCommands Blueprints;
    const FString BPPath = Root + TEXT("/BP_Surface");
    auto BPParams = Json(TEXT("{\"parent_class\":\"Actor\"}")); BPParams->SetStringField(TEXT("name"), BPPath);
    Blueprints.HandleCommand(TEXT("create_blueprint"), BPParams);
    BPParams = Json(TEXT("{\"component_name\":\"Collider\",\"component_type\":\"StaticMeshComponent\"}")); BPParams->SetStringField(TEXT("blueprint_path"), BPPath);
    Blueprints.HandleCommand(TEXT("add_component_to_blueprint"), BPParams);
    BPParams = Json(TEXT("{\"component_name\":\"Collider\",\"property_name\":\"PhysMaterialOverride\"}"));
    BPParams->SetStringField(TEXT("blueprint_path"), BPPath); BPParams->SetStringField(TEXT("property_value"), MetalPath);
    auto BPResult = Blueprints.HandleCommand(TEXT("set_component_property"), BPParams);
    TestTrue(TEXT("Reuse Blueprint component setter"), BPResult->GetBoolField(TEXT("success")));
    TestEqual(TEXT("BP result reads compiled template"), BPResult->GetStringField(TEXT("after")), MetalPath + TEXT(".PM_Metal"));
    auto BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
    UStaticMeshComponent* Template = nullptr;
    if (BP) for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes()) if (Node->GetVariableName() == TEXT("Collider")) Template = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
    if (TestNotNull(TEXT("SCS template exists"), Template)) TestEqual(TEXT("BP override applied natively"), Template->BodyInstance.GetPhysMaterialOverride(), Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(MetalPath)));
    if (BP) UEditorAssetLibrary::SaveLoadedAsset(BP, false);
    World->GetPackage()->SetDirtyFlag(false);
    AddInfo(FString::Printf(TEXT("Surface feedback assertions completed; profile class %s; test assets %s"), *ClassPath, *Root));
    return true;
}
#endif