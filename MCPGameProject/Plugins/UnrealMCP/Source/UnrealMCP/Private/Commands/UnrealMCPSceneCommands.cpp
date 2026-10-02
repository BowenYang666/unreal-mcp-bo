#include "Commands/UnrealMCPSceneCommands.h"
#include "Commands/UnrealMCPSceneAssets.h"
#include "Commands/UnrealMCPAssetProperties.h"
#include "AssetCompilingManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Editor/Transactor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/Level.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/SpotLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "EditorAssetLibrary.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Slate/SceneViewport.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ContentStreaming.h"
#include "Interfaces/IPluginManager.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace UnrealMCPScene
{
namespace
{
    TMap<FString, TSharedPtr<FJsonObject>> Tasks;
    bool CaptureBusy = false;
    FString LastUndoToken;
    TWeakObjectPtr<UWorld> LastUndoWorld;

    bool CanWriteTarget(UObject* Target)
    {
        AActor* Actor = Cast<AActor>(Target);
        if (auto Component = Cast<UActorComponent>(Target))
        {
            Actor = Component->GetOwner();
            if (Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript || Component->CreationMethod == EComponentCreationMethod::UserConstructionScript) return false;
        }
        return Actor && !Actor->GetClass()->ClassGeneratedBy;
    }
    FString ProjectPath() { return FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()); }

    TSharedPtr<FJsonObject> Failure(const FString& Message)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetBoolField(TEXT("modified"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("stage"), TEXT("preflight"));
        Result->SetStringField(TEXT("error"), Message);
        return Result;
    }

    UWorld* ResolveWorld(const TSharedPtr<FJsonObject>& Params, FString& Error)
    {
        FString Project, Level;
        if (!Params->TryGetStringField(TEXT("project_path"), Project) || FPaths::IsRelative(Project)
            || !FPaths::IsSamePath(Project, ProjectPath()))
        { Error = TEXT("project_path must exactly identify this editor's .uproject"); return nullptr; }
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor)
        { Error = TEXT("Scene operations require an editor world outside PIE/Simulate"); return nullptr; }
        if (!Params->TryGetStringField(TEXT("level_path"), Level) || World->GetPackage()->GetName() != Level)
        { Error = TEXT("level_path does not match the current editor map"); return nullptr; }
        return World;
    }

    FLevelEditorViewportClient* ResolveViewport(UWorld* World, const TSharedPtr<FJsonObject>& Params, FString& Error)
    {
        int32 Index;
        if (!Params->TryGetNumberField(TEXT("viewport_id"), Index) || !GEditor->GetLevelViewportClients().IsValidIndex(Index))
        { Error = TEXT("Use viewport_id from get_editor_context"); return nullptr; }
        auto Client = GEditor->GetLevelViewportClients()[Index];
        if (!Client || !Client->Viewport || Client->GetWorld() != World || !Client->IsPerspective())
        { Error = TEXT("Target must be a perspective Level Viewport in this world"); return nullptr; }
        return Client;
    }

    TSharedPtr<FJsonObject> ViewInfo(FLevelEditorViewportClient* Client)
    {
        auto Result = MakeShared<FJsonObject>();
        const FVector Location = Client->GetViewLocation(); const FRotator Rotation = Client->GetViewRotation();
        Result->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y), MakeShared<FJsonValueNumber>(Location.Z)});
        Result->SetArrayField(TEXT("rotation"), {MakeShared<FJsonValueNumber>(Rotation.Pitch), MakeShared<FJsonValueNumber>(Rotation.Yaw), MakeShared<FJsonValueNumber>(Rotation.Roll)});
        Result->SetNumberField(TEXT("fov"), Client->ViewFOV); Result->SetBoolField(TEXT("game_view"), Client->IsInGameView());
        Result->SetStringField(TEXT("camera_path"), Client->GetActiveActorLock().IsValid() ? Client->GetActiveActorLock()->GetPathName() : FString());
        Result->SetNumberField(TEXT("width"), Client->Viewport->GetSizeXY().X); Result->SetNumberField(TEXT("height"), Client->Viewport->GetSizeXY().Y);
        Result->SetBoolField(TEXT("editor_fixed_exposure"), Client->ExposureSettings.bFixed);
        Result->SetNumberField(TEXT("editor_fixed_ev100"), Client->ExposureSettings.FixedEV100);
        if (auto Camera = Cast<ACameraActor>(Client->GetActiveActorLock().Get()))
        {
            Result->SetNumberField(TEXT("camera_post_process_blend_weight"), Camera->GetCameraComponent()->PostProcessBlendWeight);
            Result->SetStringField(TEXT("camera_exposure_method"), StaticEnum<EAutoExposureMethod>()->GetNameStringByValue(Camera->GetCameraComponent()->PostProcessSettings.AutoExposureMethod));
        }
        return Result;
    }

    UObject* ResolveTarget(UWorld* World, const TSharedPtr<FJsonObject>& Params, FString& Error)
    {
        FString ActorPath, ComponentName;
        if (!Params->TryGetStringField(TEXT("actor_path"), ActorPath)) { Error = TEXT("actor_path required"); return nullptr; }
        AActor* Actor = nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
            if (It->GetPathName() == ActorPath) { Actor = *It; break; }
        if (!Actor) { Error = TEXT("Actor not found; use its full object path, never a label"); return nullptr; }
        if (Actor->GetPackage() != World->GetPackage() || Actor->GetLevel() != World->PersistentLevel)
        { Error = TEXT("External actor packages and streaming sublevels are not supported by this scene writer"); return nullptr; }
        if (Params->HasField(TEXT("component_name")) && !Params->TryGetStringField(TEXT("component_name"), ComponentName))
        { Error = TEXT("component_name must be a string"); return nullptr; }
        if (ComponentName.IsEmpty()) return Actor;
        UActorComponent* Component = nullptr;
        TInlineComponentArray<UActorComponent*> Components(Actor);
        for (UActorComponent* Candidate : Components)
            if (Candidate->GetName() == ComponentName || Candidate->GetPathName() == ComponentName)
            {
                if (Component) { Error = TEXT("Ambiguous component"); return nullptr; }
                Component = Candidate;
            }
        if (!Component) Error = TEXT("Component not found on the exact actor");
        return Component;
    }

    FString ActorFolderPath(const AActor* Actor)
    {
        const FName Folder = Actor->GetFolderPath();
        return Folder.IsNone() ? FString() : Folder.ToString();
    }

    bool ValidateFolderPath(const FString& Path, FString& Error, bool AllowEmpty = true)
    {
        if (Path.IsEmpty())
        {
            if (AllowEmpty) return true;
            Error = TEXT("folder_root must not be empty"); return false;
        }
        if (Path.Len() > 512 || Path.StartsWith(TEXT("/")) || Path.EndsWith(TEXT("/")) || Path.Contains(TEXT("\\")) || Path.Contains(TEXT("//")))
        { Error = TEXT("Folder paths must be relative Outliner paths, at most 512 characters, using single forward slashes"); return false; }
        TArray<FString> Segments; Path.ParseIntoArray(Segments, TEXT("/"), false);
        if (Segments.Num() > 16) { Error = TEXT("Folder nesting exceeds 16 segments"); return false; }
        for (const FString& Segment : Segments)
        {
            if (Segment.IsEmpty() || Segment != Segment.TrimStartAndEnd() || Segment == TEXT(".") || Segment == TEXT("..") || Segment.Equals(TEXT("None"), ESearchCase::IgnoreCase))
            { Error = TEXT("Folder segments must be nonempty names, not dot/parent/None or padded with whitespace"); return false; }
            for (TCHAR Character : Segment)
                if (Character < 32 || Character == 127 || FString(TEXT(":*?\"<>|")).Contains(FString::Chr(Character)))
                { Error = TEXT("Invalid character in Outliner folder path"); return false; }
        }
        return true;
    }

    bool SupportsFolderWorld(UWorld* World)
    {
        return !World->IsPartitionedWorld() && World->GetLevels().Num() == 1 && World->PersistentLevel
            && !World->PersistentLevel->IsUsingExternalActors() && !World->PersistentLevel->IsUsingExternalObjects();
    }

    bool SupportsFolderTarget(const AActor* Actor, UWorld* World)
    {
        return Actor && Actor->GetLevel() == World->PersistentLevel && Actor->GetPackage() == World->GetPackage()
            && SupportsFolderWorld(World);
    }

    TSet<FString> Fields(UObject* Target)
    {
        TSet<FString> Result;
        if (Target->IsA<USceneComponent>()) Result.Append({TEXT("Mobility"), TEXT("bVisible")});
        if (Target->IsA<ULightComponent>()) Result.Append({TEXT("Intensity"), TEXT("LightColor"), TEXT("Temperature"), TEXT("bUseTemperature"), TEXT("CastShadows")});
        if (Target->IsA<ULocalLightComponent>()) Result.Append({TEXT("IntensityUnits"), TEXT("AttenuationRadius")});
        if (Target->IsA<UPointLightComponent>()) Result.Append({TEXT("SourceRadius"), TEXT("SoftSourceRadius"), TEXT("SourceLength")});
        if (Target->IsA<USpotLightComponent>()) Result.Append({TEXT("InnerConeAngle"), TEXT("OuterConeAngle")});
        if (Target->IsA<URectLightComponent>()) Result.Append({TEXT("SourceWidth"), TEXT("SourceHeight"), TEXT("BarnDoorAngle"), TEXT("BarnDoorLength")});
        if (Target->IsA<UDirectionalLightComponent>()) Result.Append({TEXT("bAtmosphereSunLight"), TEXT("AtmosphereSunLightIndex"), TEXT("LightSourceAngle")});
        if (Target->IsA<USkyLightComponent>()) Result.Append({TEXT("Intensity"), TEXT("LightColor"), TEXT("SourceType"), TEXT("Cubemap"), TEXT("bRealTimeCapture"), TEXT("SkyDistanceThreshold")});
        if (Target->IsA<UExponentialHeightFogComponent>()) Result.Append({TEXT("FogDensity"), TEXT("FogHeightFalloff"), TEXT("StartDistance"), TEXT("FogInscatteringLuminance"),
            TEXT("bEnableVolumetricFog"), TEXT("VolumetricFogScatteringDistribution"), TEXT("VolumetricFogAlbedo"), TEXT("VolumetricFogEmissive"),
            TEXT("VolumetricFogExtinctionScale"), TEXT("VolumetricFogDistance")});
        if (Target->IsA<USkyAtmosphereComponent>()) Result.Append({TEXT("RayleighScatteringScale"), TEXT("RayleighScattering"), TEXT("RayleighExponentialDistribution"),
            TEXT("MieScatteringScale"), TEXT("MieScattering"), TEXT("MieAbsorptionScale"), TEXT("MieAbsorption"), TEXT("MieAnisotropy"),
            TEXT("MieExponentialDistribution"), TEXT("AerialPespectiveViewDistanceScale"), TEXT("GroundAlbedo")});
        if (Target->IsA<UCameraComponent>()) Result.Append({TEXT("FieldOfView"), TEXT("AspectRatio"), TEXT("bConstrainAspectRatio"), TEXT("PostProcessBlendWeight")});
        if (Target->IsA<UCineCameraComponent>()) Result.Append({TEXT("CurrentFocalLength"), TEXT("CurrentAperture"),
            TEXT("Filmback.SensorWidth"), TEXT("Filmback.SensorHeight"), TEXT("FocusSettings.FocusMethod"), TEXT("FocusSettings.ManualFocusDistance")});
        if (Target->IsA<APostProcessVolume>()) Result.Append({TEXT("bEnabled"), TEXT("bUnbound"), TEXT("Priority"), TEXT("BlendWeight"), TEXT("BlendRadius")});
        if (Target->IsA<APostProcessVolume>() || Target->IsA<UCameraComponent>())
        {
            const FString Prefix = Target->IsA<APostProcessVolume>() ? TEXT("Settings.") : TEXT("PostProcessSettings.");
            for (const FString& Name : {FString(TEXT("AutoExposureMethod")), FString(TEXT("AutoExposureBias")), FString(TEXT("AutoExposureMinBrightness")),
                FString(TEXT("AutoExposureMaxBrightness")), FString(TEXT("AutoExposureApplyPhysicalCameraExposure")), FString(TEXT("BloomIntensity")),
                FString(TEXT("BloomThreshold")), FString(TEXT("WhiteTemp")), FString(TEXT("WhiteTint")), FString(TEXT("TemperatureType")),
                FString(TEXT("ColorSaturation")), FString(TEXT("ColorContrast")), FString(TEXT("ColorGamma")), FString(TEXT("ColorGain")),
                FString(TEXT("ColorOffset")), FString(TEXT("VignetteIntensity")), FString(TEXT("LumenSceneLightingQuality")), FString(TEXT("LumenFinalGatherQuality"))})
            {
                Result.Add(Prefix + Name);
                Result.Add(Prefix + TEXT("bOverride_") + Name);
            }
        }
        return Result;
    }

    struct FField
    {
        FProperty* Root = nullptr;
        FProperty* Property = nullptr;
        void* Address = nullptr;
    };

    bool ResolveField(UObject* Target, const FString& Path, FField& Field)
    {
        if (!Fields(Target).Contains(Path)) return false;
        FString Root, Leaf;
        if (Path.Split(TEXT("."), &Root, &Leaf))
        {
            auto Struct = FindFProperty<FStructProperty>(Target->GetClass(), *Root);
            if (!Struct) return false;
            Field.Root = Struct;
            Field.Property = FindFProperty<FProperty>(Struct->Struct, *Leaf);
            if (Field.Property) Field.Address = Field.Property->ContainerPtrToValuePtr<void>(Struct->ContainerPtrToValuePtr<void>(Target));
        }
        else
        {
            Field.Root = Field.Property = FindFProperty<FProperty>(Target->GetClass(), *Path);
            if (Field.Property) Field.Address = Field.Property->ContainerPtrToValuePtr<void>(Target);
        }
        return Field.Address && !Field.Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated);
    }

    FString Unit(UObject* Target, const FString& Path)
    {
        if (Path == TEXT("CurrentFocalLength") || Path.StartsWith(TEXT("Filmback."))) return TEXT("mm");
        if (Path == TEXT("CurrentAperture")) return TEXT("f-stop");
        if (Path == TEXT("LightColor") || Path.EndsWith(TEXT("Albedo"))) return TEXT("sRGB bytes 0..255; R/G/B/A");
        if (Path == TEXT("Intensity")) return Target->IsA<UDirectionalLightComponent>() ? TEXT("lux") : Target->IsA<ULocalLightComponent>() ? TEXT("see IntensityUnits") : TEXT("intensity scale");
        if (Path == TEXT("Temperature") || Path.EndsWith(TEXT("WhiteTemp"))) return TEXT("Kelvin");
        if (Path.Contains(TEXT("Angle")) || Path == TEXT("FieldOfView")) return TEXT("degrees");
        if (Target->IsA<USkyAtmosphereComponent>()) return Path.Contains(TEXT("ExponentialDistribution")) ? TEXT("km") : TEXT("linear color or atmosphere scale");
        if (Path.EndsWith(TEXT("AutoExposureBias"))) return TEXT("stops (exposure compensation)");
        if (Path.EndsWith(TEXT("AutoExposureMinBrightness")) || Path.EndsWith(TEXT("AutoExposureMaxBrightness")))
        {
            auto Extended = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
            return Extended && Extended->GetInt() ? TEXT("EV100 (extended luminance range enabled)") : TEXT("brightness (extended luminance range disabled)");
        }
        if (Path == TEXT("FogHeightFalloff")) return TEXT("height falloff scale");
        if (Path.Contains(TEXT("Color")) || Path.Contains(TEXT("Scattering")) || Path.Contains(TEXT("Luminance"))) return TEXT("linear color or scale");
        if (Path.Contains(TEXT("Radius")) || Path.Contains(TEXT("Distance")) || Path.Contains(TEXT("Width")) || Path.Contains(TEXT("Height")) || Path.Contains(TEXT("Length"))) return TEXT("cm (unless schema specifies otherwise)");
        return TEXT("engine property units; see schema");
    }

    struct FPending
    {
        FString Path;
        FField Field;
        void* Value;
        TSharedPtr<FJsonValue> Before;
        TSharedPtr<FJsonValue> Requested;
        FPending(const FString& InPath, const FField& InField) : Path(InPath), Field(InField)
        {
            Value = FMemory::Malloc(Field.Property->GetSize(), Field.Property->GetMinAlignment());
            Field.Property->InitializeValue(Value);
            Field.Property->CopyCompleteValue(Value, Field.Address);
        }
        ~FPending() { Field.Property->DestroyValue(Value); FMemory::Free(Value); }
    };

    void Apply(UObject* Target, const FPending& Pending)
    {
        const FString& Path = Pending.Path;
        if (auto Camera = Cast<UCineCameraComponent>(Target))
        {
            if (Path == TEXT("CurrentFocalLength")) { Camera->SetCurrentFocalLength(*static_cast<float*>(Pending.Value)); return; }
            if (Path == TEXT("CurrentAperture")) { Camera->SetCurrentAperture(*static_cast<float*>(Pending.Value)); return; }
        }
        if (auto Light = Cast<ULightComponent>(Target))
        {
            if (Light->Mobility != EComponentMobility::Static)
            {
                if (Path == TEXT("Intensity")) { Light->SetIntensity(CastFieldChecked<FFloatProperty>(Pending.Field.Property)->GetPropertyValue(Pending.Value)); return; }
                if (Path == TEXT("LightColor")) { Light->SetLightColor(FLinearColor(*static_cast<FColor*>(Pending.Value)), true); return; }
                if (Path == TEXT("Temperature")) { Light->SetTemperature(*static_cast<float*>(Pending.Value)); return; }
            }
        }
        if (auto Component = Cast<USceneComponent>(Target))
        {
            if (Path == TEXT("Mobility")) { Component->SetMobility(static_cast<EComponentMobility::Type>(CastFieldChecked<FByteProperty>(Pending.Field.Property)->GetPropertyValue(Pending.Value))); return; }
            if (Path == TEXT("bVisible")) { Component->SetVisibility(CastFieldChecked<FBoolProperty>(Pending.Field.Property)->GetPropertyValue(Pending.Value)); return; }
        }
        Pending.Field.Property->CopyCompleteValue(Pending.Field.Address, Pending.Value);
    }

    bool Vector(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, FVector& Value)
    {
        if (!Params->HasField(Key)) return true;
        const TArray<TSharedPtr<FJsonValue>>* Values;
        if (!Params->TryGetArrayField(Key, Values) || Values->Num() != 3) return false;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            double Number;
            if (!(*Values)[Index]->TryGetNumber(Number) || !FMath::IsFinite(Number) || FMath::Abs(Number) > 1e8) return false;
            Value[Index] = Number;
        }
        return true;
    }

    TSharedPtr<FJsonObject> EditorVisibilityInfo(AActor* Actor)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("temporary_hidden"), Actor->IsTemporarilyHiddenInEditor(false));
        Result->SetBoolField(TEXT("temporary_hidden_in_hierarchy"), Actor->IsTemporarilyHiddenInEditor(true));
        Result->SetBoolField(TEXT("editor_hidden"), Actor->IsHiddenEd());
        Result->SetBoolField(TEXT("hidden_in_game"), Actor->IsHidden());
        return Result;
    }

    TSharedPtr<FJsonObject> ActorInfo(AActor* Actor)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        Result->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Result->SetStringField(TEXT("folder_path"), ActorFolderPath(Actor));
        Result->SetObjectField(TEXT("editor_visibility"), EditorVisibilityInfo(Actor));
        Result->SetBoolField(TEXT("scene_managed"), Actor->ActorHasTag(TEXT("UnrealMCP.SceneManaged")));
        TArray<TSharedPtr<FJsonValue>> Namespaces;
        const FString Prefix = TEXT("UnrealMCP.Manifest_");
        for (FName Tag : Actor->Tags) if (Tag.ToString().StartsWith(Prefix)) Namespaces.Add(MakeShared<FJsonValueString>(Tag.ToString().RightChop(Prefix.Len())));
        Result->SetArrayField(TEXT("managed_namespaces"), Namespaces);
        Result->SetStringField(TEXT("class_path"), Actor->GetClass()->GetPathName());
        Result->SetStringField(TEXT("package"), Actor->GetPackage()->GetName());
        Result->SetBoolField(TEXT("external_package"), Actor->GetPackage() != Actor->GetLevel()->GetPackage());
        const FVector Location = Actor->GetActorLocation(), Scale = Actor->GetActorScale3D();
        const FRotator Rotation = Actor->GetActorRotation();
        Result->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y), MakeShared<FJsonValueNumber>(Location.Z)});
        Result->SetArrayField(TEXT("rotation"), {MakeShared<FJsonValueNumber>(Rotation.Pitch), MakeShared<FJsonValueNumber>(Rotation.Yaw), MakeShared<FJsonValueNumber>(Rotation.Roll)});
        Result->SetArrayField(TEXT("scale"), {MakeShared<FJsonValueNumber>(Scale.X), MakeShared<FJsonValueNumber>(Scale.Y), MakeShared<FJsonValueNumber>(Scale.Z)});
        return Result;
    }

    TSharedPtr<FJsonObject> MeshInfo(UStaticMeshComponent* Component)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("component_path"), Component->GetPathName());
        Result->SetStringField(TEXT("static_mesh"), Component->GetStaticMesh() ? Component->GetStaticMesh()->GetPathName() : FString());
        TArray<TSharedPtr<FJsonValue>> Slots;
        const auto Names = Component->GetMaterialSlotNames();
        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
        {
            auto Slot = MakeShared<FJsonObject>(); Slot->SetNumberField(TEXT("index"), Index);
            Slot->SetStringField(TEXT("name"), Names.IsValidIndex(Index) ? Names[Index].ToString() : FString());
            Slot->SetStringField(TEXT("material"), Component->GetMaterial(Index) ? Component->GetMaterial(Index)->GetPathName() : FString());
            Slot->SetBoolField(TEXT("overridden"), Component->OverrideMaterials.IsValidIndex(Index) && Component->OverrideMaterials[Index] != nullptr);
            Slots.Add(MakeShared<FJsonValueObject>(Slot));
        }
        Result->SetArrayField(TEXT("slots"), Slots);
        Result->SetBoolField(TEXT("package_dirty"), Component->GetPackage()->IsDirty());
        return Result;
    }
}

