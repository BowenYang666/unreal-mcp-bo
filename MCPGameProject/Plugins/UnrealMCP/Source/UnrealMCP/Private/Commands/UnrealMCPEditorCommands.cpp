#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "LevelEditorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SavePackage.h"
#include "EngineUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Engine/DataAsset.h"
#include "NiagaraSystem.h"
#include "WidgetBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "ActorFactories/ActorFactory.h"
#include "ActorFactories/ActorFactoryBoxVolume.h"
#include "Builders/CubeBuilder.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "ScopedTransaction.h"

namespace
{
    // Returns the editor world (consistent with spawn handlers), falling back to GWorld.
    UWorld* GetMCPEditorWorld()
    {
        if (GEditor)
        {
            if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
            {
                return EditorWorld;
            }
        }
        return GWorld;
    }

    // Matches an actor against a user-supplied identifier, accepting either the
    // internal object name (GetName, e.g. "StaticMeshActor_26") or the Outliner
    // display label (GetActorLabel, e.g. "Cube"). Case-insensitive on the label.
    bool ActorMatchesIdentifier(const AActor* Actor, const FString& Identifier)
    {
        if (!Actor)
        {
            return false;
        }
        if (Actor->GetName() == Identifier)
        {
            return true;
        }
        return Actor->GetActorLabel().Equals(Identifier, ESearchCase::IgnoreCase);
    }

