#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionCustom.h"
#include "EditorAssetLibrary.h"
#include "Engine/Font.h"
#include "MaterialEditingLibrary.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"

FUnrealMCPMaterialCommands::FUnrealMCPMaterialCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("list_materials"))
    {
        return HandleListMaterials(Params);
    }
    else if (CommandType == TEXT("read_material"))
    {
        return HandleReadMaterial(Params);
    }
    else if (CommandType == TEXT("get_material_instance_parameters"))
    {
        return HandleGetMaterialInstanceParameters(Params);
    }
    else if (CommandType == TEXT("create_material"))
    {
        return HandleCreateMaterial(Params);
    }
    else if (CommandType == TEXT("add_material_expression"))
    {
        return HandleAddMaterialExpression(Params);
    }
    else if (CommandType == TEXT("set_material_expression_property"))
    {
        return HandleSetMaterialExpressionProperty(Params);
    }
    else if (CommandType == TEXT("connect_material_expressions"))
    {
        return HandleConnectMaterialExpressions(Params);
    }
    else if (CommandType == TEXT("connect_material_to_property"))
    {
        return HandleConnectMaterialToProperty(Params);
    }
    else if (CommandType == TEXT("add_custom_hlsl_expression"))
    {
        return HandleAddCustomHLSLExpression(Params);
    }
    else if (CommandType == TEXT("create_material_instance"))
    {
        return HandleCreateMaterialInstance(Params);
    }
    else if (CommandType == TEXT("add_material_comment"))
    {
        return HandleAddMaterialComment(Params);
    }
    else if (CommandType == TEXT("reset_material_node_layout"))
    {
        return HandleResetMaterialNodeLayout(Params);
    }
    else if (CommandType == TEXT("set_expression_position"))
    {
        return HandleSetExpressionPosition(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown material command: %s"), *CommandType));
}

// ============================================================================
// list_materials
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleListMaterials(const TSharedPtr<FJsonObject>& Params)
{
    // Parse parameters
    FString Path = TEXT("/Game");
    if (Params->HasField(TEXT("path")))
    {
        Path = Params->GetStringField(TEXT("path"));
    }

    bool bRecursive = true;
    if (Params->HasField(TEXT("recursive")))
    {
        bRecursive = Params->GetBoolField(TEXT("recursive"));
    }

    FString NameFilter;
    if (Params->HasField(TEXT("name_filter")))
    {
        NameFilter = Params->GetStringField(TEXT("name_filter"));
    }

    // Include materials, material instances, or both
    FString TypeFilter = TEXT("all"); // "material", "instance", "all"
    if (Params->HasField(TEXT("type")))
    {
        TypeFilter = Params->GetStringField(TEXT("type"));
    }

    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    TArray<TSharedPtr<FJsonValue>> MaterialArray;

    auto SearchForClass = [&](UClass* AssetClass, const FString& TypeName)
    {
        FARFilter Filter;
        Filter.ClassPaths.Add(AssetClass->GetClassPathName());
        Filter.bRecursiveClasses = true;
        Filter.bRecursivePaths = bRecursive;
        Filter.PackagePaths.Add(FName(*Path));

        TArray<FAssetData> AssetDataList;
        AssetRegistry.GetAssets(Filter, AssetDataList);

        for (const FAssetData& AssetData : AssetDataList)
        {
            FString AssetName = AssetData.AssetName.ToString();

            // Apply name filter
            if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter, ESearchCase::IgnoreCase))
            {
                continue;
            }

            TSharedPtr<FJsonObject> MatObj = MakeShareable(new FJsonObject);
            MatObj->SetStringField(TEXT("name"), AssetName);
            MatObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
            MatObj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
            MatObj->SetStringField(TEXT("type"), TypeName);

            MaterialArray.Add(MakeShareable(new FJsonValueObject(MatObj)));
        }
    };

    if (TypeFilter == TEXT("all") || TypeFilter == TEXT("material"))
    {
        SearchForClass(UMaterial::StaticClass(), TEXT("Material"));
    }
    if (TypeFilter == TEXT("all") || TypeFilter == TEXT("instance"))
    {
        SearchForClass(UMaterialInstanceConstant::StaticClass(), TEXT("MaterialInstance"));
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetNumberField(TEXT("count"), MaterialArray.Num());
    ResultJson->SetArrayField(TEXT("materials"), MaterialArray);
    return ResultJson;
}

// ============================================================================
// Helper: Serialize a material expression input connection
// ============================================================================
static TSharedPtr<FJsonObject> SerializeConnection(const FExpressionInput& Input, const TArray<TObjectPtr<UMaterialExpression>>& AllExpressions)
{
    if (!Input.Expression)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> ConnJson = MakeShareable(new FJsonObject);
    
    // Find the index of the connected expression in the array
    int32 ExprIndex = AllExpressions.IndexOfByKey(Input.Expression);
    ConnJson->SetNumberField(TEXT("node_index"), ExprIndex);
    ConnJson->SetStringField(TEXT("node_type"), Input.Expression->GetClass()->GetName());
    ConnJson->SetNumberField(TEXT("output_index"), Input.OutputIndex);

    // If the connected expression is a parameter, include its name
    if (UMaterialExpressionParameter* ParamExpr = Cast<UMaterialExpressionParameter>(Input.Expression))
    {
        ConnJson->SetStringField(TEXT("parameter_name"), ParamExpr->ParameterName.ToString());
    }

    return ConnJson;
}

