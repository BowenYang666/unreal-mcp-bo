#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UnrealMCPScene
{
    TSharedPtr<FJsonObject> Context(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Inspect(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Patch(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> List(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> ManageActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> SetActorFolders(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Mesh(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Save(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Viewport(const TSharedPtr<FJsonObject>& Params, bool Write);
    TSharedPtr<FJsonObject> Capture(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Task(const TSharedPtr<FJsonObject>& Params);
    FString UndoToken();
    void RecordTransaction(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonObject>& Result, const FString& BeforeToken);
    TSharedPtr<FJsonObject> Undo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> Manifest(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> RecaptureSky(const TSharedPtr<FJsonObject>& Params);
    bool IsBusy();
}