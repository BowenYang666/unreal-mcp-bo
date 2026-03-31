#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Niagara VFX-related MCP commands.
 * Provides tools to list, inspect, create, and configure Niagara systems and their parameters.
 */
class UNREALMCP_API FUnrealMCPNiagaraCommands
{
public:
	FUnrealMCPNiagaraCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	/** List all UNiagaraSystem assets in the project (optionally including engine content). */
	TSharedPtr<FJsonObject> HandleListNiagaraSystems(const TSharedPtr<FJsonObject>& Params);

	/** Read detailed info about a specific Niagara system: emitters, User.* parameters, sim targets. */
	TSharedPtr<FJsonObject> HandleReadNiagaraSystem(const TSharedPtr<FJsonObject>& Params);

	/** Set a User.* parameter on a Niagara component in the level. */
	TSharedPtr<FJsonObject> HandleSetNiagaraParameter(const TSharedPtr<FJsonObject>& Params);

	/** Get all User.* parameter current values from a placed Niagara component. */
	TSharedPtr<FJsonObject> HandleGetNiagaraParameters(const TSharedPtr<FJsonObject>& Params);

	/** Create a new Niagara system asset (empty or duplicated from a template). */
	TSharedPtr<FJsonObject> HandleCreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
};