TSharedPtr<FJsonObject> Context(const TSharedPtr<FJsonObject>& Params)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("project_path"), ProjectPath());
    Result->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Result->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Result->SetStringField(TEXT("scene_contract"), TEXT("1"));
    Result->SetNumberField(TEXT("folder_contract"), 1);
    Result->SetNumberField(TEXT("editor_visibility_contract"), 1);
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMCP"));
    Result->SetStringField(TEXT("plugin_version"), Plugin ? Plugin->GetDescriptor().VersionName : TEXT("unknown"));
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    Result->SetStringField(TEXT("level_path"), World ? World->GetPackage()->GetName() : FString());
    Result->SetStringField(TEXT("current_level"), World && World->GetCurrentLevel() ? World->GetCurrentLevel()->GetPackage()->GetName() : FString());
    Result->SetStringField(TEXT("mode"), !GEditor ? TEXT("unavailable") : GEditor->bIsSimulatingInEditor ? TEXT("simulate") : GEditor->PlayWorld ? TEXT("pie") : TEXT("editor"));
    TArray<UPackage*> Content, Maps;
    UEditorLoadingAndSavingUtils::GetDirtyContentPackages(Content);
    UEditorLoadingAndSavingUtils::GetDirtyMapPackages(Maps);
    TArray<TSharedPtr<FJsonValue>> Dirty;
    for (UPackage* Package : Content) Dirty.Add(MakeShared<FJsonValueString>(Package->GetName()));
    for (UPackage* Package : Maps) Dirty.Add(MakeShared<FJsonValueString>(Package->GetName()));
    Result->SetArrayField(TEXT("dirty_packages"), Dirty);
    Result->SetNumberField(TEXT("remaining_asset_compilations"), FAssetCompilingManager::Get().GetNumRemainingAssets());
    TArray<TSharedPtr<FJsonValue>> Viewports;
    if (GEditor) for (int32 Index = 0; Index < GEditor->GetLevelViewportClients().Num(); ++Index)
    {
        const auto Client = GEditor->GetLevelViewportClients()[Index];
        if (!Client || Client->GetWorld() != World) continue;
        auto View = Client->Viewport ? ViewInfo(Client) : MakeShared<FJsonObject>(); View->SetNumberField(TEXT("viewport_id"), Index);
        View->SetBoolField(TEXT("perspective"), Client->IsPerspective()); View->SetNumberField(TEXT("fov"), Client->ViewFOV);
        Viewports.Add(MakeShared<FJsonValueObject>(View));
    }
    Result->SetArrayField(TEXT("viewports"), Viewports);
    Result->SetBoolField(TEXT("external_actor_writes_supported"), false);
    auto Extended = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
    Result->SetBoolField(TEXT("exposure_range_uses_ev100"), Extended && Extended->GetInt());
    TArray<TSharedPtr<FJsonValue>> Volumes;
    if (World) for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        auto Volume = ActorInfo(*It);
        Volume->SetBoolField(TEXT("enabled"), It->bEnabled); Volume->SetBoolField(TEXT("unbound"), It->bUnbound);
        Volume->SetNumberField(TEXT("priority"), It->Priority); Volume->SetNumberField(TEXT("blend_weight"), It->BlendWeight);
        Volumes.Add(MakeShared<FJsonValueObject>(Volume));
    }
    Result->SetArrayField(TEXT("post_process_volumes"), Volumes);
    Result->SetBoolField(TEXT("scene_task_active"), CaptureBusy || UnrealMCPSceneAssets::IsBusy());
    Result->SetBoolField(TEXT("shader_compiling"), GShaderCompilingManager && GShaderCompilingManager->IsCompiling());
    Result->SetStringField(TEXT("import_state"), UnrealMCPSceneAssets::IsBusy() ? TEXT("running") : TEXT("idle"));
    return Result;
}

