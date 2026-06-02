#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "GameFramework/InputSettings.h"
#include "EditorAssetLibrary.h"
#include "JsonObjectConverter.h"
#include "Animation/Skeleton.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeTypes.h"
#include "StateTreeSchema.h"
#include "InstancedStruct.h"
#include "PropertyBindingPath.h"

FUnrealMCPProjectCommands::FUnrealMCPProjectCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_input_mapping"))
    {
        return HandleCreateInputMapping(Params);
    }
    else if (CommandType == TEXT("read_data_asset"))
    {
        return HandleReadDataAsset(Params);
    }
    else if (CommandType == TEXT("get_class_properties"))
    {
        return HandleGetClassProperties(Params);
    }
    else if (CommandType == TEXT("read_behavior_tree"))
    {
        return HandleReadBehaviorTree(Params);
    }
    else if (CommandType == TEXT("read_blackboard"))
    {
        return HandleReadBlackboard(Params);
    }
    else if (CommandType == TEXT("read_state_tree"))
    {
        return HandleReadStateTree(Params);
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown project command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleCreateInputMapping(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActionName;
    if (!Params->TryGetStringField(TEXT("action_name"), ActionName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_name' parameter"));
    }

    FString Key;
    if (!Params->TryGetStringField(TEXT("key"), Key))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'key' parameter"));
    }

    // Get the input settings
    UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
    if (!InputSettings)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get input settings"));
    }

    // Create the input action mapping
    FInputActionKeyMapping ActionMapping;
    ActionMapping.ActionName = FName(*ActionName);
    ActionMapping.Key = FKey(*Key);

    // Add modifiers if provided
    if (Params->HasField(TEXT("shift")))
    {
        ActionMapping.bShift = Params->GetBoolField(TEXT("shift"));
    }
    if (Params->HasField(TEXT("ctrl")))
    {
        ActionMapping.bCtrl = Params->GetBoolField(TEXT("ctrl"));
    }
    if (Params->HasField(TEXT("alt")))
    {
        ActionMapping.bAlt = Params->GetBoolField(TEXT("alt"));
    }
    if (Params->HasField(TEXT("cmd")))
    {
        ActionMapping.bCmd = Params->GetBoolField(TEXT("cmd"));
    }

    // Add the mapping
    InputSettings->AddActionMapping(ActionMapping);
    InputSettings->SaveConfig();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("action_name"), ActionName);
    ResultObj->SetStringField(TEXT("key"), Key);
    return ResultObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// read_data_asset
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'asset_path'"));
    }

    // Load the asset
    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!LoadedAsset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset at path '%s'"), *AssetPath));
    }

    // Serialize all BlueprintVisible properties to JSON via reflection
    TSharedPtr<FJsonObject> PropertiesJson = MakeShared<FJsonObject>();
    UClass* AssetClass = LoadedAsset->GetClass();

    for (TFieldIterator<FProperty> PropIt(AssetClass); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        if (!Prop->HasAnyPropertyFlags(CPF_BlueprintVisible))
            continue;

        const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(LoadedAsset);
        TSharedPtr<FJsonValue> JsonValue = FJsonObjectConverter::UPropertyToJsonValue(Prop, ValuePtr, 0, 0);
        if (JsonValue.IsValid())
        {
            PropertiesJson->SetField(Prop->GetNameCPP(), JsonValue);
        }
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("status"), TEXT("success"));
    ResultJson->SetStringField(TEXT("asset_name"), LoadedAsset->GetName());
    ResultJson->SetStringField(TEXT("asset_path"), AssetPath);
    ResultJson->SetStringField(TEXT("class_name"), AssetClass->GetName());
    ResultJson->SetObjectField(TEXT("properties"), PropertiesJson);
    return ResultJson;
}

