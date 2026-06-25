// Copyright 2026, 123dx-svg. MIT License.

#include "SmithUESettingsCustomization.h"

#include "SmithUESettings.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Utils/SmithUECliChecker.h"
#include "Utils/SmithUEUpdateChecker.h"
#include "Widgets/Input/SButton.h"

TSharedRef<IDetailCustomization> FSmithUESettingsCustomization::MakeInstance()
{
	return MakeShareable(new FSmithUESettingsCustomization);
}

void FSmithUESettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Trigger a CLI check on first panel open if no check has run yet
	if (FSmithUECliChecker::GetLastCheckTime().GetTicks() == 0
		&& !FSmithUECliChecker::IsCheckInFlight())
	{
		FSmithUECliChecker::CheckCliEnvironment();
	}

	IDetailCategoryBuilder& AICat = DetailBuilder.EditCategory(TEXT("AI Generation"));

	DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USmithUESettings, PollinationsGuideUrl)));

	AICat.AddCustomRow(FText::FromString(TEXT("Pollinations Links")))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Image Generation (free, no key): ")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("https://pollinations.ai")))
				.ToolTipText(FText::FromString(TEXT("Open Pollinations.ai in browser - image generation is free, no registration needed")))
				.OnNavigate_Lambda([](){ FPlatformProcess::LaunchURL(TEXT("https://pollinations.ai"), nullptr, nullptr); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Audio Generation (free key required): ")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("https://pollinations.ai - Get Free API Key")))
				.ToolTipText(FText::FromString(TEXT("Open Pollinations.ai to register and get your free API key for audio generation")))
				.OnNavigate_Lambda([](){ FPlatformProcess::LaunchURL(TEXT("https://pollinations.ai"), nullptr, nullptr); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("API Documentation: ")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("https://gen.pollinations.ai/docs")))
				.ToolTipText(FText::FromString(TEXT("Open Pollinations API documentation - see available models, parameters, and endpoints")))
				.OnNavigate_Lambda([](){ FPlatformProcess::LaunchURL(TEXT("https://gen.pollinations.ai/docs"), nullptr, nullptr); })
			]
		]
	];

	// ---- Status & Updates category ----
	IDetailCategoryBuilder& StatusCat = DetailBuilder.EditCategory(TEXT("Status & Updates"));

	StatusCat.AddCustomRow(FText::FromString(TEXT("Plugin Update")))
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		// Row 1: plugin update status text + Releases hyperlink
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([]() -> FText {
					if (FSmithUEUpdateChecker::IsUpdateAvailable())
					{
						return FText::FromString(FString::Printf(
							TEXT("SmithUE \u6709\u65b0\u7248\u672c\u53ef\u7528 (\u5f53\u524d v%s)  "),
							*FSmithUEUpdateChecker::GetCurrentVersion()));
					}
					return FText::FromString(FString::Printf(
						TEXT("\u63d2\u4ef6\u5df2\u662f\u6700\u65b0 (v%s)  "),
						*FSmithUEUpdateChecker::GetCurrentVersion()));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("GitHub Releases")))
				.ToolTipText(FText::FromString(TEXT("Open SmithUE releases page")))
				.OnNavigate_Lambda([](){
					FPlatformProcess::LaunchURL(
						TEXT("https://github.com/123dx-svg/SmithUE/releases"),
						nullptr, nullptr);
				})
			]
		]

		// Row 2: "Restart to Update" button -- visible only when update available
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Restart to Update")))
				.ToolTipText(FText::FromString(TEXT("Pull latest changes and restart editor")))
				.Visibility_Lambda([]() -> EVisibility {
					return FSmithUEUpdateChecker::IsUpdateAvailable()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.OnClicked_Lambda([]() -> FReply {
					FSmithUEUpdateChecker::ExecuteUpdate();
					return FReply::Handled();
				})
			]
		]

		// Row 3: CLI environment status (live via pull)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
		[
			SNew(STextBlock)
			.Text_Lambda([]() -> FText {
				if (!FSmithUECliChecker::GetLastCheckTime().GetTicks())
				{
					return FText::FromString(TEXT("CLI \u73af\u5883: \u672a\u68c0\u6d4b \u2014 \u70b9\u51fb\u300c\u91cd\u65b0\u68c0\u6d4b\u300d"));
				}
				const FString StateStr = []{
					switch (FSmithUECliChecker::GetState())
					{
						case ECliState::NoNode:       return TEXT("\u26a0 Node.js \u672a\u627e\u5230");
						case ECliState::NotInstalled: return TEXT("\u26a0 smithue-cli \u672a\u5b89\u88c5");
						case ECliState::Outdated:     return TEXT("\u2191 smithue-cli \u9700\u8981\u5347\u7ea7");
						case ECliState::Ready:        return TEXT("\u2713 Ready");
						default:                      return TEXT("Unknown");
					}
				}();
				const FDateTime T = FSmithUECliChecker::GetLastCheckTime();
				return FText::FromString(FString::Printf(
					TEXT("CLI \u73af\u5883: node v%s \u00b7 npm %s \u00b7 smithue-cli %s \u00b7 %s \u00b7 \u4e0a\u6b21\u68c0\u6d4b %02d:%02d:%02d"),
					*FSmithUECliChecker::GetNodeVersion(),
					*FSmithUECliChecker::GetNpmVersion(),
					*FSmithUECliChecker::GetCliVersion(),
					*StateStr,
					T.GetHour(), T.GetMinute(), T.GetSecond()));
			})
		]

		// Row 4: Re-check + Install/Update buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("\u91cd\u65b0\u68c0\u6d4b / Re-check")))
				.IsEnabled_Lambda([]() -> bool {
					return !FSmithUECliChecker::IsCheckInFlight()
						&& !FSmithUECliChecker::IsInstallInFlight();
				})
				.OnClicked_Lambda([]() -> FReply {
					FSmithUECliChecker::CheckCliEnvironment();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text_Lambda([]() -> FText {
					// During an install the button becomes a Cancel control.
					if (FSmithUECliChecker::IsInstallInFlight())
					{
						return FText::FromString(TEXT("\u53d6\u6d88")); // 取消
					}
					// While a check is running, show a non-actionable progress label.
					if (FSmithUECliChecker::IsCheckInFlight())
					{
						return FText::FromString(TEXT("\u5904\u7406\u4e2d\u2026")); // 处理中…
					}
					switch (FSmithUECliChecker::GetState())
					{
						case ECliState::NotInstalled: return FText::FromString(TEXT("\u5b89\u88c5 smithue-cli"));        // 安装 smithue-cli
						case ECliState::Outdated:     return FText::FromString(TEXT("\u5347\u7ea7\u5230\u6700\u65b0"));   // 升级到最新
						case ECliState::Ready:        return FText::FromString(TEXT("\u5df2\u662f\u6700\u65b0 \u2713"));  // 已是最新 ✓
						case ECliState::NoNode:       return FText::FromString(TEXT("\u9700\u5148\u5b89\u88c5 Node.js")); // 需先安装 Node.js
						default:                      return FText::FromString(TEXT("smithue-cli"));
					}
				})
				.ToolTipText_Lambda([]() -> FText {
					switch (FSmithUECliChecker::GetState())
					{
						case ECliState::Ready:  return FText::FromString(TEXT("smithue-cli is up to date \u2014 no action needed"));
						case ECliState::NoNode: return FText::FromString(TEXT("Install Node.js first (see the link below)"));
						default:                return FText::FromString(TEXT("Run: npm i -g smithue-cli@latest"));
					}
				})
				.IsEnabled_Lambda([]() -> bool {
					// While installing, the button is the Cancel control and is always clickable.
					if (FSmithUECliChecker::IsInstallInFlight()) { return true; }
					// Otherwise actionable ONLY when something actually needs installing or upgrading.
					const ECliState S = FSmithUECliChecker::GetState();
					return (S == ECliState::NotInstalled || S == ECliState::Outdated)
						&& !FSmithUECliChecker::IsCheckInFlight();
				})
				.OnClicked_Lambda([]() -> FReply {
					if (FSmithUECliChecker::IsInstallInFlight())
					{
						FSmithUECliChecker::CancelCliInstall();
					}
					else
					{
						FSmithUECliChecker::ExecuteCliInstall();
					}
					return FReply::Handled();
				})
			]
		]

		// Row 5: nodejs.org link -- visible only when Node is missing
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("\u5b89\u88c5 Node.js (nodejs.org)")))
				.ToolTipText(FText::FromString(TEXT("Download and install Node.js (required for npm and smithue-cli)")))
				.Visibility_Lambda([]() -> EVisibility {
					return (FSmithUECliChecker::GetState() == ECliState::NoNode
							&& FSmithUECliChecker::GetLastCheckTime().GetTicks() != 0)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.OnNavigate_Lambda([](){
					FPlatformProcess::LaunchURL(TEXT("https://nodejs.org"), nullptr, nullptr);
				})
			]
		]
	];
}
