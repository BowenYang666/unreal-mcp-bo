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
#include "EditorAssetLibrary.h"
#include "Engine/Font.h"

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
            for (FExpressionInputIterator It(Expr); It; ++It)
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
