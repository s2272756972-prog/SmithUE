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
}

void SSmithUEStatusIndicator::Construct(const FArguments& InArgs)
{
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

			// --- Clickable Dot ---
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.OnClicked_Raw(this, &SSmithUEStatusIndicator::OnDotClicked)
				.ToolTipText_Raw(this, &SSmithUEStatusIndicator::GetTooltipText)
				.ContentPadding(FMargin(0.f))
				[
					SNew(SBox)
					.WidthOverride(10.f)
					.HeightOverride(10.f)
					[
						SNew(SImage)
						.Image(&GetCircleBrush())
						.ColorAndOpacity_Raw(this, &SSmithUEStatusIndicator::GetDotColor)
					]
				]
			]

			// --- Label ---
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("SmithUE")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
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
					// Read project_name from portfile
					FString ProjName;
					if (Obj->TryGetStringField(TEXT("project_name"), ProjName))
					{
						ProjectName = ProjName;
					}
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
		bPIEActive = false;
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
		bPIEActive = false;
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		bIsReady = false;
		bPIEActive = false;
		return;
	}

	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		bool bReadyField = false;
		Obj->TryGetBoolField(TEXT("ready"), bReadyField);
		bIsReady = bReadyField;

		bool bPIEField = false;
		Obj->TryGetBoolField(TEXT("pie_active"), bPIEField);
		bPIEActive = bPIEField;
	}
	else
	{
		bIsReady = false;
		bPIEActive = false;
	}
}

FSlateColor SSmithUEStatusIndicator::GetDotColor() const
{
	if (!bIsReady)
	{
		// Gray — server unreachable / not ready
		return FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f, 1.f));
	}

	if (bPIEActive)
	{
		// Yellow — ready but PIE running (some commands locked)
		return FSlateColor(FLinearColor(0.95f, 0.75f, 0.1f, 1.f));
	}

	// Green — fully ready
	return FSlateColor(FLinearColor(0.1f, 0.9f, 0.3f, 1.f));
}

FText SSmithUEStatusIndicator::GetTooltipText() const
{
	if (!bIsReady)
	{
		return FText::FromString(TEXT("Not ready"));
	}

	FString Tip;
	if (!ProjectName.IsEmpty())
	{
		Tip += ProjectName + TEXT(" | ");
	}
	Tip += FString::Printf(TEXT("Port: %d"), CurrentPort);

	if (bPIEActive)
	{
		Tip += TEXT("\nPIE: active");
	}

	Tip += TEXT("\n(click to copy port)");
	return FText::FromString(Tip);
}

FReply SSmithUEStatusIndicator::OnDotClicked()
{
	if (CurrentPort > 0)
	{
		FPlatformApplicationMisc::ClipboardCopy(*FString::Printf(TEXT("%d"), CurrentPort));
	}
	return FReply::Handled();
}
