#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPComponentDefaults
{
    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Write(const TSharedPtr<FJsonObject>& Params);
}