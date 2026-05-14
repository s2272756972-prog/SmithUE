// Copyright 2026, 123dx-svg. MIT License.

#include "UI/SSmithUEStatusIndicator.h"

#include "Transport/SmithUEConnectionManager.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
	/** Shared circular brush (rounded box with radius = half size). */
	static FSlateBrush& GetCircleBrush()
	{
		static FSlateRoundedBoxBrush Brush(FLinearColor::White, 5.f);
		return Brush;
	}

	/** Read SmithUE plugin version from .uplugin descriptor (cached). */
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
	ChildSlot
	[
		SNew(SBox)
		.Padding(FMargin(4.f, 0.f))
		.VAlign(VAlign_Center)
		.ToolTipText_Raw(this, &SSmithUEStatusIndicator::GetTooltipText)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				// Circular colored dot
				SNew(SBox)
				.WidthOverride(10.f)
				.HeightOverride(10.f)
				[
					SNew(SImage)
					.Image(&GetCircleBrush())
					.ColorAndOpacity_Raw(this, &SSmithUEStatusIndicator::GetIndicatorColor)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("SmithUE v%s"), *GetPluginVersion())))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			]
		]
	];
}

void SSmithUEStatusIndicator::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Purge stale sessions periodically
	PurgeTimer += InDeltaTime;
	if (PurgeTimer >= 5.f)
	{
		PurgeTimer = 0.f;
		FSmithUEConnectionManager::Get().PurgeStale(20.0);
	}

	// Detect session count change to trigger blink
	const int32 CurrentCount = FSmithUEConnectionManager::Get().GetSessionCount();
	if (CurrentCount != LastSessionCount)
	{
		BlinkTimer = 3.f; // blink for 3 seconds on change
		LastSessionCount = CurrentCount;
	}

	if (BlinkTimer > 0.f)
	{
		BlinkTimer -= InDeltaTime;
	}
}

FSlateColor SSmithUEStatusIndicator::GetIndicatorColor() const
{
	const bool bHasSession = FSmithUEConnectionManager::Get().HasActiveSession();

	if (!bHasSession)
	{
		// Red — no connections
		return FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f, 1.f));
	}

	if (BlinkTimer > 0.f)
	{
		// Red-Yellow alternation — transitioning
		const float T = (FMath::Sin(BlinkTimer * 10.f) + 1.f) * 0.5f; // 0..1
		const FLinearColor Red(0.9f, 0.15f, 0.1f, 1.f);
		const FLinearColor Yellow(1.f, 0.85f, 0.1f, 1.f);
		return FSlateColor(FMath::Lerp(Red, Yellow, T));
	}

	// Solid green — stable connection
	return FSlateColor(FLinearColor(0.1f, 0.9f, 0.3f, 1.f));
}

FText SSmithUEStatusIndicator::GetTooltipText() const
{
	const TArray<FSmithUEConnectionManager::FClientSession> Sessions = FSmithUEConnectionManager::Get().GetSessions();
	const int32 ToolCount = FSmithUEToolRegistry::Get().GetAll().Num();

	FString Tip;
	Tip += FString::Printf(TEXT("SmithUE v%s\n"), *GetPluginVersion());
	Tip += FString::Printf(TEXT("─────────────────\n"));
	Tip += FString::Printf(TEXT("Connections: %d\n"), Sessions.Num());
	Tip += FString::Printf(TEXT("Tools: %d\n"), ToolCount);

	if (Sessions.Num() > 0)
	{
		Tip += TEXT("\nClients:\n");
		for (const FSmithUEConnectionManager::FClientSession& S : Sessions)
		{
			const FTimespan Elapsed = FDateTime::UtcNow() - S.ConnectedAt;
			const int32 Minutes = static_cast<int32>(Elapsed.GetTotalMinutes());
			Tip += FString::Printf(TEXT("  • %s  (%d min)\n"), *S.ClientName, Minutes);
		}
	}
	else
	{
		Tip += TEXT("\nNo AI tools connected.\nStart MCP Server to connect.");
	}

	return FText::FromString(Tip);
}
