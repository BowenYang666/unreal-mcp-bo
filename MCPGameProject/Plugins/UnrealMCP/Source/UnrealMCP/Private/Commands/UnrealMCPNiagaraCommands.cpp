#include "Commands/UnrealMCPNiagaraCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"

// Niagara includes
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraActor.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

// Editor includes
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Factories/Factory.h"

// Helper: Serialize a NiagaraParameterStore's variables to JSON array
static TArray<TSharedPtr<FJsonValue>> SerializeParameterStore(const FNiagaraParameterStore& Store)
{
	TArray<TSharedPtr<FJsonValue>> ParamArray;
	TArray<FNiagaraVariable> Params;
	Store.GetParameters(Params);

	for (const FNiagaraVariable& Var : Params)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), Var.GetName().ToString());
		FNiagaraTypeDefinition TypeDef = Var.GetType();
		ParamObj->SetStringField(TEXT("type"), TypeDef.GetName());

		// Try extracting value based on type using templated getter
		int32 Offset = Store.IndexOf(Var);
		if (Offset != INDEX_NONE)
		{
			if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
			{
				ParamObj->SetNumberField(TEXT("value"), Store.GetParameterValue<float>(Var));
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
			{
				ParamObj->SetNumberField(TEXT("value"), Store.GetParameterValue<int32>(Var));
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
			{
				FNiagaraBool Value = Store.GetParameterValue<FNiagaraBool>(Var);
				ParamObj->SetBoolField(TEXT("value"), Value.GetValue());
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def() || TypeDef == FNiagaraTypeDefinition::GetPositionDef())
			{
				FVector3f Value = Store.GetParameterValue<FVector3f>(Var);
				TArray<TSharedPtr<FJsonValue>> Vec;
				Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Z));
				ParamObj->SetArrayField(TEXT("value"), Vec);
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
			{
				FVector2f Value = Store.GetParameterValue<FVector2f>(Var);
				TArray<TSharedPtr<FJsonValue>> Vec;
				Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
				ParamObj->SetArrayField(TEXT("value"), Vec);
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetVec4Def())
			{
				FVector4f Value = Store.GetParameterValue<FVector4f>(Var);
				TArray<TSharedPtr<FJsonValue>> Vec;
				Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Z));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.W));
				ParamObj->SetArrayField(TEXT("value"), Vec);
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
			{
				FLinearColor Value = Store.GetParameterValue<FLinearColor>(Var);
				TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
				ColorObj->SetNumberField(TEXT("r"), Value.R);
				ColorObj->SetNumberField(TEXT("g"), Value.G);
				ColorObj->SetNumberField(TEXT("b"), Value.B);
				ColorObj->SetNumberField(TEXT("a"), Value.A);
				ParamObj->SetObjectField(TEXT("value"), ColorObj);
			}
		}

		ParamArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}
	return ParamArray;
}

// Helper: Get module nodes from a script's graph
static TArray<TSharedPtr<FJsonValue>> GetModulesFromScript(UNiagaraScript* Script)
{
	TArray<TSharedPtr<FJsonValue>> ModuleArray;
	if (!Script) return ModuleArray;

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!Source || !Source->NodeGraph) return ModuleArray;

	TArray<TObjectPtr<UEdGraphNode>>& AllNodes = Source->NodeGraph->Nodes;
	for (UEdGraphNode* Node : AllNodes)
	{
		UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (!FuncNode) continue;

		TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
		ModObj->SetStringField(TEXT("name"), FuncNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
		ModObj->SetBoolField(TEXT("enabled"), FuncNode->IsNodeEnabled());

		if (FuncNode->FunctionScript)
		{
			ModObj->SetStringField(TEXT("script_name"), FuncNode->FunctionScript->GetName());
		}

		ModuleArray.Add(MakeShared<FJsonValueObject>(ModObj));
	}
	return ModuleArray;
}

