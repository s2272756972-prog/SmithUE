// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEMaterialCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialShared.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialEditorUtilities.h"
#include "IMaterialEditor.h"
#include "Toolkits/ToolkitManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstance.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    /**
     * Load a material for editing. If the Material Editor is open for this asset,
     * returns the editor's working COPY (which has a valid MaterialGraph for live UI refresh).
     * Otherwise returns the original asset.
     *
     * @param OutEditor  If non-null and editor is open, receives the IMaterialEditor ptr.
     */
    UMaterial* LoadMaterialForEditing(const FString& MaterialPath, TSharedPtr<IMaterialEditor>* OutEditor = nullptr)
    {
        UMaterial* Original = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (!Original) return nullptr;

        TSharedPtr<IToolkit> Found = FToolkitManager::Get().FindEditorForAsset(Original);
        if (Found.IsValid())
        {
            TSharedPtr<IMaterialEditor> MatEditor = StaticCastSharedPtr<IMaterialEditor>(Found);
            if (MatEditor.IsValid())
            {
                UMaterial* EditMat = Cast<UMaterial>(MatEditor->GetMaterialInterface());
                if (EditMat)
                {
                    if (OutEditor) *OutEditor = MatEditor;
                    return EditMat;
                }
            }
        }

        return Original;
    }

    /**
     * Notify the editor that a material has been modified programmatically.
     * When an editor is active (working on the copy), rebuilds graph and refreshes UI.
     * When no editor is active (working on original), triggers standard change notification.
     */
    void NotifyMaterialModified(UMaterial* Material, TSharedPtr<IMaterialEditor> MatEditor = nullptr)
    {
        if (MatEditor.IsValid())
        {
            // Working on the editor's copy which has a valid MaterialGraph
            if (Material->MaterialGraph)
            {
                Material->MaterialGraph->RebuildGraph();
            }
            MatEditor->UpdateMaterialAfterGraphChange();
            UE_LOG(LogSmithUE, Log, TEXT("NotifyMaterialModified: Editor graph refreshed for %s"), *Material->GetName());
        }
        else
        {
            // Working on the original asset - no editor open
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material->MarkPackageDirty();
            UE_LOG(LogSmithUE, Verbose, TEXT("NotifyMaterialModified: No editor open for %s"), *Material->GetName());
        }
    }

    /** Resolve a short or full expression class name to a UClass. */
    UClass* ResolveExpressionClass(const FString& ClassName)
    {
        // Build candidate names
        TArray<FString> Candidates;
        Candidates.Add(ClassName);
        Candidates.Add(TEXT("U") + ClassName);
        Candidates.Add(TEXT("UMaterialExpression") + ClassName);
        Candidates.Add(TEXT("MaterialExpression") + ClassName);

        for (const FString& Name : Candidates)
        {
            // Use FindFirstObject which searches all packages (UE5.2 compatible)
            UClass* Found = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
            if (Found && Found->IsChildOf(UMaterialExpression::StaticClass()))
            {
                return Found;
            }
        }

        return nullptr;
    }

    /** Map dest_input_index to a material base input. Returns nullptr if out of range. */
    FExpressionInput* GetMaterialBaseInput(UMaterial* Material, int32 InputIndex)
    {
        if (!Material || !Material->GetEditorOnlyData())
        {
            return nullptr;
        }

        switch (InputIndex)
        {
        case 0: return &Material->GetEditorOnlyData()->BaseColor;
        case 1: return &Material->GetEditorOnlyData()->Metallic;
        case 2: return &Material->GetEditorOnlyData()->Roughness;
        case 3: return &Material->GetEditorOnlyData()->Normal;
        case 4: return &Material->GetEditorOnlyData()->EmissiveColor;
        default: return nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEMaterialCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // create_material
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_material"),
            TEXT("Material"),
            TEXT("Create a new UMaterial asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path, e.g. /Game/Materials"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleCreateMaterial(Params);
        });

    // get_material_info
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_material_info"),
            TEXT("Material"),
            TEXT("Get information about a material including its expressions"),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path, e.g. /Game/Materials/M_Test"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleGetMaterialInfo(Params);
        });

    // add_material_expression
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_material_expression"),
            TEXT("Material"),
            TEXT("Add a material expression node to a material"),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("expression_class"), TEXT("string"), TEXT("Expression class name, e.g. MaterialExpressionConstant3Vector"), true),
                FSmithUEToolParam(TEXT("position_x"), TEXT("number"), TEXT("Editor X position"), false, TEXT("0")),
                FSmithUEToolParam(TEXT("position_y"), TEXT("number"), TEXT("Editor Y position"), false, TEXT("0"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleAddMaterialExpression(Params);
        });

    // connect_material_pins
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("connect_material_pins"),
            TEXT("Material"),
            TEXT("Connect material expression pins. Use dest_expression_index=-1 to connect to material output."),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("source_expression_index"), TEXT("number"), TEXT("Index of source expression in Expressions array"), true),
                FSmithUEToolParam(TEXT("source_output_index"), TEXT("number"), TEXT("Output pin index on source expression"), false, TEXT("0")),
                FSmithUEToolParam(TEXT("dest_expression_index"), TEXT("number"), TEXT("Index of dest expression, or -1 for material output"), true),
                FSmithUEToolParam(TEXT("dest_input_index"), TEXT("number"), TEXT("Input pin index. For material output: 0=BaseColor,1=Metallic,2=Roughness,3=Normal,4=Emissive"), false, TEXT("0"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleConnectMaterialPins(Params);
        });

    // compile_material
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("compile_material"),
            TEXT("Material"),
            TEXT("Trigger material recompilation"),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleCompileMaterial(Params);
        });

    // set_material_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_material_property"),
            TEXT("Material"),
            TEXT("Set material properties (domain, blend_mode, shading_model, two_sided, blendable_location)"),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("domain"), TEXT("string"), TEXT("Material domain: surface, deferred_decal, light_function, volume, post_process, ui")),
                FSmithUEToolParam(TEXT("blend_mode"), TEXT("string"), TEXT("Blend mode: opaque, masked, translucent, additive, modulate, alpha_composite, alpha_holdout")),
                FSmithUEToolParam(TEXT("shading_model"), TEXT("string"), TEXT("Shading model: unlit, default_lit, subsurface, clear_coat, etc.")),
                FSmithUEToolParam(TEXT("two_sided"), TEXT("bool"), TEXT("Enable two-sided rendering")),
                FSmithUEToolParam(TEXT("blendable_location"), TEXT("string"), TEXT("For PostProcess: before_tonemapping, after_tonemapping, before_translucency, replacing_tonemapper, ssr_input"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleSetMaterialProperty(Params);
        });

    // set_expression_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_expression_property"),
            TEXT("Material"),
            TEXT("Set properties on a material expression node (e.g. Custom HLSL code, constant values, texture)"),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("expression_index"), TEXT("number"), TEXT("Index of expression in Expressions array"), true),
                FSmithUEToolParam(TEXT("properties"), TEXT("object"), TEXT("Key-value pairs to set. For Custom: code, output_type(float/float2/float3/float4), description, inputs(array of {name,type}). For MaterialFunctionCall: material_function(asset path)"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleSetExpressionProperty(Params);
        });

    // create_mpc
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_mpc"),
            TEXT("Material"),
            TEXT("Create a new Material Parameter Collection asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path, e.g. /Game/Materials"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleCreateMPC(Params);
        });

    // add_mpc_scalar
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_mpc_scalar"),
            TEXT("Material"),
            TEXT("Add a scalar parameter to a Material Parameter Collection"),
            {
                FSmithUEToolParam(TEXT("mpc_path"), TEXT("string"), TEXT("Full asset path to the MPC"), true),
                FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter name"), true),
                FSmithUEToolParam(TEXT("default_value"), TEXT("number"), TEXT("Default scalar value (default: 0.0)"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleAddMPCScalar(Params);
        });

    // add_mpc_vector
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_mpc_vector"),
            TEXT("Material"),
            TEXT("Add a vector parameter to a Material Parameter Collection"),
            {
                FSmithUEToolParam(TEXT("mpc_path"), TEXT("string"), TEXT("Full asset path to the MPC"), true),
                FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter name"), true),
                FSmithUEToolParam(TEXT("default_r"), TEXT("number"), TEXT("Default R value (default: 0.0)")),
                FSmithUEToolParam(TEXT("default_g"), TEXT("number"), TEXT("Default G value (default: 0.0)")),
                FSmithUEToolParam(TEXT("default_b"), TEXT("number"), TEXT("Default B value (default: 0.0)")),
                FSmithUEToolParam(TEXT("default_a"), TEXT("number"), TEXT("Default A value (default: 1.0)"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleAddMPCVector(Params);
        });

    // set_mpc_value
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_mpc_value"),
            TEXT("Material"),
            TEXT("Update the default value of a scalar parameter in a Material Parameter Collection"),
            {
                FSmithUEToolParam(TEXT("mpc_path"), TEXT("string"), TEXT("Full asset path to the MPC"), true),
                FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter name"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("New scalar value as float string"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleSetMPCValue(Params);
        });

    // create_material_instance
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_material_instance"),
            TEXT("Material"),
            TEXT("Create a new MaterialInstanceConstant asset from a parent material"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path, e.g. /Game/Materials"), true),
                FSmithUEToolParam(TEXT("parent_material"), TEXT("string"), TEXT("Full asset path of the parent UMaterial or UMaterialInstance"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleCreateMaterialInstance(Params);
        });

    // set_mi_scalar
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_mi_scalar"),
            TEXT("Material"),
            TEXT("Set a scalar parameter override on a MaterialInstanceConstant"),
            {
                FSmithUEToolParam(TEXT("mi_path"), TEXT("string"), TEXT("Full asset path to the MaterialInstanceConstant"), true),
                FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Scalar parameter name"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("number"), TEXT("Scalar value to set"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleSetMIScalar(Params);
        });

    // set_mi_vector
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_mi_vector"),
            TEXT("Material"),
            TEXT("Set a vector parameter override on a MaterialInstanceConstant"),
            {
                FSmithUEToolParam(TEXT("mi_path"), TEXT("string"), TEXT("Full asset path to the MaterialInstanceConstant"), true),
                FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Vector parameter name"), true),
                FSmithUEToolParam(TEXT("r"), TEXT("number"), TEXT("Red channel (default: 0.0)")),
                FSmithUEToolParam(TEXT("g"), TEXT("number"), TEXT("Green channel (default: 0.0)")),
                FSmithUEToolParam(TEXT("b"), TEXT("number"), TEXT("Blue channel (default: 0.0)")),
                FSmithUEToolParam(TEXT("a"), TEXT("number"), TEXT("Alpha channel (default: 1.0)"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleSetMIVector(Params);
        });

    // get_mi_info
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_mi_info"),
            TEXT("Material"),
            TEXT("Get info about a MaterialInstanceConstant: parent name and all scalar/vector parameter overrides"),
            {
                FSmithUEToolParam(TEXT("mi_path"), TEXT("string"), TEXT("Full asset path to the MaterialInstanceConstant"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleGetMIInfo(Params);
        });
}

// ---------------------------------------------------------------------------
// Command Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialName;
    if (!Params->TryGetStringField(TEXT("name"), MaterialName) || MaterialName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: name"));
    }

    FString FolderPath;
    if (!Params->TryGetStringField(TEXT("path"), FolderPath) || FolderPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: path"));
    }

    const FString FullPath = FolderPath / MaterialName;

    if (UEditorAssetLibrary::DoesAssetExist(FullPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material already exists: %s"), *FullPath));
    }

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    UMaterial* NewMaterial = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(),
        Package,
        *MaterialName,
        RF_Standalone | RF_Public,
        nullptr,
        GWarn));

    if (!NewMaterial)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create material"));
    }

    FAssetRegistryModule::AssetCreated(NewMaterial);
    Package->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), MaterialName);
    Data->SetStringField(TEXT("material_path"), FullPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    UMaterial* Material = LoadMaterialForEditing(MaterialPath);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
    int32 NumTextures = 0;

    for (int32 i = 0; i < Material->GetExpressions().Num(); ++i)
    {
        UMaterialExpression* Expr = Material->GetExpressions()[i];
        if (!Expr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
        ExprObj->SetNumberField(TEXT("index"), i);
        ExprObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
        ExprObj->SetStringField(TEXT("name"), Expr->GetName());
        ExprObj->SetStringField(TEXT("desc"), Expr->Desc);

        TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
        PosObj->SetNumberField(TEXT("x"), Expr->MaterialExpressionEditorX);
        PosObj->SetNumberField(TEXT("y"), Expr->MaterialExpressionEditorY);
        ExprObj->SetObjectField(TEXT("position"), PosObj);

        ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));

        if (Expr->IsA<UMaterialExpressionTextureSample>())
        {
            ++NumTextures;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Material->GetName());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetNumberField(TEXT("expression_count"), Material->GetExpressions().Num());
    Data->SetNumberField(TEXT("num_textures"), NumTextures);
    Data->SetArrayField(TEXT("expressions"), ExpressionsArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    FString ExpressionClassName;
    if (!Params->TryGetStringField(TEXT("expression_class"), ExpressionClassName) || ExpressionClassName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: expression_class"));
    }

    TSharedPtr<IMaterialEditor> ActiveEditor;
    UMaterial* Material = LoadMaterialForEditing(MaterialPath, &ActiveEditor);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    UClass* ExprClass = ResolveExpressionClass(ExpressionClassName);
    if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown expression class: %s"), *ExpressionClassName));
    }

    double PosX = 0.0, PosY = 0.0;
    Params->TryGetNumberField(TEXT("position_x"), PosX);
    Params->TryGetNumberField(TEXT("position_y"), PosY);

    UMaterialExpression* NewExpr = NewObject<UMaterialExpression>(Material, ExprClass);
    if (!NewExpr)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create expression object"));
    }

    NewExpr->Material = Material;
    NewExpr->MaterialExpressionEditorX = static_cast<int32>(PosX);
    NewExpr->MaterialExpressionEditorY = static_cast<int32>(PosY);

    // UE 5.2: use mutable expression collection for writes
    const int32 NewIndex = Material->GetExpressionCollection().Expressions.Add(NewExpr);

    NotifyMaterialModified(Material, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("expression_index"), NewIndex);
    Data->SetStringField(TEXT("class"), ExprClass->GetName());
    Data->SetStringField(TEXT("name"), NewExpr->GetName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleConnectMaterialPins(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    double SourceIndexD = -1.0, DestIndexD = -2.0;
    if (!Params->TryGetNumberField(TEXT("source_expression_index"), SourceIndexD))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: source_expression_index"));
    }
    if (!Params->TryGetNumberField(TEXT("dest_expression_index"), DestIndexD))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: dest_expression_index"));
    }

    const int32 SourceIndex = static_cast<int32>(SourceIndexD);
    const int32 DestIndex = static_cast<int32>(DestIndexD);

    double SourceOutputD = 0.0, DestInputD = 0.0;
    Params->TryGetNumberField(TEXT("source_output_index"), SourceOutputD);
    Params->TryGetNumberField(TEXT("dest_input_index"), DestInputD);
    const int32 SourceOutputIndex = static_cast<int32>(SourceOutputD);
    const int32 DestInputIndex = static_cast<int32>(DestInputD);

    TSharedPtr<IMaterialEditor> ActiveEditor;
    UMaterial* Material = LoadMaterialForEditing(MaterialPath, &ActiveEditor);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    if (SourceIndex < 0 || SourceIndex >= Material->GetExpressions().Num())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("source_expression_index %d out of range (0..%d)"),
                SourceIndex, Material->GetExpressions().Num() - 1));
    }

    UMaterialExpression* SourceExpr = Material->GetExpressions()[SourceIndex];
    if (!SourceExpr)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source expression at index %d is null"), SourceIndex));
    }

    if (DestIndex == -1)
    {
        // Connect to material base output
        FExpressionInput* Input = GetMaterialBaseInput(Material, DestInputIndex);
        if (!Input)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("dest_input_index %d is out of range for material output (0-4)"), DestInputIndex));
        }
        Input->Connect(SourceOutputIndex, SourceExpr);
    }
    else
    {
        if (DestIndex < 0 || DestIndex >= Material->GetExpressions().Num())
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("dest_expression_index %d out of range (0..%d)"),
                    DestIndex, Material->GetExpressions().Num() - 1));
        }

        UMaterialExpression* DestExpr = Material->GetExpressions()[DestIndex];
        if (!DestExpr)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Dest expression at index %d is null"), DestIndex));
        }

        FExpressionInput* Input = DestExpr->GetInput(DestInputIndex);
        if (!Input)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("dest_input_index %d is invalid for expression %s"),
                    DestInputIndex, *DestExpr->GetClass()->GetName()));
        }
        Input->Connect(SourceOutputIndex, SourceExpr);
    }

    NotifyMaterialModified(Material, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("connected"), true);
    Data->SetNumberField(TEXT("source_expression_index"), SourceIndex);
    Data->SetNumberField(TEXT("dest_expression_index"), DestIndex);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleCompileMaterial(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    UMaterial* Material = LoadMaterialForEditing(MaterialPath);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    // Clear stale node errors before recompilation
    for (UMaterialExpression* Expr : Material->GetExpressions())
    {
        if (Expr)
        {
            Expr->LastErrorText.Empty();
        }
    }

    // Trigger recompilation via PostEditChange
    FPropertyChangedEvent ChangeEvent(nullptr);
    Material->PostEditChangeProperty(ChangeEvent);
    Material->MarkPackageDirty();

    // Collect compile errors from material resources (shader-level)
    TArray<FString> Errors;

    // Check SM5 and SM6 feature levels
    static const ERHIFeatureLevel::Type FeatureLevels[] = { ERHIFeatureLevel::SM5, ERHIFeatureLevel::SM6 };
    for (ERHIFeatureLevel::Type FL : FeatureLevels)
    {
        for (int32 QL = 0; QL < EMaterialQualityLevel::Num; ++QL)
        {
            FMaterialResource* Resource = Material->GetMaterialResource(FL, static_cast<EMaterialQualityLevel::Type>(QL));
            if (Resource)
            {
                for (const FString& Err : Resource->GetCompileErrors())
                {
                    Errors.AddUnique(Err);
                }
            }
        }
    }

    // Collect node-level errors from expressions (LastErrorText)
    TArray<TSharedPtr<FJsonValue>> NodeErrorsArr;
    for (int32 i = 0; i < Material->GetExpressions().Num(); ++i)
    {
        UMaterialExpression* Expr = Material->GetExpressions()[i];
        if (Expr && !Expr->LastErrorText.IsEmpty())
        {
            TSharedPtr<FJsonObject> NodeErr = MakeShared<FJsonObject>();
            NodeErr->SetNumberField(TEXT("expression_index"), i);
            NodeErr->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
            NodeErr->SetStringField(TEXT("error"), Expr->LastErrorText);
            if (!Expr->Desc.IsEmpty())
            {
                NodeErr->SetStringField(TEXT("desc"), Expr->Desc);
            }
            NodeErrorsArr.Add(MakeShared<FJsonValueObject>(NodeErr));
        }
    }

    const bool bHasErrors = Errors.Num() > 0 || NodeErrorsArr.Num() > 0;

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("compiled"), !bHasErrors);
    Data->SetBoolField(TEXT("has_errors"), bHasErrors);
    Data->SetStringField(TEXT("material_path"), MaterialPath);

    // Shader compile errors
    TArray<TSharedPtr<FJsonValue>> ErrorsArr;
    for (const FString& Err : Errors)
    {
        ErrorsArr.Add(MakeShared<FJsonValueString>(Err));
    }
    Data->SetArrayField(TEXT("errors"), ErrorsArr);

    // Node-level errors
    if (NodeErrorsArr.Num() > 0)
    {
        Data->SetArrayField(TEXT("node_errors"), NodeErrorsArr);
    }

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// set_material_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleSetMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    TSharedPtr<IMaterialEditor> ActiveEditor;
    UMaterial* Material = LoadMaterialForEditing(MaterialPath, &ActiveEditor);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    TArray<FString> Changed;

    // Material Domain
    FString Domain;
    if (Params->TryGetStringField(TEXT("domain"), Domain) && !Domain.IsEmpty())
    {
        if (Domain == TEXT("surface"))             { Material->MaterialDomain = MD_Surface; }
        else if (Domain == TEXT("deferred_decal")) { Material->MaterialDomain = MD_DeferredDecal; }
        else if (Domain == TEXT("light_function")) { Material->MaterialDomain = MD_LightFunction; }
        else if (Domain == TEXT("volume"))         { Material->MaterialDomain = MD_Volume; }
        else if (Domain == TEXT("post_process"))   { Material->MaterialDomain = MD_PostProcess; }
        else if (Domain == TEXT("ui"))             { Material->MaterialDomain = MD_UI; }
        else
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown domain: %s"), *Domain));
        }
        Changed.Add(TEXT("domain"));
    }

    // Blend Mode
    FString BlendMode;
    if (Params->TryGetStringField(TEXT("blend_mode"), BlendMode) && !BlendMode.IsEmpty())
    {
        if (BlendMode == TEXT("opaque"))              { Material->BlendMode = BLEND_Opaque; }
        else if (BlendMode == TEXT("masked"))         { Material->BlendMode = BLEND_Masked; }
        else if (BlendMode == TEXT("translucent"))    { Material->BlendMode = BLEND_Translucent; }
        else if (BlendMode == TEXT("additive"))       { Material->BlendMode = BLEND_Additive; }
        else if (BlendMode == TEXT("modulate"))       { Material->BlendMode = BLEND_Modulate; }
        else if (BlendMode == TEXT("alpha_composite")) { Material->BlendMode = BLEND_AlphaComposite; }
        else if (BlendMode == TEXT("alpha_holdout"))  { Material->BlendMode = BLEND_AlphaHoldout; }
        else
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown blend_mode: %s"), *BlendMode));
        }
        Changed.Add(TEXT("blend_mode"));
    }

    // Shading Model
    FString ShadingModel;
    if (Params->TryGetStringField(TEXT("shading_model"), ShadingModel) && !ShadingModel.IsEmpty())
    {
        if (ShadingModel == TEXT("unlit"))           { Material->SetShadingModel(MSM_Unlit); }
        else if (ShadingModel == TEXT("default_lit")) { Material->SetShadingModel(MSM_DefaultLit); }
        else if (ShadingModel == TEXT("subsurface")) { Material->SetShadingModel(MSM_Subsurface); }
        else if (ShadingModel == TEXT("clear_coat")) { Material->SetShadingModel(MSM_ClearCoat); }
        else
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown shading_model: %s"), *ShadingModel));
        }
        Changed.Add(TEXT("shading_model"));
    }

    // Two-Sided
    bool bTwoSided = false;
    if (Params->TryGetBoolField(TEXT("two_sided"), bTwoSided))
    {
        Material->TwoSided = bTwoSided;
        Changed.Add(TEXT("two_sided"));
    }

    // Blendable Location (PostProcess only)
    FString BlendableLoc;
    if (Params->TryGetStringField(TEXT("blendable_location"), BlendableLoc) && !BlendableLoc.IsEmpty())
    {
        if (BlendableLoc == TEXT("before_tonemapping"))      { Material->BlendableLocation = BL_BeforeTonemapping; }
        else if (BlendableLoc == TEXT("after_tonemapping"))  { Material->BlendableLocation = BL_AfterTonemapping; }
        else if (BlendableLoc == TEXT("before_translucency")) { Material->BlendableLocation = BL_BeforeTranslucency; }
        else if (BlendableLoc == TEXT("replacing_tonemapper")) { Material->BlendableLocation = BL_ReplacingTonemapper; }
        else if (BlendableLoc == TEXT("ssr_input"))          { Material->BlendableLocation = BL_SSRInput; }
        else
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown blendable_location: %s"), *BlendableLoc));
        }
        Changed.Add(TEXT("blendable_location"));
    }

    if (Changed.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No properties specified to set"));
    }

    NotifyMaterialModified(Material, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    TArray<TSharedPtr<FJsonValue>> ChangedArr;
    for (const FString& Prop : Changed)
    {
        ChangedArr.Add(MakeShared<FJsonValueString>(Prop));
    }
    Data->SetArrayField(TEXT("changed"), ChangedArr);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// set_expression_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleSetExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    double ExprIndexD = -1.0;
    if (!Params->TryGetNumberField(TEXT("expression_index"), ExprIndexD))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: expression_index"));
    }
    const int32 ExprIndex = static_cast<int32>(ExprIndexD);

    const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("properties"), PropsPtr) || !PropsPtr || !PropsPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: properties"));
    }
    const TSharedPtr<FJsonObject>& Props = *PropsPtr;

    TSharedPtr<IMaterialEditor> ActiveEditor;
    UMaterial* Material = LoadMaterialForEditing(MaterialPath, &ActiveEditor);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    if (ExprIndex < 0 || ExprIndex >= Material->GetExpressions().Num())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("expression_index %d out of range (0..%d)"),
                ExprIndex, Material->GetExpressions().Num() - 1));
    }

    UMaterialExpression* Expr = Material->GetExpressions()[ExprIndex];
    if (!Expr)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Expression is null"));
    }

    TArray<FString> Changed;

    // Description (all expressions)
    FString Desc;
    if (Props->TryGetStringField(TEXT("description"), Desc))
    {
        Expr->Desc = Desc;
        Changed.Add(TEXT("description"));
    }

    // Handle UMaterialExpressionCustom specific properties
    UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr);
    if (CustomExpr)
    {
        // HLSL Code
        FString Code;
        if (Props->TryGetStringField(TEXT("code"), Code))
        {
            CustomExpr->Code = Code;
            Changed.Add(TEXT("code"));
        }

        // Output Type
        FString OutputType;
        if (Props->TryGetStringField(TEXT("output_type"), OutputType))
        {
            if (OutputType == TEXT("float") || OutputType == TEXT("float1"))       { CustomExpr->OutputType = CMOT_Float1; }
            else if (OutputType == TEXT("float2"))                                  { CustomExpr->OutputType = CMOT_Float2; }
            else if (OutputType == TEXT("float3"))                                  { CustomExpr->OutputType = CMOT_Float3; }
            else if (OutputType == TEXT("float4"))                                  { CustomExpr->OutputType = CMOT_Float4; }
            else
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown output_type: %s (use float/float1/float2/float3/float4)"), *OutputType));
            }
            Changed.Add(TEXT("output_type"));
        }

        // Custom Inputs
        const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
        if (Props->TryGetArrayField(TEXT("inputs"), InputsArr) && InputsArr)
        {
            CustomExpr->Inputs.Empty();
            for (const TSharedPtr<FJsonValue>& InputVal : *InputsArr)
            {
                const TSharedPtr<FJsonObject> InputObj = InputVal.IsValid() ? InputVal->AsObject() : nullptr;
                if (!InputObj.IsValid())
                {
                    continue;
                }
                FCustomInput NewInput;
                FString InputName;
                if (InputObj->TryGetStringField(TEXT("name"), InputName))
                {
                    NewInput.InputName = FName(*InputName);
                }
                CustomExpr->Inputs.Add(NewInput);
            }
            Changed.Add(TEXT("inputs"));
        }
    }

    // Handle UMaterialExpressionConstant
    UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expr);
    if (ConstExpr)
    {
        double R = 0.0;
        if (Props->TryGetNumberField(TEXT("value"), R))
        {
            ConstExpr->R = static_cast<float>(R);
            Changed.Add(TEXT("value"));
        }
    }

    // Handle UMaterialExpressionConstant3Vector
    UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expr);
    if (Const3Expr)
    {
        double R = 0, G = 0, B = 0;
        if (Props->TryGetNumberField(TEXT("r"), R)) { Const3Expr->Constant.R = static_cast<float>(R); Changed.Add(TEXT("r")); }
        if (Props->TryGetNumberField(TEXT("g"), G)) { Const3Expr->Constant.G = static_cast<float>(G); Changed.Add(TEXT("g")); }
        if (Props->TryGetNumberField(TEXT("b"), B)) { Const3Expr->Constant.B = static_cast<float>(B); Changed.Add(TEXT("b")); }
    }

    // Handle UMaterialExpressionScalarParameter
    UMaterialExpressionScalarParameter* ScalarParamExpr = Cast<UMaterialExpressionScalarParameter>(Expr);
    if (ScalarParamExpr)
    {
        FString ParamName;
        if (Props->TryGetStringField(TEXT("parameter_name"), ParamName))
        {
            ScalarParamExpr->ParameterName = FName(*ParamName);
            Changed.Add(TEXT("parameter_name"));
        }
        double DefaultVal = 0.0;
        if (Props->TryGetNumberField(TEXT("default_value"), DefaultVal))
        {
            ScalarParamExpr->DefaultValue = static_cast<float>(DefaultVal);
            Changed.Add(TEXT("default_value"));
        }
    }

    // Handle UMaterialExpressionVectorParameter
    UMaterialExpressionVectorParameter* VectorParamExpr = Cast<UMaterialExpressionVectorParameter>(Expr);
    if (VectorParamExpr)
    {
        FString ParamName;
        if (Props->TryGetStringField(TEXT("parameter_name"), ParamName))
        {
            VectorParamExpr->ParameterName = FName(*ParamName);
            Changed.Add(TEXT("parameter_name"));
        }
        double R = 0, G = 0, B = 0, A = 1;
        bool bAnyChannel = false;
        if (Props->TryGetNumberField(TEXT("r"), R)) { bAnyChannel = true; }
        if (Props->TryGetNumberField(TEXT("g"), G)) { bAnyChannel = true; }
        if (Props->TryGetNumberField(TEXT("b"), B)) { bAnyChannel = true; }
        if (Props->TryGetNumberField(TEXT("a"), A)) { bAnyChannel = true; }
        if (bAnyChannel)
        {
            VectorParamExpr->DefaultValue = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
            Changed.Add(TEXT("default_value"));
        }
    }

    // Handle UMaterialExpressionCollectionParameter
    UMaterialExpressionCollectionParameter* CollectionParamExpr = Cast<UMaterialExpressionCollectionParameter>(Expr);
    if (CollectionParamExpr)
    {
        FString CollectionPath;
        if (Props->TryGetStringField(TEXT("collection"), CollectionPath))
        {
            UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, *CollectionPath);
            if (MPC)
            {
                CollectionParamExpr->Collection = MPC;
                Changed.Add(TEXT("collection"));
            }
            else
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("MPC not found: %s"), *CollectionPath));
            }
        }
        FString ParamName;
        if (Props->TryGetStringField(TEXT("parameter_name"), ParamName))
        {
            FName ParamFName(*ParamName);
            CollectionParamExpr->ParameterName = ParamFName;

            // Look up ParameterId (GUID) from the MPC to establish the actual link
            if (CollectionParamExpr->Collection)
            {
                UMaterialParameterCollection* MPC = CollectionParamExpr->Collection;
                FGuid ParamGuid;
                bool bFoundParam = false;

                for (const FCollectionScalarParameter& Param : MPC->ScalarParameters)
                {
                    if (Param.ParameterName == ParamFName)
                    {
                        ParamGuid = Param.Id;
                        bFoundParam = true;
                        break;
                    }
                }

                if (!bFoundParam)
                {
                    for (const FCollectionVectorParameter& Param : MPC->VectorParameters)
                    {
                        if (Param.ParameterName == ParamFName)
                        {
                            ParamGuid = Param.Id;
                            bFoundParam = true;
                            break;
                        }
                    }
                }

                if (bFoundParam)
                {
                    CollectionParamExpr->ParameterId = ParamGuid;
                }
                else
                {
                    return FSmithUECommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Parameter '%s' not found in MPC '%s'"), *ParamName, *MPC->GetPathName()));
                }
            }

            Changed.Add(TEXT("parameter_name"));
        }
    }

    // Handle MaterialExpressionMaterialFunctionCall - bind a material function
    UMaterialExpressionMaterialFunctionCall* FuncCallExpr = Cast<UMaterialExpressionMaterialFunctionCall>(Expr);
    if (FuncCallExpr)
    {
        FString FuncPath;
        if (Props->TryGetStringField(TEXT("material_function"), FuncPath))
        {
            UMaterialFunctionInterface* MFI = LoadObject<UMaterialFunctionInterface>(nullptr, *FuncPath);
            if (!MFI)
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Material function not found: %s"), *FuncPath));
            }
            if (FuncCallExpr->SetMaterialFunction(MFI))
            {
                Changed.Add(TEXT("material_function"));
            }
            else
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to set material function: %s"), *FuncPath));
            }
        }
    }

    // Handle SceneTexture ID via reflection (avoids linking to unexported class)
    FString SceneTexId;
    if (Props->TryGetStringField(TEXT("scene_texture_id"), SceneTexId))
    {
        FProperty* Prop = Expr->GetClass()->FindPropertyByName(TEXT("SceneTextureId"));
        if (Prop)
        {
            // Map string to ESceneTextureId enum value (use actual enum for correct indices)
            int32 TexId = -1;
            if      (SceneTexId == TEXT("PPI_SceneColor")          || SceneTexId == TEXT("scene_color"))            { TexId = PPI_SceneColor; }
            else if (SceneTexId == TEXT("PPI_SceneDepth")          || SceneTexId == TEXT("scene_depth"))            { TexId = PPI_SceneDepth; }
            else if (SceneTexId == TEXT("PPI_DiffuseColor")        || SceneTexId == TEXT("diffuse_color"))          { TexId = PPI_DiffuseColor; }
            else if (SceneTexId == TEXT("PPI_SpecularColor")       || SceneTexId == TEXT("specular_color"))         { TexId = PPI_SpecularColor; }
            else if (SceneTexId == TEXT("PPI_BaseColor")           || SceneTexId == TEXT("base_color"))             { TexId = PPI_BaseColor; }
            else if (SceneTexId == TEXT("PPI_Metallic")            || SceneTexId == TEXT("metallic"))               { TexId = PPI_Metallic; }
            else if (SceneTexId == TEXT("PPI_WorldNormal")         || SceneTexId == TEXT("world_normal"))           { TexId = PPI_WorldNormal; }
            else if (SceneTexId == TEXT("PPI_Opacity")             || SceneTexId == TEXT("opacity"))                { TexId = PPI_Opacity; }
            else if (SceneTexId == TEXT("PPI_Roughness")           || SceneTexId == TEXT("roughness"))              { TexId = PPI_Roughness; }
            else if (SceneTexId == TEXT("PPI_MaterialAO")          || SceneTexId == TEXT("material_ao"))            { TexId = PPI_MaterialAO; }
            else if (SceneTexId == TEXT("PPI_CustomDepth")         || SceneTexId == TEXT("custom_depth"))           { TexId = PPI_CustomDepth; }
            else if (SceneTexId == TEXT("PPI_PostProcessInput0")   || SceneTexId == TEXT("post_process_input_0"))   { TexId = PPI_PostProcessInput0; }
            else if (SceneTexId == TEXT("PPI_PostProcessInput1")   || SceneTexId == TEXT("post_process_input_1"))   { TexId = PPI_PostProcessInput1; }
            else if (SceneTexId == TEXT("PPI_PostProcessInput2")   || SceneTexId == TEXT("post_process_input_2"))   { TexId = PPI_PostProcessInput2; }
            else if (SceneTexId == TEXT("PPI_ShadingModelColor")   || SceneTexId == TEXT("shading_model_color"))    { TexId = PPI_ShadingModelColor; }
            else if (SceneTexId == TEXT("PPI_ShadingModelID")      || SceneTexId == TEXT("shading_model_id"))       { TexId = PPI_ShadingModelID; }
            else if (SceneTexId == TEXT("PPI_AmbientOcclusion")    || SceneTexId == TEXT("ambient_occlusion"))      { TexId = PPI_AmbientOcclusion; }
            else if (SceneTexId == TEXT("PPI_CustomStencil")       || SceneTexId == TEXT("custom_stencil"))         { TexId = PPI_CustomStencil; }
            else if (SceneTexId == TEXT("PPI_Velocity")            || SceneTexId == TEXT("velocity"))               { TexId = PPI_Velocity; }
            else if (SceneTexId == TEXT("PPI_WorldTangent")        || SceneTexId == TEXT("world_tangent"))          { TexId = PPI_WorldTangent; }
            else if (SceneTexId == TEXT("PPI_Anisotropy")          || SceneTexId == TEXT("anisotropy"))             { TexId = PPI_Anisotropy; }

            if (TexId < 0)
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown scene_texture_id: %s. Use PPI_CustomStencil, PPI_PostProcessInput0, PPI_CustomDepth, etc."), *SceneTexId));
            }

            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
            if (ValuePtr)
            {
                *static_cast<uint8*>(ValuePtr) = static_cast<uint8>(TexId);
                Changed.Add(TEXT("scene_texture_id"));
            }
        }
    }

    if (Changed.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No recognized properties were set. Check expression type and property names."));
    }

    NotifyMaterialModified(Material, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetNumberField(TEXT("expression_index"), ExprIndex);
    Data->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
    TArray<TSharedPtr<FJsonValue>> ChangedArr;
    for (const FString& Prop : Changed)
    {
        ChangedArr.Add(MakeShared<FJsonValueString>(Prop));
    }
    Data->SetArrayField(TEXT("changed"), ChangedArr);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// MPC Commands
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleCreateMPC(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString Name;
    if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: name"));
    }

    FString FolderPath;
    if (!Params->TryGetStringField(TEXT("path"), FolderPath) || FolderPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: path"));
    }

    const FString FullPath = FolderPath / Name;

    if (UEditorAssetLibrary::DoesAssetExist(FullPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("MPC already exists: %s"), *FullPath));
    }

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    UMaterialParameterCollection* MPC = NewObject<UMaterialParameterCollection>(
        Package, FName(*Name), RF_Public | RF_Standalone);

    if (!MPC)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create MPC object"));
    }

    FAssetRegistryModule::AssetCreated(MPC);
    MPC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("path"), FullPath);
    Data->SetNumberField(TEXT("scalars"), 0);
    Data->SetNumberField(TEXT("vectors"), 0);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleAddMPCScalar(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MpcPath;
    if (!Params->TryGetStringField(TEXT("mpc_path"), MpcPath) || MpcPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: mpc_path"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName) || ParamName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_name"));
    }

    UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(
        UEditorAssetLibrary::LoadAsset(MpcPath));
    if (!MPC)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("MPC not found: %s"), *MpcPath));
    }

    // Check for duplicate
    for (const FCollectionScalarParameter& Existing : MPC->ScalarParameters)
    {
        if (Existing.ParameterName == FName(*ParamName))
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Scalar parameter already exists: %s"), *ParamName));
        }
    }

    double DefaultValue = 0.0;
    Params->TryGetNumberField(TEXT("default_value"), DefaultValue);

    FCollectionScalarParameter NewParam;
    NewParam.ParameterName = FName(*ParamName);
    NewParam.DefaultValue = static_cast<float>(DefaultValue);
    NewParam.Id = FGuid::NewGuid();
    MPC->ScalarParameters.Add(NewParam);
    MPC->PostEditChange();
    MPC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mpc_path"), MpcPath);
    Data->SetStringField(TEXT("param_name"), ParamName);
    Data->SetNumberField(TEXT("default_value"), DefaultValue);
    Data->SetNumberField(TEXT("scalar_count"), MPC->ScalarParameters.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleAddMPCVector(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MpcPath;
    if (!Params->TryGetStringField(TEXT("mpc_path"), MpcPath) || MpcPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: mpc_path"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName) || ParamName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_name"));
    }

    UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(
        UEditorAssetLibrary::LoadAsset(MpcPath));
    if (!MPC)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("MPC not found: %s"), *MpcPath));
    }

    // Check for duplicate
    for (const FCollectionVectorParameter& Existing : MPC->VectorParameters)
    {
        if (Existing.ParameterName == FName(*ParamName))
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Vector parameter already exists: %s"), *ParamName));
        }
    }

    double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
    Params->TryGetNumberField(TEXT("default_r"), R);
    Params->TryGetNumberField(TEXT("default_g"), G);
    Params->TryGetNumberField(TEXT("default_b"), B);
    Params->TryGetNumberField(TEXT("default_a"), A);

    FCollectionVectorParameter NewParam;
    NewParam.ParameterName = FName(*ParamName);
    NewParam.DefaultValue = FLinearColor(
        static_cast<float>(R), static_cast<float>(G),
        static_cast<float>(B), static_cast<float>(A));
    NewParam.Id = FGuid::NewGuid();
    MPC->VectorParameters.Add(NewParam);
    MPC->PostEditChange();
    MPC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mpc_path"), MpcPath);
    Data->SetStringField(TEXT("param_name"), ParamName);
    Data->SetNumberField(TEXT("default_r"), R);
    Data->SetNumberField(TEXT("default_g"), G);
    Data->SetNumberField(TEXT("default_b"), B);
    Data->SetNumberField(TEXT("default_a"), A);
    Data->SetNumberField(TEXT("vector_count"), MPC->VectorParameters.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleSetMPCValue(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MpcPath;
    if (!Params->TryGetStringField(TEXT("mpc_path"), MpcPath) || MpcPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: mpc_path"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName) || ParamName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_name"));
    }

    FString ValueStr;
    if (!Params->TryGetStringField(TEXT("value"), ValueStr) || ValueStr.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: value"));
    }

    float NewValue = FCString::Atof(*ValueStr);

    UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(
        UEditorAssetLibrary::LoadAsset(MpcPath));
    if (!MPC)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("MPC not found: %s"), *MpcPath));
    }

    bool bFound = false;
    for (FCollectionScalarParameter& Scalar : MPC->ScalarParameters)
    {
        if (Scalar.ParameterName == FName(*ParamName))
        {
            Scalar.DefaultValue = NewValue;
            bFound = true;
            break;
        }
    }

    if (!bFound)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Scalar parameter not found: %s"), *ParamName));
    }

    MPC->PostEditChange();
    MPC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mpc_path"), MpcPath);
    Data->SetStringField(TEXT("param_name"), ParamName);
    Data->SetNumberField(TEXT("value"), NewValue);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Material Instance handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));

    FString Name, FolderPath, ParentPath;
    if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: name"));
    if (!Params->TryGetStringField(TEXT("path"), FolderPath) || FolderPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: path"));
    if (!Params->TryGetStringField(TEXT("parent_material"), ParentPath) || ParentPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: parent_material"));

    UMaterialInterface* Parent = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(ParentPath));
    if (!Parent)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load parent material: %s"), *ParentPath));

    const FString PackagePath = FolderPath / Name;
    if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset already exists: %s"), *PackagePath));

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create package"));

    UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Package, FName(*Name), RF_Public | RF_Standalone);
    if (!MIC)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create MaterialInstanceConstant object"));

    MIC->SetParentEditorOnly(Parent);
    FAssetRegistryModule::AssetCreated(MIC);
    MIC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), PackagePath);
    Data->SetStringField(TEXT("parent_material"), ParentPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleSetMIScalar(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));

    FString MiPath, ParamName;
    if (!Params->TryGetStringField(TEXT("mi_path"), MiPath) || MiPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: mi_path"));
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName) || ParamName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_name"));

    double Value = 0.0;
    if (!Params->TryGetNumberField(TEXT("value"), Value))
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: value"));

    UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(MiPath));
    if (!MIC)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load MaterialInstanceConstant: %s"), *MiPath));

    MIC->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(*ParamName)), static_cast<float>(Value));
    MIC->PostEditChange();
    MIC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mi_path"), MiPath);
    Data->SetStringField(TEXT("param_name"), ParamName);
    Data->SetNumberField(TEXT("value"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleSetMIVector(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));

    FString MiPath, ParamName;
    if (!Params->TryGetStringField(TEXT("mi_path"), MiPath) || MiPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: mi_path"));
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName) || ParamName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_name"));

    double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
    Params->TryGetNumberField(TEXT("r"), R);
    Params->TryGetNumberField(TEXT("g"), G);
    Params->TryGetNumberField(TEXT("b"), B);
    Params->TryGetNumberField(TEXT("a"), A);

    UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(MiPath));
    if (!MIC)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load MaterialInstanceConstant: %s"), *MiPath));

    MIC->SetVectorParameterValueEditorOnly(
        FMaterialParameterInfo(FName(*ParamName)),
        FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A)));
    MIC->PostEditChange();
    MIC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mi_path"), MiPath);
    Data->SetStringField(TEXT("param_name"), ParamName);
    Data->SetNumberField(TEXT("r"), R);
    Data->SetNumberField(TEXT("g"), G);
    Data->SetNumberField(TEXT("b"), B);
    Data->SetNumberField(TEXT("a"), A);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialCommands::HandleGetMIInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));

    FString MiPath;
    if (!Params->TryGetStringField(TEXT("mi_path"), MiPath) || MiPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: mi_path"));

    UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(MiPath));
    if (!MIC)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load MaterialInstanceConstant: %s"), *MiPath));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mi_path"), MiPath);

    // Parent
    UMaterialInterface* ParentMI = MIC->Parent;
    Data->SetStringField(TEXT("parent"), ParentMI ? ParentMI->GetPathName() : TEXT("None"));

    // Scalar overrides
    TArray<TSharedPtr<FJsonValue>> ScalarArr;
    for (const FScalarParameterValue& SV : MIC->ScalarParameterValues)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), SV.ParameterInfo.Name.ToString());
        Entry->SetNumberField(TEXT("value"), SV.ParameterValue);
        ScalarArr.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Data->SetArrayField(TEXT("scalar_parameters"), ScalarArr);

    // Vector overrides
    TArray<TSharedPtr<FJsonValue>> VectorArr;
    for (const FVectorParameterValue& VV : MIC->VectorParameterValues)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), VV.ParameterInfo.Name.ToString());
        Entry->SetNumberField(TEXT("r"), VV.ParameterValue.R);
        Entry->SetNumberField(TEXT("g"), VV.ParameterValue.G);
        Entry->SetNumberField(TEXT("b"), VV.ParameterValue.B);
        Entry->SetNumberField(TEXT("a"), VV.ParameterValue.A);
        VectorArr.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Data->SetArrayField(TEXT("vector_parameters"), VectorArr);

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
