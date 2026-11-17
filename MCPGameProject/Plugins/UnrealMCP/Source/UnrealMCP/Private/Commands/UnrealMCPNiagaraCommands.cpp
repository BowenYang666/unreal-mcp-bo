#include "Commands/UnrealMCPNiagaraCommands.h"
#include "EdGraph/EdGraphPin.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPNiagaraScalability.h"
#include "Commands/UnrealMCPNiagaraAuthoring.h"

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
#include "ViewModels/NiagaraSystemViewModel.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"
#include "NiagaraSystemEditorData.h"
#include "UObject/UObjectHash.h"
#include "ScopedTransaction.h"
#include "NiagaraOverviewNode.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraNode.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceParticleRead.h"
#include "NiagaraNodeInput.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Curves/RichCurve.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "Misc/SecureHash.h"
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeExit.h"

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

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetSource(FGuid()));
	if (!Source || !Source->NodeGraph) return ModuleArray;

	TArray<TObjectPtr<UEdGraphNode>>& AllNodes = Source->NodeGraph->Nodes;
	for (UEdGraphNode* Node : AllNodes)
	{
		UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (!FuncNode) continue;

		TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
		ModObj->SetStringField(TEXT("name"), FuncNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
		ModObj->SetBoolField(TEXT("enabled"), FuncNode->IsNodeEnabled());
		ModObj->SetStringField(TEXT("node_guid"), FuncNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		ModObj->SetStringField(TEXT("node_path"), FuncNode->GetPathName());
		ModObj->SetStringField(TEXT("graph_path"), Source->NodeGraph->GetPathName());
		ModObj->SetStringField(TEXT("graph_scope"), TEXT("Function-call nodes in the container script exposed/default source graph; not proof of output-stack execution order"));
		ModObj->SetStringField(TEXT("selected_version_guid"), FuncNode->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens));
		TArray<TSharedPtr<FJsonValue>> InputPins;
		for (const UEdGraphPin* Pin : FuncNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			auto Input = MakeShared<FJsonObject>();
			Input->SetStringField(TEXT("name"), Pin->PinName.ToString());
			Input->SetStringField(TEXT("pin_id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
			Input->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
			Input->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
			Input->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			Input->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
			if (Pin->DefaultObject) Input->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
			else Input->SetField(TEXT("default_object"), MakeShared<FJsonValueNull>());
			TArray<TSharedPtr<FJsonValue>> Links;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked) continue;
				auto Link = MakeShared<FJsonObject>();
				Link->SetStringField(TEXT("node_guid"), Linked->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
				Link->SetStringField(TEXT("node_path"), Linked->GetOwningNode()->GetPathName());
				Link->SetStringField(TEXT("pin_name"), Linked->PinName.ToString());
				Link->SetStringField(TEXT("pin_id"), Linked->PinId.ToString(EGuidFormats::DigitsWithHyphens));
				Links.Add(MakeShared<FJsonValueObject>(Link));
			}
			Input->SetArrayField(TEXT("links"), Links); InputPins.Add(MakeShared<FJsonValueObject>(Input));
		}
		ModObj->SetArrayField(TEXT("input_pins"), InputPins);
		ModObj->SetStringField(TEXT("input_semantics"), TEXT("Saved call pins/defaults/links, including static-typed pins; not evaluated bindings or a proof of module data flow. Existing list_module_static_switches provides switch inventory."));

		if (FuncNode->FunctionScript)
		{
			ModObj->SetStringField(TEXT("script_name"), FuncNode->FunctionScript->GetName());
			ModObj->SetStringField(TEXT("script_asset_path"), FuncNode->FunctionScript->GetPathName());
			ModObj->SetBoolField(TEXT("versioning_enabled"), FuncNode->FunctionScript->IsVersioningEnabled());
			ModObj->SetBoolField(TEXT("uses_exposed_default"), !FuncNode->SelectedScriptVersion.IsValid());
			const auto* Version = FuncNode->FunctionScript->GetScriptData(FuncNode->SelectedScriptVersion);
			ModObj->SetStringField(TEXT("version_status"), Version ? (FuncNode->FunctionScript->IsVersioningEnabled() ? TEXT("resolved") : TEXT("unversioned")) : TEXT("unresolved"));
			if (Version)
			{
				ModObj->SetStringField(TEXT("resolved_version_guid"), Version->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
				ModObj->SetNumberField(TEXT("version_major"), Version->Version.MajorVersion);
				ModObj->SetNumberField(TEXT("version_minor"), Version->Version.MinorVersion);
				ModObj->SetBoolField(TEXT("selected_is_exposed_version"), Version->Version.VersionGuid == FuncNode->FunctionScript->GetExposedVersion().VersionGuid);
			}
		}
		else
		{
			ModObj->SetField(TEXT("script_asset_path"), MakeShared<FJsonValueNull>());
			ModObj->SetStringField(TEXT("version_status"), TEXT("not_applicable_no_script"));
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
	if (CommandType == TEXT("read_niagara_authoring")) return UnrealMCPNiagaraAuthoring::Read(Params);
	if (CommandType == TEXT("read_niagara_particle_reader")) return UnrealMCPNiagaraAuthoring::ReadParticleReader(Params);
	if (CommandType == TEXT("bind_niagara_particle_reader")) return UnrealMCPNiagaraAuthoring::BindParticleReader(Params);
	if (CommandType == TEXT("create_niagara_effect_type")) return UnrealMCPNiagaraAuthoring::CreateEffectType(Params);
	if (CommandType == TEXT("set_niagara_scalability")) return UnrealMCPNiagaraAuthoring::WriteScalability(Params);
	if (CommandType == TEXT("save_niagara_authoring")) return UnrealMCPNiagaraAuthoring::SaveDraft(Params);
	if (CommandType == TEXT("set_niagara_user_parameters")) return UnrealMCPNiagaraAuthoring::SetUserParameters(Params);
	if (CommandType == TEXT("bind_niagara_module_input")) return UnrealMCPNiagaraAuthoring::BindUserInput(Params);
	if (CommandType == TEXT("redirect_niagara_user_link")) return UnrealMCPNiagaraAuthoring::RedirectUserInput(Params);
	if (CommandType == TEXT("bind_niagara_dynamic_input")) return UnrealMCPNiagaraAuthoring::BindDynamicUserInput(Params);
	if (CommandType == TEXT("multiply_niagara_float_input")) return UnrealMCPNiagaraAuthoring::MultiplyFloatInput(Params);
	if (CommandType == TEXT("set_niagara_material_bindings")) return UnrealMCPNiagaraAuthoring::SetMaterialBindings(Params);
	if (CommandType == TEXT("read_niagara_effect_type")) return UnrealMCPNiagaraScalability::ReadEffectTypeAsset(Params);
	if (CommandType == TEXT("list_niagara_systems"))
	{
		return HandleListNiagaraSystems(Params);
	}
	else if (CommandType == TEXT("read_niagara_system"))
	{
		return HandleReadNiagaraSystem(Params);
	}
	else if (CommandType == TEXT("read_niagara_diagnostics"))
	{
		return HandleReadNiagaraDiagnostics(Params);
	}
	else if (CommandType == TEXT("set_niagara_emitter_solo"))
	{
		return HandleSetNiagaraEmitterSolo(Params);
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
	else if (CommandType == TEXT("set_niagara_renderer_property"))
	{
		return HandleSetNiagaraRendererProperty(Params);
	}
	else if (CommandType == TEXT("list_module_inputs"))
	{
		return HandleListModuleInputs(Params);
	}
	else if (CommandType == TEXT("enable_module_input"))
	{
		return HandleEnableModuleInput(Params);
	}
	else if (CommandType == TEXT("list_module_static_switches"))
	{
		return HandleListModuleStaticSwitches(Params);
	}
	else if (CommandType == TEXT("set_module_static_switch"))
	{
		return HandleSetModuleStaticSwitch(Params);
	}
	else if (CommandType == TEXT("bind_module_input_datainterface"))
	{
		return HandleBindModuleInputDataInterface(Params);
	}
	else if (CommandType == TEXT("read_curve"))
	{
		return HandleReadCurve(Params);
	}
	else if (CommandType == TEXT("set_curve_keys"))
	{
		return HandleSetCurveKeys(Params);
	}
	else if (CommandType == TEXT("set_module_dynamic_input"))
	{
		return HandleSetModuleDynamicInput(Params);
	}
	else if (CommandType == TEXT("add_renderer_to_emitter"))
	{
		return HandleAddRendererToEmitter(Params);
	}
	else if (CommandType == TEXT("remove_renderer_from_emitter"))
	{
		return HandleRemoveRendererFromEmitter(Params);
	}
	else if (CommandType == TEXT("set_mesh_renderer_mesh"))
	{
		return HandleSetMeshRendererMesh(Params);
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
// Renderer property reflection helpers (used by read_niagara_system)
//
// These serialize an arbitrary FProperty value to JSON via reflection, so we can
// expose a Niagara renderer's UPROPERTYs (material, sub-image size, alignment,
// facing/sort modes, etc.) without hard-coding member access per renderer class.
// ─────────────────────────────────────────────────────────────────────────────

static TSharedPtr<FJsonValue> NiagaraPropValueToJson(FProperty* Prop, const void* ValuePtr)
{
	if (!Prop || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	// Bool
	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}

	// Enum class (FEnumProperty)
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		const FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty();
		const int64 Val = Underlying->GetSignedIntPropertyValue(ValuePtr);
		if (const UEnum* Enum = EnumProp->GetEnum())
		{
			return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(Val));
		}
		return MakeShared<FJsonValueNumber>(static_cast<double>(Val));
	}

	// TEnumAsByte / raw byte
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		const int64 Val = ByteProp->GetPropertyValue(ValuePtr);
		if (ByteProp->Enum)
		{
			return MakeShared<FJsonValueString>(ByteProp->Enum->GetNameStringByValue(Val));
		}
		return MakeShared<FJsonValueNumber>(static_cast<double>(Val));
	}

	// Soft object -> path (check before FObjectPropertyBase to avoid forced load)
	if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
	{
		const FString Path = SoftProp->GetPropertyValue(ValuePtr).ToString();
		return Path.IsEmpty() ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>())
		                      : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Path));
	}

	// Hard object reference (Material, StaticMesh, etc.) -> asset path or null
	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		const UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		return Obj ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Obj->GetPathName()))
		           : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	// Numeric
	if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
	{
		if (NumProp->IsFloatingPoint())
		{
			return MakeShared<FJsonValueNumber>(NumProp->GetFloatingPointPropertyValue(ValuePtr));
		}
		return MakeShared<FJsonValueNumber>(static_cast<double>(NumProp->GetSignedIntPropertyValue(ValuePtr)));
	}

	// String / Name / Text
	if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}
	if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}
	if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}

	// Struct — expose common math structs cleanly, recurse the rest
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		UScriptStruct* Struct = StructProp->Struct;
		if (Struct == TBaseStructure<FVector2D>::Get())
		{
			const FVector2D* V = reinterpret_cast<const FVector2D*>(ValuePtr);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), V->X);
			O->SetNumberField(TEXT("y"), V->Y);
			return MakeShared<FJsonValueObject>(O);
		}
		if (Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* V = reinterpret_cast<const FVector*>(ValuePtr);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), V->X);
			O->SetNumberField(TEXT("y"), V->Y);
			O->SetNumberField(TEXT("z"), V->Z);
			return MakeShared<FJsonValueObject>(O);
		}
		if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor* C = reinterpret_cast<const FLinearColor*>(ValuePtr);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("r"), C->R);
			O->SetNumberField(TEXT("g"), C->G);
			O->SetNumberField(TEXT("b"), C->B);
			O->SetNumberField(TEXT("a"), C->A);
			return MakeShared<FJsonValueObject>(O);
		}

		// Generic struct: reflect its members
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Inner = *It;
			const void* InnerPtr = Inner->ContainerPtrToValuePtr<void>(ValuePtr);
			O->SetField(Inner->GetName(), NiagaraPropValueToJson(Inner, InnerPtr));
		}
		return MakeShared<FJsonValueObject>(O);
	}

	// Array — recurse per element
	if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper Helper(ArrProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			Arr.Add(NiagaraPropValueToJson(ArrProp->Inner, Helper.GetRawPtr(i)));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	// Fallback: export as text
	FString Exported;
	Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(Exported);
}

