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