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
#include "Styling/SlateBrush.h"
#include "Commands/UnrealMCPAssetProperties.h"
#include "Components/SizeBoxSlot.h"
#include "Components/BorderSlot.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Animation/WidgetAnimation.h"

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
	if (CommandName == TEXT("reparent_widget")) return HandleReparentWidget(Params);
	if (CommandName == TEXT("get_widget_preview_settings")) return HandleWidgetPreviewSettings(Params, false);
	if (CommandName == TEXT("set_widget_preview_settings")) return HandleWidgetPreviewSettings(Params, true);
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

// Serialize an FVector2D as [x, y].
static TArray<TSharedPtr<FJsonValue>> Vector2ToJsonArray(const FVector2D& V)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Add(MakeShared<FJsonValueNumber>(V.X));
	Arr.Add(MakeShared<FJsonValueNumber>(V.Y));
	return Arr;
}

// Serialize an FMargin as {left,top,right,bottom}.
static TSharedPtr<FJsonObject> MarginToJsonObject(const FMargin& M)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("left"), M.Left);
	Obj->SetNumberField(TEXT("top"), M.Top);
	Obj->SetNumberField(TEXT("right"), M.Right);
	Obj->SetNumberField(TEXT("bottom"), M.Bottom);
	return Obj;
}

// Serialize an FSlateBrush to a structured JSON object with the fields UI authors
// actually configure: draw_as, tint, image_size, margin (9-slice), tiling, mirroring,
// and resource_object path (texture/material).
static TSharedPtr<FJsonObject> BrushToJsonObject(const FSlateBrush& Brush)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("draw_as"), UEnum::GetValueAsString(Brush.DrawAs));
	Obj->SetStringField(TEXT("tiling"), UEnum::GetValueAsString(Brush.Tiling));
	Obj->SetStringField(TEXT("mirroring"), UEnum::GetValueAsString(Brush.Mirroring));
	Obj->SetArrayField(TEXT("tint"), ColorToJsonArray(Brush.TintColor.GetSpecifiedColor()));
	Obj->SetArrayField(TEXT("image_size"), Vector2ToJsonArray(Brush.ImageSize));
	Obj->SetObjectField(TEXT("margin"), MarginToJsonObject(Brush.Margin));
	if (UObject* Resource = Brush.GetResourceObject())
	{
		Obj->SetStringField(TEXT("resource_object"), Resource->GetPathName());
		Obj->SetStringField(TEXT("resource_class"), Resource->GetClass()->GetName());
	}
	if (!Brush.GetResourceName().IsNone())
	{
		Obj->SetStringField(TEXT("resource_name"), Brush.GetResourceName().ToString());
	}
	// RoundedBox outline settings, when DrawAs == RoundedBox
	if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox)
	{
		TSharedPtr<FJsonObject> Outline = MakeShared<FJsonObject>();
		Outline->SetArrayField(TEXT("color"), ColorToJsonArray(Brush.OutlineSettings.Color.GetSpecifiedColor()));
		Outline->SetNumberField(TEXT("width"), Brush.OutlineSettings.Width);
		Outline->SetBoolField(TEXT("use_brush_transparency"), Brush.OutlineSettings.bUseBrushTransparency);
		Outline->SetNumberField(TEXT("rounding_type"), static_cast<int32>(Brush.OutlineSettings.RoundingType));
		// CornerRadii: FVector4
		TArray<TSharedPtr<FJsonValue>> Radii;
		Radii.Add(MakeShared<FJsonValueNumber>(Brush.OutlineSettings.CornerRadii.X));
		Radii.Add(MakeShared<FJsonValueNumber>(Brush.OutlineSettings.CornerRadii.Y));
		Radii.Add(MakeShared<FJsonValueNumber>(Brush.OutlineSettings.CornerRadii.Z));
		Radii.Add(MakeShared<FJsonValueNumber>(Brush.OutlineSettings.CornerRadii.W));
		Outline->SetArrayField(TEXT("corner_radii"), Radii);
		Obj->SetObjectField(TEXT("outline"), Outline);
	}
	return Obj;
}

// Generic per-widget-class property fallback: walks all UPROPERTYs on the concrete
// widget class (skipping fields inherited from UWidget / UVisual base classes and
// fields already covered by the hand-written structured branches). Uses UE's own
// ExportTextItem so any type serializes safely without a special-case cast.
// This is what makes previously-invisible widget classes (SafeZone, ScrollBox,
// BackgroundBlur, RichTextBlock, CommonTextBlock's Style ref, SizeBox overrides,
// etc.) show up in read_widget_layout automatically.
static void AppendGenericWidgetProperties(UWidget* Widget, TSharedPtr<FJsonObject> Props, const TSet<FName>& AlreadyCovered)
{
	if (!Widget) return;
	UClass* Cls = Widget->GetClass();
	if (!Cls) return;

	// Only report fields declared on classes at or below UWidget in the hierarchy,
	// but skip UWidget / UVisual / UObject themselves (their 300+ fields are noise).
	UClass* WidgetBase = UWidget::StaticClass();
	for (TFieldIterator<FProperty> It(Cls, EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;
		if (AlreadyCovered.Contains(Prop->GetFName())) continue;
		// Only Details-panel-visible properties.
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
		// Skip everything owned by base UWidget itself. Fields declared on subclasses
		// (UPanelWidget, UTextBlock, USizeBox, ...) all pass because Owner is below UWidget.
		UClass* Owner = Prop->GetOwnerClass();
		if (!Owner) continue;
		if (Owner == WidgetBase) continue;
		if (!Owner->IsChildOf(WidgetBase)) continue;

		const void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Widget);
		FString ValStr;
		Prop->ExportTextItem_Direct(ValStr, ValPtr, nullptr, nullptr, PPF_None);
		if (!ValStr.IsEmpty() && ValStr.Len() < 4096)
		{
			Props->SetStringField(Prop->GetName(), ValStr);
		}
	}
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

static TSharedPtr<FJsonObject> UMGFailure(const FString& Error)
{
	auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), false);
	Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
	Result->SetStringField(TEXT("stage"), TEXT("preflight")); Result->SetStringField(TEXT("error"), Error); return Result;
}

