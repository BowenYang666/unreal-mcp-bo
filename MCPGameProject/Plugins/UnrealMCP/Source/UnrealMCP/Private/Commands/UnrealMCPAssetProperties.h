#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPAssetProperties
{
    TSharedPtr<FJsonValue> EncodeValue(FProperty* Property, const void* Address, FString& Error);
    bool DecodeValue(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, FString& Error);
    TSharedPtr<FJsonObject> DescribeValue(FProperty* Property);
    TSharedPtr<FJsonObject> CreateDataAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> CreatePhysicalMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Write(const TSharedPtr<FJsonObject>& Params);
}