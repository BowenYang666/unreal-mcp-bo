#include "Commands/UnrealMCPPhysicalMaterialCommands.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealMCPPhysicalMaterial
{
namespace
{
    FString Reference(UPhysicalMaterial* Material) { return Material ? Material->GetPathName() : FString(); }

    TSharedPtr<FJsonObject> Error(const FString& Message)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetBoolField(TEXT("modified"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("error"), Message);
        return Result;
    }

    bool Resolve(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, UPhysicalMaterial*& Material, FString& Message)
    {
        FString Path;
        const auto* Value = Params->Values.Find(Key);
        if (!Value || ((*Value)->Type != EJson::Null && !(*Value)->TryGetString(Path)))
        { Message = TEXT("Physical material reference must be a full asset path, empty string or null"); return false; }
        Material = nullptr;
        if (Path.IsEmpty()) return true;
        if (!Path.StartsWith(TEXT("/")) || Path.Contains(TEXT(":"))) { Message = TEXT("Use a full PhysicalMaterial asset path"); return false; }
        Material = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(Path));
        if (!Material || Material->GetClass() != UPhysicalMaterial::StaticClass()) { Message = TEXT("Reference is not a PhysicalMaterial asset"); return false; }
        return true;
    }

    bool Preflight(UObject* Target, UPhysicalMaterial* Previous, const TSharedPtr<FJsonObject>& Params, bool& Save, FString& Message)
    {
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) { Message = TEXT("save must be boolean"); return false; }
        if (!Target->GetPackage()->GetName().StartsWith(TEXT("/Game/"))) { Message = TEXT("Only /Game targets may be modified"); return false; }
        if (Save && Target->GetPackage()->IsDirty()) { Message = TEXT("Target package already dirty; save explicitly or use save=false"); return false; }
        if (Params->HasField(TEXT("expected_value")))
        {
            UPhysicalMaterial* Expected;
            if (!Resolve(Params, TEXT("expected_value"), Expected, Message)) return false;
            if (Previous != Expected) { Message = TEXT("expected_value conflict"); return false; }
        }
        return true;
    }

    TSharedPtr<FJsonObject> Finish(UObject* Target, UPhysicalMaterial* Before, UPhysicalMaterial* After, bool Changed, bool Save, const FString& Failure = FString())
    {
        UPackage* Package = Target->GetPackage();
        bool Saved = false;
        if (Save && Failure.IsEmpty())
        {
            const FString Extension = Cast<UWorld>(Target) ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
            FString Filename;
            if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, Extension))
            {
                FSavePackageArgs Args;
                Args.TopLevelFlags = RF_Public | RF_Standalone;
                Saved = UPackage::SavePackage(Package, Target, *Filename, Args) && !Package->IsDirty() && FPackageName::DoesPackageExist(Package->GetName());
            }
        }
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), Failure.IsEmpty() && (!Save || Saved));
        Result->SetBoolField(TEXT("modified"), Changed);
        Result->SetBoolField(TEXT("saved"), Saved);
        Result->SetBoolField(TEXT("package_dirty"), Package->IsDirty());
        Result->SetStringField(TEXT("target"), Target->GetPathName());
        Result->SetStringField(TEXT("before"), Reference(Before));
        Result->SetStringField(TEXT("after"), Reference(After));
        if (!Failure.IsEmpty() || (Save && !Saved))
        {
            Result->SetStringField(TEXT("stage"), Failure.IsEmpty() ? TEXT("save") : TEXT("compile"));
            Result->SetStringField(TEXT("error"), Failure.IsEmpty() ? TEXT("Save failed; in-memory modification may remain") : Failure);
        }
        return Result;
    }

    UWorld* World(const TSharedPtr<FJsonObject>& Params)
    {
        FString Level;
        UWorld* Current = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!Current || GEditor->PlayWorld || !Params->TryGetStringField(TEXT("level_path"), Level)
            || Current->GetPackage()->GetName() != Level) return nullptr;
        return Current;
    }

    bool Vector(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, FVector& Value)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values;
        if (!Params->TryGetArrayField(Key, Values) || Values->Num() != 3) return false;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            double Number;
            if (!(*Values)[Index]->TryGetNumber(Number) || !FMath::IsFinite(Number)) return false;
            Value[Index] = Number;
        }
        return true;
    }
}

