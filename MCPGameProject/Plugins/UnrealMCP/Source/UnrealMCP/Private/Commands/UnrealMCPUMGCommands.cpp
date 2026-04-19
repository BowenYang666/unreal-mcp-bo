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
#include "Components/PanelWidget.h"
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
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
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
	else if (CommandName == TEXT("set_widget_slot_property"))
	{
		return HandleSetWidgetSlotProperty(Params);
	}
	else if (CommandName == TEXT("read_widget_layout"))
	{
		return HandleReadWidgetLayout(Params);
	}
	else if (CommandName == TEXT("add_slider_to_widget"))
	{
		return HandleAddSliderToWidget(Params);
	}
	else if (CommandName == TEXT("add_combobox_to_widget"))
	{
		return HandleAddComboBoxToWidget(Params);
	}
	else if (CommandName == TEXT("set_widget_property"))
	{
		return HandleSetWidgetProperty(Params);
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown UMG command: %s"), *CommandName));
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
// Resolves blueprint, widget name, and optional parent_name.
// If parent_name is given, OutParent is the named UPanelWidget and OutCanvas is null.
// If parent_name is absent, OutParent is the root CanvasPanel and OutCanvas points to it too.
static TSharedPtr<FJsonObject> LoadWidgetAndParent(
	const TSharedPtr<FJsonObject>& Params,
	FString& OutBlueprintName,
	FString& OutWidgetName,
	UWidgetBlueprint*& OutBlueprint,
	UPanelWidget*& OutParent,
	UCanvasPanel*& OutCanvas)
{
	OutParent = nullptr;
	OutCanvas = nullptr;

	if (!Params->TryGetStringField(TEXT("blueprint_name"), OutBlueprintName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	if (!Params->TryGetStringField(TEXT("widget_name"), OutWidgetName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

	OutBlueprint = FindWidgetBlueprint(OutBlueprintName);
	if (!OutBlueprint)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *OutBlueprintName));

	FString ParentName;
	if (Params->TryGetStringField(TEXT("parent_name"), ParentName) && !ParentName.IsEmpty())
	{
		UWidget* Found = OutBlueprint->WidgetTree->FindWidget(FName(*ParentName));
		if (!Found)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent widget '%s' not found"), *ParentName));
		OutParent = Cast<UPanelWidget>(Found);
		if (!OutParent)
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent widget '%s' is not a panel/container — cannot add children to it"), *ParentName));
	}
	else
	{
		OutCanvas = Cast<UCanvasPanel>(OutBlueprint->WidgetTree->RootWidget);
		if (!OutCanvas)
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Root Canvas Panel not found"));
		OutParent = OutCanvas;
	}
	return nullptr;
}

// Helper: Add a child widget to a parent panel. If the parent is a CanvasPanel, set position/size.
// Otherwise use generic AddChild (VBox, HBox, Overlay, Border, etc. manage layout themselves).
// Helper: Parse a "size_rule" string param into ESlateSizeRule.
static ESlateSizeRule::Type ParseSizeRule(const TSharedPtr<FJsonObject>& Params, ESlateSizeRule::Type Default = ESlateSizeRule::Automatic)
{
	FString SizeRuleStr;
	if (Params->TryGetStringField(TEXT("size_rule"), SizeRuleStr))
	{
		if (SizeRuleStr.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
			return ESlateSizeRule::Fill;
		if (SizeRuleStr.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
			return ESlateSizeRule::Automatic;
	}
	return Default;
}

// Helper: Parse a "padding" array [L,T,R,B] into FMargin.
static FMargin ParsePadding(const TSharedPtr<FJsonObject>& Params, FMargin Default = FMargin(0))
{
	const TArray<TSharedPtr<FJsonValue>>* PadArr;
	if (Params->TryGetArrayField(TEXT("padding"), PadArr) && PadArr->Num() >= 4)
	{
		return FMargin(
			(*PadArr)[0]->AsNumber(),
			(*PadArr)[1]->AsNumber(),
			(*PadArr)[2]->AsNumber(),
			(*PadArr)[3]->AsNumber());
	}
	return Default;
}

// Helper: Parse an alignment string like "HAlign_Center" / "Center" / "Left" etc.
static EHorizontalAlignment ParseHAlign(const TSharedPtr<FJsonObject>& Params, EHorizontalAlignment Default = EHorizontalAlignment::HAlign_Fill)
{
	FString Str;
	if (Params->TryGetStringField(TEXT("h_align"), Str))
	{
		if (Str.Contains(TEXT("Left"))) return EHorizontalAlignment::HAlign_Left;
		if (Str.Contains(TEXT("Center"))) return EHorizontalAlignment::HAlign_Center;
		if (Str.Contains(TEXT("Right"))) return EHorizontalAlignment::HAlign_Right;
		if (Str.Contains(TEXT("Fill"))) return EHorizontalAlignment::HAlign_Fill;
	}
	return Default;
}

static EVerticalAlignment ParseVAlign(const TSharedPtr<FJsonObject>& Params, EVerticalAlignment Default = EVerticalAlignment::VAlign_Fill)
{
	FString Str;
	if (Params->TryGetStringField(TEXT("v_align"), Str))
	{
		if (Str.Contains(TEXT("Top"))) return EVerticalAlignment::VAlign_Top;
		if (Str.Contains(TEXT("Center"))) return EVerticalAlignment::VAlign_Center;
		if (Str.Contains(TEXT("Bottom"))) return EVerticalAlignment::VAlign_Bottom;
		if (Str.Contains(TEXT("Fill"))) return EVerticalAlignment::VAlign_Fill;
	}
	return Default;
}

static void AddChildToParent(UWidget* Child, UPanelWidget* Parent, UCanvasPanel* Canvas,
	const TSharedPtr<FJsonObject>& Params, FVector2D DefaultSize = FVector2D(200, 50))
{
	if (Canvas)
	{
		FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(0, 0));
		FVector2D Size = ParseVector2D(Params, TEXT("size"), DefaultSize);
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}
	else
	{
		Parent->AddChild(Child);

		// Configure HorizontalBox / VerticalBox slot properties if provided
		if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Child->Slot))
		{
			ESlateSizeRule::Type Rule = ParseSizeRule(Params);
			FSlateChildSize ChildSize(Rule);
			double FillVal = 1.0;
			if (Params->TryGetNumberField(TEXT("fill_size"), FillVal))
				ChildSize.Value = static_cast<float>(FillVal);
			HSlot->SetSize(ChildSize);
			HSlot->SetPadding(ParsePadding(Params));
			HSlot->SetHorizontalAlignment(ParseHAlign(Params));
			HSlot->SetVerticalAlignment(ParseVAlign(Params));
		}
		else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Child->Slot))
		{
			ESlateSizeRule::Type Rule = ParseSizeRule(Params);
			FSlateChildSize ChildSize(Rule);
			double FillVal = 1.0;
			if (Params->TryGetNumberField(TEXT("fill_size"), FillVal))
				ChildSize.Value = static_cast<float>(FillVal);
			VSlot->SetSize(ChildSize);
			VSlot->SetPadding(ParsePadding(Params));
			VSlot->SetHorizontalAlignment(ParseHAlign(Params));
			VSlot->SetVerticalAlignment(ParseVAlign(Params));
		}
		else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Child->Slot))
		{
			OSlot->SetPadding(ParsePadding(Params));
			OSlot->SetHorizontalAlignment(ParseHAlign(Params));
			OSlot->SetVerticalAlignment(ParseVAlign(Params));
		}
	}
}

