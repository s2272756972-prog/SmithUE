// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUETextureCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "SmithUESettings.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/TextureFactory.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Async/Async.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "Factories/SoundFactory.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// ---------------------------------------------------------------------------
// Task State
// ---------------------------------------------------------------------------

namespace
{
	enum class ETextureTaskStatus : uint8
	{
		Processing,
		Completed,
		Failed
	};

	struct FTextureTask
	{
		FString TaskId;
		ETextureTaskStatus Status = ETextureTaskStatus::Processing;
		FString ErrorMessage;
		FString AssetPath;
		FString MaterialPath;
		FString ImageFilePath;
		int32 Width = 0;
		int32 Height = 0;
		bool bCreateMaterial = false;
		FString SavePath;
		FString AssetName;
	};

	struct FAudioTask
	{
		FString TaskId;
		ETextureTaskStatus Status = ETextureTaskStatus::Processing;
		FString ErrorMessage;
		FString AssetPath;
		FString AudioFilePath;
		FString SavePath;
		FString AssetName;
	};

	// In-memory task store (game thread only, no locking needed)
	TMap<FString, TSharedPtr<FTextureTask>> TaskStore;
	TMap<FString, TSharedPtr<FAudioTask>> AudioTaskStore;

	using FDownloadCompleteCallback = TFunction<void(const FString& FilePath, bool bSuccess, const FString& Error)>;

