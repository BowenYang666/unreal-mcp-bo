#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPDataTableCommands
{
    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Create(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Import(const TSharedPtr<FJsonObject>& Params);
}