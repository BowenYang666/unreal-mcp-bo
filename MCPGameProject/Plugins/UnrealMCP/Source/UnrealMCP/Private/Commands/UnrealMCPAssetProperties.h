#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPAssetProperties
{
    TSharedPtr<FJsonObject> CreateDataAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> CreatePhysicalMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Write(const TSharedPtr<FJsonObject>& Params);
}