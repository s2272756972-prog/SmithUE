// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEMaterialFunctionCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialEditorUtilities.h"
#include "IMaterialEditor.h"
#include "Toolkits/ToolkitManager.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    /**
     * Load a material function for editing. If the Material Editor is open for this function,
     * returns the editor's working COPY (which enables live UI refresh via MaterialGraph).
     * Otherwise returns the original asset.
     *
     * @param OutEditor  If non-null and editor is open, receives the IMaterialEditor ptr.
     */
    UMaterialFunction* LoadMfForEditing(const FString& FunctionPath, TSharedPtr<IMaterialEditor>* OutEditor = nullptr)
    {
        UMaterialFunction* Original = Cast<UMaterialFunction>(UEditorAssetLibrary::LoadAsset(FunctionPath));
        if (!Original) return nullptr;

        TSharedPtr<IToolkit> Found = FToolkitManager::Get().FindEditorForAsset(Original);
        if (Found.IsValid())
        {
            TSharedPtr<IMaterialEditor> MatEditor = StaticCastSharedPtr<IMaterialEditor>(Found);
            if (MatEditor.IsValid())
            {
                UMaterial* EditorMat = Cast<UMaterial>(MatEditor->GetMaterialInterface());
                if (EditorMat && EditorMat->MaterialGraph && EditorMat->MaterialGraph->MaterialFunction)
                {
                    if (OutEditor) *OutEditor = MatEditor;
                    return EditorMat->MaterialGraph->MaterialFunction;
                }
            }
        }

        return Original;
    }

    /**
     * Notify the editor that a material function has been modified programmatically.
     * When an editor is active (working on the copy), rebuilds graph and refreshes UI.
     * When no editor is active (working on original), triggers standard change notification.
     */
    void NotifyMaterialFunctionModified(UMaterialFunction* Func, TSharedPtr<IMaterialEditor> MatEditor = nullptr)
    {
        if (MatEditor.IsValid())
        {
            // Working on editor's copy - get MaterialGraph through the editor's Material
            UMaterial* EditorMat = Cast<UMaterial>(MatEditor->GetMaterialInterface());
            if (EditorMat && EditorMat->MaterialGraph)
            {
                EditorMat->MaterialGraph->RebuildGraph();
            }
            MatEditor->UpdateMaterialAfterGraphChange();
            UE_LOG(LogSmithUE, Log, TEXT("NotifyMaterialFunctionModified: Editor graph refreshed for %s"), *Func->GetName());
        }
        else
        {
            // Working on original - no editor open
            Func->PreEditChange(nullptr);
            Func->PostEditChange();
            Func->MarkPackageDirty();
        }
    }

    /** Resolve a short or full expression class name to a UClass. */
    UClass* ResolveMfExpressionClass(const FString& ClassName)
    {
        TArray<FString> Candidates;
        Candidates.Add(ClassName);
        Candidates.Add(TEXT("U") + ClassName);
        Candidates.Add(TEXT("UMaterialExpression") + ClassName);
        Candidates.Add(TEXT("MaterialExpression") + ClassName);

        for (const FString& Name : Candidates)
        {
            UClass* Found = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
            if (Found && Found->IsChildOf(UMaterialExpression::StaticClass()))
            {
                return Found;
            }
        }

        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEMaterialFunctionCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // create_material_function
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_material_function"),
            TEXT("Material"),
            TEXT("Create a new UMaterialFunction asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path, e.g. /Game/Materials"), true),
                FSmithUEToolParam(TEXT("description"), TEXT("string"), TEXT("Function description"), false),
                FSmithUEToolParam(TEXT("expose_to_library"), TEXT("bool"), TEXT("Expose to material function library (default: true)"), false, TEXT("true")),
                FSmithUEToolParam(TEXT("library_categories"), TEXT("string"), TEXT("Comma-separated library categories, e.g. 'PostProcess,Utility'"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialFunctionCommands::HandleCreateMaterialFunction(Params);
        });

    // get_material_function_info
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_material_function_info"),
            TEXT("Material"),
            TEXT("Get information about a material function including its expressions"),
            {
                FSmithUEToolParam(TEXT("function_path"), TEXT("string"), TEXT("Full asset path, e.g. /Game/Materials/MF_Test"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialFunctionCommands::HandleGetMaterialFunctionInfo(Params);
        });

    // add_mf_expression
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_mf_expression"),
            TEXT("Material"),
            TEXT("Add a material expression node to a material function"),
            {
                FSmithUEToolParam(TEXT("function_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("expression_class"), TEXT("string"), TEXT("Expression class name, e.g. MaterialExpressionFunctionInput"), true),
                FSmithUEToolParam(TEXT("position_x"), TEXT("number"), TEXT("Editor X position"), false, TEXT("0")),
                FSmithUEToolParam(TEXT("position_y"), TEXT("number"), TEXT("Editor Y position"), false, TEXT("0"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialFunctionCommands::HandleAddMfExpression(Params);
        });

    // connect_mf_pins
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("connect_mf_pins"),
            TEXT("Material"),
            TEXT("Connect material expression pins within a material function"),
            {
                FSmithUEToolParam(TEXT("function_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("source_expression_index"), TEXT("number"), TEXT("Index of source expression in FunctionExpressions array"), true),
                FSmithUEToolParam(TEXT("source_output_index"), TEXT("number"), TEXT("Output pin index on source expression"), false, TEXT("0")),
                FSmithUEToolParam(TEXT("dest_expression_index"), TEXT("number"), TEXT("Index of dest expression"), true),
                FSmithUEToolParam(TEXT("dest_input_index"), TEXT("number"), TEXT("Input pin index on dest expression"), false, TEXT("0"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialFunctionCommands::HandleConnectMfPins(Params);
        });

    // set_mf_expression_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_mf_expression_property"),
            TEXT("Material"),
            TEXT("Set properties on a material function expression node (Custom HLSL code, constant values, function input/output names, etc.)"),
            {
                FSmithUEToolParam(TEXT("function_path"), TEXT("string"), TEXT("Full asset path"), true),
                FSmithUEToolParam(TEXT("expression_index"), TEXT("number"), TEXT("Index of expression in FunctionExpressions array"), true),
                FSmithUEToolParam(TEXT("properties"), TEXT("object"), TEXT("Key-value pairs. Custom: code, output_type, description, inputs. FunctionInput: input_name, input_type, preview_value. FunctionOutput: output_name"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUEMaterialFunctionCommands::HandleSetMfExpressionProperty(Params);
        });
}

// ---------------------------------------------------------------------------
// Command Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEMaterialFunctionCommands::HandleCreateMaterialFunction(const TSharedPtr<FJsonObject>& Params)
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
            FString::Printf(TEXT("Material function already exists: %s"), *FullPath));
    }

    UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    UMaterialFunction* NewFunc = Cast<UMaterialFunction>(
        Factory->FactoryCreateNew(UMaterialFunction::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn));

    if (!NewFunc)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create material function"));
    }

    // Set optional properties
    FString Description;
    if (Params->TryGetStringField(TEXT("description"), Description))
    {
        NewFunc->Description = Description;
    }

    bool bExposeToLibrary = true;
    Params->TryGetBoolField(TEXT("expose_to_library"), bExposeToLibrary);
    NewFunc->bExposeToLibrary = bExposeToLibrary;

    FString Categories;
    if (Params->TryGetStringField(TEXT("library_categories"), Categories) && !Categories.IsEmpty())
    {
        TArray<FString> CatArray;
        Categories.ParseIntoArray(CatArray, TEXT(","), true);
        NewFunc->LibraryCategoriesText.Empty();
        for (const FString& Cat : CatArray)
        {
            NewFunc->LibraryCategoriesText.Add(FText::FromString(Cat.TrimStartAndEnd()));
        }
    }

    FAssetRegistryModule::AssetCreated(NewFunc);
    Package->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("function_path"), FullPath);
    Data->SetBoolField(TEXT("expose_to_library"), NewFunc->bExposeToLibrary);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialFunctionCommands::HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString FunctionPath;
    if (!Params->TryGetStringField(TEXT("function_path"), FunctionPath) || FunctionPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: function_path"));
    }

    UMaterialFunction* Func = LoadMfForEditing(FunctionPath);
    if (!Func)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material function not found: %s"), *FunctionPath));
    }

    TArray<TSharedPtr<FJsonValue>> ExpressionsArray;

    for (int32 i = 0; i < Func->GetExpressions().Num(); ++i)
    {
        UMaterialExpression* Expr = Func->GetExpressions()[i];
        if (!Expr) continue;

        TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
        ExprObj->SetNumberField(TEXT("index"), i);
        ExprObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
        ExprObj->SetStringField(TEXT("name"), Expr->GetName());
        ExprObj->SetStringField(TEXT("desc"), Expr->Desc);

        TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
        PosObj->SetNumberField(TEXT("x"), Expr->MaterialExpressionEditorX);
        PosObj->SetNumberField(TEXT("y"), Expr->MaterialExpressionEditorY);
        ExprObj->SetObjectField(TEXT("position"), PosObj);

        // Additional info for FunctionInput/Output
        if (UMaterialExpressionFunctionInput* InputExpr = Cast<UMaterialExpressionFunctionInput>(Expr))
        {
            ExprObj->SetStringField(TEXT("input_name"), InputExpr->InputName.ToString());
            ExprObj->SetNumberField(TEXT("input_type"), static_cast<int32>(InputExpr->InputType));
        }
        else if (UMaterialExpressionFunctionOutput* OutputExpr = Cast<UMaterialExpressionFunctionOutput>(Expr))
        {
            ExprObj->SetStringField(TEXT("output_name"), OutputExpr->OutputName.ToString());
        }

        ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Func->GetName());
    Data->SetStringField(TEXT("function_path"), FunctionPath);
    Data->SetStringField(TEXT("description"), Func->Description);
    Data->SetBoolField(TEXT("expose_to_library"), Func->bExposeToLibrary);
    Data->SetNumberField(TEXT("expression_count"), Func->GetExpressions().Num());
    Data->SetArrayField(TEXT("expressions"), ExpressionsArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialFunctionCommands::HandleAddMfExpression(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString FunctionPath;
    if (!Params->TryGetStringField(TEXT("function_path"), FunctionPath) || FunctionPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: function_path"));
    }

    FString ExpressionClassName;
    if (!Params->TryGetStringField(TEXT("expression_class"), ExpressionClassName) || ExpressionClassName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: expression_class"));
    }

    TSharedPtr<IMaterialEditor> ActiveEditor;
    UMaterialFunction* Func = LoadMfForEditing(FunctionPath, &ActiveEditor);
    if (!Func)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material function not found: %s"), *FunctionPath));
    }

    UClass* ExprClass = ResolveMfExpressionClass(ExpressionClassName);
    if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown expression class: %s"), *ExpressionClassName));
    }

    double PosX = 0.0, PosY = 0.0;
    Params->TryGetNumberField(TEXT("position_x"), PosX);
    Params->TryGetNumberField(TEXT("position_y"), PosY);

    UMaterialExpression* NewExpr = NewObject<UMaterialExpression>(Func, ExprClass);
    if (!NewExpr)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create expression object"));
    }

    NewExpr->Function = Func;
    NewExpr->MaterialExpressionEditorX = static_cast<int32>(PosX);
    NewExpr->MaterialExpressionEditorY = static_cast<int32>(PosY);

    Func->GetExpressionCollection().Expressions.Add(NewExpr);
    const int32 NewIndex = Func->GetExpressions().Num() - 1;

    NotifyMaterialFunctionModified(Func, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("expression_index"), NewIndex);
    Data->SetStringField(TEXT("class"), ExprClass->GetName());
    Data->SetStringField(TEXT("name"), NewExpr->GetName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialFunctionCommands::HandleConnectMfPins(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString FunctionPath;
    if (!Params->TryGetStringField(TEXT("function_path"), FunctionPath) || FunctionPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: function_path"));
    }

    double SourceIndexD = -1.0, DestIndexD = -1.0;
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
    UMaterialFunction* Func = LoadMfForEditing(FunctionPath, &ActiveEditor);
    if (!Func)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material function not found: %s"), *FunctionPath));
    }

    if (SourceIndex < 0 || SourceIndex >= Func->GetExpressions().Num())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("source_expression_index %d out of range (0..%d)"),
                SourceIndex, Func->GetExpressions().Num() - 1));
    }
    if (DestIndex < 0 || DestIndex >= Func->GetExpressions().Num())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("dest_expression_index %d out of range (0..%d)"),
                DestIndex, Func->GetExpressions().Num() - 1));
    }

    UMaterialExpression* SourceExpr = Func->GetExpressionCollection().Expressions[SourceIndex];
    UMaterialExpression* DestExpr = Func->GetExpressionCollection().Expressions[DestIndex];
    if (!SourceExpr || !DestExpr)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Source or dest expression is null"));
    }

    FExpressionInput* Input = DestExpr->GetInput(DestInputIndex);
    if (!Input)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("dest_input_index %d is invalid for expression %s"),
                DestInputIndex, *DestExpr->GetClass()->GetName()));
    }

    Input->Connect(SourceOutputIndex, SourceExpr);

    NotifyMaterialFunctionModified(Func, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("connected"), true);
    Data->SetNumberField(TEXT("source_expression_index"), SourceIndex);
    Data->SetNumberField(TEXT("dest_expression_index"), DestIndex);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMaterialFunctionCommands::HandleSetMfExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid params"));
    }

    FString FunctionPath;
    if (!Params->TryGetStringField(TEXT("function_path"), FunctionPath) || FunctionPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: function_path"));
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
    UMaterialFunction* Func = LoadMfForEditing(FunctionPath, &ActiveEditor);
    if (!Func)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material function not found: %s"), *FunctionPath));
    }

    if (ExprIndex < 0 || ExprIndex >= Func->GetExpressions().Num())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("expression_index %d out of range (0..%d)"),
                ExprIndex, Func->GetExpressions().Num() - 1));
    }

    UMaterialExpression* Expr = Func->GetExpressionCollection().Expressions[ExprIndex];
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

    // Handle UMaterialExpressionCustom
    if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr))
    {
        FString Code;
        if (Props->TryGetStringField(TEXT("code"), Code))
        {
            CustomExpr->Code = Code;
            Changed.Add(TEXT("code"));
        }

        FString OutputType;
        if (Props->TryGetStringField(TEXT("output_type"), OutputType))
        {
            if      (OutputType == TEXT("float") || OutputType == TEXT("float1")) { CustomExpr->OutputType = CMOT_Float1; }
            else if (OutputType == TEXT("float2"))                                { CustomExpr->OutputType = CMOT_Float2; }
            else if (OutputType == TEXT("float3"))                                { CustomExpr->OutputType = CMOT_Float3; }
            else if (OutputType == TEXT("float4"))                                { CustomExpr->OutputType = CMOT_Float4; }
            else
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown output_type: %s"), *OutputType));
            }
            Changed.Add(TEXT("output_type"));
        }

        const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
        if (Props->TryGetArrayField(TEXT("inputs"), InputsArr) && InputsArr)
        {
            CustomExpr->Inputs.Empty();
            for (const TSharedPtr<FJsonValue>& InputVal : *InputsArr)
            {
                const TSharedPtr<FJsonObject> InputObj = InputVal.IsValid() ? InputVal->AsObject() : nullptr;
                if (!InputObj.IsValid()) continue;
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

    // Handle FunctionInput
    if (UMaterialExpressionFunctionInput* InputExpr = Cast<UMaterialExpressionFunctionInput>(Expr))
    {
        FString InputName;
        if (Props->TryGetStringField(TEXT("input_name"), InputName))
        {
            InputExpr->InputName = FName(*InputName);
            Changed.Add(TEXT("input_name"));
        }

        FString InputType;
        if (Props->TryGetStringField(TEXT("input_type"), InputType))
        {
            if      (InputType == TEXT("scalar")  || InputType == TEXT("float"))  { InputExpr->InputType = FunctionInput_Scalar; }
            else if (InputType == TEXT("vector2") || InputType == TEXT("float2")) { InputExpr->InputType = FunctionInput_Vector2; }
            else if (InputType == TEXT("vector3") || InputType == TEXT("float3")) { InputExpr->InputType = FunctionInput_Vector3; }
            else if (InputType == TEXT("vector4") || InputType == TEXT("float4")) { InputExpr->InputType = FunctionInput_Vector4; }
            else if (InputType == TEXT("texture2d"))                              { InputExpr->InputType = FunctionInput_Texture2D; }
            else if (InputType == TEXT("texturecube"))                            { InputExpr->InputType = FunctionInput_TextureCube; }
            else if (InputType == TEXT("bool"))                                   { InputExpr->InputType = FunctionInput_StaticBool; }
            else
            {
                return FSmithUECommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown input_type: %s (use scalar/vector2/vector3/vector4/texture2d/texturecube/bool)"), *InputType));
            }
            Changed.Add(TEXT("input_type"));
        }

        double PreviewVal = 0.0;
        if (Props->TryGetNumberField(TEXT("preview_value"), PreviewVal))
        {
            InputExpr->PreviewValue = FVector4f(PreviewVal, PreviewVal, PreviewVal, PreviewVal);
            Changed.Add(TEXT("preview_value"));
        }
    }

    // Handle FunctionOutput
    if (UMaterialExpressionFunctionOutput* OutputExpr = Cast<UMaterialExpressionFunctionOutput>(Expr))
    {
        FString OutputName;
        if (Props->TryGetStringField(TEXT("output_name"), OutputName))
        {
            OutputExpr->OutputName = FName(*OutputName);
            Changed.Add(TEXT("output_name"));
        }
    }

    // Handle UMaterialExpressionConstant
    if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expr))
    {
        double R = 0.0;
        if (Props->TryGetNumberField(TEXT("value"), R))
        {
            ConstExpr->R = static_cast<float>(R);
            Changed.Add(TEXT("value"));
        }
    }

    // Handle UMaterialExpressionConstant3Vector
    if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expr))
    {
        double R = 0, G = 0, B = 0;
        if (Props->TryGetNumberField(TEXT("r"), R)) { Const3Expr->Constant.R = static_cast<float>(R); Changed.Add(TEXT("r")); }
        if (Props->TryGetNumberField(TEXT("g"), G)) { Const3Expr->Constant.G = static_cast<float>(G); Changed.Add(TEXT("g")); }
        if (Props->TryGetNumberField(TEXT("b"), B)) { Const3Expr->Constant.B = static_cast<float>(B); Changed.Add(TEXT("b")); }
    }

    // Handle SceneTexture ID
    FString SceneTexId;
    if (Props->TryGetStringField(TEXT("scene_texture_id"), SceneTexId))
    {
        FProperty* Prop = Expr->GetClass()->FindPropertyByName(TEXT("SceneTextureId"));
        if (Prop)
        {
            int32 TexId = -1;
            if      (SceneTexId == TEXT("PPI_SceneColor")          || SceneTexId == TEXT("scene_color"))            { TexId = PPI_SceneColor; }
            else if (SceneTexId == TEXT("PPI_SceneDepth")          || SceneTexId == TEXT("scene_depth"))            { TexId = PPI_SceneDepth; }
            else if (SceneTexId == TEXT("PPI_CustomDepth")         || SceneTexId == TEXT("custom_depth"))           { TexId = PPI_CustomDepth; }
            else if (SceneTexId == TEXT("PPI_CustomStencil")       || SceneTexId == TEXT("custom_stencil"))         { TexId = PPI_CustomStencil; }
            else if (SceneTexId == TEXT("PPI_PostProcessInput0")   || SceneTexId == TEXT("post_process_input_0"))   { TexId = PPI_PostProcessInput0; }
            else if (SceneTexId == TEXT("PPI_PostProcessInput1")   || SceneTexId == TEXT("post_process_input_1"))   { TexId = PPI_PostProcessInput1; }
            else if (SceneTexId == TEXT("PPI_BaseColor")           || SceneTexId == TEXT("base_color"))             { TexId = PPI_BaseColor; }
            else if (SceneTexId == TEXT("PPI_Metallic")            || SceneTexId == TEXT("metallic"))               { TexId = PPI_Metallic; }
            else if (SceneTexId == TEXT("PPI_Roughness")           || SceneTexId == TEXT("roughness"))              { TexId = PPI_Roughness; }
            else if (SceneTexId == TEXT("PPI_WorldNormal")         || SceneTexId == TEXT("world_normal"))           { TexId = PPI_WorldNormal; }
            else if (SceneTexId == TEXT("PPI_AmbientOcclusion")    || SceneTexId == TEXT("ambient_occlusion"))      { TexId = PPI_AmbientOcclusion; }
            else if (SceneTexId == TEXT("PPI_Velocity")            || SceneTexId == TEXT("velocity"))               { TexId = PPI_Velocity; }

            if (TexId >= 0)
            {
                void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
                if (ValuePtr)
                {
                    *static_cast<uint8*>(ValuePtr) = static_cast<uint8>(TexId);
                    Changed.Add(TEXT("scene_texture_id"));
                }
            }
        }
    }

    if (Changed.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No recognized properties were set. Check expression type and property names."));
    }

    NotifyMaterialFunctionModified(Func, ActiveEditor);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("function_path"), FunctionPath);
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
