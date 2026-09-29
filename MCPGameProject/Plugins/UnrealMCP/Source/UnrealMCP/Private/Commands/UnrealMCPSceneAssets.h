#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPSceneAssets
{
    TSharedPtr<FJsonObject> Inspect(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Import(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Status(const TSharedPtr<FJsonObject>& Params);
    bool IsBusy();
#if WITH_DEV_AUTOMATION_TESTS
    int32 CachedReceiptCountForTests();
    bool HasCachedReceiptForTests(const FString& Id);
    int32 ImportDispatchCountForTests();
#endif
}