// ============================================================================
// read_material
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleReadMaterial(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params->HasField(TEXT("name")) && !Params->HasField(TEXT("path")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'name' or 'path'"));
    }

    // Load the material asset
    UMaterial* Material = nullptr;

    if (Params->HasField(TEXT("path")))
    {
        FString AssetPath = Params->GetStringField(TEXT("path"));
        UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(AssetPath);
        Material = Cast<UMaterial>(LoadedObj);
        if (!Material)
        {
            // Maybe it's a MaterialInstance — tell the user
            if (Cast<UMaterialInstance>(LoadedObj))
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("'%s' is a MaterialInstance, not a Material. Use 'get_material_instance_parameters' instead."), *AssetPath));
            }
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found at path: %s"), *AssetPath));
        }
    }
    else
    {
        FString Name = Params->GetStringField(TEXT("name"));

        // Search via AssetRegistry
        IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        FARFilter Filter;
        Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        Filter.bRecursivePaths = true;
        Filter.PackagePaths.Add(FName(TEXT("/Game")));

        TArray<FAssetData> AssetDataList;
        AssetRegistry.GetAssets(Filter, AssetDataList);

        for (const FAssetData& AssetData : AssetDataList)
        {
            if (AssetData.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                UObject* LoadedObj = AssetData.GetAsset();
                Material = Cast<UMaterial>(LoadedObj);
                break;
            }
        }

        if (!Material)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *Name));
        }
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetStringField(TEXT("name"), Material->GetName());
    ResultJson->SetStringField(TEXT("path"), Material->GetPathName());

    // Material domain & blend mode
    ResultJson->SetStringField(TEXT("material_domain"), *UEnum::GetValueAsString(Material->MaterialDomain));
    ResultJson->SetStringField(TEXT("blend_mode"), *UEnum::GetValueAsString(Material->BlendMode));
    ResultJson->SetStringField(TEXT("shading_model"), *UEnum::GetValueAsString(Material->GetShadingModels().GetFirstShadingModel()));
    ResultJson->SetBoolField(TEXT("two_sided"), Material->IsTwoSided());

    // Get expressions
    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    if (!EditorData)
    {
        ResultJson->SetStringField(TEXT("warning"), TEXT("No editor-only data available"));
        return ResultJson;
    }

    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = EditorData->ExpressionCollection.Expressions;

    // -- Expression nodes --
    TArray<TSharedPtr<FJsonValue>> NodeArray;
    for (int32 i = 0; i < Expressions.Num(); ++i)
    {
        UMaterialExpression* Expr = Expressions[i];
        if (!Expr)
        {
            continue;
        }

        UE_LOG(LogTemp, Display, TEXT("ReadMaterial: Processing node %d / %d — %s"), i, Expressions.Num(), *Expr->GetClass()->GetName());

        TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
        NodeObj->SetNumberField(TEXT("index"), i);
        NodeObj->SetStringField(TEXT("type"), Expr->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("description"), Expr->GetDescription());
        NodeObj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
        NodeObj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);

        // User desc/comment
        if (!Expr->Desc.IsEmpty())
        {
            NodeObj->SetStringField(TEXT("comment"), Expr->Desc);
        }

        // Parameter info
        if (UMaterialExpressionParameter* ParamExpr = Cast<UMaterialExpressionParameter>(Expr))
        {
            NodeObj->SetStringField(TEXT("parameter_name"), ParamExpr->ParameterName.ToString());
            NodeObj->SetStringField(TEXT("group"), ParamExpr->Group.ToString());
        }

        // Scalar parameter default
        if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
        {
            NodeObj->SetNumberField(TEXT("default_value"), ScalarParam->DefaultValue);
        }

        // Vector parameter default
        if (UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr))
        {
            TSharedPtr<FJsonObject> ColorObj = MakeShareable(new FJsonObject);
            ColorObj->SetNumberField(TEXT("r"), VecParam->DefaultValue.R);
            ColorObj->SetNumberField(TEXT("g"), VecParam->DefaultValue.G);
            ColorObj->SetNumberField(TEXT("b"), VecParam->DefaultValue.B);
            ColorObj->SetNumberField(TEXT("a"), VecParam->DefaultValue.A);
            NodeObj->SetObjectField(TEXT("default_value"), ColorObj);
        }

        // Texture info
        if (UMaterialExpressionTextureBase* TexExpr = Cast<UMaterialExpressionTextureBase>(Expr))
        {
            if (TexExpr->Texture)
            {
                NodeObj->SetStringField(TEXT("texture"), TexExpr->Texture->GetPathName());
            }
        }

        // Input connections — use FExpressionInputIterator (UE 5.5+ recommended API)
        {
            TArray<TSharedPtr<FJsonValue>> InputArray;
            int32 SafetyCounter = 0;
            const int32 MaxInputs = 64; // safety limit
            for (FExpressionInputIterator It(Expr); It && SafetyCounter < MaxInputs; ++It, ++SafetyCounter)
            {
                FExpressionInput* Input = It.Input;
                int32 InputIndex = It.Index;
                TSharedPtr<FJsonObject> InputObj = MakeShareable(new FJsonObject);
                InputObj->SetStringField(TEXT("name"), Expr->GetInputName(InputIndex).ToString());

                if (Input)
                {
                    TSharedPtr<FJsonObject> ConnObj = SerializeConnection(*Input, Expressions);
                    if (ConnObj)
                    {
                        InputObj->SetObjectField(TEXT("connection"), ConnObj);
                    }
                    else
                    {
                        InputObj->SetField(TEXT("connection"), MakeShareable(new FJsonValueNull()));

                        // When no connection, try to read the inline default value (e.g. ConstA, ConstB)
                        FString InputName = Expr->GetInputName(InputIndex).ToString();
                        FString ConstPropName = FString::Printf(TEXT("Const%s"), *InputName);
                        FProperty* ConstProp = Expr->GetClass()->FindPropertyByName(FName(*ConstPropName));
                        if (ConstProp)
                        {
                            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(ConstProp))
                            {
                                InputObj->SetNumberField(TEXT("default_value"), FloatProp->GetPropertyValue_InContainer(Expr));
                            }
                            else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(ConstProp))
                            {
                                InputObj->SetNumberField(TEXT("default_value"), DoubleProp->GetPropertyValue_InContainer(Expr));
                            }
                        }
                    }
                }
                else
                {
                    InputObj->SetField(TEXT("connection"), MakeShareable(new FJsonValueNull()));
                }
                InputArray.Add(MakeShareable(new FJsonValueObject(InputObj)));
            }
            if (InputArray.Num() > 0)
            {
                NodeObj->SetArrayField(TEXT("inputs"), InputArray);
            }
        }

        // Output names
        TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
        if (Outputs.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> OutputArray;
            for (const FExpressionOutput& Output : Outputs)
            {
                OutputArray.Add(MakeShareable(new FJsonValueString(Output.OutputName.ToString())));
            }
            NodeObj->SetArrayField(TEXT("outputs"), OutputArray);
        }

        NodeArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
    }
    ResultJson->SetArrayField(TEXT("nodes"), NodeArray);
    ResultJson->SetNumberField(TEXT("node_count"), NodeArray.Num());

    // -- Material input pins (BaseColor, Metallic, etc.) --
    TSharedPtr<FJsonObject> PinsObj = MakeShareable(new FJsonObject);

    struct FMaterialPinInfo
    {
        const TCHAR* Name;
        const FExpressionInput& Input;
    };

    TArray<FMaterialPinInfo> Pins = {
        { TEXT("BaseColor"),            EditorData->BaseColor },
        { TEXT("Metallic"),             EditorData->Metallic },
        { TEXT("Specular"),             EditorData->Specular },
        { TEXT("Roughness"),            EditorData->Roughness },
        { TEXT("Anisotropy"),           EditorData->Anisotropy },
        { TEXT("Normal"),               EditorData->Normal },
        { TEXT("Tangent"),              EditorData->Tangent },
        { TEXT("EmissiveColor"),        EditorData->EmissiveColor },
        { TEXT("Opacity"),              EditorData->Opacity },
        { TEXT("OpacityMask"),          EditorData->OpacityMask },
        { TEXT("WorldPositionOffset"),  EditorData->WorldPositionOffset },
        { TEXT("SubsurfaceColor"),      EditorData->SubsurfaceColor },
        { TEXT("AmbientOcclusion"),     EditorData->AmbientOcclusion },
        { TEXT("Refraction"),           EditorData->Refraction },
        { TEXT("PixelDepthOffset"),     EditorData->PixelDepthOffset },
    };

    for (const FMaterialPinInfo& Pin : Pins)
    {
        TSharedPtr<FJsonObject> ConnObj = SerializeConnection(Pin.Input, Expressions);
        if (ConnObj)
        {
            PinsObj->SetObjectField(Pin.Name, ConnObj);
        }
    }

    ResultJson->SetObjectField(TEXT("material_inputs"), PinsObj);

    // -- Comment nodes --
    const TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = EditorData->ExpressionCollection.EditorComments;
    if (Comments.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> CommentArray;
        for (UMaterialExpressionComment* Comment : Comments)
        {
            if (!Comment) continue;
            TSharedPtr<FJsonObject> CommentObj = MakeShareable(new FJsonObject);
            CommentObj->SetStringField(TEXT("text"), Comment->Text);
            CommentObj->SetNumberField(TEXT("pos_x"), Comment->MaterialExpressionEditorX);
            CommentObj->SetNumberField(TEXT("pos_y"), Comment->MaterialExpressionEditorY);
            CommentObj->SetNumberField(TEXT("size_x"), Comment->SizeX);
            CommentObj->SetNumberField(TEXT("size_y"), Comment->SizeY);
            CommentArray.Add(MakeShareable(new FJsonValueObject(CommentObj)));
        }
        ResultJson->SetArrayField(TEXT("comments"), CommentArray);
    }

    return ResultJson;
}