	void DownloadViaCurl(const FString& Url, const FString& OutputFilePath, FDownloadCompleteCallback&& Callback, const TArray<FString>& Headers = TArray<FString>())
	{
		TSharedRef<FDownloadCompleteCallback> CallbackRef = MakeShared<FDownloadCompleteCallback>(MoveTemp(Callback));
		TArray<FString> HeaderCopies = Headers;

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Url, OutputFilePath, HeaderCopies, CallbackRef]()
		{
			FString Args(TEXT("-s -L"));
			for (const FString& Header : HeaderCopies)
			{
				Args += FString::Printf(TEXT(" -H \"%s\""), *Header);
			}
			Args += FString::Printf(TEXT(" -o \"%s\" \"%s\""), *OutputFilePath, *Url);

			FProcHandle Proc = FPlatformProcess::CreateProc(TEXT("curl.exe"), *Args, false, true, true, nullptr, 0, nullptr, nullptr);
			if (!Proc.IsValid())
			{
				AsyncTask(ENamedThreads::GameThread, [CallbackRef]()
				{
					(*CallbackRef)(FString(), false, TEXT("Failed to launch curl.exe subprocess"));
				});
				return;
			}

			FPlatformProcess::WaitForProc(Proc);
			int32 ReturnCode = 0;
			FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
			FPlatformProcess::CloseProc(Proc);

			AsyncTask(ENamedThreads::GameThread, [OutputFilePath, ReturnCode, CallbackRef]()
			{
				if (ReturnCode != 0)
				{
					(*CallbackRef)(OutputFilePath, false, FString::Printf(TEXT("curl.exe exited with code %d"), ReturnCode));
					return;
				}
				if (!IFileManager::Get().FileExists(*OutputFilePath))
				{
					(*CallbackRef)(OutputFilePath, false, TEXT("curl completed but output file not found"));
					return;
				}
				const int64 FileSize = IFileManager::Get().FileSize(*OutputFilePath);
				if (FileSize < 500)
				{
					(*CallbackRef)(OutputFilePath, false, FString::Printf(TEXT("Downloaded file too small (%lld bytes) - likely an error response"), FileSize));
					return;
				}
				(*CallbackRef)(OutputFilePath, true, FString());
			});
		});
	}

	// ---------------------------------------------------------------------------
	// API Format Detection
	// ---------------------------------------------------------------------------

	enum class EApiFormat : uint8
	{
		DallE,       // OpenAI DALL-E /images/generations
		Imagen,      // Google Imagen /predict
		OpenAIChat,  // together.xyz / openrouter / chat/completions
		GoogleNative, // generativelanguage.googleapis.com (Gemini)
		Pollinations // free, no auth, returns raw image bytes
	};

	EApiFormat DetectApiFormat(const FString& Endpoint)
	{
		if (Endpoint.Contains(TEXT("pollinations.ai")))
		{
			return EApiFormat::Pollinations;
		}
		if (Endpoint.Contains(TEXT("images/generations")) || Endpoint.Contains(TEXT("api.openai.com/v1/images")))
		{
			return EApiFormat::DallE;
		}
		if (Endpoint.Contains(TEXT("imagen")) || Endpoint.Contains(TEXT("predict")))
		{
			return EApiFormat::Imagen;
		}
		if (Endpoint.Contains(TEXT("together.xyz")) || Endpoint.Contains(TEXT("openrouter.ai")) || Endpoint.Contains(TEXT("chat/completions")))
		{
			return EApiFormat::OpenAIChat;
		}
		return EApiFormat::GoogleNative;
	}

	// ---------------------------------------------------------------------------
	// Request Body Construction
	// ---------------------------------------------------------------------------

	FString BuildRequestBody(EApiFormat Format, const FString& Prompt, const FString& ModelName, const FString& AspectRatio)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

		switch (Format)
		{
		case EApiFormat::DallE:
		{
			if (!ModelName.IsEmpty()) { Payload->SetStringField(TEXT("model"), ModelName); }
			Payload->SetStringField(TEXT("prompt"), Prompt);
			Payload->SetNumberField(TEXT("n"), 1);
			Payload->SetStringField(TEXT("size"), AspectRatio.IsEmpty() ? TEXT("1024x1024") : AspectRatio);
			Payload->SetStringField(TEXT("response_format"), TEXT("b64_json"));
			break;
		}
		case EApiFormat::Imagen:
		{
			TArray<TSharedPtr<FJsonValue>> Instances;
			TSharedPtr<FJsonObject> Inst = MakeShared<FJsonObject>();
			Inst->SetStringField(TEXT("prompt"), Prompt);
			Instances.Add(MakeShared<FJsonValueObject>(Inst));
			Payload->SetArrayField(TEXT("instances"), Instances);

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetNumberField(TEXT("sampleCount"), 1);
			if (!AspectRatio.IsEmpty()) { Params->SetStringField(TEXT("aspectRatio"), AspectRatio); }
			Payload->SetObjectField(TEXT("parameters"), Params);
			break;
		}
		case EApiFormat::OpenAIChat:
		{
			if (!ModelName.IsEmpty()) { Payload->SetStringField(TEXT("model"), ModelName); }
			TArray<TSharedPtr<FJsonValue>> Messages;
			TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
			Msg->SetStringField(TEXT("role"), TEXT("user"));
			TArray<TSharedPtr<FJsonValue>> Content;
			TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("type"), TEXT("text"));
			TextPart->SetStringField(TEXT("text"), Prompt);
			Content.Add(MakeShared<FJsonValueObject>(TextPart));
			Msg->SetArrayField(TEXT("content"), Content);
			Messages.Add(MakeShared<FJsonValueObject>(Msg));
			Payload->SetArrayField(TEXT("messages"), Messages);
			break;
		}
		case EApiFormat::GoogleNative:
		{
			TArray<TSharedPtr<FJsonValue>> Contents;
			TSharedPtr<FJsonObject> ContentObj = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Parts;
			TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("text"), Prompt);
			Parts.Add(MakeShared<FJsonValueObject>(TextPart));
			ContentObj->SetArrayField(TEXT("parts"), Parts);
			Contents.Add(MakeShared<FJsonValueObject>(ContentObj));
			Payload->SetArrayField(TEXT("contents"), Contents);

			// Gemini image generation requires responseModalities
			TSharedPtr<FJsonObject> GenConfig = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Modalities;
			Modalities.Add(MakeShared<FJsonValueString>(TEXT("IMAGE")));
			GenConfig->SetArrayField(TEXT("responseModalities"), Modalities);
			Payload->SetObjectField(TEXT("generationConfig"), GenConfig);
			break;
		}
		case EApiFormat::Pollinations:
		{
			Payload->SetStringField(TEXT("prompt"), Prompt);
			int32 W = 1024;
			int32 H = 1024;
			if (!AspectRatio.IsEmpty())
			{
				FString Left;
				FString Right;
				if (AspectRatio.Split(TEXT("x"), &Left, &Right))
				{
					W = FCString::Atoi(*Left);
					H = FCString::Atoi(*Right);
				}
			}
			Payload->SetNumberField(TEXT("width"), W);
			Payload->SetNumberField(TEXT("height"), H);
			Payload->SetStringField(TEXT("model"), ModelName.IsEmpty() ? TEXT("flux") : ModelName);
			Payload->SetBoolField(TEXT("nologo"), true);
			Payload->SetBoolField(TEXT("enhance"), true);
			break;
		}
		}

		FString Body;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
		return Body;
	}

	// ---------------------------------------------------------------------------
	// Response Parsing — extract base64 image data
	// ---------------------------------------------------------------------------

	FString ExtractBase64FromResponse(EApiFormat Format, const TSharedPtr<FJsonObject>& Json)
	{
		switch (Format)
		{
		case EApiFormat::Pollinations:
			break;
		case EApiFormat::DallE:
		{
			const TArray<TSharedPtr<FJsonValue>>& Data = Json->GetArrayField(TEXT("data"));
			if (Data.Num() > 0)
			{
				TSharedPtr<FJsonObject> First = Data[0]->AsObject();
				if (First.IsValid()) { return First->GetStringField(TEXT("b64_json")); }
			}
			break;
		}
		case EApiFormat::Imagen:
		{
			const TArray<TSharedPtr<FJsonValue>>& Predictions = Json->GetArrayField(TEXT("predictions"));
			if (Predictions.Num() > 0)
			{
				TSharedPtr<FJsonObject> First = Predictions[0]->AsObject();
				if (First.IsValid())
				{
					// Try nested image object first
					TSharedPtr<FJsonObject> ImageObj = First->GetObjectField(TEXT("image"));
					if (ImageObj.IsValid())
					{
						for (const FString& Key : { FString(TEXT("imageBytes")), FString(TEXT("bytesBase64Encoded")), FString(TEXT("data")) })
						{
							if (ImageObj->HasField(Key)) { return ImageObj->GetStringField(Key); }
						}
					}
					// Try flat fields
					for (const FString& Key : { FString(TEXT("bytesBase64Encoded")), FString(TEXT("imageBytes")), FString(TEXT("data")) })
					{
						if (First->HasField(Key)) { return First->GetStringField(Key); }
					}
				}
			}
			break;
		}
		case EApiFormat::OpenAIChat:
		{
			const TArray<TSharedPtr<FJsonValue>>& Choices = Json->GetArrayField(TEXT("choices"));
			if (Choices.Num() > 0)
			{
				TSharedPtr<FJsonObject> Choice = Choices[0]->AsObject();
				if (Choice.IsValid())
				{
					TSharedPtr<FJsonObject> Message = Choice->GetObjectField(TEXT("message"));
					if (Message.IsValid())
					{
						const TArray<TSharedPtr<FJsonValue>>& Content = Message->GetArrayField(TEXT("content"));
						for (const TSharedPtr<FJsonValue>& Item : Content)
						{
							TSharedPtr<FJsonObject> Obj = Item->AsObject();
							if (!Obj.IsValid()) { continue; }
							FString Type = Obj->GetStringField(TEXT("type"));
							if (Type == TEXT("image_url"))
							{
								TSharedPtr<FJsonObject> UrlObj = Obj->GetObjectField(TEXT("image_url"));
								if (UrlObj.IsValid())
								{
									FString Url = UrlObj->GetStringField(TEXT("url"));
									int32 Comma = Url.Find(TEXT(","));
									if (Comma != INDEX_NONE) { return Url.RightChop(Comma + 1); }
								}
							}
						}
					}
				}
			}
			break;
		}
		case EApiFormat::GoogleNative:
		{
			const TArray<TSharedPtr<FJsonValue>>& Candidates = Json->GetArrayField(TEXT("candidates"));
			if (Candidates.Num() > 0)
			{
				TSharedPtr<FJsonObject> Candidate = Candidates[0]->AsObject();
				if (Candidate.IsValid())
				{
					TSharedPtr<FJsonObject> ContentObj = Candidate->GetObjectField(TEXT("content"));
					if (ContentObj.IsValid())
					{
						const TArray<TSharedPtr<FJsonValue>>& Parts = ContentObj->GetArrayField(TEXT("parts"));
						if (Parts.Num() > 0)
						{
							TSharedPtr<FJsonObject> Part = Parts[0]->AsObject();
							if (Part.IsValid())
							{
								TSharedPtr<FJsonObject> InlineData = Part->GetObjectField(TEXT("inlineData"));
								if (InlineData.IsValid()) { return InlineData->GetStringField(TEXT("data")); }
							}
						}
					}
				}
			}
			break;
		}
		}
		return FString();
	}

	// ---------------------------------------------------------------------------
	// Asset Import
	// ---------------------------------------------------------------------------

	bool ImportTextureAsset(const FString& PngFilePath, const FString& AssetName, const FString& PackagePath, FString& OutAssetPath, FString& OutError)
	{
		OutAssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
		if (UEditorAssetLibrary::DoesAssetExist(OutAssetPath))
		{
			OutError = FString::Printf(TEXT("Asset already exists: %s"), *OutAssetPath);
			return false;
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		UTextureFactory* Factory = NewObject<UTextureFactory>();
		Factory->SuppressImportOverwriteDialog();

		TArray<FString> Files;
		Files.Add(PngFilePath);
		TArray<UObject*> Imported = AssetToolsModule.Get().ImportAssets(Files, PackagePath, Factory, false);
		if (Imported.Num() > 0)
		{
			if (UTexture2D* Tex = Cast<UTexture2D>(Imported[0]))
			{
				Tex->CompressionSettings = TC_Default;
				Tex->SRGB = true;
				Tex->MarkPackageDirty();
				Tex->UpdateResource();
				OutAssetPath = Tex->GetPathName();
				return true;
			}
		}
		OutError = TEXT("Failed to import texture as UE asset");
		return false;
	}

	bool CreateMaterialFromTexture(const FString& TextureAssetPath, const FString& MaterialName, const FString& PackagePath, FString& OutMaterialPath, FString& OutError)
	{
		OutMaterialPath = FString::Printf(TEXT("%s/M_%s"), *PackagePath, *MaterialName);
		if (UEditorAssetLibrary::DoesAssetExist(OutMaterialPath))
		{
			// Already exists is non-fatal; just return the path
			return true;
		}

		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TextureAssetPath);
		if (!Texture) { OutError = TEXT("Cannot load texture for material creation"); return false; }

		UPackage* Package = CreatePackage(*OutMaterialPath);
		if (!Package) { OutError = TEXT("Failed to create material package"); return false; }

		UMaterialFactoryNew* MatFactory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(MatFactory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(*MaterialName),
			RF_Public | RF_Standalone, nullptr, GWarn));
		if (!Material) { OutError = TEXT("Failed to create material"); return false; }

		UMaterialExpressionTextureSample* TexExpr = NewObject<UMaterialExpressionTextureSample>(Material);
		TexExpr->Texture = Texture;
		TexExpr->SamplerType = SAMPLERTYPE_Color;
		Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(TexExpr);
		Material->GetEditorOnlyData()->BaseColor.Expression = TexExpr;
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Package->MarkPackageDirty();

		FAssetRegistryModule& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		Reg.AssetCreated(Material);
		return true;
	}

	// ---------------------------------------------------------------------------
	// HTTP Callback — processes the AI API response and finalizes the task
	// ---------------------------------------------------------------------------

	void OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, const FString& TaskId, EApiFormat Format)
	{
		TSharedPtr<FTextureTask>* TaskPtr = TaskStore.Find(TaskId);
		if (!TaskPtr || !TaskPtr->IsValid()) { return; }
		FTextureTask& Task = **TaskPtr;

		if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			Task.Status = ETextureTaskStatus::Failed;
			if (Response.IsValid())
			{
				Task.ErrorMessage = FString::Printf(TEXT("HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString().Left(300));
			}
			else
			{
				Task.ErrorMessage = TEXT("Request failed — no response");
			}
			return;
		}

		TArray<uint8> ImageBytes;
		if (Format == EApiFormat::Pollinations)
		{
			// Pollinations returns raw image bytes directly.
			ImageBytes = Response->GetContent();
			if (ImageBytes.Num() == 0)
			{
				Task.Status = ETextureTaskStatus::Failed;
				Task.ErrorMessage = TEXT("Empty image response from Pollinations");
				return;
			}
		}
		else
		{
			// Parse JSON response
			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
			{
				Task.Status = ETextureTaskStatus::Failed;
				Task.ErrorMessage = TEXT("Failed to parse API response JSON");
				return;
			}

			// Extract base64 image data
			FString Base64Data = ExtractBase64FromResponse(Format, Json);
			if (Base64Data.IsEmpty())
			{
				Task.Status = ETextureTaskStatus::Failed;
				Task.ErrorMessage = TEXT("No image data found in API response");
				return;
			}

			// Decode base64 → PNG bytes
			if (!FBase64::Decode(Base64Data, ImageBytes) || ImageBytes.Num() == 0)
			{
				Task.Status = ETextureTaskStatus::Failed;
				Task.ErrorMessage = TEXT("Failed to decode base64 image data");
				return;
			}
		}

		// Save PNG to disk
		FString Filename = FString::Printf(TEXT("GeneratedTextures/%s.png"), *Task.AssetName);
		FString FullPath = FPaths::ProjectSavedDir() / Filename;
		FString Dir = FPaths::GetPath(FullPath);
		if (!IFileManager::Get().DirectoryExists(*Dir)) { IFileManager::Get().MakeDirectory(*Dir, true); }
		if (!FFileHelper::SaveArrayToFile(ImageBytes, *FullPath))
		{
			Task.Status = ETextureTaskStatus::Failed;
			Task.ErrorMessage = TEXT("Failed to save PNG file to disk");
			return;
		}
		Task.ImageFilePath = FullPath;

		// Import as UTexture2D asset
		FString ImportError;
		if (!ImportTextureAsset(FullPath, Task.AssetName, Task.SavePath, Task.AssetPath, ImportError))
		{
			Task.Status = ETextureTaskStatus::Failed;
			Task.ErrorMessage = ImportError;
			return;
		}

		// Optional: create material
		if (Task.bCreateMaterial)
		{
			FString MatError;
			CreateMaterialFromTexture(Task.AssetPath, Task.AssetName, Task.SavePath, Task.MaterialPath, MatError);
			// Material creation failure is non-fatal
		}

		Task.Status = ETextureTaskStatus::Completed;
		UE_LOG(LogSmithUE, Log, TEXT("generate_texture: Task %s completed → %s"), *TaskId, *Task.AssetPath);
	}

	void OnAudioResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, const FString& TaskId)
	{
		TSharedPtr<FAudioTask>* TaskPtr = AudioTaskStore.Find(TaskId);
		if (!TaskPtr || !TaskPtr->IsValid()) { return; }
		FAudioTask& Task = **TaskPtr;

		if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			Task.Status = ETextureTaskStatus::Failed;
			Task.ErrorMessage = Response.IsValid()
				? FString::Printf(TEXT("HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString().Left(300))
				: TEXT("Request failed — no response");
			return;
		}

		// Raw audio bytes (mp3)
		const TArray<uint8>& AudioBytes = Response->GetContent();
		if (AudioBytes.Num() == 0)
		{
			Task.Status = ETextureTaskStatus::Failed;
			Task.ErrorMessage = TEXT("Empty audio response");
			return;
		}

		// Save mp3 to disk
		FString Filename = FString::Printf(TEXT("GeneratedAudio/%s.mp3"), *Task.AssetName);
		FString FullPath = FPaths::ProjectSavedDir() / Filename;
		FString Dir = FPaths::GetPath(FullPath);
		if (!IFileManager::Get().DirectoryExists(*Dir)) { IFileManager::Get().MakeDirectory(*Dir, true); }
		if (!FFileHelper::SaveArrayToFile(AudioBytes, *FullPath))
		{
			Task.Status = ETextureTaskStatus::Failed;
			Task.ErrorMessage = TEXT("Failed to save audio file to disk");
			return;
		}
		Task.AudioFilePath = FullPath;

		// Import as USoundWave asset
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		USoundFactory* Factory = NewObject<USoundFactory>();
		TArray<FString> Files;
		Files.Add(FullPath);
		TArray<UObject*> Imported = AssetToolsModule.Get().ImportAssets(Files, Task.SavePath, Factory, false);
		if (Imported.Num() > 0 && Imported[0])
		{
			Imported[0]->MarkPackageDirty();
			Task.AssetPath = Imported[0]->GetPathName();
			Task.Status = ETextureTaskStatus::Completed;
			UE_LOG(LogSmithUE, Log, TEXT("generate_audio: Task %s completed → %s"), *TaskId, *Task.AssetPath);
		}
		else
		{
			Task.Status = ETextureTaskStatus::Failed;
			Task.ErrorMessage = TEXT("Failed to import audio as USoundWave asset");
		}
	}
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUETextureCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(
		FSmithUEToolSchema(TEXT("generate_texture"), TEXT("Asset"),
			TEXT("Generate a texture from a text prompt using an AI image generation API. Uses Pollinations.ai (free, no API key) by default. Provide endpoint + api_key for DALL-E, Imagen, or other providers. Returns a task_id for polling."),
			{
				FSmithUEToolParam(TEXT("prompt"), TEXT("string"), TEXT("Text prompt describing the desired texture"), true),
				FSmithUEToolParam(TEXT("endpoint"), TEXT("string"), TEXT("AI API endpoint URL (default: https://image.pollinations.ai/; auto-detects Pollinations/DALL-E/Imagen/OpenAI/Google format)")),
				FSmithUEToolParam(TEXT("api_key"), TEXT("string"), TEXT("API authentication key (not needed for Pollinations.ai)")),
				FSmithUEToolParam(TEXT("save_path"), TEXT("string"), TEXT("Content Browser path for the asset (default: /Game/GeneratedTextures)")),
				FSmithUEToolParam(TEXT("asset_name"), TEXT("string"), TEXT("Custom asset name (default: auto-generated timestamp)")),
				FSmithUEToolParam(TEXT("model"), TEXT("string"), TEXT("Model name to pass to the API (e.g. dall-e-3, imagen-4.0)")),
				FSmithUEToolParam(TEXT("aspect_ratio"), TEXT("string"), TEXT("Image size/ratio (e.g. 1024x1024, 16:9)")),
				FSmithUEToolParam(TEXT("create_material"), TEXT("boolean"), TEXT("Auto-create a material with this texture (default: false)")),
			}),
		&HandleGenerateTexture);

	Registry.Register(FSmithUEToolSchema(TEXT("generate_audio"), TEXT("Asset"),
		TEXT("Generate a sound asset (USoundWave) from text using Pollinations.ai TTS. Requires a Pollinations API key (free at pollinations.ai). Set it in Project Settings > Plugins > SmithUE, or pass api_key directly."),
		{
			FSmithUEToolParam(TEXT("text"), TEXT("string"), TEXT("Text to synthesize into speech/audio"), true),
			FSmithUEToolParam(TEXT("api_key"), TEXT("string"), TEXT("Pollinations.ai API key. If omitted, reads from Project Settings > SmithUE > Pollinations Audio API Key.")),
			FSmithUEToolParam(TEXT("voice"), TEXT("string"), TEXT("Voice name: alloy, echo, fable, onyx, nova (default), shimmer")),
			FSmithUEToolParam(TEXT("model"), TEXT("string"), TEXT("TTS model (default: openai-tts)")),
			FSmithUEToolParam(TEXT("save_path"), TEXT("string"), TEXT("Content Browser path for the asset (default: /Game/GeneratedAudio)")),
			FSmithUEToolParam(TEXT("asset_name"), TEXT("string"), TEXT("Custom asset name (default: auto-generated)"))
		}),
		&HandleGenerateAudio);

	Registry.Register(
		FSmithUEToolSchema(TEXT("check_generation_task"), TEXT("Asset"),
			TEXT("Check the status of an asynchronous texture generation task"),
			{
				FSmithUEToolParam(TEXT("task_id"), TEXT("string"), TEXT("Task ID returned by generate_texture"), true),
			}),
		&HandleCheckGenerationTask);
}

