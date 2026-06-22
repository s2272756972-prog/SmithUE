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

TSharedRef<IDetailCustomization> FSmithUESettingsCustomization::MakeInstance()
{
	return MakeShareable(new FSmithUESettingsCustomization);
}

void FSmithUESettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
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
}
