#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPAnimation
{
    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> SetAdditive(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> AddNotify(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> ReadNotifyProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> PatchNotifyProperties(const TSharedPtr<FJsonObject>& Params);
}