// Helper: Compile, mark dirty, and save to disk.
static void CompileWidget(UWidgetBlueprint* WidgetBlueprint)
{
	WidgetBlueprint->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	UEditorAssetLibrary::SaveLoadedAsset(WidgetBlueprint);
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
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	// Get optional parameters — handle both string and number JSON values
	FString InitialText = TEXT("New Text Block");
	if (!Params->TryGetStringField(TEXT("text"), InitialText))
	{
		double NumVal;
		if (Params->TryGetNumberField(TEXT("text"), NumVal))
		{
			InitialText = FString::Printf(TEXT("%g"), NumVal);
		}
	}

	// Create Text Block widget
	UTextBlock* TextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *WidgetName);
	if (!TextBlock)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Text Block widget"));

	// Set initial text
	TextBlock->SetText(FText::FromString(InitialText));

	// Add to parent
	AddChildToParent(TextBlock, ParentWidget, RootCanvas, Params, FVector2D(200, 50));

	CompileWidget(WidgetBlueprint);

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

	// Resolve parent
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	FString ParentName;
	if (Params->TryGetStringField(TEXT("parent_name"), ParentName) && !ParentName.IsEmpty())
	{
		UWidget* Found = WidgetBlueprint->WidgetTree->FindWidget(FName(*ParentName));
		if (!Found) { Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent widget '%s' not found"), *ParentName)); return Response; }
		ParentWidget = Cast<UPanelWidget>(Found);
		if (!ParentWidget) { Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent widget '%s' is not a panel/container"), *ParentName)); return Response; }
	}
	else
	{
		RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
		if (!RootCanvas) { Response->SetStringField(TEXT("error"), TEXT("Root widget is not a Canvas Panel")); return Response; }
		ParentWidget = RootCanvas;
	}

	// Create Button widget
	UButton* Button = WidgetBlueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *WidgetName);
	if (!Button)
	{
		Response->SetStringField(TEXT("error"), TEXT("Failed to create Button widget"));
		return Response;
	}

	// Set button text
	UTextBlock* ButtonTextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(WidgetName + TEXT("_Text")));
	if (ButtonTextBlock)
	{
		ButtonTextBlock->SetText(FText::FromString(ButtonText));
		Button->AddChild(ButtonTextBlock);
	}

	// Add to parent
	AddChildToParent(Button, ParentWidget, RootCanvas, Params, FVector2D(200, 50));

	CompileWidget(WidgetBlueprint);

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
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

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

	// Add to parent
	AddChildToParent(ProgressBar, ParentWidget, RootCanvas, Params, FVector2D(200, 20));

	CompileWidget(WidgetBlueprint);

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

	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddImageToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
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

	// Add to parent
	AddChildToParent(ImageWidget, ParentWidget, RootCanvas, Params, FVector2D(100, 100));

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
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	UVerticalBox* VBox = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *WidgetName);
	if (!VBox)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create VerticalBox widget"));

	AddChildToParent(VBox, ParentWidget, RootCanvas, Params, FVector2D(200, 400));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddHorizontalBoxToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	UHorizontalBox* HBox = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *WidgetName);
	if (!HBox)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create HorizontalBox widget"));

	AddChildToParent(HBox, ParentWidget, RootCanvas, Params, FVector2D(400, 200));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddOverlayToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	UOverlay* OverlayWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *WidgetName);
	if (!OverlayWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Overlay widget"));

	AddChildToParent(OverlayWidget, ParentWidget, RootCanvas, Params, FVector2D(300, 300));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddSizeBoxToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
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

	AddChildToParent(SizeBoxWidget, ParentWidget, RootCanvas, Params, FVector2D(
		WidthOverride > 0 ? WidthOverride : 200,
		HeightOverride > 0 ? HeightOverride : 200));

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
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	UBorder* BorderWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *WidgetName);
	if (!BorderWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Border widget"));

	// Set background color
	FLinearColor BgColor = ParseColor(Params, TEXT("background_color"), FLinearColor(0.1f, 0.1f, 0.1f, 1.0f));
	BorderWidget->SetBrushColor(BgColor);

	AddChildToParent(BorderWidget, ParentWidget, RootCanvas, Params, FVector2D(200, 200));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddSpacerToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	USpacer* SpacerWidget = WidgetBlueprint->WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), *WidgetName);
	if (!SpacerWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Spacer widget"));

	FVector2D SpacerSize = ParseVector2D(Params, TEXT("size"), FVector2D(100, 20));
	SpacerWidget->SetSize(SpacerSize);

	AddChildToParent(SpacerWidget, ParentWidget, RootCanvas, Params, SpacerSize);

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

// ============================================================================
// set_widget_slot_property — universal slot property setter for any slot type
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetWidgetSlotProperty(const TSharedPtr<FJsonObject>& Params)
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

	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));

	if (!Widget->Slot)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' has no slot (not parented)"), *WidgetName));

	FString SlotType;

	if (UCanvasPanelSlot* CSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		SlotType = TEXT("CanvasPanelSlot");

		const TArray<TSharedPtr<FJsonValue>>* AnchorArr;
		if (Params->TryGetArrayField(TEXT("anchor"), AnchorArr) && AnchorArr->Num() >= 4)
		{
			FAnchors Anchors;
			Anchors.Minimum.X = (*AnchorArr)[0]->AsNumber();
			Anchors.Minimum.Y = (*AnchorArr)[1]->AsNumber();
			Anchors.Maximum.X = (*AnchorArr)[2]->AsNumber();
			Anchors.Maximum.Y = (*AnchorArr)[3]->AsNumber();
			CSlot->SetAnchors(Anchors);
		}

		const TArray<TSharedPtr<FJsonValue>>* AlignArr;
		if (Params->TryGetArrayField(TEXT("alignment"), AlignArr) && AlignArr->Num() >= 2)
		{
			CSlot->SetAlignment(FVector2D((*AlignArr)[0]->AsNumber(), (*AlignArr)[1]->AsNumber()));
		}

		const TArray<TSharedPtr<FJsonValue>>* OffsetArr;
		if (Params->TryGetArrayField(TEXT("offset"), OffsetArr) && OffsetArr->Num() >= 4)
		{
			CSlot->SetOffsets(FMargin(
				(*OffsetArr)[0]->AsNumber(), (*OffsetArr)[1]->AsNumber(),
				(*OffsetArr)[2]->AsNumber(), (*OffsetArr)[3]->AsNumber()));
		}

		FVector2D Position = ParseVector2D(Params, TEXT("position"), FVector2D(-1, -1));
		if (Position.X >= 0 || Position.Y >= 0)
			CSlot->SetPosition(Position);

		FVector2D Size = ParseVector2D(Params, TEXT("size"), FVector2D(-1, -1));
		if (Size.X >= 0 || Size.Y >= 0)
			CSlot->SetSize(Size);
	}
	else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
	{
		SlotType = TEXT("HorizontalBoxSlot");

		if (Params->HasField(TEXT("size_rule")))
		{
			FSlateChildSize ChildSize(ParseSizeRule(Params));
			double FillVal = 1.0;
			if (Params->TryGetNumberField(TEXT("fill_size"), FillVal))
				ChildSize.Value = static_cast<float>(FillVal);
			HSlot->SetSize(ChildSize);
		}

		if (Params->HasField(TEXT("padding")))
			HSlot->SetPadding(ParsePadding(Params));

		if (Params->HasField(TEXT("h_align")))
			HSlot->SetHorizontalAlignment(ParseHAlign(Params));

		if (Params->HasField(TEXT("v_align")))
			HSlot->SetVerticalAlignment(ParseVAlign(Params));
	}
	else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
	{
		SlotType = TEXT("VerticalBoxSlot");

		if (Params->HasField(TEXT("size_rule")))
		{
			FSlateChildSize ChildSize(ParseSizeRule(Params));
			double FillVal = 1.0;
			if (Params->TryGetNumberField(TEXT("fill_size"), FillVal))
				ChildSize.Value = static_cast<float>(FillVal);
			VSlot->SetSize(ChildSize);
		}

		if (Params->HasField(TEXT("padding")))
			VSlot->SetPadding(ParsePadding(Params));

		if (Params->HasField(TEXT("h_align")))
			VSlot->SetHorizontalAlignment(ParseHAlign(Params));

		if (Params->HasField(TEXT("v_align")))
			VSlot->SetVerticalAlignment(ParseVAlign(Params));
	}
	else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Widget->Slot))
	{
		SlotType = TEXT("OverlaySlot");

		if (Params->HasField(TEXT("padding")))
			OSlot->SetPadding(ParsePadding(Params));

		if (Params->HasField(TEXT("h_align")))
			OSlot->SetHorizontalAlignment(ParseHAlign(Params));

		if (Params->HasField(TEXT("v_align")))
			OSlot->SetVerticalAlignment(ParseVAlign(Params));
	}
	else
	{
		SlotType = Widget->Slot->GetClass()->GetName();
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unsupported slot type '%s' for widget '%s'"), *SlotType, *WidgetName));
	}

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("slot_type"), SlotType);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

