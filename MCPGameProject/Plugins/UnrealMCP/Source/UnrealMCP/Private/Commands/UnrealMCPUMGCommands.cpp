#include "Commands/UnrealMCPUMGCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Factories/Factory.h"
#include "WidgetBlueprintEditor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Event.h"

FUnrealMCPUMGCommands::FUnrealMCPUMGCommands()
{
}

// Helper: Find a Widget Blueprint by full asset path (e.g. /Game/UI/WBP_Foo).
static UWidgetBlueprint* FindWidgetBlueprint(const FString& AssetPath)
{
	return Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath));
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCommand(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandName == TEXT("create_umg_widget_blueprint"))
	{
		return HandleCreateUMGWidgetBlueprint(Params);
	}
	else if (CommandName == TEXT("add_text_block_to_widget"))
	{
		return HandleAddTextBlockToWidget(Params);
	}
	else if (CommandName == TEXT("add_widget_to_viewport"))
	{
		return HandleAddWidgetToViewport(Params);
	}
	else if (CommandName == TEXT("add_button_to_widget"))
	{
		return HandleAddButtonToWidget(Params);
	}
	else if (CommandName == TEXT("bind_widget_event"))
	{
		return HandleBindWidgetEvent(Params);
	}
	else if (CommandName == TEXT("set_text_block_binding"))
	{
		return HandleSetTextBlockBinding(Params);
	}
	else if (CommandName == TEXT("add_progress_bar_to_widget"))
	{
		return HandleAddProgressBarToWidget(Params);
	}
	else if (CommandName == TEXT("add_image_to_widget"))
	{
		return HandleAddImageToWidget(Params);
	}
	else if (CommandName == TEXT("add_vertical_box_to_widget"))
	{
		return HandleAddVerticalBoxToWidget(Params);
	}
	else if (CommandName == TEXT("add_horizontal_box_to_widget"))
	{
		return HandleAddHorizontalBoxToWidget(Params);
	}
	else if (CommandName == TEXT("add_overlay_to_widget"))
	{
		return HandleAddOverlayToWidget(Params);
	}
	else if (CommandName == TEXT("add_size_box_to_widget"))
	{
		return HandleAddSizeBoxToWidget(Params);
	}
	else if (CommandName == TEXT("add_border_to_widget"))
	{
		return HandleAddBorderToWidget(Params);
	}
	else if (CommandName == TEXT("add_spacer_to_widget"))
	{
		return HandleAddSpacerToWidget(Params);
	}
	else if (CommandName == TEXT("set_widget_anchor"))
	{
		return HandleSetWidgetAnchor(Params);
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown UMG command: %s"), *CommandName));
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCreateUMGWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
	}

	// Get optional path (default: /Game/Widgets/)
	FString PackagePath = TEXT("/Game/Widgets/");
	FString CustomPath;
	if (Params->TryGetStringField(TEXT("path"), CustomPath) && !CustomPath.IsEmpty())
	{
		PackagePath = CustomPath;
		if (!PackagePath.EndsWith(TEXT("/")))
		{
			PackagePath += TEXT("/");
		}
	}

	FString AssetName = BlueprintName;
	FString FullPath = PackagePath + AssetName;

	// Check if asset already exists
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' already exists at '%s'"), *BlueprintName, *FullPath));
	}

	// Resolve parent class (default: UUserWidget)
	UClass* ParentClass = UUserWidget::StaticClass();
	FString ParentClassName;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
	{
		UClass* ResolvedClass = nullptr;

		// Strategy 1: FindObject with exact path (e.g. /Script/UMG.UserWidget)
		ResolvedClass = FindObject<UClass>(nullptr, *ParentClassName);

		// Strategy 2: LoadClass — handles blueprint class paths like /Game/UI/WBP_Base.WBP_Base_C
		if (!ResolvedClass)
		{
			ResolvedClass = LoadClass<UUserWidget>(nullptr, *ParentClassName);
		}

		// Strategy 3: Try loading as a Blueprint asset path (e.g. /Game/UI/WBP_Base)
		if (!ResolvedClass)
		{
			FString BPPath = ParentClassName;
			if (!BPPath.EndsWith(TEXT("_C")))
			{
				// Try loading the Blueprint and get its generated class
				UBlueprint* ParentBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
				if (ParentBP && ParentBP->GeneratedClass && ParentBP->GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
				{
					ResolvedClass = ParentBP->GeneratedClass;
				}
			}
		}

		// Strategy 4: Iterate all classes by name (last resort)
		if (!ResolvedClass)
		{
			FString CleanName = FPaths::GetBaseFilename(ParentClassName);
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == CleanName && It->IsChildOf(UUserWidget::StaticClass()))
				{
					ResolvedClass = *It;
					break;
				}
			}
		}

		if (ResolvedClass && ResolvedClass->IsChildOf(UUserWidget::StaticClass()))
		{
			ParentClass = ResolvedClass;
			UE_LOG(LogTemp, Display, TEXT("CreateUMGWidget: Using parent class '%s' (resolved to '%s')"), *ParentClassName, *ParentClass->GetPathName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateUMGWidget: Parent class '%s' not found or not a UUserWidget subclass. Using default UUserWidget."), *ParentClassName);
		}
	}

	// Create Widget Blueprint using UWidgetBlueprintFactory
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
	}

	UObject* NewAsset = Factory->FactoryCreateNew(
		UWidgetBlueprint::StaticClass(),
		Package,
		FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn
	);

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(NewAsset);
	if (!WidgetBlueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Widget Blueprint"));
	}

	// Add a default Canvas Panel if one doesn't exist
	if (!WidgetBlueprint->WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = WidgetBlueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetBlueprint->WidgetTree->RootWidget = RootCanvas;
	}

	// Mark the package dirty and notify asset registry
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(WidgetBlueprint);

	// Compile the blueprint
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	// Save the asset
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	// Create success response
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("name"), BlueprintName);
	ResultObj->SetStringField(TEXT("path"), FullPath);
	ResultObj->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddTextBlockToWidget(const TSharedPtr<FJsonObject>& Params)
{
	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	// Find the Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Get optional parameters
	FString InitialText = TEXT("New Text Block");
	Params->TryGetStringField(TEXT("text"), InitialText);

	FVector2D Position(0.0f, 0.0f);
	if (Params->HasField(TEXT("position")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PosArray;
		if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray->Num() >= 2)
		{
			Position.X = (*PosArray)[0]->AsNumber();
			Position.Y = (*PosArray)[1]->AsNumber();
		}
	}

	// Create Text Block widget
	UTextBlock* TextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *WidgetName);
	if (!TextBlock)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Text Block widget"));
	}

	// Set initial text
	TextBlock->SetText(FText::FromString(InitialText));

	// Add to canvas panel
	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Root Canvas Panel not found"));
	}

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(TextBlock);
	PanelSlot->SetPosition(Position);

	// Mark the package dirty and compile
	WidgetBlueprint->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	// Create success response
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	ResultObj->SetStringField(TEXT("text"), InitialText);
	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddWidgetToViewport(const TSharedPtr<FJsonObject>& Params)
{
	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	// Find the Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Get optional Z-order parameter
	int32 ZOrder = 0;
	Params->TryGetNumberField(TEXT("z_order"), ZOrder);

	// Create widget instance
	UClass* WidgetClass = WidgetBlueprint->GeneratedClass;
	if (!WidgetClass)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get widget class"));
	}

	// Note: This creates the widget but doesn't add it to viewport
	// The actual addition to viewport should be done through Blueprint nodes
	// as it requires a game context

	// Create success response with instructions
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("class_path"), WidgetClass->GetPathName());
	ResultObj->SetNumberField(TEXT("z_order"), ZOrder);
	ResultObj->SetStringField(TEXT("note"), TEXT("Widget class ready. Use CreateWidget and AddToViewport nodes in Blueprint to display in game."));
	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddButtonToWidget(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing blueprint_name parameter"));
		return Response;
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing widget_name parameter"));
		return Response;
	}

	FString ButtonText;
	if (!Params->TryGetStringField(TEXT("text"), ButtonText))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing text parameter"));
		return Response;
	}

	// Load the Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
	{
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
		return Response;
	}

	// Create Button widget
	UButton* Button = NewObject<UButton>(WidgetBlueprint->GeneratedClass->GetDefaultObject(), UButton::StaticClass(), *WidgetName);
	if (!Button)
	{
		Response->SetStringField(TEXT("error"), TEXT("Failed to create Button widget"));
		return Response;
	}

	// Set button text
	UTextBlock* ButtonTextBlock = NewObject<UTextBlock>(Button, UTextBlock::StaticClass(), *(WidgetName + TEXT("_Text")));
	if (ButtonTextBlock)
	{
		ButtonTextBlock->SetText(FText::FromString(ButtonText));
		Button->AddChild(ButtonTextBlock);
	}

	// Get canvas panel and add button
	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		Response->SetStringField(TEXT("error"), TEXT("Root widget is not a Canvas Panel"));
		return Response;
	}

	// Add to canvas and set position
	UCanvasPanelSlot* ButtonSlot = RootCanvas->AddChildToCanvas(Button);
	if (ButtonSlot)
	{
		const TArray<TSharedPtr<FJsonValue>>* Position;
		if (Params->TryGetArrayField(TEXT("position"), Position) && Position->Num() >= 2)
		{
			FVector2D Pos(
				(*Position)[0]->AsNumber(),
				(*Position)[1]->AsNumber()
			);
			ButtonSlot->SetPosition(Pos);
		}
	}

	// Save the Widget Blueprint
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	UEditorAssetLibrary::SaveAsset(BlueprintName, false);

	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("widget_name"), WidgetName);
	return Response;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleBindWidgetEvent(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing blueprint_name parameter"));
		return Response;
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing widget_name parameter"));
		return Response;
	}

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing event_name parameter"));
		return Response;
	}

	// Load the Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
	{
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
		return Response;
	}

	// Create the event graph if it doesn't exist
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(WidgetBlueprint);
	if (!EventGraph)
	{
		Response->SetStringField(TEXT("error"), TEXT("Failed to find or create event graph"));
		return Response;
	}

	// Find the widget in the blueprint
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(*WidgetName);
	if (!Widget)
	{
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to find widget: %s"), *WidgetName));
		return Response;
	}

	// Create the event node (e.g., OnClicked for buttons)
	UK2Node_Event* EventNode = nullptr;
	
	// Find existing nodes first
	TArray<UK2Node_Event*> AllEventNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Event>(WidgetBlueprint, AllEventNodes);
	
	for (UK2Node_Event* Node : AllEventNodes)
	{
		if (Node->CustomFunctionName == FName(*EventName) && Node->EventReference.GetMemberParentClass() == Widget->GetClass())
		{
			EventNode = Node;
			break;
		}
	}

	// If no existing node, create a new one
	if (!EventNode)
	{
		// Calculate position - place it below existing nodes
		float MaxHeight = 0.0f;
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			MaxHeight = FMath::Max(MaxHeight, Node->NodePosY);
		}
		
		const FVector2D NodePos(200, MaxHeight + 200);

		// Call CreateNewBoundEventForClass, which returns void, so we can't capture the return value directly
		// We'll need to find the node after creating it
		FKismetEditorUtilities::CreateNewBoundEventForClass(
			Widget->GetClass(),
			FName(*EventName),
			WidgetBlueprint,
			nullptr  // We don't need a specific property binding
		);

		// Now find the newly created node
		TArray<UK2Node_Event*> UpdatedEventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Event>(WidgetBlueprint, UpdatedEventNodes);
		
		for (UK2Node_Event* Node : UpdatedEventNodes)
		{
			if (Node->CustomFunctionName == FName(*EventName) && Node->EventReference.GetMemberParentClass() == Widget->GetClass())
			{
				EventNode = Node;
				
				// Set position of the node
				EventNode->NodePosX = NodePos.X;
				EventNode->NodePosY = NodePos.Y;
				
				break;
			}
		}
	}

	if (!EventNode)
	{
		Response->SetStringField(TEXT("error"), TEXT("Failed to create event node"));
		return Response;
	}

	// Save the Widget Blueprint
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	UEditorAssetLibrary::SaveAsset(BlueprintName, false);

	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("event_name"), EventName);
	return Response;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetTextBlockBinding(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing blueprint_name parameter"));
		return Response;
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing widget_name parameter"));
		return Response;
	}

	FString BindingName;
	if (!Params->TryGetStringField(TEXT("binding_name"), BindingName))
	{
		Response->SetStringField(TEXT("error"), TEXT("Missing binding_name parameter"));
		return Response;
	}

	// Load the Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
	{
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
		return Response;
	}

	// Create a variable for binding if it doesn't exist
	FBlueprintEditorUtils::AddMemberVariable(
		WidgetBlueprint,
		FName(*BindingName),
		FEdGraphPinType(UEdGraphSchema_K2::PC_Text, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType())
	);

	// Find the TextBlock widget
	UTextBlock* TextBlock = Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName)));
	if (!TextBlock)
	{
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to find TextBlock widget: %s"), *WidgetName));
		return Response;
	}

	// Create binding function
	const FString FunctionName = FString::Printf(TEXT("Get%s"), *BindingName);
	UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(
		WidgetBlueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (FuncGraph)
	{
		// Add the function to the blueprint with proper template parameter
		// Template requires null for last parameter when not using a signature-source
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(WidgetBlueprint, FuncGraph, false, nullptr);

		// Create entry node
		UK2Node_FunctionEntry* EntryNode = nullptr;
		
		// Create entry node - use the API that exists in UE 5.5
		EntryNode = NewObject<UK2Node_FunctionEntry>(FuncGraph);
		FuncGraph->AddNode(EntryNode, false, false);
		EntryNode->NodePosX = 0;
		EntryNode->NodePosY = 0;
		EntryNode->FunctionReference.SetExternalMember(FName(*FunctionName), WidgetBlueprint->GeneratedClass);
		EntryNode->AllocateDefaultPins();

		// Create get variable node
		UK2Node_VariableGet* GetVarNode = NewObject<UK2Node_VariableGet>(FuncGraph);
		GetVarNode->VariableReference.SetSelfMember(FName(*BindingName));
		FuncGraph->AddNode(GetVarNode, false, false);
		GetVarNode->NodePosX = 200;
		GetVarNode->NodePosY = 0;
		GetVarNode->AllocateDefaultPins();

		// Connect nodes
		UEdGraphPin* EntryThenPin = EntryNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* GetVarOutPin = GetVarNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
		if (EntryThenPin && GetVarOutPin)
		{
			EntryThenPin->MakeLinkTo(GetVarOutPin);
		}
	}

	// Save the Widget Blueprint
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	UEditorAssetLibrary::SaveAsset(BlueprintName, false);

	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("binding_name"), BindingName);
	return Response;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddProgressBarToWidget(const TSharedPtr<FJsonObject>& Params)
{
	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	// Find the Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Get optional parameters
	float Percent = 1.0f;
	if (Params->HasField(TEXT("percent")))
	{
		Percent = static_cast<float>(Params->GetNumberField(TEXT("percent")));
	}

	FLinearColor FillColor(1.0f, 0.0f, 0.0f, 1.0f); // Default: red
	if (Params->HasField(TEXT("fill_color")))
	{
		const TArray<TSharedPtr<FJsonValue>>* ColorArray;
		if (Params->TryGetArrayField(TEXT("fill_color"), ColorArray) && ColorArray->Num() >= 3)
		{
			FillColor.R = static_cast<float>((*ColorArray)[0]->AsNumber());
			FillColor.G = static_cast<float>((*ColorArray)[1]->AsNumber());
			FillColor.B = static_cast<float>((*ColorArray)[2]->AsNumber());
			FillColor.A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
		}
	}

	FVector2D Position(0.0f, 0.0f);
	if (Params->HasField(TEXT("position")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PosArray;
		if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray->Num() >= 2)
		{
			Position.X = (*PosArray)[0]->AsNumber();
			Position.Y = (*PosArray)[1]->AsNumber();
		}
	}

	FVector2D Size(200.0f, 20.0f); // Default size for a progress bar
	if (Params->HasField(TEXT("size")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SizeArray;
		if (Params->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray->Num() >= 2)
		{
			Size.X = (*SizeArray)[0]->AsNumber();
			Size.Y = (*SizeArray)[1]->AsNumber();
		}
	}

	FString FillType = TEXT("LeftToRight");
	Params->TryGetStringField(TEXT("fill_type"), FillType);

	// Create ProgressBar widget
	UProgressBar* ProgressBar = WidgetBlueprint->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), *WidgetName);
	if (!ProgressBar)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create ProgressBar widget"));
	}

	// Configure properties
	ProgressBar->SetPercent(Percent);
	ProgressBar->SetFillColorAndOpacity(FillColor);

	// Set fill type
	if (FillType == TEXT("RightToLeft"))
	{
		ProgressBar->SetBarFillType(EProgressBarFillType::RightToLeft);
	}
	else if (FillType == TEXT("FillFromCenter"))
	{
		ProgressBar->SetBarFillType(EProgressBarFillType::FillFromCenter);
	}
	else if (FillType == TEXT("FillFromCenterHorizontal"))
	{
		ProgressBar->SetBarFillType(EProgressBarFillType::FillFromCenterHorizontal);
	}
	else if (FillType == TEXT("FillFromCenterVertical"))
	{
		ProgressBar->SetBarFillType(EProgressBarFillType::FillFromCenterVertical);
	}
	else if (FillType == TEXT("TopToBottom"))
	{
		ProgressBar->SetBarFillType(EProgressBarFillType::TopToBottom);
	}
	else if (FillType == TEXT("BottomToTop"))
	{
		ProgressBar->SetBarFillType(EProgressBarFillType::BottomToTop);
	}
	// else default LeftToRight

	// Add to canvas panel
	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Root Canvas Panel not found"));
	}

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(ProgressBar);
	PanelSlot->SetPosition(Position);
	PanelSlot->SetSize(Size);

	// Mark dirty and compile
	WidgetBlueprint->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	// Create success response
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	ResultObj->SetNumberField(TEXT("percent"), Percent);

	TArray<TSharedPtr<FJsonValue>> ColorArr;
	ColorArr.Add(MakeShared<FJsonValueNumber>(FillColor.R));
	ColorArr.Add(MakeShared<FJsonValueNumber>(FillColor.G));
	ColorArr.Add(MakeShared<FJsonValueNumber>(FillColor.B));
	ColorArr.Add(MakeShared<FJsonValueNumber>(FillColor.A));
	ResultObj->SetArrayField(TEXT("fill_color"), ColorArr);

	TArray<TSharedPtr<FJsonValue>> SizeArr;
	SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
	SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
	ResultObj->SetArrayField(TEXT("size"), SizeArr);

	return ResultObj;
}