// ---------------------------------------------------------------------------
// HandleGenerateTexture
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUETextureCommands::HandleGenerateTexture(const TSharedPtr<FJsonObject>& Params)
{
	FString Prompt = Params->GetStringField(TEXT("prompt"));

	if (Prompt.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("prompt is required")); }

	const USmithUESettings* Settings = USmithUESettings::Get();
	FString Endpoint = Params->GetStringField(TEXT("endpoint"));
	if (Endpoint.IsEmpty() && Settings)
	{
		Endpoint = Settings->DefaultImageEndpoint;
	}
	if (Endpoint.IsEmpty())
	{
		Endpoint = TEXT("https://image.pollinations.ai/");
	}

	FString ApiKey = Params->GetStringField(TEXT("api_key"));
	if (ApiKey.IsEmpty() && Settings)
	{
		ApiKey = Settings->DefaultApiKey;
	}

	FString SavePath = Params->GetStringField(TEXT("save_path"));
	if (SavePath.IsEmpty()) { SavePath = TEXT("/Game/GeneratedTextures"); }

	FString AssetName = Params->GetStringField(TEXT("asset_name"));
	if (AssetName.IsEmpty())
	{
		AssetName = FString::Printf(TEXT("GenTex_%s"), *FGuid::NewGuid().ToString().Left(8));
	}

	FString ModelName = Params->GetStringField(TEXT("model"));
	if (ModelName.IsEmpty() && Settings)
	{
		ModelName = Settings->DefaultModel;
	}
	FString AspectRatio = Params->GetStringField(TEXT("aspect_ratio"));
	bool bCreateMaterial = Params->HasField(TEXT("create_material")) && Params->GetBoolField(TEXT("create_material"));

	// Create task
	FString TaskId = FGuid::NewGuid().ToString();
	TSharedPtr<FTextureTask> Task = MakeShared<FTextureTask>();
	Task->TaskId = TaskId;
	Task->SavePath = SavePath;
	Task->AssetName = AssetName;
	Task->bCreateMaterial = bCreateMaterial;
	TaskStore.Add(TaskId, Task);

	// Detect API format and build request
	EApiFormat Format = DetectApiFormat(Endpoint);
	if (Format != EApiFormat::Pollinations && ApiKey.IsEmpty())
	{
		TaskStore.Remove(TaskId);
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("api_key is required for the selected image endpoint"));
	}
		FString RequestBody = BuildRequestBody(Format, Prompt, ModelName, AspectRatio);

		if (Format == EApiFormat::Pollinations)
		{
			// Pollinations CDN blocks UE's libcurl TLS fingerprint. Use curl.exe subprocess.
			FString EncodedPrompt = FGenericPlatformHttp::UrlEncode(Prompt);
			int32 W = 1024, H = 1024;
			if (!AspectRatio.IsEmpty())
		{
			FString Left, Right;
			if (AspectRatio.Split(TEXT("x"), &Left, &Right))
			{
				W = FCString::Atoi(*Left);
				H = FCString::Atoi(*Right);
			}
		}
		FString PollModel = ModelName.IsEmpty() ? TEXT("flux") : ModelName;
			FString PollUrl = FString::Printf(
				TEXT("https://image.pollinations.ai/prompt/%s?width=%d&height=%d&model=%s&nologo=true&enhance=true"),
				*EncodedPrompt, W, H, *PollModel);

			FString OutputPath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("GeneratedTextures/%s.png"), *AssetName);
			FString Dir = FPaths::GetPath(OutputPath);
			if (!IFileManager::Get().DirectoryExists(*Dir)) { IFileManager::Get().MakeDirectory(*Dir, true); }

			DownloadViaCurl(PollUrl, OutputPath, [TaskId](const FString& FilePath, bool bOk, const FString& Err)
			{
				TSharedPtr<FTextureTask>* TaskPtr = TaskStore.Find(TaskId);
				if (!TaskPtr || !TaskPtr->IsValid()) { return; }
				FTextureTask& Task = **TaskPtr;
				if (!bOk)
				{
					Task.Status = ETextureTaskStatus::Failed;
					Task.ErrorMessage = Err;
					return;
				}

				Task.ImageFilePath = FilePath;

				FString ImportError;
				if (!ImportTextureAsset(FilePath, Task.AssetName, Task.SavePath, Task.AssetPath, ImportError))
				{
					Task.Status = ETextureTaskStatus::Failed;
					Task.ErrorMessage = ImportError;
					return;
				}

				if (Task.bCreateMaterial)
				{
					FString MatError;
					CreateMaterialFromTexture(Task.AssetPath, Task.AssetName, Task.SavePath, Task.MaterialPath, MatError);
				}

				Task.Status = ETextureTaskStatus::Completed;
				UE_LOG(LogSmithUE, Log, TEXT("generate_texture: Task %s completed (curl) → %s"), *TaskId, *Task.AssetPath);
			});

			UE_LOG(LogSmithUE, Log, TEXT("generate_texture: Started task %s (Pollinations via curl)"), *TaskId);

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("task_id"), TaskId);
			Data->SetStringField(TEXT("status"), TEXT("processing"));
			return FSmithUECommonUtils::CreateSuccessResponse(Data);
		}

		// Fire HTTP request for non-Pollinations formats.
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetTimeout(300.0f);
		HttpRequest->SetVerb(TEXT("POST"));
		HttpRequest->SetURL(Endpoint);
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		bool bIsGoogle = (Format == EApiFormat::Imagen || Format == EApiFormat::GoogleNative);
		if (bIsGoogle)
		{
			HttpRequest->SetHeader(TEXT("x-goog-api-key"), ApiKey);
		}
		else
		{
			HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		}
		HttpRequest->SetContentAsString(RequestBody);

		HttpRequest->OnProcessRequestComplete().BindLambda(
			[TaskId, Format](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
			{
			OnApiResponseReceived(Req, Resp, bSuccess, TaskId, Format);
		});
	HttpRequest->ProcessRequest();

	UE_LOG(LogSmithUE, Log, TEXT("generate_texture: Started task %s (endpoint: %s)"), *TaskId, *Endpoint.Left(80));

	// Return task_id immediately
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	Data->SetStringField(TEXT("status"), TEXT("processing"));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleGenerateAudio
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUETextureCommands::HandleGenerateAudio(const TSharedPtr<FJsonObject>& Params)
{
	FString Text, ApiKey, Voice, Model, SavePath, AssetName;
	Params->TryGetStringField(TEXT("text"), Text);
	if (Text.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: text"));
	}

	Params->TryGetStringField(TEXT("api_key"), ApiKey);
	Params->TryGetStringField(TEXT("voice"), Voice);
	Params->TryGetStringField(TEXT("model"), Model);
	Params->TryGetStringField(TEXT("save_path"), SavePath);
	Params->TryGetStringField(TEXT("asset_name"), AssetName);

	// Fallback chain: param → settings → error
	const USmithUESettings* Settings = USmithUESettings::Get();
	if (ApiKey.IsEmpty() && Settings) { ApiKey = Settings->DefaultAudioApiKey; }
	if (ApiKey.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			TEXT("api_key required for audio generation. Get a free key at https://pollinations.ai and set it in Project Settings > Plugins > SmithUE > Pollinations Audio API Key."));
	}

	if (Voice.IsEmpty()) { Voice = TEXT("nova"); }
	if (Model.IsEmpty()) { Model = TEXT("openai-tts"); }
	if (SavePath.IsEmpty()) { SavePath = TEXT("/Game/GeneratedAudio"); }
	if (AssetName.IsEmpty())
	{
		AssetName = FString::Printf(TEXT("SW_Generated_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	}

	// Build Pollinations audio URL: GET /audio/{url-encoded-text}?voice=&model=
	FString EncodedText = FGenericPlatformHttp::UrlEncode(Text);
	FString Url = FString::Printf(TEXT("https://gen.pollinations.ai/audio/%s?voice=%s&model=%s"),
		*EncodedText, *Voice, *Model);

	// Create task
	FString TaskId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToUpper();
	TSharedPtr<FAudioTask> Task = MakeShared<FAudioTask>();
	Task->TaskId = TaskId;
	Task->SavePath = SavePath;
	Task->AssetName = AssetName;
	AudioTaskStore.Add(TaskId, Task);

		FString OutputPath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("GeneratedAudio/%s.mp3"), *AssetName);
		FString Dir = FPaths::GetPath(OutputPath);
		if (!IFileManager::Get().DirectoryExists(*Dir)) { IFileManager::Get().MakeDirectory(*Dir, true); }

		TArray<FString> Headers;
		Headers.Add(FString::Printf(TEXT("Authorization: Bearer %s"), *ApiKey));
		Headers.Add(TEXT("Accept: audio/mpeg"));
		DownloadViaCurl(Url, OutputPath, [TaskId](const FString& FilePath, bool bOk, const FString& Err)
		{
			TSharedPtr<FAudioTask>* TaskPtr = AudioTaskStore.Find(TaskId);
			if (!TaskPtr || !TaskPtr->IsValid()) { return; }
			FAudioTask& Task = **TaskPtr;
			if (!bOk)
			{
				Task.Status = ETextureTaskStatus::Failed;
				Task.ErrorMessage = Err;
				return;
			}

			Task.AudioFilePath = FilePath;

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			USoundFactory* Factory = NewObject<USoundFactory>();
			TArray<FString> Files;
			Files.Add(FilePath);
			TArray<UObject*> Imported = AssetToolsModule.Get().ImportAssets(Files, Task.SavePath, Factory, false);
			if (Imported.Num() > 0 && Imported[0])
			{
				Imported[0]->MarkPackageDirty();
				Task.AssetPath = Imported[0]->GetPathName();
				Task.Status = ETextureTaskStatus::Completed;
				UE_LOG(LogSmithUE, Log, TEXT("generate_audio: Task %s completed (curl) → %s"), *TaskId, *Task.AssetPath);
			}
			else
			{
				Task.Status = ETextureTaskStatus::Failed;
				Task.ErrorMessage = TEXT("Failed to import audio as USoundWave asset");
			}
		}, Headers);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	Data->SetStringField(TEXT("status"), TEXT("processing"));
	Data->SetStringField(TEXT("voice"), Voice);
	Data->SetStringField(TEXT("model"), Model);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleCheckGenerationTask
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUETextureCommands::HandleCheckGenerationTask(const TSharedPtr<FJsonObject>& Params)
{
	FString TaskId = Params->GetStringField(TEXT("task_id"));
	if (TaskId.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("task_id is required")); }

	TSharedPtr<FTextureTask>* TaskPtr = TaskStore.Find(TaskId);
	if (!TaskPtr || !TaskPtr->IsValid())
	{
		TSharedPtr<FAudioTask>* AudioTaskPtr = AudioTaskStore.Find(TaskId);
		if (!AudioTaskPtr || !AudioTaskPtr->IsValid()) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Task not found: %s"), *TaskId)); }

		const FAudioTask& AudioTask = **AudioTaskPtr;

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("task_id"), TaskId);

		switch (AudioTask.Status)
		{
		case ETextureTaskStatus::Processing:
			Data->SetStringField(TEXT("status"), TEXT("processing"));
			break;
		case ETextureTaskStatus::Completed:
			Data->SetStringField(TEXT("status"), TEXT("completed"));
			Data->SetStringField(TEXT("asset_path"), AudioTask.AssetPath);
			if (!AudioTask.AudioFilePath.IsEmpty()) { Data->SetStringField(TEXT("audio_file"), AudioTask.AudioFilePath); }
			break;
		case ETextureTaskStatus::Failed:
			Data->SetStringField(TEXT("status"), TEXT("failed"));
			Data->SetStringField(TEXT("error"), AudioTask.ErrorMessage);
			break;
		}

		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	const FTextureTask& Task = **TaskPtr;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);

	switch (Task.Status)
	{
	case ETextureTaskStatus::Processing:
		Data->SetStringField(TEXT("status"), TEXT("processing"));
		break;
	case ETextureTaskStatus::Completed:
		Data->SetStringField(TEXT("status"), TEXT("completed"));
		Data->SetStringField(TEXT("asset_path"), Task.AssetPath);
		if (!Task.MaterialPath.IsEmpty()) { Data->SetStringField(TEXT("material_path"), Task.MaterialPath); }
		if (!Task.ImageFilePath.IsEmpty()) { Data->SetStringField(TEXT("image_file"), Task.ImageFilePath); }
		break;
	case ETextureTaskStatus::Failed:
		Data->SetStringField(TEXT("status"), TEXT("failed"));
		Data->SetStringField(TEXT("error"), Task.ErrorMessage);
		break;
	}

	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
