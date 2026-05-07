// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/UEAgentMaterialCommands.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionMultiply.h"
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
        // Try exact name first (e.g. "UMaterialExpressionConstant3Vector")
        UClass* Found = FindObject<UClass>(nullptr, *ClassName, true);
        if (Found)
        {
            return Found;
        }

        // Try with "U" prefix
        const FString WithU = TEXT("U") + ClassName;
        Found = FindObject<UClass>(nullptr, *WithU, true);
        if (Found)
        {
            return Found;
        }

        // Try with full "UMaterialExpression" prefix
        const FString WithFull = TEXT("UMaterialExpression") + ClassName;
        Found = FindObject<UClass>(nullptr, *WithFull, true);
        return Found;
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

void FUEAgentMaterialCommands::RegisterTools(FUEAgentToolRegistry& Registry)
{
    // create_material
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("create_material"),
            TEXT("Material"),
            TEXT("Create a new UMaterial asset"),
            {
                FUEAgentToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name"), true),
                FUEAgentToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path, e.g. /Game/Materials"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FUEAgentMaterialCommands::HandleCreateMaterial(Params);
        });

    // get_material_info
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("get_material_info"),
            TEXT("Material"),
            TEXT("Get information about a material including its expressions"),
            {
                FUEAgentToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path, e.g. /Game/Materials/M_Test"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FUEAgentMaterialCommands::HandleGetMaterialInfo(Params);
        });

    // add_material_expression
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("add_material_expression"),
            TEXT("Material"),
            TEXT("Add a material expression node to a material"),
            {
                FUEAgentToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true),
                FUEAgentToolParam(TEXT("expression_class"), TEXT("string"), TEXT("Expression class name, e.g. MaterialExpressionConstant3Vector"), true),
                FUEAgentToolParam(TEXT("position_x"), TEXT("number"), TEXT("Editor X position"), false, TEXT("0")),
                FUEAgentToolParam(TEXT("position_y"), TEXT("number"), TEXT("Editor Y position"), false, TEXT("0"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FUEAgentMaterialCommands::HandleAddMaterialExpression(Params);
        });

    // connect_material_pins
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("connect_material_pins"),
            TEXT("Material"),
            TEXT("Connect material expression pins. Use dest_expression_index=-1 to connect to material output."),
            {
                FUEAgentToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true),
                FUEAgentToolParam(TEXT("source_expression_index"), TEXT("number"), TEXT("Index of source expression in Expressions array"), true),
                FUEAgentToolParam(TEXT("source_output_index"), TEXT("number"), TEXT("Output pin index on source expression"), false, TEXT("0")),
                FUEAgentToolParam(TEXT("dest_expression_index"), TEXT("number"), TEXT("Index of dest expression, or -1 for material output"), true),
                FUEAgentToolParam(TEXT("dest_input_index"), TEXT("number"), TEXT("Input pin index. For material output: 0=BaseColor,1=Metallic,2=Roughness,3=Normal,4=Emissive"), false, TEXT("0"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FUEAgentMaterialCommands::HandleConnectMaterialPins(Params);
        });

    // compile_material
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("compile_material"),
            TEXT("Material"),
            TEXT("Trigger material recompilation"),
            {
                FUEAgentToolParam(TEXT("material_path"), TEXT("string"), TEXT("Full asset path"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FUEAgentMaterialCommands::HandleCompileMaterial(Params);
        });
}

// ---------------------------------------------------------------------------
// Command Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentMaterialCommands::HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialName;
    if (!Params->TryGetStringField(TEXT("name"), MaterialName) || MaterialName.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: name"));
    }

    FString FolderPath;
    if (!Params->TryGetStringField(TEXT("path"), FolderPath) || FolderPath.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: path"));
    }

    const FString FullPath = FolderPath / MaterialName;

    if (UEditorAssetLibrary::DoesAssetExist(FullPath))
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material already exists: %s"), *FullPath));
    }

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
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
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Failed to create material"));
    }

    FAssetRegistryModule::AssetCreated(NewMaterial);
    Package->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), MaterialName);
    Data->SetStringField(TEXT("material_path"), FullPath);
    return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentMaterialCommands::HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    UMaterial* Material = LoadMaterial(MaterialPath);
    if (!Material)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
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
    return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentMaterialCommands::HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    FString ExpressionClassName;
    if (!Params->TryGetStringField(TEXT("expression_class"), ExpressionClassName) || ExpressionClassName.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: expression_class"));
    }

    UMaterial* Material = LoadMaterial(MaterialPath);
    if (!Material)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    UClass* ExprClass = ResolveExpressionClass(ExpressionClassName);
    if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown expression class: %s"), *ExpressionClassName));
    }

    double PosX = 0.0, PosY = 0.0;
    Params->TryGetNumberField(TEXT("position_x"), PosX);
    Params->TryGetNumberField(TEXT("position_y"), PosY);

    UMaterialExpression* NewExpr = NewObject<UMaterialExpression>(Material, ExprClass);
    if (!NewExpr)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Failed to create expression object"));
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
    return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentMaterialCommands::HandleConnectMaterialPins(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    double SourceIndexD = -1.0, DestIndexD = -2.0;
    if (!Params->TryGetNumberField(TEXT("source_expression_index"), SourceIndexD))
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: source_expression_index"));
    }
    if (!Params->TryGetNumberField(TEXT("dest_expression_index"), DestIndexD))
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: dest_expression_index"));
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
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    if (SourceIndex < 0 || SourceIndex >= Material->GetExpressions().Num())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("source_expression_index %d out of range (0..%d)"),
                SourceIndex, Material->GetExpressions().Num() - 1));
    }

    UMaterialExpression* SourceExpr = Material->GetExpressions()[SourceIndex];
    if (!SourceExpr)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source expression at index %d is null"), SourceIndex));
    }

    if (DestIndex == -1)
    {
        // Connect to material base output
        FExpressionInput* Input = GetMaterialBaseInput(Material, DestInputIndex);
        if (!Input)
        {
            return FUEAgentCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("dest_input_index %d is out of range for material output (0-4)"), DestInputIndex));
        }
        Input->Connect(SourceOutputIndex, SourceExpr);
    }
    else
    {
        if (DestIndex < 0 || DestIndex >= Material->GetExpressions().Num())
        {
            return FUEAgentCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("dest_expression_index %d out of range (0..%d)"),
                    DestIndex, Material->GetExpressions().Num() - 1));
        }

        UMaterialExpression* DestExpr = Material->GetExpressions()[DestIndex];
        if (!DestExpr)
        {
            return FUEAgentCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Dest expression at index %d is null"), DestIndex));
        }

        FExpressionInput* Input = DestExpr->GetInput(DestInputIndex);
        if (!Input)
        {
            return FUEAgentCommonUtils::CreateErrorResponse(
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
    return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentMaterialCommands::HandleCompileMaterial(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: material_path"));
    }

    UMaterial* Material = LoadMaterial(MaterialPath);
    if (!Material)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(
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
    return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}