// ============================================================================
// get_material_instance_parameters
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params->HasField(TEXT("name")) && !Params->HasField(TEXT("path")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'name' or 'path'"));
    }

    UMaterialInstanceConstant* MatInst = nullptr;

    if (Params->HasField(TEXT("path")))
    {
        FString AssetPath = Params->GetStringField(TEXT("path"));
        UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(AssetPath);
        MatInst = Cast<UMaterialInstanceConstant>(LoadedObj);
        if (!MatInst)
        {
            if (Cast<UMaterial>(LoadedObj))
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("'%s' is a Material, not a MaterialInstance. Use 'read_material' instead."), *AssetPath));
            }
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("MaterialInstance not found at path: %s"), *AssetPath));
        }
    }
    else
    {
        FString Name = Params->GetStringField(TEXT("name"));

        IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        FARFilter Filter;
        Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        Filter.bRecursivePaths = true;
        Filter.PackagePaths.Add(FName(TEXT("/Game")));

        TArray<FAssetData> AssetDataList;
        AssetRegistry.GetAssets(Filter, AssetDataList);

        for (const FAssetData& AssetData : AssetDataList)
        {
            if (AssetData.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                UObject* LoadedObj = AssetData.GetAsset();
                MatInst = Cast<UMaterialInstanceConstant>(LoadedObj);
                break;
            }
        }

        if (!MatInst)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("MaterialInstance not found: %s"), *Name));
        }
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetStringField(TEXT("name"), MatInst->GetName());
    ResultJson->SetStringField(TEXT("path"), MatInst->GetPathName());

    // Parent material
    if (MatInst->Parent)
    {
        ResultJson->SetStringField(TEXT("parent"), MatInst->Parent->GetPathName());
        ResultJson->SetStringField(TEXT("parent_name"), MatInst->Parent->GetName());
    }

    // Scalar parameters
    TArray<TSharedPtr<FJsonValue>> ScalarArray;
    for (const FScalarParameterValue& Param : MatInst->ScalarParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
        ParamObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
        ParamObj->SetNumberField(TEXT("value"), Param.ParameterValue);
        ScalarArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
    }
    ResultJson->SetArrayField(TEXT("scalar_parameters"), ScalarArray);

    // Vector parameters
    TArray<TSharedPtr<FJsonValue>> VectorArray;
    for (const FVectorParameterValue& Param : MatInst->VectorParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
        ParamObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
        TSharedPtr<FJsonObject> ColorObj = MakeShareable(new FJsonObject);
        ColorObj->SetNumberField(TEXT("r"), Param.ParameterValue.R);
        ColorObj->SetNumberField(TEXT("g"), Param.ParameterValue.G);
        ColorObj->SetNumberField(TEXT("b"), Param.ParameterValue.B);
        ColorObj->SetNumberField(TEXT("a"), Param.ParameterValue.A);
        ParamObj->SetObjectField(TEXT("value"), ColorObj);
        VectorArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
    }
    ResultJson->SetArrayField(TEXT("vector_parameters"), VectorArray);

    // Texture parameters
    TArray<TSharedPtr<FJsonValue>> TextureArray;
    for (const FTextureParameterValue& Param : MatInst->TextureParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
        ParamObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
        if (Param.ParameterValue)
        {
            ParamObj->SetStringField(TEXT("texture"), Param.ParameterValue->GetPathName());
            ParamObj->SetStringField(TEXT("texture_name"), Param.ParameterValue->GetName());
        }
        else
        {
            ParamObj->SetField(TEXT("texture"), MakeShareable(new FJsonValueNull()));
        }
        TextureArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
    }
    ResultJson->SetArrayField(TEXT("texture_parameters"), TextureArray);

    // Font parameters
    TArray<TSharedPtr<FJsonValue>> FontArray;
    for (const FFontParameterValue& Param : MatInst->FontParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
        ParamObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
        if (Param.FontValue)
        {
            ParamObj->SetStringField(TEXT("font"), Param.FontValue->GetPathName());
        }
        ParamObj->SetNumberField(TEXT("font_page"), Param.FontPage);
        FontArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
    }
    if (FontArray.Num() > 0)
    {
        ResultJson->SetArrayField(TEXT("font_parameters"), FontArray);
    }

    return ResultJson;
}

// ============================================================================
// Helper: Load a UMaterial from asset_path parameter
// ============================================================================
static UMaterial* LoadMaterialFromParams(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutError)
{
    if (!Params->HasField(TEXT("asset_path")))
    {
        OutError = FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'asset_path'"));
        return nullptr;
    }

    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(AssetPath);
    UMaterial* Material = Cast<UMaterial>(LoadedObj);
    if (!Material)
    {
        if (Cast<UMaterialInstance>(LoadedObj))
        {
            OutError = FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("'%s' is a MaterialInstance, not a Material. Material editing requires a base Material."), *AssetPath));
        }
        else
        {
            OutError = FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Material not found at path: %s"), *AssetPath));
        }
        return nullptr;
    }
    return Material;
}

// ============================================================================
// Helper: Get expression by node_index from a Material
// ============================================================================
static UMaterialExpression* GetExpressionByIndex(UMaterial* Material, int32 NodeIndex, TSharedPtr<FJsonObject>& OutError)
{
    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    if (!EditorData)
    {
        OutError = FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Material has no editor-only data"));
        return nullptr;
    }

    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = EditorData->ExpressionCollection.Expressions;
    if (NodeIndex < 0 || NodeIndex >= Expressions.Num())
    {
        OutError = FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("node_index %d out of range [0, %d)"), NodeIndex, Expressions.Num()));
        return nullptr;
    }

    UMaterialExpression* Expr = Expressions[NodeIndex];
    if (!Expr)
    {
        OutError = FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Expression at index %d is null"), NodeIndex));
        return nullptr;
    }
    return Expr;
}