TSharedPtr<FJsonObject> Inspect(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    UObject* Target = ResolveTarget(World, Params, Error);
    if (!Target) return Failure(Error);
    TArray<FString> Paths;
    if (Params->HasField(TEXT("property_paths")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Values;
        if (!Params->TryGetArrayField(TEXT("property_paths"), Values) || Values->Num() > 64) return Failure(TEXT("property_paths must contain at most 64 strings"));
        for (const auto& Value : *Values) { FString Path; if (!Value->TryGetString(Path)) return Failure(TEXT("property path must be string")); Paths.Add(Path); }
    }
    else Paths = Fields(Target).Array();
    Paths.Sort();
    TArray<TSharedPtr<FJsonValue>> Properties;
    for (const FString& Path : Paths)
    {
        FField Field;
        auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), Path);
        if (!ResolveField(Target, Path, Field)) return Failure(TEXT("Unsupported scene property or unavailable in this engine: ") + Path);
        Error.Reset(); auto Value = UnrealMCPAssetProperties::EncodeValue(Field.Property, Field.Address, Error);
        if (!Value) return Failure(Path + TEXT(": ") + Error);
        Entry->SetField(TEXT("value"), Value); Entry->SetBoolField(TEXT("writable"), CanWriteTarget(Target));
        Entry->SetObjectField(TEXT("schema"), UnrealMCPAssetProperties::DescribeValue(Field.Property));
        Entry->SetStringField(TEXT("units"), Unit(Target, Path));
        Properties.Add(MakeShared<FJsonValueObject>(Entry));
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true); Result->SetStringField(TEXT("target"), Target->GetPathName());
    Result->SetStringField(TEXT("class_path"), Target->GetClass()->GetPathName());
    Result->SetBoolField(TEXT("package_dirty"), Target->GetPackage()->IsDirty()); Result->SetArrayField(TEXT("properties"), Properties);
    if (auto Actor = Cast<AActor>(Target))
    {
        Result->SetStringField(TEXT("folder_path"), ActorFolderPath(Actor));
        Result->SetObjectField(TEXT("editor_visibility"), EditorVisibilityInfo(Actor));
        Result->SetBoolField(TEXT("folder_editable"), SupportsFolderTarget(Actor, World));
        Result->SetBoolField(TEXT("scene_managed"), Actor->ActorHasTag(TEXT("UnrealMCP.SceneManaged")));
        TArray<TSharedPtr<FJsonValue>> Components;
        TInlineComponentArray<UActorComponent*> Owned(Actor);
        for (auto Component : Owned)
        {
            auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Component->GetName());
            Entry->SetStringField(TEXT("path"), Component->GetPathName()); Entry->SetStringField(TEXT("class_path"), Component->GetClass()->GetPathName());
            Components.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("components"), Components);
    }
    return Result;
}

