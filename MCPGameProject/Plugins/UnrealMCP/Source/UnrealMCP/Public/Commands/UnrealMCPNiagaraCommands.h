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

	/** List available Niagara emitter templates (engine built-in). */
	TSharedPtr<FJsonObject> HandleListNiagaraEmitterTemplates(const TSharedPtr<FJsonObject>& Params);

	/** Add an emitter to a system by copying from another system, engine template, or duplicating within the same system. */
	TSharedPtr<FJsonObject> HandleAddEmitterToSystem(const TSharedPtr<FJsonObject>& Params);

	/** Remove an emitter from a system by name. */
	TSharedPtr<FJsonObject> HandleRemoveEmitterFromSystem(const TSharedPtr<FJsonObject>& Params);

	/** Add a module script to an emitter's spawn or update stack. */
	TSharedPtr<FJsonObject> HandleAddModuleToEmitter(const TSharedPtr<FJsonObject>& Params);

	/** Remove a module from an emitter's spawn or update stack. */
	TSharedPtr<FJsonObject> HandleRemoveModuleFromEmitter(const TSharedPtr<FJsonObject>& Params);

	/** Set a property (Material, SubImageSize, etc.) on an emitter's renderer. */
	TSharedPtr<FJsonObject> HandleSetNiagaraRendererProperty(const TSharedPtr<FJsonObject>& Params);

	/** List all input pins of a module in an emitter script, with their type and current value mode. */
	TSharedPtr<FJsonObject> HandleListModuleInputs(const TSharedPtr<FJsonObject>& Params);

	/** Enable a module input pin as a Local Value (creates a rapid-iteration parameter) so it can be read/written. */
	TSharedPtr<FJsonObject> HandleEnableModuleInput(const TSharedPtr<FJsonObject>& Params);

	/** List a module's static-switch selectors ("Mode" dropdowns) with current + allowed values. */
	TSharedPtr<FJsonObject> HandleListModuleStaticSwitches(const TSharedPtr<FJsonObject>& Params);

	/** Set a module static-switch selector (e.g. "Sprite Rotation Mode" = "Random Range"). */
	TSharedPtr<FJsonObject> HandleSetModuleStaticSwitch(const TSharedPtr<FJsonObject>& Params);

	/** Bind a DataInterface-typed module input (e.g. SubUVAnimation."Sprite Renderer") to an emitter renderer or asset. */
	TSharedPtr<FJsonObject> HandleBindModuleInputDataInterface(const TSharedPtr<FJsonObject>& Params);

	/** Read the keyframes of a curve DataInterface module input (ScaleColor, ScaleSpriteSize, FloatFromCurve, ...). */
	TSharedPtr<FJsonObject> HandleReadCurve(const TSharedPtr<FJsonObject>& Params);

	/** Write the keyframes of a curve DataInterface module input (creates an override curve). */
	TSharedPtr<FJsonObject> HandleSetCurveKeys(const TSharedPtr<FJsonObject>& Params);

	/** Set a module input's dynamic input (e.g. "Float from Curve") so it samples a curve/function. */
	TSharedPtr<FJsonObject> HandleSetModuleDynamicInput(const TSharedPtr<FJsonObject>& Params);
};
