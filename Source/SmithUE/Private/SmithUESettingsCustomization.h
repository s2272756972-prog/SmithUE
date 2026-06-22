// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FSmithUESettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