    // Resolves a Blueprint from a user-supplied identifier. Accepts:
    //   1. A full object/package path, e.g. "/Game/Test/AI/BP_FT_PerceptionFullCycle"
    //      (with or without the ".BP_..." object suffix).
    //   2. A bare name, e.g. "BP_MyActor" — tried first under the legacy
    //      "/Game/Blueprints/" folder, then via an AssetRegistry-wide search by name.
    // Returns nullptr if nothing matches. On failure, OutTried lists what was attempted.
    UBlueprint* ResolveBlueprint(const FString& Identifier, TArray<FString>& OutTried)
    {
        if (Identifier.IsEmpty())
        {
            return nullptr;
        }

        // Case 1: looks like a content path.
        if (Identifier.StartsWith(TEXT("/")))
        {
            OutTried.Add(Identifier);
            if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Identifier))
            {
                return BP;
            }
            // Try appending the object name suffix: "/Game/Foo/BP_X" -> "/Game/Foo/BP_X.BP_X"
            if (!Identifier.Contains(TEXT(".")))
            {
                FString ObjName;
                Identifier.Split(TEXT("/"), nullptr, &ObjName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
                const FString WithSuffix = Identifier + TEXT(".") + ObjName;
                OutTried.Add(WithSuffix);
                if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *WithSuffix))
                {
                    return BP;
                }
            }
            return nullptr;
        }

        // Case 2a: legacy default folder.
        const FString LegacyPath = TEXT("/Game/Blueprints/") + Identifier;
        OutTried.Add(LegacyPath);
        if (FPackageName::DoesPackageExist(LegacyPath))
        {
            if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *LegacyPath))
            {
                return BP;
            }
        }

        // Case 2b: AssetRegistry-wide search by asset name.
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

        FARFilter Filter;
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        Filter.PackagePaths.Add(FName(TEXT("/Game")));
        Filter.bRecursivePaths = true;

        TArray<FAssetData> Assets;
        AssetRegistry.GetAssets(Filter, Assets);

        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetName.ToString().Equals(Identifier, ESearchCase::IgnoreCase))
            {
                OutTried.Add(Asset.GetObjectPathString());
                if (UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset()))
                {
                    return BP;
                }
            }
        }

        return nullptr;
    }

    TSharedPtr<FJsonValue> VectorToJson(const FVector& Vector)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
        return MakeShared<FJsonValueArray>(Values);
    }

    TSharedPtr<FJsonValue> RotatorToJson(const FRotator& Rotator)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Add(MakeShared<FJsonValueNumber>(Rotator.Pitch));
        Values.Add(MakeShared<FJsonValueNumber>(Rotator.Yaw));
        Values.Add(MakeShared<FJsonValueNumber>(Rotator.Roll));
        return MakeShared<FJsonValueArray>(Values);
    }

    bool GetValidatedEditorWorld(
        const TSharedPtr<FJsonObject>& Params,
        UWorld*& OutWorld,
        FString& OutLevelPath,
        FString& OutError)
    {
        FString RequestedLevelPath;
        if (!Params->TryGetStringField(TEXT("level_path"), RequestedLevelPath) || RequestedLevelPath.IsEmpty())
        {
            OutError = TEXT("Missing 'level_path' parameter");
            return false;
        }

        int32 DotIndex;
        if (RequestedLevelPath.FindChar('.', DotIndex))
        {
            RequestedLevelPath = RequestedLevelPath.Left(DotIndex);
        }

        UWorld* World = GetMCPEditorWorld();
        if (!World)
        {
            OutError = TEXT("Failed to get editor world");
            return false;
        }
        if (World->WorldType != EWorldType::Editor)
        {
            OutError = FString::Printf(
                TEXT("Navigation command requires the Editor world; current world type is %d"),
                static_cast<int32>(World->WorldType));
            return false;
        }

        const FString CurrentLevelPath = World->GetOutermost()->GetName();
        if (CurrentLevelPath != RequestedLevelPath)
        {
            OutError = FString::Printf(
                TEXT("Current editor level is '%s', not requested level '%s'. Open the requested level first."),
                *CurrentLevelPath,
                *RequestedLevelPath);
            return false;
        }

        OutWorld = World;
        OutLevelPath = CurrentLevelPath;
        return true;
    }

    TSharedPtr<FJsonObject> NavMeshBoundsVolumeToJson(ANavMeshBoundsVolume* Volume)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("actor_name"), Volume->GetName());
        Json->SetStringField(TEXT("actor_label"), Volume->GetActorLabel());
        Json->SetField(TEXT("location"), VectorToJson(Volume->GetActorLocation()));
        Json->SetField(TEXT("rotation"), RotatorToJson(Volume->GetActorRotation()));
        Json->SetField(TEXT("scale"), VectorToJson(Volume->GetActorScale3D()));

        const FBox Bounds = Volume->GetComponentsBoundingBox(true);
        Json->SetBoolField(TEXT("bounds_valid"), Bounds.IsValid != 0);
        if (Bounds.IsValid)
        {
            Json->SetField(TEXT("bounds_min"), VectorToJson(Bounds.Min));
            Json->SetField(TEXT("bounds_max"), VectorToJson(Bounds.Max));
            Json->SetField(TEXT("bounds_center"), VectorToJson(Bounds.GetCenter()));
            Json->SetField(TEXT("full_size"), VectorToJson(Bounds.GetSize()));
        }
        return Json;
    }

    bool ResolveNavAgent(
        const FString& AgentClassPath,
        UObject*& OutAgentObject,
        FNavAgentProperties& OutAgentProperties,
        FString& OutPropertiesSource,
        FString& OutResolvedClassPath,
        FString& OutError)
    {
        UClass* AgentClass = LoadObject<UClass>(nullptr, *AgentClassPath);
        if (!AgentClass)
        {
            TArray<FString> Tried;
            if (UBlueprint* Blueprint = ResolveBlueprint(AgentClassPath, Tried))
            {
                AgentClass = Blueprint->GeneratedClass;
            }
        }
        if (!AgentClass)
        {
            OutError = FString::Printf(TEXT("Failed to resolve agent class: %s"), *AgentClassPath);
            return false;
        }

        UObject* AgentCDO = AgentClass->GetDefaultObject();
        const INavAgentInterface* NavAgent = Cast<INavAgentInterface>(AgentCDO);
        if (!NavAgent)
        {
            OutError = FString::Printf(
                TEXT("Agent class does not implement INavAgentInterface: %s"),
                *AgentClass->GetPathName());
            return false;
        }

        OutAgentObject = AgentCDO;
        OutAgentProperties = NavAgent->GetNavAgentPropertiesRef();
        OutPropertiesSource = TEXT("cdo_nav_agent_interface");
        if (!OutAgentProperties.IsValid())
        {
            const APawn* PawnCDO = Cast<APawn>(AgentCDO);
            const UCapsuleComponent* CollisionCapsule = PawnCDO
                ? PawnCDO->FindComponentByClass<UCapsuleComponent>()
                : nullptr;
            if (CollisionCapsule)
            {
                OutAgentProperties.AgentRadius = CollisionCapsule->GetScaledCapsuleRadius();
                OutAgentProperties.AgentHeight = CollisionCapsule->GetScaledCapsuleHalfHeight() * 2.0f;
                OutPropertiesSource = TEXT("cdo_nav_agent_interface_with_capsule");
            }
        }
        OutResolvedClassPath = AgentClass->GetPathName();
        return true;
    }

    TSharedPtr<FJsonObject> NavAgentToJson(
        const FString& RequestedClassPath,
        const FString& ResolvedClassPath,
        const FString& PropertiesSource,
        const FNavAgentProperties& Properties,
        const ANavigationData* NavData)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("requested_class_path"), RequestedClassPath);
        Json->SetStringField(TEXT("resolved_class_path"), ResolvedClassPath);
        Json->SetStringField(TEXT("properties_source"), PropertiesSource);
        Json->SetNumberField(TEXT("radius"), Properties.AgentRadius);
        Json->SetNumberField(TEXT("height"), Properties.AgentHeight);
        Json->SetNumberField(TEXT("step_height"), Properties.AgentStepHeight);
        Json->SetBoolField(TEXT("can_crouch"), Properties.bCanCrouch);
        Json->SetBoolField(TEXT("can_jump"), Properties.bCanJump);
        Json->SetBoolField(TEXT("can_walk"), Properties.bCanWalk);
        Json->SetBoolField(TEXT("can_swim"), Properties.bCanSwim);
        Json->SetBoolField(TEXT("can_fly"), Properties.bCanFly);
        if (NavData)
        {
            Json->SetStringField(TEXT("nav_data_name"), NavData->GetName());
            Json->SetStringField(TEXT("nav_data_class"), NavData->GetClass()->GetPathName());
            Json->SetStringField(TEXT("nav_data_path"), NavData->GetPathName());
        }
        return Json;
    }

    TMap<FString, FString> NavigationBuildRequestIds;

    TSharedPtr<FJsonObject> NavigationStatusToJson(UWorld* World, const FString& LevelPath)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetBoolField(TEXT("success"), true);
        Json->SetStringField(TEXT("level_path"), LevelPath);

        UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        const bool bInProgress = NavSystem && NavSystem->IsNavigationBuildInProgress();
        ANavigationData* DefaultNavData = NavSystem
            ? NavSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
            : nullptr;
        const bool bRequested = NavigationBuildRequestIds.Contains(LevelPath);

        Json->SetBoolField(TEXT("navigation_system_available"), NavSystem != nullptr);
        Json->SetBoolField(TEXT("build_requested"), bRequested);
        Json->SetBoolField(TEXT("build_in_progress"), bInProgress);
        Json->SetBoolField(TEXT("nav_data_available"), DefaultNavData != nullptr);
        Json->SetStringField(
            TEXT("state"),
            !NavSystem ? TEXT("failed")
                : bInProgress ? TEXT("in_progress")
                : DefaultNavData ? TEXT("completed")
                : bRequested ? TEXT("failed")
                : TEXT("not_built"));

        if (bRequested)
        {
            Json->SetStringField(TEXT("request_id"), NavigationBuildRequestIds[LevelPath]);
        }
        if (NavSystem)
        {
            Json->SetBoolField(TEXT("dirty_areas_queued"), NavSystem->HasDirtyAreasQueued());
            Json->SetNumberField(TEXT("dirty_area_count"), NavSystem->GetNumDirtyAreas());
            Json->SetNumberField(TEXT("remaining_build_tasks"), NavSystem->GetNumRemainingBuildTasks());
            Json->SetNumberField(TEXT("running_build_tasks"), NavSystem->GetNumRunningBuildTasks());
        }
        if (DefaultNavData)
        {
            Json->SetStringField(TEXT("nav_data_name"), DefaultNavData->GetName());
            Json->SetStringField(TEXT("nav_data_class"), DefaultNavData->GetClass()->GetPathName());
            Json->SetStringField(TEXT("nav_data_path"), DefaultNavData->GetPathName());
        }
        return Json;
    }

    FString NavigationQueryResultToString(ENavigationQueryResult::Type Result)
    {
        switch (Result)
        {
        case ENavigationQueryResult::Success:
            return TEXT("success");
        case ENavigationQueryResult::Fail:
            return TEXT("fail");
        case ENavigationQueryResult::Error:
            return TEXT("error");
        default:
            return TEXT("invalid");
        }
    }
}