// Read a named UPROPERTY off a renderer and add it to Obj under JsonKey.
// Silently skips if the property doesn't exist on this renderer class, so callers
// can list every candidate field without worrying about version/type differences.
static void AddNiagaraRendererProp(const TSharedPtr<FJsonObject>& Obj, const UObject* Renderer, const TCHAR* JsonKey, const TCHAR* PropName)
{
	if (!Obj.IsValid() || !Renderer)
	{
		return;
	}
	FProperty* Prop = Renderer->GetClass()->FindPropertyByName(FName(PropName));
	if (!Prop)
	{
		return;
	}
	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Renderer);
	Obj->SetField(JsonKey, NiagaraPropValueToJson(Prop, ValuePtr));
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
	ResultJson->SetStringField(TEXT("class_path"), System->GetClass()->GetPathName());
	ResultJson->SetNumberField(TEXT("reader_contract"), 2);
	ResultJson->SetObjectField(TEXT("scalability"), UnrealMCPNiagaraScalability::System(System));
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
		EmitterObj->SetStringField(TEXT("handle_id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
		EmitterObj->SetStringField(TEXT("enabled_semantics"), TEXT("authoring_enabled_not_runtime_eligibility"));
		EmitterObj->SetObjectField(TEXT("scalability"), UnrealMCPNiagaraScalability::Emitter(Handle));

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

			AddScriptModules(TEXT("emitter_spawn_script"), EmitterData->EmitterSpawnScriptProps.Script);
			AddScriptModules(TEXT("emitter_update_script"), EmitterData->EmitterUpdateScriptProps.Script);
			AddScriptModules(TEXT("spawn_script"), EmitterData->SpawnScriptProps.Script);
			AddScriptModules(TEXT("update_script"), EmitterData->UpdateScriptProps.Script);

			// Renderers
			const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
			TArray<TSharedPtr<FJsonValue>> RendererArray;
			TArray<TSharedPtr<FJsonValue>> NullRendererIndices;
			int32 RendererIndex = 0;
			for (const UNiagaraRendererProperties* Renderer : Renderers)
			{
				const int32 Index = RendererIndex++;
				if (!Renderer) { NullRendererIndices.Add(MakeShared<FJsonValueNumber>(Index)); continue; }
				TSharedPtr<FJsonObject> RendObj = MakeShared<FJsonObject>();
				const FString RendClassName = Renderer->GetClass()->GetName();
				RendObj->SetStringField(TEXT("type"), RendClassName);
				RendObj->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
				RendObj->SetNumberField(TEXT("renderer_index"), Index);
				RendObj->SetStringField(TEXT("object_path"), Renderer->GetPathName());
				RendObj->SetObjectField(TEXT("scalability"), UnrealMCPNiagaraScalability::Renderer(Renderer));
				RendObj->SetObjectField(TEXT("material_parameters"), UnrealMCPNiagaraAuthoring::ReadMaterialParameters(Renderer));

				// Base-class fields common to every renderer type
				AddNiagaraRendererProp(RendObj, Renderer, TEXT("motion_vector_setting"), TEXT("MotionVectorSetting"));
				AddNiagaraRendererProp(RendObj, Renderer, TEXT("renderer_visibility_tag"), TEXT("RendererVisibility"));
				AddNiagaraRendererProp(RendObj, Renderer, TEXT("platforms"), TEXT("Platforms"));

				if (RendClassName.Contains(TEXT("Sprite")))
				{
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("material"), TEXT("Material"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("material_user_binding"), TEXT("MaterialUserParamBinding"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sub_image_size"), TEXT("SubImageSize"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sub_uv_blending_enabled"), TEXT("bSubImageBlend"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("alignment"), TEXT("Alignment"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("facing_mode"), TEXT("FacingMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sort_mode"), TEXT("SortMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("pivot_offset"), TEXT("PivotOffset"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("min_facing_camera_blend_distance"), TEXT("MinFacingCameraBlendDistance"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("max_facing_camera_blend_distance"), TEXT("MaxFacingCameraBlendDistance"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sort_only_when_translucent"), TEXT("bSortOnlyWhenTranslucent"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("use_gpu_init"), TEXT("bGpuLowLatencyTranslucency"));
				}
				else if (RendClassName.Contains(TEXT("Mesh")))
				{
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("meshes"), TEXT("Meshes"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("override_materials"), TEXT("OverrideMaterials"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("source_mode"), TEXT("SourceMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("facing_mode"), TEXT("FacingMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("mesh_alignment"), TEXT("Alignment"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sort_mode"), TEXT("SortMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("enable_frustum_culling"), TEXT("bEnableFrustumCulling"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("enable_camera_distance_culling"), TEXT("bEnableCameraDistanceCulling"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sub_image_size"), TEXT("SubImageSize"));
				}
				else if (RendClassName.Contains(TEXT("Ribbon")))
				{
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("material"), TEXT("Material"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("facing_mode"), TEXT("FacingMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("uv0_settings"), TEXT("UV0Settings"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("uv1_settings"), TEXT("UV1Settings"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("draw_direction"), TEXT("DrawDirection"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("tessellation_mode"), TEXT("TessellationMode"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("curve_tension"), TEXT("CurveTension"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("tessellation_factor"), TEXT("TessellationFactor"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("sub_image_size"), TEXT("SubImageSize"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("shape"), TEXT("Shape"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("custom_vertices"), TEXT("CustomVertices"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("renderer_visibility_tag_id"), TEXT("RendererVisibilityTagId"));
				}
				else if (RendClassName.Contains(TEXT("Light")))
				{
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("radius_scale"), TEXT("RadiusScale"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("color_add"), TEXT("ColorAdd"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("use_inverse_squared_falloff"), TEXT("bUseInverseSquaredFalloff"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("affects_translucency"), TEXT("bAffectsTranslucency"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("light_rendering_enabled"), TEXT("bLightsEnabled"));
					AddNiagaraRendererProp(RendObj, Renderer, TEXT("volumetric_scattering_intensity"), TEXT("VolumetricScatteringIntensity"));
				}

				RendererArray.Add(MakeShared<FJsonValueObject>(RendObj));
			}
			EmitterObj->SetArrayField(TEXT("renderers"), RendererArray);
			EmitterObj->SetNumberField(TEXT("renderer_slot_count"), Renderers.Num());
			EmitterObj->SetArrayField(TEXT("null_renderer_indices"), NullRendererIndices);
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
	bool ScalabilityComplete = ResultJson->GetObjectField(TEXT("scalability"))->GetBoolField(TEXT("complete"));
	for (const auto& Value : EmitterArray)
	{
		const auto Emitter = Value->AsObject();
		ScalabilityComplete &= Emitter->GetObjectField(TEXT("scalability"))->GetBoolField(TEXT("complete"));
		const TArray<TSharedPtr<FJsonValue>>* Renderers = nullptr;
		if (Emitter->TryGetArrayField(TEXT("renderers"), Renderers))
			for (const auto& Renderer : *Renderers) ScalabilityComplete &= Renderer->AsObject()->GetObjectField(TEXT("scalability"))->GetBoolField(TEXT("complete"));
		const TArray<TSharedPtr<FJsonValue>>* NullSlots = nullptr;
		if (Emitter->TryGetArrayField(TEXT("null_renderer_indices"), NullSlots) && !NullSlots->IsEmpty()) ScalabilityComplete = false;
	}
	ResultJson->SetBoolField(TEXT("scalability_complete"), ScalabilityComplete);
	ResultJson->SetObjectField(TEXT("read_context"), UnrealMCPNiagaraScalability::Context());
	ResultJson->SetBoolField(TEXT("modified"), false); ResultJson->SetBoolField(TEXT("saved"), false);

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
#if WITH_DEV_AUTOMATION_TESTS
static bool FailNiagaraSaveForTest = false;
static bool FailReaderLocalizationForTest = false;
void UnrealMCPNiagaraTests::SetReaderLocalizationFailure(bool Fail) { FailReaderLocalizationForTest = Fail; }
#endif

static bool SaveNiagaraSystemAsset(UNiagaraSystem* System)
{
	if (!System || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask) return false;
#if WITH_DEV_AUTOMATION_TESTS
	if (FailNiagaraSaveForTest) return false;
#endif
	UPackage* Package = System->GetOutermost();
	if (!Package) return false;

	FString PackageFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		return false;

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	return UPackage::SavePackage(Package, System, *PackageFilename, SaveArgs) && !Package->IsDirty();
}

static TSharedPtr<FJsonObject> NiagaraSaveFailure(UNiagaraSystem* System)
{
	auto Result = FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Niagara changes were applied but automatic save failed or was unsafe; inspect the asset before retrying"));
	Result->SetBoolField(TEXT("success"), false);
	Result->SetBoolField(TEXT("modified"), true);
	Result->SetBoolField(TEXT("saved"), false);
	Result->SetStringField(TEXT("stage"), TEXT("save"));
	Result->SetStringField(TEXT("error_code"), TEXT("niagara_save_failed"));
	Result->SetStringField(TEXT("asset_path"), System->GetPathName());
	Result->SetBoolField(TEXT("package_dirty"), System->GetPackage()->IsDirty());
	return Result;
}

// Helper: synchronize the System Overview graph with emitter handles
static void SyncOverviewGraph(UNiagaraSystem* System)
{
	if (!System) return;
	UNiagaraSystemEditorData* EditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
	if (EditorData)
	{
		EditorData->SynchronizeOverviewGraphWithSystem(*System);
	}
}

static bool IsAuthoredParticleReader(const UNiagaraDataInterfaceParticleRead* Reader, const UNiagaraEmitter* Emitter)
{
	const auto* Node = Reader ? Cast<UNiagaraNodeInput>(Reader->GetOuter()) : nullptr;
	const auto* Property = Node ? FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("DataInterface")) : nullptr;
	return Property && Property->GetObjectPropertyValue_InContainer(Node) == Reader && Reader->GetTypedOuter<UNiagaraEmitter>() == Emitter;
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
	else if (ScriptType == TEXT("emitter_spawn"))
		Script = EmitterData->EmitterSpawnScriptProps.Script;
	else if (ScriptType == TEXT("emitter_update"))
		Script = EmitterData->EmitterUpdateScriptProps.Script;
	else
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid script_type '%s'. Must be 'spawn', 'update', 'emitter_spawn', or 'emitter_update'."), *ScriptType));

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
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

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
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

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

		// Use the editor utility which properly rebuilds emitter nodes for compilation
		FGuid NewHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*TargetSystem, *TemplateEmitter, TemplateEmitter->GetExposedVersion().VersionGuid);

		// Find the new handle by ID and optionally rename it
		FString ActualEmitterName;
		for (FNiagaraEmitterHandle& Handle : TargetSystem->GetEmitterHandles())
		{
			if (Handle.GetId() == NewHandleId)
			{
				if (!NewEmitterName.IsEmpty())
				{
					Handle.SetName(FName(*NewEmitterName), *TargetSystem);
				}
				ActualEmitterName = Handle.GetName().ToString();
				break;
			}
		}

		TargetSystem->RequestCompile(true);
		TargetSystem->WaitForCompilationComplete();
		TargetSystem->MarkPackageDirty();
		if (!SaveNiagaraSystemAsset(TargetSystem)) return NiagaraSaveFailure(TargetSystem);
		TargetSystem->PostEditChange();

		TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("status"), TEXT("success"));
		ResultJson->SetStringField(TEXT("message"), TEXT("Emitter added from engine template"));
		ResultJson->SetStringField(TEXT("system"), TargetSystem->GetName());
		ResultJson->SetStringField(TEXT("template"), TemplateName);
		ResultJson->SetStringField(TEXT("new_emitter"), ActualEmitterName);
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

		SyncOverviewGraph(TargetSystem);
		TargetSystem->InvalidateCachedData();
		TargetSystem->RequestCompile(true);
		TargetSystem->WaitForCompilationComplete();
		TargetSystem->MarkPackageDirty();
		if (!SaveNiagaraSystemAsset(TargetSystem)) return NiagaraSaveFailure(TargetSystem);
		TargetSystem->PostEditChange();

		TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("status"), TEXT("success"));
		ResultJson->SetStringField(TEXT("message"), TEXT("Emitter duplicated within system"));
		ResultJson->SetStringField(TEXT("system"), TargetSystem->GetName());
		ResultJson->SetStringField(TEXT("new_emitter"), NewHandle.GetName().ToString());
		ResultJson->SetNumberField(TEXT("emitter_count"), TargetSystem->GetEmitterHandles().Num());
		return ResultJson;
	}

	FVersionedNiagaraEmitter SourceInstance = SourceHandle->GetInstance();
	if (!SourceInstance.Emitter)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Source emitter instance is null"));

	auto CopyRejected = [&](const FString& Error)
	{
		auto Result = FUnrealMCPCommonUtils::CreateErrorResponse(Error);
		Result->SetBoolField(TEXT("success"), false); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("stage"), TEXT("preflight")); return Result;
	};
	if (TargetSystem->GetPackage()->IsDirty()) return CopyRejected(TEXT("Preexisting dirty copy target refused; explicitly save the approved draft first"));
	TMap<FName, FName> ReaderNames;
	const TArray<TSharedPtr<FJsonValue>>* ReaderBindings = nullptr;
	if (Params->HasField(TEXT("reader_source_bindings")))
	{
		if (!Params->TryGetArrayField(TEXT("reader_source_bindings"), ReaderBindings) || ReaderBindings->Num() > 64)
			return CopyRejected(TEXT("reader_source_bindings requires at most 64 exact source_handle_id/target_handle_id pairs"));
		for (const auto& Value : *ReaderBindings)
		{
			FString FromId, ToId; FGuid FromGuid, ToGuid;
			if (Value->Type != EJson::Object || Value->AsObject()->Values.Num() != 2
				|| !Value->AsObject()->TryGetStringField(TEXT("source_handle_id"), FromId) || !FGuid::Parse(FromId, FromGuid)
				|| !Value->AsObject()->TryGetStringField(TEXT("target_handle_id"), ToId) || !FGuid::Parse(ToId, ToGuid))
				return CopyRejected(TEXT("Reader mapping requires exact source_handle_id and target_handle_id GUID strings"));
			const auto* From = SourceSystem->GetEmitterHandles().FindByPredicate([&](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == FromGuid; });
			const auto* To = TargetSystem->GetEmitterHandles().FindByPredicate([&](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == ToGuid; });
			if (!From || !To || !From->GetInstance().Emitter || !To->GetInstance().Emitter || !To->GetEmitterData()
				|| To->GetEmitterData()->SimTarget != ENiagaraSimTarget::CPUSim || From->GetId() == SourceHandle->GetId())
				return CopyRejected(TEXT("Reader source mapping must resolve to existing distinct stateful CPU Handles in source/target systems"));
			const FName FromName(*From->GetInstance().Emitter->GetUniqueEmitterName());
			if (ReaderNames.Contains(FromName)) return CopyRejected(TEXT("Duplicate Reader source mapping"));
			ReaderNames.Add(FromName, FName(*To->GetInstance().Emitter->GetUniqueEmitterName()));
		}
	}
	TArray<UObject*> SourceObjects; GetObjectsWithOuter(SourceInstance.Emitter, SourceObjects, true);
	for (auto* Object : SourceObjects) if (const auto* Reader = Cast<UNiagaraDataInterfaceParticleRead>(Object))
	{
		if (!IsAuthoredParticleReader(Reader, SourceInstance.Emitter)
			|| Reader->EmitterBinding.BindingMode != ENiagaraDataInterfaceEmitterBindingMode::Other) continue;
		const FName Desired = ReaderNames.Contains(Reader->EmitterBinding.EmitterName) ? ReaderNames[Reader->EmitterBinding.EmitterName] : Reader->EmitterBinding.EmitterName;
		int32 Matches = 0;
		for (const auto& Handle : TargetSystem->GetEmitterHandles())
			if (Handle.GetInstance().Emitter && FName(*Handle.GetInstance().Emitter->GetUniqueEmitterName()) == Desired
				&& Handle.GetEmitterData() && Handle.GetEmitterData()->SimTarget == ENiagaraSimTarget::CPUSim) ++Matches;
		if (Matches != 1) return CopyRejected(FString::Printf(TEXT("Copied Reader source '%s' has %d target matches for emitter '%s' (%s); copy the source layer first or provide exact reader_source_bindings"),
			*Desired.ToString(), Matches, *SourceEmitterName, *Reader->GetPathName()));
	}
	SourceSystem->WaitForCompilationComplete();
	TargetSystem->WaitForCompilationComplete();
	TSet<FGuid> PreviousHandles;
	for (const FNiagaraEmitterHandle& Handle : TargetSystem->GetEmitterHandles()) PreviousHandles.Add(Handle.GetId());
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "CopySystemEmitter", "Copy Niagara system emitter"));
	FNiagaraEditorUtilities::AddEmitterToSystem(*TargetSystem, *SourceInstance.Emitter, SourceInstance.Version);

	FString ActualEmitterName;
	FVersionedNiagaraEmitter CopiedInstance;
	FGuid CopiedHandleId;
	for (FNiagaraEmitterHandle& Handle : TargetSystem->GetEmitterHandles())
	{
		if (!PreviousHandles.Contains(Handle.GetId()))
		{
			CopiedInstance = Handle.GetInstance(); CopiedHandleId = Handle.GetId();
			if (auto EmitterData = Handle.GetEmitterData()) EmitterData->RemoveParent();
			Handle.SetIsEnabled(SourceHandle->GetIsEnabled(), *TargetSystem, false);
			if (!NewEmitterName.IsEmpty())
			{
				Handle.SetName(FName(*NewEmitterName), *TargetSystem);
			}
			ActualEmitterName = Handle.GetName().ToString();
			break;
		}
	}

	auto ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("inheritance_mode"), TEXT("independent_snapshot"));
	ResultJson->SetBoolField(TEXT("modified"), TargetSystem->GetEmitterHandles().Num() != PreviousHandles.Num());
	ResultJson->SetBoolField(TEXT("saved"), false);
	ResultJson->SetStringField(TEXT("created_handle_id"), CopiedHandleId.ToString(EGuidFormats::DigitsWithHyphens));
	ResultJson->SetStringField(TEXT("new_emitter"), ActualEmitterName);
	ResultJson->SetBoolField(TEXT("package_dirty"), TargetSystem->GetPackage()->IsDirty());
	if (ActualEmitterName.IsEmpty() || TargetSystem->GetEmitterHandles().Num() != PreviousHandles.Num() + 1)
	{
		ResultJson->SetBoolField(TEXT("success"), false);
		ResultJson->SetStringField(TEXT("error"), TEXT("Emitter copy did not produce exactly one handle; inspect target before retrying"));
		return ResultJson;
	}
	TArray<UObject*> AuthorObjects; GetObjectsWithOuter(CopiedInstance.Emitter, AuthorObjects, true);
	for (auto* Object : AuthorObjects) if (auto* Reader = Cast<UNiagaraDataInterfaceParticleRead>(Object))
		if (IsAuthoredParticleReader(Reader, CopiedInstance.Emitter)
			&& Reader->EmitterBinding.BindingMode == ENiagaraDataInterfaceEmitterBindingMode::Other)
			if (const auto* Name = ReaderNames.Find(Reader->EmitterBinding.EmitterName))
			{ Reader->Modify(); Reader->EmitterBinding.EmitterName = *Name; Reader->PostEditChange(); }
	auto LocalizeReaders = [&]()
	{
		TArray<UObject*> CopiedObjects;
		GetObjectsWithOuter(CopiedInstance.Emitter, CopiedObjects, true);
#if WITH_DEV_AUTOMATION_TESTS
		if (FailReaderLocalizationForTest)
		{
			for (auto* SourceObject : SourceObjects) if (auto* Foreign = Cast<UNiagaraDataInterfaceParticleRead>(SourceObject))
				if (IsAuthoredParticleReader(Foreign, SourceInstance.Emitter))
					for (auto* TargetObject : CopiedObjects) if (auto* Local = Cast<UNiagaraDataInterfaceParticleRead>(TargetObject))
						if (IsAuthoredParticleReader(Local, CopiedInstance.Emitter))
						{
							auto* Node = Local->GetOuter();
							auto* Property = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("DataInterface"));
							Property->SetObjectPropertyValue_InContainer(Node, Foreign);
							return 0;
						}
			return 0;
		}
#endif
		TMap<UObject*, UObject*> ReaderRemap;
		TArray<TSharedPtr<FJsonValue>> ReaderCandidates;
		auto DescribeReader = [&](UNiagaraDataInterfaceParticleRead* Reader)
		{
			auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("object"), Reader->GetPathName());
			Entry->SetStringField(TEXT("outer"), Reader->GetOuter()->GetPathName());
			Entry->SetStringField(TEXT("binding_mode"), Reader->EmitterBinding.BindingMode == ENiagaraDataInterfaceEmitterBindingMode::Other ? TEXT("Other") : TEXT("Self"));
			Entry->SetStringField(TEXT("emitter_name"), Reader->EmitterBinding.EmitterName.ToString());
			ReaderCandidates.Add(MakeShared<FJsonValueObject>(Entry));
		};
		for (auto* Object : CopiedObjects) if (auto* Reader = Cast<UNiagaraDataInterfaceParticleRead>(Object)) DescribeReader(Reader);
		for (UObject* Object : CopiedObjects)
		{
			TArray<UObject*> References;
			FReferenceFinder Finder(References, nullptr, false, true, false, false); Finder.FindReferences(Object);
			for (UObject* Reference : References)
			{
				auto* ForeignReader = Cast<UNiagaraDataInterfaceParticleRead>(Reference);
				if (!ForeignReader || ForeignReader->GetPackage() != SourceSystem->GetPackage() || ForeignReader->HasAnyFlags(RF_Public | RF_Transient)) continue;
				DescribeReader(ForeignReader);
				auto Wanted = ForeignReader->EmitterBinding;
				if (const auto* Name = ReaderNames.Find(Wanted.EmitterName)) Wanted.EmitterName = *Name;
				UNiagaraDataInterfaceParticleRead* LocalReader = nullptr;
				bool Ambiguous = false;
				for (UObject* Candidate : CopiedObjects)
				{
					auto* Reader = Cast<UNiagaraDataInterfaceParticleRead>(Candidate);
					if (!IsAuthoredParticleReader(Reader, CopiedInstance.Emitter) || Reader->GetPackage() != TargetSystem->GetPackage()
						|| Reader->EmitterBinding != Wanted) continue;
					if (LocalReader && LocalReader != Reader) { Ambiguous = true; break; }
					LocalReader = Reader;
				}
				if (LocalReader && !Ambiguous) ReaderRemap.Add(ForeignReader, LocalReader);
			}
		}
		for (UObject* Object : CopiedObjects)
		{
			FArchiveReplaceObjectRef<UObject> Remap(Object, ReaderRemap, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
		}
		ResultJson->SetArrayField(TEXT("reader_localization_candidates"), ReaderCandidates);
		return ReaderRemap.Num();
	};
	TargetSystem->PostEditChange();
	TargetSystem->RequestCompile(true);
	TargetSystem->WaitForCompilationComplete();
	ResultJson->SetNumberField(TEXT("localized_reader_cache_references"), LocalizeReaders());
	TargetSystem->MarkPackageDirty();
	TArray<UObject*> Exports{TargetSystem};
	TSet<UObject*> Reachable{TargetSystem};
	for (int32 Index = 0; Index < Exports.Num(); ++Index)
	{
		TArray<UObject*> References;
		FReferenceFinder Finder(References, nullptr, false, true, false, false); Finder.FindReferences(Exports[Index]);
		for (auto* Reference : References)
			if (Reference && Reference->GetPackage() == TargetSystem->GetPackage() && !Reference->HasAnyFlags(RF_Transient) && !Reachable.Contains(Reference))
			{ Reachable.Add(Reference); Exports.Add(Reference); }
	}
	TArray<TSharedPtr<FJsonValue>> PrivateReferences;
	for (UObject* Export : Exports)
	{
		if (Export->HasAnyFlags(RF_Transient)) continue;
		TArray<UObject*> References;
		FReferenceFinder Finder(References, nullptr, false, true, false, false);
		Finder.FindReferences(Export);
		for (UObject* Reference : References)
			if (Reference && Reference->GetPackage() == SourceSystem->GetPackage() && !Reference->HasAnyFlags(RF_Public | RF_Transient))
			{
				auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("referencer"), Export->GetPathName());
				Entry->SetStringField(TEXT("reference"), Reference->GetPathName()); PrivateReferences.Add(MakeShared<FJsonValueObject>(Entry));
			}
	}
	if (!PrivateReferences.IsEmpty())
	{
		ResultJson->SetBoolField(TEXT("success"), false); ResultJson->SetStringField(TEXT("stage"), TEXT("reference_preflight"));
		ResultJson->SetStringField(TEXT("error"), TEXT("Copied emitter still references private source objects; target not saved. Inspect references before retrying"));
		ResultJson->SetArrayField(TEXT("private_references"), PrivateReferences); return ResultJson;
	}
	if (!TargetSystem->IsValid() || !TargetSystem->IsReadyToRun())
	{
		ResultJson->SetBoolField(TEXT("success"), false); ResultJson->SetStringField(TEXT("stage"), TEXT("compile"));
		ResultJson->SetStringField(TEXT("error"), TEXT("Copied system compilation is not ready/valid; target not saved")); return ResultJson;
	}
	const bool Saved = SaveNiagaraSystemAsset(TargetSystem) && !TargetSystem->GetPackage()->IsDirty();

	ResultJson->SetBoolField(TEXT("success"), Saved);
	ResultJson->SetBoolField(TEXT("saved"), Saved);
	ResultJson->SetStringField(TEXT("status"), Saved ? TEXT("success") : TEXT("error"));
	if (!Saved) ResultJson->SetStringField(TEXT("error"), TEXT("Emitter copied in memory but target save failed; inspect before retrying"));
	ResultJson->SetStringField(TEXT("message"), TEXT("Emitter copied from source system"));
	ResultJson->SetStringField(TEXT("system"), TargetSystem->GetName());
	ResultJson->SetStringField(TEXT("source_system"), SourceSystem->GetName());
	ResultJson->SetStringField(TEXT("source_emitter"), SourceEmitterName);
	ResultJson->SetStringField(TEXT("new_emitter"), ActualEmitterName);
	ResultJson->SetNumberField(TEXT("emitter_count"), TargetSystem->GetEmitterHandles().Num());
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_emitter_from_system
// ─────────────────────────────────────────────────────────────────────────────

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraCrossSystemCopyTest, "UnrealMCP.Niagara.CrossSystemEmitterCopy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraCrossSystemCopyTest::RunTest(const FString& Parameters)
{
	FString Root;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MCPEmitterCopyRoot="), Root))
		Root = TEXT("/Game/__Dev/EmitterCopy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!Root.StartsWith(TEXT("/Game/__Dev/EmitterCopy_")) || !FPackageName::IsValidLongPackageName(Root)) return false;
	const FString SourcePath = Root + TEXT("/NS_Source"), TargetPath = Root + TEXT("/NS_Target");
	UNiagaraSystem* Source = nullptr; UNiagaraSystem* Target = nullptr;
	const bool Reload = FParse::Param(FCommandLine::Get(), TEXT("MCPEmitterCopyReload"));
	if (Reload)
	{
		Source = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SourcePath));
		Target = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(TargetPath));
	}
	else
	{
		if (FPackageName::DoesPackageExist(SourcePath) || FPackageName::DoesPackageExist(TargetPath))
		{ AddError(TEXT("Fixture root occupied; nothing overwritten")); return false; }
		FString FixtureSource = TEXT("/Niagara/DefaultAssets/DefaultSystem");
		const bool RealSource = FParse::Value(FCommandLine::Get(), TEXT("MCPEmitterSource="), FixtureSource);
		Source = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(FixtureSource, SourcePath));
		Target = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), TargetPath));
		if (!TestNotNull(TEXT("Source system"), Source) || !TestNotNull(TEXT("Target system"), Target)) return false;
		FUnrealMCPNiagaraCommands Commands;
		auto Add = MakeShared<FJsonObject>(); Add->SetStringField(TEXT("path"), SourcePath);
		Add->SetStringField(TEXT("template_name"), TEXT("SimpleSpriteBurst")); Add->SetStringField(TEXT("new_emitter_name"), TEXT("Refraction"));
		if (!RealSource)
		{
			auto Added = Commands.HandleCommand(TEXT("add_emitter_to_system"), Add);
			if (!TestTrue(TEXT("Engine template added"), Added->HasField(TEXT("new_emitter")))) return false;
		}
		auto Selected = Source->GetEmitterHandles().FindByPredicate([](const FNiagaraEmitterHandle& Handle) { return Handle.GetName() == TEXT("Refraction"); });
		if (!TestNotNull(TEXT("Refraction source layer"), Selected)) return false;
		auto& SourceHandle = *Selected;
		SourceHandle.GetInstance().Emitter->bIsInheritable = true;
		if (!RealSource)
		{
			SourceHandle.GetEmitterData()->bLocalSpace = true;
			SourceHandle.SetIsEnabled(false, *Source, false);
		}
		Source->PostEditChange(); Source->RequestCompile(true); Source->WaitForCompilationComplete();
		if (!TestTrue(TEXT("Save source fixture"), SaveNiagaraSystemAsset(Source))) return false;
		if (!TestTrue(TEXT("Save initial target"), SaveNiagaraSystemAsset(Target))) return false;
		FString SourceFile; FPackageName::DoesPackageExist(SourcePath, &SourceFile);
		const FMD5Hash SourceHash = FMD5Hash::HashFile(*SourceFile);
		auto Copy = MakeShared<FJsonObject>(); Copy->SetStringField(TEXT("path"), TargetPath);
		Copy->SetStringField(TEXT("source_system_path"), SourcePath); Copy->SetStringField(TEXT("source_emitter_name"), TEXT("Refraction"));
		Copy->SetStringField(TEXT("new_emitter_name"), TEXT("RefractionCopy"));
		auto Result = Commands.HandleCommand(TEXT("add_emitter_to_system"), Copy);
		if (!TestTrue(TEXT("Cross-system copy saved without private reference fatal"), Result->GetBoolField(TEXT("success"))))
		{ FString Error; Result->TryGetStringField(TEXT("error"), Error); AddError(Error); return false; }
		TestTrue(TEXT("Actual save receipt"), Result->GetBoolField(TEXT("saved")));
		TestTrue(TEXT("Original file untouched"), SourceHash == FMD5Hash::HashFile(*SourceFile));
	}
	if (!TestNotNull(TEXT("Source available"), Source) || !TestNotNull(TEXT("Target available"), Target)) return false;
	Source->WaitForCompilationComplete(); Target->WaitForCompilationComplete();
	if (!TestEqual(TEXT("Original layer plus one copied emitter"), Target->GetEmitterHandles().Num(), 2)) return false;
	const FNiagaraEmitterHandle* CopiedHandle = Target->GetEmitterHandles().FindByPredicate([](const FNiagaraEmitterHandle& Candidate) { return Candidate.GetName() == TEXT("RefractionCopy"); });
	const FNiagaraEmitterHandle* OriginalHandle = Source->GetEmitterHandles().FindByPredicate([](const FNiagaraEmitterHandle& Candidate) { return Candidate.GetName() == TEXT("Refraction"); });
	if (!TestNotNull(TEXT("Copied layer found"), CopiedHandle) || !TestNotNull(TEXT("Source layer found"), OriginalHandle)) return false;
	const auto& Handle = *CopiedHandle;
	TestEqual(TEXT("Requested display name"), Handle.GetName(), FName(TEXT("RefractionCopy")));
	TestEqual(TEXT("Enabled state preserved"), Handle.GetIsEnabled(), OriginalHandle->GetIsEnabled());
	TestTrue(TEXT("Local-space setting preserved"), Handle.GetEmitterData()->bLocalSpace == OriginalHandle->GetEmitterData()->bLocalSpace);
	TestNull(TEXT("No private source parent"), Handle.GetEmitterData()->GetParent().Emitter);
	TestNull(TEXT("No previous-merge parent"), Handle.GetEmitterData()->GetParentAtLastMerge().Emitter);
	TestEqual(TEXT("Copied emitter belongs to target"), Handle.GetInstance().Emitter->GetOuter(), static_cast<UObject*>(Target));
	TestTrue(TEXT("Source still inheritable and unchanged"), OriginalHandle->GetInstance().Emitter->bIsInheritable);
	TestTrue(TEXT("Target compiles"), Target->IsValid() && Target->IsReadyToRun());
	TestFalse(TEXT("Target clean after save/reload"), Target->GetPackage()->IsDirty());
	TestFalse(TEXT("Source remains clean"), Source->GetPackage()->IsDirty());
	AddInfo(TEXT("Emitter copy fixture: ") + Root);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraEmitterRemovalTest, "UnrealMCP.Niagara.EmitterRemoval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraEmitterRemovalTest::RunTest(const FString& Parameters)
{
	FUnrealMCPNiagaraCommands Commands;
	for (bool WithViewModel : {false, true})
	{
		const FString Path = TEXT("/Game/__Dev/EmitterRemoval_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/NS_Test");
		auto System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), Path));
		if (!TestNotNull(TEXT("Isolated removal fixture"), System)) return false;
		auto Add = MakeShared<FJsonObject>(); Add->SetStringField(TEXT("path"), Path);
		Add->SetStringField(TEXT("template_name"), TEXT("SimpleSpriteBurst")); Add->SetStringField(TEXT("new_emitter_name"), TEXT("RemoveMe"));
		if (!TestTrue(TEXT("Second fixture emitter added"), Commands.HandleCommand(TEXT("add_emitter_to_system"), Add)->HasField(TEXT("new_emitter")))) return false;
		const auto Removed = System->GetEmitterHandles().FindByPredicate([](const FNiagaraEmitterHandle& Handle) { return Handle.GetName() == TEXT("RemoveMe"); });
		if (!TestNotNull(TEXT("Removal selector resolves"), Removed)) return false;
		const FGuid RemovedId = Removed->GetId(), KeptId = System->GetEmitterHandles()[0].GetId();
		TSharedPtr<FNiagaraSystemViewModel> ViewModel;
		if (WithViewModel)
		{
			ViewModel = MakeShared<FNiagaraSystemViewModel>();
			FNiagaraSystemViewModelOptions Options; Options.bCanAutoCompile = false;
			Options.MessageLogGuid = FGuid::NewGuid();
			ViewModel->Initialize(*System, Options);
			TestTrue(TEXT("Preview ViewModel registered"), FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor")).GetExistingViewModelForSystem(System) == ViewModel);
		}
		auto Remove = MakeShared<FJsonObject>(); Remove->SetStringField(TEXT("path"), Path); Remove->SetStringField(TEXT("emitter_name"), TEXT("RemoveMe"));
		auto Result = Commands.HandleCommand(TEXT("remove_emitter_from_system"), Remove);
		if (!TestTrue(TEXT("Removal completed and saved"), Result->HasField(TEXT("removed_emitter")) && Result->GetBoolField(TEXT("saved")))) return false;
		TestEqual(TEXT("Exactly one handle remains"), System->GetEmitterHandles().Num(), 1);
		TestFalse(TEXT("Removed GUID absent"), System->GetEmitterHandles().ContainsByPredicate([&](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == RemovedId; }));
		TestTrue(TEXT("Unselected GUID preserved"), System->GetEmitterHandles().ContainsByPredicate([&](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == KeptId; }));
		TestTrue(TEXT("Remaining system compiles"), System->IsValid() && System->IsReadyToRun());
		TestFalse(TEXT("Saved package clean"), System->GetPackage()->IsDirty());
		if (ViewModel) TestEqual(TEXT("ViewModel handles rebuilt"), ViewModel->GetEmitterHandleViewModels().Num(), 1);
		AddInfo(TEXT("Emitter removal fixture: ") + Path);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraSaveFailureTest, "UnrealMCP.Niagara.SaveFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraSaveFailureTest::RunTest(const FString& Parameters)
{
	const FString Path = TEXT("/Game/__Dev/NiagaraSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/NS_Test");
	auto System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), Path));
	if (!TestNotNull(TEXT("Isolated saved Niagara fixture"), System) || !TestTrue(TEXT("Default emitter available"), !System->GetEmitterHandles().IsEmpty())) return false;
	System->WaitForCompilationComplete();
	if (!TestTrue(TEXT("Initial fixture explicitly saved"), SaveNiagaraSystemAsset(System))) return false;
	FString Filename;
	if (!TestTrue(TEXT("Fixture file exists"), FPackageName::DoesPackageExist(Path, &Filename))) return false;
	const FMD5Hash Before = FMD5Hash::HashFile(*Filename);
	const FName EmitterName = System->GetEmitterHandles()[0].GetName();
	const bool LocalSpace = !System->GetEmitterHandles()[0].GetEmitterData()->bLocalSpace;
	auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("path"), Path);
	Request->SetStringField(TEXT("emitter_name"), EmitterName.ToString()); Request->SetBoolField(TEXT("local_space"), LocalSpace);
	FUnrealMCPNiagaraCommands Commands;
	{
		TGuardValue<bool> Failure(FailNiagaraSaveForTest, true);
		auto Result = Commands.HandleCommand(TEXT("modify_emitter_properties"), Request);
		TestFalse(TEXT("Save failure cannot return success"), Result->GetBoolField(TEXT("success")));
		TestTrue(TEXT("Failure truthfully reports memory modification"), Result->GetBoolField(TEXT("modified")));
		TestFalse(TEXT("Failure truthfully reports not saved"), Result->GetBoolField(TEXT("saved")));
		TestEqual(TEXT("Failure stage"), Result->GetStringField(TEXT("stage")), FString(TEXT("save")));
		TestTrue(TEXT("Unsaveable edit remains dirty"), System->GetPackage()->IsDirty());
		TestTrue(TEXT("Disk did not silently change"), Before == FMD5Hash::HashFile(*Filename));
		TestEqual(TEXT("Applied property remains inspectable"), System->GetEmitterHandles()[0].GetEmitterData()->bLocalSpace, LocalSpace);
	}
	TestTrue(TEXT("Explicit inspected recovery can save"), SaveNiagaraSystemAsset(System));
	TestFalse(TEXT("Successful save leaves package clean"), System->GetPackage()->IsDirty());
	AddInfo(TEXT("Niagara save failure fixture: ") + Path);
	return !HasAnyErrors();
}
#endif

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

	auto Reject = [&](const FString& Error)
	{
		auto Result = FUnrealMCPCommonUtils::CreateErrorResponse(Error);
		Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("stage"), TEXT("preflight")); return Result;
	};
	bool SaveRequested = true;
	if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), SaveRequested)) return Reject(TEXT("save must be bool"));
	if (SaveRequested && System->GetPackage()->IsDirty()) return Reject(TEXT("Preexisting dirty system refused for automatic save; inspect draft and use explicit save=false for unsaved cleanup"));
	FString ExpectedId;
	if (Params->TryGetStringField(TEXT("expected_handle_id"), ExpectedId))
	{
		FGuid Expected;
		if (!FGuid::Parse(ExpectedId, Expected) || Expected != TargetHandle->GetId()) return Reject(TEXT("Stale expected_handle_id; inspect exact emitter before deleting"));
	}
	const FName RemovedUniqueName(*TargetHandle->GetInstance().Emitter->GetUniqueEmitterName());
	for (const auto& Other : EmitterHandles) if (Other.GetId() != TargetHandle->GetId() && Other.GetInstance().Emitter)
	{
		TArray<UObject*> Objects; GetObjectsWithOuter(Other.GetInstance().Emitter, Objects, true);
		for (auto* Object : Objects) if (const auto* Reader = Cast<UNiagaraDataInterfaceParticleRead>(Object))
			if (IsAuthoredParticleReader(Reader, Other.GetInstance().Emitter)
				&& Reader->EmitterBinding.BindingMode == ENiagaraDataInterfaceEmitterBindingMode::Other && Reader->EmitterBinding.EmitterName == RemovedUniqueName)
				return Reject(TEXT("Emitter is still referenced by another Particle Reader; remove dependent layers first"));
	}
	int32 OldCount = EmitterHandles.Num();

	TSet<FGuid> HandlesToRemove;
	HandlesToRemove.Add(TargetHandle->GetId());
	System->WaitForCompilationComplete();
	auto ViewModel = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor")).GetExistingViewModelForSystem(System);
	if (!ViewModel)
	{
		ViewModel = MakeShared<FNiagaraSystemViewModel>();
		FNiagaraSystemViewModelOptions Options;
		Options.bCanAutoCompile = false; Options.bCanSimulate = false; Options.bIsForDataProcessingOnly = true;
		Options.MessageLogGuid = FGuid::NewGuid();
		ViewModel->Initialize(*System, Options);
	}
	ViewModel->DeleteEmitters(HandlesToRemove);

	SyncOverviewGraph(System);
	System->InvalidateCachedData();
	System->PostEditChange();
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (SaveRequested && !SaveNiagaraSystemAsset(System))
	{
		auto Failure = NiagaraSaveFailure(System);
		Failure->SetStringField(TEXT("removed_handle_id"), HandlesToRemove.Array()[0].ToString(EGuidFormats::DigitsWithHyphens));
		Failure->SetNumberField(TEXT("old_emitter_count"), OldCount); Failure->SetNumberField(TEXT("new_emitter_count"), System->GetEmitterHandles().Num());
		return Failure;
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetBoolField(TEXT("success"), true);
	ResultJson->SetBoolField(TEXT("modified"), true);
	ResultJson->SetBoolField(TEXT("saved"), SaveRequested);
	ResultJson->SetBoolField(TEXT("compiled"), System->IsValid() && System->IsReadyToRun());
	ResultJson->SetBoolField(TEXT("package_dirty"), System->GetPackage()->IsDirty());
	ResultJson->SetStringField(TEXT("removed_handle_id"), HandlesToRemove.Array()[0].ToString(EGuidFormats::DigitsWithHyphens));
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

	// Determine script usage. Emitter-scope scripts host modules like SpawnRate,
	// SpawnBurst_Instantaneous, EmitterState — placing them in particle scripts
	// causes compile errors ("Cannot Set external constant Emitter.Module.*").
	ENiagaraScriptUsage Usage;
	if (ScriptType == TEXT("spawn"))
		Usage = ENiagaraScriptUsage::ParticleSpawnScript;
	else if (ScriptType == TEXT("update"))
		Usage = ENiagaraScriptUsage::ParticleUpdateScript;
	else if (ScriptType == TEXT("emitter_spawn"))
		Usage = ENiagaraScriptUsage::EmitterSpawnScript;
	else if (ScriptType == TEXT("emitter_update"))
		Usage = ENiagaraScriptUsage::EmitterUpdateScript;
	else
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Unsupported script_type: '%s'. Use 'spawn', 'update', 'emitter_spawn', or 'emitter_update'."), *ScriptType));

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
	else if (Usage == ENiagaraScriptUsage::ParticleUpdateScript)
		TargetScript = EmitterData->UpdateScriptProps.Script;
	else if (Usage == ENiagaraScriptUsage::EmitterSpawnScript)
		TargetScript = EmitterData->EmitterSpawnScriptProps.Script;
	else if (Usage == ENiagaraScriptUsage::EmitterUpdateScript)
		TargetScript = EmitterData->EmitterUpdateScriptProps.Script;

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

	// Smart insertion: if no index specified and we're in the update stack,
	// insert before SolveForcesAndVelocity so force modules are evaluated correctly.
	if (TargetIndex == INDEX_NONE && Usage == ENiagaraScriptUsage::ParticleUpdateScript)
	{
		// Walk backward from OutputNode through parameter-map pin links to build ordered module list
		TArray<UNiagaraNodeFunctionCall*> OrderedModules;
		UEdGraphNode* WalkNode = OutputNode;
		while (WalkNode != nullptr)
		{
			UEdGraphPin* InputMapPin = nullptr;
			for (UEdGraphPin* Pin : WalkNode->Pins)
			{
				if (Pin->Direction == EGPD_Input)
				{
					FNiagaraTypeDefinition PinDef = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
					if (PinDef == FNiagaraTypeDefinition::GetParameterMapDef())
					{
						InputMapPin = Pin;
						break;
					}
				}
			}
			if (!InputMapPin || InputMapPin->LinkedTo.Num() == 0)
				break;
			UEdGraphNode* PrevNode = InputMapPin->LinkedTo[0]->GetOwningNode();
			UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(PrevNode);
			if (FuncNode)
				OrderedModules.Insert(FuncNode, 0);
			WalkNode = PrevNode;
		}

		for (int32 i = 0; i < OrderedModules.Num(); ++i)
		{
			if (OrderedModules[i]->GetFunctionName().Contains(TEXT("SolveForcesAndVelocity")))
			{
				TargetIndex = i;
				break;
			}
		}
	}

	// Add the module to the stack
	Source->NodeGraph->Modify();
	UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScript, *OutputNode, TargetIndex);
	if (!NewNode)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AddScriptModuleToStack failed"));

	// Recompile and save
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

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
	else if (ScriptType == TEXT("emitter_spawn"))
		Usage = ENiagaraScriptUsage::EmitterSpawnScript;
	else if (ScriptType == TEXT("emitter_update"))
		Usage = ENiagaraScriptUsage::EmitterUpdateScript;
	else
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported script_type: '%s'. Use 'spawn', 'update', 'emitter_spawn', or 'emitter_update'."), *ScriptType));

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
	else if (Usage == ENiagaraScriptUsage::ParticleUpdateScript)
		TargetScript = EmitterData->UpdateScriptProps.Script;
	else if (Usage == ENiagaraScriptUsage::EmitterSpawnScript)
		TargetScript = EmitterData->EmitterSpawnScriptProps.Script;
	else if (Usage == ENiagaraScriptUsage::EmitterUpdateScript)
		TargetScript = EmitterData->EmitterUpdateScriptProps.Script;

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

	// Bridge the execution chain before removing the module.
	// Niagara modules are connected in a chain via parameter map pins.
	// We must reconnect neighbors to avoid corrupting the stack.
	Source->NodeGraph->Modify();
	for (UEdGraphPin* OutPin : TargetModule->Pins)
	{
		if (OutPin->Direction != EGPD_Output || OutPin->LinkedTo.Num() == 0) continue;

		for (UEdGraphPin* InPin : TargetModule->Pins)
		{
			if (InPin->Direction != EGPD_Input || InPin->LinkedTo.Num() == 0) continue;
			if (InPin->PinType != OutPin->PinType) continue;

			// Bridge: connect upstream outputs to downstream inputs
			TArray<UEdGraphPin*> UpstreamPins = InPin->LinkedTo;
			TArray<UEdGraphPin*> DownstreamPins = OutPin->LinkedTo;
			for (UEdGraphPin* Up : UpstreamPins)
				for (UEdGraphPin* Down : DownstreamPins)
					Up->MakeLinkTo(Down);
			break; // one match per output pin
		}
	}

	// Now safely remove the disconnected node
	TargetModule->BreakAllNodeLinks();
	Source->NodeGraph->RemoveNode(TargetModule, false);

	// Recompile and save
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Module '%s' removed from %s script of emitter '%s'"), *RemovedName, *ScriptType, *EmitterName));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("removed_module"), RemovedName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_niagara_renderer_property
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetNiagaraRendererProperty(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));
	if (!Params->HasField(TEXT("property_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'property_name'"));
	if (!Params->HasField(TEXT("property_value")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'property_value'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	const FString EmitterName = Params->GetStringField(TEXT("emitter_name"));
	const FString PropertyName = Params->GetStringField(TEXT("property_name"));

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

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (Renderers.Num() == 0)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' has no renderers"), *EmitterName));

	// Select the renderer: by 'renderer_type' (class name substring, e.g. "Sprite",
	// "Mesh", "Ribbon", "Light") if provided, else by 'renderer_index' (default 0).
	UNiagaraRendererProperties* TargetRenderer = nullptr;
	int32 SelectedIndex = INDEX_NONE;

	if (Params->HasField(TEXT("renderer_type")))
	{
		const FString TypeFilter = Params->GetStringField(TEXT("renderer_type"));
		for (int32 i = 0; i < Renderers.Num(); ++i)
		{
			if (Renderers[i] && Renderers[i]->GetClass()->GetName().Contains(TypeFilter))
			{
				TargetRenderer = Renderers[i];
				SelectedIndex = i;
				break;
			}
		}
		if (!TargetRenderer)
		{
			TArray<FString> Available;
			for (const UNiagaraRendererProperties* R : Renderers)
				if (R) Available.Add(R->GetClass()->GetName());
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("No renderer matching type '%s' on emitter '%s'. Available: %s"),
				*TypeFilter, *EmitterName, *FString::Join(Available, TEXT(", "))));
		}
	}
	else
	{
		int32 Index = 0;
		if (Params->HasField(TEXT("renderer_index")))
			Index = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));
		if (Index < 0 || Index >= Renderers.Num())
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("renderer_index %d out of range (emitter '%s' has %d renderer(s))"), Index, *EmitterName, Renderers.Num()));
		TargetRenderer = Renderers[Index];
		SelectedIndex = Index;
	}

	if (!TargetRenderer)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Selected renderer is null"));

	// Apply the property via reflection (handles Material as object ref, SubImageSize
	// as a struct via ImportText, bool flags, etc.).
	TSharedPtr<FJsonValue> PropertyValue = Params->Values.FindRef(TEXT("property_value"));
	TargetRenderer->Modify();

	FString PropError;
	if (!FUnrealMCPCommonUtils::SetObjectProperty(TargetRenderer, PropertyName, PropertyValue, PropError))
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Failed to set '%s' on %s: %s"), *PropertyName, *TargetRenderer->GetClass()->GetName(), *PropError));

	TargetRenderer->PostEditChange();

	// Recompile and save
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetNumberField(TEXT("renderer_index"), SelectedIndex);
	ResultJson->SetStringField(TEXT("renderer_type"), TargetRenderer->GetClass()->GetName());
	ResultJson->SetStringField(TEXT("property"), PropertyName);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// Module input helpers (used by list_module_inputs / enable_module_input)