// Helper: Parse a JSON [X,Y] array into FVector2D with a default value.
static FVector2D ParseVector2D(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FVector2D Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Params->TryGetArrayField(FieldName, Arr) && Arr->Num() >= 2)
	{
		Default.X = (*Arr)[0]->AsNumber();
		Default.Y = (*Arr)[1]->AsNumber();
	}
	return Default;
}

// Helper: Parse a JSON [R,G,B,A] array into FLinearColor with a default value.
static FLinearColor ParseColor(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FLinearColor Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Params->TryGetArrayField(FieldName, Arr) && Arr->Num() >= 3)
	{
		Default.R = static_cast<float>((*Arr)[0]->AsNumber());
		Default.G = static_cast<float>((*Arr)[1]->AsNumber());
		Default.B = static_cast<float>((*Arr)[2]->AsNumber());
		Default.A = Arr->Num() >= 4 ? static_cast<float>((*Arr)[3]->AsNumber()) : 1.0f;
	}
	return Default;
}

// Helper: Common preamble for "add X to widget" handlers.
// Returns nullptr on success (fills out WidgetBlueprint, RootCanvas), or an error response.
static TSharedPtr<FJsonObject> LoadWidgetAndCanvas(
	const TSharedPtr<FJsonObject>& Params,
	FString& OutBlueprintName,
	FString& OutWidgetName,
	UWidgetBlueprint*& OutBlueprint,
	UCanvasPanel*& OutCanvas)
{
	if (!Params->TryGetStringField(TEXT("blueprint_name"), OutBlueprintName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	if (!Params->TryGetStringField(TEXT("widget_name"), OutWidgetName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

	OutBlueprint = FindWidgetBlueprint(OutBlueprintName);
	if (!OutBlueprint)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *OutBlueprintName));

	OutCanvas = Cast<UCanvasPanel>(OutBlueprint->WidgetTree->RootWidget);
	if (!OutCanvas)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Root Canvas Panel not found"));

	return nullptr; // Success
}

// Helper: Compile, mark dirty. Does NOT save to disk (callers opt-in).
static void CompileWidget(UWidgetBlueprint* WidgetBlueprint)
{
	WidgetBlueprint->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddImageToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	// Create Image widget
	UImage* ImageWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *WidgetName);
	if (!ImageWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Image widget"));

	// Set tint color
	FLinearColor TintColor = ParseColor(Params, TEXT("tint_color"), FLinearColor::White);
	ImageWidget->SetColorAndOpacity(TintColor);

	// Load texture if provided
	FString TexturePath;
	if (Params->TryGetStringField(TEXT("texture_path"), TexturePath) && !TexturePath.IsEmpty())
	{
		UTexture2D* Texture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(TexturePath));
		if (Texture)
		{
			ImageWidget->SetBrushFromTexture(Texture);
		}
	}

	// Add to canvas
	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(100, 100));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(ImageWidget);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	if (!TexturePath.IsEmpty()) Result->SetStringField(TEXT("texture_path"), TexturePath);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddVerticalBoxToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	UVerticalBox* VBox = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *WidgetName);
	if (!VBox)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create VerticalBox widget"));

	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(200, 400));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(VBox);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddHorizontalBoxToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	UHorizontalBox* HBox = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *WidgetName);
	if (!HBox)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create HorizontalBox widget"));

	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(400, 200));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(HBox);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddOverlayToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	UOverlay* OverlayWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *WidgetName);
	if (!OverlayWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Overlay widget"));

	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(300, 300));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(OverlayWidget);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddSizeBoxToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	USizeBox* SizeBoxWidget = WidgetBlueprint->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *WidgetName);
	if (!SizeBoxWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create SizeBox widget"));

	// Set optional size overrides
	double WidthOverride = 0;
	if (Params->TryGetNumberField(TEXT("width_override"), WidthOverride) && WidthOverride > 0)
	{
		SizeBoxWidget->SetWidthOverride(static_cast<float>(WidthOverride));
	}

	double HeightOverride = 0;
	if (Params->TryGetNumberField(TEXT("height_override"), HeightOverride) && HeightOverride > 0)
	{
		SizeBoxWidget->SetHeightOverride(static_cast<float>(HeightOverride));
	}

	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(SizeBoxWidget);
	Slot->SetPosition(Position);
	// If overrides are set, use them as slot size; otherwise use default
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(
		WidthOverride > 0 ? WidthOverride : 200,
		HeightOverride > 0 ? HeightOverride : 200));
	Slot->SetSize(Size);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	if (WidthOverride > 0) Result->SetNumberField(TEXT("width_override"), WidthOverride);
	if (HeightOverride > 0) Result->SetNumberField(TEXT("height_override"), HeightOverride);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddBorderToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	UBorder* BorderWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *WidgetName);
	if (!BorderWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Border widget"));

	// Set background color
	FLinearColor BgColor = ParseColor(Params, TEXT("background_color"), FLinearColor(0.1f, 0.1f, 0.1f, 1.0f));
	BorderWidget->SetBrushColor(BgColor);

	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(200, 200));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(BorderWidget);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddSpacerToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndCanvas(Params, BlueprintName, WidgetName, WidgetBlueprint, RootCanvas))
		return Err;

	USpacer* SpacerWidget = WidgetBlueprint->WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), *WidgetName);
	if (!SpacerWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Spacer widget"));

	FVector2D SpacerSize = ParseVector2D(Params, TEXT("size"), FVector2D(100, 20));
	SpacerWidget->SetSize(SpacerSize);

	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(SpacerWidget);
	Slot->SetPosition(Position);
	Slot->SetSize(SpacerSize);

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetWidgetAnchor(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));

	// Find the widget in the tree
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));

	// The widget must be in a CanvasPanel to have a CanvasPanelSlot
	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!Slot)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' is not in a CanvasPanel"), *WidgetName));

	// Set anchor: expects [MinX, MinY, MaxX, MaxY]
	const TArray<TSharedPtr<FJsonValue>>* AnchorArr;
	if (Params->TryGetArrayField(TEXT("anchor"), AnchorArr) && AnchorArr->Num() >= 4)
	{
		FAnchors Anchors;
		Anchors.Minimum.X = (*AnchorArr)[0]->AsNumber();
		Anchors.Minimum.Y = (*AnchorArr)[1]->AsNumber();
		Anchors.Maximum.X = (*AnchorArr)[2]->AsNumber();
		Anchors.Maximum.Y = (*AnchorArr)[3]->AsNumber();
		Slot->SetAnchors(Anchors);
	}

	// Set alignment: expects [X, Y]
	const TArray<TSharedPtr<FJsonValue>>* AlignArr;
	if (Params->TryGetArrayField(TEXT("alignment"), AlignArr) && AlignArr->Num() >= 2)
	{
		FVector2D Alignment((*AlignArr)[0]->AsNumber(), (*AlignArr)[1]->AsNumber());
		Slot->SetAlignment(Alignment);
	}

	// Set offset: expects [Left, Top, Right, Bottom]
	const TArray<TSharedPtr<FJsonValue>>* OffsetArr;
	if (Params->TryGetArrayField(TEXT("offset"), OffsetArr) && OffsetArr->Num() >= 4)
	{
		FMargin Offsets(
			(*OffsetArr)[0]->AsNumber(),
			(*OffsetArr)[1]->AsNumber(),
			(*OffsetArr)[2]->AsNumber(),
			(*OffsetArr)[3]->AsNumber()
		);
		Slot->SetOffsets(Offsets);
	}

	// Set position
	FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(-1, -1));
	if (Position.X >= 0 || Position.Y >= 0)
	{
		Slot->SetPosition(Position);
	}

	// Set size
	FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(-1, -1));
	if (Size.X >= 0 || Size.Y >= 0)
	{
		Slot->SetSize(Size);
	}

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}