FUnrealMCPNiagaraCommands::FUnrealMCPNiagaraCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("list_niagara_systems"))
	{
		return HandleListNiagaraSystems(Params);
	}
	else if (CommandType == TEXT("read_niagara_system"))
	{
		return HandleReadNiagaraSystem(Params);
	}
	else if (CommandType == TEXT("set_niagara_parameter"))
	{
		return HandleSetNiagaraParameter(Params);
	}
	else if (CommandType == TEXT("get_niagara_parameters"))
	{
		return HandleGetNiagaraParameters(Params);
	}
	else if (CommandType == TEXT("create_niagara_system"))
	{
		return HandleCreateNiagaraSystem(Params);
	}
	else if (CommandType == TEXT("set_niagara_rapid_parameter"))
	{
		return HandleSetNiagaraRapidParameter(Params);
	}
	else if (CommandType == TEXT("modify_emitter_properties"))
	{
		return HandleModifyEmitterProperties(Params);
	}
	else if (CommandType == TEXT("list_niagara_emitter_templates"))
	{
		return HandleListNiagaraEmitterTemplates(Params);
	}
	else if (CommandType == TEXT("add_emitter_to_system"))
	{
		return HandleAddEmitterToSystem(Params);
	}
	else if (CommandType == TEXT("remove_emitter_from_system"))
	{
		return HandleRemoveEmitterFromSystem(Params);
	}
	else if (CommandType == TEXT("add_module_to_emitter"))
	{
		return HandleAddModuleToEmitter(Params);
	}
	else if (CommandType == TEXT("remove_module_from_emitter"))
	{
		return HandleRemoveModuleFromEmitter(Params);
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown Niagara command: %s"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// list_niagara_systems
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleListNiagaraSystems(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = TEXT("/Game");
	if (Params->HasField(TEXT("path")))
	{
		Path = Params->GetStringField(TEXT("path"));
	}

	bool bIncludeEngineContent = false;
	if (Params->HasField(TEXT("include_engine_content")))
	{
		bIncludeEngineContent = Params->GetBoolField(TEXT("include_engine_content"));
	}

	FString NameFilter;
	if (Params->HasField(TEXT("name_filter")))
	{
		NameFilter = Params->GetStringField(TEXT("name_filter"));
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	if (!bIncludeEngineContent)
	{
		Filter.PackagePaths.Add(FName(*Path));
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TArray<TSharedPtr<FJsonValue>> SystemArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		FString AssetName = AssetData.AssetName.ToString();

		if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		// Filter engine content if not including it
		FString PackagePath = AssetData.PackagePath.ToString();
		bool bIsEngineContent = PackagePath.StartsWith(TEXT("/Engine")) || PackagePath.StartsWith(TEXT("/Niagara"));
		
		if (!bIncludeEngineContent && bIsEngineContent)
		{
			continue;
		}

		TSharedPtr<FJsonObject> SysObj = MakeShared<FJsonObject>();
		SysObj->SetStringField(TEXT("name"), AssetName);
		SysObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		SysObj->SetStringField(TEXT("package_path"), PackagePath);
		SysObj->SetBoolField(TEXT("is_engine_content"), bIsEngineContent);

		// Try to load to get emitter count
		UNiagaraSystem* System = Cast<UNiagaraSystem>(AssetData.GetAsset());
		if (System)
		{
			SysObj->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
		}

		SystemArray.Add(MakeShared<FJsonValueObject>(SysObj));
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetNumberField(TEXT("count"), SystemArray.Num());
	ResultJson->SetArrayField(TEXT("systems"), SystemArray);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// read_niagara_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleReadNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("name")) && !Params->HasField(TEXT("path")))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'name' or 'path'"));
	}

	UNiagaraSystem* System = nullptr;

	if (Params->HasField(TEXT("path")))
	{
		FString AssetPath = Params->GetStringField(TEXT("path"));
		UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(AssetPath);
		System = Cast<UNiagaraSystem>(LoadedObj);
		if (!System)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Niagara system not found at path: %s"), *AssetPath));
		}
	}
	else
	{
		FString Name = Params->GetStringField(TEXT("name"));

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			if (AssetData.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				System = Cast<UNiagaraSystem>(AssetData.GetAsset());
				break;
			}
		}

		if (!System)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Niagara system not found: %s"), *Name));
		}
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("name"), System->GetName());
	ResultJson->SetStringField(TEXT("path"), System->GetPathName());
	ResultJson->SetBoolField(TEXT("needs_warm_up"), System->NeedsWarmup());
	ResultJson->SetNumberField(TEXT("warm_up_time"), System->GetWarmupTime());
	ResultJson->SetBoolField(TEXT("fixed_bounds"), System->GetFixedBounds().IsValid != 0);

	// Emitters
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	TArray<TSharedPtr<FJsonValue>> EmitterArray;

	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
		EmitterObj->SetStringField(TEXT("unique_name"), Handle.GetUniqueInstanceName());

		// Get the versioned emitter data
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData)
		{
			// Sim target
			ENiagaraSimTarget SimTarget = EmitterData->SimTarget;
			EmitterObj->SetStringField(TEXT("sim_target"), SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));

			EmitterObj->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
			EmitterObj->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);

			// Module stacks (Spawn / Update)
			auto AddScriptModules = [&](const TCHAR* SectionName, UNiagaraScript* Script)
			{
				if (!Script) return;

				TSharedPtr<FJsonObject> ScriptSection = MakeShared<FJsonObject>();

				// Modules from graph
				TArray<TSharedPtr<FJsonValue>> Modules = GetModulesFromScript(Script);
				ScriptSection->SetArrayField(TEXT("modules"), Modules);

				// Rapid iteration parameters
				TArray<TSharedPtr<FJsonValue>> RapidParams = SerializeParameterStore(Script->RapidIterationParameters);
				ScriptSection->SetArrayField(TEXT("rapid_iteration_parameters"), RapidParams);

				EmitterObj->SetObjectField(SectionName, ScriptSection);
			};

			AddScriptModules(TEXT("spawn_script"), EmitterData->SpawnScriptProps.Script);
			AddScriptModules(TEXT("update_script"), EmitterData->UpdateScriptProps.Script);

			// Renderers
			const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
			TArray<TSharedPtr<FJsonValue>> RendererArray;
			for (const UNiagaraRendererProperties* Renderer : Renderers)
			{
				if (!Renderer) continue;
				TSharedPtr<FJsonObject> RendObj = MakeShared<FJsonObject>();
				RendObj->SetStringField(TEXT("type"), Renderer->GetClass()->GetName());
				RendObj->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
				RendererArray.Add(MakeShared<FJsonValueObject>(RendObj));
			}
			EmitterObj->SetArrayField(TEXT("renderers"), RendererArray);
		}

		EmitterArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}

	ResultJson->SetNumberField(TEXT("emitter_count"), EmitterArray.Num());
	ResultJson->SetArrayField(TEXT("emitters"), EmitterArray);

	// User-exposed parameters
	const FNiagaraUserRedirectionParameterStore& UserParamStore = System->GetExposedParameters();
	TArray<FNiagaraVariable> UserParams;
	UserParamStore.GetParameters(UserParams);

	TArray<TSharedPtr<FJsonValue>> ParamArray;

	for (const FNiagaraVariable& Var : UserParams)
	{
		FString ParamName = Var.GetName().ToString();

		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), ParamName);
		
		FNiagaraTypeDefinition TypeDef = Var.GetType();
		ParamObj->SetStringField(TEXT("type"), TypeDef.GetName());

		// Extract default value based on type
		if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
		{
			float Value = UserParamStore.GetParameterValue<float>(Var);
			ParamObj->SetNumberField(TEXT("default_value"), Value);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 Value = UserParamStore.GetParameterValue<int32>(Var);
			ParamObj->SetNumberField(TEXT("default_value"), Value);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
		{
			// Niagara stores bool as FNiagaraBool (int32 internally)
			FNiagaraBool Value = UserParamStore.GetParameterValue<FNiagaraBool>(Var);
			ParamObj->SetBoolField(TEXT("default_value"), Value.GetValue());
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def() || TypeDef == FNiagaraTypeDefinition::GetPositionDef())
		{
			FVector3f Value = UserParamStore.GetParameterValue<FVector3f>(Var);
			TArray<TSharedPtr<FJsonValue>> Vec;
			Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
			Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
			Vec.Add(MakeShared<FJsonValueNumber>(Value.Z));
			ParamObj->SetArrayField(TEXT("default_value"), Vec);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
		{
			FVector2f Value = UserParamStore.GetParameterValue<FVector2f>(Var);
			TArray<TSharedPtr<FJsonValue>> Vec;
			Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
			Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
			ParamObj->SetArrayField(TEXT("default_value"), Vec);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec4Def())
		{
			FVector4f Value = UserParamStore.GetParameterValue<FVector4f>(Var);
			TArray<TSharedPtr<FJsonValue>> Vec;
			Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
			Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
			Vec.Add(MakeShared<FJsonValueNumber>(Value.Z));
			Vec.Add(MakeShared<FJsonValueNumber>(Value.W));
			ParamObj->SetArrayField(TEXT("default_value"), Vec);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
		{
			FLinearColor Value = UserParamStore.GetParameterValue<FLinearColor>(Var);
			TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
			ColorObj->SetNumberField(TEXT("r"), Value.R);
			ColorObj->SetNumberField(TEXT("g"), Value.G);
			ColorObj->SetNumberField(TEXT("b"), Value.B);
			ColorObj->SetNumberField(TEXT("a"), Value.A);
			ParamObj->SetObjectField(TEXT("default_value"), ColorObj);
		}

		ParamArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	ResultJson->SetNumberField(TEXT("user_parameter_count"), ParamArray.Num());
	ResultJson->SetArrayField(TEXT("user_parameters"), ParamArray);

	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Find a Niagara component on an actor by name
// ─────────────────────────────────────────────────────────────────────────────

static UNiagaraComponent* FindNiagaraComponentOnActor(const FString& ActorName)
{
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);

	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName() == ActorName)
		{
			// First check if it's a NiagaraActor
			ANiagaraActor* NiagaraActor = Cast<ANiagaraActor>(Actor);
			if (NiagaraActor)
			{
				return NiagaraActor->GetNiagaraComponent();
			}

			// Otherwise look for a NiagaraComponent on the actor
			return Actor->FindComponentByClass<UNiagaraComponent>();
		}
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_niagara_parameter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetNiagaraParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
	}

	FString ParameterName;
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parameter_name' parameter"));
	}

	FString ParameterType;
	if (!Params->TryGetStringField(TEXT("parameter_type"), ParameterType))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parameter_type' parameter. Valid types: float, int, bool, vec2, vec3, vec4, color"));
	}

	if (!Params->HasField(TEXT("value")))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	UNiagaraComponent* NiagaraComp = FindNiagaraComponentOnActor(ActorName);
	if (!NiagaraComp)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No Niagara component found on actor: %s"), *ActorName));
	}

	FName ParamFName(*ParameterName);

	if (ParameterType == TEXT("float"))
	{
		float Value = (float)Params->GetNumberField(TEXT("value"));
		NiagaraComp->SetVariableFloat(ParamFName, Value);
	}
	else if (ParameterType == TEXT("int"))
	{
		int32 Value = (int32)Params->GetNumberField(TEXT("value"));
		NiagaraComp->SetVariableInt(ParamFName, Value);
	}
	else if (ParameterType == TEXT("bool"))
	{
		bool Value = Params->GetBoolField(TEXT("value"));
		NiagaraComp->SetVariableBool(ParamFName, Value);
	}
	else if (ParameterType == TEXT("vec2"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Params->TryGetArrayField(TEXT("value"), Arr) || Arr->Num() < 2)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("vec2 value must be an array of 2 numbers"));
		}
		FVector2D Value((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber());
		NiagaraComp->SetVariableVec2(ParamFName, Value);
	}
	else if (ParameterType == TEXT("vec3"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Params->TryGetArrayField(TEXT("value"), Arr) || Arr->Num() < 3)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("vec3 value must be an array of 3 numbers"));
		}
		FVector Value((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		NiagaraComp->SetVariableVec3(ParamFName, Value);
	}
	else if (ParameterType == TEXT("vec4"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Params->TryGetArrayField(TEXT("value"), Arr) || Arr->Num() < 4)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("vec4 value must be an array of 4 numbers"));
		}
		FVector4 Value((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber(), (*Arr)[3]->AsNumber());
		NiagaraComp->SetVariableVec4(ParamFName, Value);
	}
	else if (ParameterType == TEXT("color"))
	{
		// Accept either {r,g,b,a} object or [r,g,b,a] array
		FLinearColor Color(1.f, 1.f, 1.f, 1.f);
		const TSharedPtr<FJsonObject>* ColorObj;
		const TArray<TSharedPtr<FJsonValue>>* ColorArr;

		if (Params->TryGetObjectField(TEXT("value"), ColorObj))
		{
			Color.R = (float)(*ColorObj)->GetNumberField(TEXT("r"));
			Color.G = (float)(*ColorObj)->GetNumberField(TEXT("g"));
			Color.B = (float)(*ColorObj)->GetNumberField(TEXT("b"));
			if ((*ColorObj)->HasField(TEXT("a")))
			{
				Color.A = (float)(*ColorObj)->GetNumberField(TEXT("a"));
			}
		}
		else if (Params->TryGetArrayField(TEXT("value"), ColorArr) && ColorArr->Num() >= 3)
		{
			Color.R = (float)(*ColorArr)[0]->AsNumber();
			Color.G = (float)(*ColorArr)[1]->AsNumber();
			Color.B = (float)(*ColorArr)[2]->AsNumber();
			if (ColorArr->Num() >= 4)
			{
				Color.A = (float)(*ColorArr)[3]->AsNumber();
			}
		}
		else
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("color value must be {r,g,b,a} object or [r,g,b,a] array"));
		}

		NiagaraComp->SetVariableLinearColor(ParamFName, Color);
	}
	else
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported parameter type: %s. Valid types: float, int, bool, vec2, vec3, vec4, color"), *ParameterType));
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Parameter '%s' set successfully on actor '%s'"), *ParameterName, *ActorName));
	ResultJson->SetStringField(TEXT("parameter_name"), ParameterName);
	ResultJson->SetStringField(TEXT("parameter_type"), ParameterType);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// get_niagara_parameters
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleGetNiagaraParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
	}

	UNiagaraComponent* NiagaraComp = FindNiagaraComponentOnActor(ActorName);
	if (!NiagaraComp)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No Niagara component found on actor: %s"), *ActorName));
	}

	UNiagaraSystem* System = NiagaraComp->GetAsset();
	if (!System)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Niagara component on '%s' has no system asset assigned"), *ActorName));
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("actor_name"), ActorName);
	ResultJson->SetStringField(TEXT("system_name"), System->GetName());
	ResultJson->SetStringField(TEXT("system_path"), System->GetPathName());
	ResultJson->SetBoolField(TEXT("is_active"), NiagaraComp->IsActive());

	// Read User.* parameters from the component's override store
	const FNiagaraUserRedirectionParameterStore& UserParamStore = System->GetExposedParameters();
	TArray<FNiagaraVariable> UserParams;
	UserParamStore.GetParameters(UserParams);

	TArray<TSharedPtr<FJsonValue>> ParamArray;

	for (const FNiagaraVariable& Var : UserParams)
	{
		FString ParamName = Var.GetName().ToString();
		FNiagaraTypeDefinition TypeDef = Var.GetType();

		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), ParamName);
		ParamObj->SetStringField(TEXT("type"), TypeDef.GetName());

		// Try reading current override value from the component
		bool bIsValid = false;

		if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
		{
			float Value = NiagaraComp->GetVariableFloat(FName(*ParamName), bIsValid);
			if (bIsValid)
			{
				ParamObj->SetNumberField(TEXT("value"), Value);
			}
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 Value = NiagaraComp->GetVariableInt(FName(*ParamName), bIsValid);
			if (bIsValid)
			{
				ParamObj->SetNumberField(TEXT("value"), Value);
			}
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
		{
			bool Value = NiagaraComp->GetVariableBool(FName(*ParamName), bIsValid);
			if (bIsValid)
			{
				ParamObj->SetBoolField(TEXT("value"), Value);
			}
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def() || TypeDef == FNiagaraTypeDefinition::GetPositionDef())
		{
			FVector Value = NiagaraComp->GetVariableVec3(FName(*ParamName), bIsValid);
			if (bIsValid)
			{
				TArray<TSharedPtr<FJsonValue>> Vec;
				Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Z));
				ParamObj->SetArrayField(TEXT("value"), Vec);
			}
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
		{
			FVector2D Value = NiagaraComp->GetVariableVec2(FName(*ParamName), bIsValid);
			if (bIsValid)
			{
				TArray<TSharedPtr<FJsonValue>> Vec;
				Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
				ParamObj->SetArrayField(TEXT("value"), Vec);
			}
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec4Def())
		{
			FVector4 Value = NiagaraComp->GetVariableVec4(FName(*ParamName), bIsValid);
			if (bIsValid)
			{
				TArray<TSharedPtr<FJsonValue>> Vec;
				Vec.Add(MakeShared<FJsonValueNumber>(Value.X));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Y));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.Z));
				Vec.Add(MakeShared<FJsonValueNumber>(Value.W));
				ParamObj->SetArrayField(TEXT("value"), Vec);
			}
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
		{
			// UNiagaraComponent does not have GetVariableLinearColor;
			// read from the override parameter store directly.
			const FNiagaraParameterStore& OverrideParams = NiagaraComp->GetOverrideParameters();
			const FNiagaraVariable OverrideVar(TypeDef, FName(*ParamName));
			const uint8* ParamData = OverrideParams.GetParameterData(OverrideVar);
			if (ParamData)
			{
				FLinearColor Value;
				FMemory::Memcpy(&Value, ParamData, sizeof(FLinearColor));
				bIsValid = true;
				TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
				ColorObj->SetNumberField(TEXT("r"), Value.R);
				ColorObj->SetNumberField(TEXT("g"), Value.G);
				ColorObj->SetNumberField(TEXT("b"), Value.B);
				ColorObj->SetNumberField(TEXT("a"), Value.A);
				ParamObj->SetObjectField(TEXT("value"), ColorObj);
			}
		}

		if (!bIsValid)
		{
			ParamObj->SetStringField(TEXT("value"), TEXT("(no override)"));
		}

		ParamArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	ResultJson->SetNumberField(TEXT("parameter_count"), ParamArray.Num());
	ResultJson->SetArrayField(TEXT("parameters"), ParamArray);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// create_niagara_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleCreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter (e.g. '/Game/VFX/NS_Explosion')"));
	}

	FString TemplateSystemPath;
	bool bHasTemplate = Params->TryGetStringField(TEXT("template_system_path"), TemplateSystemPath);

	if (bHasTemplate && !TemplateSystemPath.IsEmpty())
	{
		// Duplicate mode: copy an existing system
		UObject* SourceObj = UEditorAssetLibrary::LoadAsset(TemplateSystemPath);
		UNiagaraSystem* SourceSystem = Cast<UNiagaraSystem>(SourceObj);
		if (!SourceSystem)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Template Niagara system not found: %s"), *TemplateSystemPath));
		}

		// Parse the destination into package path and asset name
		FString PackagePath;
		FString AssetName;
		int32 LastSlash;
		if (AssetPath.FindLastChar('/', LastSlash))
		{
			PackagePath = AssetPath.Left(LastSlash);
			AssetName = AssetPath.RightChop(LastSlash + 1);
		}
		else
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("asset_path must contain a directory, e.g. '/Game/VFX/NS_Explosion'"));
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UObject* DuplicatedObj = AssetTools.DuplicateAsset(AssetName, PackagePath, SourceSystem);

		if (!DuplicatedObj)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to duplicate Niagara system"));
		}

		UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(DuplicatedObj);
		TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("message"), TEXT("Niagara system created by duplicating template"));
		ResultJson->SetStringField(TEXT("name"), NewSystem->GetName());
		ResultJson->SetStringField(TEXT("path"), NewSystem->GetPathName());
		ResultJson->SetStringField(TEXT("template_path"), TemplateSystemPath);
		ResultJson->SetNumberField(TEXT("emitter_count"), NewSystem->GetEmitterHandles().Num());
		return ResultJson;
	}
	else
	{
		// Create empty system using the factory
		FString PackagePath;
		FString AssetName;
		int32 LastSlash;
		if (AssetPath.FindLastChar('/', LastSlash))
		{
			PackagePath = AssetPath.Left(LastSlash);
			AssetName = AssetPath.RightChop(LastSlash + 1);
		}
		else
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("asset_path must contain a directory, e.g. '/Game/VFX/NS_Explosion'"));
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		// Find the NiagaraSystemFactory
		UFactory* Factory = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class->IsChildOf(UFactory::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
			{
				UFactory* TestFactory = Class->GetDefaultObject<UFactory>();
				if (TestFactory && TestFactory->SupportedClass == UNiagaraSystem::StaticClass())
				{
					Factory = NewObject<UFactory>(GetTransientPackage(), Class);
					break;
				}
			}
		}

		if (!Factory)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Could not find NiagaraSystem factory. Make sure Niagara plugin is enabled."));
		}

		UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UNiagaraSystem::StaticClass(), Factory);
		if (!NewAsset)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create empty Niagara system"));
		}

		UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(NewAsset);
		TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("message"), TEXT("Empty Niagara system created"));
		ResultJson->SetStringField(TEXT("name"), NewSystem->GetName());
		ResultJson->SetStringField(TEXT("path"), NewSystem->GetPathName());
		ResultJson->SetNumberField(TEXT("emitter_count"), NewSystem->GetEmitterHandles().Num());
		return ResultJson;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// set_niagara_rapid_parameter