TSharedPtr<FJsonObject> Patch(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy) return Failure(TEXT("Scene capture active; wait for task completion before editing"));
    FString Error;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    UObject* Target = ResolveTarget(World, Params, Error);
    if (!Target) return Failure(Error);
    if (!CanWriteTarget(Target)) return Failure(TEXT("Construction-script/Blueprint-instance writes unsupported; native scene targets only"));
    const TArray<TSharedPtr<FJsonValue>>* Changes;
    if (!Params->TryGetArrayField(TEXT("changes"), Changes) || Changes->IsEmpty() || Changes->Num() > 64) return Failure(TEXT("changes must contain 1..64 patches"));
    TArray<TSharedPtr<FPending>> Pending;
    TSet<FString> Seen;
    for (const auto& Value : *Changes)
    {
        const TSharedPtr<FJsonObject>* Change;
        FString Path;
        if (!Value->TryGetObject(Change) || !(*Change)->TryGetStringField(TEXT("path"), Path) || !(*Change)->HasField(TEXT("value"))) return Failure(TEXT("Each change needs path and value"));
        for (const auto& Field : (*Change)->Values) if (Field.Key != TEXT("path") && Field.Key != TEXT("value") && Field.Key != TEXT("expected_value")) return Failure(TEXT("Unknown change key"));
        FField Field;
        if (!ResolveField(Target, Path, Field) || Seen.Contains(Path)) return Failure(TEXT("Unsupported or duplicate scene field: ") + Path);
        Seen.Add(Path);
        auto Item = MakeShared<FPending>(Path, Field);
        Item->Before = UnrealMCPAssetProperties::EncodeValue(Field.Property, Field.Address, Error);
        if (!Item->Before) return Failure(Error);
        if ((*Change)->HasField(TEXT("expected_value")))
        {
            if (!UnrealMCPAssetProperties::DecodeValue(Field.Property, Item->Value, (*Change)->Values[TEXT("expected_value")], Error)) return Failure(Error);
            if (!Field.Property->Identical(Field.Address, Item->Value)) return Failure(TEXT("expected_value conflict: ") + Path);
        }
        Item->Requested = (*Change)->Values[TEXT("value")];
        if (!UnrealMCPAssetProperties::DecodeValue(Field.Property, Item->Value, Item->Requested, Error)) return Failure(Path + TEXT(": ") + Error);
        if (Item->Requested->Type == EJson::Number)
        {
            const double Number = Item->Requested->AsNumber();
            if ((Path == TEXT("Intensity") || Path == TEXT("AttenuationRadius") || Path.StartsWith(TEXT("Source")) || Path == TEXT("FogDensity") || Path.EndsWith(TEXT("BlendRadius"))) && Number < 0)
                return Failure(TEXT("Negative scene magnitude: ") + Path);
            if (Path == TEXT("Temperature") && (Number < 1000 || Number > 15000)) return Failure(TEXT("Temperature requires 1000..15000 Kelvin"));
            if (Path.EndsWith(TEXT("BlendWeight")) && (Number < 0 || Number > 1)) return Failure(TEXT("BlendWeight requires 0..1"));
        }
        Pending.Add(Item);
    }
    TArray<TSharedPtr<FPending>> Overrides;
    for (const auto& Item : Pending)
    {
        FString Root, Leaf;
        if (!Item->Path.Split(TEXT("."), &Root, &Leaf) || Leaf.StartsWith(TEXT("bOverride_"))) continue;
        if (Root != TEXT("Settings") && Root != TEXT("PostProcessSettings")) continue;
        const FString OverridePath = Root + TEXT(".bOverride_") + Leaf;
        if (Seen.Contains(OverridePath)) continue;
        FField Field;
        if (!ResolveField(Target, OverridePath, Field)) return Failure(TEXT("Post process override unavailable: ") + OverridePath);
        auto Override = MakeShared<FPending>(OverridePath, Field);
        Override->Before = UnrealMCPAssetProperties::EncodeValue(Field.Property, Field.Address, Error);
        Override->Requested = MakeShared<FJsonValueBoolean>(true);
        if (!UnrealMCPAssetProperties::DecodeValue(Field.Property, Override->Value, Override->Requested, Error)) return Failure(Error);
        Overrides.Add(Override);
    }
    Pending.Append(Overrides);
    auto ProposedNumber = [&](const FString& Path, double& Number)
    {
        for (const auto& Item : Pending) if (Item->Path == Path) return Item->Requested->TryGetNumber(Number);
        FField Field; if (!ResolveField(Target, Path, Field)) return false;
        auto NumberProperty = CastField<FNumericProperty>(Field.Property);
        if (!NumberProperty || !NumberProperty->IsFloatingPoint()) return false;
        Number = NumberProperty->GetFloatingPointPropertyValue(Field.Address); return true;
    };
    double Minimum, Maximum;
    if (Target->IsA<USpotLightComponent>() && ProposedNumber(TEXT("InnerConeAngle"), Minimum) && ProposedNumber(TEXT("OuterConeAngle"), Maximum)
        && (Minimum < 0 || Maximum > 89 || Minimum > Maximum)) return Failure(TEXT("Spot angles require 0 <= inner <= outer <= 89 degrees"));
    const FString Exposure = Target->IsA<APostProcessVolume>() ? TEXT("Settings.") : TEXT("PostProcessSettings.");
    if (ProposedNumber(Exposure + TEXT("AutoExposureMinBrightness"), Minimum) && ProposedNumber(Exposure + TEXT("AutoExposureMaxBrightness"), Maximum)
        && Minimum > Maximum) return Failure(TEXT("Exposure minimum exceeds maximum"));
    bool Modified = false;
    TArray<TSharedPtr<FJsonValue>> Receipts;
    FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "ScenePatch", "Edit scene instance properties"), World);
    for (const auto& Item : Pending)
    {
        if (Item->Field.Property->Identical(Item->Field.Address, Item->Value)) continue;
        if (!Modified) Target->Modify();
        Target->PreEditChange(Item->Field.Root);
        Apply(Target, *Item);
        FPropertyChangedEvent Event(Item->Field.Property, EPropertyChangeType::ValueSet);
        Target->PostEditChangeProperty(Event);
        Modified = true;
    }
    if (Modified) Target->MarkPackageDirty();
    else Transaction.Cancel();
    if (auto Component = Cast<UActorComponent>(Target)) if (Modified) Component->MarkRenderStateDirty();
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true);
    for (const auto& Item : Pending)
    {
        auto Receipt = MakeShared<FJsonObject>(); Receipt->SetStringField(TEXT("path"), Item->Path);
        Receipt->SetField(TEXT("before"), Item->Before); Receipt->SetField(TEXT("requested"), Item->Requested);
        FField Actual;
        auto After = ResolveField(Target, Item->Path, Actual) ? UnrealMCPAssetProperties::EncodeValue(Actual.Property, Actual.Address, Error) : nullptr;
        if (!After) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("error"), TEXT("Readback failed; in-memory edits may remain")); }
        else Receipt->SetField(TEXT("after"), After);
        Receipts.Add(MakeShared<FJsonValueObject>(Receipt));
    }
    Result->SetStringField(TEXT("target"), Target->GetPathName()); Result->SetBoolField(TEXT("modified"), Modified);
    Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), Target->GetPackage()->IsDirty());
    Result->SetArrayField(TEXT("changes"), Receipts);
    return Result;
}

TSharedPtr<FJsonObject> List(const TSharedPtr<FJsonObject>& Params)
{
    FString Error, Filter;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    Params->TryGetStringField(TEXT("filter"), Filter);
    int32 Offset = 0, Limit = 50;
    if ((Params->HasField(TEXT("offset")) && !Params->TryGetNumberField(TEXT("offset"), Offset))
        || (Params->HasField(TEXT("limit")) && !Params->TryGetNumberField(TEXT("limit"), Limit)) || Offset < 0 || Limit < 1 || Limit > 200)
        return Failure(TEXT("offset >= 0 and limit 1..200 required"));
    TArray<AActor*> Actors;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (Filter.IsEmpty() || It->GetActorLabel().Contains(Filter) || It->GetClass()->GetName().Contains(Filter)) Actors.Add(*It);
    Actors.Sort([](const AActor& Left, const AActor& Right) { return Left.GetPathName() < Right.GetPathName(); });
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (int32 Index = Offset; Index < FMath::Min(Offset + Limit, Actors.Num()); ++Index) Entries.Add(MakeShared<FJsonValueObject>(ActorInfo(Actors[Index])));
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true); Result->SetArrayField(TEXT("actors"), Entries);
    Result->SetNumberField(TEXT("total"), Actors.Num()); Result->SetBoolField(TEXT("has_more"), Offset + Limit < Actors.Num());
    return Result;
}

TSharedPtr<FJsonObject> ManageActor(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy) return Failure(TEXT("Scene capture active"));
    FString Error, Operation;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    if (!Params->TryGetStringField(TEXT("operation"), Operation)) return Failure(TEXT("operation required"));
    if (World->IsPartitionedWorld()) return Failure(TEXT("World Partition creation/save is not supported"));
    FString Folder;
    const bool HasFolder = Params->HasField(TEXT("folder_path"));
    if (HasFolder && (Operation != TEXT("create") || !SupportsFolderWorld(World))) return Failure(TEXT("folder_path is only supported for create in a single persistent level without external objects; use set_scene_actor_folders for existing actors"));
    if (HasFolder && (!Params->TryGetStringField(TEXT("folder_path"), Folder) || !ValidateFolderPath(Folder, Error)))
        return Failure(Error.IsEmpty() ? TEXT("folder_path must be a string") : Error);
    AActor* Actor = nullptr;
    TSharedPtr<FJsonObject> Before;
    if (Operation != TEXT("create"))
    {
        Actor = Cast<AActor>(ResolveTarget(World, Params, Error));
        if (!Actor) return Failure(Error.IsEmpty() ? TEXT("Expected an actor target") : Error);
        Before = ActorInfo(Actor);
    }
    FVector Location = Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
    FRotator Rotation = Actor ? Actor->GetActorRotation() : FRotator::ZeroRotator;
    FVector Angles(Rotation.Pitch, Rotation.Yaw, Rotation.Roll), Scale = Actor ? Actor->GetActorScale3D() : FVector::OneVector;
    if (!Vector(Params, TEXT("location"), Location) || !Vector(Params, TEXT("rotation"), Angles) || !Vector(Params, TEXT("scale"), Scale)
        || Scale.GetAbsMin() < 0.0001) return Failure(TEXT("Invalid finite location/rotation/scale vectors (cm/degrees)"));
    if (Operation == TEXT("create"))
    {
        FString Type, Id;
        if (!Params->TryGetStringField(TEXT("actor_type"), Type) || !Params->TryGetStringField(TEXT("managed_id"), Id)
            || Id.IsEmpty() || Id.Len() > 64) return Failure(TEXT("actor_type and managed_id (1..64 alphanumeric/underscore) required"));
        for (TCHAR Character : Id) if (!(FChar::IsAlnum(Character) || Character == TEXT('_'))) return Failure(TEXT("Invalid managed_id"));
        static const TMap<FString, UClass*> Classes = {{TEXT("RectLight"), ARectLight::StaticClass()}, {TEXT("PointLight"), APointLight::StaticClass()},
            {TEXT("SpotLight"), ASpotLight::StaticClass()}, {TEXT("DirectionalLight"), ADirectionalLight::StaticClass()}, {TEXT("SkyLight"), ASkyLight::StaticClass()},
            {TEXT("SkyAtmosphere"), ASkyAtmosphere::StaticClass()}, {TEXT("HeightFog"), AExponentialHeightFog::StaticClass()},
            {TEXT("PostProcessVolume"), APostProcessVolume::StaticClass()}, {TEXT("StaticMeshActor"), AStaticMeshActor::StaticClass()},
            {TEXT("CameraActor"), ACameraActor::StaticClass()}, {TEXT("CineCameraActor"), ACineCameraActor::StaticClass()}, {TEXT("PlayerStart"), APlayerStart::StaticClass()}};
        UClass* Class = Classes.FindRef(Type);
        if (!Class) return Failure(TEXT("Unsupported actor_type"));
        const FName Name(*(TEXT("MCPScene_") + Id));
        if (FindObject<UObject>(World->PersistentLevel, *Name.ToString())) return Failure(TEXT("managed_id already exists; inspect existing actor instead of retrying creation"));
        const FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "SceneCreate", "Create managed scene actor"), World);
        World->PersistentLevel->Modify();
        FActorSpawnParameters Spawn; Spawn.Name = Name; Spawn.OverrideLevel = World->PersistentLevel; Spawn.ObjectFlags |= RF_Transactional;
        Actor = World->SpawnActor<AActor>(Class, Location, FRotator(Angles.X, Angles.Y, Angles.Z), Spawn);
        if (!Actor) return Failure(TEXT("Spawn failed"));
        Actor->Tags.Add(TEXT("UnrealMCP.SceneManaged"));
        Actor->SetActorScale3D(Scale);
        if (HasFolder) Actor->SetFolderPath(FName(*Folder));
        if (auto Volume = Cast<APostProcessVolume>(Actor)) Volume->bUnbound = true;
    }
    else if (Operation == TEXT("delete"))
    {
        if (!Actor->ActorHasTag(TEXT("UnrealMCP.SceneManaged"))) return Failure(TEXT("Only tool-managed actors may be deleted"));
        const FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "SceneDelete", "Delete managed scene actor"), World);
        Actor->Modify(); World->PersistentLevel->Modify();
        if (!World->EditorDestroyActor(Actor, true)) return Failure(TEXT("Delete failed"));
        auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), true);
        Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), World->GetPackage()->IsDirty()); Result->SetObjectField(TEXT("before"), Before);
        return Result;
    }
    else if (Operation == TEXT("transform"))
    {
        const FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "SceneTransform", "Transform scene actor"), World);
        Actor->Modify(); if (Actor->GetRootComponent()) Actor->GetRootComponent()->Modify();
        Actor->SetActorLocationAndRotation(Location, FRotator(Angles.X, Angles.Y, Angles.Z)); Actor->SetActorScale3D(Scale); Actor->PostEditMove(true);
    }
    else return Failure(TEXT("Unknown operation; expected create, transform or delete"));
    Actor->MarkPackageDirty();
    auto Result = ActorInfo(Actor); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), true);
    Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), Actor->GetPackage()->IsDirty());
    if (HasFolder && Actor->GetFolderPath() != FName(*Folder))
    {
        Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("folder_readback"));
        Result->SetStringField(TEXT("error"), TEXT("Actor created but folder assignment did not match; inspect before retrying"));
    }
    if (Before) Result->SetObjectField(TEXT("before"), Before);
    return Result;
}

