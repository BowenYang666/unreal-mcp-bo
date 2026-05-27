#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UBTNode;
class UBTCompositeNode;

/**
 * Handler class for Project-wide MCP commands
 */
class UNREALMCP_API FUnrealMCPProjectCommands
{
public:
    FUnrealMCPProjectCommands();

    // Handle project commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Specific project command handlers
    TSharedPtr<FJsonObject> HandleCreateInputMapping(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetClassProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadBehaviorTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadBlackboard(const TSharedPtr<FJsonObject>& Params);

    // BT helpers
    static TSharedPtr<FJsonObject> BTNodeToJson(UBTNode* Node);
    static TSharedPtr<FJsonObject> BTCompositeToJson(UBTCompositeNode* Node);
}; 