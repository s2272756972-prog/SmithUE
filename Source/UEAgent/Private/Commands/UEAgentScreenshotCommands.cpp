// Copyright 2026, 123dx-svg. MIT License.
#include "Commands/UEAgentScreenshotCommands.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "ToolRegistry/UEAgentToolSchema.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Commands/UEAgentEditorStateUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "RenderingThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "UEAgentModule.h"

void FUEAgentScreenshotCommands::RegisterTools(FUEAgentToolRegistry& Registry)
{
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("take_viewport_screenshot"),
            TEXT("Viewport"),
            TEXT("Capture the active editor viewport as a PNG file"),
            {
                FUEAgentToolParam(TEXT("path"), TEXT("string"), TEXT("Full file path for the PNG output (e.g. C:/temp/shot.png)"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleTakeViewportScreenshot(Params); });
}

TSharedPtr<FJsonObject> FUEAgentScreenshotCommands::HandleTakeViewportScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    // PIE and viewport guards
    if (UEAgentEditorState::IsInPIE())
        return UEAgentEditorState::CreatePIEErrorResponse();

    FEditorViewportClient* Client = UEAgentEditorState::GetActiveViewportClient();
    if (!Client)
        return UEAgentEditorState::CreateNoViewportErrorResponse();

    FViewport* Viewport = Client->Viewport;
    if (!Viewport)
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Viewport pointer is null"));

    // Validate path parameter
    FString FilePath;
    if (!Params->TryGetStringField(TEXT("path"), FilePath) || FilePath.IsEmpty())
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Parameter 'path' is required and must be a non-empty string"));

    if (!FilePath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Parameter 'path' must end with .png"));

    FString Directory = FPaths::GetPath(FilePath);
    if (!FPaths::DirectoryExists(Directory))
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Directory does not exist: %s"), *Directory));

    // Note if caller provided width/height (not supported in v1)
    bool bHasCustomDimensions = Params->HasField(TEXT("width")) || Params->HasField(TEXT("height"));

    // Force a full synchronous scene render into the viewport buffer.
    // Invalidate marks dirty, Draw(false) executes the full render pipeline
    // (FSceneViewFamily → SceneRenderer → overlays), FlushRenderingCommands
    // waits for the render thread to complete.
    Client->Invalidate();
    Viewport->Draw(false);
    FlushRenderingCommands();

    // Read pixels from viewport
    int32 Width = Viewport->GetSizeXY().X;
    int32 Height = Viewport->GetSizeXY().Y;

    if (Width == 0 || Height == 0)
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Viewport has zero dimensions"));

    TArray<FColor> Pixels;
    if (!Viewport->ReadPixels(Pixels) || Pixels.Num() == 0)
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Failed to read pixels from viewport"));

    // Fix alpha channel: viewport back buffer renders scene with A=0 (unused).
    // Without this, 32-bit PNG encodes pixels as transparent → appears white.
    for (FColor& Pixel : Pixels)
    {
        Pixel.A = 255;
    }

    // Encode to PNG via IImageWrapper
    IImageWrapperModule& ImageWrapperModule =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper =
        ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid())
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Failed to create PNG image wrapper"));

    ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
        Width, Height, ERGBFormat::BGRA, 8);

    const TArray64<uint8>& PngData = ImageWrapper->GetCompressed();
    if (PngData.Num() == 0)
        return FUEAgentCommonUtils::CreateErrorResponse(TEXT("PNG compression produced empty output"));

    if (!FFileHelper::SaveArrayToFile(PngData, *FilePath))
        return FUEAgentCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to write file: %s"), *FilePath));

    UE_LOG(LogUEAgent, Log, TEXT("take_viewport_screenshot: saved %s (%dx%d, %lld bytes)"),
        *FilePath, Width, Height, (int64)PngData.Num());

    // Build success response
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("file_path"), FilePath);
    Data->SetNumberField(TEXT("width"), Width);
    Data->SetNumberField(TEXT("height"), Height);
    Data->SetNumberField(TEXT("size_bytes"), (double)PngData.Num());

    if (bHasCustomDimensions)
        Data->SetStringField(TEXT("note"), TEXT("Custom width/height dimensions are not supported in v1; captured at current viewport size"));

    return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}
