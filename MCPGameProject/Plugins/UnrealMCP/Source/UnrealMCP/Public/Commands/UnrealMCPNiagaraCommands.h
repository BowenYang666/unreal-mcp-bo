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

	/** Set a rapid-iteration parameter on an emitter's spawn or update script (asset-level edit). */
	TSharedPtr<FJsonObject> HandleSetNiagaraRapidParameter(const TSharedPtr<FJsonObject>& Params);

	/** Modify emitter-level properties (sim target, local space, determinism, enabled, etc.). */
	TSharedPtr<FJsonObject> HandleModifyEmitterProperties(const TSharedPtr<FJsonObject>& Params);

	/** Add an emitter to a system by copying from another system or duplicating within the same system. */
	TSharedPtr<FJsonObject> HandleAddEmitterToSystem(const TSharedPtr<FJsonObject>& Params);

	/** Remove an emitter from a system by name. */
	TSharedPtr<FJsonObject> HandleRemoveEmitterFromSystem(const TSharedPtr<FJsonObject>& Params);
};