// Helper: Convert FProperty type info to a human-readable string
static FString GetPropertyTypeString(FProperty* Prop)
{
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		if (UEnum* Enum = EnumProp->GetEnum())
			return FString::Printf(TEXT("enum (%s)"), *Enum->GetName());
		return TEXT("enum");
	}
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
			return FString::Printf(TEXT("enum (%s)"), *ByteProp->Enum->GetName());
		return TEXT("byte");
	}
	if (CastField<FBoolProperty>(Prop)) return TEXT("bool");
	if (CastField<FIntProperty>(Prop)) return TEXT("int");
	if (CastField<FInt64Property>(Prop)) return TEXT("int64");
	if (CastField<FFloatProperty>(Prop)) return TEXT("float");
	if (CastField<FDoubleProperty>(Prop)) return TEXT("double");
	if (CastField<FStrProperty>(Prop)) return TEXT("string");
	if (CastField<FNameProperty>(Prop)) return TEXT("name");
	if (CastField<FTextProperty>(Prop)) return TEXT("text");
	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		return FString::Printf(TEXT("struct (%s)"), *StructProp->Struct->GetName());
	}
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
	{
		return FString::Printf(TEXT("object (%s)"), *ObjProp->PropertyClass->GetName());
	}
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
	{
		return FString::Printf(TEXT("class (%s)"), *ClassProp->MetaClass->GetName());
	}
	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
	{
		return FString::Printf(TEXT("soft_object (%s)"), *SoftObjProp->PropertyClass->GetName());
	}
	if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
	{
		return FString::Printf(TEXT("soft_class (%s)"), *SoftClassProp->MetaClass->GetName());
	}
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		return FString::Printf(TEXT("array (%s)"), *GetPropertyTypeString(ArrayProp->Inner));
	}
	if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		return FString::Printf(TEXT("set (%s)"), *GetPropertyTypeString(SetProp->ElementProp));
	}
	if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
	{
		return FString::Printf(TEXT("map (%s -> %s)"),
			*GetPropertyTypeString(MapProp->KeyProp), *GetPropertyTypeString(MapProp->ValueProp));
	}
	if (CastField<FDelegateProperty>(Prop)) return TEXT("delegate");
	if (CastField<FMulticastDelegateProperty>(Prop)) return TEXT("multicast_delegate");
	if (CastField<FInterfaceProperty>(Prop)) return TEXT("interface");

	return Prop->GetCPPType();
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleGetClassProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	FString AssetPath;
	FString CategoryFilter;

	Params->TryGetStringField(TEXT("class_name"), ClassName);
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	Params->TryGetStringField(TEXT("category"), CategoryFilter);

	UClass* TargetClass = nullptr;
	UObject* AssetInstance = nullptr;

	// If asset_path provided, load the asset and get its class
	if (!AssetPath.IsEmpty())
	{
		AssetInstance = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!AssetInstance)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
		}
		TargetClass = AssetInstance->GetClass();

		// If the loaded asset is a UBlueprint, dive into its GeneratedClass + CDO
		// so we expose actual Blueprint-defined properties (e.g. AIControllerClass)
		// with their default values, not the UBlueprint wrapper's own properties.
		if (UBlueprint* AsBP = Cast<UBlueprint>(AssetInstance))
		{
			if (AsBP->GeneratedClass)
			{
				TargetClass = AsBP->GeneratedClass;
				if (UObject* CDO = TargetClass->GetDefaultObject())
				{
					AssetInstance = CDO;
				}
			}
		}
	}
	else if (!ClassName.IsEmpty())
	{
		// Search for class by name
		TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);

		// If not found, try common module paths
		if (!TargetClass)
		{
			const FString ModulePaths[] = {
				TEXT("/Script/Engine."),
				TEXT("/Script/CoreUObject."),
				TEXT("/Script/UMG."),
				TEXT("/Script/AnimGraphRuntime."),
				TEXT("/Script/Niagara."),
				TEXT("/Script/EnhancedInput."),
			};
			for (const FString& Prefix : ModulePaths)
			{
				TargetClass = FindObject<UClass>(nullptr, *(Prefix + ClassName));
				if (TargetClass) break;
			}
		}
		if (!TargetClass)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Class not found: %s"), *ClassName));
		}

		// If we found a class via class_name, use its CDO so that current default
		// values are returned alongside property metadata.
		if (UObject* CDO = TargetClass->GetDefaultObject())
		{
			AssetInstance = CDO;
		}
	}
	else
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Must provide either 'class_name' or 'asset_path'"));
	}

	// Skip engine base classes
	TSet<UClass*> SkipClasses;
	SkipClasses.Add(UObject::StaticClass());

	// Build properties array
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> PropIt(TargetClass); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop) continue;

		// Skip UObject base properties
		if (SkipClasses.Contains(Prop->GetOwnerClass()))
			continue;

		// Skip transient/deprecated
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			continue;

		// Skip BoneTree for Skeleton assets (replaced by bone_hierarchy)
		if (AssetInstance && AssetInstance->IsA<USkeleton>() && Prop->GetName() == TEXT("BoneTree"))
			continue;

		// Get category
		FString Category = Prop->GetMetaData(TEXT("Category"));

		// Category filter
		if (!CategoryFilter.IsEmpty() && !Category.Contains(CategoryFilter))
			continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), GetPropertyTypeString(Prop));

		if (!Category.IsEmpty())
			PropObj->SetStringField(TEXT("category"), Category);

		// Edit flags
		bool bEditable = Prop->HasAnyPropertyFlags(CPF_Edit);
		PropObj->SetBoolField(TEXT("editable"), bEditable);

		bool bBlueprintVisible = Prop->HasAnyPropertyFlags(CPF_BlueprintVisible);
		if (bBlueprintVisible)
			PropObj->SetBoolField(TEXT("blueprint_visible"), true);

		bool bBlueprintReadOnly = Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
		if (bBlueprintReadOnly)
			PropObj->SetBoolField(TEXT("blueprint_read_only"), true);

		// Tooltip
		FString Tooltip = Prop->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty())
			PropObj->SetStringField(TEXT("tooltip"), Tooltip);

		// Owner class (which class defined this property)
		if (Prop->GetOwnerClass() && Prop->GetOwnerClass() != TargetClass)
			PropObj->SetStringField(TEXT("defined_in"), Prop->GetOwnerClass()->GetName());

		// Current value (only if we have a concrete asset instance)
		if (AssetInstance)
		{
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(AssetInstance);

			// For arrays of UObjects, expand each element's properties
			FArrayProperty* ArrayPropCast = CastField<FArrayProperty>(Prop);
			FObjectProperty* InnerObjProp = ArrayPropCast ? CastField<FObjectProperty>(ArrayPropCast->Inner) : nullptr;
			if (InnerObjProp)
			{
				FScriptArrayHelper ArrayHelper(ArrayPropCast, ValuePtr);
				TArray<TSharedPtr<FJsonValue>> ExpandedArray;
				const int32 MaxExpand = FMath::Min(ArrayHelper.Num(), 200);
				for (int32 i = 0; i < MaxExpand; ++i)
				{
					UObject* Elem = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetElementPtr(i));
					if (!Elem) continue;

					TSharedPtr<FJsonObject> ElemObj = MakeShared<FJsonObject>();
					for (TFieldIterator<FProperty> ElemIt(Elem->GetClass()); ElemIt; ++ElemIt)
					{
						FProperty* ElemProp = *ElemIt;
						if (!ElemProp || ElemProp->GetOwnerClass() == UObject::StaticClass()) continue;
						if (ElemProp->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;

						const void* ElemValPtr = ElemProp->ContainerPtrToValuePtr<void>(Elem);
						FString ElemValStr;
						ElemProp->ExportTextItem_Direct(ElemValStr, ElemValPtr, nullptr, nullptr, PPF_None);
						if (!ElemValStr.IsEmpty())
							ElemObj->SetStringField(ElemProp->GetName(), ElemValStr);
					}
					ExpandedArray.Add(MakeShared<FJsonValueObject>(ElemObj));
				}
				PropObj->SetArrayField(TEXT("value"), ExpandedArray);
				PropObj->SetNumberField(TEXT("count"), ArrayHelper.Num());
				if (ArrayHelper.Num() > MaxExpand)
					PropObj->SetBoolField(TEXT("truncated"), true);
			}
			else
			{
				FString ValueStr;
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
				if (!ValueStr.IsEmpty())
					PropObj->SetStringField(TEXT("value"), ValueStr);
			}
		}

		PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	// Build result
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("class"), TargetClass->GetName());
	if (TargetClass->GetSuperClass())
		ResultObj->SetStringField(TEXT("parent_class"), TargetClass->GetSuperClass()->GetName());
	if (!AssetPath.IsEmpty())
		ResultObj->SetStringField(TEXT("asset_path"), AssetPath);
	ResultObj->SetNumberField(TEXT("property_count"), PropertiesArray.Num());
	ResultObj->SetArrayField(TEXT("properties"), PropertiesArray);

	// Special handling: USkeleton bone hierarchy (stored in FReferenceSkeleton, not in UProperties)
	if (AssetInstance)
	{
		USkeleton* Skeleton = Cast<USkeleton>(AssetInstance);
		if (Skeleton)
		{
			const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
			TArray<TSharedPtr<FJsonValue>> BonesArray;
			for (int32 i = 0; i < RefSkel.GetRawBoneNum(); ++i)
			{
				const FMeshBoneInfo& BoneInfo = RefSkel.GetRawRefBoneInfo()[i];
				TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
				BoneObj->SetNumberField(TEXT("index"), i);
				BoneObj->SetStringField(TEXT("name"), BoneInfo.Name.ToString());
				BoneObj->SetNumberField(TEXT("parent_index"), BoneInfo.ParentIndex);
				BonesArray.Add(MakeShared<FJsonValueObject>(BoneObj));
			}
			ResultObj->SetArrayField(TEXT("bone_hierarchy"), BonesArray);
		}
	}

	return ResultObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// BT helpers
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::BTNodeToJson(UBTNode* Node)
{
	if (!Node) return nullptr;
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

	FString Name = Node->GetNodeName();
	if (!Name.IsEmpty())
		Obj->SetStringField(TEXT("name"), Name);

	Obj->SetNumberField(TEXT("execution_index"), Node->GetExecutionIndex());

	// Export editable properties (skip base class noise)
	for (TFieldIterator<FProperty> PropIt(Node->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
		if (Prop->GetOwnerClass() == UBTNode::StaticClass()) continue;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;

		const void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Node);
		FString ValStr;
		Prop->ExportTextItem_Direct(ValStr, ValPtr, nullptr, nullptr, PPF_None);
		if (!ValStr.IsEmpty() && ValStr.Len() < 2048)
			Obj->SetStringField(Prop->GetName(), ValStr);
	}

	return Obj;
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::BTCompositeToJson(UBTCompositeNode* CompNode)
{
	if (!CompNode) return nullptr;

	TSharedPtr<FJsonObject> NodeObj = BTNodeToJson(CompNode);

	// Services
	TArray<TSharedPtr<FJsonValue>> ServicesArr;
	for (UBTService* Service : CompNode->Services)
	{
		TSharedPtr<FJsonObject> SvcObj = BTNodeToJson(Service);
		if (SvcObj)
		{
			ServicesArr.Add(MakeShared<FJsonValueObject>(SvcObj));
		}
	}
	if (ServicesArr.Num() > 0)
		NodeObj->SetArrayField(TEXT("services"), ServicesArr);

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildrenArr;
	for (const FBTCompositeChild& Child : CompNode->Children)
	{
		TSharedPtr<FJsonObject> ChildObj;

		if (Child.ChildComposite)
		{
			ChildObj = BTCompositeToJson(Child.ChildComposite);
		}
		else if (Child.ChildTask)
		{
			ChildObj = BTNodeToJson(Child.ChildTask);
		}

		if (!ChildObj) continue;

		// Decorators on this child edge
		TArray<TSharedPtr<FJsonValue>> DecoArr;
		for (UBTDecorator* Deco : Child.Decorators)
		{
			TSharedPtr<FJsonObject> DecoObj = BTNodeToJson(Deco);
			if (DecoObj)
			{
				FString AbortMode = UEnum::GetValueAsString(Deco->GetFlowAbortMode());
				AbortMode.ReplaceInline(TEXT("EBTFlowAbortMode::"), TEXT(""));
				DecoObj->SetStringField(TEXT("flow_abort"), AbortMode);
				if (Deco->IsInversed())
					DecoObj->SetBoolField(TEXT("inversed"), true);
				DecoArr.Add(MakeShared<FJsonValueObject>(DecoObj));
			}
		}
		if (DecoArr.Num() > 0)
			ChildObj->SetArrayField(TEXT("decorators"), DecoArr);

		ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildObj));
	}
	if (ChildrenArr.Num() > 0)
		NodeObj->SetArrayField(TEXT("children"), ChildrenArr);

	return NodeObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// read_behavior_tree
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleReadBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBehaviorTree* BT = Cast<UBehaviorTree>(Loaded);
	if (!BT)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load BehaviorTree at: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("name"), BT->GetName());
	ResultObj->SetStringField(TEXT("asset_path"), AssetPath);

	// Blackboard reference
	if (BT->BlackboardAsset)
	{
		ResultObj->SetStringField(TEXT("blackboard"), BT->BlackboardAsset->GetPathName());
	}

	// Root tree
	if (BT->RootNode)
	{
		ResultObj->SetObjectField(TEXT("root"), BTCompositeToJson(BT->RootNode));
	}

	// Root decorators (subtree decorators)
	if (BT->RootDecorators.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RootDecArr;
		for (UBTDecorator* Deco : BT->RootDecorators)
		{
			TSharedPtr<FJsonObject> DecoObj = BTNodeToJson(Deco);
			if (DecoObj)
				RootDecArr.Add(MakeShared<FJsonValueObject>(DecoObj));
		}
		ResultObj->SetArrayField(TEXT("root_decorators"), RootDecArr);
	}

	return ResultObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// read_blackboard
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleReadBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlackboardData* BB = Cast<UBlackboardData>(Loaded);
	if (!BB)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load BlackboardData at: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("name"), BB->GetName());
	ResultObj->SetStringField(TEXT("asset_path"), AssetPath);

	if (BB->Parent)
	{
		ResultObj->SetStringField(TEXT("parent"), BB->Parent->GetPathName());
	}

	TArray<TSharedPtr<FJsonValue>> KeysArr;
	for (const FBlackboardEntry& Entry : BB->Keys)
	{
		TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
		KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());

		if (Entry.KeyType)
		{
			FString TypeName = Entry.KeyType->GetClass()->GetName();
			TypeName.ReplaceInline(TEXT("BlackboardKeyType_"), TEXT(""));
			KeyObj->SetStringField(TEXT("type"), TypeName);
		}

		KeyObj->SetBoolField(TEXT("instance_synced"), Entry.bInstanceSynced != 0);
		KeysArr.Add(MakeShared<FJsonValueObject>(KeyObj));
	}
	ResultObj->SetArrayField(TEXT("keys"), KeysArr);

	return ResultObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// read_state_tree
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Map from node instance GUID → array of {TargetProperty, SourceDescription}
	using FBindingMap = TMap<FGuid, TArray<TPair<FString, FString>>>;

	// Build a lookup map of all property bindings keyed by target struct ID
	FBindingMap BuildBindingMap(const UStateTreeEditorData* EditorData)
	{
		FBindingMap Map;
		const FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
		if (!Bindings) return Map;

		for (const FStateTreePropertyPathBinding& Binding : Bindings->GetBindings())
		{
			const FPropertyBindingPath& TargetPath = Binding.GetTargetPath();
			const FPropertyBindingPath& SourcePath = Binding.GetSourcePath();

			FGuid TargetStructID = TargetPath.GetStructID();
			if (!TargetStructID.IsValid()) continue;

			// Target property name: the path segments after the struct
			FString TargetPropName = TargetPath.ToString();

			// Source: resolve to display name via struct descriptor + path
			FString SourceStr;
			FGuid SourceStructID = SourcePath.GetStructID();
			TInstancedStruct<FPropertyBindingBindableStructDescriptor> SourceDesc;
			if (SourceStructID.IsValid() && EditorData->GetBindableStructByID(SourceStructID, SourceDesc))
			{
				SourceStr = SourceDesc.Get<FPropertyBindingBindableStructDescriptor>().Name.ToString();
				FString PathStr = SourcePath.ToString();
				if (!PathStr.IsEmpty())
				{
					SourceStr += TEXT(".");
					SourceStr += PathStr;
				}
			}
			else
			{
				SourceStr = SourcePath.ToString();
			}

			if (!TargetPropName.IsEmpty() && !SourceStr.IsEmpty())
			{
				Map.FindOrAdd(TargetStructID).Emplace(TargetPropName, SourceStr);
			}
		}
		return Map;
	}

	// Serialize all EditDefaultsOnly UPROPERTYs of a UStruct instance to JSON.
	TSharedPtr<FJsonObject> StructInstanceToJson(const UScriptStruct* Struct, const void* StructMem)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Struct || !StructMem) return Obj;

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;

			const void* ValPtr = Prop->ContainerPtrToValuePtr<void>(StructMem);
			FString ValStr;
			Prop->ExportTextItem_Direct(ValStr, ValPtr, nullptr, nullptr, PPF_None);
			if (!ValStr.IsEmpty() && ValStr.Len() < 2048)
				Obj->SetStringField(Prop->GetName(), ValStr);
		}
		return Obj;
	}

	// Serialize a FStateTreeEditorNode (used for Tasks, Conditions, Evaluators, etc.)
	TSharedPtr<FJsonObject> EditorNodeToJson(const FStateTreeEditorNode& Node, const FBindingMap& Bindings)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

		// Node struct class (e.g. FSTTask_MoveToActor)
		const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
		if (NodeStruct)
		{
			Obj->SetStringField(TEXT("class"), NodeStruct->GetName());
		}

		// Blueprint task class lives in InstanceObject (UStateTreeTaskBlueprintBase subclass)
		if (Node.InstanceObject)
		{
			Obj->SetStringField(TEXT("instance_class"), Node.InstanceObject->GetClass()->GetName());

			// Dump editable properties on the BP task instance
			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> PropIt(Node.InstanceObject->GetClass()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
				if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
				if (Prop->GetOwnerClass() == UObject::StaticClass()) continue;

				const void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Node.InstanceObject);
				FString ValStr;
				Prop->ExportTextItem_Direct(ValStr, ValPtr, nullptr, nullptr, PPF_None);
				if (!ValStr.IsEmpty() && ValStr.Len() < 2048)
					Props->SetStringField(Prop->GetName(), ValStr);
			}
			if (Props->Values.Num() > 0)
				Obj->SetObjectField(TEXT("instance_properties"), Props);
		}
		else if (Node.Instance.IsValid())
		{
			// C++ task: instance data lives in Node.Instance (FInstancedStruct)
			TSharedPtr<FJsonObject> InstProps = StructInstanceToJson(
				Node.Instance.GetScriptStruct(),
				Node.Instance.GetMemory());
			if (InstProps->Values.Num() > 0)
				Obj->SetObjectField(TEXT("instance_properties"), InstProps);
		}

		// Node struct itself may have edit properties (rare)
		if (NodeStruct)
		{
			TSharedPtr<FJsonObject> NodeProps = StructInstanceToJson(NodeStruct, Node.Node.GetMemory());
			if (NodeProps->Values.Num() > 0)
				Obj->SetObjectField(TEXT("node_properties"), NodeProps);
		}

		// Property bindings: check both the instance ID and the node struct ID
		if (Node.ID.IsValid())
		{
			TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();

			// Bindings targeting the instance data
			if (const TArray<TPair<FString, FString>>* Found = Bindings.Find(Node.ID))
			{
				for (const auto& Pair : *Found)
					BindObj->SetStringField(Pair.Key, Pair.Value);
			}

			// Bindings targeting the node struct itself (uses GetNodeID())
			FGuid NodeID = Node.GetNodeID();
			if (const TArray<TPair<FString, FString>>* Found = Bindings.Find(NodeID))
			{
				for (const auto& Pair : *Found)
					BindObj->SetStringField(Pair.Key, Pair.Value);
			}

			if (BindObj->Values.Num() > 0)
				Obj->SetObjectField(TEXT("bindings"), BindObj);
		}

		return Obj;
	}

	TSharedPtr<FJsonObject> StateToJson(const UStateTreeState* State, const FBindingMap& Bindings)
	{
		if (!State) return nullptr;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), State->Name.ToString());
		Obj->SetStringField(TEXT("type"), UEnum::GetValueAsString(State->Type));
		Obj->SetStringField(TEXT("selection_behavior"), UEnum::GetValueAsString(State->SelectionBehavior));

		// Utility AI: Weight & Considerations (relevant for Utility-based selection behaviors)
		// Note: This feature is EXPERIMENTAL in UE 5.5+ — API may change.
		Obj->SetNumberField(TEXT("weight"), State->Weight);

		// Required event to enter this state
		if (State->bHasRequiredEventToEnter && State->RequiredEventToEnter.Tag.IsValid())
			Obj->SetStringField(TEXT("required_event_to_enter"), State->RequiredEventToEnter.Tag.ToString());

		if (State->Considerations.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ConsArr;
			for (const FStateTreeEditorNode& N : State->Considerations)
				ConsArr.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(N, Bindings)));
			Obj->SetArrayField(TEXT("considerations"), ConsArr);
		}

		// Enter conditions
		if (State->EnterConditions.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FStateTreeEditorNode& N : State->EnterConditions)
				Arr.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(N, Bindings)));
			Obj->SetArrayField(TEXT("enter_conditions"), Arr);
		}

		// Tasks
		if (State->Tasks.Num() > 0)
		{
			Obj->SetStringField(TEXT("tasks_completion"), UEnum::GetValueAsString(State->TasksCompletion));
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FStateTreeEditorNode& N : State->Tasks)
				Arr.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(N, Bindings)));
			Obj->SetArrayField(TEXT("tasks"), Arr);
		}

		// Transitions
		if (State->Transitions.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FStateTreeTransition& T : State->Transitions)
			{
				TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
				TObj->SetStringField(TEXT("trigger"), UEnum::GetValueAsString(T.Trigger));
				TObj->SetStringField(TEXT("priority"), UEnum::GetValueAsString(T.Priority));
				TObj->SetStringField(TEXT("link_type"), UEnum::GetValueAsString(T.State.LinkType));
				if (!T.State.Name.IsNone())
					TObj->SetStringField(TEXT("target_state"), T.State.Name.ToString());
				if (T.bDelayTransition)
				{
					TObj->SetNumberField(TEXT("delay_duration"), T.DelayDuration);
					TObj->SetNumberField(TEXT("delay_random_variance"), T.DelayRandomVariance);
				}
				if (!T.bTransitionEnabled)
					TObj->SetBoolField(TEXT("enabled"), false);

				// Event tag for OnEvent transitions
				if (T.RequiredEvent.Tag.IsValid())
					TObj->SetStringField(TEXT("event_tag"), T.RequiredEvent.Tag.ToString());

				if (T.Conditions.Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> CondArr;
					for (const FStateTreeEditorNode& C : T.Conditions)
						CondArr.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(C, Bindings)));
					TObj->SetArrayField(TEXT("conditions"), CondArr);
				}

				Arr.Add(MakeShared<FJsonValueObject>(TObj));
			}
			Obj->SetArrayField(TEXT("transitions"), Arr);
		}

		// Child states (recursive)
		if (State->Children.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (UStateTreeState* Child : State->Children)
			{
				TSharedPtr<FJsonObject> ChildObj = StateToJson(Child, Bindings);
				if (ChildObj)
					Arr.Add(MakeShared<FJsonValueObject>(ChildObj));
			}
			Obj->SetArrayField(TEXT("children"), Arr);
		}

		return Obj;
	}
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleReadStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	UStateTree* ST = Cast<UStateTree>(Loaded);
	if (!ST)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load StateTree at: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("name"), ST->GetName());
	ResultObj->SetStringField(TEXT("asset_path"), AssetPath);

	if (const UStateTreeSchema* Schema = ST->GetSchema())
	{
		ResultObj->SetStringField(TEXT("schema"), Schema->GetClass()->GetName());
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(ST->EditorData);
	if (!EditorData)
	{
		ResultObj->SetStringField(TEXT("warning"), TEXT("EditorData not available (asset may have been cooked or stripped)"));
		return ResultObj;
	}

	// Global parameters (root)
	const FInstancedPropertyBag& RootBag = EditorData->GetRootParametersPropertyBag();
	TArray<TSharedPtr<FJsonValue>> ParamArr;
	if (const UPropertyBag* BagStruct = RootBag.GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Desc.Name.ToString());
			if (Desc.CachedProperty)
				P->SetStringField(TEXT("type"), Desc.CachedProperty->GetCPPType());
			ParamArr.Add(MakeShared<FJsonValueObject>(P));
		}
	}
	ResultObj->SetArrayField(TEXT("global_parameters"), ParamArr);

	// Build property binding lookup map
	const FBindingMap BindingMap = BuildBindingMap(EditorData);

	// Evaluators
	TArray<TSharedPtr<FJsonValue>> EvalArr;
	for (const FStateTreeEditorNode& N : EditorData->Evaluators)
		EvalArr.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(N, BindingMap)));
	ResultObj->SetArrayField(TEXT("evaluators"), EvalArr);

	// Global tasks
	TArray<TSharedPtr<FJsonValue>> GTaskArr;
	for (const FStateTreeEditorNode& N : EditorData->GlobalTasks)
		GTaskArr.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(N, BindingMap)));
	ResultObj->SetArrayField(TEXT("global_tasks"), GTaskArr);

	// State hierarchy (subtrees: usually one "Root", or named subtrees)
	TArray<TSharedPtr<FJsonValue>> StatesArr;
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		TSharedPtr<FJsonObject> StateObj = StateToJson(SubTree, BindingMap);
		if (StateObj)
			StatesArr.Add(MakeShared<FJsonValueObject>(StateObj));
	}
	ResultObj->SetArrayField(TEXT("states"), StatesArr);

	return ResultObj;
}