TSharedPtr<FJsonObject> SetActorFolders(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy || UnrealMCPSceneAssets::IsBusy()) return Failure(TEXT("Scene task active"));
    FString Error; UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    if (!SupportsFolderWorld(World)) return Failure(TEXT("Folder writes require a non-partitioned single persistent level without external actors/objects"));
    bool DryRun = true, ManagedOnly = true;
    if ((Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))
        || (Params->HasField(TEXT("managed_only")) && !Params->TryGetBoolField(TEXT("managed_only"), ManagedOnly))) return Failure(TEXT("dry_run and managed_only must be booleans"));
    const TArray<TSharedPtr<FJsonValue>>* Changes;
    if (!Params->TryGetArrayField(TEXT("changes"), Changes) || Changes->IsEmpty() || Changes->Num() > 200) return Failure(TEXT("changes must contain 1..200 exact actor folder assignments"));
    struct FAssignment { AActor* Actor; FString Before, Requested; };
    TArray<FAssignment> Assignments; TSet<AActor*> Seen; TMap<FString, AActor*> Actors;
    for (TActorIterator<AActor> It(World); It; ++It) Actors.Add(It->GetPathName(), *It);
    for (const auto& Value : *Changes)
    {
        const TSharedPtr<FJsonObject>* Change; FString ActorPath, Folder, Expected;
        if (!Value->TryGetObject(Change) || !(*Change)->TryGetStringField(TEXT("actor_path"), ActorPath) || !(*Change)->TryGetStringField(TEXT("folder_path"), Folder))
            return Failure(TEXT("Each assignment requires actor_path and folder_path strings"));
        for (const auto& Pair : (*Change)->Values)
            if (Pair.Key != TEXT("actor_path") && Pair.Key != TEXT("folder_path") && Pair.Key != TEXT("expected_folder_path")) return Failure(TEXT("Unknown folder assignment key: ") + Pair.Key);
        if (!ValidateFolderPath(Folder, Error)) return Failure(Error);
        AActor* Actor = Actors.FindRef(ActorPath);
        if (!SupportsFolderTarget(Actor, World) || Seen.Contains(Actor)) return Failure(TEXT("Unknown, duplicate, external or unsupported actor: ") + ActorPath);
        if (ManagedOnly && !Actor->ActorHasTag(TEXT("UnrealMCP.SceneManaged"))) return Failure(TEXT("Actor is not scene-managed; review exact targets before explicitly allowing unmanaged actors: ") + ActorPath);
        const FString Before = ActorFolderPath(Actor);
        if ((*Change)->HasField(TEXT("expected_folder_path")))
        {
            if (!(*Change)->TryGetStringField(TEXT("expected_folder_path"), Expected) || !ValidateFolderPath(Expected, Error)) return Failure(TEXT("Invalid expected_folder_path"));
            if (Actor->GetFolderPath() != FName(*Expected)) return Failure(TEXT("expected_folder_path conflict: ") + ActorPath);
        }
        Seen.Add(Actor); Assignments.Add({Actor, Before, Folder});
    }
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("dry_run"), DryRun);
    TArray<TSharedPtr<FJsonValue>> Items; bool Modified = false;
    FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "SceneFolders", "Set scene actor folders"), World, !DryRun);
    for (const FAssignment& Assignment : Assignments)
    {
        auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("actor_path"), Assignment.Actor->GetPathName());
        Item->SetStringField(TEXT("before"), Assignment.Before); Item->SetStringField(TEXT("requested"), Assignment.Requested);
        const bool Changed = Assignment.Actor->GetFolderPath() != FName(*Assignment.Requested);
        Item->SetBoolField(TEXT("would_modify"), Changed);
        if (!DryRun && Changed)
        {
            Assignment.Actor->Modify(); Assignment.Actor->SetFolderPath(FName(*Assignment.Requested));
            Assignment.Actor->MarkPackageDirty(); Modified = true;
        }
        Item->SetStringField(TEXT("after"), ActorFolderPath(Assignment.Actor)); Items.Add(MakeShared<FJsonValueObject>(Item));
        if (!DryRun && Assignment.Actor->GetFolderPath() != FName(*Assignment.Requested))
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("folder_readback"));
            Result->SetStringField(TEXT("error"), TEXT("Folder assignment did not match; partial edits may remain, inspect items before retrying")); break;
        }
    }
    if (!Modified) Transaction.Cancel();
    Result->SetBoolField(TEXT("modified"), Modified); Result->SetBoolField(TEXT("package_dirty"), World->GetPackage()->IsDirty());
    Result->SetArrayField(TEXT("items"), Items); return Result;
}

TSharedPtr<FJsonObject> SetActorVisibility(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy || UnrealMCPSceneAssets::IsBusy()) return Failure(TEXT("Scene task active"));
    FString Error; UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    if (World->IsPartitionedWorld()) return Failure(TEXT("World Partition visibility edits are unsupported"));
    auto Actor = Cast<AActor>(ResolveTarget(World, Params, Error));
    if (!Actor) return Failure(Error.IsEmpty() ? TEXT("Select an exact actor, not a component") : Error);
    for (const auto& Pair : Params->Values)
        if (Pair.Key != TEXT("project_path") && Pair.Key != TEXT("level_path") && Pair.Key != TEXT("actor_path")
            && Pair.Key != TEXT("hidden_in_editor") && Pair.Key != TEXT("dry_run") && Pair.Key != TEXT("expected_hidden_in_editor"))
            return Failure(TEXT("Unknown visibility parameter: ") + Pair.Key);
    bool Hidden, DryRun = true, Expected;
    if (!Params->TryGetBoolField(TEXT("hidden_in_editor"), Hidden)
        || (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)))
        return Failure(TEXT("hidden_in_editor is required and dry_run must be boolean"));
    const bool Before = Actor->IsTemporarilyHiddenInEditor(false);
    if (Params->HasField(TEXT("expected_hidden_in_editor")))
    {
        if (!Params->TryGetBoolField(TEXT("expected_hidden_in_editor"), Expected)) return Failure(TEXT("expected_hidden_in_editor must be boolean"));
        if (Expected != Before) return Failure(TEXT("expected_hidden_in_editor conflict; re-inspect the actor"));
    }
    auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Result->SetObjectField(TEXT("before"), EditorVisibilityInfo(Actor));
    Result->SetBoolField(TEXT("requested_hidden_in_editor"), Hidden);
    Result->SetBoolField(TEXT("package_dirty_before"), Actor->GetPackage()->IsDirty());
    const bool WouldModify = Before != Hidden;
    const bool Applied = !DryRun && WouldModify;
    if (Applied)
    {
        Actor->SetIsTemporarilyHiddenInEditor(Hidden);
        GEditor->RedrawLevelEditingViewports();
    }
    const bool Success = DryRun || Actor->IsTemporarilyHiddenInEditor(false) == Hidden;
    Result->SetBoolField(TEXT("success"), Success); Result->SetBoolField(TEXT("dry_run"), DryRun);
    Result->SetBoolField(TEXT("would_modify"), WouldModify); Result->SetBoolField(TEXT("modified"), Applied);
    Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("session_only"), true);
    Result->SetBoolField(TEXT("undo_supported"), false);
    Result->SetBoolField(TEXT("package_dirty"), Actor->GetPackage()->IsDirty());
    Result->SetObjectField(TEXT("after"), EditorVisibilityInfo(Actor));
    if (!Success)
    {
        Result->SetStringField(TEXT("stage"), TEXT("visibility_readback"));
        Result->SetStringField(TEXT("error"), TEXT("Actor visibility did not match after apply; re-inspect before retrying"));
    }
    return Result;
}

TSharedPtr<FJsonObject> Mesh(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy && (Params->HasField(TEXT("static_mesh")) || Params->HasField(TEXT("materials")))) return Failure(TEXT("Scene capture active"));
    FString Error;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    auto Component = Cast<UStaticMeshComponent>(ResolveTarget(World, Params, Error));
    if (!Component) return Failure(Error.IsEmpty() ? TEXT("Expected an exact StaticMeshComponent") : Error);
    if (!Params->HasField(TEXT("static_mesh")) && !Params->HasField(TEXT("materials"))) return MeshInfo(Component);
    UStaticMesh* MeshAsset = Component->GetStaticMesh();
    FString Path;
    if (Params->HasField(TEXT("static_mesh")))
    {
        if (!Params->TryGetStringField(TEXT("static_mesh"), Path) || !Path.StartsWith(TEXT("/"))) return Failure(TEXT("static_mesh requires full existing asset path"));
        MeshAsset = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(Path));
        if (!MeshAsset) return Failure(TEXT("StaticMesh not found"));
    }
    TMap<int32, UMaterialInterface*> Materials;
    if (Params->HasField(TEXT("materials")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items;
        if (!MeshAsset || !Params->TryGetArrayField(TEXT("materials"), Items) || Items->Num() > 64) return Failure(TEXT("materials needs mesh and at most 64 assignments"));
        for (const auto& Value : *Items)
        {
            const TSharedPtr<FJsonObject>* Item;
            int32 Index = INDEX_NONE; FString Slot;
            if (!Value->TryGetObject(Item) || !(*Item)->TryGetStringField(TEXT("material_path"), Path)) return Failure(TEXT("Each material assignment requires material_path and slot_index or slot_name"));
            if ((*Item)->HasField(TEXT("slot_index")) == (*Item)->HasField(TEXT("slot_name"))) return Failure(TEXT("Exactly one slot selector required"));
            if ((*Item)->HasField(TEXT("slot_index"))) { if (!(*Item)->TryGetNumberField(TEXT("slot_index"), Index)) return Failure(TEXT("Invalid slot_index")); }
            else
            {
                if (!(*Item)->TryGetStringField(TEXT("slot_name"), Slot)) return Failure(TEXT("slot_name must be string"));
                for (int32 Candidate = 0; Candidate < MeshAsset->GetStaticMaterials().Num(); ++Candidate)
                    if (MeshAsset->GetStaticMaterials()[Candidate].MaterialSlotName.ToString() == Slot)
                    { if (Index != INDEX_NONE) return Failure(TEXT("Ambiguous material slot name")); Index = Candidate; }
            }
            if (Index < 0 || Index >= MeshAsset->GetStaticMaterials().Num() || Materials.Contains(Index)) return Failure(TEXT("Unknown or duplicate material slot"));
            UMaterialInterface* Material = Path.IsEmpty() ? nullptr : Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(Path));
            if (!Path.IsEmpty() && !Material) return Failure(TEXT("Material reference invalid"));
            Materials.Add(Index, Material);
        }
    }
    auto Before = MeshInfo(Component);
    const FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "SceneMesh", "Edit scene mesh and material slots"), World);
    Component->Modify();
    if (MeshAsset != Component->GetStaticMesh()) Component->SetStaticMesh(MeshAsset);
    for (const auto& Pair : Materials) Component->SetMaterial(Pair.Key, Pair.Value);
    Component->MarkRenderStateDirty(); Component->MarkPackageDirty();
    auto Result = MeshInfo(Component); Result->SetBoolField(TEXT("modified"), true); Result->SetBoolField(TEXT("saved"), false); Result->SetObjectField(TEXT("before"), Before);
    return Result;
}