// ─────────────────────────────────────────────────────────────────────────────

// Map a script_type string to the emitter script + its usage.
static bool ResolveEmitterScript(FVersionedNiagaraEmitterData* EmitterData, const FString& ScriptType,
	UNiagaraScript*& OutScript, ENiagaraScriptUsage& OutUsage, FString& OutError)
{
	const FString S = ScriptType.ToLower();
	if (S == TEXT("spawn"))               { OutScript = EmitterData->SpawnScriptProps.Script;        OutUsage = ENiagaraScriptUsage::ParticleSpawnScript; }
	else if (S == TEXT("update"))         { OutScript = EmitterData->UpdateScriptProps.Script;       OutUsage = ENiagaraScriptUsage::ParticleUpdateScript; }
	else if (S == TEXT("emitter_spawn"))  { OutScript = EmitterData->EmitterSpawnScriptProps.Script; OutUsage = ENiagaraScriptUsage::EmitterSpawnScript; }
	else if (S == TEXT("emitter_update")) { OutScript = EmitterData->EmitterUpdateScriptProps.Script;OutUsage = ENiagaraScriptUsage::EmitterUpdateScript; }
	else { OutError = FString::Printf(TEXT("Invalid script_type '%s'. Legacy input editing supports spawn/update/emitter_spawn/emitter_update only. For event/simulation-stage reads use read_niagara_diagnostics with exact emitter_handle_id, script_usage_id and module_node_guid; write scopes are unchanged."), *ScriptType); return false; }
	if (!OutScript) { OutError = FString::Printf(TEXT("No %s script found on emitter"), *ScriptType); return false; }
	return true;
}

// Find a module function-call node in a script's graph by name (matches function name or display title).
static UNiagaraNodeFunctionCall* FindModuleNodeByName(UNiagaraScript* Script, const FString& ModuleName)
{
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!Source || !Source->NodeGraph) return nullptr;
	UNiagaraNodeFunctionCall* Fallback = nullptr;
	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (!FuncNode) continue;
		const FString FuncName = FuncNode->GetFunctionName();
		const FString Title = FuncNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		if (FuncName.Equals(ModuleName, ESearchCase::IgnoreCase) || Title.Equals(ModuleName, ESearchCase::IgnoreCase))
			return FuncNode;
		if (!Fallback && (FuncName.Contains(ModuleName) || Title.Contains(ModuleName)))
			Fallback = FuncNode;
	}
	return Fallback;
}

// Strip the leading "Module." namespace from an input handle string for display / matching.
static FString StripModuleNamespace(const FString& HandleString)
{
	const FString Prefix = TEXT("Module.");
	return HandleString.StartsWith(Prefix) ? HandleString.RightChop(Prefix.Len()) : HandleString;
}