// ─────────────────────────────────────────────────────────────────────────────

// Helper: Load a UNiagaraSystem by name or path (shared by multiple handlers)
static UNiagaraSystem* LoadNiagaraSystemByNameOrPath(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	if (Params->HasField(TEXT("path")))
	{
		FString AssetPath = Params->GetStringField(TEXT("path"));
		UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(AssetPath);
		UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadedObj);
		if (!System) OutError = FString::Printf(TEXT("Niagara system not found at path: %s"), *AssetPath);
		return System;
	}
	else if (Params->HasField(TEXT("system_name")))
	{
		FString Name = Params->GetStringField(TEXT("system_name"));
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			if (AssetData.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				UNiagaraSystem* System = Cast<UNiagaraSystem>(AssetData.GetAsset());
				if (System) return System;
			}
		}
		OutError = FString::Printf(TEXT("Niagara system not found: %s"), *Name);
		return nullptr;
	}
	OutError = TEXT("Missing required parameter: 'system_name' or 'path'");
	return nullptr;
}

// Helper: auto-save a Niagara system asset after modification
static bool SaveNiagaraSystemAsset(UNiagaraSystem* System)
{
	if (!System) return false;
	UPackage* Package = System->GetOutermost();
	if (!Package) return false;

	FString PackageFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		return false;

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	return UPackage::SavePackage(Package, System, *PackageFilename, SaveArgs);
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetNiagaraRapidParameter(const TSharedPtr<FJsonObject>& Params)
{
	// Validate required parameters
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));
	if (!Params->HasField(TEXT("parameter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'parameter_name'"));
	if (!Params->HasField(TEXT("value")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'value'"));

	// Load the system
	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));
	FString ParameterName = Params->GetStringField(TEXT("parameter_name"));
	FString ScriptType = TEXT("spawn");
	if (Params->HasField(TEXT("script_type")))
		ScriptType = Params->GetStringField(TEXT("script_type")).ToLower();

	// Find the emitter handle
	TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			TargetHandle = &Handle;
			break;
		}
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	// Get the appropriate script
	UNiagaraScript* Script = nullptr;
	if (ScriptType == TEXT("spawn"))
		Script = EmitterData->SpawnScriptProps.Script;
	else if (ScriptType == TEXT("update"))
		Script = EmitterData->UpdateScriptProps.Script;
	else
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid script_type '%s'. Must be 'spawn' or 'update'."), *ScriptType));

	if (!Script)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No %s script found on emitter '%s'"), *ScriptType, *EmitterName));

	// Find the parameter in the store
	FNiagaraParameterStore& RapidParams = Script->RapidIterationParameters;
	TArray<FNiagaraVariable> AllParams;
	RapidParams.GetParameters(AllParams);

	FNiagaraVariable* TargetVar = nullptr;
	for (FNiagaraVariable& Var : AllParams)
	{
		if (Var.GetName().ToString().Equals(ParameterName, ESearchCase::IgnoreCase))
		{
			TargetVar = &Var;
			break;
		}
	}

	// Also try partial match: user may pass "InitializeParticle.Lifetime" instead of full "Constants.Smoke.InitializeParticle.Lifetime"
	if (!TargetVar)
	{
		for (FNiagaraVariable& Var : AllParams)
		{
			if (Var.GetName().ToString().Contains(ParameterName))
			{
				TargetVar = &Var;
				break;
			}
		}
	}

	if (!TargetVar)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parameter '%s' not found in %s script rapid iteration parameters of emitter '%s'"), *ParameterName, *ScriptType, *EmitterName));

	// Set the value based on type
	FNiagaraTypeDefinition TypeDef = TargetVar->GetType();
	FString TypeName = TypeDef.GetName();
	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("parameter"), TargetVar->GetName().ToString());
	ResultJson->SetStringField(TEXT("type"), TypeName);

	if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
	{
		float OldValue = RapidParams.GetParameterValue<float>(*TargetVar);
		float NewValue = static_cast<float>(Params->GetNumberField(TEXT("value")));
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		ResultJson->SetNumberField(TEXT("old_value"), OldValue);
		ResultJson->SetNumberField(TEXT("new_value"), NewValue);
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
	{
		int32 OldValue = RapidParams.GetParameterValue<int32>(*TargetVar);
		int32 NewValue = static_cast<int32>(Params->GetNumberField(TEXT("value")));
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		ResultJson->SetNumberField(TEXT("old_value"), OldValue);
		ResultJson->SetNumberField(TEXT("new_value"), NewValue);
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
	{
		FNiagaraBool OldValue = RapidParams.GetParameterValue<FNiagaraBool>(*TargetVar);
		bool bNewValue = Params->GetBoolField(TEXT("value"));
		FNiagaraBool NewValue;
		NewValue.SetValue(bNewValue);
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		ResultJson->SetBoolField(TEXT("old_value"), OldValue.GetValue());
		ResultJson->SetBoolField(TEXT("new_value"), bNewValue);
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
	{
		FVector2f OldValue = RapidParams.GetParameterValue<FVector2f>(*TargetVar);
		const TArray<TSharedPtr<FJsonValue>>& Arr = Params->GetArrayField(TEXT("value"));
		if (Arr.Num() < 2) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("vec2 value requires array of 2 numbers"));
		FVector2f NewValue(Arr[0]->AsNumber(), Arr[1]->AsNumber());
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		TArray<TSharedPtr<FJsonValue>> OldArr, NewArr;
		OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.X)); OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.Y));
		NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.X)); NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.Y));
		ResultJson->SetArrayField(TEXT("old_value"), OldArr);
		ResultJson->SetArrayField(TEXT("new_value"), NewArr);
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def() || TypeDef == FNiagaraTypeDefinition::GetPositionDef())
	{
		FVector3f OldValue = RapidParams.GetParameterValue<FVector3f>(*TargetVar);
		const TArray<TSharedPtr<FJsonValue>>& Arr = Params->GetArrayField(TEXT("value"));
		if (Arr.Num() < 3) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("vec3 value requires array of 3 numbers"));
		FVector3f NewValue(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		TArray<TSharedPtr<FJsonValue>> OldArr, NewArr;
		OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.X)); OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.Y)); OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.Z));
		NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.X)); NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.Y)); NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.Z));
		ResultJson->SetArrayField(TEXT("old_value"), OldArr);
		ResultJson->SetArrayField(TEXT("new_value"), NewArr);
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetVec4Def())
	{
		FVector4f OldValue = RapidParams.GetParameterValue<FVector4f>(*TargetVar);
		const TArray<TSharedPtr<FJsonValue>>& Arr = Params->GetArrayField(TEXT("value"));
		if (Arr.Num() < 4) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("vec4 value requires array of 4 numbers"));
		FVector4f NewValue(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber(), Arr[3]->AsNumber());
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		TArray<TSharedPtr<FJsonValue>> OldArr, NewArr;
		OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.X)); OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.Y)); OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.Z)); OldArr.Add(MakeShared<FJsonValueNumber>(OldValue.W));
		NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.X)); NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.Y)); NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.Z)); NewArr.Add(MakeShared<FJsonValueNumber>(NewValue.W));
		ResultJson->SetArrayField(TEXT("old_value"), OldArr);
		ResultJson->SetArrayField(TEXT("new_value"), NewArr);
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
	{
		FLinearColor OldValue = RapidParams.GetParameterValue<FLinearColor>(*TargetVar);
		const TSharedPtr<FJsonObject>& ColorObj = Params->GetObjectField(TEXT("value"));
		FLinearColor NewValue(
			ColorObj->GetNumberField(TEXT("r")),
			ColorObj->GetNumberField(TEXT("g")),
			ColorObj->GetNumberField(TEXT("b")),
			ColorObj->HasField(TEXT("a")) ? ColorObj->GetNumberField(TEXT("a")) : 1.0f
		);
		RapidParams.SetParameterValue(NewValue, *TargetVar);
		TSharedPtr<FJsonObject> OldObj = MakeShared<FJsonObject>();
		OldObj->SetNumberField(TEXT("r"), OldValue.R); OldObj->SetNumberField(TEXT("g"), OldValue.G);
		OldObj->SetNumberField(TEXT("b"), OldValue.B); OldObj->SetNumberField(TEXT("a"), OldValue.A);
		TSharedPtr<FJsonObject> NewObj = MakeShared<FJsonObject>();
		NewObj->SetNumberField(TEXT("r"), NewValue.R); NewObj->SetNumberField(TEXT("g"), NewValue.G);
		NewObj->SetNumberField(TEXT("b"), NewValue.B); NewObj->SetNumberField(TEXT("a"), NewValue.A);
		ResultJson->SetObjectField(TEXT("old_value"), OldObj);
		ResultJson->SetObjectField(TEXT("new_value"), NewObj);
	}
	else
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported parameter type: %s"), *TypeName));
	}

	// Recompile the system so changes take effect
	System->RequestCompile(true);
	System->WaitForCompilationComplete();

	// Save the asset
	System->MarkPackageDirty();
	SaveNiagaraSystemAsset(System);

	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);

	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// modify_emitter_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleModifyEmitterProperties(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));

	// Find emitter handle
	TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			TargetHandle = &Handle;
			break;
		}
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> ChangesObj = MakeShared<FJsonObject>();
	int32 ChangeCount = 0;

	// enabled (on the handle, not emitter data)
	if (Params->HasField(TEXT("enabled")))
	{
		bool bOld = TargetHandle->GetIsEnabled();
		bool bNew = Params->GetBoolField(TEXT("enabled"));
		TargetHandle->SetIsEnabled(bNew, *System, false);
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetBoolField(TEXT("old"), bOld);
		Change->SetBoolField(TEXT("new"), bNew);
		ChangesObj->SetObjectField(TEXT("enabled"), Change);
		ChangeCount++;
	}

	// local_space
	if (Params->HasField(TEXT("local_space")))
	{
		bool bOld = EmitterData->bLocalSpace;
		bool bNew = Params->GetBoolField(TEXT("local_space"));
		EmitterData->bLocalSpace = bNew;
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetBoolField(TEXT("old"), bOld);
		Change->SetBoolField(TEXT("new"), bNew);
		ChangesObj->SetObjectField(TEXT("local_space"), Change);
		ChangeCount++;
	}

	// determinism
	if (Params->HasField(TEXT("determinism")))
	{
		bool bOld = EmitterData->bDeterminism;
		bool bNew = Params->GetBoolField(TEXT("determinism"));
		EmitterData->bDeterminism = bNew;
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetBoolField(TEXT("old"), bOld);
		Change->SetBoolField(TEXT("new"), bNew);
		ChangesObj->SetObjectField(TEXT("determinism"), Change);
		ChangeCount++;
	}

	// random_seed
	if (Params->HasField(TEXT("random_seed")))
	{
		int32 Old = EmitterData->RandomSeed;
		int32 New = static_cast<int32>(Params->GetNumberField(TEXT("random_seed")));
		EmitterData->RandomSeed = New;
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetNumberField(TEXT("old"), Old);
		Change->SetNumberField(TEXT("new"), New);
		ChangesObj->SetObjectField(TEXT("random_seed"), Change);
		ChangeCount++;
	}

	// sim_target
	if (Params->HasField(TEXT("sim_target")))
	{
		FString OldStr = EmitterData->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU");
		FString NewStr = Params->GetStringField(TEXT("sim_target")).ToUpper();
		if (NewStr == TEXT("CPU"))
			EmitterData->SimTarget = ENiagaraSimTarget::CPUSim;
		else if (NewStr == TEXT("GPU"))
			EmitterData->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		else
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid sim_target '%s'. Must be 'CPU' or 'GPU'."), *NewStr));

		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("old"), OldStr);
		Change->SetStringField(TEXT("new"), NewStr);
		ChangesObj->SetObjectField(TEXT("sim_target"), Change);
		ChangeCount++;
	}

	if (ChangeCount == 0)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No properties specified. Supported: enabled, local_space, determinism, random_seed, sim_target"));

	// Recompile and save
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	SaveNiagaraSystemAsset(System);

	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetNumberField(TEXT("changes_count"), ChangeCount);
	ResultJson->SetObjectField(TEXT("changes"), ChangesObj);

	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// list_niagara_emitter_templates
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleListNiagaraEmitterTemplates(const TSharedPtr<FJsonObject>& Params)
{
	// Template emitters live under /Niagara/DefaultAssets/Templates/ in engine content
	// Categories: Emitters, BehaviorExamples, CascadeConversion, Systems
	FString CategoryFilter;
	if (Params.IsValid() && Params->HasField(TEXT("category")))
		CategoryFilter = Params->GetStringField(TEXT("category"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UNiagaraEmitter::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/Niagara/DefaultAssets/Templates")));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	TArray<TSharedPtr<FJsonValue>> TemplateArray;
	for (const FAssetData& Asset : AssetList)
	{
		FString PackagePath = Asset.PackageName.ToString();

		// Determine category from path
		FString Category = TEXT("Unknown");
		if (PackagePath.Contains(TEXT("/Templates/Emitters/")))
			Category = TEXT("Emitters");
		else if (PackagePath.Contains(TEXT("/Templates/BehaviorExamples/")))
			Category = TEXT("BehaviorExamples");
		else if (PackagePath.Contains(TEXT("/Templates/CascadeConversion/")))
			Category = TEXT("CascadeConversion");
		else if (PackagePath.Contains(TEXT("/Templates/Systems/")))
			Category = TEXT("Systems");

		if (!CategoryFilter.IsEmpty() && !Category.Equals(CategoryFilter, ESearchCase::IgnoreCase))
			continue;

		TSharedPtr<FJsonObject> TemplateObj = MakeShared<FJsonObject>();
		TemplateObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		TemplateObj->SetStringField(TEXT("category"), Category);
		TemplateObj->SetStringField(TEXT("path"), PackagePath);
		TemplateArray.Add(MakeShared<FJsonValueObject>(TemplateObj));
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetNumberField(TEXT("count"), TemplateArray.Num());
	ResultJson->SetArrayField(TEXT("templates"), TemplateArray);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_emitter_to_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleAddEmitterToSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString LoadError;
	UNiagaraSystem* TargetSystem = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!TargetSystem)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	bool bHasTemplate = Params->HasField(TEXT("template_name"));
	bool bHasSourceEmitter = Params->HasField(TEXT("source_emitter_name"));
	if (!bHasTemplate && !bHasSourceEmitter)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Must specify either 'template_name' or 'source_emitter_name'"));

	FString NewEmitterName;
	if (Params->HasField(TEXT("new_emitter_name")))
		NewEmitterName = Params->GetStringField(TEXT("new_emitter_name"));

	// ── Path 1: Add from engine template ──────────────────────────────────
	if (bHasTemplate)
	{
		FString TemplateName = Params->GetStringField(TEXT("template_name"));
		if (NewEmitterName.IsEmpty())
			NewEmitterName = TemplateName;

		// Search for the template emitter asset under /Niagara/DefaultAssets/Templates/
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(UNiagaraEmitter::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(TEXT("/Niagara/DefaultAssets/Templates")));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssets(Filter, AssetList);

		UNiagaraEmitter* TemplateEmitter = nullptr;
		for (const FAssetData& Asset : AssetList)
		{
			if (Asset.AssetName.ToString().Equals(TemplateName, ESearchCase::IgnoreCase))
			{
				TemplateEmitter = Cast<UNiagaraEmitter>(Asset.GetAsset());
				break;
			}
		}
		if (!TemplateEmitter)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Template emitter '%s' not found. Use list_niagara_emitter_templates to see available templates."), *TemplateName));

		FNiagaraEmitterHandle NewHandle = TargetSystem->AddEmitterHandle(*TemplateEmitter, FName(*NewEmitterName), TemplateEmitter->GetExposedVersion().VersionGuid);

		TargetSystem->RequestCompile(true);
		TargetSystem->WaitForCompilationComplete();
		TargetSystem->MarkPackageDirty();
		SaveNiagaraSystemAsset(TargetSystem);

		TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("status"), TEXT("success"));
		ResultJson->SetStringField(TEXT("message"), TEXT("Emitter added from engine template"));
		ResultJson->SetStringField(TEXT("system"), TargetSystem->GetName());
		ResultJson->SetStringField(TEXT("template"), TemplateName);
		ResultJson->SetStringField(TEXT("new_emitter"), NewHandle.GetName().ToString());
		ResultJson->SetNumberField(TEXT("emitter_count"), TargetSystem->GetEmitterHandles().Num());
		return ResultJson;
	}

	// ── Path 2: Copy from another system or duplicate within same system ──
	FString SourceEmitterName = Params->GetStringField(TEXT("source_emitter_name"));
	if (NewEmitterName.IsEmpty())
		NewEmitterName = SourceEmitterName;

	// Determine source: same system (duplicate) or different system (copy)
	UNiagaraSystem* SourceSystem = TargetSystem;
	if (Params->HasField(TEXT("source_system_name")) || Params->HasField(TEXT("source_system_path")))
	{
		// Build a temp params object for loading the source system
		TSharedPtr<FJsonObject> SourceParams = MakeShared<FJsonObject>();
		if (Params->HasField(TEXT("source_system_path")))
			SourceParams->SetStringField(TEXT("path"), Params->GetStringField(TEXT("source_system_path")));
		else
			SourceParams->SetStringField(TEXT("system_name"), Params->GetStringField(TEXT("source_system_name")));

		FString SourceError;
		SourceSystem = LoadNiagaraSystemByNameOrPath(SourceParams, SourceError);
		if (!SourceSystem)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source system error: %s"), *SourceError));
	}

	// Find source emitter handle
	const TArray<FNiagaraEmitterHandle>& SourceHandles = SourceSystem->GetEmitterHandles();
	const FNiagaraEmitterHandle* SourceHandle = nullptr;
	for (const FNiagaraEmitterHandle& Handle : SourceHandles)
	{
		if (Handle.GetName().ToString().Equals(SourceEmitterName, ESearchCase::IgnoreCase))
		{
			SourceHandle = &Handle;
			break;
		}
	}
	if (!SourceHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in source system '%s'"), *SourceEmitterName, *SourceSystem->GetName()));

	// If duplicating within same system, use DuplicateEmitterHandle
	if (SourceSystem == TargetSystem)
	{
		FNiagaraEmitterHandle NewHandle = TargetSystem->DuplicateEmitterHandle(*SourceHandle, FName(*NewEmitterName));

		TargetSystem->RequestCompile(true);
		TargetSystem->WaitForCompilationComplete();
		TargetSystem->MarkPackageDirty();
		SaveNiagaraSystemAsset(TargetSystem);

		TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("status"), TEXT("success"));
		ResultJson->SetStringField(TEXT("message"), TEXT("Emitter duplicated within system"));
		ResultJson->SetStringField(TEXT("system"), TargetSystem->GetName());
		ResultJson->SetStringField(TEXT("new_emitter"), NewHandle.GetName().ToString());
		ResultJson->SetNumberField(TEXT("emitter_count"), TargetSystem->GetEmitterHandles().Num());
		return ResultJson;
	}

	// Copy from different system using AddEmitterHandle
	FVersionedNiagaraEmitter SourceInstance = SourceHandle->GetInstance();
	if (!SourceInstance.Emitter)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Source emitter instance is null"));

	FNiagaraEmitterHandle NewHandle = TargetSystem->AddEmitterHandle(*SourceInstance.Emitter, FName(*NewEmitterName), SourceInstance.Version);

	TargetSystem->RequestCompile(true);
	TargetSystem->WaitForCompilationComplete();
	TargetSystem->MarkPackageDirty();
	SaveNiagaraSystemAsset(TargetSystem);

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("message"), TEXT("Emitter copied from source system"));
	ResultJson->SetStringField(TEXT("system"), TargetSystem->GetName());
	ResultJson->SetStringField(TEXT("source_system"), SourceSystem->GetName());
	ResultJson->SetStringField(TEXT("source_emitter"), SourceEmitterName);
	ResultJson->SetStringField(TEXT("new_emitter"), NewHandle.GetName().ToString());
	ResultJson->SetNumberField(TEXT("emitter_count"), TargetSystem->GetEmitterHandles().Num());
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_emitter_from_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleRemoveEmitterFromSystem(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));

	// Find emitter handle
	TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	const FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			TargetHandle = &Handle;
			break;
		}
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	int32 OldCount = EmitterHandles.Num();

	System->RemoveEmitterHandle(*TargetHandle);

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	SaveNiagaraSystemAsset(System);

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Emitter '%s' removed from system"), *EmitterName));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("removed_emitter"), EmitterName);
	ResultJson->SetNumberField(TEXT("old_emitter_count"), OldCount);
	ResultJson->SetNumberField(TEXT("new_emitter_count"), System->GetEmitterHandles().Num());
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_module_to_emitter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleAddModuleToEmitter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));
	if (!Params->HasField(TEXT("module_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'module_name'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));
	FString ModuleName = Params->GetStringField(TEXT("module_name"));
	FString ScriptType = TEXT("update");
	if (Params->HasField(TEXT("script_type")))
		ScriptType = Params->GetStringField(TEXT("script_type")).ToLower();

	int32 TargetIndex = INDEX_NONE;
	if (Params->HasField(TEXT("index")))
		TargetIndex = static_cast<int32>(Params->GetNumberField(TEXT("index")));

	// Determine script usage
	ENiagaraScriptUsage Usage;
	if (ScriptType == TEXT("spawn"))
		Usage = ENiagaraScriptUsage::ParticleSpawnScript;
	else if (ScriptType == TEXT("update"))
		Usage = ENiagaraScriptUsage::ParticleUpdateScript;
	else
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported script_type: '%s'. Use 'spawn' or 'update'."), *ScriptType));

	// Find the emitter
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	const FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			TargetHandle = &Handle;
			break;
		}
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	FVersionedNiagaraEmitter VersionedEmitter = TargetHandle->GetInstance();
	UNiagaraEmitter* Emitter = VersionedEmitter.Emitter;
	if (!Emitter)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Emitter instance is null"));

	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	// Get the script for the target stage
	UNiagaraScript* TargetScript = nullptr;
	if (Usage == ENiagaraScriptUsage::ParticleSpawnScript)
		TargetScript = EmitterData->SpawnScriptProps.Script;
	else
		TargetScript = EmitterData->UpdateScriptProps.Script;

	if (!TargetScript)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No %s script found on emitter '%s'"), *ScriptType, *EmitterName));

	// Get the graph and find the output node
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(TargetScript->GetLatestSource());
	if (!Source || !Source->NodeGraph)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get script graph"));

	UNiagaraNodeOutput* OutputNode = Source->NodeGraph->FindEquivalentOutputNode(Usage);
	if (!OutputNode)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No output node found for %s script"), *ScriptType));

	// Find the module script asset by name
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UNiagaraScript::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	UNiagaraScript* ModuleScript = nullptr;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (AssetData.AssetName.ToString().Equals(ModuleName, ESearchCase::IgnoreCase))
		{
			UNiagaraScript* CandidateScript = Cast<UNiagaraScript>(AssetData.GetAsset());
			if (CandidateScript && CandidateScript->GetUsage() == ENiagaraScriptUsage::Module)
			{
				ModuleScript = CandidateScript;
				break;
			}
		}
	}
	if (!ModuleScript)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Module script '%s' not found. Check available modules in the engine Niagara content."), *ModuleName));

	// Add the module to the stack
	UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScript, *OutputNode, TargetIndex);
	if (!NewNode)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AddScriptModuleToStack failed"));

	// Recompile and save
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	SaveNiagaraSystemAsset(System);

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Module '%s' added to %s script of emitter '%s'"), *ModuleName, *ScriptType, *EmitterName));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("module"), NewNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	if (TargetIndex != INDEX_NONE)
		ResultJson->SetNumberField(TEXT("index"), TargetIndex);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_module_from_emitter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleRemoveModuleFromEmitter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));
	if (!Params->HasField(TEXT("module_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'module_name'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));
	FString ModuleName = Params->GetStringField(TEXT("module_name"));
	FString ScriptType = TEXT("update");
	if (Params->HasField(TEXT("script_type")))
		ScriptType = Params->GetStringField(TEXT("script_type")).ToLower();

	// Determine script usage
	ENiagaraScriptUsage Usage;
	if (ScriptType == TEXT("spawn"))
		Usage = ENiagaraScriptUsage::ParticleSpawnScript;
	else if (ScriptType == TEXT("update"))
		Usage = ENiagaraScriptUsage::ParticleUpdateScript;
	else
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported script_type: '%s'. Use 'spawn' or 'update'."), *ScriptType));

	// Find the emitter
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	const FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			TargetHandle = &Handle;
			break;
		}
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	FVersionedNiagaraEmitter VersionedEmitter = TargetHandle->GetInstance();
	UNiagaraEmitter* Emitter = VersionedEmitter.Emitter;
	if (!Emitter)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Emitter instance is null"));

	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	// Get the script for the target stage
	UNiagaraScript* TargetScript = nullptr;
	if (Usage == ENiagaraScriptUsage::ParticleSpawnScript)
		TargetScript = EmitterData->SpawnScriptProps.Script;
	else
		TargetScript = EmitterData->UpdateScriptProps.Script;

	if (!TargetScript)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No %s script found on emitter '%s'"), *ScriptType, *EmitterName));

	// Get the graph and find the module node
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(TargetScript->GetLatestSource());
	if (!Source || !Source->NodeGraph)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get script graph"));

	UNiagaraNodeFunctionCall* TargetModule = nullptr;
	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (!FuncNode) continue;

		FString NodeName = FuncNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		FString ScriptName = FuncNode->FunctionScript ? FuncNode->FunctionScript->GetName() : TEXT("");

		if (NodeName.Equals(ModuleName, ESearchCase::IgnoreCase) || ScriptName.Equals(ModuleName, ESearchCase::IgnoreCase))
		{
			TargetModule = FuncNode;
			break;
		}
	}
	if (!TargetModule)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Module '%s' not found in %s script of emitter '%s'"), *ModuleName, *ScriptType, *EmitterName));

	FString RemovedName = TargetModule->GetNodeTitle(ENodeTitleType::ListView).ToString();

	// Remove the module node from the graph (Source is already in scope)
	Source->NodeGraph->RemoveNode(TargetModule);

	// Recompile and save
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	SaveNiagaraSystemAsset(System);

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Module '%s' removed from %s script of emitter '%s'"), *RemovedName, *ScriptType, *EmitterName));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("removed_module"), RemovedName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	return ResultJson;
}
