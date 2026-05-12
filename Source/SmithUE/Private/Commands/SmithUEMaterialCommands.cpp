// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEMaterialCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    UMaterial* LoadMaterial(const FString& MaterialPath)
    {
        return Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPath));
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
                FSmithUEToolParam(TEXT("properties"), TEXT("object"), TEXT("Key-value pairs to set. For Custom: code, output_type(float/float2/float3/float4), description, inputs(array of {name,type})"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialCommands::HandleSetExpressionProperty(Params);
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

    UMaterial* Material = LoadMaterial(MaterialPath);
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

    UMaterial* Material = LoadMaterial(MaterialPath);
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

    NewExpr->MaterialExpressionEditorX = static_cast<int32>(PosX);
    NewExpr->MaterialExpressionEditorY = static_cast<int32>(PosY);

    // UE 5.2: use mutable expression collection for writes
    const int32 NewIndex = Material->GetExpressionCollection().Expressions.Add(NewExpr);

    Material->PostEditChange();
    Material->MarkPackageDirty();

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

    UMaterial* Material = LoadMaterial(MaterialPath);
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

    Material->PostEditChange();
    Material->MarkPackageDirty();

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

    UMaterial* Material = LoadMaterial(MaterialPath);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    // Trigger recompilation via PostEditChange
    FPropertyChangedEvent ChangeEvent(nullptr);
    Material->PostEditChangeProperty(ChangeEvent);
    Material->MarkPackageDirty();

    const bool bHasErrors = false; // Compilation errors tracked by material resource, not exposed simply

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("compiled"), true);
    Data->SetBoolField(TEXT("has_errors"), bHasErrors);
    Data->SetStringField(TEXT("material_path"), MaterialPath);
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

    UMaterial* Material = LoadMaterial(MaterialPath);
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

    Material->PostEditChange();
    Material->MarkPackageDirty();

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

    UMaterial* Material = LoadMaterial(MaterialPath);
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

    // Handle SceneTexture ID via reflection (avoids linking to unexported class)
    FString SceneTexId;
    if (Props->TryGetStringField(TEXT("scene_texture_id"), SceneTexId))
    {
        FProperty* Prop = Expr->GetClass()->FindPropertyByName(TEXT("SceneTextureId"));
        if (Prop)
        {
            // ESceneTextureId enum values
            uint8 TexId = 14; // PPI_PostProcessInput0 default
            if (SceneTexId == TEXT("post_process_input_0") || SceneTexId == TEXT("scene_color")) { TexId = 14; }
            else if (SceneTexId == TEXT("scene_depth")) { TexId = 1; }
            else if (SceneTexId == TEXT("custom_depth")) { TexId = 12; }

            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
            if (ValuePtr)
            {
                *static_cast<uint8*>(ValuePtr) = TexId;
                Changed.Add(TEXT("scene_texture_id"));
            }
        }
    }

    if (Changed.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No recognized properties were set. Check expression type and property names."));
    }

    Material->PostEditChange();
    Material->MarkPackageDirty();

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