TSharedPtr<FJsonObject> Manifest(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy || UnrealMCPSceneAssets::IsBusy()) return Failure(TEXT("Scene task active"));
    FString Error;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    if (World->IsPartitionedWorld()) return Failure(TEXT("World Partition manifest writes unsupported"));
    const TSharedPtr<FJsonObject>* ManifestData; bool DryRun = true;
    if (!Params->TryGetObjectField(TEXT("manifest"), ManifestData)
        || (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))) return Failure(TEXT("manifest object and boolean dry_run required"));
    auto Data = *ManifestData; FString Namespace, Hash, Units, UpAxis, Handedness; int32 Version;
    FString FolderRoot;
    const bool HasFolderRoot = Data->HasField(TEXT("folder_root"));
    if (HasFolderRoot && (!Data->TryGetStringField(TEXT("folder_root"), FolderRoot) || !ValidateFolderPath(FolderRoot, Error, false)))
        return Failure(Error.IsEmpty() ? TEXT("folder_root must be a string") : Error);
    if (HasFolderRoot && !SupportsFolderWorld(World)) return Failure(TEXT("Manifest folders require a single persistent level without external actors/objects"));
    if (!Data->TryGetNumberField(TEXT("version"), Version) || Version != 1
        || !Data->TryGetStringField(TEXT("namespace"), Namespace) || Namespace.IsEmpty() || Namespace.Len() > 32
        || !Data->TryGetStringField(TEXT("source_hash"), Hash) || Hash.IsEmpty() || Hash.Len() > 128
        || !Data->TryGetStringField(TEXT("units"), Units) || Units != TEXT("cm")
        || !Data->TryGetStringField(TEXT("up_axis"), UpAxis) || UpAxis != TEXT("Z")
        || !Data->TryGetStringField(TEXT("handedness"), Handedness) || Handedness != TEXT("left"))
        return Failure(TEXT("Manifest v1 requires namespace, source_hash, units=cm, up_axis=Z, handedness=left; convert DCC transforms explicitly"));
    auto ValidId = [](const FString& Id) { if (Id.IsEmpty() || Id.Len() > 32) return false; for (TCHAR Character : Id) if (!(FChar::IsAlnum(Character) || Character == TEXT('_'))) return false; return true; };
    if (!ValidId(Namespace)) return Failure(TEXT("Invalid manifest namespace"));
    for (TCHAR Character : Hash) if (!FChar::IsAlnum(Character) && Character != TEXT('_')) return Failure(TEXT("Invalid source_hash"));
    const TArray<TSharedPtr<FJsonValue>>* Objects;
    if (!Data->TryGetArrayField(TEXT("objects"), Objects) || Objects->IsEmpty() || Objects->Num() > 64) return Failure(TEXT("Manifest requires 1..64 objects"));
    const FName ManagedTag(*(TEXT("UnrealMCP.Manifest_") + Namespace));
    const FName HashTag(*(TEXT("UnrealMCP.SourceHash_") + Hash));
    struct FPlacement
    {
        FString Id, ParentId, Label;
        FString Folder;
        bool HasFolder = false;
        FName Name;
        FTransform Transform;
        AStaticMeshActor* Actor = nullptr;
        UStaticMesh* Mesh = nullptr;
        TMap<int32, UMaterialInterface*> Materials;
        TSharedPtr<FJsonObject> Request;
    };
    TArray<FPlacement> Nodes; TMap<FString, int32> Indices; TSet<FName> Ids;
    for (const auto& Value : *Objects)
    {
        const TSharedPtr<FJsonObject>* Object;
        if (!Value->TryGetObject(Object)) return Failure(TEXT("Each placement must be an object"));
        FPlacement Node; Node.Request = *Object;
        if (!(*Object)->TryGetStringField(TEXT("id"), Node.Id) || !ValidId(Node.Id) || Ids.Contains(FName(*Node.Id))) return Failure(TEXT("Invalid/duplicate stable object id"));
        Ids.Add(FName(*Node.Id)); Indices.Add(Node.Id, Nodes.Num());
        Node.Label = Node.Id;
        Node.HasFolder = HasFolderRoot;
        if ((*Object)->HasField(TEXT("folder")) && !HasFolderRoot) return Failure(TEXT("Object folder requires an explicit manifest folder_root"));
        if (HasFolderRoot)
        {
            FString RelativeFolder;
            if (((*Object)->HasField(TEXT("folder")) && !(*Object)->TryGetStringField(TEXT("folder"), RelativeFolder)) || !ValidateFolderPath(RelativeFolder, Error))
                return Failure(Error.IsEmpty() ? TEXT("Object folder must be a relative string") : Error);
            Node.Folder = RelativeFolder.IsEmpty() ? FolderRoot : FolderRoot + TEXT("/") + RelativeFolder;
            if (!ValidateFolderPath(Node.Folder, Error)) return Failure(Error);
        }
        if (((*Object)->HasField(TEXT("parent_id")) && !(*Object)->TryGetStringField(TEXT("parent_id"), Node.ParentId))
            || ((*Object)->HasField(TEXT("label")) && !(*Object)->TryGetStringField(TEXT("label"), Node.Label)) || Node.Label.Len() > 256) return Failure(TEXT("Invalid label/parent_id"));
        FVector Location, Rotation, Scale;
        if (!(*Object)->HasField(TEXT("location")) || !(*Object)->HasField(TEXT("rotation")) || !(*Object)->HasField(TEXT("scale"))
            || !Vector(*Object, TEXT("location"), Location) || !Vector(*Object, TEXT("rotation"), Rotation) || !Vector(*Object, TEXT("scale"), Scale) || Scale.GetAbsMin() < 0.0001)
            return Failure(TEXT("Every object requires finite local location/rotation/scale; units cm/degrees"));
        Node.Transform = FTransform(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Location, Scale);
        FString MeshPath;
        if (!(*Object)->TryGetStringField(TEXT("mesh"), MeshPath) || !MeshPath.StartsWith(TEXT("/")) || !(Node.Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath)))) return Failure(TEXT("Placement requires existing StaticMesh path"));
        Node.Name = FName(*(TEXT("MCPScene_") + Namespace + TEXT("_") + Node.Id));
        if (auto Existing = FindObject<UObject>(World->PersistentLevel, *Node.Name.ToString()))
        {
            Node.Actor = Cast<AStaticMeshActor>(Existing);
            if (!Node.Actor || !Node.Actor->ActorHasTag(ManagedTag) || Node.Actor->GetPackage() != World->GetPackage()) return Failure(TEXT("Placement name collision with unmanaged/external object: ") + Node.Id);
            if (Node.HasFolder && !SupportsFolderTarget(Node.Actor, World)) return Failure(TEXT("Unsupported manifest folder target: ") + Node.Id);
            FString ExpectedHash;
            if (Data->TryGetStringField(TEXT("expected_source_hash"), ExpectedHash) && !Node.Actor->ActorHasTag(FName(*(TEXT("UnrealMCP.SourceHash_") + ExpectedHash)))) return Failure(TEXT("Manifest source hash conflict: ") + Node.Id);
        }
        const TArray<TSharedPtr<FJsonValue>>* Materials;
        if ((*Object)->HasField(TEXT("materials")))
        {
            if (!(*Object)->TryGetArrayField(TEXT("materials"), Materials) || Materials->Num() > 64) return Failure(TEXT("materials must be an array of at most 64 slots"));
            for (const auto& Material : *Materials)
            {
                const TSharedPtr<FJsonObject>* Assignment; FString Path, SlotName; int32 Slot = INDEX_NONE;
                if (!Material->TryGetObject(Assignment) || !(*Assignment)->TryGetStringField(TEXT("material_path"), Path)
                    || (*Assignment)->HasField(TEXT("slot_index")) == (*Assignment)->HasField(TEXT("slot_name"))) return Failure(TEXT("Material needs path and exactly one slot selector"));
                if ((*Assignment)->HasField(TEXT("slot_index"))) { if (!(*Assignment)->TryGetNumberField(TEXT("slot_index"), Slot)) return Failure(TEXT("Invalid slot index")); }
                else
                {
                    if (!(*Assignment)->TryGetStringField(TEXT("slot_name"), SlotName)) return Failure(TEXT("Invalid slot name"));
                    for (int32 Index = 0; Index < Node.Mesh->GetStaticMaterials().Num(); ++Index) if (Node.Mesh->GetStaticMaterials()[Index].MaterialSlotName.ToString() == SlotName)
                    { if (Slot != INDEX_NONE) return Failure(TEXT("Ambiguous slot name")); Slot = Index; }
                }
                if (!Node.Mesh->GetStaticMaterials().IsValidIndex(Slot) || Node.Materials.Contains(Slot)) return Failure(TEXT("Invalid/duplicate material slot"));
                UMaterialInterface* Asset = Path.IsEmpty() ? nullptr : Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(Path));
                if (!Path.IsEmpty() && (!Path.StartsWith(TEXT("/")) || !Asset)) return Failure(TEXT("Material asset not found"));
                Node.Materials.Add(Slot, Asset);
            }
        }
        Nodes.Add(MoveTemp(Node));
    }
    for (const auto& Node : Nodes) if (!Node.ParentId.IsEmpty() && !Indices.Contains(Node.ParentId)) return Failure(TEXT("Unknown parent id"));
    TArray<int32> Order; TSet<FString> Ordered;
    for (int32 Pass = 0; Pass < Nodes.Num(); ++Pass) for (int32 Index = 0; Index < Nodes.Num(); ++Index)
        if (!Ordered.Contains(Nodes[Index].Id) && (Nodes[Index].ParentId.IsEmpty() || Ordered.Contains(Nodes[Index].ParentId))) { Ordered.Add(Nodes[Index].Id); Order.Add(Index); }
    if (Order.Num() != Nodes.Num()) return Failure(TEXT("Cyclic placement hierarchy"));
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("dry_run"), DryRun);
    TArray<TSharedPtr<FJsonValue>> Items; bool Modified = false;
    FScopedTransaction Transaction(TEXT("UnrealMCP.Scene"), NSLOCTEXT("UnrealMCP", "SceneManifest", "Apply scene placement manifest"), World, !DryRun);
    for (int32 Index : Order)
    {
        auto& Node = Nodes[Index]; AActor* Parent = Node.ParentId.IsEmpty() ? nullptr : Nodes[Indices[Node.ParentId]].Actor;
        auto Component = Node.Actor ? Node.Actor->GetStaticMeshComponent() : nullptr;
        bool PlacementChanged = !Node.Actor || Node.Actor->GetAttachParentActor() != Parent || (!Node.ParentId.IsEmpty() && !Parent)
            || !Component->GetRelativeTransform().Equals(Node.Transform) || Component->GetStaticMesh() != Node.Mesh
            || !Node.Actor->ActorHasTag(HashTag) || Node.Actor->GetActorLabel() != Node.Label;
        if (Component) for (const auto& Assignment : Node.Materials)
            PlacementChanged |= (Component->OverrideMaterials.IsValidIndex(Assignment.Key) ? Component->OverrideMaterials[Assignment.Key].Get() : nullptr) != Assignment.Value;
        const bool FolderChanged = Node.HasFolder && (!Node.Actor || Node.Actor->GetFolderPath() != FName(*Node.Folder));
        const bool Changed = PlacementChanged || FolderChanged;
        auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("id"), Node.Id); Item->SetStringField(TEXT("action"), !Node.Actor ? TEXT("create") : Changed ? TEXT("update") : TEXT("unchanged"));
        Item->SetObjectField(TEXT("requested"), Node.Request);
        if (Node.Actor) Item->SetObjectField(TEXT("before"), ActorInfo(Node.Actor));
        if (!DryRun && Changed)
        {
            if (!Modified) World->PersistentLevel->Modify();
            if (!Node.Actor)
            {
                FActorSpawnParameters Spawn; Spawn.Name = Node.Name; Spawn.OverrideLevel = World->PersistentLevel; Spawn.ObjectFlags |= RF_Transactional;
                Node.Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Spawn);
                if (!Node.Actor)
                {
                    Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("apply"));
                    Result->SetStringField(TEXT("error"), TEXT("Spawn failed after preflight; inspect partial items before retry")); break;
                }
                Component = Node.Actor->GetStaticMeshComponent(); Node.Actor->Tags.Add(TEXT("UnrealMCP.SceneManaged")); Node.Actor->Tags.Add(ManagedTag);
            }
            Node.Actor->Modify();
            if (PlacementChanged)
            {
                Component->Modify();
                if (Parent) Node.Actor->AttachToActor(Parent, FAttachmentTransformRules::KeepRelativeTransform);
                else Node.Actor->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
                Component->SetRelativeTransform(Node.Transform); Component->SetStaticMesh(Node.Mesh);
                for (const auto& Assignment : Node.Materials) Component->SetMaterial(Assignment.Key, Assignment.Value);
                Node.Actor->Tags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(TEXT("UnrealMCP.SourceHash_")); }); Node.Actor->Tags.Add(HashTag);
                Node.Actor->SetActorLabel(Node.Label, false); Node.Actor->PostEditMove(true);
            }
            if (FolderChanged) Node.Actor->SetFolderPath(FName(*Node.Folder));
            Node.Actor->MarkPackageDirty(); Modified = true;
        }
        if (Node.Actor) Item->SetObjectField(TEXT("after"), ActorInfo(Node.Actor));
        Items.Add(MakeShared<FJsonValueObject>(Item));
        if (!DryRun && Node.HasFolder && Node.Actor->GetFolderPath() != FName(*Node.Folder))
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("folder_readback"));
            Result->SetStringField(TEXT("error"), TEXT("Manifest folder assignment did not match; inspect partial items before retrying")); break;
        }
    }
    if (!Modified) Transaction.Cancel();
    Result->SetBoolField(TEXT("modified"), Modified); Result->SetBoolField(TEXT("package_dirty"), World->GetPackage()->IsDirty()); Result->SetArrayField(TEXT("items"), Items);
    return Result;
}