TSharedPtr<FJsonObject> AssignMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString Path, Message;
    if (!Params->TryGetStringField(TEXT("asset_path"), Path)) return Error(TEXT("asset_path required"));
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(Path));
    if (!Material || !(Material->IsA<UMaterial>() || Material->IsA<UMaterialInstanceConstant>())) return Error(TEXT("Target must be Material or MaterialInstanceConstant"));
    FObjectProperty* Property = FindFProperty<FObjectProperty>(Material->GetClass(), TEXT("PhysMaterial"));
    if (!Property) return Error(TEXT("Physical material property unavailable"));
    UPhysicalMaterial* Before = Cast<UPhysicalMaterial>(Property->GetObjectPropertyValue_InContainer(Material));
    UPhysicalMaterial* Desired;
    bool Save = true;
    if (!Resolve(Params, TEXT("physical_material_path"), Desired, Message) || !Preflight(Material, Before, Params, Save, Message)) return Error(Message);
    const bool Changed = Before != Desired;
    if (Changed)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "AssignPhysMat", "Assign physical material"));
        Material->Modify();
        Material->PreEditChange(Property);
        Property->SetObjectPropertyValue_InContainer(Material, Desired);
        FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
        Material->PostEditChangeProperty(Event);
        Material->MarkPackageDirty();
        for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
        {
            if (!It->IsRegistered()) continue;
            bool UsesMaterial = false;
            for (int32 Slot = 0; Slot < It->GetNumMaterials() && !UsesMaterial; ++Slot)
            {
                TSet<UMaterialInterface*> Visited;
                for (UMaterialInterface* Current = It->GetMaterial(Slot); Current && !Visited.Contains(Current);)
                {
                    Visited.Add(Current);
                    if (Current == Material) { UsesMaterial = true; break; }
                    UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
                    Current = Instance ? Instance->Parent : nullptr;
                }
            }
            if (UsesMaterial)
                if (FBodyInstance* Body = It->GetBodyInstance()) Body->UpdatePhysicalMaterials();
        }
    }
    auto Result = Finish(Material, Before, Cast<UPhysicalMaterial>(Property->GetObjectPropertyValue_InContainer(Material)), Changed, Save);
    Result->SetStringField(TEXT("effective_physical_material"), Reference(Material->GetPhysicalMaterial()));
    return Result;
}

TSharedPtr<FJsonObject> AssignLevelComponent(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* Current = World(Params);
    if (!Current) return Error(TEXT("Expected current editor level_path and no active PIE"));
    FString ActorName, ComponentName, Message;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName) || !Params->TryGetStringField(TEXT("component_name"), ComponentName)) return Error(TEXT("actor_name and component_name required"));
    AActor* Actor = nullptr;
    for (TActorIterator<AActor> It(Current); It; ++It)
        if (It->GetName() == ActorName || It->GetActorLabel() == ActorName)
        {
            if (Actor) return Error(TEXT("Ambiguous actor name"));
            Actor = *It;
        }
    if (!Actor || Actor->GetPackage() != Current->GetPackage()) return Error(TEXT("Actor not found or externally packaged; unsupported target"));
    UPrimitiveComponent* Component = nullptr;
    TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
    for (UPrimitiveComponent* Candidate : Components) if (Candidate->GetName() == ComponentName) Component = Candidate;
    if (!Component) return Error(TEXT("Primitive component not found"));
    UPhysicalMaterial* Before = Component->BodyInstance.GetPhysMaterialOverride();
    UPhysicalMaterial* Desired;
    bool Save = false;
    if (!Resolve(Params, TEXT("physical_material_path"), Desired, Message) || !Preflight(Current, Before, Params, Save, Message)) return Error(Message);
    const bool Changed = Before != Desired;
    if (Changed)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "ComponentPhysMat", "Set component physical material"));
        Actor->Modify(); Component->Modify();
        Component->SetPhysMaterialOverride(Desired);
        Actor->MarkPackageDirty();
    }
    auto Result = Finish(Current, Before, Component->BodyInstance.GetPhysMaterialOverride(), Changed, Save);
    Result->SetStringField(TEXT("component"), Component->GetPathName());
    return Result;
}