// Fill a rapid-iteration-typed FNiagaraVariable's data from a JSON value.
// Supports float, int, bool, vec2, vec3/position, vec4/quat, color.
static bool WriteJsonToRapidVariable(FNiagaraVariable& Var, const TSharedPtr<FJsonValue>& JsonVal, FString& OutError)
{
	if (!JsonVal.IsValid()) { OutError = TEXT("initial_value is not a valid JSON value"); return false; }
	const FNiagaraTypeDefinition& Type = Var.GetType();
	Var.AllocateData();

	if (Type == FNiagaraTypeDefinition::GetFloatDef())
	{
		double N = 0; if (!JsonVal->TryGetNumber(N)) { OutError = TEXT("Expected a number for float input"); return false; }
		Var.SetValue<float>(static_cast<float>(N)); return true;
	}
	if (Type == FNiagaraTypeDefinition::GetIntDef())
	{
		double N = 0; if (!JsonVal->TryGetNumber(N)) { OutError = TEXT("Expected a number for int input"); return false; }
		Var.SetValue<int32>(static_cast<int32>(N)); return true;
	}
	if (Type == FNiagaraTypeDefinition::GetBoolDef())
	{
		bool B = false; if (!JsonVal->TryGetBool(B)) { OutError = TEXT("Expected a boolean for bool input"); return false; }
		FNiagaraBool NB; NB.SetValue(B); Var.SetValue<FNiagaraBool>(NB); return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Type == FNiagaraTypeDefinition::GetVec2Def())
	{
		if (!JsonVal->TryGetArray(Arr) || Arr->Num() < 2) { OutError = TEXT("Expected array of 2 numbers for vec2 input"); return false; }
		Var.SetValue<FVector2f>(FVector2f((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber())); return true;
	}
	if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
	{
		if (!JsonVal->TryGetArray(Arr) || Arr->Num() < 3) { OutError = TEXT("Expected array of 3 numbers for vec3 input"); return false; }
		Var.SetValue<FVector3f>(FVector3f((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber())); return true;
	}
	if (Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetQuatDef())
	{
		if (!JsonVal->TryGetArray(Arr) || Arr->Num() < 4) { OutError = TEXT("Expected array of 4 numbers for vec4/quat input"); return false; }
		Var.SetValue<FVector4f>(FVector4f((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber(), (*Arr)[3]->AsNumber())); return true;
	}
	if (Type == FNiagaraTypeDefinition::GetColorDef())
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (JsonVal->TryGetObject(Obj))
		{
			FLinearColor C((*Obj)->GetNumberField(TEXT("r")), (*Obj)->GetNumberField(TEXT("g")), (*Obj)->GetNumberField(TEXT("b")),
				(*Obj)->HasField(TEXT("a")) ? (*Obj)->GetNumberField(TEXT("a")) : 1.0);
			Var.SetValue<FLinearColor>(C); return true;
		}
		if (JsonVal->TryGetArray(Arr) && Arr->Num() >= 3)
		{
			FLinearColor C((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber(), Arr->Num() >= 4 ? (*Arr)[3]->AsNumber() : 1.0);
			Var.SetValue<FLinearColor>(C); return true;
		}
		OutError = TEXT("Expected {r,g,b,a} object or [r,g,b,a] array for color input"); return false;
	}

	OutError = FString::Printf(TEXT("Unsupported input type for local value: %s"), *Type.GetName());
	return false;
}

// Read a rapid-iteration parameter's current value into JSON (mirrors WriteJsonToRapidVariable types).
static TSharedPtr<FJsonValue> ReadRapidVariableToJson(FNiagaraParameterStore& Store, const FNiagaraVariable& Var)
{
	const FNiagaraTypeDefinition& Type = Var.GetType();
	if (Type == FNiagaraTypeDefinition::GetFloatDef())
		return MakeShared<FJsonValueNumber>(Store.GetParameterValue<float>(Var));
	if (Type == FNiagaraTypeDefinition::GetIntDef())
		return MakeShared<FJsonValueNumber>(Store.GetParameterValue<int32>(Var));
	if (Type == FNiagaraTypeDefinition::GetBoolDef())
		return MakeShared<FJsonValueBoolean>(Store.GetParameterValue<FNiagaraBool>(Var).GetValue());
	if (Type == FNiagaraTypeDefinition::GetVec2Def())
	{
		FVector2f V = Store.GetParameterValue<FVector2f>(Var);
		TArray<TSharedPtr<FJsonValue>> A; A.Add(MakeShared<FJsonValueNumber>(V.X)); A.Add(MakeShared<FJsonValueNumber>(V.Y));
		return MakeShared<FJsonValueArray>(A);
	}
	if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
	{
		FVector3f V = Store.GetParameterValue<FVector3f>(Var);
		TArray<TSharedPtr<FJsonValue>> A; A.Add(MakeShared<FJsonValueNumber>(V.X)); A.Add(MakeShared<FJsonValueNumber>(V.Y)); A.Add(MakeShared<FJsonValueNumber>(V.Z));
		return MakeShared<FJsonValueArray>(A);
	}
	if (Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetQuatDef())
	{
		FVector4f V = Store.GetParameterValue<FVector4f>(Var);
		TArray<TSharedPtr<FJsonValue>> A; A.Add(MakeShared<FJsonValueNumber>(V.X)); A.Add(MakeShared<FJsonValueNumber>(V.Y)); A.Add(MakeShared<FJsonValueNumber>(V.Z)); A.Add(MakeShared<FJsonValueNumber>(V.W));
		return MakeShared<FJsonValueArray>(A);
	}
	if (Type == FNiagaraTypeDefinition::GetColorDef())
	{
		FLinearColor C = Store.GetParameterValue<FLinearColor>(Var);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("r"), C.R); O->SetNumberField(TEXT("g"), C.G); O->SetNumberField(TEXT("b"), C.B); O->SetNumberField(TEXT("a"), C.A);
		return MakeShared<FJsonValueObject>(O);
	}
	return MakeShared<FJsonValueNull>();
}

// Collect the set of rapid-iteration parameter names currently present in a script store.
static TSet<FName> GetRapidParameterNameSet(FNiagaraParameterStore& Store)
{
	TSet<FName> Names;
	TArray<FNiagaraVariable> All;
	Store.GetParameters(All);
	for (const FNiagaraVariable& V : All) Names.Add(V.GetName());
	return Names;
}

// Local reimplementations of two NiagaraEditor helpers that lack NIAGARAEDITOR_API
// (so they can't be linked from another module). Logic mirrors
// FNiagaraStackGraphUtilities in UE 5.7.
static bool IsRapidIterationInputType(const FNiagaraTypeDefinition& InputType)
{
	if (!InputType.IsValid()) return false;
	if (InputType.IsStatic()) return true;
	return InputType != FNiagaraTypeDefinition::GetBoolDef() && !InputType.IsEnum()
		&& InputType != FNiagaraTypeDefinition::GetParameterMapDef() && !InputType.IsUObject();
}

static FNiagaraVariable MakeRapidIterationParameter(const FString& UniqueEmitterName, ENiagaraScriptUsage Usage,
	const FName& AliasedInputName, const FNiagaraTypeDefinition& InputType)
{
	FNiagaraVariable InputVariable(InputType, AliasedInputName);
	const bool bSystemScript = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
	return FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(InputVariable, bSystemScript ? nullptr : *UniqueEmitterName, Usage);
}

// Read-only reimplementations of unexported FNiagaraStackGraphUtilities helpers.
// Find the ParameterMapSet "override node" feeding a module/dynamic-input node: it is the
// UNiagaraNodeParameterMapSet linked to the node's parameter-map input pin. (Class matched by
// name because NiagaraNodeParameterMapSet.h is a private NiagaraEditor header.)
static UNiagaraNode* FindOverrideNodeReadOnly(UNiagaraNodeFunctionCall& FuncNode)
{
	TArray<UEdGraphPin*> InputPins;
	FuncNode.GetInputPins(InputPins);
	for (UEdGraphPin* Pin : InputPins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 1)
		{
			UEdGraphNode* Owner = Pin->LinkedTo[0]->GetOwningNode();
			if (Owner && Owner->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapSet"))
				return Cast<UNiagaraNode>(Owner);
		}
	}
	return nullptr;
}

// Find the override pin for a specific aliased input handle, without creating one.
static UEdGraphPin* FindOverridePinReadOnly(UNiagaraNodeFunctionCall& FuncNode, const FString& AliasedHandleStr)
{
	UNiagaraNode* OverrideNode = FindOverrideNodeReadOnly(FuncNode);
	if (!OverrideNode) return nullptr;
	TArray<UEdGraphPin*> InputPins;
	OverrideNode->GetInputPins(InputPins);
	for (UEdGraphPin* Pin : InputPins)
		if (Pin && Pin->PinName.ToString() == AliasedHandleStr)
			return Pin;
	return nullptr;
}

// For a dynamic-input function-call node, collect its local (rapid-iteration) sub-input values,
// e.g. RandomRangeVector's Minimum/Maximum. Values live in the owning module Script's store.
static TSharedPtr<FJsonObject> ReadDynamicInputInfo(UNiagaraNodeFunctionCall& DynNode, UNiagaraScript* Script,
	ENiagaraScriptUsage Usage, const FString& UniqueEmitterName, FCompileConstantResolver& ConstantResolver)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	const FString ScriptName = DynNode.FunctionScript ? DynNode.FunctionScript->GetName() : DynNode.GetFunctionName();
	Info->SetStringField(TEXT("name"), ScriptName);
	Info->SetStringField(TEXT("node"), DynNode.GetFunctionName());

	if (!Script) return Info;
	FNiagaraParameterStore& RapidParams = Script->RapidIterationParameters;
	const TSet<FName> ExistingRapidNames = GetRapidParameterNameSet(RapidParams);

	TArray<FNiagaraVariable> DynInputs;
	TSet<FNiagaraVariable> DynHidden;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		DynNode, DynInputs, DynHidden, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
	for (const FNiagaraVariable& DynVar : DynInputs)
	{
		if (!IsRapidIterationInputType(DynVar.GetType())) continue;
		FNiagaraParameterHandle H(DynVar.GetName());
		FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(H, &DynNode);
		FNiagaraVariable RapidVar = MakeRapidIterationParameter(UniqueEmitterName, Usage, Aliased.GetParameterHandleString(), DynVar.GetType());
		if (ExistingRapidNames.Contains(RapidVar.GetName()))
		{
			Values->SetField(StripModuleNamespace(DynVar.GetName().ToString()), ReadRapidVariableToJson(RapidParams, RapidVar));
		}
	}
	if (Values->Values.Num() > 0)
		Info->SetObjectField(TEXT("values"), Values);
	return Info;
}

// Classify what an override pin is bound to and annotate the input JSON object.
// Returns the mode string ("DynamicInput" / "Expression" / "Linked" / "Override").
static FString AnnotateOverrideMode(TSharedPtr<FJsonObject> Obj, UEdGraphPin* OverridePin, UNiagaraScript* Script,
	ENiagaraScriptUsage Usage, const FString& UniqueEmitterName, FCompileConstantResolver& ConstantResolver)
{
	if (!OverridePin || OverridePin->LinkedTo.Num() == 0)
		return FString();
	UEdGraphNode* Src = OverridePin->LinkedTo[0]->GetOwningNode();
	if (!Src) return FString();

	const FString SrcClass = Src->GetClass()->GetName();

	// UNiagaraNodeCustomHlsl derives from UNiagaraNodeFunctionCall, so check it first (by name,
	// since the ParameterMap node headers are private to NiagaraEditor).
	if (SrcClass == TEXT("NiagaraNodeCustomHlsl"))
		return TEXT("Expression");
	if (SrcClass == TEXT("NiagaraNodeParameterMapGet"))
	{
		if (OverridePin->LinkedTo[0])
			Obj->SetStringField(TEXT("linked_parameter"), OverridePin->LinkedTo[0]->PinName.ToString());
		return TEXT("Linked");
	}
	if (UNiagaraNodeFunctionCall* DynNode = Cast<UNiagaraNodeFunctionCall>(Src))
	{
		Obj->SetObjectField(TEXT("dynamic_input"), ReadDynamicInputInfo(*DynNode, Script, Usage, UniqueEmitterName, ConstantResolver));
		return TEXT("DynamicInput");
	}
	return TEXT("Override");
}

// ─────────────────────────────────────────────────────────────────────────────
// list_module_inputs
// ─────────────────────────────────────────────────────────────────────────────

static bool ReadDiagnosticStack(UNiagaraNodeOutput* Output, TArray<UNiagaraNodeFunctionCall*>& Modules)
{
	TSet<UEdGraphNode*> Visited; UNiagaraNode* Current = Output;
	while (Current && Visited.Num() < 2048)
	{
		if (Visited.Contains(Current)) return false;
		Visited.Add(Current);
		if (auto* Module = Cast<UNiagaraNodeFunctionCall>(Current)) Modules.Add(Module);
		TArray<UEdGraphPin*> Pins; Current->GetInputPins(Pins); UEdGraphPin* ParameterMap = nullptr;
		for (auto* Pin : Pins)
			if (Pin && Pin->PinType.PinSubCategoryObject == FNiagaraTypeDefinition::GetParameterMapDef().GetStruct())
			{ if (ParameterMap) return false; ParameterMap = Pin; }
		if (!ParameterMap) { Algo::Reverse(Modules); return Current->IsA<UNiagaraNodeInput>(); }
		if (ParameterMap->LinkedTo.Num() != 1 || !ParameterMap->LinkedTo[0]) return false;
		Current = Cast<UNiagaraNode>(ParameterMap->LinkedTo[0]->GetOwningNode());
	}
	return false;
}

static TArray<TSharedPtr<FJsonValue>> ReadDiagnosticInputs(FVersionedNiagaraEmitter Emitter, UNiagaraScript* Script,
	UNiagaraNodeFunctionCall* Module, TSet<FGuid>& Visited, int32 Depth, int32& Budget)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!Script || !Module) return Result;
	if (Depth > 6 || --Budget < 0 || Visited.Contains(Module->NodeGuid)) { Budget = -1; return Result; }
	Visited.Add(Module->NodeGuid);
	FCompileConstantResolver Resolver(Emitter, Script->GetUsage());
	TArray<FNiagaraVariable> Variables; TSet<FNiagaraVariable> Hidden;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Module, Variables, Hidden, Resolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);
	const auto RapidNames = GetRapidParameterNameSet(Script->RapidIterationParameters);
	for (const auto& Variable : Variables)
	{
		if (--Budget < 0) break;
		auto Row = MakeShared<FJsonObject>(); const auto Type = Variable.GetType();
		Row->SetStringField(TEXT("name"), Variable.GetName().ToString()); Row->SetStringField(TEXT("type"), Type.GetName());
		Row->SetBoolField(TEXT("hidden"), Hidden.Contains(Variable)); Row->SetBoolField(TEXT("static"), Type.IsStatic());
		Row->SetStringField(TEXT("time_domain"), TEXT("unresolved: determined by actual graph bindings, not inferred from module name"));
		Row->SetStringField(TEXT("units"), TEXT("unresolved: no unit conversion or time-domain inference"));
		FNiagaraParameterHandle Handle(Variable.GetName());
		const auto Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(Handle, Module);
		auto* Override = FindOverridePinReadOnly(*Module, Aliased.GetParameterHandleString().ToString());
		Row->SetBoolField(TEXT("has_explicit_override"), Override != nullptr);
		FString Origin = TEXT("script_default");
		Row->SetBoolField(TEXT("value_resolved"), false);
		UEdGraphPin* ValuePin = Override;
		if (!ValuePin) Row->SetStringField(TEXT("default_access"), TEXT("unresolved: script default pin API is not exported; see exact function/version and static switch pins"));
		if (ValuePin)
		{
			Row->SetStringField(TEXT("raw_default"), ValuePin->GetDefaultAsString());
			Row->SetStringField(TEXT("value_pin_path"), ValuePin->GetOwningNode()->GetPathName());
			Row->SetStringField(TEXT("value_pin_id"), ValuePin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
			if (ValuePin->LinkedTo.IsEmpty())
			{
				float Scalar; int32 Integer; const FString Raw = ValuePin->GetDefaultAsString();
				if (Type == FNiagaraTypeDefinition::GetFloatDef() && LexTryParseString(Scalar, *Raw) && FMath::IsFinite(Scalar))
				{ Row->SetNumberField(TEXT("value"), Scalar); Row->SetBoolField(TEXT("value_resolved"), true); }
				else if (Type == FNiagaraTypeDefinition::GetIntDef() && LexTryParseString(Integer, *Raw))
				{ Row->SetNumberField(TEXT("value"), Integer); Row->SetBoolField(TEXT("value_resolved"), true); }
			}
		}
		if (!Override && ValuePin && !ValuePin->LinkedTo.IsEmpty()) Override = ValuePin;
		if (Override)
		{
			Row->SetStringField(TEXT("override_pin"), Override->PinName.ToString());
			Row->SetStringField(TEXT("raw_default"), Override->GetDefaultAsString());
			Origin = Override->LinkedTo.IsEmpty() ? TEXT("local_pin") : TEXT("unresolved_override");
			if (Override->LinkedTo.Num() == 1)
			{
				auto* Source = Override->LinkedTo[0]->GetOwningNode();
				if (Source)
				{
					Row->SetStringField(TEXT("source_node_guid"), Source->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
					Row->SetStringField(TEXT("source_node_path"), Source->GetPathName());
					Row->SetStringField(TEXT("source_pin"), Override->LinkedTo[0]->PinName.ToString());
					if (Source->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
					{ Origin = TEXT("linked_parameter"); Row->SetStringField(TEXT("linked_parameter"), Override->LinkedTo[0]->PinName.ToString()); }
					else if (Source->GetClass()->GetName() == TEXT("NiagaraNodeCustomHlsl")) Origin = TEXT("expression_unresolved");
					else if (auto* Dynamic = Cast<UNiagaraNodeFunctionCall>(Source))
					{
						Origin = TEXT("dynamic_input"); Row->SetStringField(TEXT("function_path"), Dynamic->FunctionScript ? Dynamic->FunctionScript->GetPathName() : TEXT("unavailable"));
						Row->SetArrayField(TEXT("inputs"), ReadDiagnosticInputs(Emitter, Script, Dynamic, Visited, Depth + 1, Budget));
					}
					else if (Source->IsA<UNiagaraNodeInput>()) Origin = Type.IsDataInterface() ? TEXT("data_interface") : TEXT("input_node_unresolved");
				}
			}
			else if (Override->LinkedTo.Num() > 1) Origin = TEXT("ambiguous_multiple_links");
		}
		if ((Origin == TEXT("script_default") || Origin == TEXT("local_pin")) && IsRapidIterationInputType(Type))
		{
			const auto Rapid = MakeRapidIterationParameter(Emitter.Emitter->GetUniqueEmitterName(), Script->GetUsage(), Aliased.GetParameterHandleString(), Type);
			if (RapidNames.Contains(Rapid.GetName()))
			{
				Origin = TEXT("rapid_iteration"); Row->SetStringField(TEXT("rapid_parameter"), Rapid.GetName().ToString());
				auto Value = ReadRapidVariableToJson(Script->RapidIterationParameters, Rapid); Row->SetField(TEXT("value"), Value);
				Row->SetBoolField(TEXT("value_resolved"), Value.IsValid() && Value->Type != EJson::Null);
			}
		}
		if (const UEnum* Enum = Type.GetEnum())
		{
			auto EnumInfo = MakeShared<FJsonObject>(); EnumInfo->SetStringField(TEXT("path"), Enum->GetPathName());
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (int32 Index = 0; Index < FMath::Min(Enum->NumEnums(), 128); ++Index)
			{
				auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
				Entry->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Index)); Entry->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(Index).ToString());
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
			EnumInfo->SetArrayField(TEXT("entries"), Entries); EnumInfo->SetBoolField(TEXT("entries_truncated"), Enum->NumEnums() > 128);
			if (ValuePin && ValuePin->LinkedTo.IsEmpty())
			{
				const FString Raw = ValuePin->GetDefaultAsString(); int64 Numeric;
				const int64 EnumValue = LexTryParseString(Numeric, *Raw) ? Numeric : Enum->GetValueByNameString(Raw);
				const int32 Index = Enum->GetIndexByValue(EnumValue);
				if (Index != INDEX_NONE)
				{ EnumInfo->SetStringField(TEXT("selected_name"), Enum->GetNameStringByIndex(Index)); EnumInfo->SetStringField(TEXT("selected_display_name"), Enum->GetDisplayNameTextByIndex(Index).ToString()); EnumInfo->SetNumberField(TEXT("selected_value"), EnumValue); Row->SetBoolField(TEXT("value_resolved"), true); }
			}
			Row->SetObjectField(TEXT("enum"), EnumInfo);
		}
		Row->SetStringField(TEXT("origin"), Origin); Result.Add(MakeShared<FJsonValueObject>(Row));
	}
	Visited.Remove(Module->NodeGuid); return Result;
}

