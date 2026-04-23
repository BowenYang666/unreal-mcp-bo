#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateMachineGraph.h"

FUnrealMCPBlueprintCommands::FUnrealMCPBlueprintCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_blueprint"))
    {
        return HandleCreateBlueprint(Params);
    }
    else if (CommandType == TEXT("add_component_to_blueprint"))
    {
        return HandleAddComponentToBlueprint(Params);
    }
    else if (CommandType == TEXT("set_component_property"))
    {
        return HandleSetComponentProperty(Params);
    }
    else if (CommandType == TEXT("set_physics_properties"))
    {
        return HandleSetPhysicsProperties(Params);
    }
    else if (CommandType == TEXT("compile_blueprint"))
    {
        return HandleCompileBlueprint(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    else if (CommandType == TEXT("set_blueprint_property"))
    {
        return HandleSetBlueprintProperty(Params);
    }
    else if (CommandType == TEXT("set_static_mesh_properties"))
    {
        return HandleSetStaticMeshProperties(Params);
    }
    else if (CommandType == TEXT("set_pawn_properties"))
    {
        return HandleSetPawnProperties(Params);
    }
    else if (CommandType == TEXT("read_blueprint"))
    {
        return HandleReadBlueprint(Params);
    }
    else if (CommandType == TEXT("list_blueprints"))
    {
        return HandleListBlueprints(Params);
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Check if blueprint already exists
    FString PackagePath;
    FString AssetName;
    if (BlueprintName.StartsWith(TEXT("/Game/")) || BlueprintName.StartsWith(TEXT("/Script/")))
    {
        // Full path provided — split into package path and asset name
        int32 LastSlash;
        if (BlueprintName.FindLastChar('/', LastSlash))
        {
            PackagePath = BlueprintName.Left(LastSlash + 1);
            AssetName = BlueprintName.Mid(LastSlash + 1);
        }
        else
        {
            PackagePath = TEXT("/Game/Blueprints/");
            AssetName = BlueprintName;
        }
    }
    else
    {
        PackagePath = TEXT("/Game/Blueprints/");
        AssetName = BlueprintName;
    }
    if (UEditorAssetLibrary::DoesAssetExist(PackagePath + AssetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *BlueprintName));
    }

    // Create the blueprint factory
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    
    // Handle parent class
    FString ParentClass;
    Params->TryGetStringField(TEXT("parent_class"), ParentClass);
    
    // Default to Actor if no parent class specified
    UClass* SelectedParentClass = AActor::StaticClass();
    
    // Try to find the specified parent class
    if (!ParentClass.IsEmpty())
    {
        UClass* FoundClass = nullptr;

        // If it looks like a full class path (e.g. /Script/Engine.APawn), try loading directly
        if (ParentClass.Contains(TEXT(".")))
        {
            FoundClass = LoadClass<UObject>(nullptr, *ParentClass);
        }

        if (!FoundClass)
        {
            FString ClassName = ParentClass;
            // Strip path prefix if present (e.g. /Script/ThirdPersonTest1.DamageNumberActor -> DamageNumberActor)
            int32 DotIndex;
            if (ClassName.FindLastChar('.', DotIndex))
            {
                ClassName = ClassName.Mid(DotIndex + 1);
            }

            if (!ClassName.StartsWith(TEXT("A")))
            {
                ClassName = TEXT("A") + ClassName;
            }
            
            // First try direct StaticClass lookup for common classes
            if (ClassName == TEXT("APawn"))
            {
                FoundClass = APawn::StaticClass();
            }
            else if (ClassName == TEXT("AActor"))
            {
                FoundClass = AActor::StaticClass();
            }
            else
            {
                // Try loading the class using LoadClass which is more reliable than FindObject
                const FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
                FoundClass = LoadClass<AActor>(nullptr, *ClassPath);
                
                if (!FoundClass)
                {
                    // Try alternate paths if not found
                    const FString GameClassPath = FString::Printf(TEXT("/Script/Game.%s"), *ClassName);
                    FoundClass = LoadClass<AActor>(nullptr, *GameClassPath);
                }
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ParentClass);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find specified parent class '%s', defaulting to AActor"), 
                *ParentClass);
        }
    }
    
    Factory->ParentClass = SelectedParentClass;

    // Create the blueprint
    UPackage* Package = CreatePackage(*(PackagePath + AssetName));
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), PackagePath + AssetName);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create blueprint"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Create the component - dynamically find the component class by name
    UClass* ComponentClass = nullptr;

    // Try to find the class with exact name first
    ComponentClass = FindFirstObject<UClass>(*ComponentType, EFindFirstObjectOptions::NativeFirst);
    
    // If not found, try with "Component" suffix
    if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
    {
        FString ComponentTypeWithSuffix = ComponentType + TEXT("Component");
        ComponentClass = FindFirstObject<UClass>(*ComponentTypeWithSuffix, EFindFirstObjectOptions::NativeFirst);
    }
    
    // If still not found, try with "U" prefix
    if (!ComponentClass && !ComponentType.StartsWith(TEXT("U")))
    {
        FString ComponentTypeWithPrefix = TEXT("U") + ComponentType;
        ComponentClass = FindFirstObject<UClass>(*ComponentTypeWithPrefix, EFindFirstObjectOptions::NativeFirst);
        
        // Try with both prefix and suffix
        if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
        {
            FString ComponentTypeWithBoth = TEXT("U") + ComponentType + TEXT("Component");
            ComponentClass = FindFirstObject<UClass>(*ComponentTypeWithBoth, EFindFirstObjectOptions::NativeFirst);
        }
    }
    
    // Verify that the class is a valid component type
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown component type: %s"), *ComponentType));
    }

    // Add the component to the blueprint
    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
    if (NewNode)
    {
        // Set transform if provided
        USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate);
        if (SceneComponent)
        {
            if (Params->HasField(TEXT("location")))
            {
                SceneComponent->SetRelativeLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
            }
            if (Params->HasField(TEXT("rotation")))
            {
                SceneComponent->SetRelativeRotation(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
            }
            if (Params->HasField(TEXT("scale")))
            {
                SceneComponent->SetRelativeScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
            }
        }

        // Add to root if no parent specified
        Blueprint->SimpleConstructionScript->AddNode(NewNode);

        // Compile the blueprint
        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("component_name"), ComponentName);
        ResultObj->SetStringField(TEXT("component_type"), ComponentType);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to add component to blueprint"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Log all input parameters for debugging
    UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Blueprint: %s, Component: %s, Property: %s"), 
        *BlueprintName, *ComponentName, *PropertyName);
    
    // Log property_value if available
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        FString ValueType;
        
        switch(JsonValue->Type)
        {
            case EJson::Boolean: ValueType = FString::Printf(TEXT("Boolean: %s"), JsonValue->AsBool() ? TEXT("true") : TEXT("false")); break;
            case EJson::Number: ValueType = FString::Printf(TEXT("Number: %f"), JsonValue->AsNumber()); break;
            case EJson::String: ValueType = FString::Printf(TEXT("String: %s"), *JsonValue->AsString()); break;
            case EJson::Array: ValueType = TEXT("Array"); break;
            case EJson::Object: ValueType = TEXT("Object"); break;
            default: ValueType = TEXT("Unknown"); break;
        }
        
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Value Type: %s"), *ValueType);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - No property_value provided"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Blueprint not found: %s"), *BlueprintName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Blueprint found: %s (Class: %s)"), 
            *BlueprintName, 
            Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetName() : TEXT("NULL"));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Searching for component %s in blueprint nodes"), *ComponentName);
    
    if (!Blueprint->SimpleConstructionScript)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - SimpleConstructionScript is NULL for blueprint %s"), *BlueprintName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid blueprint construction script"));
    }
    
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node)
        {
            UE_LOG(LogTemp, Verbose, TEXT("SetComponentProperty - Found node: %s"), *Node->GetVariableName().ToString());
            if (Node->GetVariableName().ToString() == ComponentName)
            {
                ComponentNode = Node;
                break;
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Found NULL node in blueprint"));
        }
    }

    if (!ComponentNode)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component not found: %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Component found: %s (Class: %s)"), 
            *ComponentName, 
            ComponentNode->ComponentTemplate ? *ComponentNode->ComponentTemplate->GetClass()->GetName() : TEXT("NULL"));
    }

    // Get the component template
    UObject* ComponentTemplate = ComponentNode->ComponentTemplate;
    if (!ComponentTemplate)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component template is NULL for %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid component template"));
    }

    // Check if this is a Spring Arm component and log special debug info
    if (ComponentTemplate->GetClass()->GetName().Contains(TEXT("SpringArm")))
    {
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - SpringArm component detected! Class: %s"), 
            *ComponentTemplate->GetClass()->GetPathName());
            
        // Log all properties of the SpringArm component class
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - SpringArm properties:"));
        for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            UE_LOG(LogTemp, Warning, TEXT("  - %s (%s)"), *Prop->GetName(), *Prop->GetCPPType());
        }

        // Special handling for Spring Arm properties
        if (Params->HasField(TEXT("property_value")))
        {
            TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
            
            // Get the property using the new FField system
            FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), *PropertyName);
            if (!Property)
            {
                UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Property %s not found on SpringArm component"), *PropertyName);
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Property %s not found on SpringArm component"), *PropertyName));
            }

            // Create a scope guard to ensure property cleanup
            struct FScopeGuard
            {
                UObject* Object;
                FScopeGuard(UObject* InObject) : Object(InObject) 
                {
                    if (Object)
                    {
                        Object->Modify();
                    }
                }
                ~FScopeGuard()
                {
                    if (Object)
                    {
                        Object->PostEditChange();
                    }
                }
            } ScopeGuard(ComponentTemplate);

            bool bSuccess = false;
            FString ErrorMessage;

            // Handle specific Spring Arm property types
            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
            {
                if (JsonValue->Type == EJson::Number)
                {
                    const float Value = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting float property %s to %f"), *PropertyName, Value);
                    FloatProp->SetPropertyValue_InContainer(ComponentTemplate, Value);
                    bSuccess = true;
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
            {
                if (JsonValue->Type == EJson::Boolean)
                {
                    const bool Value = JsonValue->AsBool();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting bool property %s to %d"), *PropertyName, Value);
                    BoolProp->SetPropertyValue_InContainer(ComponentTemplate, Value);
                    bSuccess = true;
                }
            }
            else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Handling struct property %s of type %s"), 
                    *PropertyName, *StructProp->Struct->GetName());
                
                // Special handling for common Spring Arm struct properties
                if (StructProp->Struct == TBaseStructure<FVector>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FVector Vec(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            StructProp->CopySingleValue(PropertyAddr, &Vec);
                            bSuccess = true;
                        }
                    }
                }
                else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FRotator Rot(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            StructProp->CopySingleValue(PropertyAddr, &Rot);
                            bSuccess = true;
                        }
                    }
                }
            }

            if (bSuccess)
            {
                // Mark the blueprint as modified
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Successfully set SpringArm property %s"), *PropertyName);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("component"), ComponentName);
                ResultObj->SetStringField(TEXT("property"), PropertyName);
                ResultObj->SetBoolField(TEXT("success"), true);
                return ResultObj;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set SpringArm property %s"), *PropertyName);
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to set SpringArm property %s"), *PropertyName));
            }
        }
    }

    // Regular property handling for non-Spring Arm components continues...

    // Set the property value
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        
        // Get the property
        FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), *PropertyName);
        if (!Property)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Property %s not found on component %s"), 
                *PropertyName, *ComponentName);
            
            // List all available properties for this component
            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Available properties for %s:"), *ComponentName);
            for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                UE_LOG(LogTemp, Warning, TEXT("  - %s (%s)"), *Prop->GetName(), *Prop->GetCPPType());
            }
            
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Property %s not found on component %s"), *PropertyName, *ComponentName));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property found: %s (Type: %s)"), 
                *PropertyName, *Property->GetCPPType());
        }

        bool bSuccess = false;
        FString ErrorMessage;

        // Handle different property types
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Attempting to set property %s"), *PropertyName);
        
        // Add try-catch block to catch and log any crashes
        try
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                // Handle vector properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is a struct: %s"), 
                    StructProp->Struct ? *StructProp->Struct->GetName() : TEXT("NULL"));
                    
                if (StructProp->Struct == TBaseStructure<FVector>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        // Handle array input [x, y, z]
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FVector Vec(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting Vector(%f, %f, %f)"), 
                                Vec.X, Vec.Y, Vec.Z);
                            StructProp->CopySingleValue(PropertyAddr, &Vec);
                            bSuccess = true;
                        }
                        else
                        {
                            ErrorMessage = FString::Printf(TEXT("Vector property requires 3 values, got %d"), Arr.Num());
                            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                        }
                    }
                    else if (JsonValue->Type == EJson::Number)
                    {
                        // Handle scalar input (sets all components to same value)
                        float Value = JsonValue->AsNumber();
                        FVector Vec(Value, Value, Value);
                        void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting Vector(%f, %f, %f) from scalar"), 
                            Vec.X, Vec.Y, Vec.Z);
                        StructProp->CopySingleValue(PropertyAddr, &Vec);
                        bSuccess = true;
                    }
                    else
                    {
                        ErrorMessage = TEXT("Vector property requires either a single number or array of 3 numbers");
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                    }
                }
                else
                {
                    // Handle other struct properties using default handler
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Using generic struct handler for %s"), 
                        *PropertyName);
                    bSuccess = FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage);
                    if (!bSuccess)
                    {
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set struct property: %s"), *ErrorMessage);
                    }
                }
            }
            else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
            {
                // Handle enum properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is an enum"));
                if (JsonValue->Type == EJson::String)
                {
                    FString EnumValueName = JsonValue->AsString();
                    UEnum* Enum = EnumProp->GetEnum();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting enum from string: %s"), *EnumValueName);
                    
                    if (Enum)
                    {
                        int64 EnumValue = Enum->GetValueByNameString(EnumValueName);
                        
                        if (EnumValue != INDEX_NONE)
                        {
                            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Found enum value: %lld"), EnumValue);
                            EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
                                ComponentTemplate, 
                                EnumValue
                            );
                            bSuccess = true;
                        }
                        else
                        {
                            // List all possible enum values
                            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Available enum values for %s:"), 
                                *Enum->GetName());
                            for (int32 i = 0; i < Enum->NumEnums(); i++)
                            {
                                UE_LOG(LogTemp, Warning, TEXT("  - %s (%lld)"), 
                                    *Enum->GetNameStringByIndex(i),
                                    Enum->GetValueByIndex(i));
                            }
                            
                            ErrorMessage = FString::Printf(TEXT("Invalid enum value '%s' for property %s"), 
                                *EnumValueName, *PropertyName);
                            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                        }
                    }
                    else
                    {
                        ErrorMessage = TEXT("Enum object is NULL");
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                    }
                }
                else if (JsonValue->Type == EJson::Number)
                {
                    // Allow setting enum by integer value
                    int64 EnumValue = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting enum from number: %lld"), EnumValue);
                    EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
                        ComponentTemplate, 
                        EnumValue
                    );
                    bSuccess = true;
                }
                else
                {
                    ErrorMessage = TEXT("Enum property requires either a string name or integer value");
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                }
            }
            else if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
            {
                // Handle numeric properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is numeric: IsInteger=%d, IsFloat=%d"), 
                    NumericProp->IsInteger(), NumericProp->IsFloatingPoint());
                    
                if (JsonValue->Type == EJson::Number)
                {
                    double Value = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting numeric value: %f"), Value);
                    
                    if (NumericProp->IsInteger())
                    {
                        NumericProp->SetIntPropertyValue(ComponentTemplate, (int64)Value);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Set integer value: %lld"), (int64)Value);
                        bSuccess = true;
                    }
                    else if (NumericProp->IsFloatingPoint())
                    {
                        NumericProp->SetFloatingPointPropertyValue(ComponentTemplate, Value);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Set float value: %f"), Value);
                        bSuccess = true;
                    }
                }
                else
                {
                    ErrorMessage = TEXT("Numeric property requires a number value");
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                }
            }
            else
            {
                // Handle all other property types using default handler
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Using generic property handler for %s (Type: %s)"), 
                    *PropertyName, *Property->GetCPPType());
                bSuccess = FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage);
                if (!bSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set property: %s"), *ErrorMessage);
                }
            }
        }
        catch (const std::exception& Ex)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - EXCEPTION: %s"), ANSI_TO_TCHAR(Ex.what()));
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Exception while setting property %s: %s"), *PropertyName, ANSI_TO_TCHAR(Ex.what())));
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - UNKNOWN EXCEPTION occurred while setting property %s"), *PropertyName);
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown exception while setting property %s"), *PropertyName));
        }

        if (bSuccess)
        {
            // Mark the blueprint as modified
            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Successfully set property %s on component %s"), 
                *PropertyName, *ComponentName);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("component"), ComponentName);
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set property %s: %s"), 
                *PropertyName, *ErrorMessage);
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Missing 'property_value' parameter"));
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Set physics properties
    if (Params->HasField(TEXT("simulate_physics")))
    {
        PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
    }

    if (Params->HasField(TEXT("mass")))
    {
        float Mass = Params->GetNumberField(TEXT("mass"));
        // In UE5.5, use proper overrideMass instead of just scaling
        PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
        UE_LOG(LogTemp, Display, TEXT("Set mass for component %s to %f kg"), *ComponentName, Mass);
    }

    if (Params->HasField(TEXT("linear_damping")))
    {
        PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
    }

    if (Params->HasField(TEXT("angular_damping")))
    {
        PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Compile the blueprint
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform);
    if (NewActor)
    {
        NewActor->SetActorLabel(*ActorName);
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the default object
    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
    if (!DefaultObject)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get default object"));
    }

    // Set the property value
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, PropertyName, JsonValue, ErrorMessage))
        {
            // Mark the blueprint as modified
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Set static mesh properties
    if (Params->HasField(TEXT("static_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("static_mesh"));
        UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (Mesh)
        {
            MeshComponent->SetStaticMesh(Mesh);
        }
    }

    if (Params->HasField(TEXT("material")))
    {
        FString MaterialPath = Params->GetStringField(TEXT("material"));
        UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (Material)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetPawnProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the default object
    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
    if (!DefaultObject)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get default object"));
    }

    // Track if any properties were set successfully
    bool bAnyPropertiesSet = false;
    TSharedPtr<FJsonObject> ResultsObj = MakeShared<FJsonObject>();
    
    // Set auto possess player if specified
    if (Params->HasField(TEXT("auto_possess_player")))
    {
        TSharedPtr<FJsonValue> AutoPossessValue = Params->Values.FindRef(TEXT("auto_possess_player"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, TEXT("AutoPossessPlayer"), AutoPossessValue, ErrorMessage))
        {
            bAnyPropertiesSet = true;
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), true);
            ResultsObj->SetObjectField(TEXT("AutoPossessPlayer"), PropResultObj);
        }
        else
        {
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), false);
            PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
            ResultsObj->SetObjectField(TEXT("AutoPossessPlayer"), PropResultObj);
        }
    }
    
    // Set controller rotation properties
    const TCHAR* RotationProps[] = {
        TEXT("bUseControllerRotationYaw"),
        TEXT("bUseControllerRotationPitch"),
        TEXT("bUseControllerRotationRoll")
    };
    
    const TCHAR* ParamNames[] = {
        TEXT("use_controller_rotation_yaw"),
        TEXT("use_controller_rotation_pitch"),
        TEXT("use_controller_rotation_roll")
    };
    
    for (int32 i = 0; i < 3; i++)
    {
        if (Params->HasField(ParamNames[i]))
        {
            TSharedPtr<FJsonValue> Value = Params->Values.FindRef(ParamNames[i]);
            
            FString ErrorMessage;
            if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, RotationProps[i], Value, ErrorMessage))
            {
                bAnyPropertiesSet = true;
                TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
                PropResultObj->SetBoolField(TEXT("success"), true);
                ResultsObj->SetObjectField(RotationProps[i], PropResultObj);
            }
            else
            {
                TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
                PropResultObj->SetBoolField(TEXT("success"), false);
                PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
                ResultsObj->SetObjectField(RotationProps[i], PropResultObj);
            }
        }
    }
    
    // Set can be damaged property
    if (Params->HasField(TEXT("can_be_damaged")))
    {
        TSharedPtr<FJsonValue> Value = Params->Values.FindRef(TEXT("can_be_damaged"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, TEXT("bCanBeDamaged"), Value, ErrorMessage))
        {
            bAnyPropertiesSet = true;
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), true);
            ResultsObj->SetObjectField(TEXT("bCanBeDamaged"), PropResultObj);
        }
        else
        {
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), false);
            PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
            ResultsObj->SetObjectField(TEXT("bCanBeDamaged"), PropResultObj);
        }
    }

    // Mark the blueprint as modified if any properties were set
    if (bAnyPropertiesSet)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    else if (ResultsObj->Values.Num() == 0)
    {
        // No properties were specified
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No properties specified to set"));
    }

    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetStringField(TEXT("blueprint"), BlueprintName);
    ResponseObj->SetBoolField(TEXT("success"), bAnyPropertiesSet);
    ResponseObj->SetObjectField(TEXT("results"), ResultsObj);
    return ResponseObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!FUnrealMCPCommonUtils::GetBlueprintPath(Params, BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Check if a full asset path was provided
    UBlueprint* Blueprint = nullptr;
    FString AssetPath;

    if (BlueprintName.StartsWith(TEXT("/Game/")) || BlueprintName.StartsWith(TEXT("/Script/")))
    {
        // Full path provided - load directly
        AssetPath = BlueprintName;
        Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    }
    else
    {
        // Try default path first
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
        AssetPath = TEXT("/Game/Blueprints/") + BlueprintName;

        // If not found, try searching the asset registry
        if (!Blueprint)
        {
            FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

            FARFilter Filter;
            Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
            Filter.bRecursiveClasses = true;
            Filter.bRecursivePaths = true;
            Filter.PackagePaths.Add(FName(TEXT("/Game")));

            TArray<FAssetData> AssetDataList;
            AssetRegistry.GetAssets(Filter, AssetDataList);

            for (const FAssetData& AssetData : AssetDataList)
            {
                if (AssetData.AssetName.ToString() == BlueprintName)
                {
                    Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
                    AssetPath = AssetData.GetObjectPathString();
                    break;
                }
            }
        }
    }

    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Build result
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("path"), AssetPath);

    // Parent class info
    if (Blueprint->ParentClass)
    {
        ResultObj->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
        ResultObj->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass->GetPathName());
    }

    // Blueprint type
    ResultObj->SetStringField(TEXT("blueprint_type"), 
        Blueprint->BlueprintType == BPTYPE_Normal ? TEXT("Normal") :
        Blueprint->BlueprintType == BPTYPE_MacroLibrary ? TEXT("MacroLibrary") :
        Blueprint->BlueprintType == BPTYPE_Interface ? TEXT("Interface") :
        Blueprint->BlueprintType == BPTYPE_FunctionLibrary ? TEXT("FunctionLibrary") :
        TEXT("Other"));

    // ===== Components =====
    TArray<TSharedPtr<FJsonValue>> ComponentsArray;
    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node) continue;

            TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
            CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());

            if (Node->ComponentTemplate)
            {
                CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());

                // If it's a scene component, include transform info
                USceneComponent* SceneComp = Cast<USceneComponent>(Node->ComponentTemplate);
                if (SceneComp)
                {
                    FVector Loc = SceneComp->GetRelativeLocation();
                    FRotator Rot = SceneComp->GetRelativeRotation();
                    FVector Scale = SceneComp->GetRelativeScale3D();

                    TArray<TSharedPtr<FJsonValue>> LocArr;
                    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
                    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
                    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
                    CompObj->SetArrayField(TEXT("location"), LocArr);

                    TArray<TSharedPtr<FJsonValue>> RotArr;
                    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
                    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
                    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
                    CompObj->SetArrayField(TEXT("rotation"), RotArr);

                    TArray<TSharedPtr<FJsonValue>> ScaleArr;
                    ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
                    ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
                    ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));
                    CompObj->SetArrayField(TEXT("scale"), ScaleArr);
                }

                // Include key properties of the component template
                TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> PropIt(Node->ComponentTemplate->GetClass()); PropIt; ++PropIt)
                {
                    FProperty* Prop = *PropIt;
                    // Only include properties that belong directly to this class (not inherited from UObject/UActorComponent)
                    if (Prop->GetOwnerClass() == UObject::StaticClass() || 
                        Prop->GetOwnerClass() == UActorComponent::StaticClass())
                    {
                        continue;
                    }

                    FString ValueStr;
                    const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node->ComponentTemplate);
                    Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
                    
                    if (!ValueStr.IsEmpty())
                    {
                        PropsObj->SetStringField(Prop->GetName(), ValueStr);
                    }
                }
                CompObj->SetObjectField(TEXT("properties"), PropsObj);

                // Parent component info
                if (Node->ParentComponentOrVariableName != NAME_None)
                {
                    CompObj->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
                }
            }

            ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
        }
    }
    ResultObj->SetArrayField(TEXT("components"), ComponentsArray);

    // ===== Variables =====
    TArray<TSharedPtr<FJsonValue>> VariablesArray;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
        
        if (Var.VarType.PinSubCategoryObject.IsValid())
        {
            VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
        }
        if (!Var.VarType.PinSubCategory.IsNone())
        {
            VarObj->SetStringField(TEXT("sub_category"), Var.VarType.PinSubCategory.ToString());
        }
        
        VarObj->SetBoolField(TEXT("is_array"), Var.VarType.IsArray());
        VarObj->SetBoolField(TEXT("is_set"), Var.VarType.IsSet());
        VarObj->SetBoolField(TEXT("is_map"), Var.VarType.IsMap());

        if (!Var.DefaultValue.IsEmpty())
        {
            VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
        }

        // Replication info
        VarObj->SetBoolField(TEXT("is_instance_editable"), 
            Var.PropertyFlags & CPF_Edit ? true : false);
        VarObj->SetBoolField(TEXT("is_blueprint_read_only"), 
            Var.PropertyFlags & CPF_BlueprintReadOnly ? true : false);

        if (!Var.Category.IsEmpty())
        {
            VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
        }
        if (Var.HasMetaData(FName(TEXT("tooltip"))))
        {
            VarObj->SetStringField(TEXT("tooltip"), Var.GetMetaData(FName(TEXT("tooltip"))));
        }

        VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    ResultObj->SetArrayField(TEXT("variables"), VariablesArray);

    // ===== Event Graphs =====
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph) continue;

        TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
        GraphObj->SetStringField(TEXT("name"), Graph->GetName());
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        TArray<TSharedPtr<FJsonValue>> NodesArray;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;

            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
            NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
            NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

            if (!Node->NodeComment.IsEmpty())
            {
                NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
            }

            // Event node specifics
            UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
            if (EventNode)
            {
                NodeObj->SetStringField(TEXT("event_name"), EventNode->EventReference.GetMemberName().ToString());
            }

            // Function call node specifics
            UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node);
            if (FuncNode)
            {
                NodeObj->SetStringField(TEXT("function_name"), FuncNode->FunctionReference.GetMemberName().ToString());
                if (FuncNode->FunctionReference.GetMemberParentClass())
                {
                    NodeObj->SetStringField(TEXT("function_class"), FuncNode->FunctionReference.GetMemberParentClass()->GetName());
                }
            }

            // Pins info
            TArray<TSharedPtr<FJsonValue>> PinsArray;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin) continue;
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
                PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);
                
                // Add linked node IDs
                if (Pin->LinkedTo.Num() > 0)
                {
                    TArray<TSharedPtr<FJsonValue>> LinkedArray;
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (LinkedPin && LinkedPin->GetOwningNode())
                        {
                            TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                            LinkObj->SetStringField(TEXT("node_id"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
                            LinkObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
                            LinkedArray.Add(MakeShared<FJsonValueObject>(LinkObj));
                        }
                    }
                    PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);
                }

                PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
            }
            NodeObj->SetArrayField(TEXT("pins"), PinsArray);

            NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
        GraphObj->SetArrayField(TEXT("nodes"), NodesArray);
        GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
    }
    ResultObj->SetArrayField(TEXT("event_graphs"), GraphsArray);

    // ===== Functions =====
    TArray<TSharedPtr<FJsonValue>> FunctionsArray;
    for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
    {
        if (!FuncGraph) continue;

        TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
        FuncObj->SetStringField(TEXT("name"), FuncGraph->GetName());
        FuncObj->SetNumberField(TEXT("node_count"), FuncGraph->Nodes.Num());
        FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
    }
    ResultObj->SetArrayField(TEXT("functions"), FunctionsArray);

    // ===== Animation Graph (AnimBP only) =====
    bool bIncludeAnimGraph = false;
    if (Params->HasField(TEXT("include_anim_graph")))
    {
        bIncludeAnimGraph = Params->GetBoolField(TEXT("include_anim_graph"));
    }

    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
    if (AnimBP && bIncludeAnimGraph)
    {
        TArray<TSharedPtr<FJsonValue>> AnimGraphsArray;

        for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
        {
            if (!FuncGraph) continue;
            // AnimGraph graphs typically have the schema UAnimationGraphSchema
            // but checking by name is simpler and reliable
            if (FuncGraph->GetName() != TEXT("AnimGraph")) continue;

            TSharedPtr<FJsonObject> AnimGraphObj = MakeShared<FJsonObject>();
            AnimGraphObj->SetStringField(TEXT("name"), TEXT("AnimGraph"));

            TArray<TSharedPtr<FJsonValue>> NodesArray;
            TArray<TSharedPtr<FJsonValue>> ConnectionsArray;

            for (UEdGraphNode* Node : FuncGraph->Nodes)
            {
                if (!Node) continue;
                UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
                if (!AnimNode) continue;

                TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
                NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
                NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

                // Determine simplified type from class name
                FString ClassName = Node->GetClass()->GetName();
                FString TypeName = ClassName;
                if (TypeName.StartsWith(TEXT("AnimGraphNode_")))
                {
                    TypeName = TypeName.RightChop(14);
                }
                NodeObj->SetStringField(TEXT("type"), TypeName);
                NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

                // StateMachine: expand states and transitions
                UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
                if (SMNode && SMNode->EditorStateMachineGraph)
                {
                    UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
                    NodeObj->SetStringField(TEXT("name"), SMGraph->GetName());

                    TArray<TSharedPtr<FJsonValue>> StatesArray;
                    TArray<TSharedPtr<FJsonValue>> TransitionsArray;

                    for (UEdGraphNode* SMGraphNode : SMGraph->Nodes)
                    {
                        // --- States ---
                        UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMGraphNode);
                        if (StateNode)
                        {
                            TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
                            StateObj->SetStringField(TEXT("name"), StateNode->GetStateName());

                            // Nodes inside the state
                            if (StateNode->BoundGraph)
                            {
                                TArray<TSharedPtr<FJsonValue>> StateNodesArray;
                                for (UEdGraphNode* InnerNode : StateNode->BoundGraph->Nodes)
                                {
                                    UAnimGraphNode_Base* InnerAnimNode = Cast<UAnimGraphNode_Base>(InnerNode);
                                    if (!InnerAnimNode) continue;

                                    FString InnerClass = InnerNode->GetClass()->GetName();
                                    // Skip StateResult nodes (they're just the output connector)
                                    if (InnerClass.Contains(TEXT("StateResult"))) continue;

                                    TSharedPtr<FJsonObject> InnerObj = MakeShared<FJsonObject>();
                                    FString InnerType = InnerClass;
                                    if (InnerType.StartsWith(TEXT("AnimGraphNode_")))
                                        InnerType = InnerType.RightChop(14);
                                    InnerObj->SetStringField(TEXT("type"), InnerType);
                                    InnerObj->SetStringField(TEXT("title"), InnerNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                                    StateNodesArray.Add(MakeShared<FJsonValueObject>(InnerObj));
                                }
                                StateObj->SetArrayField(TEXT("nodes"), StateNodesArray);
                            }
                            StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
                        }

                        // --- Transitions ---
                        UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMGraphNode);
                        if (TransNode)
                        {
                            TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();

                            UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
                            UAnimStateNodeBase* NextState = TransNode->GetNextState();
                            if (PrevState)
                                TransObj->SetStringField(TEXT("from"), PrevState->GetStateName());
                            if (NextState)
                                TransObj->SetStringField(TEXT("to"), NextState->GetStateName());

                            TransObj->SetNumberField(TEXT("blend_time"), TransNode->CrossfadeDuration);

                            // Extract variables used in transition condition
                            if (TransNode->BoundGraph)
                            {
                                TArray<FString> VarsUsed;
                                for (UEdGraphNode* CondNode : TransNode->BoundGraph->Nodes)
                                {
                                    UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(CondNode);
                                    if (VarGet)
                                    {
                                        VarsUsed.AddUnique(VarGet->VariableReference.GetMemberName().ToString());
                                    }
                                }
                                if (VarsUsed.Num() > 0)
                                {
                                    TArray<TSharedPtr<FJsonValue>> VarsArray;
                                    for (const FString& Var : VarsUsed)
                                        VarsArray.Add(MakeShared<FJsonValueString>(Var));
                                    TransObj->SetArrayField(TEXT("variables_used"), VarsArray);
                                }
                            }

                            TransitionsArray.Add(MakeShared<FJsonValueObject>(TransObj));
                        }
                    }

                    NodeObj->SetArrayField(TEXT("states"), StatesArray);
                    NodeObj->SetArrayField(TEXT("transitions"), TransitionsArray);
                }

                NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));

                // Build connections from output pins
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin || Pin->Direction != EGPD_Output) continue;
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
                        TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                        ConnObj->SetStringField(TEXT("from"), Node->NodeGuid.ToString());
                        ConnObj->SetStringField(TEXT("to"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
                        ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
                        ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                    }
                }
            }

            AnimGraphObj->SetArrayField(TEXT("nodes"), NodesArray);
            AnimGraphObj->SetArrayField(TEXT("connections"), ConnectionsArray);
            AnimGraphsArray.Add(MakeShared<FJsonValueObject>(AnimGraphObj));
        }

        ResultObj->SetArrayField(TEXT("anim_graphs"), AnimGraphsArray);
    }

    // ===== Interfaces =====
    TArray<TSharedPtr<FJsonValue>> InterfacesArray;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Interface)
        {
            InterfacesArray.Add(MakeShared<FJsonValueString>(Interface.Interface->GetName()));
        }
    }
    ResultObj->SetArrayField(TEXT("interfaces"), InterfacesArray);

    // ===== Class Defaults (CDO overrides) =====
    if (Blueprint->GeneratedClass)
    {
        UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
        UObject* ParentCDO = Blueprint->ParentClass ? Blueprint->ParentClass->GetDefaultObject() : nullptr;

        if (CDO)
        {
            TSharedPtr<FJsonObject> DefaultsObj = MakeShared<FJsonObject>();

            // Collect engine base classes to skip their properties
            TSet<UClass*> SkipClasses;
            SkipClasses.Add(UObject::StaticClass());
            SkipClasses.Add(AActor::StaticClass());
            if (UClass* PawnClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.Pawn")))
                SkipClasses.Add(PawnClass);
            if (UClass* CharClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.Character")))
                SkipClasses.Add(CharClass);
            if (UClass* PCClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.PlayerController")))
                SkipClasses.Add(PCClass);
            if (UClass* CompClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ActorComponent")))
                SkipClasses.Add(CompClass);
            if (UClass* SceneClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SceneComponent")))
                SkipClasses.Add(SceneClass);

            for (TFieldIterator<FProperty> PropIt(CDO->GetClass()); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                if (!Prop) continue;

                // Skip properties from engine base classes
                UClass* OwnerClass = Prop->GetOwnerClass();
                if (SkipClasses.Contains(OwnerClass))
                    continue;

                // Skip transient / deprecated / compiler-generated properties
                if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
                    continue;
                if (Prop->GetName().StartsWith(TEXT("__")))
                    continue;

                // Always filter out AnimGraph internal properties from CDO
                if (Prop->GetName().StartsWith(TEXT("AnimGraphNode_")))
                    continue;
                if (Prop->GetName().StartsWith(TEXT("AnimBlueprintExtension_")))
                    continue;

                const void* CDOValue = Prop->ContainerPtrToValuePtr<void>(CDO);

                // Only export values that differ from parent CDO (i.e. overridden in this BP)
                if (ParentCDO && ParentCDO->GetClass()->IsChildOf(OwnerClass))
                {
                    const void* ParentValue = Prop->ContainerPtrToValuePtr<void>(ParentCDO);
                    if (Prop->Identical(CDOValue, ParentValue))
                        continue;
                }

                FString ValueStr;
                Prop->ExportTextItem_Direct(ValueStr, CDOValue, nullptr, nullptr, PPF_None);

                if (!ValueStr.IsEmpty())
                {
                    DefaultsObj->SetStringField(Prop->GetName(), ValueStr);
                }
            }

            if (DefaultsObj->Values.Num() > 0)
            {
                ResultObj->SetObjectField(TEXT("class_defaults"), DefaultsObj);
            }
        }
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleListBlueprints(const TSharedPtr<FJsonObject>& Params)
{
    FString SearchPath = TEXT("/Game");
    if (Params->HasField(TEXT("path")))
    {
        Params->TryGetStringField(TEXT("path"), SearchPath);
    }

    bool bRecursive = true;
    if (Params->HasField(TEXT("recursive")))
    {
        bRecursive = Params->GetBoolField(TEXT("recursive"));
    }

    FString NameFilter;
    if (Params->HasField(TEXT("name_filter")))
    {
        Params->TryGetStringField(TEXT("name_filter"), NameFilter);
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = bRecursive;
    Filter.PackagePaths.Add(FName(*SearchPath));

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssets(Filter, AssetDataList);

    TArray<TSharedPtr<FJsonValue>> BlueprintsArray;
    for (const FAssetData& AssetData : AssetDataList)
    {
        // Apply name filter if specified
        if (!NameFilter.IsEmpty())
        {
            if (!AssetData.AssetName.ToString().Contains(NameFilter))
            {
                continue;
            }
        }

        TSharedPtr<FJsonObject> BPObj = MakeShared<FJsonObject>();
        BPObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
        BPObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        BPObj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());

        // Try to get parent class from asset data tags
        FString ParentClassPath;
        if (AssetData.GetTagValue(FName(TEXT("ParentClass")), ParentClassPath))
        {
            BPObj->SetStringField(TEXT("parent_class"), ParentClassPath);
        }

        BlueprintsArray.Add(MakeShared<FJsonValueObject>(BPObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetNumberField(TEXT("count"), BlueprintsArray.Num());
    ResultObj->SetArrayField(TEXT("blueprints"), BlueprintsArray);
    return ResultObj;
}