static UWidgetBlueprint* ResolveEditableWidgetBlueprint(const TSharedPtr<FJsonObject>& Params, FString& Error)
{
	FString Path;
	if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor) { Error = TEXT("Requires editor mode outside PIE/Simulate"); return nullptr; }
	if (!Params->TryGetStringField(TEXT("path"), Path) || !Path.StartsWith(TEXT("/Game/"))) { Error = TEXT("path must identify a /Game Widget Blueprint"); return nullptr; }
	auto Blueprint = FindWidgetBlueprint(Path);
	if (!Blueprint || !Blueprint->WidgetTree) { Error = TEXT("Widget Blueprint with a local WidgetTree not found"); return nullptr; }
	return Blueprint;
}

static TArray<FName> MovableSlotFields(UClass* Class)
{
	if (Class == UCanvasPanelSlot::StaticClass()) return {TEXT("LayoutData"), TEXT("bAutoSize"), TEXT("ZOrder")};
	if (Class == UHorizontalBoxSlot::StaticClass() || Class == UVerticalBoxSlot::StaticClass())
		return {TEXT("Padding"), TEXT("Size"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment")};
	if (Class == UOverlaySlot::StaticClass() || Class == USizeBoxSlot::StaticClass() || Class == UBorderSlot::StaticClass())
		return {TEXT("Padding"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment")};
	return {};
}

static TSharedPtr<FJsonObject> StructuredSlot(UPanelSlot* Slot)
{
	auto Result = MakeShared<FJsonObject>();
	if (!Slot) return Result;
	Result->SetStringField(TEXT("class_path"), Slot->GetClass()->GetPathName());
	auto Values = MakeShared<FJsonObject>(); auto Schemas = MakeShared<FJsonObject>();
	for (FName Name : MovableSlotFields(Slot->GetClass()))
	{
		auto Property = FindFProperty<FProperty>(Slot->GetClass(), Name); FString Error;
		if (!Property) continue;
		auto Value = UnrealMCPAssetProperties::EncodeValue(Property, Property->ContainerPtrToValuePtr<void>(Slot), Error);
		if (Value) Values->SetField(Name.ToString(), Value);
		Schemas->SetObjectField(Name.ToString(), UnrealMCPAssetProperties::DescribeValue(Property));
	}
	Result->SetObjectField(TEXT("properties"), Values); Result->SetObjectField(TEXT("schema"), Schemas); return Result;
}

static TSharedPtr<FJsonObject> WidgetPlacement(UWidget* Widget)
{
	auto Result = MakeShared<FJsonObject>(); auto Parent = Widget->GetParent();
	Result->SetStringField(TEXT("widget_path"), Widget->GetPathName());
	Result->SetStringField(TEXT("parent"), Parent ? Parent->GetName() : FString());
	Result->SetNumberField(TEXT("index"), Parent ? Parent->GetChildIndex(Widget) : INDEX_NONE);
	Result->SetObjectField(TEXT("slot"), StructuredSlot(Widget->Slot)); return Result;
}

static TSharedPtr<FJsonObject> WidgetPreviewInfo(UUserWidget* Defaults)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mode"), StaticEnum<EDesignPreviewSizeMode>()->GetNameStringByValue(static_cast<int64>(Defaults->DesignSizeMode)));
	Result->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(Defaults->DesignTimeSize.X), MakeShared<FJsonValueNumber>(Defaults->DesignTimeSize.Y)});
	Result->SetBoolField(TEXT("size_applies"), Defaults->DesignSizeMode == EDesignPreviewSizeMode::Custom || Defaults->DesignSizeMode == EDesignPreviewSizeMode::CustomOnScreen);
	Result->SetBoolField(TEXT("editor_only"), true);
	Result->SetStringField(TEXT("size_semantics"), TEXT("Stored custom Designer size; not measured live geometry, embedded-card bounds, DPI-adjusted screen resolution or runtime layout"));
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleWidgetPreviewSettings(const TSharedPtr<FJsonObject>& Params, bool Write)
{
	FString Error; auto Blueprint = ResolveEditableWidgetBlueprint(Params, Error); if (!Blueprint) return UMGFailure(Error);
	auto Defaults = Blueprint->GeneratedClass ? Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Defaults) return UMGFailure(TEXT("A compiled Widget Blueprint generated class is required; no implicit compilation was performed"));
	auto Result = MakeShared<FJsonObject>(); auto Before = WidgetPreviewInfo(Defaults);
	Result->SetStringField(TEXT("path"), Blueprint->GetPathName());
	TArray<TSharedPtr<FJsonValue>> Modes;
	const TArray<FString> AllowedModes = {TEXT("FillScreen"), TEXT("Custom"), TEXT("CustomOnScreen"), TEXT("Desired"), TEXT("DesiredOnScreen")};
	for (const FString& Mode : AllowedModes) Modes.Add(MakeShared<FJsonValueString>(Mode));
	Result->SetArrayField(TEXT("supported_modes"), Modes);
	for (const auto& Pair : Params->Values)
		if (Pair.Key != TEXT("path") && (!Write || (Pair.Key != TEXT("mode") && Pair.Key != TEXT("size") && Pair.Key != TEXT("dry_run"))))
			return UMGFailure(TEXT("Unknown preview parameter: ") + Pair.Key);
	bool Modified = false;
	if (Write)
	{
		FString Mode; bool DryRun = true;
		if (!Params->TryGetStringField(TEXT("mode"), Mode) || !AllowedModes.Contains(Mode)
			|| (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))) return UMGFailure(TEXT("mode must match a supported Designer mode; dry_run must be boolean"));
		const auto NewMode = static_cast<EDesignPreviewSizeMode>(StaticEnum<EDesignPreviewSizeMode>()->GetValueByNameString(Mode));
		FVector2D Size = Defaults->DesignTimeSize;
		if (Params->HasField(TEXT("size")))
		{
			if (NewMode != EDesignPreviewSizeMode::Custom && NewMode != EDesignPreviewSizeMode::CustomOnScreen)
				return UMGFailure(TEXT("size is only supported for Custom/CustomOnScreen modes"));
			const TArray<TSharedPtr<FJsonValue>>* Values;
			if (!Params->TryGetArrayField(TEXT("size"), Values) || Values->Num() != 2) return UMGFailure(TEXT("size must contain exactly two finite positive numbers"));
			for (int32 Index = 0; Index < 2; ++Index)
			{
				double Number;
				if (!(*Values)[Index]->TryGetNumber(Number) || !FMath::IsFinite(Number) || Number <= 0 || Number > 100000)
					return UMGFailure(TEXT("size dimensions must be finite, >0 and <=100000 Designer units"));
				Size[Index] = Number;
			}
		}
		if ((NewMode == EDesignPreviewSizeMode::Custom || NewMode == EDesignPreviewSizeMode::CustomOnScreen) && (Size.X <= 0 || Size.Y <= 0))
			return UMGFailure(TEXT("Custom preview requires a positive size"));
		const bool Changed = NewMode != Defaults->DesignSizeMode || Size != Defaults->DesignTimeSize;
		Result->SetObjectField(TEXT("before"), Before); Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), Changed);
		auto Requested = MakeShared<FJsonObject>(); Requested->SetStringField(TEXT("mode"), Mode);
		Requested->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(Size.X), MakeShared<FJsonValueNumber>(Size.Y)}); Result->SetObjectField(TEXT("requested"), Requested);
		if (!DryRun && Changed)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "WidgetPreview", "Set Widget Designer preview"));
			Blueprint->Modify(); Defaults->SetFlags(RF_Transactional); Defaults->Modify();
			Defaults->DesignSizeMode = NewMode; Defaults->DesignTimeSize = Size;
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); Modified = true;
		}
	}
	Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), Modified); Result->SetBoolField(TEXT("saved"), false);
	Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetPackage()->IsDirty()); Result->SetObjectField(TEXT("settings"), WidgetPreviewInfo(Defaults));
	return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleReparentWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString Error, WidgetName, ParentName; double IndexValue; bool DryRun = true;
	auto Blueprint = ResolveEditableWidgetBlueprint(Params, Error); if (!Blueprint) return UMGFailure(Error);
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty()
		|| !Params->TryGetStringField(TEXT("new_parent_name"), ParentName) || ParentName.IsEmpty()
		|| !Params->TryGetNumberField(TEXT("insert_index"), IndexValue) || !FMath::IsFinite(IndexValue) || IndexValue < 0 || IndexValue > MAX_int32 || IndexValue != FMath::FloorToDouble(IndexValue)
		|| (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))) return UMGFailure(TEXT("Require widget_name, new_parent_name and integer insert_index; dry_run must be boolean"));
	const int32 Index = static_cast<int32>(IndexValue);
	for (const auto& Pair : Params->Values)
		if (Pair.Key != TEXT("path") && Pair.Key != TEXT("widget_name") && Pair.Key != TEXT("new_parent_name") && Pair.Key != TEXT("insert_index")
			&& Pair.Key != TEXT("slot_properties") && Pair.Key != TEXT("dry_run")) return UMGFailure(TEXT("Unknown reparent parameter: ") + Pair.Key);
	auto Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
	auto Parent = Cast<UPanelWidget>(Blueprint->WidgetTree->FindWidget(FName(*ParentName)));
	if (!Widget || Widget->GetOuter() != Blueprint->WidgetTree || !Parent || Parent->GetOuter() != Blueprint->WidgetTree)
		return UMGFailure(TEXT("Select existing widgets owned by this WidgetTree; inherited/nested user-widget trees are not supported"));
	auto OldParent = Widget->GetParent(); auto OldSlot = Widget->Slot;
	if (Widget == Blueprint->WidgetTree->RootWidget || !OldParent || !OldSlot || OldParent->GetOuter() != Blueprint->WidgetTree)
		return UMGFailure(TEXT("Only locally parented non-root widgets can be moved"));
	const TSet<UClass*> Panels = {UCanvasPanel::StaticClass(), UHorizontalBox::StaticClass(), UVerticalBox::StaticClass(), UOverlay::StaticClass(), USizeBox::StaticClass(), UBorder::StaticClass()};
	if (!Panels.Contains(Parent->GetClass()) || !Panels.Contains(OldParent->GetClass())) return UMGFailure(TEXT("Supported source/target panels: CanvasPanel, HorizontalBox, VerticalBox, Overlay, SizeBox, Border (native classes)"));
	for (UWidget* Ancestor = Parent; Ancestor; Ancestor = Ancestor->GetParent())
		if (Ancestor == Widget) return UMGFailure(TEXT("Reparent would create a cycle"));
	const bool SameParent = Parent == OldParent;
	if (!SameParent)
		for (auto Animation : Blueprint->Animations)
			if (Animation) for (const auto& Binding : Animation->GetBindings())
				if (!Binding.SlotWidgetName.IsNone() && (Binding.WidgetName == Widget->GetFName() || Binding.SlotWidgetName == Widget->GetFName()))
					return UMGFailure(TEXT("Widget has an animated Slot binding; reparenting would replace that Slot. Migrate its animation tracks explicitly before moving"));
	const int32 OldIndex = OldParent->GetChildIndex(Widget), MaxIndex = Parent->GetChildrenCount() - (SameParent ? 1 : 0);
	if (OldIndex == INDEX_NONE || Index < 0 || Index > MaxIndex) return UMGFailure(TEXT("insert_index must be a final zero-based index after removal, within target children; appending is allowed"));
	if (!SameParent && !Parent->CanAddMoreChildren()) return UMGFailure(TEXT("Target single-content container is full; no content is replaced"));
	TStrongObjectPtr<UPanelSlot> Candidate(NewObject<UPanelSlot>(GetTransientPackage(), Parent->GetSlotClass(), NAME_None, RF_Transient));
	if (SameParent || OldSlot->GetClass() == Candidate->GetClass())
		for (FName Name : MovableSlotFields(Candidate->GetClass()))
			if (auto Property = FindFProperty<FProperty>(Candidate->GetClass(), Name))
				Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Candidate.Get()), Property->ContainerPtrToValuePtr<void>(OldSlot));
	const TSharedPtr<FJsonObject>* Properties;
	if (Params->HasField(TEXT("slot_properties")))
	{
		if (!Params->TryGetObjectField(TEXT("slot_properties"), Properties)) return UMGFailure(TEXT("slot_properties must be an object"));
		for (const auto& Pair : (*Properties)->Values)
		{
			if (!MovableSlotFields(Candidate->GetClass()).Contains(FName(*Pair.Key))) return UMGFailure(TEXT("Unsupported slot property: ") + Pair.Key);
			auto Property = FindFProperty<FProperty>(Candidate->GetClass(), *Pair.Key);
			if (!Property || !UnrealMCPAssetProperties::DecodeValue(Property, Property->ContainerPtrToValuePtr<void>(Candidate.Get()), Pair.Value, Error))
				return UMGFailure(Pair.Key + TEXT(": ") + Error);
		}
	}
	bool SlotChanged = false;
	if (SameParent) for (FName Name : MovableSlotFields(Candidate->GetClass()))
	{
		auto Property = FindFProperty<FProperty>(Candidate->GetClass(), Name);
		SlotChanged |= !Property->Identical_InContainer(OldSlot, Candidate.Get());
	}
	const bool Changed = !SameParent || OldIndex != Index || SlotChanged;
	auto Result = MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("before"), WidgetPlacement(Widget));
	auto Requested = MakeShared<FJsonObject>(); Requested->SetStringField(TEXT("parent"), ParentName); Requested->SetNumberField(TEXT("index"), Index);
	Requested->SetObjectField(TEXT("slot"), StructuredSlot(Candidate.Get())); Result->SetObjectField(TEXT("requested"), Requested);
	if (!DryRun && Changed)
	{
		TStrongObjectPtr<UPanelSlot> OldTemplate(NewObject<UPanelSlot>(GetTransientPackage(), OldSlot->GetClass(), NAME_None, RF_Transient));
		for (FName Name : MovableSlotFields(OldSlot->GetClass()))
		{
			auto Property = FindFProperty<FProperty>(OldSlot->GetClass(), Name);
			Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(OldTemplate.Get()), Property->ContainerPtrToValuePtr<void>(OldSlot));
		}
		const bool DirtyBefore = Blueprint->GetPackage()->IsDirty();
		FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "ReparentWidget", "Reparent widget"));
		for (UObject* Object : TArray<UObject*>{Blueprint, Blueprint->WidgetTree, Widget, OldParent, Parent, OldSlot})
		{ Object->SetFlags(RF_Transactional); Object->Modify(); }
		if (SameParent)
		{
			Parent->ShiftChild(Index, Widget);
			for (FName Name : MovableSlotFields(Candidate->GetClass()))
			{
				auto Property = FindFProperty<FProperty>(Candidate->GetClass(), Name);
				Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(OldSlot), Property->ContainerPtrToValuePtr<void>(Candidate.Get()));
			}
			OldSlot->SynchronizeProperties();
		}
		else if (!Parent->InsertChildAt(Index, Widget, Candidate.Get()))
		{
			if (Widget->GetParent() != OldParent) OldParent->InsertChildAt(OldIndex, Widget, OldTemplate.Get());
			const bool Restored = Widget->GetParent() == OldParent && OldParent->GetChildIndex(Widget) == OldIndex;
			Transaction.Cancel();
			if (Restored) Blueprint->GetPackage()->SetDirtyFlag(DirtyBefore);
			auto Failure = UMGFailure(TEXT("Insertion failed; original placement restoration attempted"));
			Failure->SetStringField(TEXT("stage"), TEXT("apply")); Failure->SetBoolField(TEXT("rolled_back"), Restored);
			Failure->SetBoolField(TEXT("modified"), !Restored); Failure->SetObjectField(TEXT("after"), WidgetPlacement(Widget)); return Failure;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), Changed);
	Result->SetBoolField(TEXT("modified"), !DryRun && Changed); Result->SetBoolField(TEXT("saved"), false);
	Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetPackage()->IsDirty()); Result->SetObjectField(TEXT("after"), WidgetPlacement(Widget)); return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor/Transactor.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