namespace MCPSolo
{
static bool Busy()
{
	return !IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor ||
		GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask || FAssetCompilingManager::Get().GetNumRemainingAssets() > 0 ||
		(FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid());
}

static bool Snapshot(UNiagaraSystem* System, TSharedPtr<FJsonObject>& State, TArray<UNiagaraComponent*>& Components, FString& Error)
{
	if (System->GetEmitterHandles().Num() > 256) { Error = TEXT("Solo supports at most 256 emitter Handles"); return false; }
	State = MakeShared<FJsonObject>(); Components.Reset();
	State->SetBoolField(TEXT("isolation_enabled"), System->GetIsolateEnabled());
	TArray<TSharedPtr<FJsonValue>> Handles, Isolated, Affected; TSet<FGuid> Ids;
	bool AnyIsolated = false;
	static const FString Session = FGuid::NewGuid().ToString();
	FString Witness = Session + TEXT("|") + System->GetPathName() + FString::FromInt(System->GetUniqueID()) + (System->GetIsolateEnabled() ? TEXT("|1") : TEXT("|0"));
	for (const auto& Handle : System->GetEmitterHandles())
	{
		if (!Handle.GetId().IsValid() || Ids.Contains(Handle.GetId())) { Error = TEXT("Invalid or ambiguous emitter Handle identity"); return false; }
		Ids.Add(Handle.GetId()); const FString Id = Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens);
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("handle_id"), Id); Row->SetStringField(TEXT("name"), Handle.GetName().ToString());
		Row->SetBoolField(TEXT("isolated"), Handle.IsIsolated()); Row->SetBoolField(TEXT("authored_enabled"), Handle.GetIsEnabled());
		Row->SetStringField(TEXT("version_guid"), Handle.GetInstance().Version.ToString(EGuidFormats::DigitsWithHyphens));
		Handles.Add(MakeShared<FJsonValueObject>(Row));
		if (Handle.IsIsolated()) { AnyIsolated = true; Isolated.Add(MakeShared<FJsonValueString>(Id)); }
		FString Encoded; auto Writer = TJsonWriterFactory<>::Create(&Encoded); FJsonSerializer::Serialize(Row, Writer); Witness += TEXT("|") + Encoded;
	}
	if (AnyIsolated != System->GetIsolateEnabled()) { Error = TEXT("Inconsistent native isolation flags; inspect/correct through the Niagara editor before using Solo"); return false; }
	for (TObjectIterator<UNiagaraComponent> Component; Component; ++Component)
	{
		if (!IsValid(*Component) || Component->IsTemplate() || !Component->IsRegistered() || Component->GetAsset() != System) continue;
		if (Component->GetWorld() && Component->GetWorld()->IsGameWorld()) { Error = TEXT("A matching game-world component is present; Solo is editor-only"); return false; }
		if (Components.Num() >= 128) { Error = TEXT("More than 128 affected components; Solo refuses rather than hiding impact"); return false; }
		Components.Add(*Component);
	}
	Components.Sort([](const UNiagaraComponent& Left, const UNiagaraComponent& Right) { return Left.GetPathName() < Right.GetPathName(); });
	for (auto* Component : Components)
	{
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("object_path"), Component->GetPathName());
		Row->SetStringField(TEXT("world_path"), Component->GetWorld() ? Component->GetWorld()->GetPathName() : TEXT("unavailable"));
		Row->SetBoolField(TEXT("active"), Component->IsActive()); Affected.Add(MakeShared<FJsonValueObject>(Row));
		Witness += TEXT("|") + Component->GetPathName() + TEXT(":") + FString::FromInt(Component->GetUniqueID());
	}
	FTCHARToUTF8 Bytes(*Witness); uint8 Digest[FSHA1::DigestSize]; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Digest);
	State->SetStringField(TEXT("baseline"), BytesToHex(Digest, FSHA1::DigestSize));
	State->SetArrayField(TEXT("emitter_handle_ids"), Isolated); State->SetArrayField(TEXT("emitters"), Handles); State->SetArrayField(TEXT("affected_components"), Affected);
	return true;
}
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetNiagaraEmitterSolo(const TSharedPtr<FJsonObject>& Params)
{
	auto Fail = [](const FString& Message)
	{
		auto Result = FUnrealMCPCommonUtils::CreateErrorResponse(Message); Result->SetBoolField(TEXT("success"), false);
		Result->SetNumberField(TEXT("solo_contract"), 1); Result->SetStringField(TEXT("stage"), TEXT("preflight"));
		Result->SetStringField(TEXT("execution_state"), TEXT("not_started")); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
	};
	if (MCPSolo::Busy()) return Fail(TEXT("Solo requires idle editor mode, without PIE/simulation, saving, GC, modal dialogs or asset compilation"));
	const TSet<FString> Allowed{TEXT("asset_path"), TEXT("emitter_handle_ids"), TEXT("expected_baseline"), TEXT("dry_run")};
	if (!Params) return Fail(TEXT("Missing Solo arguments"));
	for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) return Fail(TEXT("Unknown Solo argument: ") + Pair.Key);
	FString Path, Expected; bool DryRun = true; const TArray<TSharedPtr<FJsonValue>>* Requested;
	if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/")) || Path.Contains(TEXT(".")) || Path.Len() >= NAME_SIZE || !FPackageName::IsValidLongPackageName(Path))
		return Fail(TEXT("Require an exact /Game NiagaraSystem package path"));
	if (!Params->TryGetArrayField(TEXT("emitter_handle_ids"), Requested) || Requested->Num() > 256) return Fail(TEXT("emitter_handle_ids must be an array of at most 256 exact GUIDs; [] clears Solo"));
	if ((Params->HasField(TEXT("expected_baseline")) && !Params->TryGetStringField(TEXT("expected_baseline"), Expected)) || (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))) return Fail(TEXT("Invalid baseline or dry_run type"));
	TSet<FGuid> Desired;
	for (const auto& Value : *Requested)
	{
		FString Text; FGuid Id;
		if (!Value.IsValid() || !Value->TryGetString(Text) || !FGuid::ParseExact(Text, EGuidFormats::DigitsWithHyphens, Id) || !Id.IsValid() || Desired.Contains(Id))
			return Fail(TEXT("Require unique nonzero hyphenated emitter Handle GUIDs, not names"));
		Desired.Add(Id);
	}
	if (!UEditorAssetLibrary::DoesAssetExist(Path)) return Fail(TEXT("NiagaraSystem not found"));
	auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Path));
	if (!System || System->IsTemplate() || System->GetPackage()->GetName() != Path) return Fail(TEXT("Exact NiagaraSystem not found; no redirector fallback"));
	if (MCPSolo::Busy()) return Fail(TEXT("Editor became busy while loading; inspect state before retrying"));
	TSharedPtr<FJsonObject> Before; TArray<UNiagaraComponent*> Components; FString Error;
	if (!MCPSolo::Snapshot(System, Before, Components, Error)) return Fail(Error);
	int32 Found = 0; bool Changed = System->GetIsolateEnabled() != !Desired.IsEmpty();
	TArray<TSharedPtr<FJsonValue>> ProposedIds;
	for (const auto& Handle : System->GetEmitterHandles())
	{
		const bool Isolated = Desired.Contains(Handle.GetId()); if (Isolated) { ++Found; ProposedIds.Add(MakeShared<FJsonValueString>(Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens))); }
		Changed |= Isolated != Handle.IsIsolated();
	}
	if (Found != Desired.Num()) return Fail(TEXT("An exact emitter Handle was not found; no display-name fallback"));
	if ((!DryRun && Expected.IsEmpty()) || (!Expected.IsEmpty() && Expected != Before->GetStringField(TEXT("baseline"))))
	{
		auto Result = Fail(TEXT("Missing/stale Solo baseline; preview again to protect UI state and affected instances")); Result->SetObjectField(TEXT("before"), Before); return Result;
	}
	const bool DirtyBefore = System->GetPackage()->IsDirty();
	auto After = Before;
	if (!DryRun && Changed)
	{
		for (auto& Handle : System->GetEmitterHandles()) Handle.SetIsolated(Desired.Contains(Handle.GetId()));
		System->SetIsolateEnabled(!Desired.IsEmpty());
		for (auto* Component : Components) Component->MarkRenderStateDirty();
		if (!MCPSolo::Snapshot(System, After, Components, Error))
		{
			auto Result = Fail(Error); Result->SetStringField(TEXT("stage"), TEXT("verify")); Result->SetStringField(TEXT("execution_state"), TEXT("completed"));
			Result->SetBoolField(TEXT("modified"), true); Result->SetBoolField(TEXT("after_unavailable"), true); Result->SetBoolField(TEXT("package_dirty"), System->GetPackage()->IsDirty()); return Result;
		}
	}
	auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), Error.IsEmpty() && DirtyBefore == System->GetPackage()->IsDirty());
	Result->SetNumberField(TEXT("solo_contract"), 1); Result->SetStringField(TEXT("asset_path"), Path); Result->SetBoolField(TEXT("dry_run"), DryRun);
	Result->SetBoolField(TEXT("modified"), !DryRun && Changed); Result->SetBoolField(TEXT("would_modify"), Changed); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("compiled"), false);
	Result->SetBoolField(TEXT("session_only"), true); Result->SetBoolField(TEXT("undo_supported"), false); Result->SetBoolField(TEXT("affects_all_instances"), true);
	Result->SetBoolField(TEXT("package_dirty"), System->GetPackage()->IsDirty()); Result->SetObjectField(TEXT("before"), Before); Result->SetObjectField(TEXT("after"), After);
	Result->SetArrayField(TEXT("affected_components"), Before->GetArrayField(TEXT("affected_components"))); Result->SetStringField(TEXT("baseline"), After->GetStringField(TEXT("baseline")));
	auto Proposed = MakeShared<FJsonObject>(); Proposed->SetBoolField(TEXT("isolation_enabled"), !Desired.IsEmpty()); Proposed->SetArrayField(TEXT("emitter_handle_ids"), ProposedIds); Result->SetObjectField(TEXT("proposed"), Proposed);
	auto Restore = MakeShared<FJsonObject>(); Restore->SetStringField(TEXT("asset_path"), Path); Restore->SetArrayField(TEXT("emitter_handle_ids"), Before->GetArrayField(TEXT("emitter_handle_ids")));
	Restore->SetStringField(TEXT("expected_baseline"), After->GetStringField(TEXT("baseline"))); Restore->SetBoolField(TEXT("dry_run"), false); Result->SetObjectField(TEXT("restore_arguments"), Restore);
	Result->SetStringField(TEXT("warning"), TEXT("Native Solo changes ALL registered instances of this asset, including scene VFX, not only the Niagara preview. No simulation reset, dependency stop, asset save or persistent Undo. Restore explicitly with a fresh baseline; never blindly retry unknown outcomes."));
	if (!Result->GetBoolField(TEXT("success"))) { Result->SetStringField(TEXT("stage"), TEXT("verify")); Result->SetStringField(TEXT("error"), Error.IsEmpty() ? TEXT("Package dirty state changed during Solo") : Error); }
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleReadNiagaraDiagnostics(const TSharedPtr<FJsonObject>& Params)
{
	const TSet<FString> Allowed{TEXT("asset_path"), TEXT("emitter_handle_id"), TEXT("script_type"), TEXT("script_usage_id"), TEXT("module_node_guid"), TEXT("include_inputs")};
	if (!Params) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing diagnostic arguments"));
	for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Unknown diagnostic argument: ") + Pair.Key);
	FString Path, HandleText, StageFilter, UsageText, NodeText; bool IncludeInputs = true;
	if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/")) || Path.Contains(TEXT(".")) || Path.Len() >= NAME_SIZE || !FPackageName::IsValidLongPackageName(Path))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Require an exact /Game NiagaraSystem package path, not an inline object path"));
	Params->TryGetStringField(TEXT("emitter_handle_id"), HandleText); Params->TryGetStringField(TEXT("script_type"), StageFilter);
	Params->TryGetStringField(TEXT("script_usage_id"), UsageText); Params->TryGetStringField(TEXT("module_node_guid"), NodeText);
	if (Params->HasField(TEXT("include_inputs")) && !Params->TryGetBoolField(TEXT("include_inputs"), IncludeInputs)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("include_inputs must be boolean"));
	FGuid HandleId, UsageId, NodeId;
	auto Guid = [](const FString& Text, FGuid& Value) { return Text.IsEmpty() || (FGuid::ParseExact(Text, EGuidFormats::DigitsWithHyphens, Value) && Value.IsValid()); };
	if (!Guid(HandleText, HandleId) || !Guid(UsageText, UsageId) || !Guid(NodeText, NodeId) || ((!UsageText.IsEmpty() || !NodeText.IsEmpty()) && HandleText.IsEmpty()))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Require nonzero hyphenated GUIDs and exact emitter Handle for Usage/module selection"));
	const TSet<FString> Stages{TEXT(""), TEXT("spawn"), TEXT("update"), TEXT("emitter_spawn"), TEXT("emitter_update"), TEXT("event"), TEXT("simulation_stage")};
	if (!Stages.Contains(StageFilter)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Unknown diagnostic script_type; use spawn/update/emitter_spawn/emitter_update/event/simulation_stage"));
	if ((!NodeText.IsEmpty() && StageFilter.IsEmpty()) || (!NodeText.IsEmpty() && (StageFilter == TEXT("event") || StageFilter == TEXT("simulation_stage")) && UsageText.IsEmpty()))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Module selection requires exact script_type; event/simulation-stage modules additionally require script_usage_id"));
	auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Path));
	if (!System || System->GetPackage()->GetName() != Path) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Exact NiagaraSystem not found"));
	auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true); Result->SetNumberField(TEXT("diagnostics_contract"), 1);
	Result->SetStringField(TEXT("asset_path"), Path); Result->SetStringField(TEXT("object_path"), System->GetPathName());
	Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), System->GetPackage()->IsDirty());
	auto Coverage = MakeShared<FJsonObject>(); Coverage->SetStringField(TEXT("authored_data"), TEXT("selected versions, saved event settings and renderer enabled flags"));
	Coverage->SetStringField(TEXT("execution_stack"), TEXT("ordered parameter-map stack from exact usage/Usage ID output, not all shared graph nodes"));
	Coverage->SetStringField(TEXT("editor_preview"), TEXT("current system/handle isolation plus existing component identities; not full visibility/culling evaluation"));
	Coverage->SetStringField(TEXT("runtime"), TEXT("unsupported")); Coverage->SetStringField(TEXT("external_dependencies"), TEXT("references only; materials/textures/conversion logs not expanded"));
	Result->SetObjectField(TEXT("coverage"), Coverage); Result->SetBoolField(TEXT("isolation_enabled"), System->GetIsolateEnabled());
	TArray<TSharedPtr<FJsonValue>> Emitters, Handlers, Unsupported, Components;
	bool Complete = true, FoundHandle = HandleText.IsEmpty(), FoundUsage = UsageText.IsEmpty(), FoundNode = NodeText.IsEmpty(); int32 Budget = 8192, HandleMatches = 0, UsageMatches = 0, NodeMatches = 0;
	auto Missing = [&](const FString& Identity, const FString& Reason)
	{ Complete = false; if (Unsupported.Num() < 128) { auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("identity"), Identity); Row->SetStringField(TEXT("reason"), Reason); Unsupported.Add(MakeShared<FJsonValueObject>(Row)); } };
	for (const auto& Handle : System->GetEmitterHandles())
	{
		if (!HandleText.IsEmpty() && Handle.GetId() != HandleId) continue;
		FoundHandle = true; ++HandleMatches; const auto Emitter = Handle.GetInstance(); auto* Data = Emitter.GetEmitterData();
		auto Row = MakeShared<FJsonObject>(); const FString Id = Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens);
		Row->SetStringField(TEXT("handle_id"), Id); Row->SetStringField(TEXT("name"), Handle.GetName().ToString());
		Row->SetBoolField(TEXT("authored_enabled"), Handle.GetIsEnabled()); Row->SetBoolField(TEXT("isolated"), Handle.IsIsolated());
		Row->SetBoolField(TEXT("render_excluded_by_isolation"), System->GetIsolateEnabled() && !Handle.IsIsolated());
		Row->SetStringField(TEXT("simulation_state"), TEXT("unresolved: isolation filtering is not proof simulation stopped"));
		if (!Data || !Emitter.Emitter) { Missing(Id, TEXT("Emitter data unavailable or stateless; no stateful execution reconstruction")); Emitters.Add(MakeShared<FJsonValueObject>(Row)); continue; }
		Row->SetStringField(TEXT("emitter_path"), Emitter.Emitter->GetPathName()); Row->SetStringField(TEXT("version_guid"), Emitter.Version.ToString(EGuidFormats::DigitsWithHyphens));
		Row->SetStringField(TEXT("simulation_target"), Data->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));
		TArray<TSharedPtr<FJsonValue>> Renderers, Stacks;
		const auto& NativeRenderers = Data->GetRenderers();
		for (int32 Index = 0; Index < NativeRenderers.Num(); ++Index)
		{
			const auto* Renderer = NativeRenderers[Index]; if (!Renderer) continue;
			auto RendererRow = MakeShared<FJsonObject>(); RendererRow->SetNumberField(TEXT("index"), Index); RendererRow->SetStringField(TEXT("path"), Renderer->GetPathName());
			RendererRow->SetStringField(TEXT("class"), Renderer->GetClass()->GetPathName()); RendererRow->SetBoolField(TEXT("authored_enabled"), Renderer->GetIsEnabled());
			RendererRow->SetStringField(TEXT("visibility"), TEXT("unresolved: renderer enabled/isolation are not complete runtime culling")); Renderers.Add(MakeShared<FJsonValueObject>(RendererRow));
		}
		Row->SetArrayField(TEXT("renderers"), Renderers); Row->SetBoolField(TEXT("has_renderer"), !NativeRenderers.IsEmpty());
		auto Stack = [&](UNiagaraScript* Script, const FString& Stage)
		{
			if (!StageFilter.IsEmpty() && StageFilter != Stage) return;
			if (!Script) { Missing(Id + TEXT("/") + Stage, TEXT("Missing saved script")); return; }
			if (!UsageText.IsEmpty() && Script->GetUsageId() != UsageId) return;
			FoundUsage = true; ++UsageMatches; auto StackRow = MakeShared<FJsonObject>();
			StackRow->SetStringField(TEXT("script_type"), Stage); StackRow->SetStringField(TEXT("script_path"), Script->GetPathName());
			StackRow->SetStringField(TEXT("usage"), StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage())));
			StackRow->SetStringField(TEXT("usage_id"), Script->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens));
			auto* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource()); auto* Graph = Source ? Source->NodeGraph.Get() : nullptr;
			UNiagaraNodeOutput* Output = nullptr; int32 Matches = 0;
			if (Graph)
			{
				StackRow->SetStringField(TEXT("graph_path"), Graph->GetPathName());
				for (UEdGraphNode* GraphNode : Graph->Nodes)
					if (auto* Candidate = Cast<UNiagaraNodeOutput>(GraphNode))
						if (UNiagaraScript::IsEquivalentUsage(Candidate->GetUsage(), Script->GetUsage()) && Candidate->GetUsageId() == Script->GetUsageId()) { Output = Candidate; ++Matches; }
			}
			if (Matches != 1) { Missing(Script->GetPathName(), TEXT("Missing/ambiguous exact usage output; never use another stage or shared graph node list")); StackRow->SetBoolField(TEXT("complete"), false); }
			else
			{
				StackRow->SetStringField(TEXT("output_node_guid"), Output->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)); TArray<UNiagaraNodeFunctionCall*> Modules;
				const bool StackResolved = ReadDiagnosticStack(Output, Modules); TArray<TSharedPtr<FJsonValue>> Ordered;
				if (!StackResolved) { Missing(Script->GetPathName(), TEXT("Cyclic/ambiguous/disconnected parameter-map execution chain")); Modules.Empty(); }
				if (Modules.Num() > 256) { Missing(Script->GetPathName(), TEXT("Stack exceeds 256 modules")); Modules.SetNum(256); }
				for (int32 Index = 0; Index < Modules.Num(); ++Index)
				{
					auto* Module = Modules[Index]; if (!Module) continue; if (!NodeText.IsEmpty() && Module->NodeGuid != NodeId) continue;
					FoundNode = true; ++NodeMatches; auto ModuleRow = MakeShared<FJsonObject>(); ModuleRow->SetNumberField(TEXT("stack_index"), Index);
					ModuleRow->SetStringField(TEXT("name"), Module->GetFunctionName()); ModuleRow->SetStringField(TEXT("node_guid"), Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
					ModuleRow->SetStringField(TEXT("node_path"), Module->GetPathName()); ModuleRow->SetBoolField(TEXT("enabled"), Module->IsNodeEnabled());
					ModuleRow->SetStringField(TEXT("function_path"), Module->FunctionScript ? Module->FunctionScript->GetPathName() : TEXT("unavailable"));
					ModuleRow->SetStringField(TEXT("selected_version_guid"), Module->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens));
					const auto* Version = Module->FunctionScript ? Module->FunctionScript->GetScriptData(Module->SelectedScriptVersion) : nullptr;
					ModuleRow->SetStringField(TEXT("version_status"), Version ? TEXT("resolved") : TEXT("unresolved"));
					if (Version) ModuleRow->SetStringField(TEXT("resolved_version_guid"), Version->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
					FCompileConstantResolver Resolver(Emitter, Script->GetUsage()); TArray<UEdGraphPin*> Switches; TSet<UEdGraphPin*> Hidden;
					FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*Module, Switches, Hidden, Resolver);
					TArray<TSharedPtr<FJsonValue>> StaticPins;
					if (Switches.Num() > 256) { Missing(Module->GetPathName(), TEXT("Static switches exceed 256 pins")); Switches.SetNum(256); }
					for (const auto* Pin : Switches)
					{
						if (!Pin) continue; auto PinRow = MakeShared<FJsonObject>(); PinRow->SetStringField(TEXT("name"), Pin->PinName.ToString());
						PinRow->SetStringField(TEXT("raw_default"), Pin->GetDefaultAsString()); PinRow->SetBoolField(TEXT("hidden"), Hidden.Contains(const_cast<UEdGraphPin*>(Pin)));
						PinRow->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
						if (auto* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
						{
							PinRow->SetStringField(TEXT("enum_path"), Enum->GetPathName()); int64 Numeric; const FString Raw = Pin->GetDefaultAsString();
							const int64 Value = LexTryParseString(Numeric, *Raw) ? Numeric : Enum->GetValueByNameString(Raw); const int32 EnumIndex = Enum->GetIndexByValue(Value);
							if (EnumIndex != INDEX_NONE) { PinRow->SetNumberField(TEXT("enum_value"), Value); PinRow->SetStringField(TEXT("enum_name"), Enum->GetNameStringByIndex(EnumIndex)); PinRow->SetStringField(TEXT("enum_display_name"), Enum->GetDisplayNameTextByIndex(EnumIndex).ToString()); }
						}
						StaticPins.Add(MakeShared<FJsonValueObject>(PinRow));
					}
					ModuleRow->SetArrayField(TEXT("static_switch_pins"), StaticPins);
					if (IncludeInputs) { TSet<FGuid> Visited; ModuleRow->SetArrayField(TEXT("inputs"), ReadDiagnosticInputs(Emitter, Script, Module, Visited, 0, Budget)); }
					Ordered.Add(MakeShared<FJsonValueObject>(ModuleRow));
				}
				StackRow->SetArrayField(TEXT("modules"), Ordered); StackRow->SetBoolField(TEXT("complete"), StackResolved && Budget >= 0);
			}
			Stacks.Add(MakeShared<FJsonValueObject>(StackRow));
		};
		Stack(Data->EmitterSpawnScriptProps.Script, TEXT("emitter_spawn")); Stack(Data->EmitterUpdateScriptProps.Script, TEXT("emitter_update"));
		Stack(Data->SpawnScriptProps.Script, TEXT("spawn")); Stack(Data->UpdateScriptProps.Script, TEXT("update"));
		for (const auto& Event : Data->GetEventHandlers())
		{
			if (!Event.Script) { Missing(Id, TEXT("Null event handler script")); continue; }
			if ((!StageFilter.IsEmpty() && StageFilter != TEXT("event")) || (!UsageText.IsEmpty() && Event.Script->GetUsageId() != UsageId)) continue;
			auto EventRow = MakeShared<FJsonObject>(); EventRow->SetStringField(TEXT("emitter_handle_id"), Id); EventRow->SetStringField(TEXT("script_path"), Event.Script->GetPathName());
			EventRow->SetStringField(TEXT("usage_id"), Event.Script->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens)); EventRow->SetStringField(TEXT("source_event_name"), Event.SourceEventName.ToString());
			EventRow->SetStringField(TEXT("execution_mode"), StaticEnum<EScriptExecutionMode>()->GetNameStringByValue(static_cast<int64>(Event.ExecutionMode)));
			EventRow->SetNumberField(TEXT("spawn_number"), Event.SpawnNumber); EventRow->SetNumberField(TEXT("max_events_per_frame"), Event.MaxEventsPerFrame);
			EventRow->SetBoolField(TEXT("random_spawn_number"), Event.bRandomSpawnNumber); EventRow->SetNumberField(TEXT("min_spawn_number"), Event.MinSpawnNumber);
			EventRow->SetStringField(TEXT("source_emitter_id"), Event.SourceEmitterID.ToString(EGuidFormats::DigitsWithHyphens));
			const FGuid ResolvedSource = Event.SourceEmitterID.IsValid() ? Event.SourceEmitterID : Handle.GetId();
			int32 SourceMatches = 0; for (const auto& SourceHandle : System->GetEmitterHandles()) if (SourceHandle.GetId() == ResolvedSource) { ++SourceMatches; EventRow->SetStringField(TEXT("source_emitter_name"), SourceHandle.GetName().ToString()); }
			EventRow->SetStringField(TEXT("resolved_source_handle_id"), ResolvedSource.ToString(EGuidFormats::DigitsWithHyphens)); EventRow->SetStringField(TEXT("source_resolution"), SourceMatches == 1 ? TEXT("resolved") : TEXT("missing_or_ambiguous"));
			if (SourceMatches != 1) Missing(Id, TEXT("Event source Handle missing/ambiguous"));
			EventRow->SetStringField(TEXT("event_counts"), TEXT("unsupported: authored limits are not runtime production/consumption counts"));
			Handlers.Add(MakeShared<FJsonValueObject>(EventRow)); Stack(Event.Script, TEXT("event"));
		}
		for (const auto* SimulationStage : Data->GetSimulationStages()) if (SimulationStage) Stack(SimulationStage->Script, TEXT("simulation_stage"));
		Row->SetArrayField(TEXT("stacks"), Stacks); Emitters.Add(MakeShared<FJsonValueObject>(Row));
	}
	if (!FoundHandle || !FoundUsage || !FoundNode) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Exact Handle/Usage ID/module not found in the selected execution stack; no name or stage fallback"));
	if ((!HandleText.IsEmpty() && HandleMatches != 1) || (!UsageText.IsEmpty() && UsageMatches != 1) || (!NodeText.IsEmpty() && NodeMatches != 1))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Ambiguous duplicate Handle/Usage ID/module identity in selected execution stacks"));
	if (Budget < 0) Missing(Path, TEXT("Input traversal exceeds 8192 values or six dynamic levels"));
	for (TObjectIterator<UNiagaraComponent> Component; Component; ++Component)
	{
		if (Component->GetAsset() != System || Component->IsTemplate()) continue;
		if (Components.Num() >= 128) { Missing(Path, TEXT("More than 128 existing components; preview identities truncated")); break; }
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("object_path"), Component->GetPathName());
		Row->SetBoolField(TEXT("active"), Component->IsActive()); Row->SetBoolField(TEXT("registered"), Component->IsRegistered());
		Row->SetStringField(TEXT("world_path"), Component->GetWorld() ? Component->GetWorld()->GetPathName() : TEXT("unavailable"));
		Row->SetStringField(TEXT("particle_snapshot"), TEXT("unsupported: no simulation tick/reset or GPU readback")); Components.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("emitters"), Emitters); Result->SetArrayField(TEXT("event_handlers"), Handlers); Result->SetArrayField(TEXT("components"), Components);
	Result->SetStringField(TEXT("preview_instance_status"), Components.IsEmpty() ? TEXT("no_existing_component") : TEXT("existing_components_observed"));
	Result->SetArrayField(TEXT("unsupported"), Unsupported); Result->SetBoolField(TEXT("complete"), Complete);
	Result->SetStringField(TEXT("scope"), TEXT("Stored selected execution stacks plus current editor isolation only; no runtime reconstruction, visibility proof, conversion-log parsing or asset mutation."));
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "AssetCompilingManager.h"
#include "NiagaraRibbonRendererProperties.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraSoloTest, "UnrealMCP.Niagara.Solo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPNiagaraSoloTest::RunTest(const FString& Parameters)
{
	const FString Path = TEXT("/Game/__Dev/NiagaraSolo_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/NS_Test");
	auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), Path));
	if (!TestNotNull(TEXT("Isolated Solo fixture"), System)) return false;
	FUnrealMCPNiagaraCommands Commands; auto Add = MakeShared<FJsonObject>(); Add->SetStringField(TEXT("path"), Path);
	Add->SetStringField(TEXT("template_name"), TEXT("SimpleSpriteBurst")); Add->SetStringField(TEXT("new_emitter_name"), TEXT("SoloChild"));
	if (!TestTrue(TEXT("Second emitter"), Commands.HandleCommand(TEXT("add_emitter_to_system"), Add)->HasField(TEXT("new_emitter")))) return false;
	auto& Source = System->GetEmitterHandles()[0]; auto& Child = System->GetEmitterHandles()[1];
	Source.SetIsEnabled(false, *System, false);
	System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(true, false);
	FAssetCompilingManager::Get().FinishAllCompilation(); FAssetCompilingManager::Get().ProcessAsyncTasks(false);
	if (!TestTrue(TEXT("Save only owned fixture"), UEditorAssetLibrary::SaveLoadedAsset(System, false))) return false;
	const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()); const auto Hash = FMD5Hash::HashFile(*File);
	auto* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("Owned preview world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* First = World->SpawnActor<ANiagaraActor>()->GetNiagaraComponent(); First->SetAutoActivate(false); First->SetAsset(System);
	auto* Second = World->SpawnActor<ANiagaraActor>()->GetNiagaraComponent(); Second->SetAutoActivate(false); Second->SetAsset(System);
	auto* Other = World->SpawnActor<ANiagaraActor>()->GetNiagaraComponent(); Other->SetAutoActivate(false);
	const bool ActiveBefore = First->IsActive(); const float AgeBefore = First->GetDesiredAge();
	World->SendAllEndOfFrameUpdates();
	System->WaitForCompilationComplete(true, false);
	FAssetCompilingManager::Get().FinishAllCompilation(); FAssetCompilingManager::Get().ProcessAsyncTasks(false);
	if (!TestEqual(TEXT("Owned component setup compilation finished"), FAssetCompilingManager::Get().GetNumRemainingAssets(), 0)) return false;
	auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path);
	TArray<TSharedPtr<FJsonValue>> ChildIds{MakeShared<FJsonValueString>(Child.GetId().ToString(EGuidFormats::DigitsWithHyphens))};
	Params->SetArrayField(TEXT("emitter_handle_ids"), ChildIds);
	auto Invoke = [&]() { return Commands.HandleCommand(TEXT("set_niagara_emitter_solo"), Params); };
	const auto Preview = Invoke();
	if (!TestTrue(TEXT("Preview succeeds"), Preview->GetBoolField(TEXT("success")))) { FString Message; Preview->TryGetStringField(TEXT("error"), Message); AddError(Message); return false; }
	TestTrue(TEXT("Default dry-run"), Preview->GetBoolField(TEXT("dry_run"))); TestFalse(TEXT("Preview did not modify"), Preview->GetBoolField(TEXT("modified")));
	TestEqual(TEXT("Both same-asset components visible in impact"), Preview->GetArrayField(TEXT("affected_components")).Num(), 2);
	TestFalse(TEXT("Preview did not isolate"), System->GetIsolateEnabled());
	Params->SetBoolField(TEXT("dry_run"), false); TestTrue(TEXT("Apply without baseline refuses"), Invoke()->HasField(TEXT("error")));
	const FString Initial = Preview->GetStringField(TEXT("baseline")); Params->SetStringField(TEXT("expected_baseline"), Initial);
	Params->SetArrayField(TEXT("emitter_handle_ids"), {MakeShared<FJsonValueString>(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens))});
	TestTrue(TEXT("Unknown Handle refuses"), Invoke()->HasField(TEXT("error")));
	Params->SetArrayField(TEXT("emitter_handle_ids"), {ChildIds[0], ChildIds[0]}); TestTrue(TEXT("Duplicate Handle refuses"), Invoke()->HasField(TEXT("error")));
	Params->SetArrayField(TEXT("emitter_handle_ids"), ChildIds);
	System->SetIsolateEnabled(true); Source.SetIsolated(true);
	TestTrue(TEXT("UI state change invalidates baseline"), Invoke()->HasField(TEXT("error")));
	System->SetIsolateEnabled(false); Source.SetIsolated(false);
	auto* NewComponent = World->SpawnActor<ANiagaraActor>()->GetNiagaraComponent(); NewComponent->SetAutoActivate(false); NewComponent->SetAsset(System);
	TestTrue(TEXT("New affected component invalidates baseline"), Invoke()->HasField(TEXT("error"))); NewComponent->UnregisterComponent();
	const auto Applied = Invoke();
	if (!TestTrue(TEXT("Exact Solo apply succeeds"), Applied->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("Child isolated"), System->GetIsolateEnabled() && Child.IsIsolated() && !Source.IsIsolated());
	TestFalse(TEXT("Disabled source stays authored disabled"), Source.GetIsEnabled()); TestTrue(TEXT("Child authored enabled stays true"), Child.GetIsEnabled());
	TestTrue(TEXT("Both same-asset render states refreshed"), First->IsRenderStateDirty() && Second->IsRenderStateDirty());
	TestEqual(TEXT("No component activation change"), First->IsActive(), ActiveBefore); TestEqual(TEXT("No playback age reset"), First->GetDesiredAge(), AgeBefore);
	TestFalse(TEXT("Other asset render state untouched"), Other->IsRenderStateDirty()); TestFalse(TEXT("Solo never saves"), Applied->GetBoolField(TEXT("saved")));
	World->SendAllEndOfFrameUpdates(); Params->SetStringField(TEXT("expected_baseline"), Applied->GetStringField(TEXT("baseline")));
	const auto Noop = Invoke(); TestFalse(TEXT("Same Solo is no-op"), Noop->GetBoolField(TEXT("modified"))); TestFalse(TEXT("No-op does not refresh render state"), First->IsRenderStateDirty());
	const auto Restored = Commands.HandleCommand(TEXT("set_niagara_emitter_solo"), Applied->GetObjectField(TEXT("restore_arguments")));
	TestTrue(TEXT("Restore succeeds"), Restored->GetBoolField(TEXT("success"))); TestFalse(TEXT("Restore original no-Solo state"), System->GetIsolateEnabled() || Source.IsIsolated() || Child.IsIsolated());
	TestTrue(TEXT("Old apply baseline cannot clear newer state"), Commands.HandleCommand(TEXT("set_niagara_emitter_solo"), Applied->GetObjectField(TEXT("restore_arguments")))->HasField(TEXT("error")));
	Params->SetStringField(TEXT("expected_baseline"), Restored->GetStringField(TEXT("baseline")));
	Params->SetArrayField(TEXT("emitter_handle_ids"), {MakeShared<FJsonValueString>(Source.GetId().ToString(EGuidFormats::DigitsWithHyphens)), ChildIds[0]});
	const auto Multi = Invoke(); TestTrue(TEXT("Multi-emitter Solo"), Multi->GetBoolField(TEXT("success")) && Source.IsIsolated() && Child.IsIsolated());
	System->GetPackage()->SetDirtyFlag(true); Params->SetStringField(TEXT("expected_baseline"), Multi->GetStringField(TEXT("baseline"))); Params->SetArrayField(TEXT("emitter_handle_ids"), {});
	const auto Clear = Invoke(); TestTrue(TEXT("Clear Solo preserves existing dirty draft"), Clear->GetBoolField(TEXT("success")) && System->GetPackage()->IsDirty());
	TestFalse(TEXT("Clear does not enable disabled source"), Source.GetIsEnabled()); TestFalse(TEXT("All isolation cleared"), System->GetIsolateEnabled() || Source.IsIsolated() || Child.IsIsolated());
	System->GetPackage()->SetDirtyFlag(false);
	System->SetIsolateEnabled(true); TestTrue(TEXT("Inconsistent isolation refuses"), Invoke()->HasField(TEXT("error"))); System->SetIsolateEnabled(false);
	Params->SetStringField(TEXT("expected_baseline"), Clear->GetStringField(TEXT("baseline")));
	{ TGuardValue<bool> Saving(GIsSavingPackage, true); TestTrue(TEXT("Saving refuses without mutation"), Invoke()->HasField(TEXT("error"))); }
	{ TGuardValue<bool> SlowTask(GIsSlowTask, true); TestTrue(TEXT("Slow task refuses without mutation"), Invoke()->HasField(TEXT("error"))); }
	TestFalse(TEXT("Clean package preserved"), System->GetPackage()->IsDirty()); TestTrue(TEXT("All Solo actions leave disk bytes"), Hash == FMD5Hash::HashFile(*File));
	AddInfo(TEXT("Niagara Solo fixture: ") + Path); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraDiagnosticsTest, "UnrealMCP.Niagara.Diagnostics.ReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPNiagaraDiagnosticsTest::RunTest(const FString& Parameters)
{
	const FString Root = TEXT("/Game/__Dev/NiagaraDiagnostics_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Path = Root / TEXT("NS_Test");
	auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), Path));
	if (!TestNotNull(TEXT("Initialized diagnostic system"), System) || !TestTrue(TEXT("Default emitter present"), System->GetEmitterHandles().Num() > 0)) return false;
	FUnrealMCPNiagaraCommands Commands; auto Add = MakeShared<FJsonObject>(); Add->SetStringField(TEXT("path"), Path);
	Add->SetStringField(TEXT("template_name"), TEXT("SimpleSpriteBurst")); Add->SetStringField(TEXT("new_emitter_name"), TEXT("Electricity"));
	if (!TestTrue(TEXT("Independent child created"), Commands.HandleCommand(TEXT("add_emitter_to_system"), Add)->HasField(TEXT("new_emitter")))) return false;
	auto& Source = System->GetEmitterHandles()[0]; auto& Child = System->GetEmitterHandles()[1];
	Source.SetName(TEXT("TrailCore"), *System); Source.GetEmitterData()->bRequiresPersistentIDs = true; Child.GetEmitterData()->bRequiresPersistentIDs = true;
	auto Renderers = Source.GetEmitterData()->GetRenderers();
	for (auto* Renderer : Renderers) Source.GetInstance().Emitter->RemoveRenderer(Renderer, Source.GetInstance().Version);
	auto* Ribbon = NewObject<UNiagaraRibbonRendererProperties>(Child.GetInstance().Emitter, NAME_None, RF_Transactional);
	Child.GetInstance().Emitter->AddRenderer(Ribbon, Child.GetInstance().Version);
	auto* Generate = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(TEXT("/Niagara/Modules/Events/GenerateLocationEvent")));
	auto* Receive = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(TEXT("/Niagara/Modules/Events/ReceiveLocationEvent")));
	if (!TestNotNull(TEXT("Real GenerateLocationEvent"), Generate) || !TestNotNull(TEXT("Real ReceiveLocationEvent"), Receive)) return false;
	auto* SourceGraph = CastChecked<UNiagaraScriptSource>(Source.GetEmitterData()->UpdateScriptProps.Script->GetLatestSource());
	auto* SourceOutput = SourceGraph->NodeGraph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript);
	if (!TestNotNull(TEXT("Source output"), SourceOutput) || !TestNotNull(TEXT("Generator module on actual update stack"), FNiagaraStackGraphUtilities::AddScriptModuleToStack(Generate, *SourceOutput))) return false;
	auto* SharedSource = CastChecked<UNiagaraScriptSource>(Child.GetEmitterData()->SpawnScriptProps.Script->GetLatestSource());
	TArray<FGuid> Usages; TArray<FGuid> Modules;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FGuid Usage = FGuid::NewGuid(); Usages.Add(Usage);
		auto* Script = DuplicateObject<UNiagaraScript>(Child.GetEmitterData()->SpawnScriptProps.Script, Child.GetInstance().Emitter, FName(*FString::Printf(TEXT("DiagnosticEvent%d"), Index)));
		Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript); Script->SetUsageId(Usage); Script->SetLatestSource(SharedSource);
		auto* Graph = SharedSource->NodeGraph.Get(); auto* Output = NewObject<UNiagaraNodeOutput>(Graph);
		Graph->AddNode(Output, false, false); Output->CreateNewGuid(); Output->SetUsage(ENiagaraScriptUsage::ParticleEventScript); Output->SetUsageId(Usage);
		Output->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("OutputMap"))); Output->AllocateDefaultPins();
		auto* Input = NewObject<UNiagaraNodeInput>(Graph); Graph->AddNode(Input, false, false); Input->CreateNewGuid();
		Input->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap")); Input->Usage = ENiagaraInputNodeUsage::Parameter; Input->AllocateDefaultPins();
		TArray<UEdGraphPin*> InputPins, OutputPins; Output->GetInputPins(InputPins); Input->GetOutputPins(OutputPins);
		if (!TestTrue(TEXT("Event parameter-map pins"), !InputPins.IsEmpty() && !OutputPins.IsEmpty())) return false;
		OutputPins[0]->MakeLinkTo(InputPins[0]);
		FNiagaraEventScriptProperties Handler; Handler.Script = Script; Handler.SourceEmitterID = Source.GetId(); Handler.SourceEventName = TEXT("LocationEvent");
		Handler.ExecutionMode = EScriptExecutionMode::SpawnedParticles; Handler.SpawnNumber = Index + 2; Handler.MaxEventsPerFrame = Index + 7;
		Child.GetInstance().Emitter->AddEventHandler(Handler, Child.GetInstance().Version);
		auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Receive, *Output);
		if (!TestNotNull(TEXT("Receive on exact event output"), Module)) return false;
		Modules.Add(Module->NodeGuid);
	}
	System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(true, false);
	FAssetCompilingManager::Get().FinishAllCompilation(); FAssetCompilingManager::Get().ProcessAsyncTasks(false);
	if (!TestTrue(TEXT("Save isolated diagnostic fixture"), UEditorAssetLibrary::SaveLoadedAsset(System, false))) return false;
	const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()); const auto Hash = FMD5Hash::HashFile(*File);
	auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path); Params->SetBoolField(TEXT("include_inputs"), true);
	const auto Full = Commands.HandleCommand(TEXT("read_niagara_diagnostics"), Params);
	if (!TestTrue(TEXT("Diagnostics contract readable"), Full->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("Normal/interpolated spawn and event stacks complete within scope"), Full->GetBoolField(TEXT("complete")));
	TestEqual(TEXT("Two independent Handler rows"), Full->GetArrayField(TEXT("event_handlers")).Num(), 2);
	TestFalse(TEXT("Invisible source needs no renderer"), Full->GetArrayField(TEXT("emitters"))[0]->AsObject()->GetBoolField(TEXT("has_renderer")));
	TestEqual(TEXT("Runtime coverage explicitly unsupported"), Full->GetObjectField(TEXT("coverage"))->GetStringField(TEXT("runtime")), FString(TEXT("unsupported")));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto Event = Full->GetArrayField(TEXT("event_handlers"))[Index]->AsObject();
		TestEqual(TEXT("Exact event source"), Event->GetStringField(TEXT("resolved_source_handle_id")), Source.GetId().ToString(EGuidFormats::DigitsWithHyphens));
		TestEqual(TEXT("Authored spawn number"), Event->GetNumberField(TEXT("spawn_number")), static_cast<double>(Index + 2));
	}
	Params->SetStringField(TEXT("emitter_handle_id"), Child.GetId().ToString(EGuidFormats::DigitsWithHyphens)); Params->SetStringField(TEXT("script_type"), TEXT("event"));
	Params->SetStringField(TEXT("script_usage_id"), Usages[0].ToString(EGuidFormats::DigitsWithHyphens)); Params->SetStringField(TEXT("module_node_guid"), Modules[0].ToString(EGuidFormats::DigitsWithHyphens));
	const auto Selected = Commands.HandleCommand(TEXT("read_niagara_diagnostics"), Params);
	if (!TestTrue(TEXT("Exact event and module selected"), Selected->GetBoolField(TEXT("success")))) return false;
	auto Stack = Selected->GetArrayField(TEXT("emitters"))[0]->AsObject()->GetArrayField(TEXT("stacks"))[0]->AsObject();
	TestEqual(TEXT("Selected stack has exact Usage ID"), Stack->GetStringField(TEXT("usage_id")), Usages[0].ToString(EGuidFormats::DigitsWithHyphens));
	TestEqual(TEXT("Only reachable selected module returned"), Stack->GetArrayField(TEXT("modules")).Num(), 1);
	TestTrue(TEXT("Event module inputs use same typed reader"), Stack->GetArrayField(TEXT("modules"))[0]->AsObject()->HasField(TEXT("inputs")));
	Params->SetStringField(TEXT("script_usage_id"), Usages[1].ToString(EGuidFormats::DigitsWithHyphens));
	TestTrue(TEXT("Module cannot fall back across handlers"), Commands.HandleCommand(TEXT("read_niagara_diagnostics"), Params)->HasField(TEXT("error")));
	Params->RemoveField(TEXT("script_usage_id")); TestTrue(TEXT("Ambiguous event module request rejected"), Commands.HandleCommand(TEXT("read_niagara_diagnostics"), Params)->HasField(TEXT("error")));
	Params->RemoveField(TEXT("module_node_guid")); Params->RemoveField(TEXT("emitter_handle_id")); Params->RemoveField(TEXT("script_type"));
	System->SetIsolateEnabled(true); Source.SetIsolated(false); Child.SetIsolated(true);
	const auto Isolated = Commands.HandleCommand(TEXT("read_niagara_diagnostics"), Params);
	TestTrue(TEXT("Current isolation observed"), Isolated->GetBoolField(TEXT("isolation_enabled")));
	TestTrue(TEXT("Source authored enabled retained"), Isolated->GetArrayField(TEXT("emitters"))[0]->AsObject()->GetBoolField(TEXT("authored_enabled")));
	TestTrue(TEXT("Source only excluded from isolation render"), Isolated->GetArrayField(TEXT("emitters"))[0]->AsObject()->GetBoolField(TEXT("render_excluded_by_isolation")));
	TestFalse(TEXT("Child remains included"), Isolated->GetArrayField(TEXT("emitters"))[1]->AsObject()->GetBoolField(TEXT("render_excluded_by_isolation")));
	TestTrue(TEXT("Reader did not remove Solo"), System->GetIsolateEnabled() && Child.IsIsolated() && !Source.IsIsolated());
	TestFalse(TEXT("Reader leaves clean package"), System->GetPackage()->IsDirty()); TestTrue(TEXT("Read leaves disk bytes"), Hash == FMD5Hash::HashFile(*File));
	System->SetIsolateEnabled(false); Child.SetIsolated(false);
	const FGuid SavedSource = Child.GetEmitterData()->EventHandlerScriptProps[0].SourceEmitterID;
	Child.GetEmitterData()->EventHandlerScriptProps[0].SourceEmitterID = FGuid::NewGuid();
	const auto Invalid = Commands.HandleCommand(TEXT("read_niagara_diagnostics"), Params); TestFalse(TEXT("Invalid source explicit incomplete"), Invalid->GetBoolField(TEXT("complete")));
	TestEqual(TEXT("Invalid source is not inferred by name"), Invalid->GetArrayField(TEXT("event_handlers"))[0]->AsObject()->GetStringField(TEXT("source_resolution")), FString(TEXT("missing_or_ambiguous")));
	Child.GetEmitterData()->EventHandlerScriptProps[0].SourceEmitterID = SavedSource;
	AddInfo(TEXT("Niagara diagnostic fixture: ") + Path); return !HasAnyErrors();
}
#endif

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleListModuleInputs(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));
	if (!Params->HasField(TEXT("module_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'module_name'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	const FString EmitterName = Params->GetStringField(TEXT("emitter_name"));
	const FString ModuleName = Params->GetStringField(TEXT("module_name"));
	const FString ScriptType = Params->HasField(TEXT("script_type")) ? Params->GetStringField(TEXT("script_type")).ToLower() : TEXT("spawn");

	// Find the emitter handle
	TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase)) { TargetHandle = &Handle; break; }
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	FVersionedNiagaraEmitter VersionedEmitter = TargetHandle->GetInstance();
	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	FString ScriptError;
	if (!ResolveEmitterScript(EmitterData, ScriptType, Script, Usage, ScriptError))
		return FUnrealMCPCommonUtils::CreateErrorResponse(ScriptError);

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNodeByName(Script, ModuleName);
	if (!ModuleNode)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Module '%s' not found in %s script of emitter '%s'"), *ModuleName, *ScriptType, *EmitterName));

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<FNiagaraVariable> InputVars;
	TSet<FNiagaraVariable> HiddenVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, HiddenVars, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	const FString UniqueEmitterName = VersionedEmitter.Emitter ? VersionedEmitter.Emitter->GetUniqueEmitterName() : FString();
	FNiagaraParameterStore& RapidParams = Script->RapidIterationParameters;
	const TSet<FName> ExistingRapidNames = GetRapidParameterNameSet(RapidParams);

	TArray<TSharedPtr<FJsonValue>> InputArray;
	for (const FNiagaraVariable& InputVar : InputVars)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), StripModuleNamespace(InputVar.GetName().ToString()));
		Obj->SetStringField(TEXT("type"), InputVar.GetType().GetName());
		Obj->SetBoolField(TEXT("is_static"), InputVar.GetType().IsStatic());
		Obj->SetBoolField(TEXT("is_hidden"), HiddenVars.Contains(InputVar));

		const bool bRapidType = IsRapidIterationInputType(InputVar.GetType());
		Obj->SetBoolField(TEXT("can_enable_local"), bRapidType);

		// DataInterface-typed inputs (Sprite/Mesh Renderer info, Curve, StaticMesh, etc.)
		// can't be a Local Value but can be bound via bind_module_input_datainterface.
		const bool bIsDataInterface = InputVar.GetType().IsDataInterface();
		Obj->SetBoolField(TEXT("can_bind_datainterface"), bIsDataInterface);
		if (bIsDataInterface)
		{
			const FString DIClass = InputVar.GetType().GetClass() ? InputVar.GetType().GetClass()->GetName() : FString();
			TArray<TSharedPtr<FJsonValue>> Kinds;
			if (DIClass.Contains(TEXT("RendererInfo")))
				Kinds.Add(MakeShared<FJsonValueString>(TEXT("Renderer")));
			else
				Kinds.Add(MakeShared<FJsonValueString>(TEXT("Asset")));
			Obj->SetArrayField(TEXT("compatible_binding_kinds"), Kinds);
		}

		FNiagaraParameterHandle InputHandle(InputVar.GetName());
		FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);

		FString CurrentMode = TEXT("Default");

		// Detect an override pin bound to a dynamic input / custom expression / linked parameter.
		UEdGraphPin* OverridePin = FindOverridePinReadOnly(*ModuleNode, Aliased.GetParameterHandleString().ToString());
		const FString OverrideMode = AnnotateOverrideMode(Obj, OverridePin, Script, Usage, UniqueEmitterName, ConstantResolver);
		if (!OverrideMode.IsEmpty())
			CurrentMode = OverrideMode;

		if (bRapidType)
		{
			FNiagaraVariable RapidVar = MakeRapidIterationParameter(
				UniqueEmitterName, Usage, Aliased.GetParameterHandleString(), InputVar.GetType());
			Obj->SetStringField(TEXT("rapid_parameter_name"), RapidVar.GetName().ToString());
			if (CurrentMode == TEXT("Default") && ExistingRapidNames.Contains(RapidVar.GetName()))
			{
				CurrentMode = TEXT("Local");
				Obj->SetField(TEXT("value"), ReadRapidVariableToJson(RapidParams, RapidVar));
			}
		}
		Obj->SetStringField(TEXT("current_mode"), CurrentMode);

		InputArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetNumberField(TEXT("input_count"), InputArray.Num());
	ResultJson->SetArrayField(TEXT("inputs"), InputArray);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// enable_module_input
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleEnableModuleInput(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("emitter_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'emitter_name'"));
	if (!Params->HasField(TEXT("module_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'module_name'"));
	if (!Params->HasField(TEXT("input_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'input_name'"));

	FString LoadError;
	UNiagaraSystem* System = LoadNiagaraSystemByNameOrPath(Params, LoadError);
	if (!System)
		return FUnrealMCPCommonUtils::CreateErrorResponse(LoadError);

	const FString EmitterName = Params->GetStringField(TEXT("emitter_name"));
	const FString ModuleName = Params->GetStringField(TEXT("module_name"));
	const FString InputName = Params->GetStringField(TEXT("input_name"));
	const FString ScriptType = Params->HasField(TEXT("script_type")) ? Params->GetStringField(TEXT("script_type")).ToLower() : TEXT("spawn");

	// Find the emitter handle
	TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase)) { TargetHandle = &Handle; break; }
	}
	if (!TargetHandle)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *System->GetName()));

	FVersionedNiagaraEmitter VersionedEmitter = TargetHandle->GetInstance();
	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	FString ScriptError;
	if (!ResolveEmitterScript(EmitterData, ScriptType, Script, Usage, ScriptError))
		return FUnrealMCPCommonUtils::CreateErrorResponse(ScriptError);

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNodeByName(Script, ModuleName);
	if (!ModuleNode)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Module '%s' not found in %s script of emitter '%s'"), *ModuleName, *ScriptType, *EmitterName));

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<FNiagaraVariable> InputVars;
	TSet<FNiagaraVariable> HiddenVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, HiddenVars, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	// Match the requested input by short name (case-insensitive).
	const FNiagaraVariable* MatchVar = nullptr;
	TArray<FString> Candidates;
	for (const FNiagaraVariable& InputVar : InputVars)
	{
		const FString ShortName = StripModuleNamespace(InputVar.GetName().ToString());
		Candidates.Add(ShortName);
		if (ShortName.Equals(InputName, ESearchCase::IgnoreCase)) { MatchVar = &InputVar; break; }
	}
	if (!MatchVar)
	{
		// Second pass: contains match
		for (const FNiagaraVariable& InputVar : InputVars)
		{
			const FString ShortName = StripModuleNamespace(InputVar.GetName().ToString());
			if (ShortName.Contains(InputName)) { MatchVar = &InputVar; break; }
		}
	}
	if (!MatchVar)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Input '%s' not found on module '%s'. Available inputs: %s"),
			*InputName, *ModuleName, *FString::Join(Candidates, TEXT(", "))));

	const FNiagaraVariable InputVar = *MatchVar;
	const FNiagaraTypeDefinition InputType = InputVar.GetType();

	if (!IsRapidIterationInputType(InputType))
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Input '%s' has type '%s' which cannot be enabled as a Local Value (only constant/rapid-iteration types are supported; data interfaces and object assets use dedicated tools)."),
			*InputName, *InputType.GetName()));

	// Build the rapid iteration parameter for this module input.
	const FString UniqueEmitterName = VersionedEmitter.Emitter ? VersionedEmitter.Emitter->GetUniqueEmitterName() : FString();
	FNiagaraParameterHandle InputHandle(InputVar.GetName());
	FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);
	FNiagaraVariable RapidVar = MakeRapidIterationParameter(
		UniqueEmitterName, Usage, Aliased.GetParameterHandleString(), InputType);

	FNiagaraParameterStore& RapidParams = Script->RapidIterationParameters;
	const bool bAlreadyEnabled = GetRapidParameterNameSet(RapidParams).Contains(RapidVar.GetName());
	const bool bHasInitialValue = Params->HasField(TEXT("initial_value"));

	// If it's already enabled and the caller didn't ask to change the value, report a no-op success.
	if (bAlreadyEnabled && !bHasInitialValue)
	{
		TSharedPtr<FJsonObject> NoopJson = MakeShared<FJsonObject>();
		NoopJson->SetStringField(TEXT("status"), TEXT("success"));
		NoopJson->SetBoolField(TEXT("noop"), true);
		NoopJson->SetBoolField(TEXT("already_enabled"), true);
		NoopJson->SetStringField(TEXT("system"), System->GetName());
		NoopJson->SetStringField(TEXT("emitter"), EmitterName);
		NoopJson->SetStringField(TEXT("script_type"), ScriptType);
		NoopJson->SetStringField(TEXT("module"), ModuleName);
		NoopJson->SetStringField(TEXT("input"), InputName);
		NoopJson->SetStringField(TEXT("parameter_name"), RapidVar.GetName().ToString());
		NoopJson->SetStringField(TEXT("type"), InputType.GetName());
		NoopJson->SetField(TEXT("value"), ReadRapidVariableToJson(RapidParams, RapidVar));
		return NoopJson;
	}

	// Determine the value bytes to write.
	FNiagaraVariable ValueVar(InputType, NAME_None);
	if (bHasInitialValue)
	{
		FString WriteError;
		if (!WriteJsonToRapidVariable(ValueVar, Params->Values.FindRef(TEXT("initial_value")), WriteError))
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Failed to set initial_value for input '%s': %s"), *InputName, *WriteError));
	}
	else
	{
		// No explicit value: seed the rapid iteration parameter with zero-initialized data.
		// (The module's own default still applies until a value is set; callers who want a
		// specific starting value should pass initial_value.)
		ValueVar.AllocateData();
		FMemory::Memzero(ValueVar.GetData(), ValueVar.GetSizeInBytes());
	}

	// Write the rapid iteration parameter (creates it if missing), mirroring the Niagara stack behavior.
	Script->Modify();
	RapidParams.SetParameterData(ValueVar.GetData(), RapidVar, /*bAddParameterIfMissing*/ true);

	// Recompile and save.
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetBoolField(TEXT("already_enabled"), bAlreadyEnabled);
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetStringField(TEXT("input"), InputName);
	ResultJson->SetStringField(TEXT("parameter_name"), RapidVar.GetName().ToString());
	ResultJson->SetStringField(TEXT("type"), InputType.GetName());
	ResultJson->SetField(TEXT("value"), ReadRapidVariableToJson(RapidParams, RapidVar));
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// Static switch helpers (used by list_module_static_switches / set_module_static_switch)
// ─────────────────────────────────────────────────────────────────────────────

