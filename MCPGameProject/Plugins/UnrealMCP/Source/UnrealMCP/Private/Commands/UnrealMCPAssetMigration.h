#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPAssetMigration
{
#if WITH_DEV_AUTOMATION_TESTS
    TMap<FString, FString> SnapshotForTests(UPackage* Package, const TMap<FString, FString>& ObjectPaths = {});
    bool CheckSnapshotForTests(const TMap<FString, FString>& Expected, const TMap<FString, FString>& Actual,
        const TSharedPtr<FJsonObject>& Result);
#endif
    TSharedPtr<FJsonObject> Plan(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Status(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Verify(const TSharedPtr<FJsonObject>& Params);
    int64 RemapPackage(UPackage* Package, const TMap<UObject*, UObject*>& Objects, const TMap<FString, FString>& Packages,
        const TMap<FString, FString>& Namespaces = {});
}