TSharedPtr<FJsonObject> AssignBlueprintComponent(UBlueprint* Blueprint, UPrimitiveComponent* Component, const TSharedPtr<FJsonObject>& Params)
{
    if (!Blueprint || !Component) return Error(TEXT("Expected Blueprint primitive component template"));
    FString Message;
    UPhysicalMaterial* Before = Component->BodyInstance.GetPhysMaterialOverride();
    UPhysicalMaterial* Desired;
    bool Save = false;
    if (!Resolve(Params, TEXT("property_value"), Desired, Message) || !Preflight(Blueprint, Before, Params, Save, Message)) return Error(Message);
    const bool Changed = Before != Desired;
    if (Changed)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "BlueprintPhysMat", "Set Blueprint physical material"));
        const FName TemplateName = Component->GetFName();
        Blueprint->Modify(); Component->Modify();
        Component->SetPhysMaterialOverride(Desired);
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        Component = nullptr;
        if (Blueprint->SimpleConstructionScript)
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
                if (Node->ComponentTemplate && Node->ComponentTemplate->GetFName() == TemplateName)
                    Component = Cast<UPrimitiveComponent>(Node->ComponentTemplate);
        if (!Component)
        {
            auto Result = Finish(Blueprint, Before, nullptr, true, false, TEXT("Component template unavailable after compile; not saved"));
            Result->RemoveField(TEXT("after"));
            return Result;
        }
        UPhysicalMaterial* Actual = Component->BodyInstance.GetPhysMaterialOverride();
        if (Blueprint->Status == BS_Error) return Finish(Blueprint, Before, Actual, true, false, TEXT("Blueprint compile failed; not saved"));
        if (Actual != Desired) return Finish(Blueprint, Before, Actual, true, false, TEXT("Compiled override differs from requested value; not saved"));
    }
    return Finish(Blueprint, Before, Component->BodyInstance.GetPhysMaterialOverride(), Changed, Save);
}

TSharedPtr<FJsonObject> Trace(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* Current = World(Params);
    FVector Start, End;
    bool Complex = false;
    if (!Current || !Vector(Params, TEXT("start"), Start) || !Vector(Params, TEXT("end"), End)) return Error(TEXT("Expected current editor level_path and finite start/end vectors"));
    if (Params->HasField(TEXT("trace_complex")) && !Params->TryGetBoolField(TEXT("trace_complex"), Complex)) return Error(TEXT("trace_complex must be boolean"));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(UnrealMCPPhysicalSurface), Complex);
    Query.bReturnPhysicalMaterial = true;
    FHitResult Hit;
    const bool Blocking = Current->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("blocking_hit"), Blocking);
    Result->SetStringField(TEXT("physical_material"), Reference(Hit.PhysMaterial.Get()));
    Result->SetStringField(TEXT("surface_type"), StaticEnum<EPhysicalSurface>()->GetNameStringByValue(UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get())));
    Result->SetStringField(TEXT("actor"), Hit.GetActor() ? Hit.GetActor()->GetName() : FString());
    Result->SetStringField(TEXT("component"), Hit.GetComponent() ? Hit.GetComponent()->GetName() : FString());
    return Result;
}
}