// Resolve system + emitter + script + module node from common params. Returns false + error.
static bool ResolveModuleContext(const TSharedPtr<FJsonObject>& Params,
	UNiagaraSystem*& OutSystem, FVersionedNiagaraEmitter& OutVersionedEmitter,
	UNiagaraScript*& OutScript, ENiagaraScriptUsage& OutUsage, UNiagaraNodeFunctionCall*& OutModuleNode,
	FString& OutEmitterName, FString& OutModuleName, FString& OutScriptType, FString& OutError)
{
	if (!Params->HasField(TEXT("emitter_name"))) { OutError = TEXT("Missing required parameter: 'emitter_name'"); return false; }
	if (!Params->HasField(TEXT("module_name"))) { OutError = TEXT("Missing required parameter: 'module_name'"); return false; }

	OutSystem = LoadNiagaraSystemByNameOrPath(Params, OutError);
	if (!OutSystem) return false;

	OutEmitterName = Params->GetStringField(TEXT("emitter_name"));
	OutModuleName = Params->GetStringField(TEXT("module_name"));
	OutScriptType = Params->HasField(TEXT("script_type")) ? Params->GetStringField(TEXT("script_type")).ToLower() : TEXT("spawn");

	TArray<FNiagaraEmitterHandle>& EmitterHandles = OutSystem->GetEmitterHandles();
	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetName().ToString().Equals(OutEmitterName, ESearchCase::IgnoreCase)) { TargetHandle = &Handle; break; }
	}
	if (!TargetHandle) { OutError = FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *OutEmitterName, *OutSystem->GetName()); return false; }

	OutVersionedEmitter = TargetHandle->GetInstance();
	FVersionedNiagaraEmitterData* EmitterData = OutVersionedEmitter.GetEmitterData();
	if (!EmitterData) { OutError = TEXT("Failed to get emitter data"); return false; }

	if (!ResolveEmitterScript(EmitterData, OutScriptType, OutScript, OutUsage, OutError)) return false;

	OutModuleNode = FindModuleNodeByName(OutScript, OutModuleName);
	if (!OutModuleNode) { OutError = FString::Printf(TEXT("Module '%s' not found in %s script of emitter '%s'"), *OutModuleName, *OutScriptType, *OutEmitterName); return false; }
	return true;
}

// Read a static switch pin's current value as a display string, and (optionally) fill allowed values.
static FString ReadStaticSwitchValue(const UEdGraphPin* Pin, TArray<TSharedPtr<FJsonValue>>* OutAllowed, FString& OutTypeName)
{
	FNiagaraVariable Var = UEdGraphSchema_Niagara::PinToNiagaraVariable(Pin, true);
	const FNiagaraTypeDefinition Type = Var.GetType();
	OutTypeName = Type.GetName();

	if (Type.IsEnum())
	{
		UEnum* Enum = Type.GetEnum();
		const int32 Cur = Var.IsDataAllocated() ? *reinterpret_cast<const int32*>(Var.GetData()) : 0;
		if (OutAllowed && Enum)
		{
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i) // skip auto-generated _MAX
			{
				// Prefer the UI display name (e.g. "Random Range"); fall back to the raw
				// enumerator identifier (e.g. "NewEnumerator2") only if no display name exists.
				FString Display = Enum->GetDisplayNameTextByIndex(i).ToString();
				if (Display.IsEmpty()) Display = Enum->GetNameStringByIndex(i);
				OutAllowed->Add(MakeShared<FJsonValueString>(Display));
			}
		}
		if (!Enum) return FString::FromInt(Cur);
		FString CurDisplay = Enum->GetDisplayNameTextByValue(Cur).ToString();
		if (CurDisplay.IsEmpty()) CurDisplay = Enum->GetNameStringByValue(Cur);
		return CurDisplay;
	}
	if (Type == FNiagaraTypeDefinition::GetBoolDef())
	{
		if (OutAllowed)
		{
			OutAllowed->Add(MakeShared<FJsonValueString>(TEXT("false")));
			OutAllowed->Add(MakeShared<FJsonValueString>(TEXT("true")));
		}
		const bool b = Var.IsDataAllocated() ? Var.GetValue<FNiagaraBool>().GetValue() : false;
		return b ? TEXT("true") : TEXT("false");
	}
	if (Type == FNiagaraTypeDefinition::GetIntDef())
	{
		const int32 Cur = Var.IsDataAllocated() ? *reinterpret_cast<const int32*>(Var.GetData()) : 0;
		return FString::FromInt(Cur);
	}
	return Pin->DefaultValue;
}