// ============================================================================
// create_material
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params->HasField(TEXT("asset_path")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'asset_path'"));
    }

    FString AssetPath = Params->GetStringField(TEXT("asset_path"));

    // Check if asset already exists
    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists at path: %s"), *AssetPath));
    }

    // Extract package path and asset name from the full path
    FString PackagePath, AssetName;
    AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

    if (PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid asset_path format: %s. Expected /Game/Folder/AssetName"), *AssetPath));
    }

    // Create the Material using factory
    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    FString FullPackagePath = PackagePath + TEXT("/") + AssetName;
    UPackage* Package = CreatePackage(*FullPackagePath);
    if (!Package)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    UMaterial* NewMaterial = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), Package, FName(*AssetName),
        RF_Public | RF_Standalone, nullptr, GWarn));

    if (!NewMaterial)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create material"));
    }

    // Set optional properties
    if (Params->HasField(TEXT("blend_mode")))
    {
        FString BlendModeStr = Params->GetStringField(TEXT("blend_mode"));
        if (BlendModeStr == TEXT("Translucent")) NewMaterial->BlendMode = BLEND_Translucent;
        else if (BlendModeStr == TEXT("Additive")) NewMaterial->BlendMode = BLEND_Additive;
        else if (BlendModeStr == TEXT("Modulate")) NewMaterial->BlendMode = BLEND_Modulate;
        else if (BlendModeStr == TEXT("Masked")) NewMaterial->BlendMode = BLEND_Masked;
        // Opaque is default
    }

    if (Params->HasField(TEXT("shading_model")))
    {
        FString ShadingStr = Params->GetStringField(TEXT("shading_model"));
        if (ShadingStr == TEXT("Unlit")) NewMaterial->SetShadingModel(MSM_Unlit);
        else if (ShadingStr == TEXT("Subsurface")) NewMaterial->SetShadingModel(MSM_Subsurface);
        else if (ShadingStr == TEXT("ClearCoat")) NewMaterial->SetShadingModel(MSM_ClearCoat);
        // DefaultLit is default
    }

    if (Params->HasField(TEXT("two_sided")))
    {
        NewMaterial->TwoSided = Params->GetBoolField(TEXT("two_sided"));
    }

    if (Params->HasField(TEXT("material_domain")))
    {
        FString DomainStr = Params->GetStringField(TEXT("material_domain"));
        if (DomainStr == TEXT("PostProcess")) NewMaterial->MaterialDomain = MD_PostProcess;
        else if (DomainStr == TEXT("DeferredDecal")) NewMaterial->MaterialDomain = MD_DeferredDecal;
        else if (DomainStr == TEXT("LightFunction")) NewMaterial->MaterialDomain = MD_LightFunction;
        else if (DomainStr == TEXT("UI")) NewMaterial->MaterialDomain = MD_UI;
        // Surface is default
    }

    // Notify asset registry and mark dirty
    FAssetRegistryModule::AssetCreated(NewMaterial);
    NewMaterial->MarkPackageDirty();
    NewMaterial->PostEditChange();

    // Save the asset
    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetStringField(TEXT("name"), NewMaterial->GetName());
    ResultJson->SetStringField(TEXT("path"), NewMaterial->GetPathName());
    ResultJson->SetStringField(TEXT("blend_mode"), *UEnum::GetValueAsString(NewMaterial->BlendMode));
    ResultJson->SetStringField(TEXT("shading_model"), *UEnum::GetValueAsString(NewMaterial->GetShadingModels().GetFirstShadingModel()));
    ResultJson->SetBoolField(TEXT("two_sided"), NewMaterial->IsTwoSided());
    return ResultJson;
}

// ============================================================================
// add_material_expression
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    if (!Params->HasField(TEXT("expression_type")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'expression_type'"));
    }

    FString ExpressionType = Params->GetStringField(TEXT("expression_type"));

    // Resolve the class. The type can be: "Constant3Vector" or "MaterialExpressionConstant3Vector"
    FString ClassName = ExpressionType;
    if (!ClassName.StartsWith(TEXT("MaterialExpression")))
    {
        ClassName = TEXT("MaterialExpression") + ClassName;
    }

    UClass* ExprClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
    if (!ExprClass)
    {
        // Try with U prefix
        ExprClass = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None);
    }
    if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown expression type: %s (resolved to class: %s)"), *ExpressionType, *ClassName));
    }

    int32 PosX = 0, PosY = 0;
    if (Params->HasField(TEXT("pos_x"))) PosX = (int32)Params->GetNumberField(TEXT("pos_x"));
    if (Params->HasField(TEXT("pos_y"))) PosY = (int32)Params->GetNumberField(TEXT("pos_y"));

    UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExprClass, PosX, PosY);
    if (!NewExpr)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create expression of type: %s"), *ExpressionType));
    }

    // Find the index of the new expression
    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    int32 NodeIndex = EditorData ? EditorData->ExpressionCollection.Expressions.IndexOfByKey(NewExpr) : -1;

    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetNumberField(TEXT("node_index"), NodeIndex);
    ResultJson->SetStringField(TEXT("node_type"), NewExpr->GetClass()->GetName());
    ResultJson->SetStringField(TEXT("description"), NewExpr->GetDescription());
    ResultJson->SetNumberField(TEXT("pos_x"), NewExpr->MaterialExpressionEditorX);
    ResultJson->SetNumberField(TEXT("pos_y"), NewExpr->MaterialExpressionEditorY);
    return ResultJson;
}

