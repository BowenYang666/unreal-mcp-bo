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

    /** Create a new Material asset. */
    TSharedPtr<FJsonObject> HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params);

    /** Add a material expression (node) to a Material. */
    TSharedPtr<FJsonObject> HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params);

    /** Set a property on a material expression node. */
    TSharedPtr<FJsonObject> HandleSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Params);

    /** Connect two material expression nodes. */
    TSharedPtr<FJsonObject> HandleConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params);

    /** Connect a material expression to a material input property (e.g. BaseColor). */
    TSharedPtr<FJsonObject> HandleConnectMaterialToProperty(const TSharedPtr<FJsonObject>& Params);

    /** Add a Custom HLSL expression node. */
    TSharedPtr<FJsonObject> HandleAddCustomHLSLExpression(const TSharedPtr<FJsonObject>& Params);

    /** Create a MaterialInstanceConstant from a parent Material. */
    TSharedPtr<FJsonObject> HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params);

    /** Add a comment box (group) to the material graph. */
    TSharedPtr<FJsonObject> HandleAddMaterialComment(const TSharedPtr<FJsonObject>& Params);

    /** Auto-layout material nodes using topological sort. */
    TSharedPtr<FJsonObject> HandleResetMaterialNodeLayout(const TSharedPtr<FJsonObject>& Params);

    /** Set the editor position of a material expression node. */
    TSharedPtr<FJsonObject> HandleSetExpressionPosition(const TSharedPtr<FJsonObject>& Params);
};