// ─────────────────────────────────────────────────────────────────────────────
// list_module_static_switches
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleListModuleStaticSwitches(const TSharedPtr<FJsonObject>& Params)
{
	UNiagaraSystem* System = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	FString EmitterName, ModuleName, ScriptType, Error;
	if (!ResolveModuleContext(Params, System, VersionedEmitter, Script, Usage, ModuleNode, EmitterName, ModuleName, ScriptType, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<UEdGraphPin*> SwitchPins;
	TSet<UEdGraphPin*> HiddenPins;
	FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*ModuleNode, SwitchPins, HiddenPins, ConstantResolver);

	TArray<TSharedPtr<FJsonValue>> SwitchArray;
	for (const UEdGraphPin* Pin : SwitchPins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		TArray<TSharedPtr<FJsonValue>> Allowed;
		FString TypeName;
		const FString CurrentValue = ReadStaticSwitchValue(Pin, &Allowed, TypeName);
		Obj->SetStringField(TEXT("type"), TypeName);
		Obj->SetStringField(TEXT("current_value"), CurrentValue);
		Obj->SetBoolField(TEXT("is_hidden"), HiddenPins.Contains(const_cast<UEdGraphPin*>(Pin)));
		Obj->SetArrayField(TEXT("allowed_values"), Allowed);
		SwitchArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetNumberField(TEXT("switch_count"), SwitchArray.Num());
	ResultJson->SetArrayField(TEXT("switches"), SwitchArray);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_module_static_switch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetModuleStaticSwitch(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("switch_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'switch_name'"));
	if (!Params->HasField(TEXT("value")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'value'"));

	UNiagaraSystem* System = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	FString EmitterName, ModuleName, ScriptType, Error;
	if (!ResolveModuleContext(Params, System, VersionedEmitter, Script, Usage, ModuleNode, EmitterName, ModuleName, ScriptType, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	const FString SwitchName = Params->GetStringField(TEXT("switch_name"));
	const FString DesiredValue = Params->GetStringField(TEXT("value"));

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<UEdGraphPin*> SwitchPins;
	TSet<UEdGraphPin*> HiddenPins;
	FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*ModuleNode, SwitchPins, HiddenPins, ConstantResolver);

	// Find the target switch pin (exact match first, then contains).
	UEdGraphPin* TargetPin = nullptr;
	TArray<FString> Candidates;
	for (UEdGraphPin* Pin : SwitchPins)
	{
		if (!Pin) continue;
		const FString PinName = Pin->PinName.ToString();
		Candidates.Add(PinName);
		if (PinName.Equals(SwitchName, ESearchCase::IgnoreCase)) { TargetPin = Pin; break; }
	}
	if (!TargetPin)
	{
		for (UEdGraphPin* Pin : SwitchPins)
		{
			if (Pin && Pin->PinName.ToString().Contains(SwitchName)) { TargetPin = Pin; break; }
		}
	}
	if (!TargetPin)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Static switch '%s' not found on module '%s'. Available switches: %s"),
			*SwitchName, *ModuleName, *FString::Join(Candidates, TEXT(", "))));

	// Determine the target value bytes from the desired display string.
	FNiagaraVariable Var = UEdGraphSchema_Niagara::PinToNiagaraVariable(TargetPin, true);
	const FNiagaraTypeDefinition Type = Var.GetType();

	TArray<TSharedPtr<FJsonValue>> DummyAllowed;
	FString DummyType;
	const FString PreviousValue = ReadStaticSwitchValue(TargetPin, &DummyAllowed, DummyType);

	FNiagaraVariable NewVar(Type, TargetPin->PinName);
	NewVar.AllocateData();
	FString NewDisplay;

	if (Type.IsEnum())
	{
		UEnum* Enum = Type.GetEnum();
		int32 Target = INDEX_NONE;
		if (Enum)
		{
			// Accept the UI display name ("Random Range"), the raw enumerator identifier
			// ("NewEnumerator2"), or a numeric value — matched case-insensitively.
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
			{
				const FString Display = Enum->GetDisplayNameTextByIndex(i).ToString();
				const FString Raw = Enum->GetNameStringByIndex(i);
				if (Display.Equals(DesiredValue, ESearchCase::IgnoreCase) || Raw.Equals(DesiredValue, ESearchCase::IgnoreCase))
				{
					Target = static_cast<int32>(Enum->GetValueByIndex(i));
					break;
				}
			}
		}
		if (Target == INDEX_NONE)
		{
			if (DesiredValue.IsNumeric())
			{
				Target = FCString::Atoi(*DesiredValue);
			}
			else
			{
				TArray<FString> Allowed;
				if (Enum)
				{
					for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
					{
						FString Display = Enum->GetDisplayNameTextByIndex(i).ToString();
						if (Display.IsEmpty()) Display = Enum->GetNameStringByIndex(i);
						Allowed.Add(Display);
					}
				}
				return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
					TEXT("Invalid value '%s' for switch '%s'. Allowed: %s"),
					*DesiredValue, *SwitchName, *FString::Join(Allowed, TEXT(", "))));
			}
		}
		NewVar.SetValue<int32>(Target);
		if (Enum)
		{
			NewDisplay = Enum->GetDisplayNameTextByValue(Target).ToString();
			if (NewDisplay.IsEmpty()) NewDisplay = Enum->GetNameStringByValue(Target);
		}
		else
		{
			NewDisplay = FString::FromInt(Target);
		}
	}
	else if (Type == FNiagaraTypeDefinition::GetBoolDef())
	{
		const bool b = DesiredValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || DesiredValue == TEXT("1");
		FNiagaraBool NB; NB.SetValue(b);
		NewVar.SetValue<FNiagaraBool>(NB);
		NewDisplay = b ? TEXT("true") : TEXT("false");
	}
	else if (Type == FNiagaraTypeDefinition::GetIntDef())
	{
		if (!DesiredValue.IsNumeric())
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Integer switch '%s' requires a numeric value"), *SwitchName));
		const int32 v = FCString::Atoi(*DesiredValue);
		NewVar.SetValue<int32>(v);
		NewDisplay = FString::FromInt(v);
	}
	else
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Unsupported static switch type '%s' for switch '%s'"), *Type.GetName(), *SwitchName));
	}

	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	FString PinDefault;
	if (!Schema->TryGetPinDefaultValueFromNiagaraVariable(NewVar, PinDefault))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to convert value to a pin default string"));

	// Commit the change on the function call node's static switch pin, then recompile.
	TargetPin->Modify();
	TargetPin->DefaultValue = PinDefault;
	if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(TargetPin->GetOwningNode()))
	{
		OwningNode->MarkNodeRequiresSynchronization(TEXT("Static switch value changed via MCP"), true);
	}

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	// Re-read visible value pins after the switch change (they become usable now).
	TArray<TSharedPtr<FJsonValue>> VisiblePins;
	{
		TArray<FNiagaraVariable> InputVars;
		TSet<FNiagaraVariable> HiddenVars;
		FNiagaraStackGraphUtilities::GetStackFunctionInputs(
			*ModuleNode, InputVars, HiddenVars, ConstantResolver,
			FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);
		for (const FNiagaraVariable& InputVar : InputVars)
		{
			if (!HiddenVars.Contains(InputVar))
				VisiblePins.Add(MakeShared<FJsonValueString>(StripModuleNamespace(InputVar.GetName().ToString())));
		}
	}

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetStringField(TEXT("switch_name"), TargetPin->PinName.ToString());
	ResultJson->SetStringField(TEXT("previous_value"), PreviousValue);
	ResultJson->SetStringField(TEXT("new_value"), NewDisplay);
	ResultJson->SetArrayField(TEXT("resulting_visible_pins"), VisiblePins);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// bind_module_input_datainterface
//
// Binds a DataInterface-typed module input (e.g. SubUVAnimation."Sprite Renderer",
// type NiagaraDataInterfaceSpriteRendererInfo) to either an emitter renderer or a
// referenced asset. This is the layer enable_module_input can't handle — DI inputs
// are object/interface bindings, not rapid-iteration constants.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleBindModuleInputDataInterface(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("input_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'input_name'"));

	UNiagaraSystem* System = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	FString EmitterName, ModuleName, ScriptType, Error;
	if (!ResolveModuleContext(Params, System, VersionedEmitter, Script, Usage, ModuleNode, EmitterName, ModuleName, ScriptType, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	const FString InputName = Params->GetStringField(TEXT("input_name"));
	const FString BindingKind = Params->HasField(TEXT("binding_kind")) ? Params->GetStringField(TEXT("binding_kind")) : TEXT("Renderer");

	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	// Find the matching DataInterface input on the module.
	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<FNiagaraVariable> InputVars;
	TSet<FNiagaraVariable> HiddenVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, HiddenVars, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	const FNiagaraVariable* MatchVar = nullptr;
	TArray<FString> DICandidates;
	for (const FNiagaraVariable& InputVar : InputVars)
	{
		if (!InputVar.GetType().IsDataInterface()) continue;
		const FString ShortName = StripModuleNamespace(InputVar.GetName().ToString());
		DICandidates.Add(ShortName);
		// Trim trailing spaces (some module inputs like "Sprite Renderer " have them).
		if (ShortName.TrimStartAndEnd().Equals(InputName.TrimStartAndEnd(), ESearchCase::IgnoreCase)) { MatchVar = &InputVar; break; }
	}
	if (!MatchVar)
	{
		for (const FNiagaraVariable& InputVar : InputVars)
		{
			if (!InputVar.GetType().IsDataInterface()) continue;
			if (StripModuleNamespace(InputVar.GetName().ToString()).Contains(InputName)) { MatchVar = &InputVar; break; }
		}
	}
	if (!MatchVar)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("DataInterface input '%s' not found on module '%s'. Available DI inputs: %s"),
			*InputName, *ModuleName, *FString::Join(DICandidates, TEXT(", "))));

	const FNiagaraVariable InputVar = *MatchVar;
	UClass* DIClass = InputVar.GetType().GetClass();
	if (!DIClass || !DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Input '%s' is not a DataInterface type"), *InputName));

	// Get or create the override pin for this input. If it already has a linked value,
	// report it as already bound (rebinding requires clearing, which we don't do here).
	FNiagaraParameterHandle InputHandle(InputVar.GetName());
	FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, Aliased, InputVar.GetType(), FGuid(), FGuid());

	if (OverridePin.LinkedTo.Num() > 0)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Input '%s' already has a bound value. Clearing/rebinding an existing DataInterface binding is not supported yet."), *InputName));

	// Create the DataInterface instance on the override pin.
	UNiagaraDataInterface* NewDI = nullptr;
	FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
		OverridePin, DIClass, Aliased.GetParameterHandleString().ToString(), NewDI);
	if (!NewDI)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create DataInterface instance for the input"));

	FString ResolvedBinding;

	if (BindingKind.Equals(TEXT("Renderer"), ESearchCase::IgnoreCase))
	{
		// Find the DI's renderer-properties object slot via reflection.
		FObjectProperty* RendererSlot = nullptr;
		for (TFieldIterator<FObjectProperty> It(DIClass); It; ++It)
		{
			if (It->PropertyClass && It->PropertyClass->IsChildOf(UNiagaraRendererProperties::StaticClass()))
			{
				RendererSlot = *It;
				break;
			}
		}
		if (!RendererSlot)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("DataInterface '%s' has no renderer-properties slot; try binding_kind='Asset'."), *DIClass->GetName()));

		// Select the emitter renderer matching the slot's class (and optional type filter).
		const FString RendererTypeFilter = Params->HasField(TEXT("renderer_type")) ? Params->GetStringField(TEXT("renderer_type")) : FString();
		const int32 RendererIndex = Params->HasField(TEXT("renderer_index")) ? static_cast<int32>(Params->GetNumberField(TEXT("renderer_index"))) : 0;

		const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
		TArray<UNiagaraRendererProperties*> Matching;
		for (UNiagaraRendererProperties* R : Renderers)
		{
			if (!R) continue;
			if (!R->GetClass()->IsChildOf(RendererSlot->PropertyClass)) continue;
			if (!RendererTypeFilter.IsEmpty() && !R->GetClass()->GetName().Contains(RendererTypeFilter)) continue;
			Matching.Add(R);
		}
		if (!Matching.IsValidIndex(RendererIndex))
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("No matching %s renderer at index %d on emitter '%s' (found %d)."),
				*RendererSlot->PropertyClass->GetName(), RendererIndex, *EmitterName, Matching.Num()));

		UNiagaraRendererProperties* TargetRenderer = Matching[RendererIndex];
		NewDI->Modify();
		RendererSlot->SetObjectPropertyValue(RendererSlot->ContainerPtrToValuePtr<void>(NewDI), TargetRenderer);
		NewDI->PostEditChange();
		ResolvedBinding = FString::Printf(TEXT("Renderer[%d] %s"), RendererIndex, *TargetRenderer->GetClass()->GetName());
	}
	else if (BindingKind.Equals(TEXT("Asset"), ESearchCase::IgnoreCase))
	{
		if (!Params->HasField(TEXT("asset_path")))
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("binding_kind='Asset' requires 'asset_path'"));
		const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!Asset)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

		// Find the DI object slot whose class matches the asset.
		FObjectProperty* AssetSlot = nullptr;
		for (TFieldIterator<FObjectProperty> It(DIClass); It; ++It)
		{
			if (It->PropertyClass && Asset->GetClass()->IsChildOf(It->PropertyClass))
			{
				AssetSlot = *It;
				break;
			}
		}
		if (!AssetSlot)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("DataInterface '%s' has no object slot accepting asset of type '%s'."), *DIClass->GetName(), *Asset->GetClass()->GetName()));

		NewDI->Modify();
		AssetSlot->SetObjectPropertyValue(AssetSlot->ContainerPtrToValuePtr<void>(NewDI), Asset);
		NewDI->PostEditChange();
		ResolvedBinding = FString::Printf(TEXT("Asset %s"), *Asset->GetPathName());
	}
	else
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Unsupported binding_kind '%s'. Supported: 'Renderer', 'Asset'."), *BindingKind));
	}

	// Recompile and save.
	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetStringField(TEXT("input"), InputName);
	ResultJson->SetStringField(TEXT("data_interface_class"), DIClass->GetName());
	ResultJson->SetStringField(TEXT("binding_kind"), BindingKind);
	ResultJson->SetStringField(TEXT("resolved_binding"), ResolvedBinding);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// Curve helpers (used by read_curve / set_curve_keys)
//
// Niagara modules hold curves as Curve DataInterfaces (NiagaraDataInterfaceCurve /
// ColorCurve / Vector[2/4]Curve) whose FRichCurve members are public UPROPERTYs.
// We reach the bound DI via the UNiagaraNodeInput linked to the input's override
// (or module default) pin, then read/write FRichCurve keys by reflection.
// ─────────────────────────────────────────────────────────────────────────────

static FString RichInterpToString(ERichCurveInterpMode M)
{
	switch (M)
	{
	case RCIM_Linear:   return TEXT("Linear");
	case RCIM_Constant: return TEXT("Constant");
	case RCIM_Cubic:    return TEXT("Cubic");
	default:            return TEXT("None");
	}
}

static ERichCurveInterpMode StringToRichInterp(const FString& S)
{
	if (S.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)) return RCIM_Linear;
	if (S.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
	if (S.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return RCIM_None;
	return RCIM_Cubic; // default for authoring
}

static FString RichExtrapToString(TEnumAsByte<ERichCurveExtrapolation> E)
{
	switch (E.GetValue())
	{
	case RCCE_Cycle:           return TEXT("Cycle");
	case RCCE_CycleWithOffset: return TEXT("CycleWithOffset");
	case RCCE_Oscillate:       return TEXT("Oscillate");
	case RCCE_Linear:          return TEXT("Linear");
	case RCCE_Constant:        return TEXT("Constant");
	default:                   return TEXT("None");
	}
}

static ERichCurveExtrapolation StringToRichExtrap(const FString& S)
{
	if (S.Equals(TEXT("Cycle"), ESearchCase::IgnoreCase)) return RCCE_Cycle;
	if (S.Equals(TEXT("CycleWithOffset"), ESearchCase::IgnoreCase)) return RCCE_CycleWithOffset;
	if (S.Equals(TEXT("Oscillate"), ESearchCase::IgnoreCase)) return RCCE_Oscillate;
	if (S.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)) return RCCE_Linear;
	if (S.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return RCCE_Constant;
	return RCCE_None;
}

// Map an FRichCurve UPROPERTY name to a short channel key.
static FString CurvePropToChannel(const FString& PropName)
{
	if (PropName == TEXT("Curve")) return FString();
	if (PropName == TEXT("RedCurve")) return TEXT("r");
	if (PropName == TEXT("GreenCurve")) return TEXT("g");
	if (PropName == TEXT("BlueCurve")) return TEXT("b");
	if (PropName == TEXT("AlphaCurve")) return TEXT("a");
	if (PropName == TEXT("XCurve")) return TEXT("x");
	if (PropName == TEXT("YCurve")) return TEXT("y");
	if (PropName == TEXT("ZCurve")) return TEXT("z");
	if (PropName == TEXT("WCurve")) return TEXT("w");
	return PropName.ToLower();
}

// Collect a DataInterface's editable FRichCurve channels (excludes the cooked caches).
static void CollectCurveChannels(UNiagaraDataInterface* DI, TArray<TPair<FString, FRichCurve*>>& Out)
{
	if (!DI) return;
	for (TFieldIterator<FStructProperty> It(DI->GetClass()); It; ++It)
	{
		if (It->Struct != TBaseStructure<FRichCurve>::Get()) continue;
		const FString PropName = It->GetName();
		if (PropName.Contains(TEXT("CookedEditorCache"))) continue;
		FRichCurve* Curve = It->ContainerPtrToValuePtr<FRichCurve>(DI);
		Out.Add(TPair<FString, FRichCurve*>(CurvePropToChannel(PropName), Curve));
	}
}

// Serialize an FRichCurve to a JSON object {keys, pre_infinity, post_infinity}.
static TSharedPtr<FJsonObject> RichCurveToJson(const FRichCurve& Curve)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Keys;
	for (const FRichCurveKey& K : Curve.GetConstRefOfKeys())
	{
		TSharedPtr<FJsonObject> KO = MakeShared<FJsonObject>();
		KO->SetNumberField(TEXT("time"), K.Time);
		KO->SetNumberField(TEXT("value"), K.Value);
		KO->SetNumberField(TEXT("arrive_tangent"), K.ArriveTangent);
		KO->SetNumberField(TEXT("leave_tangent"), K.LeaveTangent);
		KO->SetStringField(TEXT("interp_mode"), RichInterpToString(K.InterpMode));
		Keys.Add(MakeShared<FJsonValueObject>(KO));
	}
	O->SetArrayField(TEXT("keys"), Keys);
	O->SetStringField(TEXT("pre_infinity"), RichExtrapToString(Curve.PreInfinityExtrap));
	O->SetStringField(TEXT("post_infinity"), RichExtrapToString(Curve.PostInfinityExtrap));
	return O;
}

// Replace an FRichCurve's keys from a JSON keys array. Auto-sets tangents for keys
// that don't supply explicit tangents.
static void WriteJsonToRichCurve(FRichCurve& Curve, const TArray<TSharedPtr<FJsonValue>>& Keys,
	const FString& PreInf, const FString& PostInf)
{
	Curve.Reset();
	bool bAnyMissingTangent = false;
	for (const TSharedPtr<FJsonValue>& KV : Keys)
	{
		const TSharedPtr<FJsonObject>* KOPtr = nullptr;
		if (!KV->TryGetObject(KOPtr)) continue;
		const TSharedPtr<FJsonObject>& KO = *KOPtr;

		double Time = 0.0, Value = 0.0;
		KO->TryGetNumberField(TEXT("time"), Time);
		KO->TryGetNumberField(TEXT("value"), Value);

		FKeyHandle H = Curve.AddKey(static_cast<float>(Time), static_cast<float>(Value));
		FRichCurveKey& K = Curve.GetKey(H);
		K.InterpMode = StringToRichInterp(KO->HasField(TEXT("interp_mode")) ? KO->GetStringField(TEXT("interp_mode")) : TEXT("Cubic"));

		double AT = 0.0, LT = 0.0;
		if (KO->TryGetNumberField(TEXT("arrive_tangent"), AT)) K.ArriveTangent = static_cast<float>(AT); else bAnyMissingTangent = true;
		if (KO->TryGetNumberField(TEXT("leave_tangent"), LT)) K.LeaveTangent = static_cast<float>(LT); else bAnyMissingTangent = true;
	}
	if (!PreInf.IsEmpty()) Curve.PreInfinityExtrap = StringToRichExtrap(PreInf);
	if (!PostInf.IsEmpty()) Curve.PostInfinityExtrap = StringToRichExtrap(PostInf);
	if (bAnyMissingTangent) Curve.AutoSetTangents();
}

// Reflectively read the DataInterface held by a UNiagaraNodeInput (getter is not exported).
static UNiagaraDataInterface* GetInputNodeDataInterface(UNiagaraNodeInput* InputNode)
{
	if (!InputNode) return nullptr;
	FObjectProperty* Prop = CastField<FObjectProperty>(InputNode->GetClass()->FindPropertyByName(TEXT("DataInterface")));
	if (!Prop) return nullptr;
	return Cast<UNiagaraDataInterface>(Prop->GetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(InputNode)));
}

// Find the DataInterface bound to a module input by locating the UNiagaraNodeInput in a graph
// whose Input name matches. Used for both override (emitter graph, aliased name) and default
// (module graph, module-namespaced name).
static UNiagaraDataInterface* FindDataInterfaceInputInGraph(UNiagaraGraph* Graph, const FString& MatchName)
{
	if (!Graph) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UNiagaraNodeInput* In = Cast<UNiagaraNodeInput>(Node);
		if (!In) continue;
		if (In->Input.GetName().ToString().Equals(MatchName, ESearchCase::IgnoreCase))
			return GetInputNodeDataInterface(In);
	}
	return nullptr;
}