static UWidgetBlueprint* MakeUMGFixture(const FString& Path)
{
	auto Factory = NewObject<UWidgetBlueprintFactory>();
	auto Blueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(*Path),
		FName(*FPackageName::GetLongPackageAssetName(Path)), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (Blueprint) FAssetRegistryModule::AssetCreated(Blueprint);
	return Blueprint;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPUMGReparentTest, "UnrealMCP.UMG.ReparentWidget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPUMGReparentTest::RunTest(const FString& Parameters)
{
	const FString Path = TEXT("/Game/__Dev/UMGReparent_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/WBP_Test");
	auto Blueprint = MakeUMGFixture(Path);
	if (!TestNotNull(TEXT("Widget Blueprint fixture"), Blueprint)) return false;
	auto Tree = Blueprint->WidgetTree;
	auto Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root")); Tree->RootWidget = Root;
	auto Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("KeyText")); Root->AddChild(Text);
	Text->SetText(FText::FromString(TEXT("F"))); auto Font = Text->GetFont(); Font.Size = 24; Text->SetFont(Font);
	auto Image = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("KeyFrame")); Root->AddChild(Image);
	UObject* Texture = UEditorAssetLibrary::LoadAsset(TEXT("/Engine/EngineResources/DefaultTexture"));
	FSlateBrush Brush; Brush.SetResourceObject(Texture); Image->SetBrush(Brush);
	TArray<UPanelWidget*> Parents;
	for (UClass* Class : TArray<UClass*>{UHorizontalBox::StaticClass(), UVerticalBox::StaticClass(), UOverlay::StaticClass(), USizeBox::StaticClass(), UBorder::StaticClass(), UCanvasPanel::StaticClass()})
	{
		auto Panel = Cast<UPanelWidget>(Tree->ConstructWidget<UWidget>(Class, FName(*Class->GetName())));
		Root->AddChild(Panel); Parents.Add(Panel);
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FDelegateEditorBinding DelegateBinding; DelegateBinding.ObjectName = TEXT("KeyText"); DelegateBinding.PropertyName = TEXT("Text"); DelegateBinding.FunctionName = TEXT("GetKeyText");
	Blueprint->Bindings.Add(DelegateBinding);
	auto Animation = NewObject<UWidgetAnimation>(Blueprint, TEXT("KeyPulse"), RF_Transactional);
	FWidgetAnimationBinding AnimationBinding; AnimationBinding.WidgetName = Text->GetFName(); AnimationBinding.AnimationGuid = FGuid::NewGuid(); AnimationBinding.bIsRootWidget = false;
	Animation->AnimationBindings.Add(AnimationBinding); Blueprint->Animations.Add(Animation);
	Blueprint->OnVariableAdded(Animation->GetFName());
	FUnrealMCPUMGCommands Commands;
	auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("path"), Path); Request->SetStringField(TEXT("widget_name"), TEXT("KeyText")); Request->SetNumberField(TEXT("insert_index"), 0);
	const FString ObjectPath = Text->GetPathName(); auto OriginalSlot = Text->Slot;
	auto OriginalLayout = CastChecked<UCanvasPanelSlot>(OriginalSlot)->GetLayout();
	for (UPanelWidget* Parent : Parents)
	{
		Request->SetStringField(TEXT("new_parent_name"), Parent->GetName()); Request->SetBoolField(TEXT("dry_run"), true);
		Blueprint->GetPackage()->SetDirtyFlag(false);
		auto Preview = Commands.HandleCommand(TEXT("reparent_widget"), Request);
		if (!TestTrue(TEXT("Preview supported panel: ") + Parent->GetName(), Preview->GetBoolField(TEXT("success"))))
		{ AddError(Preview->GetStringField(TEXT("error"))); return false; }
		TestFalse(TEXT("Preview leaves package clean"), Blueprint->GetPackage()->IsDirty());
		TestEqual(TEXT("Preview leaves original parent"), Text->GetParent(), static_cast<UPanelWidget*>(Root));
		TestEqual(TEXT("Preview leaves original Slot"), Text->Slot.Get(), OriginalSlot.Get());
		auto SlotValues = Preview->GetObjectField(TEXT("requested"))->GetObjectField(TEXT("slot"))->GetObjectField(TEXT("properties"));
		if (Parent->GetClass() != UCanvasPanel::StaticClass())
		{
			SlotValues->SetStringField(TEXT("HorizontalAlignment"), TEXT("HAlign_Center")); SlotValues->SetStringField(TEXT("VerticalAlignment"), TEXT("VAlign_Center"));
		}
		Request->SetObjectField(TEXT("slot_properties"), SlotValues); Request->SetBoolField(TEXT("dry_run"), false);
		auto Applied = Commands.HandleCommand(TEXT("reparent_widget"), Request);
		if (!TestTrue(TEXT("Move to panel: ") + Parent->GetName(), Applied->GetBoolField(TEXT("success"))))
		{ AddError(Applied->GetStringField(TEXT("error"))); return false; }
		TestEqual(TEXT("Original widget pointer preserved"), Tree->FindWidget(TEXT("KeyText")), static_cast<UWidget*>(Text));
		TestEqual(TEXT("Original widget path preserved"), Text->GetPathName(), ObjectPath);
		TestEqual(TEXT("New parent"), Text->GetParent(), Parent); TestEqual(TEXT("Correct slot type"), Text->Slot->GetClass(), Parent->GetSlotClass());
		TestEqual(TEXT("Font unchanged"), Text->GetFont().Size, 24.f); TestEqual(TEXT("Text unchanged"), Text->GetText().ToString(), FString(TEXT("F")));
		TestTrue(TEXT("Property binding unchanged"), Blueprint->Bindings.Num() == 1 && Blueprint->Bindings[0].FunctionName == TEXT("GetKeyText"));
		TestTrue(TEXT("Animation references unchanged"), Blueprint->Animations[0] == Animation && Animation->AnimationBindings[0] == AnimationBinding);
		TestEqual(TEXT("Unmoved image texture unchanged"), Image->GetBrush().GetResourceObject(), Texture);
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo restores original parent"), Text->GetParent(), static_cast<UPanelWidget*>(Root));
		TestEqual(TEXT("Undo restores original slot identity"), Text->Slot.Get(), OriginalSlot.Get());
		TestTrue(TEXT("Undo restores Canvas layout"), CastChecked<UCanvasPanelSlot>(Text->Slot)->GetLayout() == OriginalLayout);
		GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores requested parent"), Text->GetParent(), Parent);
		GEditor->UndoTransaction();
		Request->RemoveField(TEXT("slot_properties"));
	}
	Blueprint->GetPackage()->SetDirtyFlag(false);
	const FString BeforeUndo = GEditor->Trans->GetUndoContext().TransactionId.ToString();
	for (const FString Invalid : {TEXT("Missing"), TEXT("KeyText"), TEXT("KeyFrame")})
	{
		Request->SetStringField(TEXT("new_parent_name"), Invalid);
		TestFalse(TEXT("Invalid/noncontainer parent refused"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	}
	Request->SetStringField(TEXT("new_parent_name"), Parents[0]->GetName()); Request->SetNumberField(TEXT("insert_index"), 9);
	TestFalse(TEXT("Out of range index refused"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	Request->SetNumberField(TEXT("insert_index"), 0.5);
	TestFalse(TEXT("Fractional index refused rather than truncated"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	Request->SetNumberField(TEXT("insert_index"), 0);
	auto InvalidProperties = MakeShared<FJsonObject>(); InvalidProperties->SetStringField(TEXT("HorizontalAlignment"), TEXT("Bogus")); Request->SetObjectField(TEXT("slot_properties"), InvalidProperties);
	TestFalse(TEXT("Invalid Slot enum refused before removal"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	InvalidProperties->SetBoolField(TEXT("Content"), true);
	TestFalse(TEXT("Non-layout Slot fields refused"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	Request->RemoveField(TEXT("slot_properties"));
	auto Border = Parents[4]; auto Occupant = Tree->ConstructWidget<USpacer>(); Border->AddChild(Occupant); Blueprint->OnVariableAdded(Occupant->GetFName());
	Blueprint->GetPackage()->SetDirtyFlag(false); Request->SetStringField(TEXT("new_parent_name"), Border->GetName());
	TestFalse(TEXT("Full single-child Border refused"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	Request->SetStringField(TEXT("widget_name"), TEXT("Root")); Request->SetStringField(TEXT("new_parent_name"), Parents[0]->GetName());
	TestFalse(TEXT("Root move refused"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	auto Nested = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Nested")); Parents[0]->AddChild(Nested);
	Blueprint->OnVariableAdded(Nested->GetFName());
	Blueprint->GetPackage()->SetDirtyFlag(false); Request->SetStringField(TEXT("widget_name"), Parents[0]->GetName()); Request->SetStringField(TEXT("new_parent_name"), TEXT("Nested"));
	TestFalse(TEXT("Cycle refused"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	Request->SetStringField(TEXT("widget_name"), TEXT("KeyText")); Request->SetStringField(TEXT("new_parent_name"), Parents[0]->GetName());
	FWidgetAnimationBinding SlotBinding = AnimationBinding; SlotBinding.SlotWidgetName = TEXT("CanvasPanelSlot"); Animation->AnimationBindings.Add(SlotBinding);
	TestFalse(TEXT("Animated Slot refuses replacement"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	Animation->AnimationBindings.Pop();
	TestEqual(TEXT("Failed requests preserve original Widget parent"), Text->GetParent(), static_cast<UPanelWidget*>(Root));
	TestEqual(TEXT("Failed requests preserve original Widget Slot"), Text->Slot.Get(), OriginalSlot.Get());
	TestFalse(TEXT("Failed preflight leaves package clean"), Blueprint->GetPackage()->IsDirty());
	TestEqual(TEXT("Failed requests do not add undo"), GEditor->Trans->GetUndoContext().TransactionId.ToString(), BeforeUndo);
	Request->SetStringField(TEXT("new_parent_name"), TEXT("Root")); Request->SetNumberField(TEXT("insert_index"), Root->GetChildrenCount() - 1);
	TestTrue(TEXT("Same-parent reorder succeeds"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("success")));
	TestEqual(TEXT("Same-parent keeps Slot identity"), Text->Slot.Get(), OriginalSlot.Get());
	TestEqual(TEXT("Final-index semantics"), Root->GetChildIndex(Text), Root->GetChildrenCount() - 1);
	TestFalse(TEXT("Repeated identical request is no-op"), Commands.HandleCommand(TEXT("reparent_widget"), Request)->GetBoolField(TEXT("modified")));
	GEditor->UndoTransaction(); TestEqual(TEXT("Reorder undo"), Root->GetChildIndex(Text), 0);
	Blueprint->GetPackage()->SetDirtyFlag(false);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPUMGPreviewTest, "UnrealMCP.UMG.PreviewSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPUMGPreviewTest::RunTest(const FString& Parameters)
{
	const FString Path = TEXT("/Game/__Dev/UMGPreview_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/WBP_Test");
	auto Blueprint = MakeUMGFixture(Path); if (!TestNotNull(TEXT("Preview Blueprint fixture"), Blueprint)) return false;
	auto Root = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(); Blueprint->WidgetTree->RootWidget = Root;
	auto Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text")); Root->AddChild(Text);
	auto Font = Text->GetFont(); Font.Size = 37; Text->SetFont(Font);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	auto Defaults = CastChecked<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject());
	const auto OriginalMode = Defaults->DesignSizeMode; const auto OriginalSize = Defaults->DesignTimeSize;
	FUnrealMCPUMGCommands Commands; auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("path"), Path);
	Blueprint->GetPackage()->SetDirtyFlag(false);
	auto Read = Commands.HandleCommand(TEXT("get_widget_preview_settings"), Request);
	TestTrue(TEXT("Read preview"), Read->GetBoolField(TEXT("success"))); TestFalse(TEXT("Read not dirty"), Blueprint->GetPackage()->IsDirty());
	Request->SetStringField(TEXT("mode"), TEXT("Custom")); Request->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(320), MakeShared<FJsonValueNumber>(120)});
	auto Preview = Commands.HandleCommand(TEXT("set_widget_preview_settings"), Request);
	TestFalse(TEXT("Preview defaults to no modification"), Preview->GetBoolField(TEXT("modified")));
	TestTrue(TEXT("Preview leaves existing size"), Defaults->DesignTimeSize == OriginalSize);
	Request->SetBoolField(TEXT("dry_run"), false);
	for (double Bad : {-1., 0., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		Request->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(Bad), MakeShared<FJsonValueNumber>(120)});
		TestFalse(TEXT("Bad preview size refused"), Commands.HandleCommand(TEXT("set_widget_preview_settings"), Request)->GetBoolField(TEXT("success")));
	}
	Request->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(320), MakeShared<FJsonValueNumber>(120)});
	auto Applied = Commands.HandleCommand(TEXT("set_widget_preview_settings"), Request);
	TestTrue(TEXT("Set custom preview"), Applied->GetBoolField(TEXT("success"))); TestFalse(TEXT("Preview settings never autosave"), Applied->GetBoolField(TEXT("saved")));
	TestTrue(TEXT("CDO custom size set"), Defaults->DesignTimeSize == FVector2D(320,120));
	TestTrue(TEXT("CDO custom mode set"), Defaults->DesignSizeMode == EDesignPreviewSizeMode::Custom);
	TestEqual(TEXT("Font unchanged"), Text->GetFont().Size, 37.f); TestEqual(TEXT("Root unchanged"), Blueprint->WidgetTree->RootWidget.Get(), static_cast<UWidget*>(Root));
	GEditor->UndoTransaction(); TestTrue(TEXT("Preview mode/size Undo"), Defaults->DesignSizeMode == OriginalMode && Defaults->DesignTimeSize == OriginalSize);
	GEditor->RedoTransaction(); TestTrue(TEXT("Preview Redo"), Defaults->DesignSizeMode == EDesignPreviewSizeMode::Custom && Defaults->DesignTimeSize == FVector2D(320,120));
	TestFalse(TEXT("Preview no-op"), Commands.HandleCommand(TEXT("set_widget_preview_settings"), Request)->GetBoolField(TEXT("modified")));
	Request->SetStringField(TEXT("mode"), TEXT("Desired"));
	TestFalse(TEXT("No silently ignored size in Desired mode"), Commands.HandleCommand(TEXT("set_widget_preview_settings"), Request)->GetBoolField(TEXT("success")));
	Request->RemoveField(TEXT("size"));
	for (const FString Mode : {TEXT("FillScreen"), TEXT("Desired"), TEXT("DesiredOnScreen"), TEXT("CustomOnScreen"), TEXT("Custom")})
	{
		Request->SetStringField(TEXT("mode"), Mode); TestTrue(TEXT("Supported mode: ") + Mode, Commands.HandleCommand(TEXT("set_widget_preview_settings"), Request)->GetBoolField(TEXT("success")));
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Defaults = CastChecked<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject());
	TestTrue(TEXT("Editor settings survive compile"), Defaults->DesignSizeMode == EDesignPreviewSizeMode::Custom && Defaults->DesignTimeSize == FVector2D(320,120));
	Blueprint->GetPackage()->SetDirtyFlag(false);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPUMGPersistenceTest, "UnrealMCP.UMG.Persistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPUMGPersistenceTest::RunTest(const FString& Parameters)
{
	FString Path;
	if (!FParse::Value(FCommandLine::Get(), TEXT("UMGPersistencePath="), Path))
	{
		AddInfo(TEXT("Persistence requires a unique -UMGPersistencePath=/Game/__Dev/.../WBP_Test and a second process with -UMGPersistenceReload"));
		return true;
	}
	if (!TestTrue(TEXT("Persistence fixture restricted to __Dev"), Path.StartsWith(TEXT("/Game/__Dev/")) && FPackageName::IsValidLongPackageName(Path))) return false;
	const bool Reload = FParse::Param(FCommandLine::Get(), TEXT("UMGPersistenceReload"));
	UWidgetBlueprint* Blueprint = nullptr;
	FUnrealMCPUMGCommands Commands;
	if (!Reload)
	{
		if (!TestFalse(TEXT("Never overwrite an existing fixture"), UEditorAssetLibrary::DoesAssetExist(Path))) return false;
		Blueprint = MakeUMGFixture(Path);
		if (!TestNotNull(TEXT("Persistence fixture"), Blueprint)) return false;
		auto Tree = Blueprint->WidgetTree;
		auto Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root")); Tree->RootWidget = Root;
		auto Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("KeyText")); Root->AddChild(Text);
		Text->SetText(FText::FromString(TEXT("F"))); auto Font = Text->GetFont(); Font.Size = 37; Text->SetFont(Font);
		auto Image = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("KeyFrame")); Root->AddChild(Image);
		FSlateBrush Brush; Brush.SetResourceObject(UEditorAssetLibrary::LoadAsset(TEXT("/Engine/EngineResources/DefaultTexture"))); Image->SetBrush(Brush);
		auto Overlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("KeyOverlay")); Root->AddChild(Overlay);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		auto Move = MakeShared<FJsonObject>(); Move->SetStringField(TEXT("path"), Path); Move->SetStringField(TEXT("new_parent_name"), TEXT("KeyOverlay"));
		Move->SetBoolField(TEXT("dry_run"), false);
		auto Layout = MakeShared<FJsonObject>(); Layout->SetStringField(TEXT("HorizontalAlignment"), TEXT("HAlign_Center")); Layout->SetStringField(TEXT("VerticalAlignment"), TEXT("VAlign_Center"));
		Move->SetObjectField(TEXT("slot_properties"), Layout);
		for (const FString Name : {TEXT("KeyFrame"), TEXT("KeyText")})
		{
			Move->SetStringField(TEXT("widget_name"), Name); Move->SetNumberField(TEXT("insert_index"), Overlay->GetChildrenCount());
			if (!TestTrue(TEXT("Persistence move: ") + Name, Commands.HandleCommand(TEXT("reparent_widget"), Move)->GetBoolField(TEXT("success")))) return false;
		}
		auto Preview = MakeShared<FJsonObject>(); Preview->SetStringField(TEXT("path"), Path); Preview->SetStringField(TEXT("mode"), TEXT("Custom")); Preview->SetBoolField(TEXT("dry_run"), false);
		Preview->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(320), MakeShared<FJsonValueNumber>(120)});
		if (!TestTrue(TEXT("Persistence preview apply"), Commands.HandleCommand(TEXT("set_widget_preview_settings"), Preview)->GetBoolField(TEXT("success")))) return false;
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (!TestTrue(TEXT("Explicit save succeeds"), UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false))) return false;
	}
	else Blueprint = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
	if (!TestNotNull(TEXT("Persisted Blueprint available"), Blueprint)) return false;
	auto Text = Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(TEXT("KeyText")));
	auto Image = Cast<UImage>(Blueprint->WidgetTree->FindWidget(TEXT("KeyFrame")));
	auto Overlay = Cast<UOverlay>(Blueprint->WidgetTree->FindWidget(TEXT("KeyOverlay")));
	if (!TestNotNull(TEXT("Persisted text"), Text) || !TestNotNull(TEXT("Persisted image"), Image) || !TestNotNull(TEXT("Persisted parent"), Overlay)) return false;
	TestEqual(TEXT("Persisted child count"), Overlay->GetChildrenCount(), 2);
	TestEqual(TEXT("Persisted first image"), Overlay->GetChildAt(0), static_cast<UWidget*>(Image));
	TestEqual(TEXT("Persisted second text"), Overlay->GetChildAt(1), static_cast<UWidget*>(Text));
	auto Slot = Cast<UOverlaySlot>(Text->Slot);
	if (!TestNotNull(TEXT("Persisted OverlaySlot"), Slot)) return false;
	TestTrue(TEXT("Persisted centered slot"), Slot->GetHorizontalAlignment() == HAlign_Center && Slot->GetVerticalAlignment() == VAlign_Center);
	TestEqual(TEXT("Persisted font size"), Text->GetFont().Size, 37.f);
	TestEqual(TEXT("Persisted text value"), Text->GetText().ToString(), FString(TEXT("F")));
	TestEqual(TEXT("Persisted texture reference"), Image->GetBrush().GetResourceObject(), UEditorAssetLibrary::LoadAsset(TEXT("/Engine/EngineResources/DefaultTexture")));
	auto Read = MakeShared<FJsonObject>(); Read->SetStringField(TEXT("path"), Path);
	auto Settings = Commands.HandleCommand(TEXT("get_widget_preview_settings"), Read);
	if (!TestTrue(TEXT("Persisted preview readable"), Settings->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("Persisted Designer mode"), Settings->GetObjectField(TEXT("settings"))->GetStringField(TEXT("mode")), FString(TEXT("Custom")));
	const auto Size = Settings->GetObjectField(TEXT("settings"))->GetArrayField(TEXT("size"));
	TestTrue(TEXT("Persisted Designer dimensions"), Size.Num() == 2 && Size[0]->AsNumber() == 320 && Size[1]->AsNumber() == 120);
	TestFalse(TEXT("Persisted Blueprint clean"), Blueprint->GetPackage()->IsDirty());
	AddInfo(FString::Printf(TEXT("%s persistence fixture %s"), Reload ? TEXT("Cold-read") : TEXT("Wrote"), *Path));
	return !HasAnyErrors();
}
#endif

// Serialize widget-type-specific properties — safe, no UPropertyToJsonValue.
static TSharedPtr<FJsonObject> SerializeWidgetProperties(UWidget* Widget)
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	Props->SetBoolField(TEXT("is_visible"), Widget->IsVisible());

	// Names of properties we serialize with a structured, hand-written formatter below.
	// These get skipped by the generic fallback to avoid duplicated / uglier output.
	TSet<FName> Covered;

	if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		Props->SetStringField(TEXT("text"), TB->GetText().ToString());
		Props->SetArrayField(TEXT("color"), ColorToJsonArray(TB->GetColorAndOpacity().GetSpecifiedColor()));
		Covered.Add(TEXT("Text"));
		Covered.Add(TEXT("ColorAndOpacity"));
	}
	else if (UButton* Btn = Cast<UButton>(Widget))
	{
		Props->SetBoolField(TEXT("is_enabled"), Btn->GetIsEnabled());
	}
	else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
	{
		Props->SetNumberField(TEXT("percent"), PB->GetPercent());
		Props->SetArrayField(TEXT("fill_color"), ColorToJsonArray(PB->GetFillColorAndOpacity()));
		Covered.Add(TEXT("Percent"));
		Covered.Add(TEXT("FillColorAndOpacity"));
	}
	else if (UImage* Img = Cast<UImage>(Widget))
	{
		Props->SetArrayField(TEXT("tint_color"), ColorToJsonArray(Img->GetColorAndOpacity()));
		// Full structured brush: DrawAs, Margin (9-slice), ImageSize, Tiling, Mirroring,
		// resource path, RoundedBox outline settings if applicable.
		Props->SetObjectField(TEXT("brush"), BrushToJsonObject(Img->GetBrush()));
		// Back-compat: keep the flat texture_path some callers already rely on.
		if (UObject* Resource = Img->GetBrush().GetResourceObject())
		{
			Props->SetStringField(TEXT("texture_path"), Resource->GetPathName());
		}
		Covered.Add(TEXT("ColorAndOpacity"));
		Covered.Add(TEXT("Brush"));
	}
	else if (UBorder* Bdr = Cast<UBorder>(Widget))
	{
		Props->SetArrayField(TEXT("brush_color"), ColorToJsonArray(Bdr->GetBrushColor()));
		Props->SetObjectField(TEXT("background_brush"), BrushToJsonObject(Bdr->Background));
		Covered.Add(TEXT("BrushColor"));
		Covered.Add(TEXT("Background"));
	}
	else if (USizeBox* SB = Cast<USizeBox>(Widget))
	{
		// SizeBox has 4 pairs of (Is*Override, Get*) accessors that drive its layout.
		// Report the value only when the override is actually enabled, so the output
		// mirrors what a designer sees in the Details panel.
		auto MaybeSize = [&](const TCHAR* Key, bool bOverride, float Value)
		{
			if (bOverride)
			{
				Props->SetNumberField(Key, Value);
			}
		};
		MaybeSize(TEXT("width_override"), SB->IsWidthOverride(), SB->GetWidthOverride());
		MaybeSize(TEXT("height_override"), SB->IsHeightOverride(), SB->GetHeightOverride());
		MaybeSize(TEXT("min_desired_width"), SB->IsMinDesiredWidthOverride(), SB->GetMinDesiredWidth());
		MaybeSize(TEXT("max_desired_width"), SB->IsMaxDesiredWidthOverride(), SB->GetMaxDesiredWidth());
		MaybeSize(TEXT("min_desired_height"), SB->IsMinDesiredHeightOverride(), SB->GetMinDesiredHeight());
		MaybeSize(TEXT("max_desired_height"), SB->IsMaxDesiredHeightOverride(), SB->GetMaxDesiredHeight());
		MaybeSize(TEXT("min_aspect_ratio"), SB->IsMinAspectRatioOverride(), SB->GetMinAspectRatio());
		MaybeSize(TEXT("max_aspect_ratio"), SB->IsMaxAspectRatioOverride(), SB->GetMaxAspectRatio());
		// Skip the raw override flags/values in the generic pass to keep output clean.
		Covered.Append({
			TEXT("bOverride_WidthOverride"), TEXT("WidthOverride"),
			TEXT("bOverride_HeightOverride"), TEXT("HeightOverride"),
			TEXT("bOverride_MinDesiredWidth"), TEXT("MinDesiredWidth"),
			TEXT("bOverride_MaxDesiredWidth"), TEXT("MaxDesiredWidth"),
			TEXT("bOverride_MinDesiredHeight"), TEXT("MinDesiredHeight"),
			TEXT("bOverride_MaxDesiredHeight"), TEXT("MaxDesiredHeight"),
			TEXT("bOverride_MinAspectRatio"), TEXT("MinAspectRatio"),
			TEXT("bOverride_MaxAspectRatio"), TEXT("MaxAspectRatio"),
		});
	}
	else if (USpacer* Sp = Cast<USpacer>(Widget))
	{
		FVector2D SpSize = Sp->GetSize();
		TArray<TSharedPtr<FJsonValue>> SArr;
		SArr.Add(MakeShared<FJsonValueNumber>(SpSize.X));
		SArr.Add(MakeShared<FJsonValueNumber>(SpSize.Y));
		Props->SetArrayField(TEXT("size"), SArr);
		Covered.Add(TEXT("Size"));
	}

	// Generic fallback: dump every remaining CPF_Edit UPROPERTY declared on the
	// widget's concrete class (skipping UWidget base fields, transient/deprecated,
	// and anything the hand-written branch above already emitted). This covers
	// SafeZone, ScrollBox, BackgroundBlur, RichTextBlock, WidgetSwitcher,
	// CommonTextBlock's Style ref, and anything new the engine adds.
	AppendGenericWidgetProperties(Widget, Props, Covered);

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
	Node->SetObjectField(TEXT("placement"), WidgetPlacement(Widget));
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