// ============================================================================
// set_material_expression_property
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    if (!Params->HasField(TEXT("node_index")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'node_index'"));
    }
    if (!Params->HasField(TEXT("property_name")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'property_name'"));
    }
    if (!Params->HasField(TEXT("value")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'value'"));
    }

    int32 NodeIndex = (int32)Params->GetNumberField(TEXT("node_index"));
    FString PropertyName = Params->GetStringField(TEXT("property_name"));

    UMaterialExpression* Expr = GetExpressionByIndex(Material, NodeIndex, Error);
    if (!Expr) return Error;

    // Use FProperty reflection to find and set the property
    FProperty* Prop = Expr->GetClass()->FindPropertyByName(FName(*PropertyName));
    if (!Prop)
    {
        // Build a list of available properties for better error messages
        TArray<FString> AvailableProps;
        for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
            {
                AvailableProps.Add(It->GetName());
            }
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Property '%s' not found on %s. Available editable properties: %s"),
                *PropertyName, *Expr->GetClass()->GetName(),
                *FString::Join(AvailableProps, TEXT(", "))));
    }

    TSharedPtr<FJsonValue> ValueJson = Params->TryGetField(TEXT("value"));
    bool bSuccess = false;

    // Handle various property types
    if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
    {
        float Val = (float)ValueJson->AsNumber();
        FloatProp->SetPropertyValue_InContainer(Expr, Val);
        bSuccess = true;
    }
    else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
    {
        double Val = ValueJson->AsNumber();
        DoubleProp->SetPropertyValue_InContainer(Expr, Val);
        bSuccess = true;
    }
    else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
    {
        int32 Val = (int32)ValueJson->AsNumber();
        IntProp->SetPropertyValue_InContainer(Expr, Val);
        bSuccess = true;
    }
    else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
    {
        bool Val = ValueJson->AsBool();
        BoolProp->SetPropertyValue_InContainer(Expr, Val);
        bSuccess = true;
    }
    else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
    {
        FString Val = ValueJson->AsString();
        StrProp->SetPropertyValue_InContainer(Expr, Val);
        bSuccess = true;
    }
    else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
    {
        FName Val = FName(*ValueJson->AsString());
        NameProp->SetPropertyValue_InContainer(Expr, Val);
        bSuccess = true;
    }
    else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
    {
        void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Expr);
        FString StructName = StructProp->Struct->GetName();

        if (StructName == TEXT("LinearColor"))
        {
            FLinearColor* Color = (FLinearColor*)StructPtr;
            if (ValueJson->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> ColorObj = ValueJson->AsObject();
                Color->R = (float)ColorObj->GetNumberField(TEXT("r"));
                Color->G = (float)ColorObj->GetNumberField(TEXT("g"));
                Color->B = (float)ColorObj->GetNumberField(TEXT("b"));
                if (ColorObj->HasField(TEXT("a"))) Color->A = (float)ColorObj->GetNumberField(TEXT("a"));
            }
            bSuccess = true;
        }
        else if (StructName == TEXT("Color"))
        {
            FColor* Color = (FColor*)StructPtr;
            if (ValueJson->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> ColorObj = ValueJson->AsObject();
                Color->R = (uint8)ColorObj->GetNumberField(TEXT("r"));
                Color->G = (uint8)ColorObj->GetNumberField(TEXT("g"));
                Color->B = (uint8)ColorObj->GetNumberField(TEXT("b"));
                if (ColorObj->HasField(TEXT("a"))) Color->A = (uint8)ColorObj->GetNumberField(TEXT("a"));
            }
            bSuccess = true;
        }
        else if (StructName == TEXT("Vector") || StructName == TEXT("Vector3d"))
        {
            FVector* Vec = (FVector*)StructPtr;
            if (ValueJson->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> VecObj = ValueJson->AsObject();
                Vec->X = VecObj->GetNumberField(TEXT("x"));
                Vec->Y = VecObj->GetNumberField(TEXT("y"));
                Vec->Z = VecObj->GetNumberField(TEXT("z"));
            }
            bSuccess = true;
        }
        else if (StructName == TEXT("Vector4") || StructName == TEXT("Vector4d"))
        {
            FVector4* Vec = (FVector4*)StructPtr;
            if (ValueJson->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> VecObj = ValueJson->AsObject();
                Vec->X = VecObj->GetNumberField(TEXT("x"));
                Vec->Y = VecObj->GetNumberField(TEXT("y"));
                Vec->Z = VecObj->GetNumberField(TEXT("z"));
                if (VecObj->HasField(TEXT("w"))) Vec->W = VecObj->GetNumberField(TEXT("w"));
            }
            bSuccess = true;
        }
    }
    else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
    {
        // Handle enum by name string
        FString EnumValueStr = ValueJson->AsString();
        UEnum* EnumDef = EnumProp->GetEnum();
        int64 EnumVal = EnumDef->GetValueByNameString(EnumValueStr);
        if (EnumVal != INDEX_NONE)
        {
            FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
            void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Expr);
            UnderlyingProp->SetIntPropertyValue(ValuePtr, EnumVal);
            bSuccess = true;
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Invalid enum value '%s' for property '%s'"), *EnumValueStr, *PropertyName));
        }
    }
    else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
    {
        if (ByteProp->Enum)
        {
            FString EnumValueStr = ValueJson->AsString();
            int64 EnumVal = ByteProp->Enum->GetValueByNameString(EnumValueStr);
            if (EnumVal != INDEX_NONE)
            {
                ByteProp->SetPropertyValue_InContainer(Expr, (uint8)EnumVal);
                bSuccess = true;
            }
            else
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Invalid enum value '%s' for property '%s'"), *EnumValueStr, *PropertyName));
            }
        }
        else
        {
            ByteProp->SetPropertyValue_InContainer(Expr, (uint8)ValueJson->AsNumber());
            bSuccess = true;
        }
    }

    if (!bSuccess)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unsupported property type '%s' for property '%s'. Supported: float, double, int, bool, string, name, LinearColor, Color, Vector, Vector4, enum."),
                *Prop->GetCPPType(), *PropertyName));
    }

    Expr->PostEditChange();
    Material->PostEditChange();
    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("node_type"), Expr->GetClass()->GetName());
    ResultJson->SetNumberField(TEXT("node_index"), NodeIndex);
    ResultJson->SetStringField(TEXT("property_name"), PropertyName);
    return ResultJson;
}

// ============================================================================
// connect_material_expressions
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    if (!Params->HasField(TEXT("from_node_index")) || !Params->HasField(TEXT("to_node_index")) || !Params->HasField(TEXT("to_input_name")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Missing required parameters. Need: from_node_index, to_node_index, to_input_name"));
    }

    int32 FromIndex = (int32)Params->GetNumberField(TEXT("from_node_index"));
    int32 ToIndex = (int32)Params->GetNumberField(TEXT("to_node_index"));
    FString ToInputName = Params->GetStringField(TEXT("to_input_name"));

    FString FromOutputName;
    if (Params->HasField(TEXT("from_output_name")))
    {
        FromOutputName = Params->GetStringField(TEXT("from_output_name"));
    }

    UMaterialExpression* FromExpr = GetExpressionByIndex(Material, FromIndex, Error);
    if (!FromExpr) return Error;
    UMaterialExpression* ToExpr = GetExpressionByIndex(Material, ToIndex, Error);
    if (!ToExpr) return Error;

    bool bResult = UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpr, FromOutputName, ToExpr, ToInputName);
    if (!bResult)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to connect node %d (%s) output '%s' -> node %d (%s) input '%s'"),
                FromIndex, *FromExpr->GetClass()->GetName(), *FromOutputName,
                ToIndex, *ToExpr->GetClass()->GetName(), *ToInputName));
    }

    Material->PostEditChange();
    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("from_node"), FString::Printf(TEXT("%d (%s)"), FromIndex, *FromExpr->GetClass()->GetName()));
    ResultJson->SetStringField(TEXT("to_node"), FString::Printf(TEXT("%d (%s)"), ToIndex, *ToExpr->GetClass()->GetName()));
    ResultJson->SetStringField(TEXT("from_output"), FromOutputName.IsEmpty() ? TEXT("(default)") : FromOutputName);
    ResultJson->SetStringField(TEXT("to_input"), ToInputName);
    return ResultJson;
}

// ============================================================================
// connect_material_to_property
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleConnectMaterialToProperty(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    if (!Params->HasField(TEXT("node_index")) || !Params->HasField(TEXT("material_property")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Missing required parameters. Need: node_index, material_property"));
    }

    int32 NodeIndex = (int32)Params->GetNumberField(TEXT("node_index"));
    FString MaterialPropertyStr = Params->GetStringField(TEXT("material_property"));

    FString OutputName;
    if (Params->HasField(TEXT("output_name")))
    {
        OutputName = Params->GetStringField(TEXT("output_name"));
    }

    UMaterialExpression* Expr = GetExpressionByIndex(Material, NodeIndex, Error);
    if (!Expr) return Error;

    // Map string to EMaterialProperty
    EMaterialProperty MatProp;
    if (MaterialPropertyStr == TEXT("BaseColor")) MatProp = MP_BaseColor;
    else if (MaterialPropertyStr == TEXT("Metallic")) MatProp = MP_Metallic;
    else if (MaterialPropertyStr == TEXT("Specular")) MatProp = MP_Specular;
    else if (MaterialPropertyStr == TEXT("Roughness")) MatProp = MP_Roughness;
    else if (MaterialPropertyStr == TEXT("Anisotropy")) MatProp = MP_Anisotropy;
    else if (MaterialPropertyStr == TEXT("Normal")) MatProp = MP_Normal;
    else if (MaterialPropertyStr == TEXT("Tangent")) MatProp = MP_Tangent;
    else if (MaterialPropertyStr == TEXT("EmissiveColor")) MatProp = MP_EmissiveColor;
    else if (MaterialPropertyStr == TEXT("Opacity")) MatProp = MP_Opacity;
    else if (MaterialPropertyStr == TEXT("OpacityMask")) MatProp = MP_OpacityMask;
    else if (MaterialPropertyStr == TEXT("WorldPositionOffset")) MatProp = MP_WorldPositionOffset;
    else if (MaterialPropertyStr == TEXT("SubsurfaceColor")) MatProp = MP_SubsurfaceColor;
    else if (MaterialPropertyStr == TEXT("AmbientOcclusion")) MatProp = MP_AmbientOcclusion;
    else if (MaterialPropertyStr == TEXT("Refraction")) MatProp = MP_Refraction;
    else if (MaterialPropertyStr == TEXT("PixelDepthOffset")) MatProp = MP_PixelDepthOffset;
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown material_property: '%s'. Valid values: BaseColor, Metallic, Specular, Roughness, Anisotropy, Normal, Tangent, EmissiveColor, Opacity, OpacityMask, WorldPositionOffset, SubsurfaceColor, AmbientOcclusion, Refraction, PixelDepthOffset"),
                *MaterialPropertyStr));
    }

    bool bResult = UMaterialEditingLibrary::ConnectMaterialProperty(Expr, OutputName, MatProp);
    if (!bResult)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to connect node %d (%s) output '%s' -> material property '%s'"),
                NodeIndex, *Expr->GetClass()->GetName(), *OutputName, *MaterialPropertyStr));
    }

    Material->PostEditChange();
    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("node"), FString::Printf(TEXT("%d (%s)"), NodeIndex, *Expr->GetClass()->GetName()));
    ResultJson->SetStringField(TEXT("output"), OutputName.IsEmpty() ? TEXT("(default)") : OutputName);
    ResultJson->SetStringField(TEXT("material_property"), MaterialPropertyStr);
    return ResultJson;
}

