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

// Editor includes
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Factories/Factory.h"

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

			// Scalability — max particles
			EmitterObj->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
			EmitterObj->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);
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
