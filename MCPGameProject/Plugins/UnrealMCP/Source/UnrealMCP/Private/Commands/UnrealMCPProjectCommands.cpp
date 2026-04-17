#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "GameFramework/InputSettings.h"
#include "EditorAssetLibrary.h"
#include "JsonObjectConverter.h"

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
			FString ValueStr;
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
			if (!ValueStr.IsEmpty())
				PropObj->SetStringField(TEXT("value"), ValueStr);
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

	return ResultObj;
}