// ============================================================================
// add_custom_hlsl_expression
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleAddCustomHLSLExpression(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    if (!Params->HasField(TEXT("code")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'code'"));
    }

    FString Code = Params->GetStringField(TEXT("code"));

    int32 PosX = 0, PosY = 0;
    if (Params->HasField(TEXT("pos_x"))) PosX = (int32)Params->GetNumberField(TEXT("pos_x"));
    if (Params->HasField(TEXT("pos_y"))) PosY = (int32)Params->GetNumberField(TEXT("pos_y"));

    UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(
        Material, UMaterialExpressionCustom::StaticClass(), PosX, PosY);
    if (!NewExpr)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Custom HLSL expression"));
    }

    UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(NewExpr);
    CustomExpr->Code = Code;

    // Set output type
    if (Params->HasField(TEXT("output_type")))
    {
        FString OutputTypeStr = Params->GetStringField(TEXT("output_type"));
        if (OutputTypeStr == TEXT("CMOT_Float1")) CustomExpr->OutputType = CMOT_Float1;
        else if (OutputTypeStr == TEXT("CMOT_Float2")) CustomExpr->OutputType = CMOT_Float2;
        else if (OutputTypeStr == TEXT("CMOT_Float3")) CustomExpr->OutputType = CMOT_Float3;
        else if (OutputTypeStr == TEXT("CMOT_Float4")) CustomExpr->OutputType = CMOT_Float4;
        else if (OutputTypeStr == TEXT("CMOT_MaterialAttributes")) CustomExpr->OutputType = CMOT_MaterialAttributes;
        // Default is CMOT_Float3
    }

    // Set description
    if (Params->HasField(TEXT("description")))
    {
        CustomExpr->Description = Params->GetStringField(TEXT("description"));
    }

    // Set input pins
    if (Params->HasField(TEXT("inputs")))
    {
        const TArray<TSharedPtr<FJsonValue>>& InputArray = Params->GetArrayField(TEXT("inputs"));
        CustomExpr->Inputs.Empty();
        for (const TSharedPtr<FJsonValue>& InputVal : InputArray)
        {
            FCustomInput NewInput;
            NewInput.InputName = FName(*InputVal->AsString());
            CustomExpr->Inputs.Add(NewInput);
        }
    }

    // Find the index of the new expression
    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    int32 NodeIndex = EditorData ? EditorData->ExpressionCollection.Expressions.IndexOfByKey(NewExpr) : -1;

    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetNumberField(TEXT("node_index"), NodeIndex);
    ResultJson->SetStringField(TEXT("node_type"), TEXT("MaterialExpressionCustom"));
    ResultJson->SetStringField(TEXT("description"), CustomExpr->Description);
    ResultJson->SetStringField(TEXT("code"), CustomExpr->Code);
    ResultJson->SetStringField(TEXT("output_type"), *UEnum::GetValueAsString(CustomExpr->OutputType));

    TArray<TSharedPtr<FJsonValue>> InputNames;
    for (const FCustomInput& Input : CustomExpr->Inputs)
    {
        InputNames.Add(MakeShareable(new FJsonValueString(Input.InputName.ToString())));
    }
    ResultJson->SetArrayField(TEXT("inputs"), InputNames);
    return ResultJson;
}

// ============================================================================
// create_material_instance
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params->HasField(TEXT("asset_path")) || !Params->HasField(TEXT("parent_material_path")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Missing required parameters: 'asset_path' and 'parent_material_path'"));
    }

    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FString ParentPath = Params->GetStringField(TEXT("parent_material_path"));

    // Check if asset already exists
    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists at path: %s"), *AssetPath));
    }

    // Load parent material
    UObject* ParentObj = UEditorAssetLibrary::LoadAsset(ParentPath);
    UMaterialInterface* ParentMaterial = Cast<UMaterialInterface>(ParentObj);
    if (!ParentMaterial)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Parent material not found at path: %s"), *ParentPath));
    }

    // Create the package
    FString PackagePath, AssetName;
    AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

    if (PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid asset_path format: %s"), *AssetPath));
    }

    FString FullPackagePath = PackagePath + TEXT("/") + AssetName;
    UPackage* Package = CreatePackage(*FullPackagePath);
    if (!Package)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    // Create MIC using factory
    UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    Factory->InitialParent = ParentMaterial;

    UMaterialInstanceConstant* NewInst = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
        UMaterialInstanceConstant::StaticClass(), Package, FName(*AssetName),
        RF_Public | RF_Standalone, nullptr, GWarn));

    if (!NewInst)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create material instance"));
    }

    // Set scalar parameter overrides
    if (Params->HasField(TEXT("scalar_params")))
    {
        TSharedPtr<FJsonObject> ScalarObj = Params->GetObjectField(TEXT("scalar_params"));
        for (const auto& Pair : ScalarObj->Values)
        {
            FName ParamName = FName(*Pair.Key);
            float Value = (float)Pair.Value->AsNumber();
            NewInst->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ParamName), Value);
        }
    }

    // Set vector parameter overrides
    if (Params->HasField(TEXT("vector_params")))
    {
        TSharedPtr<FJsonObject> VectorObj = Params->GetObjectField(TEXT("vector_params"));
        for (const auto& Pair : VectorObj->Values)
        {
            FName ParamName = FName(*Pair.Key);
            TSharedPtr<FJsonObject> ColorObj = Pair.Value->AsObject();
            if (ColorObj)
            {
                FLinearColor Color;
                Color.R = (float)ColorObj->GetNumberField(TEXT("r"));
                Color.G = (float)ColorObj->GetNumberField(TEXT("g"));
                Color.B = (float)ColorObj->GetNumberField(TEXT("b"));
                Color.A = ColorObj->HasField(TEXT("a")) ? (float)ColorObj->GetNumberField(TEXT("a")) : 1.0f;
                NewInst->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ParamName), Color);
            }
        }
    }

    FAssetRegistryModule::AssetCreated(NewInst);
    NewInst->MarkPackageDirty();
    NewInst->PostEditChange();

    // Save
    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);

    // Build response
    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetStringField(TEXT("name"), NewInst->GetName());
    ResultJson->SetStringField(TEXT("path"), NewInst->GetPathName());
    ResultJson->SetStringField(TEXT("parent"), ParentMaterial->GetPathName());

    // Return set parameters
    TArray<TSharedPtr<FJsonValue>> ScalarArray;
    for (const FScalarParameterValue& Param : NewInst->ScalarParameterValues)
    {
        TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
        P->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
        P->SetNumberField(TEXT("value"), Param.ParameterValue);
        ScalarArray.Add(MakeShareable(new FJsonValueObject(P)));
    }
    ResultJson->SetArrayField(TEXT("scalar_parameters"), ScalarArray);

    TArray<TSharedPtr<FJsonValue>> VectorArray;
    for (const FVectorParameterValue& Param : NewInst->VectorParameterValues)
    {
        TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
        P->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
        TSharedPtr<FJsonObject> C = MakeShareable(new FJsonObject);
        C->SetNumberField(TEXT("r"), Param.ParameterValue.R);
        C->SetNumberField(TEXT("g"), Param.ParameterValue.G);
        C->SetNumberField(TEXT("b"), Param.ParameterValue.B);
        C->SetNumberField(TEXT("a"), Param.ParameterValue.A);
        P->SetObjectField(TEXT("value"), C);
        VectorArray.Add(MakeShareable(new FJsonValueObject(P)));
    }
    ResultJson->SetArrayField(TEXT("vector_parameters"), VectorArray);

    return ResultJson;
}