// Resolve the curve DataInterface for a module input, handling two cases:
//  (a) the input is itself a curve DI (edit it directly), or
//  (b) the input is a scalar driven by a dynamic-input function (e.g. FloatFromCurve) — dive
//      into that dynamic node and edit its curve DI.
// If bCreate, an override curve DI is created when none exists yet (for writing).
static UNiagaraDataInterface* ResolveCurveDI(
	UNiagaraScript* EmitterScript, UNiagaraNodeFunctionCall* ModuleNode, FCompileConstantResolver& ConstantResolver,
	const TArray<FNiagaraVariable>& ModuleInputs, const FString& InputName,
	bool bCreate, bool& bOutIsDefault, bool& bOutViaDynamic, FString& OutDynamicName, FString& OutError)
{
	bOutIsDefault = false;
	bOutViaDynamic = false;

	// Find the requested input by short name (any type).
	const FNiagaraVariable* MatchVar = nullptr;
	TArray<FString> Candidates;
	for (const FNiagaraVariable& V : ModuleInputs)
	{
		const FString S = StripModuleNamespace(V.GetName().ToString());
		Candidates.Add(S);
		if (S.TrimStartAndEnd().Equals(InputName.TrimStartAndEnd(), ESearchCase::IgnoreCase)) { MatchVar = &V; break; }
	}
	if (!MatchVar)
		for (const FNiagaraVariable& V : ModuleInputs)
			if (StripModuleNamespace(V.GetName().ToString()).Contains(InputName)) { MatchVar = &V; break; }
	if (!MatchVar)
	{
		OutError = FString::Printf(TEXT("Input '%s' not found on module. Available: %s"), *InputName, *FString::Join(Candidates, TEXT(", ")));
		return nullptr;
	}

	const FNiagaraVariable InputVar = *MatchVar;
	UNiagaraNodeFunctionCall* HostNode = ModuleNode;
	FNiagaraVariable CurveInputVar = InputVar;

	auto IsCurveDIType = [](const FNiagaraTypeDefinition& T) -> bool
	{
		return T.IsDataInterface() && T.GetClass() && T.GetClass()->GetName().Contains(TEXT("Curve"));
	};

	if (!InputVar.GetType().IsDataInterface())
	{
		// Not a DI: look for a dynamic-input function-call node linked to the input's override pin.
		FNiagaraParameterHandle H(InputVar.GetName());
		FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(H, ModuleNode);
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*ModuleNode, Aliased, InputVar.GetType(), FGuid(), FGuid());
		UNiagaraNodeFunctionCall* DynNode = (Pin.LinkedTo.Num() == 1) ? Cast<UNiagaraNodeFunctionCall>(Pin.LinkedTo[0]->GetOwningNode()) : nullptr;
		if (!DynNode)
		{
			OutError = FString::Printf(TEXT("Input '%s' is not a curve and has no curve dynamic input. Use set_module_dynamic_input (e.g. FloatFromCurve) first."), *InputName);
			return nullptr;
		}
		TArray<FNiagaraVariable> DynInputs;
		TSet<FNiagaraVariable> DynHidden;
		FNiagaraStackGraphUtilities::GetStackFunctionInputs(*DynNode, DynInputs, DynHidden, ConstantResolver,
			FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);
		const FNiagaraVariable* CurveVar = nullptr;
		for (const FNiagaraVariable& V : DynInputs)
			if (IsCurveDIType(V.GetType())) { CurveVar = &V; break; }
		if (!CurveVar)
		{
			OutError = FString::Printf(TEXT("Dynamic input '%s' has no curve input."), *DynNode->GetFunctionName());
			return nullptr;
		}
		HostNode = DynNode;
		CurveInputVar = *CurveVar;
		bOutViaDynamic = true;
		OutDynamicName = DynNode->GetFunctionName();
	}
	else if (!IsCurveDIType(InputVar.GetType()))
	{
		OutError = FString::Printf(TEXT("Input '%s' is a DataInterface but not a curve type."), *InputName);
		return nullptr;
	}

	// Find an existing override curve DI on the host node (emitter graph, aliased name).
	FNiagaraParameterHandle CH(CurveInputVar.GetName());
	FNiagaraParameterHandle CAliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(CH, HostNode);
	if (UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterScript->GetLatestSource()))
	{
		if (UNiagaraDataInterface* DI = FindDataInterfaceInputInGraph(Source->NodeGraph, CAliased.GetParameterHandleString().ToString()))
			return DI;
	}
	// Else the host's module-default curve (in the called graph).
	if (UNiagaraGraph* Called = HostNode->GetCalledGraph())
	{
		if (UNiagaraDataInterface* DI = FindDataInterfaceInputInGraph(Called, CurveInputVar.GetName().ToString()))
		{
			bOutIsDefault = true;
			if (!bCreate) return DI;
		}
	}
	if (!bCreate)
	{
		OutError = FString::Printf(TEXT("No curve DataInterface bound for input '%s'."), *InputName);
		return nullptr;
	}

	// Create an override curve DI so we don't mutate the shared default.
	UEdGraphPin& CPin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*HostNode, CAliased, CurveInputVar.GetType(), FGuid(), FGuid());
	if (CPin.LinkedTo.Num() > 0)
	{
		if (UNiagaraNodeInput* In = Cast<UNiagaraNodeInput>(CPin.LinkedTo[0]->GetOwningNode()))
			return GetInputNodeDataInterface(In);
		OutError = FString::Printf(TEXT("Curve input for '%s' already has a non-DI override."), *InputName);
		return nullptr;
	}
	UNiagaraDataInterface* NewDI = nullptr;
	FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
		CPin, CurveInputVar.GetType().GetClass(), CAliased.GetParameterHandleString().ToString(), NewDI);
	bOutIsDefault = false;
	return NewDI;
}

// ─────────────────────────────────────────────────────────────────────────────
// read_curve
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleReadCurve(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("input_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'input_name'"));

	UNiagaraSystem* System = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	FString EmitterName, ModuleName, ScriptType, Error;
	if (!ResolveModuleContext(Params, System, VersionedEmitter, Script, Usage, ModuleNode, EmitterName, ModuleName, ScriptType, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	const FString InputName = Params->GetStringField(TEXT("input_name"));

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<FNiagaraVariable> InputVars;
	TSet<FNiagaraVariable> HiddenVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, HiddenVars, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	bool bIsDefault = false;
	bool bViaDynamic = false;
	FString DynamicName, ResolveError;
	UNiagaraDataInterface* DI = ResolveCurveDI(Script, ModuleNode, ConstantResolver, InputVars, InputName,
		/*bCreate*/ false, bIsDefault, bViaDynamic, DynamicName, ResolveError);
	if (!DI)
		return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);

	TArray<TPair<FString, FRichCurve*>> Channels;
	CollectCurveChannels(DI, Channels);
	if (Channels.Num() == 0)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("DataInterface '%s' has no editable curves."), *DI->GetClass()->GetName()));

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetStringField(TEXT("input"), InputName);
	ResultJson->SetStringField(TEXT("data_interface_class"), DI->GetClass()->GetName());
	ResultJson->SetBoolField(TEXT("is_default"), bIsDefault);
	ResultJson->SetBoolField(TEXT("via_dynamic_input"), bViaDynamic);
	if (bViaDynamic) ResultJson->SetStringField(TEXT("dynamic_input"), DynamicName);

	if (Channels.Num() == 1 && Channels[0].Key.IsEmpty())
	{
		// Single float curve: inline keys / pre / post at the top level.
		TSharedPtr<FJsonObject> CurveJson = RichCurveToJson(*Channels[0].Value);
		ResultJson->SetStringField(TEXT("type"), TEXT("FloatCurve"));
		ResultJson->SetArrayField(TEXT("keys"), CurveJson->GetArrayField(TEXT("keys")));
		ResultJson->SetStringField(TEXT("pre_infinity"), CurveJson->GetStringField(TEXT("pre_infinity")));
		ResultJson->SetStringField(TEXT("post_infinity"), CurveJson->GetStringField(TEXT("post_infinity")));
	}
	else
	{
		TSharedPtr<FJsonObject> ChannelsJson = MakeShared<FJsonObject>();
		for (const TPair<FString, FRichCurve*>& Ch : Channels)
			ChannelsJson->SetObjectField(Ch.Key, RichCurveToJson(*Ch.Value));
		ResultJson->SetStringField(TEXT("type"), FString::Printf(TEXT("MultiCurve(%d)"), Channels.Num()));
		ResultJson->SetObjectField(TEXT("channels"), ChannelsJson);
	}
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_curve_keys
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetCurveKeys(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("input_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'input_name'"));
	if (!Params->HasField(TEXT("keys")) && !Params->HasField(TEXT("channels")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Provide either 'keys' (single-channel) or 'channels' (multi-channel)"));

	UNiagaraSystem* System = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	FString EmitterName, ModuleName, ScriptType, Error;
	if (!ResolveModuleContext(Params, System, VersionedEmitter, Script, Usage, ModuleNode, EmitterName, ModuleName, ScriptType, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	const FString InputName = Params->GetStringField(TEXT("input_name"));
	const FString PreInf = Params->HasField(TEXT("pre_infinity")) ? Params->GetStringField(TEXT("pre_infinity")) : FString();
	const FString PostInf = Params->HasField(TEXT("post_infinity")) ? Params->GetStringField(TEXT("post_infinity")) : FString();

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<FNiagaraVariable> InputVars;
	TSet<FNiagaraVariable> HiddenVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, HiddenVars, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	TArray<FString> Candidates;
	bool bWasDefault = false;
	bool bViaDynamic = false;
	FString DynamicName, ResolveError;
	UNiagaraDataInterface* DI = ResolveCurveDI(Script, ModuleNode, ConstantResolver, InputVars, InputName,
		/*bCreate*/ true, bWasDefault, bViaDynamic, DynamicName, ResolveError);
	if (!DI)
		return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);

	TArray<TPair<FString, FRichCurve*>> Channels;
	CollectCurveChannels(DI, Channels);
	if (Channels.Num() == 0)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("DataInterface '%s' has no editable curves."), *DI->GetClass()->GetName()));

	DI->Modify();
	TSharedPtr<FJsonObject> AppliedJson = MakeShared<FJsonObject>();

	if (Channels.Num() == 1 && Channels[0].Key.IsEmpty())
	{
		if (!Params->HasField(TEXT("keys")))
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("This is a single-channel curve; provide 'keys'."));
		const TArray<TSharedPtr<FJsonValue>>& Keys = Params->GetArrayField(TEXT("keys"));
		WriteJsonToRichCurve(*Channels[0].Value, Keys, PreInf, PostInf);
		AppliedJson->SetNumberField(TEXT("keys"), Keys.Num());
	}
	else
	{
		if (!Params->HasField(TEXT("channels")))
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("This is a multi-channel curve; provide 'channels' with keys per channel. Channels: %s"),
				*FString::JoinBy(Channels, TEXT(","), [](const TPair<FString, FRichCurve*>& C){ return C.Key; })));
		const TSharedPtr<FJsonObject>& ChannelsObj = Params->GetObjectField(TEXT("channels"));
		for (const TPair<FString, FRichCurve*>& Ch : Channels)
		{
			if (!ChannelsObj->HasField(Ch.Key)) continue;
			const TArray<TSharedPtr<FJsonValue>>& Keys = ChannelsObj->GetArrayField(Ch.Key);
			WriteJsonToRichCurve(*Ch.Value, Keys, PreInf, PostInf);
			AppliedJson->SetNumberField(Ch.Key, Keys.Num());
		}
	}

	DI->PostEditChange();

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetStringField(TEXT("input"), InputName);
	ResultJson->SetStringField(TEXT("data_interface_class"), DI->GetClass()->GetName());
	ResultJson->SetBoolField(TEXT("created_override"), bWasDefault);
	ResultJson->SetBoolField(TEXT("via_dynamic_input"), bViaDynamic);
	if (bViaDynamic) ResultJson->SetStringField(TEXT("dynamic_input"), DynamicName);
	ResultJson->SetObjectField(TEXT("applied_key_counts"), AppliedJson);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_module_dynamic_input
//
// Sets a module input's "dynamic input" (a sub-function like "Float from Curve" /
// "Color from Curve") so the input is driven by that function instead of a constant.
// After this, set_curve_keys can author the curve nested inside the dynamic input.
// ─────────────────────────────────────────────────────────────────────────────

// Find a UNiagaraScript asset by name (used for dynamic input scripts like "FloatFromCurve").
static UNiagaraScript* FindNiagaraScriptByName(const FString& Name)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UNiagaraScript::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);
	for (const FAssetData& A : Assets)
	{
		if (A.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			return Cast<UNiagaraScript>(A.GetAsset());
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetModuleDynamicInput(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("input_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'input_name'"));
	if (!Params->HasField(TEXT("dynamic_input_name")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'dynamic_input_name'"));

	UNiagaraSystem* System = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage Usage;
	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	FString EmitterName, ModuleName, ScriptType, Error;
	if (!ResolveModuleContext(Params, System, VersionedEmitter, Script, Usage, ModuleNode, EmitterName, ModuleName, ScriptType, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	const FString InputName = Params->GetStringField(TEXT("input_name"));
	const FString DynamicInputName = Params->GetStringField(TEXT("dynamic_input_name"));

	FCompileConstantResolver ConstantResolver(VersionedEmitter, Usage);
	TArray<FNiagaraVariable> InputVars;
	TSet<FNiagaraVariable> HiddenVars;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, HiddenVars, ConstantResolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

	const FNiagaraVariable* MatchVar = nullptr;
	TArray<FString> Candidates;
	for (const FNiagaraVariable& InputVar : InputVars)
	{
		const FString ShortName = StripModuleNamespace(InputVar.GetName().ToString());
		Candidates.Add(ShortName);
		if (ShortName.TrimStartAndEnd().Equals(InputName.TrimStartAndEnd(), ESearchCase::IgnoreCase)) { MatchVar = &InputVar; break; }
	}
	if (!MatchVar)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Input '%s' not found on module '%s'. Available inputs: %s"),
			*InputName, *ModuleName, *FString::Join(Candidates, TEXT(", "))));

	const FNiagaraVariable InputVar = *MatchVar;

	// Locate the dynamic input script asset.
	UNiagaraScript* DynScript = FindNiagaraScriptByName(DynamicInputName);
	if (!DynScript)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Dynamic input script '%s' not found. Common curve dynamic inputs: FloatFromCurve, ColorFromCurve, VectorFromCurve, Vector2DFromCurve, Vector4FromCurve."), *DynamicInputName));

	// Get/create the override pin. Only fresh inputs are supported (replacing an existing
	// override needs node removal that isn't exposed to external modules).
	FNiagaraParameterHandle InputHandle(InputVar.GetName());
	FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, Aliased, InputVar.GetType(), FGuid(), FGuid());
	if (OverridePin.LinkedTo.Num() > 0)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Input '%s' already has an override (dynamic input / linked value). Replacing it is not supported yet."), *InputName));

	// If the input was a Local (rapid iteration) value, remove that rapid parameter so the
	// dynamic input drives the value instead.
	if (IsRapidIterationInputType(InputVar.GetType()))
	{
		const FString UniqueEmitterName = VersionedEmitter.Emitter ? VersionedEmitter.Emitter->GetUniqueEmitterName() : FString();
		FNiagaraVariable RapidVar = MakeRapidIterationParameter(UniqueEmitterName, Usage, Aliased.GetParameterHandleString(), InputVar.GetType());
		Script->Modify();
		Script->RapidIterationParameters.RemoveParameter(RapidVar);
	}

	UNiagaraNodeFunctionCall* DynNode = nullptr;
	FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(OverridePin, DynScript, DynNode, FGuid(), DynamicInputName, FGuid());
	if (!DynNode)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create the dynamic input node"));

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("script_type"), ScriptType);
	ResultJson->SetStringField(TEXT("module"), ModuleName);
	ResultJson->SetStringField(TEXT("input"), InputName);
	ResultJson->SetStringField(TEXT("dynamic_input"), DynScript->GetName());
	ResultJson->SetStringField(TEXT("dynamic_input_function"), DynNode->GetFunctionName());
	ResultJson->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Set '%s' to dynamic input '%s'. Use set_curve_keys on this input to author its curve."), *InputName, *DynScript->GetName()));
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_renderer_to_emitter / remove_renderer_from_emitter
// ─────────────────────────────────────────────────────────────────────────────

// Resolve a friendly renderer type ("Sprite"/"Mesh"/"Ribbon"/"Light"/"Decal") to its
// UNiagaraRendererProperties subclass by enumerating derived classes.
static UClass* FindRendererClassByType(const FString& Type, TArray<FString>& OutAvailable)
{
	TArray<UClass*> Classes;
	GetDerivedClasses(UNiagaraRendererProperties::StaticClass(), Classes);
	const FString Needle = FString::Printf(TEXT("%sRenderer"), *Type); // e.g. "MeshRenderer"
	UClass* Found = nullptr;
	for (UClass* C : Classes)
	{
		if (!C || C->HasAnyClassFlags(CLASS_Abstract)) continue;
		// Friendly name = strip "Niagara" prefix and "RendererProperties" suffix.
		FString Friendly = C->GetName();
		Friendly.RemoveFromStart(TEXT("Niagara"));
		Friendly.RemoveFromEnd(TEXT("RendererProperties"));
		OutAvailable.AddUnique(Friendly);
		if (!Found && C->GetName().Contains(Needle, ESearchCase::IgnoreCase))
			Found = C;
	}
	return Found;
}

// Resolve system + emitter handle + versioned emitter from params.
static bool ResolveEmitterContext(const TSharedPtr<FJsonObject>& Params, UNiagaraSystem*& OutSystem,
	FNiagaraEmitterHandle*& OutHandle, FVersionedNiagaraEmitter& OutVersionedEmitter, FString& OutEmitterName, FString& OutError)
{
	if (!Params->HasField(TEXT("emitter_name"))) { OutError = TEXT("Missing required parameter: 'emitter_name'"); return false; }
	OutSystem = LoadNiagaraSystemByNameOrPath(Params, OutError);
	if (!OutSystem) return false;
	OutEmitterName = Params->GetStringField(TEXT("emitter_name"));
	TArray<FNiagaraEmitterHandle>& Handles = OutSystem->GetEmitterHandles();
	OutHandle = nullptr;
	for (FNiagaraEmitterHandle& H : Handles)
		if (H.GetName().ToString().Equals(OutEmitterName, ESearchCase::IgnoreCase)) { OutHandle = &H; break; }
	if (!OutHandle) { OutError = FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *OutEmitterName, *OutSystem->GetName()); return false; }
	OutVersionedEmitter = OutHandle->GetInstance();
	if (!OutVersionedEmitter.Emitter) { OutError = TEXT("Emitter instance is null"); return false; }
	return true;
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleAddRendererToEmitter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("renderer_type")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'renderer_type'"));

	UNiagaraSystem* System = nullptr;
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	FString EmitterName, Error;
	if (!ResolveEmitterContext(Params, System, Handle, VersionedEmitter, EmitterName, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	const FString RendererType = Params->GetStringField(TEXT("renderer_type"));
	TArray<FString> Available;
	UClass* RendererClass = FindRendererClassByType(RendererType, Available);
	if (!RendererClass)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Unknown renderer_type '%s'. Available: %s"), *RendererType, *FString::Join(Available, TEXT(", "))));

	UNiagaraEmitter* Emitter = VersionedEmitter.Emitter;
	UNiagaraRendererProperties* NewRenderer = NewObject<UNiagaraRendererProperties>(Emitter, RendererClass, NAME_None, RF_Transactional);
	if (!NewRenderer)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create renderer object"));

	Emitter->Modify();
	Emitter->AddRenderer(NewRenderer, VersionedEmitter.Version);

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	const int32 RendererIndex = EmitterData ? EmitterData->GetRenderers().Num() - 1 : INDEX_NONE;

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetStringField(TEXT("renderer_type"), RendererType);
	ResultJson->SetStringField(TEXT("renderer_class_name"), RendererClass->GetName());
	ResultJson->SetNumberField(TEXT("renderer_index"), RendererIndex);
	return ResultJson;
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleRemoveRendererFromEmitter(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("renderer_index")))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'renderer_index'"));

	UNiagaraSystem* System = nullptr;
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	FString EmitterName, Error;
	if (!ResolveEmitterContext(Params, System, Handle, VersionedEmitter, EmitterName, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));
	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (!Renderers.IsValidIndex(Index))
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("renderer_index %d out of range (emitter '%s' has %d renderer(s))"), Index, *EmitterName, Renderers.Num()));

	UNiagaraRendererProperties* Renderer = Renderers[Index];
	const FString RemovedClass = Renderer ? Renderer->GetClass()->GetName() : TEXT("(null)");

	UNiagaraEmitter* Emitter = VersionedEmitter.Emitter;
	Emitter->Modify();
	Emitter->RemoveRenderer(Renderer, VersionedEmitter.Version);

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetNumberField(TEXT("removed_index"), Index);
	ResultJson->SetStringField(TEXT("removed_class"), RemovedClass);
	ResultJson->SetNumberField(TEXT("remaining_count"), VersionedEmitter.GetEmitterData() ? VersionedEmitter.GetEmitterData()->GetRenderers().Num() : 0);
	return ResultJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_mesh_renderer_mesh — assign a static mesh and/or override material to a
// Mesh renderer's Meshes[] / OverrideMaterials[] slots.
// ─────────────────────────────────────────────────────────────────────────────

// Load a UStaticMesh from a content path, tolerating "/Game/Path/SM_X" (no object suffix).
static UStaticMesh* LoadStaticMeshByPath(const FString& InPath)
{
	FString Path = InPath;
	if (!Path.Contains(TEXT(".")))
	{
		FString Left, Right;
		if (Path.Split(TEXT("/"), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			Path = FString::Printf(TEXT("%s.%s"), *InPath, *Right);
	}
	return LoadObject<UStaticMesh>(nullptr, *Path);
}

// Load a UMaterialInterface from a content path, tolerating a missing object suffix.
static UMaterialInterface* LoadMaterialByPath(const FString& InPath)
{
	FString Path = InPath;
	if (!Path.Contains(TEXT(".")))
	{
		FString Left, Right;
		if (Path.Split(TEXT("/"), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			Path = FString::Printf(TEXT("%s.%s"), *InPath, *Right);
	}
	return LoadObject<UMaterialInterface>(nullptr, *Path);
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleSetMeshRendererMesh(const TSharedPtr<FJsonObject>& Params)
{
	const bool bHasMesh = Params->HasField(TEXT("static_mesh_path")) && !Params->GetStringField(TEXT("static_mesh_path")).IsEmpty();
	const bool bHasMaterial = Params->HasField(TEXT("override_material_path")) && !Params->GetStringField(TEXT("override_material_path")).IsEmpty();
	if (!bHasMesh && !bHasMaterial)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Provide at least one of 'static_mesh_path' or 'override_material_path'"));

	UNiagaraSystem* System = nullptr;
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitter VersionedEmitter;
	FString EmitterName, Error;
	if (!ResolveEmitterContext(Params, System, Handle, VersionedEmitter, EmitterName, Error))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Error);

	FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
	if (!EmitterData)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();

	// Select the Mesh renderer: by 'renderer_index' if given, else first UNiagaraMeshRendererProperties.
	UNiagaraMeshRendererProperties* MeshRenderer = nullptr;
	int32 SelectedIndex = INDEX_NONE;
	if (Params->HasField(TEXT("renderer_index")))
	{
		const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));
		if (!Renderers.IsValidIndex(Index))
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("renderer_index %d out of range (emitter '%s' has %d renderer(s))"), Index, *EmitterName, Renderers.Num()));
		MeshRenderer = Cast<UNiagaraMeshRendererProperties>(Renderers[Index]);
		if (!MeshRenderer)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Renderer at index %d is %s, not a Mesh renderer"), Index,
				Renderers[Index] ? *Renderers[Index]->GetClass()->GetName() : TEXT("(null)")));
		SelectedIndex = Index;
	}
	else
	{
		for (int32 i = 0; i < Renderers.Num(); ++i)
		{
			if (UNiagaraMeshRendererProperties* MR = Cast<UNiagaraMeshRendererProperties>(Renderers[i]))
			{
				MeshRenderer = MR; SelectedIndex = i; break;
			}
		}
		if (!MeshRenderer)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Emitter '%s' has no Mesh renderer. Use add_renderer_to_emitter first."), *EmitterName));
	}

	MeshRenderer->Modify();

	TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();

	// --- Assign static mesh (optional) ---
	if (bHasMesh)
	{
		const FString MeshPath = Params->GetStringField(TEXT("static_mesh_path"));
		UStaticMesh* StaticMesh = LoadStaticMeshByPath(MeshPath);
		if (!StaticMesh)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static mesh not found at path: %s"), *MeshPath));

		int32 Slot = 0;
		if (Params->HasField(TEXT("mesh_slot")))
			Slot = FMath::Max(0, static_cast<int32>(Params->GetNumberField(TEXT("mesh_slot"))));

		if (MeshRenderer->Meshes.Num() <= Slot)
			MeshRenderer->Meshes.SetNum(Slot + 1);
		MeshRenderer->Meshes[Slot].Mesh = StaticMesh;

		// Optional per-slot scale [x, y, z].
		if (Params->HasField(TEXT("scale")))
		{
			const TArray<TSharedPtr<FJsonValue>>* ScaleArr = nullptr;
			if (Params->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr->Num() >= 3)
			{
				MeshRenderer->Meshes[Slot].Scale = FVector(
					(*ScaleArr)[0]->AsNumber(), (*ScaleArr)[1]->AsNumber(), (*ScaleArr)[2]->AsNumber());
			}
		}

		ResultJson->SetNumberField(TEXT("mesh_slot"), Slot);
		ResultJson->SetStringField(TEXT("mesh"), StaticMesh->GetPathName());
		ResultJson->SetNumberField(TEXT("mesh_count"), MeshRenderer->Meshes.Num());
	}

	// --- Assign override material (optional) ---
	if (bHasMaterial)
	{
		const FString MatPath = Params->GetStringField(TEXT("override_material_path"));
		UMaterialInterface* Material = LoadMaterialByPath(MatPath);
		if (!Material)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found at path: %s"), *MatPath));

		int32 MatSlot = 0;
		if (Params->HasField(TEXT("material_slot")))
			MatSlot = FMath::Max(0, static_cast<int32>(Params->GetNumberField(TEXT("material_slot"))));

		// OverrideMaterials only take effect when bOverrideMaterials is enabled.
		MeshRenderer->bOverrideMaterials = true;
		if (MeshRenderer->OverrideMaterials.Num() <= MatSlot)
			MeshRenderer->OverrideMaterials.SetNum(MatSlot + 1);
		MeshRenderer->OverrideMaterials[MatSlot].ExplicitMat = Material;

		ResultJson->SetNumberField(TEXT("material_slot"), MatSlot);
		ResultJson->SetStringField(TEXT("override_material"), Material->GetPathName());
		ResultJson->SetNumberField(TEXT("override_material_count"), MeshRenderer->OverrideMaterials.Num());
	}

	MeshRenderer->PostEditChange();

	System->RequestCompile(true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	if (!SaveNiagaraSystemAsset(System)) return NiagaraSaveFailure(System);
	System->PostEditChange();

	ResultJson->SetStringField(TEXT("status"), TEXT("success"));
	ResultJson->SetStringField(TEXT("system"), System->GetName());
	ResultJson->SetStringField(TEXT("emitter"), EmitterName);
	ResultJson->SetNumberField(TEXT("renderer_index"), SelectedIndex);
	return ResultJson;
}