// ---- read_widget_layout helpers ----

// Serialize a color array to JSON.
static TArray<TSharedPtr<FJsonValue>> ColorToJsonArray(const FLinearColor& C)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Add(MakeShared<FJsonValueNumber>(C.R));
	Arr.Add(MakeShared<FJsonValueNumber>(C.G));
	Arr.Add(MakeShared<FJsonValueNumber>(C.B));
	Arr.Add(MakeShared<FJsonValueNumber>(C.A));
	return Arr;
}

// Serialize slot info — safe, no UPropertyToJsonValue.
static TSharedPtr<FJsonObject> SerializeSlot(UPanelSlot* Slot)
{
	if (!Slot) return nullptr;

	TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
	SlotObj->SetStringField(TEXT("type"), Slot->GetClass()->GetName());

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		FVector2D Pos = CanvasSlot->GetPosition();
		FVector2D Size = CanvasSlot->GetSize();
		FAnchors Anchors = CanvasSlot->GetAnchors();
		FVector2D Align = CanvasSlot->GetAlignment();

		TArray<TSharedPtr<FJsonValue>> PosArr;
		PosArr.Add(MakeShared<FJsonValueNumber>(Pos.X));
		PosArr.Add(MakeShared<FJsonValueNumber>(Pos.Y));
		SlotObj->SetArrayField(TEXT("position"), PosArr);

		TArray<TSharedPtr<FJsonValue>> SizeArr;
		SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
		SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
		SlotObj->SetArrayField(TEXT("size"), SizeArr);

		TArray<TSharedPtr<FJsonValue>> AnchorArr;
		AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Minimum.X));
		AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Minimum.Y));
		AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Maximum.X));
		AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Maximum.Y));
		SlotObj->SetArrayField(TEXT("anchor"), AnchorArr);

		TArray<TSharedPtr<FJsonValue>> AlignArr;
		AlignArr.Add(MakeShared<FJsonValueNumber>(Align.X));
		AlignArr.Add(MakeShared<FJsonValueNumber>(Align.Y));
		SlotObj->SetArrayField(TEXT("alignment"), AlignArr);

		FMargin Offsets = CanvasSlot->GetOffsets();
		TArray<TSharedPtr<FJsonValue>> OffArr;
		OffArr.Add(MakeShared<FJsonValueNumber>(Offsets.Left));
		OffArr.Add(MakeShared<FJsonValueNumber>(Offsets.Top));
		OffArr.Add(MakeShared<FJsonValueNumber>(Offsets.Right));
		OffArr.Add(MakeShared<FJsonValueNumber>(Offsets.Bottom));
		SlotObj->SetArrayField(TEXT("offset"), OffArr);

		bool bAutoSize = CanvasSlot->GetAutoSize();
		SlotObj->SetBoolField(TEXT("auto_size"), bAutoSize);
	}
	else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		FSlateChildSize SizeRule = VSlot->GetSize();
		SlotObj->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
		SlotObj->SetNumberField(TEXT("fill_value"), SizeRule.Value);

		FMargin Padding = VSlot->GetPadding();
		TArray<TSharedPtr<FJsonValue>> PadArr;
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Left));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Top));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Right));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Bottom));
		SlotObj->SetArrayField(TEXT("padding"), PadArr);

		SlotObj->SetStringField(TEXT("h_align"), UEnum::GetValueAsString(VSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("v_align"), UEnum::GetValueAsString(VSlot->GetVerticalAlignment()));
	}
	else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		FSlateChildSize SizeRule = HSlot->GetSize();
		SlotObj->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
		SlotObj->SetNumberField(TEXT("fill_value"), SizeRule.Value);

		FMargin Padding = HSlot->GetPadding();
		TArray<TSharedPtr<FJsonValue>> PadArr;
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Left));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Top));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Right));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Bottom));
		SlotObj->SetArrayField(TEXT("padding"), PadArr);

		SlotObj->SetStringField(TEXT("h_align"), UEnum::GetValueAsString(HSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("v_align"), UEnum::GetValueAsString(HSlot->GetVerticalAlignment()));
	}
	else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Slot))
	{
		FMargin Padding = OSlot->GetPadding();
		TArray<TSharedPtr<FJsonValue>> PadArr;
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Left));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Top));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Right));
		PadArr.Add(MakeShared<FJsonValueNumber>(Padding.Bottom));
		SlotObj->SetArrayField(TEXT("padding"), PadArr);

		SlotObj->SetStringField(TEXT("h_align"), UEnum::GetValueAsString(OSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("v_align"), UEnum::GetValueAsString(OSlot->GetVerticalAlignment()));
	}

	return SlotObj;
}