TSharedPtr<FJsonObject> Save(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy) return Failure(TEXT("Scene capture active"));
    FString Error;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    bool Confirm = false;
    if (!Params->TryGetBoolField(TEXT("confirm_all_changes_in_map"), Confirm) || !Confirm)
        return Failure(TEXT("Explicit confirmation required: saving includes ALL current changes in this map package"));
    if (World->IsPartitionedWorld() || World->GetLevels().Num() != 1) return Failure(TEXT("World Partition and streaming level saves are unsupported"));
    for (TActorIterator<AActor> It(World); It; ++It) if (It->GetPackage() != World->GetPackage()) return Failure(TEXT("External actor package save unsupported"));
    const FString Name = World->GetPackage()->GetName();
    if (!Name.StartsWith(TEXT("/Game/"))) return Failure(TEXT("Save requires a named /Game map"));
    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(Name, Filename, FPackageName::GetMapPackageExtension())) return Failure(TEXT("Invalid map package filename"));
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
    const bool Saved = UPackage::SavePackage(World->GetPackage(), World, *Filename, Args) && !World->GetPackage()->IsDirty() && FPackageName::DoesPackageExist(Name);
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), Saved); Result->SetBoolField(TEXT("saved"), Saved);
    Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("package_dirty"), World->GetPackage()->IsDirty());
    Result->SetStringField(TEXT("level_path"), Name);
    if (!Saved) Result->SetStringField(TEXT("error"), TEXT("Map save failed; no rollback"));
    return Result;
}

bool IsBusy() { return CaptureBusy; }

TSharedPtr<FJsonObject> RecaptureSky(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy || UnrealMCPSceneAssets::IsBusy()) return Failure(TEXT("Scene task active"));
    FString Error; UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    auto Sky = Cast<USkyLightComponent>(ResolveTarget(World, Params, Error));
    if (!Sky) return Failure(Error.IsEmpty() ? TEXT("Select an exact SkyLightComponent") : Error);
    Sky->RecaptureSky();
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("recapture_requested"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
    Result->SetStringField(TEXT("target"), Sky->GetPathName());
    Result->SetStringField(TEXT("note"), TEXT("Render recapture queued, not a completed frame; use bounded viewport warmup and compare actual image"));
    return Result;
}

FString UndoToken()
{
    return GEditor && GEditor->Trans ? GEditor->Trans->GetUndoContext().TransactionId.ToString() : FString();
}

void RecordTransaction(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonObject>& Result, const FString& BeforeToken)
{
    bool Modified = false;
    if (!Result->TryGetBoolField(TEXT("modified"), Modified) || !Modified || !GEditor || !GEditor->Trans) return;
    const auto Context = GEditor->Trans->GetUndoContext();
    if (Context.Context != TEXT("UnrealMCP.Scene") || Context.TransactionId.ToString() == BeforeToken || !Context.TransactionId.IsValid()) return;
    FString Error; UWorld* World = ResolveWorld(Params, Error);
    if (!World || Context.PrimaryObject != World) return;
    LastUndoToken = Context.TransactionId.ToString(); LastUndoWorld = World;
    Result->SetStringField(TEXT("transaction_id"), LastUndoToken);
}

TSharedPtr<FJsonObject> Undo(const TSharedPtr<FJsonObject>& Params)
{
    if (CaptureBusy || UnrealMCPSceneAssets::IsBusy()) return Failure(TEXT("Scene task active"));
    FString Error, Id; UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    if (!Params->TryGetStringField(TEXT("transaction_id"), Id) || Id.IsEmpty() || Id != LastUndoToken || LastUndoWorld.Get() != World || !GEditor->Trans)
        return Failure(TEXT("Only a recorded MCP scene transaction in this exact world may be undone"));
    const auto Context = GEditor->Trans->GetUndoContext();
    if (Context.Context != TEXT("UnrealMCP.Scene") || Context.TransactionId.ToString() != Id || Context.PrimaryObject != World)
        return Failure(TEXT("Undo stack changed; refusing to undo another operation"));
    if (!GEditor->UndoTransaction()) return Failure(TEXT("Editor refused undo"));
    LastUndoToken.Reset(); LastUndoWorld.Reset();
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), true);
    Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), World->GetPackage()->IsDirty());
    Result->SetStringField(TEXT("undone_transaction_id"), Id); return Result;
}

TSharedPtr<FJsonObject> Viewport(const TSharedPtr<FJsonObject>& Params, bool Write)
{
    FString Error;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    auto Client = ResolveViewport(World, Params, Error);
    if (!Client) return Failure(Error);
    if (Write)
    {
        if (CaptureBusy) return Failure(TEXT("Scene capture active"));
        FVector Location = Client->GetViewLocation(); const FRotator Previous = Client->GetViewRotation();
        FVector Rotation(Previous.Pitch, Previous.Yaw, Previous.Roll);
        double Fov = Client->ViewFOV; bool GameView = Client->IsInGameView(); FString CameraPath;
        if (!Vector(Params, TEXT("location"), Location) || !Vector(Params, TEXT("rotation"), Rotation)
            || (Params->HasField(TEXT("fov")) && !Params->TryGetNumberField(TEXT("fov"), Fov)) || !FMath::IsFinite(Fov) || Fov < 5 || Fov > 170
            || (Params->HasField(TEXT("game_view")) && !Params->TryGetBoolField(TEXT("game_view"), GameView))) return Failure(TEXT("Invalid viewport parameters"));
        ACameraActor* Camera = Cast<ACameraActor>(Client->GetActiveActorLock().Get());
        if (Params->HasField(TEXT("location")) || Params->HasField(TEXT("rotation")) || Params->HasField(TEXT("fov"))) Camera = nullptr;
        if (Params->HasField(TEXT("camera_path")))
        {
            Camera = nullptr;
            if (!Params->TryGetStringField(TEXT("camera_path"), CameraPath)) return Failure(TEXT("camera_path must be string"));
            if (!CameraPath.IsEmpty())
            {
                if (Params->HasField(TEXT("location")) || Params->HasField(TEXT("rotation")) || Params->HasField(TEXT("fov"))) return Failure(TEXT("Camera pose and explicit pose cannot be combined"));
                for (TActorIterator<ACameraActor> It(World); It; ++It) if (It->GetPathName() == CameraPath) Camera = *It;
                if (!Camera) return Failure(TEXT("Exact CameraActor path not found"));
            }
        }
        Client->SetActorLock(Camera);
        if (Camera) Client->UpdateViewForLockedActor();
        else { Client->SetViewLocation(Location); Client->SetViewRotation(FRotator(Rotation.X, Rotation.Y, Rotation.Z)); Client->ViewFOV = Client->FOVAngle = Fov; }
        Client->SetGameView(GameView); Client->Invalidate();
    }
    auto Result = ViewInfo(Client); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), Write); Result->SetBoolField(TEXT("saved"), false);
    return Result;
}

