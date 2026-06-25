// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEUMGCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"

namespace SmithUEUMG
{
    static UClass* ResolveWidgetClass(const FString& ClassName)
    {
        FString Lower = ClassName.ToLower();
        if (Lower == TEXT("button")) return UButton::StaticClass();
        if (Lower == TEXT("textblock") || Lower == TEXT("text")) return UTextBlock::StaticClass();
        if (Lower == TEXT("image")) return UImage::StaticClass();
        if (Lower == TEXT("canvaspanel") || Lower == TEXT("canvas")) return UCanvasPanel::StaticClass();
        if (Lower == TEXT("verticalbox") || Lower == TEXT("vbox")) return UVerticalBox::StaticClass();
        if (Lower == TEXT("horizontalbox") || Lower == TEXT("hbox")) return UHorizontalBox::StaticClass();
        if (Lower == TEXT("overlay")) return UOverlay::StaticClass();
        if (Lower == TEXT("scrollbox")) return UScrollBox::StaticClass();
        if (Lower == TEXT("border")) return UBorder::StaticClass();
        if (Lower == TEXT("sizebox")) return USizeBox::StaticClass();
        return UUserWidget::StaticClass();
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEUMGCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_widget_blueprint"),
            TEXT("UMG"),
            TEXT("Create a new Widget Blueprint asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Widget Blueprint asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content path, e.g. /Game/UI"), true),
                FSmithUEToolParam(TEXT("rootWidget"), TEXT("string"), TEXT("Root widget class (default: CanvasPanel)"), false, TEXT("CanvasPanel"))
            }),
        &HandleCreateWidgetBlueprint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("read_widget_blueprint"),
            TEXT("UMG"),
            TEXT("Read the widget tree of an existing Widget Blueprint"),
            {
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Full asset path, e.g. /Game/UI/WBP_MyWidget"), true)
            }),
        &HandleReadWidgetBlueprint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_widget"),
            TEXT("UMG"),
            TEXT("Add a widget to an existing Widget Blueprint tree. The parent must be a panel that accepts children, and the widget tree must already exist."),
            {
                FSmithUEToolParam(TEXT("blueprint"), TEXT("string"), TEXT("Full asset path to the Widget Blueprint"), true),
                FSmithUEToolParam(TEXT("widgetClass"), TEXT("string"), TEXT("Widget class: Button, TextBlock, Image, CanvasPanel, VerticalBox, HorizontalBox, Overlay, ScrollBox, Border, SizeBox"), true),
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Name for the new widget"), true),
                FSmithUEToolParam(TEXT("parent"), TEXT("string"), TEXT("Optional parent widget name; defaults to root panel"), false)
            }),
        &HandleAddWidget);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_widget_property"),
            TEXT("UMG"),
            TEXT("Set a property on a widget inside a Widget Blueprint via reflection"),
            {
                FSmithUEToolParam(TEXT("blueprint"), TEXT("string"), TEXT("Full asset path to the Widget Blueprint"), true),
                FSmithUEToolParam(TEXT("widget"), TEXT("string"), TEXT("Widget name inside the blueprint"), true),
                FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Property name"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value as string (imported via property reflection)"), true)
            }),
        &HandleSetWidgetProperty);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEUMGCommands::HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, Path, RootWidgetType;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    if (!Params->TryGetStringField(TEXT("rootWidget"), RootWidgetType) || RootWidgetType.IsEmpty())
    {
        RootWidgetType = TEXT("CanvasPanel");
    }

    if (Name.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'name' is required"));
    }
    if (Path.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'path' is required"));
    }

    FString FullPath = Path / Name;
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(),
        Package,
        FName(*Name),
        BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),
        UWidgetBlueprintGeneratedClass::StaticClass()
    );

    UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(BP);
    if (!WidgetBP)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create WidgetBlueprint"));
    }

    // Set root widget
    UClass* RootClass = SmithUEUMG::ResolveWidgetClass(RootWidgetType);
    if (RootClass && WidgetBP->WidgetTree)
    {
        WidgetBP->WidgetTree->Modify();
        UWidget* RootWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(TEXT("RootPanel")));
        if (RootWidget)
        {
            WidgetBP->WidgetTree->RootWidget = RootWidget;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
    FAssetRegistryModule::AssetCreated(WidgetBP);
    WidgetBP->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), WidgetBP->GetPathName());
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("rootWidget"), RootWidgetType);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEUMGCommands::HandleReadWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString Path;
    Params->TryGetStringField(TEXT("path"), Path);

    if (Path.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'path' is required"));
    }

    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *Path);
    if (!WidgetBP)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *Path));
    }

    // Recursive widget serializer
    TFunction<TSharedPtr<FJsonObject>(UWidget*)> SerializeWidget;
    SerializeWidget = [&](UWidget* W) -> TSharedPtr<FJsonObject>
    {
        if (!W) return nullptr;

        TSharedPtr<FJsonObject> WidgetObj = MakeShared<FJsonObject>();
        WidgetObj->SetStringField(TEXT("name"), W->GetName());
        WidgetObj->SetStringField(TEXT("class"), W->GetClass()->GetName());

        if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
        {
            TArray<TSharedPtr<FJsonValue>> Children;
            for (int32 i = 0; i < Panel->GetChildrenCount(); i++)
            {
                UWidget* Child = Panel->GetChildAt(i);
                TSharedPtr<FJsonObject> ChildObj = SerializeWidget(Child);
                if (ChildObj.IsValid())
                {
                    Children.Add(MakeShared<FJsonValueObject>(ChildObj));
                }
            }
            if (Children.Num() > 0)
            {
                WidgetObj->SetArrayField(TEXT("children"), Children);
            }
        }

        return WidgetObj;
    };

    TArray<TSharedPtr<FJsonValue>> WidgetsArray;
    if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget)
    {
        TSharedPtr<FJsonObject> TreeObj = SerializeWidget(WidgetBP->WidgetTree->RootWidget);
        if (TreeObj.IsValid())
        {
            WidgetsArray.Add(MakeShared<FJsonValueObject>(TreeObj));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), WidgetBP->GetName());
    Data->SetStringField(TEXT("path"), WidgetBP->GetPathName());
    Data->SetArrayField(TEXT("widgets"), WidgetsArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEUMGCommands::HandleAddWidget(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, WidgetClassName, WidgetName, ParentName;
    Params->TryGetStringField(TEXT("blueprint"), BlueprintPath);
    Params->TryGetStringField(TEXT("widgetClass"), WidgetClassName);
    Params->TryGetStringField(TEXT("name"), WidgetName);
    Params->TryGetStringField(TEXT("parent"), ParentName);

    if (BlueprintPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'blueprint' is required"));
    }
    if (WidgetClassName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'widgetClass' is required"));
    }
    if (WidgetName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'name' is required"));
    }

    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *BlueprintPath);
    if (!WidgetBP)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *BlueprintPath));
    }

    UClass* WClass = SmithUEUMG::ResolveWidgetClass(WidgetClassName);
    if (!WClass)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown widget class: %s"), *WidgetClassName));
    }

    UWidgetTree* Tree = WidgetBP->WidgetTree;
    if (!Tree)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No widget tree found"));
    }

    Tree->Modify();

    UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WClass, FName(*WidgetName));
    if (!NewWidget)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to construct widget"));
    }

    // Find parent panel or use root
    UPanelWidget* ParentPanel = nullptr;
    if (!ParentName.IsEmpty())
    {
        ParentPanel = Cast<UPanelWidget>(Tree->FindWidget(FName(*ParentName)));
    }
    if (!ParentPanel)
    {
        ParentPanel = Cast<UPanelWidget>(Tree->RootWidget);
    }

    if (ParentPanel)
    {
        UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
        if (!Slot)
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("Parent panel cannot accept children"));
        }
    }
    else
    {
        Tree->RootWidget = NewWidget;
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
    WidgetBP->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("widget"), WidgetName);
    Data->SetStringField(TEXT("class"), WClass->GetName());
    Data->SetStringField(TEXT("parent"), ParentPanel ? ParentPanel->GetName() : TEXT("root"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEUMGCommands::HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, WidgetName, PropertyName, Value;
    Params->TryGetStringField(TEXT("blueprint"), BlueprintPath);
    Params->TryGetStringField(TEXT("widget"), WidgetName);
    Params->TryGetStringField(TEXT("property"), PropertyName);
    Params->TryGetStringField(TEXT("value"), Value);

    if (BlueprintPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'blueprint' is required"));
    }
    if (WidgetName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'widget' is required"));
    }
    if (PropertyName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'property' is required"));
    }

    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *BlueprintPath);
    if (!WidgetBP)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *BlueprintPath));
    }

    UWidget* Widget = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
    if (!Widget)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
    }

    FProperty* Prop = FindFProperty<FProperty>(Widget->GetClass(), *PropertyName);
    if (!Prop)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property not found: %s"), *PropertyName));
    }

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
    const TCHAR* Result = Prop->ImportText_Direct(*Value, ValuePtr, Widget, PPF_None);
    if (!Result)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s' to '%s'"), *PropertyName, *Value));
    }

    WidgetBP->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("result"), TEXT("property set"));
    Data->SetStringField(TEXT("widget"), WidgetName);
    Data->SetStringField(TEXT("property"), PropertyName);
    Data->SetStringField(TEXT("value"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