// Serialize widget-type-specific properties — safe, no UPropertyToJsonValue.
static TSharedPtr<FJsonObject> SerializeWidgetProperties(UWidget* Widget)
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	Props->SetBoolField(TEXT("is_visible"), Widget->IsVisible());

	if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		Props->SetStringField(TEXT("text"), TB->GetText().ToString());
		Props->SetArrayField(TEXT("color"), ColorToJsonArray(TB->GetColorAndOpacity().GetSpecifiedColor()));
	}
	else if (UButton* Btn = Cast<UButton>(Widget))
	{
		Props->SetBoolField(TEXT("is_enabled"), Btn->GetIsEnabled());
	}
	else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
	{
		Props->SetNumberField(TEXT("percent"), PB->GetPercent());
		Props->SetArrayField(TEXT("fill_color"), ColorToJsonArray(PB->GetFillColorAndOpacity()));
	}
	else if (UImage* Img = Cast<UImage>(Widget))
	{
		Props->SetArrayField(TEXT("tint_color"), ColorToJsonArray(Img->GetColorAndOpacity()));
		// Report brush resource name if set
		UObject* Resource = Img->GetBrush().GetResourceObject();
		if (Resource)
		{
			Props->SetStringField(TEXT("texture_path"), Resource->GetPathName());
		}
	}
	else if (UBorder* Bdr = Cast<UBorder>(Widget))
	{
		Props->SetArrayField(TEXT("brush_color"), ColorToJsonArray(Bdr->GetBrushColor()));
	}
	else if (USizeBox* SB = Cast<USizeBox>(Widget))
	{
		// SizeBox override getters aren't exposed directly; read from FOptionalSize properties
		// We'll just note it's a SizeBox; the slot size is in the slot info.
	}
	else if (USpacer* Sp = Cast<USpacer>(Widget))
	{
		FVector2D SpSize = Sp->GetSize();
		TArray<TSharedPtr<FJsonValue>> SArr;
		SArr.Add(MakeShared<FJsonValueNumber>(SpSize.X));
		SArr.Add(MakeShared<FJsonValueNumber>(SpSize.Y));
		Props->SetArrayField(TEXT("size"), SArr);
	}

	return Props;
}