// ============================================================================
// add_material_comment
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleAddMaterialComment(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    if (!Params->HasField(TEXT("text")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'text'"));
    }

    FString Text = Params->GetStringField(TEXT("text"));

    int32 PosX = 0, PosY = 0, SizeX = 400, SizeY = 300;
    if (Params->HasField(TEXT("pos_x"))) PosX = (int32)Params->GetNumberField(TEXT("pos_x"));
    if (Params->HasField(TEXT("pos_y"))) PosY = (int32)Params->GetNumberField(TEXT("pos_y"));
    if (Params->HasField(TEXT("size_x"))) SizeX = (int32)Params->GetNumberField(TEXT("size_x"));
    if (Params->HasField(TEXT("size_y"))) SizeY = (int32)Params->GetNumberField(TEXT("size_y"));

    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    if (!EditorData)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Material has no editor-only data"));
    }

    UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
    Comment->Text = Text;
    Comment->MaterialExpressionEditorX = PosX;
    Comment->MaterialExpressionEditorY = PosY;
    Comment->SizeX = SizeX;
    Comment->SizeY = SizeY;
    Comment->Material = Material;

    EditorData->ExpressionCollection.EditorComments.Add(Comment);

    Material->PostEditChange();
    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetStringField(TEXT("text"), Text);
    ResultJson->SetNumberField(TEXT("pos_x"), PosX);
    ResultJson->SetNumberField(TEXT("pos_y"), PosY);
    ResultJson->SetNumberField(TEXT("size_x"), SizeX);
    ResultJson->SetNumberField(TEXT("size_y"), SizeY);
    return ResultJson;
}