TSharedPtr<FJsonObject> Capture(const TSharedPtr<FJsonObject>& Params)
{
    FString Error, Id;
    UWorld* World = ResolveWorld(Params, Error);
    if (!World) return Failure(Error);
    if (!Params->TryGetStringField(TEXT("request_id"), Id) || Id.IsEmpty() || Id.Len() > 64) return Failure(TEXT("request_id required (1..64 letters/digits/underscore)"));
    for (TCHAR Character : Id) if (!(FChar::IsAlnum(Character) || Character == TEXT('_'))) return Failure(TEXT("Invalid request_id"));
    if (const auto Previous = Tasks.Find(Id)) return *Previous;
    if (CaptureBusy || Tasks.Num() >= 32) return Failure(TEXT("Capture already active or task cache full"));
    auto Client = ResolveViewport(World, Params, Error);
    if (!Client) return Failure(Error);
    int32 Width = 1280, Height = 720, Frames = 32, Seconds = 60;
    if ((Params->HasField(TEXT("width")) && !Params->TryGetNumberField(TEXT("width"), Width))
        || (Params->HasField(TEXT("height")) && !Params->TryGetNumberField(TEXT("height"), Height))
        || (Params->HasField(TEXT("warmup_frames")) && !Params->TryGetNumberField(TEXT("warmup_frames"), Frames))
        || (Params->HasField(TEXT("timeout_seconds")) && !Params->TryGetNumberField(TEXT("timeout_seconds"), Seconds))
        || Width < 64 || Height < 64 || Width > 4096 || Height > 4096 || Frames < 1 || Frames > 300 || Seconds < 5 || Seconds > 120)
        return Failure(TEXT("Capture dimensions 64..4096, warmup_frames 1..300, timeout_seconds 5..120 required"));
    auto SceneViewport = Client->Viewport;
    if (!SceneViewport || SceneViewport->GetSizeXY().GetMin() < 1) return Failure(TEXT("Level viewport has no render surface"));
    if (GUsingNullRHI) return Failure(TEXT("Scene capture requires a rendering RHI"));
    const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/UnrealMCP") / (Id + TEXT(".png")));
    if (IFileManager::Get().FileExists(*Filename)) return Failure(TEXT("Screenshot output already exists; use a new request_id"));
    const FIntPoint PreviousSize = SceneViewport->GetSizeXY();
    TSharedPtr<FSceneViewport> CaptureViewport = MakeShared<FSceneViewport>(Client, nullptr);
    CaptureViewport->UpdateViewportRHI(false, Width, Height, EWindowMode::Windowed, PF_B8G8R8A8);
    if (CaptureViewport->GetSizeXY() != FIntPoint(Width, Height)) return Failure(TEXT("Failed to allocate requested offscreen capture dimensions"));
    auto Result = ViewInfo(Client);
    Result->SetNumberField(TEXT("requested_width"), Width); Result->SetNumberField(TEXT("requested_height"), Height);
    Result->SetNumberField(TEXT("actual_width"), Width); Result->SetNumberField(TEXT("actual_height"), Height);
    Result->SetNumberField(TEXT("width"), Width); Result->SetNumberField(TEXT("height"), Height);
    Result->SetNumberField(TEXT("source_viewport_width"), PreviousSize.X); Result->SetNumberField(TEXT("source_viewport_height"), PreviousSize.Y);
    const TWeakObjectPtr<AActor> CameraLock = Client->GetActiveActorLock();
    const auto Camera = Cast<ACameraActor>(CameraLock.Get());
    const bool Constrained = Camera && Camera->GetCameraComponent()->bConstrainAspectRatio;
    const float CameraAspect = Camera ? Camera->GetCameraComponent()->AspectRatio : 0;
    Result->SetStringField(TEXT("aspect_ratio_policy"), Constrained ? TEXT("camera_letterbox") : TEXT("viewport_projection"));
    if (Camera) Result->SetNumberField(TEXT("camera_aspect_ratio"), CameraAspect);
    Result->SetStringField(TEXT("render_target"), TEXT("offscreen"));
    Result->SetBoolField(TEXT("success"), true); Result->SetStringField(TEXT("state"), TEXT("pending"));
    Result->SetStringField(TEXT("request_id"), Id); Result->SetStringField(TEXT("project_path"), ProjectPath());
    Result->SetStringField(TEXT("level_path"), World->GetPackage()->GetName());
    Result->SetNumberField(TEXT("warmup_frames"), Frames); Result->SetNumberField(TEXT("timeout_seconds"), Seconds);
    Result->SetBoolField(TEXT("saved"), false);
    Tasks.Add(Id, Result); CaptureBusy = true;
    const double Started = FPlatformTime::Seconds(); const FVector Location = Client->GetViewLocation(); const FRotator Rotation = Client->GetViewRotation(); const float Fov = Client->ViewFOV;
    const auto Exposure = Client->ExposureSettings; const bool GameView = Client->IsInGameView();
    TWeakObjectPtr<UWorld> ExpectedWorld(World);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Client, SceneViewport, CaptureViewport, Result, ExpectedWorld, CameraLock, Constrained, CameraAspect, Frames, Seconds, Started, Location, Rotation, Fov, Exposure, GameView, Width, Height, Filename, PreviousSize, Warmed = 0](float DeltaTime) mutable
    {
        const bool ClientAlive = GEditor && GEditor->GetLevelViewportClients().Contains(Client) && Client->Viewport == SceneViewport;
        auto Finish = [&](const FString& ErrorMessage)
        {
            Result->SetBoolField(TEXT("success"), ErrorMessage.IsEmpty()); Result->SetStringField(TEXT("state"), ErrorMessage.IsEmpty() ? TEXT("completed") : TEXT("failed"));
            Result->SetNumberField(TEXT("frames_rendered"), Warmed);
            Result->SetNumberField(TEXT("actual_width"), CaptureViewport->GetSizeXY().X); Result->SetNumberField(TEXT("actual_height"), CaptureViewport->GetSizeXY().Y);
            Result->SetNumberField(TEXT("width"), CaptureViewport->GetSizeXY().X); Result->SetNumberField(TEXT("height"), CaptureViewport->GetSizeXY().Y);
            Result->SetBoolField(TEXT("source_viewport_unchanged"), ClientAlive && SceneViewport->GetSizeXY() == PreviousSize);
            if (!ErrorMessage.IsEmpty()) Result->SetStringField(TEXT("error"), ErrorMessage);
            CaptureViewport.Reset();
            CaptureBusy = false; return false;
        };
        if (!ClientAlive || !ExpectedWorld.IsValid() || Client->GetWorld() != ExpectedWorld.Get() || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor) return Finish(TEXT("Viewport/world disappeared or PIE/Simulate started"));
        if (FPlatformTime::Seconds() - Started > Seconds) return Finish(TEXT("Capture timed out waiting for rendering/resources; do not recreate blindly"));
        if (!Client->GetViewLocation().Equals(Location, 0.01) || !Client->GetViewRotation().Equals(Rotation, 0.01) || !FMath::IsNearlyEqual(Client->ViewFOV, Fov)) return Finish(TEXT("Camera changed during capture"));
        if (Client->ExposureSettings.bFixed != Exposure.bFixed || Client->ExposureSettings.FixedEV100 != Exposure.FixedEV100 || Client->IsInGameView() != GameView) return Finish(TEXT("Exposure/Game View changed during capture"));
        if (Client->GetActiveActorLock() != CameraLock) return Finish(TEXT("Camera lock changed during capture"));
        if (auto LockedCamera = Cast<ACameraActor>(CameraLock.Get()))
            if (LockedCamera->GetCameraComponent()->bConstrainAspectRatio != Constrained || LockedCamera->GetCameraComponent()->AspectRatio != CameraAspect)
                return Finish(TEXT("Camera aspect constraint changed during capture"));
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) || IsAsyncLoading()) { Warmed = 0; return true; }
        {
            TGuardValue<FViewport*> DrawingViewport(Client->Viewport, CaptureViewport.Get());
            CaptureViewport->Draw(false); FlushRenderingCommands();
        }
        if (++Warmed < Frames) return true;
        if (CaptureViewport->GetSizeXY() != FIntPoint(Width, Height)) return Finish(TEXT("Offscreen viewport did not render at requested dimensions"));
        TArray<FColor> Pixels;
        if (!CaptureViewport->ReadPixels(Pixels) || Pixels.Num() != Width * Height) return Finish(TEXT("Viewport pixel read failed"));
        TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Width, Height, MakeArrayView(Pixels), Png);
        auto Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper")).CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper->SetCompressed(Png.GetData(), Png.Num()) || Wrapper->GetWidth() != Width || Wrapper->GetHeight() != Height) return Finish(TEXT("PNG validation failed"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        if (!FFileHelper::SaveArrayToFile(Png, *Filename) || IFileManager::Get().FileSize(*Filename) != Png.Num()) return Finish(TEXT("PNG save failed"));
        Result->SetStringField(TEXT("file_path"), Filename); Result->SetNumberField(TEXT("width"), Width); Result->SetNumberField(TEXT("height"), Height);
        Result->SetNumberField(TEXT("bytes"), Png.Num()); Result->SetBoolField(TEXT("saved"), true);
        Result->SetStringField(TEXT("convergence"), TEXT("bounded warmup, not a guarantee of Lumen/exposure convergence"));
        return Finish(FString());
    }));
    return Result;
}

TSharedPtr<FJsonObject> Task(const TSharedPtr<FJsonObject>& Params)
{
    FString Project, Id;
    if (!Params->TryGetStringField(TEXT("project_path"), Project) || !FPaths::IsSamePath(Project, ProjectPath())) return Failure(TEXT("Project mismatch"));
    if (!Params->TryGetStringField(TEXT("request_id"), Id) || !Tasks.Contains(Id)) return Failure(TEXT("Unknown request_id; editor restart expires task status; inspect output before retry"));
    return Tasks[Id];
}
}