FUnrealMCPEditorCommands::FUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Actor manipulation commands
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor") || CommandType == TEXT("create_actor"))
    {
        if (CommandType == TEXT("create_actor"))
        {
            UE_LOG(LogTemp, Warning, TEXT("'create_actor' command is deprecated and will be removed in a future version. Please use 'spawn_actor' instead."));
        }
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    else if (CommandType == TEXT("get_actor_properties"))
    {
        return HandleGetActorProperties(Params);
    }
    else if (CommandType == TEXT("set_actor_property"))
    {
        return HandleSetActorProperty(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    // Editor viewport commands
    else if (CommandType == TEXT("focus_viewport"))
    {
        return HandleFocusViewport(Params);
    }
    else if (CommandType == TEXT("take_screenshot"))
    {
        return HandleTakeScreenshot(Params);
    }
    // Editor state commands
    else if (CommandType == TEXT("get_unsaved_changes"))
    {
        return HandleGetUnsavedChanges(Params);
    }
    else if (CommandType == TEXT("save_asset"))
    {
        return HandleSaveAsset(Params);
    }
    else if (CommandType == TEXT("rename_asset"))
    {
        return HandleRenameAsset(Params);
    }
    else if (CommandType == TEXT("move_asset"))
    {
        return HandleMoveAsset(Params);
    }
    else if (CommandType == TEXT("close_editor"))
    {
        return HandleCloseEditor(Params);
    }
    else if (CommandType == TEXT("open_asset"))
    {
        return HandleOpenAsset(Params);
    }
    // Navigation editor/query commands
    else if (CommandType == TEXT("set_nav_mesh_bounds_volume"))
    {
        return HandleSetNavMeshBoundsVolume(Params);
    }
    else if (CommandType == TEXT("list_nav_mesh_bounds_volumes"))
    {
        return HandleListNavMeshBoundsVolumes(Params);
    }
    else if (CommandType == TEXT("build_navigation"))
    {
        return HandleBuildNavigation(Params);
    }
    else if (CommandType == TEXT("get_navigation_status"))
    {
        return HandleGetNavigationStatus(Params);
    }
    else if (CommandType == TEXT("project_point_to_navigation"))
    {
        return HandleProjectPointToNavigation(Params);
    }
    else if (CommandType == TEXT("find_navigation_path"))
    {
        return HandleFindNavigationPath(Params);
    }
    // Level management commands
    else if (CommandType == TEXT("open_level"))
    {
        return HandleOpenLevel(Params);
    }
    else if (CommandType == TEXT("save_level"))
    {
        return HandleSaveLevel(Params);
    }
    else if (CommandType == TEXT("create_level"))
    {
        return HandleCreateLevel(Params);
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetMCPEditorWorld();

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (AActor* Actor : AllActors)
    {
        if (Actor)
        {
            ActorArray.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);

    // Include which level/world these actors came from, so callers can tell
    // exactly which map was queried (avoids "missing actors" confusion).
    if (World)
    {
        if (UPackage* WorldPackage = World->GetOutermost())
        {
            const FString PackageName = WorldPackage->GetName();
            ResultObj->SetStringField(TEXT("level_package_path"), PackageName);
            // Short name derived from the package path is more reliable than GetMapName(),
            // which may carry a streaming/PIE prefix.
            ResultObj->SetStringField(TEXT("level_name"), FPackageName::GetShortName(PackageName));
        }
        else
        {
            ResultObj->SetStringField(TEXT("level_name"), World->GetMapName());
        }
        if (ULevel* CurrentLevel = World->GetCurrentLevel())
        {
            ResultObj->SetStringField(TEXT("current_level"), CurrentLevel->GetOutermost()->GetName());
        }
    }
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetMCPEditorWorld(), AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (AActor* Actor : AllActors)
    {
        // Match against the internal name OR the Outliner display label (case-insensitive).
        if (Actor && (Actor->GetName().Contains(Pattern)
            || Actor->GetActorLabel().Contains(Pattern, ESearchCase::IgnoreCase)))
        {
            MatchingActors.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    // Get actor name (required parameter)
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Get optional transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Create the actor based on type
    AActor* NewActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Check if an actor with this name already exists
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;

    if (ActorType == TEXT("StaticMeshActor"))
    {
        NewActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("PointLight"))
    {
        NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("SpotLight"))
    {
        NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("DirectionalLight"))
    {
        NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("CameraActor"))
    {
        NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
    }

    if (NewActor)
    {
        // Set scale (since SpawnActor only takes location and rotation)
        FTransform Transform = NewActor->GetTransform();
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);

        // Return the created actor's details
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetMCPEditorWorld(), AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (ActorMatchesIdentifier(Actor, ActorName))
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FUnrealMCPCommonUtils::ActorToJsonObject(Actor);
            
            // Delete the actor
            Actor->Destroy();
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            return ResultObj;
        }
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetMCPEditorWorld(), AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (ActorMatchesIdentifier(Actor, ActorName))
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetMCPEditorWorld(), AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (ActorMatchesIdentifier(Actor, ActorName))
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Always return detailed properties for this command
    return FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetMCPEditorWorld(), AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (ActorMatchesIdentifier(Actor, ActorName))
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get property name
    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Get property value
    if (!Params->HasField(TEXT("property_value")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
    }
    
    TSharedPtr<FJsonValue> PropertyValue = Params->Values.FindRef(TEXT("property_value"));
    
    // Set the property using our utility function
    FString ErrorMessage;
    if (FUnrealMCPCommonUtils::SetObjectProperty(TargetActor, PropertyName, PropertyValue, ErrorMessage))
    {
        // Property set successfully
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("actor"), ActorName);
        ResultObj->SetStringField(TEXT("property"), PropertyName);
        ResultObj->SetBoolField(TEXT("success"), true);
        
        // Also include the full actor details
        ResultObj->SetObjectField(TEXT("actor_details"), FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true));
        return ResultObj;
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the blueprint
    if (BlueprintName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint name is empty"));
    }

    // Resolve the blueprint: accepts a full asset path (e.g. "/Game/Test/AI/BP_X"),
    // a bare name under /Game/Blueprints/, or any blueprint in the project by name.
    TArray<FString> Tried;
    UBlueprint* Blueprint = ResolveBlueprint(BlueprintName, Tried);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Blueprint '%s' not found. Provide a full asset path (e.g. /Game/Test/AI/BP_X) or a unique blueprint name. Tried: %s"),
            *BlueprintName, *FString::Join(Tried, TEXT(", "))));
    }

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));
    SpawnTransform.SetScale3D(Scale);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform, SpawnParams);
    if (NewActor)
    {
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFocusViewport(const TSharedPtr<FJsonObject>& Params)
{
    // Get target actor name if provided
    FString TargetActorName;
    bool HasTargetActor = Params->TryGetStringField(TEXT("target"), TargetActorName);

    // Get location if provided
    FVector Location(0.0f, 0.0f, 0.0f);
    bool HasLocation = false;
    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        HasLocation = true;
    }

    // Get distance
    float Distance = 1000.0f;
    if (Params->HasField(TEXT("distance")))
    {
        Distance = Params->GetNumberField(TEXT("distance"));
    }

    // Get orientation if provided
    FRotator Orientation(0.0f, 0.0f, 0.0f);
    bool HasOrientation = false;
    if (Params->HasField(TEXT("orientation")))
    {
        Orientation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("orientation"));
        HasOrientation = true;
    }

    // Get the active viewport
    FLevelEditorViewportClient* ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
    if (!ViewportClient)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get active viewport"));
    }

    // If we have a target actor, focus on it
    if (HasTargetActor)
    {
        // Find the actor
        AActor* TargetActor = nullptr;
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(GetMCPEditorWorld(), AActor::StaticClass(), AllActors);
        
        for (AActor* Actor : AllActors)
        {
            if (ActorMatchesIdentifier(Actor, TargetActorName))
            {
                TargetActor = Actor;
                break;
            }
        }

        if (!TargetActor)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *TargetActorName));
        }

        // Focus on the actor
        ViewportClient->SetViewLocation(TargetActor->GetActorLocation() - FVector(Distance, 0.0f, 0.0f));
    }
    // Otherwise use the provided location
    else if (HasLocation)
    {
        ViewportClient->SetViewLocation(Location - FVector(Distance, 0.0f, 0.0f));
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Either 'target' or 'location' must be provided"));
    }

    // Set orientation if provided
    if (HasOrientation)
    {
        ViewportClient->SetViewRotation(Orientation);
    }

    // Force viewport to redraw
    ViewportClient->Invalidate();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    // Get file path parameter
    FString FilePath;
    if (!Params->TryGetStringField(TEXT("filepath"), FilePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'filepath' parameter"));
    }
    
    // Ensure the file path has a proper extension
    if (!FilePath.EndsWith(TEXT(".png")))
    {
        FilePath += TEXT(".png");
    }

    // Get the active viewport
    if (GEditor && GEditor->GetActiveViewport())
    {
        FViewport* Viewport = GEditor->GetActiveViewport();
        TArray<FColor> Bitmap;
        FIntRect ViewportRect(0, 0, Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y);
        
        if (Viewport->ReadPixels(Bitmap, FReadSurfaceDataFlags(), ViewportRect))
        {
            TArray<uint8> CompressedBitmap;
            FImageUtils::ThumbnailCompressImageArray(Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y, Bitmap, CompressedBitmap);
            
            if (FFileHelper::SaveArrayToFile(CompressedBitmap, *FilePath))
            {
                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("filepath"), FilePath);
                return ResultObj;
            }
        }
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to take screenshot"));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetUnsavedChanges(const TSharedPtr<FJsonObject>& Params)
{
    TArray<UPackage*> DirtyContent;
    TArray<UPackage*> DirtyMaps;
    UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyContent);
    UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyMaps);

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);

    int32 TotalDirty = DirtyContent.Num() + DirtyMaps.Num();
    ResultJson->SetNumberField(TEXT("total_unsaved"), TotalDirty);
    ResultJson->SetNumberField(TEXT("unsaved_content_count"), DirtyContent.Num());
    ResultJson->SetNumberField(TEXT("unsaved_map_count"), DirtyMaps.Num());

    // Content packages (blueprints, materials, textures, etc.)
    TArray<TSharedPtr<FJsonValue>> ContentArray;
    for (UPackage* Package : DirtyContent)
    {
        if (Package)
        {
            TSharedPtr<FJsonObject> PkgObj = MakeShareable(new FJsonObject);
            PkgObj->SetStringField(TEXT("name"), Package->GetName());
            PkgObj->SetStringField(TEXT("path"), Package->GetPathName());
            ContentArray.Add(MakeShareable(new FJsonValueObject(PkgObj)));
        }
    }
    ResultJson->SetArrayField(TEXT("unsaved_content"), ContentArray);

    // Map/level packages
    TArray<TSharedPtr<FJsonValue>> MapArray;
    for (UPackage* Package : DirtyMaps)
    {
        if (Package)
        {
            TSharedPtr<FJsonObject> PkgObj = MakeShareable(new FJsonObject);
            PkgObj->SetStringField(TEXT("name"), Package->GetName());
            PkgObj->SetStringField(TEXT("path"), Package->GetPathName());
            MapArray.Add(MakeShareable(new FJsonValueObject(PkgObj)));
        }
    }
    ResultJson->SetArrayField(TEXT("unsaved_maps"), MapArray);

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSaveAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    UPackage* Package = Asset->GetOutermost();
    if (!Package)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Could not find package for asset"));
    }

    FString PackageFilename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Could not resolve package filename"));
    }

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);

    if (!bSaved)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath));
    }

    // Clear dirty flag so editor doesn't prompt "unsaved changes" after MCP save
    Package->SetDirtyFlag(false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Asset saved successfully: %s"), *AssetPath));
    ResultJson->SetStringField(TEXT("path"), AssetPath);
    return ResultJson;
}