// Recursively serialize a widget and its children into a JSON tree.
// Max depth protects against any unforeseen cycles.
static TSharedPtr<FJsonObject> SerializeWidgetTree(UWidget* Widget, UWidgetBlueprint* WidgetBlueprint, int32 Depth = 0)
{
	if (!Widget || Depth > 50) return nullptr;

	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("name"), Widget->GetName());
	Node->SetStringField(TEXT("type"), Widget->GetClass()->GetName());

	// Slot info (safe manual extraction)
	TSharedPtr<FJsonObject> SlotObj = SerializeSlot(Widget->Slot);
	if (SlotObj)
	{
		Node->SetObjectField(TEXT("slot"), SlotObj);
	}

	// Widget-specific properties
	Node->SetObjectField(TEXT("properties"), SerializeWidgetProperties(Widget));

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildrenArr;
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); i++)
		{
			UWidget* Child = Panel->GetChildAt(i);
			TSharedPtr<FJsonObject> ChildNode = SerializeWidgetTree(Child, WidgetBlueprint, Depth + 1);
			if (ChildNode)
			{
				ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildNode));
			}
		}
	}
	// Also handle single-child containers (SizeBox, Border inherit from UContentWidget)
	else if (UContentWidget* Content = Cast<UContentWidget>(Widget))
	{
		UWidget* Child = Content->GetContent();
		if (Child)
		{
			TSharedPtr<FJsonObject> ChildNode = SerializeWidgetTree(Child, WidgetBlueprint, Depth + 1);
			if (ChildNode)
			{
				ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildNode));
			}
		}
	}
	Node->SetArrayField(TEXT("children"), ChildrenArr);

	return Node;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleReadWidgetLayout(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));

	if (!WidgetBlueprint->WidgetTree || !WidgetBlueprint->WidgetTree->RootWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no widget tree or root widget"));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), BlueprintName);

	// Parent class info
	if (WidgetBlueprint->GeneratedClass)
	{
		UClass* ParentClass = WidgetBlueprint->GeneratedClass->GetSuperClass();
		if (ParentClass)
		{
			Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
		}
	}

	// Serialize the widget tree recursively
	TSharedPtr<FJsonObject> RootNode = SerializeWidgetTree(WidgetBlueprint->WidgetTree->RootWidget, WidgetBlueprint);
	if (RootNode)
	{
		Result->SetObjectField(TEXT("root"), RootNode);
	}

	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddSliderToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	// Create Slider widget
	USlider* SliderWidget = WidgetBlueprint->WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), *WidgetName);
	if (!SliderWidget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Slider widget"));

	// Set properties
	double MinValue = 0.0;
	if (Params->TryGetNumberField(TEXT("min_value"), MinValue))
		SliderWidget->SetMinValue(static_cast<float>(MinValue));

	double MaxValue = 1.0;
	if (Params->TryGetNumberField(TEXT("max_value"), MaxValue))
		SliderWidget->SetMaxValue(static_cast<float>(MaxValue));

	double Value = 0.0;
	if (Params->TryGetNumberField(TEXT("value"), Value))
		SliderWidget->SetValue(static_cast<float>(Value));

	double StepSize = 0.0;
	if (Params->TryGetNumberField(TEXT("step_size"), StepSize))
		SliderWidget->SetStepSize(static_cast<float>(StepSize));

	// Slider color
	FLinearColor SliderColor = ParseColor(Params, TEXT("slider_color"), FLinearColor(0.04f, 0.04f, 0.04f, 1.0f));
	SliderWidget->SetSliderBarColor(SliderColor);

	FLinearColor HandleColor = ParseColor(Params, TEXT("handle_color"), FLinearColor::White);
	SliderWidget->SetSliderHandleColor(HandleColor);

	// Orientation
	FString Orientation;
	if (Params->TryGetStringField(TEXT("orientation"), Orientation) && Orientation == TEXT("Vertical"))
		SliderWidget->SetOrientation(EOrientation::Orient_Vertical);

	// Add to parent
	AddChildToParent(SliderWidget, ParentWidget, RootCanvas, Params, FVector2D(200, 20));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetNumberField(TEXT("min_value"), MinValue);
	Result->SetNumberField(TEXT("max_value"), MaxValue);
	Result->SetNumberField(TEXT("value"), Value);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddComboBoxToWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, WidgetName;
	UWidgetBlueprint* WidgetBlueprint = nullptr;
	UPanelWidget* ParentWidget = nullptr;
	UCanvasPanel* RootCanvas = nullptr;
	if (auto Err = LoadWidgetAndParent(Params, BlueprintName, WidgetName, WidgetBlueprint, ParentWidget, RootCanvas))
		return Err;

	// Create ComboBoxString widget
	UComboBoxString* ComboBox = WidgetBlueprint->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), *WidgetName);
	if (!ComboBox)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create ComboBoxString widget"));

	// Add options
	const TArray<TSharedPtr<FJsonValue>>* OptionsArray;
	if (Params->TryGetArrayField(TEXT("options"), OptionsArray))
	{
		for (const auto& Option : *OptionsArray)
		{
			FString OptionStr = Option->AsString();
			if (!OptionStr.IsEmpty())
				ComboBox->AddOption(OptionStr);
		}
	}

	// Set selected option
	FString SelectedOption;
	if (Params->TryGetStringField(TEXT("selected_option"), SelectedOption) && !SelectedOption.IsEmpty())
	{
		ComboBox->SetSelectedOption(SelectedOption);
	}
	else if (OptionsArray && OptionsArray->Num() > 0)
	{
		// Default to first option if none specified
		ComboBox->SetSelectedOption((*OptionsArray)[0]->AsString());
	}

	// Font size — ComboBoxString::Font is set at construction, not runtime-modifiable
	// We skip font_size for ComboBox since the UE API deprecated direct access
	// and provides no setter. The default engine font will be used.

	// Add to parent
	AddChildToParent(ComboBox, ParentWidget, RootCanvas, Params, FVector2D(200, 40));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	if (!SelectedOption.IsEmpty())
		Result->SetStringField(TEXT("selected_option"), SelectedOption);
	if (OptionsArray)
	{
		TArray<TSharedPtr<FJsonValue>> OptionsOut;
		for (const auto& Opt : *OptionsArray)
			OptionsOut.Add(MakeShared<FJsonValueString>(Opt->AsString()));
		Result->SetArrayField(TEXT("options"), OptionsOut);
	}
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));

	FString PropertyValue;
	if (!Params->TryGetStringField(TEXT("property_value"), PropertyValue))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(BlueprintName);
	if (!WidgetBlueprint)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *BlueprintName));

	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));

	// Find the property via reflection
	FProperty* Prop = Widget->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found on widget '%s' (class: %s)"), *PropertyName, *WidgetName, *Widget->GetClass()->GetName()));

	// Use ImportText for maximum type compatibility (handles FText, FLinearColor, FVector, enums, etc.)
	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Widget);
	const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, PropAddr, Widget, PPF_None);
	if (!ImportResult)
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s' to value '%s'"), *PropertyName, *PropertyValue));

	CompileWidget(WidgetBlueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	ResultObj->SetStringField(TEXT("property_name"), PropertyName);
	ResultObj->SetStringField(TEXT("property_value"), PropertyValue);
	ResultObj->SetBoolField(TEXT("success"), true);
	return ResultObj;
}