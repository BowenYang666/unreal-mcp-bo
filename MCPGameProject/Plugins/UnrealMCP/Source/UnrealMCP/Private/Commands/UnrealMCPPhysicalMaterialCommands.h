#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UPrimitiveComponent;
class UBlueprint;
namespace UnrealMCPPhysicalMaterial
{
    TSharedPtr<FJsonObject> AssignMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> AssignLevelComponent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> AssignBlueprintComponent(UBlueprint* Blueprint, UPrimitiveComponent* Component, const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Trace(const TSharedPtr<FJsonObject>& Params);
}