namespace
{
    FString NormalizeContentAssetPath(const FString& InPath)
    {
        FString Path = InPath.TrimStartAndEnd();
        int32 DotIndex = INDEX_NONE;
        if (Path.FindChar(TEXT('.'), DotIndex))
        {
            Path = Path.Left(DotIndex);
        }
        while (Path.EndsWith(TEXT("/")))
        {
            Path.LeftChopInline(1);
        }
        return Path;
    }

    bool ValidateContentAssetPath(const FString& Path, FString& OutError)
    {
        if (!Path.StartsWith(TEXT("/Game/")) || Path.Contains(TEXT("\\")))
        {
            OutError = FString::Printf(TEXT("Asset path must be a full /Game/... content path: %s"), *Path);
            return false;
        }

        FString PackagePath;
        FString AssetName;
        if (!Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
            || PackagePath.IsEmpty() || AssetName.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Asset path must include an asset name: %s"), *Path);
            return false;
        }
        return true;
    }

    TSharedPtr<FJsonObject> RenameOrMoveAsset(const FString& SourcePath, const FString& DestinationPath, const TCHAR* Operation)
    {
        FString Error;
        if (!ValidateContentAssetPath(SourcePath, Error) || !ValidateContentAssetPath(DestinationPath, Error))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
        if (SourcePath.Equals(DestinationPath, ESearchCase::CaseSensitive))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Source and destination asset paths are identical"));
        }
        if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Source asset not found: %s"), *SourcePath));
        }
        if (UEditorAssetLibrary::DoesAssetExist(DestinationPath))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Destination asset already exists: %s"), *DestinationPath));
        }

        FString DestinationFolder;
        FString DestinationName;
        DestinationPath.Split(TEXT("/"), &DestinationFolder, &DestinationName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationFolder)
            && !UEditorAssetLibrary::MakeDirectory(DestinationFolder))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to create destination folder: %s"), *DestinationFolder));
        }

        UObject* SourceAsset = UEditorAssetLibrary::LoadAsset(SourcePath);
        const FString AssetClass = SourceAsset ? SourceAsset->GetClass()->GetName() : FString();
        if (!UEditorAssetLibrary::RenameAsset(SourcePath, DestinationPath))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to %s asset from '%s' to '%s'"), Operation, *SourcePath, *DestinationPath));
        }

        TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
        ResultJson->SetStringField(TEXT("operation"), Operation);
        ResultJson->SetStringField(TEXT("source_path"), SourcePath);
        ResultJson->SetStringField(TEXT("destination_path"), DestinationPath);
        if (!AssetClass.IsEmpty())
        {
            ResultJson->SetStringField(TEXT("asset_class"), AssetClass);
        }
        ResultJson->SetBoolField(TEXT("success"), true);
        return ResultJson;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleRenameAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString RawAssetPath;
    FString NewName;
    if (!Params->TryGetStringField(TEXT("asset_path"), RawAssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("new_name"), NewName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_name' parameter"));
    }

    NewName = NewName.TrimStartAndEnd();
    if (NewName.IsEmpty() || NewName.Contains(TEXT("/")) || NewName.Contains(TEXT("\\")) || NewName.Contains(TEXT(".")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("new_name must be an asset name only, without a path, slash, or object suffix"));
    }

    const FString SourcePath = NormalizeContentAssetPath(RawAssetPath);
    FString SourceFolder;
    FString SourceName;
    if (!SourcePath.Split(TEXT("/"), &SourceFolder, &SourceName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("asset_path must include an asset name"));
    }
    return RenameOrMoveAsset(SourcePath, SourceFolder + TEXT("/") + NewName, TEXT("rename"));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleMoveAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString RawAssetPath;
    FString RawDestinationFolder;
    if (!Params->TryGetStringField(TEXT("asset_path"), RawAssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("destination_folder"), RawDestinationFolder))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'destination_folder' parameter"));
    }

    const FString SourcePath = NormalizeContentAssetPath(RawAssetPath);
    FString SourceFolder;
    FString AssetName;
    if (!SourcePath.Split(TEXT("/"), &SourceFolder, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("asset_path must include an asset name"));
    }

    FString DestinationFolder = NormalizeContentAssetPath(RawDestinationFolder);
    if (!DestinationFolder.StartsWith(TEXT("/Game")) || DestinationFolder.Contains(TEXT("\\")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("destination_folder must be a full /Game/... folder path: %s"), *DestinationFolder));
    }
    return RenameOrMoveAsset(SourcePath, DestinationFolder + TEXT("/") + AssetName, TEXT("move"));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleCloseEditor(const TSharedPtr<FJsonObject>& Params)
{
    bool bSaveAll = true;
    Params->TryGetBoolField(TEXT("save_all"), bSaveAll);

    int32 SavedCount = 0;
    TArray<FString> FailedSaves;

    if (bSaveAll)
    {
        TArray<UPackage*> DirtyContent;
        TArray<UPackage*> DirtyMaps;
        UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyContent);
        UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyMaps);

        // Save dirty content packages
        for (UPackage* Package : DirtyContent)
        {
            if (Package)
            {
                FString PackageFilename;
                if (FPackageName::TryConvertLongPackageNameToFilename(
                        Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
                {
                    FSavePackageArgs SaveArgs;
                    SaveArgs.TopLevelFlags = RF_Standalone;
                    if (UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
                    {
                        Package->SetDirtyFlag(false);
                        SavedCount++;
                    }
                    else
                    {
                        FailedSaves.Add(Package->GetName());
                    }
                }
            }
        }

        // Save dirty map packages
        for (UPackage* Package : DirtyMaps)
        {
            if (Package)
            {
                FString PackageFilename;
                if (FPackageName::TryConvertLongPackageNameToFilename(
                        Package->GetName(), PackageFilename, FPackageName::GetMapPackageExtension()))
                {
                    FSavePackageArgs SaveArgs;
                    SaveArgs.TopLevelFlags = RF_Standalone;
                    if (UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
                    {
                        Package->SetDirtyFlag(false);
                        SavedCount++;
                    }
                    else
                    {
                        FailedSaves.Add(Package->GetName());
                    }
                }
            }
        }
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("closing"), true);
    ResultJson->SetNumberField(TEXT("saved_count"), SavedCount);

    if (FailedSaves.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> FailArr;
        for (const FString& Name : FailedSaves)
        {
            FailArr.Add(MakeShared<FJsonValueString>(Name));
        }
        ResultJson->SetArrayField(TEXT("failed_saves"), FailArr);
    }

    // Close all open asset editors before shutdown to prevent ACCESS_VIOLATION
    // crash during Slate teardown. Users can reopen needed editors via open_asset.
    if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
    {
        AssetEditorSub->CloseAllAssetEditors();
    }

    // Schedule engine exit after giving Slate a few frames to finish cleaning up.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [](float) -> bool
        {
            RequestEngineExit(TEXT("MCP close_editor"));
            return false; // one-shot
        }),
        0.5f // 500ms to let Slate finish widget teardown
    );

    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleOpenAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
    }

    UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSub)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AssetEditorSubsystem not available"));
    }

    bool bOpened = AssetEditorSub->OpenEditorForAsset(Asset);
    if (!bOpened)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to open editor for: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
    ResultJson->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetNavMeshBoundsVolume(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = nullptr;
    FString LevelPath;
    FString Error;
    if (!GetValidatedEditorWorld(Params, World, LevelPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* LocationValues = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* SizeValues = nullptr;
    if (!Params->TryGetArrayField(TEXT("location"), LocationValues) || LocationValues->Num() < 3)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'location' must contain three numbers"));
    }
    if (!Params->TryGetArrayField(TEXT("full_size"), SizeValues) || SizeValues->Num() < 3)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'full_size' must contain three numbers"));
    }

    const FVector Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    const FVector FullSize = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("full_size"));
    if (!FMath::IsFinite(FullSize.X) || !FMath::IsFinite(FullSize.Y) || !FMath::IsFinite(FullSize.Z)
        || FullSize.X <= 0.0 || FullSize.Y <= 0.0 || FullSize.Z <= 0.0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'full_size' values must be finite and greater than zero"));
    }

    FString ActorIdentifier = TEXT("NavMeshBoundsVolume");
    Params->TryGetStringField(TEXT("actor_name"), ActorIdentifier);
    TArray<ANavMeshBoundsVolume*> Matches;
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
    {
        if (ActorMatchesIdentifier(*It, ActorIdentifier))
        {
            Matches.Add(*It);
        }
    }
    if (Matches.Num() > 1)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Multiple NavMeshBoundsVolume actors match '%s'; use a unique actor_name"), *ActorIdentifier));
    }

    const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "SetNavMeshBoundsVolume", "Set Nav Mesh Bounds Volume"));
    ANavMeshBoundsVolume* Volume = Matches.Num() == 1 ? Matches[0] : nullptr;
    const bool bCreated = Volume == nullptr;
    if (!Volume)
    {
        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Name = MakeUniqueObjectName(World->PersistentLevel, ANavMeshBoundsVolume::StaticClass(), FName(*ActorIdentifier));
        SpawnParameters.OverrideLevel = World->PersistentLevel;
        SpawnParameters.ObjectFlags |= RF_Transactional;
        Volume = World->SpawnActor<ANavMeshBoundsVolume>(
            ANavMeshBoundsVolume::StaticClass(),
            Location,
            FRotator::ZeroRotator,
            SpawnParameters);
        if (!Volume)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create NavMeshBoundsVolume"));
        }
        Volume->SetActorLabel(ActorIdentifier);
    }

    Volume->Modify();
    Volume->SetActorLocation(Location);
    Volume->SetActorRotation(FRotator::ZeroRotator);
    Volume->SetActorScale3D(FVector::OneVector);

    UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(GetTransientPackage());
    CubeBuilder->X = FullSize.X;
    CubeBuilder->Y = FullSize.Y;
    CubeBuilder->Z = FullSize.Z;
    UActorFactory::CreateBrushForVolumeActor(Volume, CubeBuilder);
    Volume->PostEditMove(true);

    if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
    {
        NavSystem->OnNavigationBoundsUpdated(Volume);
    }
    World->GetOutermost()->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("level_path"), LevelPath);
    ResultJson->SetStringField(TEXT("operation"), bCreated ? TEXT("created") : TEXT("updated"));
    ResultJson->SetObjectField(TEXT("volume"), NavMeshBoundsVolumeToJson(Volume));
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleListNavMeshBoundsVolumes(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = nullptr;
    FString LevelPath;
    FString Error;
    if (!GetValidatedEditorWorld(Params, World, LevelPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TArray<TSharedPtr<FJsonValue>> Volumes;
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
    {
        Volumes.Add(MakeShared<FJsonValueObject>(NavMeshBoundsVolumeToJson(*It)));
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("level_path"), LevelPath);
    ResultJson->SetNumberField(TEXT("count"), Volumes.Num());
    ResultJson->SetArrayField(TEXT("volumes"), Volumes);
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleBuildNavigation(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = nullptr;
    FString LevelPath;
    FString Error;
    if (!GetValidatedEditorWorld(Params, World, LevelPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavSystem)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NavigationSystemV1 is not available in the current editor world"));
    }
    if (NavSystem->IsNavigationBuildInProgress())
    {
        TSharedPtr<FJsonObject> ResultJson = NavigationStatusToJson(World, LevelPath);
        ResultJson->SetBoolField(TEXT("accepted"), false);
        ResultJson->SetStringField(TEXT("message"), TEXT("A navigation build is already in progress"));
        return ResultJson;
    }

    const FString RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    NavigationBuildRequestIds.Add(LevelPath, RequestId);
    NavSystem->Build();

    TSharedPtr<FJsonObject> ResultJson = NavigationStatusToJson(World, LevelPath);
    ResultJson->SetBoolField(TEXT("accepted"), true);
    ResultJson->SetStringField(TEXT("request_id"), RequestId);
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetNavigationStatus(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = nullptr;
    FString LevelPath;
    FString Error;
    if (!GetValidatedEditorWorld(Params, World, LevelPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    return NavigationStatusToJson(World, LevelPath);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleProjectPointToNavigation(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = nullptr;
    FString LevelPath;
    FString Error;
    if (!GetValidatedEditorWorld(Params, World, LevelPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FString AgentClassPath;
    if (!Params->TryGetStringField(TEXT("agent_class_path"), AgentClassPath) || AgentClassPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'agent_class_path' parameter"));
    }
    const TArray<TSharedPtr<FJsonValue>>* PointValues = nullptr;
    if (!Params->TryGetArrayField(TEXT("point"), PointValues) || PointValues->Num() < 3)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'point' must contain three numbers"));
    }

    UObject* AgentObject = nullptr;
    FNavAgentProperties AgentProperties;
    FString PropertiesSource;
    FString ResolvedClassPath;
    if (!ResolveNavAgent(AgentClassPath, AgentObject, AgentProperties, PropertiesSource, ResolvedClassPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavSystem)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NavigationSystemV1 is not available in the current editor world"));
    }
    const FVector Point = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("point"));
    const FVector Extent = Params->HasField(TEXT("extent"))
        ? FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("extent"))
        : INVALID_NAVEXTENT;
    ANavigationData* NavData = NavSystem->GetNavDataForProps(AgentProperties, Point, Extent);
    if (!NavData)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No navigation data matches the requested agent"));
    }

    FNavLocation ProjectedLocation;
    const bool bProjected = NavSystem->ProjectPointToNavigation(Point, ProjectedLocation, Extent, NavData);
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetBoolField(TEXT("projected"), bProjected);
    ResultJson->SetStringField(TEXT("level_path"), LevelPath);
    ResultJson->SetField(TEXT("input_point"), VectorToJson(Point));
    ResultJson->SetField(TEXT("query_extent"), VectorToJson(Extent));
    if (bProjected)
    {
        ResultJson->SetField(TEXT("projected_point"), VectorToJson(ProjectedLocation.Location));
        ResultJson->SetStringField(TEXT("node_ref"), FString::Printf(TEXT("%llu"), ProjectedLocation.NodeRef));
    }
    ResultJson->SetObjectField(
        TEXT("agent"),
        NavAgentToJson(AgentClassPath, ResolvedClassPath, PropertiesSource, AgentProperties, NavData));
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFindNavigationPath(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = nullptr;
    FString LevelPath;
    FString Error;
    if (!GetValidatedEditorWorld(Params, World, LevelPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FString AgentClassPath;
    if (!Params->TryGetStringField(TEXT("agent_class_path"), AgentClassPath) || AgentClassPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'agent_class_path' parameter"));
    }
    const TArray<TSharedPtr<FJsonValue>>* StartValues = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* EndValues = nullptr;
    if (!Params->TryGetArrayField(TEXT("start"), StartValues) || StartValues->Num() < 3
        || !Params->TryGetArrayField(TEXT("end"), EndValues) || EndValues->Num() < 3)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'start' and 'end' must each contain three numbers"));
    }

    UObject* AgentObject = nullptr;
    FNavAgentProperties AgentProperties;
    FString PropertiesSource;
    FString ResolvedClassPath;
    if (!ResolveNavAgent(AgentClassPath, AgentObject, AgentProperties, PropertiesSource, ResolvedClassPath, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavSystem)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NavigationSystemV1 is not available in the current editor world"));
    }

    const FVector Start = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("start"));
    const FVector End = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("end"));
    const FVector Extent = Params->HasField(TEXT("extent"))
        ? FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("extent"))
        : INVALID_NAVEXTENT;
    ANavigationData* NavData = NavSystem->GetNavDataForProps(AgentProperties, Start, Extent);
    if (!NavData)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No navigation data matches the requested agent"));
    }

    FNavLocation ProjectedStart;
    FNavLocation ProjectedEnd;
    const bool bStartProjected = NavSystem->ProjectPointToNavigation(Start, ProjectedStart, Extent, NavData);
    const bool bEndProjected = NavSystem->ProjectPointToNavigation(End, ProjectedEnd, Extent, NavData);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("level_path"), LevelPath);
    ResultJson->SetField(TEXT("start"), VectorToJson(Start));
    ResultJson->SetField(TEXT("end"), VectorToJson(End));
    ResultJson->SetField(TEXT("query_extent"), VectorToJson(Extent));
    ResultJson->SetBoolField(TEXT("start_projected"), bStartProjected);
    ResultJson->SetBoolField(TEXT("end_projected"), bEndProjected);
    if (bStartProjected)
    {
        ResultJson->SetField(TEXT("projected_start"), VectorToJson(ProjectedStart.Location));
    }
    if (bEndProjected)
    {
        ResultJson->SetField(TEXT("projected_end"), VectorToJson(ProjectedEnd.Location));
    }
    ResultJson->SetObjectField(
        TEXT("agent"),
        NavAgentToJson(AgentClassPath, ResolvedClassPath, PropertiesSource, AgentProperties, NavData));

    if (!bStartProjected || !bEndProjected)
    {
        ResultJson->SetStringField(TEXT("query_result"), TEXT("not_run"));
        ResultJson->SetBoolField(TEXT("path_valid"), false);
        ResultJson->SetBoolField(TEXT("partial"), false);
        ResultJson->SetBoolField(TEXT("complete"), false);
        ResultJson->SetStringField(
            TEXT("failure_reason"),
            !bStartProjected && !bEndProjected
                ? TEXT("start_and_end_projection_failed")
                : !bStartProjected ? TEXT("start_projection_failed") : TEXT("end_projection_failed"));
        return ResultJson;
    }

    FPathFindingQuery Query(AgentObject, *NavData, ProjectedStart.Location, ProjectedEnd.Location);
    const FPathFindingResult PathResult = NavSystem->FindPathSync(AgentProperties, Query);
    const bool bPathValid = PathResult.Path.IsValid() && PathResult.Path->IsValid();
    const bool bPartial = bPathValid && PathResult.Path->IsPartial();
    const bool bComplete = PathResult.IsSuccessful() && bPathValid && !bPartial;
    ResultJson->SetStringField(TEXT("query_result"), NavigationQueryResultToString(PathResult.Result));
    ResultJson->SetBoolField(TEXT("path_valid"), bPathValid);
    ResultJson->SetBoolField(TEXT("partial"), bPartial);
    ResultJson->SetBoolField(TEXT("complete"), bComplete);

    TArray<TSharedPtr<FJsonValue>> PathPoints;
    if (bPathValid)
    {
        for (const FNavPathPoint& PathPoint : PathResult.Path->GetPathPoints())
        {
            PathPoints.Add(VectorToJson(PathPoint.Location));
        }
        ResultJson->SetNumberField(TEXT("length"), PathResult.Path->GetLength());
        ResultJson->SetNumberField(TEXT("cost"), PathResult.Path->GetCost());
    }
    ResultJson->SetArrayField(TEXT("path_points"), PathPoints);
    if (!bComplete)
    {
        ResultJson->SetStringField(
            TEXT("failure_reason"),
            bPartial ? TEXT("partial_path")
                : !bPathValid ? TEXT("invalid_path")
                : NavigationQueryResultToString(PathResult.Result));
    }
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleOpenLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath;
    if (!Params->TryGetStringField(TEXT("level_path"), LevelPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'level_path' parameter"));
    }

    // Normalize: strip any object suffix ("/Game/Maps/Foo.Foo" -> "/Game/Maps/Foo").
    FString PackagePath = LevelPath;
    int32 DotIndex;
    if (PackagePath.FindChar('.', DotIndex))
    {
        PackagePath = PackagePath.Left(DotIndex);
    }

    if (!FPackageName::DoesPackageExist(PackagePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Level not found: %s"), *PackagePath));
    }

    // Optionally save dirty packages before switching (default: prompt avoided, no save).
    bool bSaveDirty = false;
    Params->TryGetBoolField(TEXT("save_dirty"), bSaveDirty);
    if (bSaveDirty)
    {
        FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ false, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ true);
    }

    // Load the map into the editor (blocking). This replaces the current editor world.
    const bool bLoaded = FEditorFileUtils::LoadMap(PackagePath, /*bLoadAsTemplate*/ false, /*bShowProgress*/ true);
    if (!bLoaded)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to open level: %s"), *PackagePath));
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("level_package_path"), PackagePath);
    // Derive the short name from the package path (reliable; GetMapName() can return
    // a streaming-prefixed or stale value right after a map switch).
    ResultJson->SetStringField(TEXT("level_name"), FPackageName::GetShortName(PackagePath));
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSaveLevel(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetMCPEditorWorld();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    UPackage* WorldPackage = World->GetOutermost();
    if (!WorldPackage)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Could not find world package"));
    }

    const FString PackageName = WorldPackage->GetName();

    // Reject unsaved/untitled maps (e.g. "/Temp/Untitled") which have no on-disk file.
    if (!FPackageName::IsValidLongPackageName(PackageName) || PackageName.StartsWith(TEXT("/Temp/")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Current level is untitled/unsaved (%s). Use 'Save As' in the editor first."), *PackageName));
    }

    FString PackageFilename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, FPackageName::GetMapPackageExtension()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Could not resolve level package filename"));
    }

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    const bool bSaved = UPackage::SavePackage(WorldPackage, World, *PackageFilename, SaveArgs);
    if (!bSaved)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to save level: %s"), *PackageName));
    }

    WorldPackage->SetDirtyFlag(false);

    // Force the in-memory Asset Registry to re-scan the just-saved map file so that
    // its updated tags (e.g. Functional Test info from newly added FT actors) are
    // picked up. Without this, Session Frontend's test discovery — which reads the
    // Asset Registry — keeps showing stale data until the editor is restarted.
    bool bRescanned = false;
    {
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        TArray<FString> ModifiedFiles;
        ModifiedFiles.Add(FPaths::ConvertRelativePathToFull(PackageFilename));
        AssetRegistryModule.Get().ScanModifiedAssetFiles(ModifiedFiles);
        bRescanned = true;
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("level_package_path"), PackageName);
    ResultJson->SetBoolField(TEXT("asset_registry_rescanned"), bRescanned);
    ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Level saved: %s"), *PackageName));
    ResultJson->SetStringField(TEXT("note"), TEXT("If a new Functional Test was added, click 'Refresh Tests' in Session Frontend to see it (no editor restart needed)."));
    return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleCreateLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath;
    if (!Params->TryGetStringField(TEXT("level_path"), LevelPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'level_path' parameter"));
    }

    // Normalize: strip any object suffix ("/Game/Maps/Foo.Foo" -> "/Game/Maps/Foo").
    FString PackagePath = LevelPath;
    int32 DotIndex;
    if (PackagePath.FindChar('.', DotIndex))
    {
        PackagePath = PackagePath.Left(DotIndex);
    }

    if (!PackagePath.StartsWith(TEXT("/")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("level_path must be a content path starting with '/Game/', got: %s"), *PackagePath));
    }

    if (FPackageName::DoesPackageExist(PackagePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Level already exists: %s (use open_level to load it)"), *PackagePath));
    }

    ULevelEditorSubsystem* LevelSub = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
    if (!LevelSub)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("LevelEditorSubsystem not available"));
    }

    // Optional template: full path to an existing .umap to clone, e.g.
    // "/Engine/Maps/Templates/OpenWorld" or "/Game/Maps/MyTemplate".
    FString TemplatePath;
    const bool bHasTemplate = Params->TryGetStringField(TEXT("template_path"), TemplatePath) && !TemplatePath.IsEmpty();

    // Partitioned world flag (UE5 World Partition). Default off; ignored when template is given
    // because NewLevelFromTemplate inherits the partitioning of the source.
    bool bPartitioned = false;
    Params->TryGetBoolField(TEXT("partitioned"), bPartitioned);

    bool bCreated = false;
    if (bHasTemplate)
    {
        // Strip object suffix from template too.
        FString TemplatePackage = TemplatePath;
        int32 TDot;
        if (TemplatePackage.FindChar('.', TDot))
        {
            TemplatePackage = TemplatePackage.Left(TDot);
        }
        if (!FPackageName::DoesPackageExist(TemplatePackage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Template level not found: %s"), *TemplatePackage));
        }
        bCreated = LevelSub->NewLevelFromTemplate(PackagePath, TemplatePackage);
    }
    else
    {
        bCreated = LevelSub->NewLevel(PackagePath, bPartitioned);
    }

    if (!bCreated)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create level: %s"), *PackagePath));
    }

    // NewLevel/NewLevelFromTemplate both save and open the new level into the editor.
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("level_package_path"), PackagePath);
    ResultJson->SetStringField(TEXT("level_name"), FPackageName::GetShortName(PackagePath));
    if (bHasTemplate)
    {
        ResultJson->SetStringField(TEXT("template_path"), TemplatePath);
    }
    ResultJson->SetBoolField(TEXT("partitioned"), bPartitioned && !bHasTemplate);
    return ResultJson;
}