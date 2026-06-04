// Copyright 2026, 123dx-svg. MIT License.

#include "UI/SSmithUEStatusIndicator.h"

#include "Interfaces/IPluginManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/SlateColor.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Json.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformApplicationMisc.h"

namespace
{
	static FSlateBrush& GetCircleBrush()
	{
		static FSlateRoundedBoxBrush Brush(FLinearColor::White, 5.f);
		return Brush;
	}

	static FString GetPluginVersion()
	{
		static FString CachedVersion;
		if (CachedVersion.IsEmpty())
		{
			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE"));
			if (Plugin.IsValid())
			{
				CachedVersion = Plugin->GetDescriptor().VersionName;
			}
			if (CachedVersion.IsEmpty())
			{
				CachedVersion = TEXT("?");
			}
		}
		return CachedVersion;
	}
}

void SSmithUEStatusIndicator::Construct(const FArguments& InArgs)
{
	ToolCount = InArgs._ToolCount;

	// Fire first poll immediately
	PollReady();

	// Register 5-second ticker
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float) -> bool
		{
			PollReady();
			return true; // keep ticking
		}),
		5.0f
	);

	ChildSlot
	[
		SNew(SBox)
		.Padding(FMargin(4.f, 0.f))
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			// --- Dot ---
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(10.f)
				.HeightOverride(10.f)
				.ToolTipText_Raw(this, &SSmithUEStatusIndicator::GetTooltipText)
				[
					SNew(SImage)
					.Image(&GetCircleBrush())
					.ColorAndOpacity_Raw(this, &SSmithUEStatusIndicator::GetDotColor)
				]
			]

			// --- Label ---
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("SmithUE")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			]

			// --- Copy CLI button (visible only when ready) ---
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Visibility_Raw(this, &SSmithUEStatusIndicator::GetCopyButtonVisibility)
				.OnClicked_Raw(this, &SSmithUEStatusIndicator::OnCopyCliCommand)
				.ToolTipText(FText::FromString(TEXT("Copy: npx smithue-cli exec ping {}")))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Copy CLI Command")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
		]
	];
}

SSmithUEStatusIndicator::~SSmithUEStatusIndicator()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

int32 SSmithUEStatusIndicator::ReadPortFile()
{
	// %LOCALAPPDATA%\.smithue\*.port
	const FString Dir = FPaths::Combine(
		FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")),
		TEXT(".smithue")
	);

	TArray<FString> PortFiles;
	IFileManager::Get().FindFiles(PortFiles, *(Dir / TEXT("*.port")), true, false);

	for (const FString& FileName : PortFiles)
	{
		FString Content;
		if (FFileHelper::LoadFileToString(Content, *(Dir / FileName)))
		{
			TSharedPtr<FJsonObject> Obj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
			if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
			{
				int32 Port = 0;
				if (Obj->TryGetNumberField(TEXT("port"), Port) && Port > 0)
				{
					return Port;
				}
			}
		}
	}
	return 0;
}

void SSmithUEStatusIndicator::PollReady()
{
	CurrentPort = ReadPortFile();
	if (CurrentPort <= 0)
	{
		bIsReady = false;
		return;
	}

	const FString Url = FString::Printf(TEXT("http://127.0.0.1:%d/ready"), CurrentPort);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	Req->SetTimeout(0.5f);
	Req->OnProcessRequestComplete().BindRaw(this, &SSmithUEStatusIndicator::OnReadyResponse);
	Req->ProcessRequest();
}

void SSmithUEStatusIndicator::OnReadyResponse(FHttpRequestPtr /*Request*/, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		bIsReady = false;
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		bIsReady = false;
		return;
	}

	// Expect JSON: {"ready": true}
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		bool bReadyField = false;
		Obj->TryGetBoolField(TEXT("ready"), bReadyField);
		bIsReady = bReadyField;
	}
	else
	{
		bIsReady = false;
	}
}

FSlateColor SSmithUEStatusIndicator::GetDotColor() const
{
	if (bIsReady)
	{
		// Green — server ready
		return FSlateColor(FLinearColor(0.1f, 0.9f, 0.3f, 1.f));
	}
	// Gray — server unreachable / not ready
	return FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f, 1.f));
}

FText SSmithUEStatusIndicator::GetTooltipText() const
{
	FString Tip;
	Tip += FString::Printf(TEXT("SmithUE v%s\n"), *GetPluginVersion());
	Tip += TEXT("─────────────────\n");
	if (CurrentPort > 0)
	{
		Tip += FString::Printf(TEXT("HTTP port : %d\n"), CurrentPort);
	}
	else
	{
		Tip += TEXT("HTTP port : (no port file)\n");
	}
	Tip += FString::Printf(TEXT("Tools     : %d\n"), ToolCount);
	Tip += TEXT("─────────────────\n");
	Tip += bIsReady ? TEXT("Status    : ready") : TEXT("Status    : down");
	return FText::FromString(Tip);
}

EVisibility SSmithUEStatusIndicator::GetCopyButtonVisibility() const
{
	return bIsReady ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SSmithUEStatusIndicator::OnCopyCliCommand()
{
	FPlatformApplicationMisc::ClipboardCopy(TEXT("npx smithue-cli exec ping {}"));
	return FReply::Handled();
}
