#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Material-related MCP commands.
 * Provides read-only access to Material assets, their node graphs,
 * and MaterialInstance parameter values.
 */
class UNREALMCP_API FUnrealMCPMaterialCommands
{
public:
    FUnrealMCPMaterialCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /** List all Material and MaterialInstance assets in the project. */
    TSharedPtr<FJsonObject> HandleListMaterials(const TSharedPtr<FJsonObject>& Params);

    /** Read a Material's expression node graph, connections, and properties. */
    TSharedPtr<FJsonObject> HandleReadMaterial(const TSharedPtr<FJsonObject>& Params);

    /** Get a MaterialInstance's parameter overrides (scalar, vector, texture). */
    TSharedPtr<FJsonObject> HandleGetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params);
};
