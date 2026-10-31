#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPStringTables
{
    TSharedPtr<FJsonObject> Handle(const FString& Command, const TSharedPtr<FJsonObject>& Params);
}
