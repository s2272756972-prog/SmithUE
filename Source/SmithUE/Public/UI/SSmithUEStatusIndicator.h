// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateColor.h"

/**
 * Status bar indicator for SmithUE MCP connections.
 * - Green solid   : at least one active session
 * - Red solid     : no active sessions
 * - Green blinking: session recently registered (first 3 seconds)
 * Hover tooltip shows client names, session count, tool count.
 */
class SSmithUEStatusIndicator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmithUEStatusIndicator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetUpdateAvailable(bool bAvailable);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FSlateColor GetIndicatorColor() const;
	FText GetTooltipText() const;

	/** Seconds since last session-count change (drives blink). */
	float BlinkTimer = 0.f;
	bool bUpdateAvailable = false;
	float UpdateBlinkTimer = 0.f;
	int32 LastSessionCount = 0;

	/** Purge stale sessions every N seconds. */
	float PurgeTimer = 0.f;
};