// ============================================================================
// reset_material_node_layout
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleResetMaterialNodeLayout(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    int32 SpacingX = 400;
    int32 SpacingY = 250;
    if (Params->HasField(TEXT("spacing_x"))) SpacingX = (int32)Params->GetNumberField(TEXT("spacing_x"));
    if (Params->HasField(TEXT("spacing_y"))) SpacingY = (int32)Params->GetNumberField(TEXT("spacing_y"));

    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    if (!EditorData)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Material has no editor-only data"));
    }

    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = EditorData->ExpressionCollection.Expressions;
    int32 NumExprs = Expressions.Num();

    if (NumExprs == 0)
    {
        TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetNumberField(TEXT("nodes_laid_out"), 0);
        return ResultJson;
    }

    // Build adjacency: for each expression, find which expressions feed into it
    // InputProviders[i] = set of expression indices that provide inputs to expression i
    // OutputConsumers[i] = set of expression indices that consume outputs from expression i
    TArray<TSet<int32>> InputProviders;
    TArray<TSet<int32>> OutputConsumers;
    InputProviders.SetNum(NumExprs);
    OutputConsumers.SetNum(NumExprs);

    for (int32 i = 0; i < NumExprs; ++i)
    {
        UMaterialExpression* Expr = Expressions[i];
        if (!Expr) continue;

        for (FExpressionInputIterator It(Expr); It; ++It)
        {
            FExpressionInput* Input = It.Input;
            if (Input && Input->Expression)
            {
                int32 ProviderIdx = Expressions.IndexOfByKey(Input->Expression);
                if (ProviderIdx != INDEX_NONE)
                {
                    InputProviders[i].Add(ProviderIdx);
                    OutputConsumers[ProviderIdx].Add(i);
                }
            }
        }
    }

    // ---- Row-based layout: each material property chain gets a horizontal row ----

    struct FMaterialPinRef
    {
        const TCHAR* Name;
        const FExpressionInput& Input;
    };
    TArray<FMaterialPinRef> MaterialPins = {
        { TEXT("BaseColor"),            EditorData->BaseColor },
        { TEXT("Metallic"),             EditorData->Metallic },
        { TEXT("Specular"),             EditorData->Specular },
        { TEXT("Roughness"),            EditorData->Roughness },
        { TEXT("Normal"),               EditorData->Normal },
        { TEXT("EmissiveColor"),        EditorData->EmissiveColor },
        { TEXT("Opacity"),              EditorData->Opacity },
        { TEXT("OpacityMask"),          EditorData->OpacityMask },
        { TEXT("WorldPositionOffset"),  EditorData->WorldPositionOffset },
        { TEXT("AmbientOcclusion"),     EditorData->AmbientOcclusion },
        { TEXT("Refraction"),           EditorData->Refraction },
        { TEXT("SubsurfaceColor"),      EditorData->SubsurfaceColor },
    };

    // Step 1: Assign each node to a row (one row per material property chain)
    TArray<int32> NodeRow;
    NodeRow.Init(-1, NumExprs);

    struct FRowInfo
    {
        FString PropertyName;
        TArray<int32> NodeIndices;
    };
    TArray<FRowInfo> Rows;

    for (const FMaterialPinRef& Pin : MaterialPins)
    {
        if (!Pin.Input.Expression) continue;
        int32 RootIdx = Expressions.IndexOfByKey(Pin.Input.Expression);
        if (RootIdx == INDEX_NONE) continue;
        if (NodeRow[RootIdx] != -1) continue; // already claimed by another property

        int32 RowIdx = Rows.Num();
        FRowInfo NewRow;
        NewRow.PropertyName = Pin.Name;

        // BFS backward to claim unclaimed nodes for this row
        TArray<int32> BFSQueue;
        TSet<int32> ChainVisited;

        NodeRow[RootIdx] = RowIdx;
        NewRow.NodeIndices.Add(RootIdx);
        BFSQueue.Add(RootIdx);
        ChainVisited.Add(RootIdx);

        int32 Head = 0;
        while (Head < BFSQueue.Num())
        {
            int32 Current = BFSQueue[Head++];
            for (int32 Provider : InputProviders[Current])
            {
                if (!ChainVisited.Contains(Provider))
                {
                    ChainVisited.Add(Provider);
                    if (NodeRow[Provider] == -1)
                    {
                        NodeRow[Provider] = RowIdx;
                        NewRow.NodeIndices.Add(Provider);
                    }
                    BFSQueue.Add(Provider); // always traverse to discover full upstream
                }
            }
        }

        Rows.Add(MoveTemp(NewRow));
    }

    // Handle disconnected nodes
    {
        FRowInfo DisconnectedRow;
        DisconnectedRow.PropertyName = TEXT("Disconnected");
        for (int32 i = 0; i < NumExprs; ++i)
        {
            if (NodeRow[i] == -1 && Expressions[i])
            {
                NodeRow[i] = Rows.Num();
                DisconnectedRow.NodeIndices.Add(i);
            }
        }
        if (DisconnectedRow.NodeIndices.Num() > 0)
        {
            Rows.Add(MoveTemp(DisconnectedRow));
        }
    }

    // Step 2: Compute per-row depth (longest path from row root within the row)
    TArray<int32> NodeDepth;
    NodeDepth.Init(0, NumExprs);

    for (int32 RowIdx = 0; RowIdx < Rows.Num(); ++RowIdx)
    {
        const TArray<int32>& NodesInRow = Rows[RowIdx].NodeIndices;
        if (NodesInRow.Num() == 0) continue;

        // Root = first node (directly connected to material property)
        int32 RootIdx = NodesInRow[0];
        NodeDepth[RootIdx] = 0;

        // BFS with longest-path relaxation, only within same row
        TArray<int32> DepthQueue;
        DepthQueue.Add(RootIdx);
        int32 Head = 0;
        while (Head < DepthQueue.Num())
        {
            int32 Current = DepthQueue[Head++];
            int32 CurDepth = NodeDepth[Current];
            for (int32 Provider : InputProviders[Current])
            {
                if (NodeRow[Provider] == RowIdx && NodeDepth[Provider] < CurDepth + 1)
                {
                    NodeDepth[Provider] = CurDepth + 1;
                    DepthQueue.Add(Provider); // re-enqueue for propagation
                }
            }
        }
    }

    // Step 3: Position nodes — each row is a horizontal band, depth = X column
    int32 CurrentY = 0;
    int32 RowGap = SpacingY / 2; // extra gap between property rows

    for (int32 RowIdx = 0; RowIdx < Rows.Num(); ++RowIdx)
    {
        const TArray<int32>& NodesInRow = Rows[RowIdx].NodeIndices;
        if (NodesInRow.Num() == 0) continue;

        // Group by depth
        TMap<int32, TArray<int32>> DepthGroups;
        for (int32 NodeIdx : NodesInRow)
        {
            DepthGroups.FindOrAdd(NodeDepth[NodeIdx]).Add(NodeIdx);
        }

        // Sort each depth group by expression index for determinism
        for (auto& Pair : DepthGroups)
        {
            Pair.Value.Sort();
        }

        // Max stacked nodes at any single depth = row height multiplier
        int32 MaxStack = 1;
        for (auto& Pair : DepthGroups)
        {
            MaxStack = FMath::Max(MaxStack, Pair.Value.Num());
        }

        int32 RowHeight = (MaxStack - 1) * SpacingY;

        // Position each depth group
        for (auto& Pair : DepthGroups)
        {
            int32 Depth = Pair.Key;
            const TArray<int32>& Group = Pair.Value;
            int32 X = -Depth * SpacingX;

            // Center group vertically within row band
            int32 GroupHeight = (Group.Num() - 1) * SpacingY;
            int32 GroupStartY = CurrentY + (RowHeight - GroupHeight) / 2;

            for (int32 Slot = 0; Slot < Group.Num(); ++Slot)
            {
                UMaterialExpression* Expr = Expressions[Group[Slot]];
                if (Expr)
                {
                    Expr->MaterialExpressionEditorX = X;
                    Expr->MaterialExpressionEditorY = GroupStartY + Slot * SpacingY;
                }
            }
        }

        CurrentY += RowHeight + SpacingY + RowGap;
    }

    // Position the material output node to the right of all expression nodes, vertically centered
    int32 TotalHeight = FMath::Max(0, CurrentY - SpacingY - RowGap);
    Material->EditorX = SpacingX;
    Material->EditorY = TotalHeight / 2 - 200; // offset up slightly since the output node is tall

    Material->PostEditChange();
    Material->MarkPackageDirty();

    // Build response with row-based layout info
    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetNumberField(TEXT("nodes_laid_out"), NumExprs);
    ResultJson->SetNumberField(TEXT("rows"), Rows.Num());
    ResultJson->SetNumberField(TEXT("output_node_x"), Material->EditorX);
    ResultJson->SetNumberField(TEXT("output_node_y"), Material->EditorY);

    TArray<TSharedPtr<FJsonValue>> RowArray;
    for (int32 RowIdx = 0; RowIdx < Rows.Num(); ++RowIdx)
    {
        TSharedPtr<FJsonObject> RowObj = MakeShareable(new FJsonObject);
        RowObj->SetStringField(TEXT("property"), Rows[RowIdx].PropertyName);

        TArray<TSharedPtr<FJsonValue>> NodesArr;
        for (int32 NodeIdx : Rows[RowIdx].NodeIndices)
        {
            UMaterialExpression* Expr = Expressions[NodeIdx];
            if (!Expr) continue;
            TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
            NodeObj->SetNumberField(TEXT("index"), NodeIdx);
            NodeObj->SetStringField(TEXT("type"), Expr->GetClass()->GetName());
            NodeObj->SetNumberField(TEXT("depth"), NodeDepth[NodeIdx]);
            NodeObj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
            NodeObj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
            NodesArr.Add(MakeShareable(new FJsonValueObject(NodeObj)));
        }
        RowObj->SetArrayField(TEXT("nodes"), NodesArr);
        RowArray.Add(MakeShareable(new FJsonValueObject(RowObj)));
    }
    ResultJson->SetArrayField(TEXT("layout"), RowArray);

    return ResultJson;
}

// ============================================================================
// set_expression_position
// ============================================================================
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetExpressionPosition(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    UMaterial* Material = LoadMaterialFromParams(Params, Error);
    if (!Material) return Error;

    UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
    if (!EditorData)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Material has no editor-only data"));
    }

    int32 NodeIndex = (int32)Params->GetNumberField(TEXT("node_index"));
    int32 PosX = (int32)Params->GetNumberField(TEXT("pos_x"));
    int32 PosY = (int32)Params->GetNumberField(TEXT("pos_y"));

    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = EditorData->ExpressionCollection.Expressions;
    UMaterialExpression* Expr = GetExpressionByIndex(Material, NodeIndex, Error);
    if (!Expr) return Error;

    Expr->MaterialExpressionEditorX = PosX;
    Expr->MaterialExpressionEditorY = PosY;

    Material->PostEditChange();
    Material->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
    ResultJson->SetBoolField(TEXT("success"), true);
    ResultJson->SetNumberField(TEXT("node_index"), NodeIndex);
    ResultJson->SetStringField(TEXT("type"), Expr->GetClass()->GetName());
    ResultJson->SetNumberField(TEXT("pos_x"), PosX);
    ResultJson->SetNumberField(TEXT("pos_y"), PosY);
    return ResultJson;
}
