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