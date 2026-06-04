// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateColor.h"

class IHttpRequest;
class IHttpResponse;

using FHttpRequestPtr = TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>;
using FHttpResponsePtr = TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>;

/**
 * Status bar indicator for SmithUE CLI connectivity.
 * - Green dot : HTTP /ready returned {ready:true}
 * - Gray dot  : server unreachable or not ready
 * Polls 127.0.0.1:{port}/ready every 5 seconds.
 * Port is read from %LOCALAPPDATA%\.smithue\*.port file.
 * Tooltip shows version, port, tool count.
 * "Copy CLI Command" button visible when server is ready.
 */
class SSmithUEStatusIndicator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmithUEStatusIndicator)
		: _ToolCount(0)
	{}
		SLATE_ARGUMENT(int32, ToolCount)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSmithUEStatusIndicator() override;

private:
	/** Read port from %LOCALAPPDATA%\.smithue\*.port — returns 0 if not found. */
	int32 ReadPortFile();

	/** Fire HTTP GET 127.0.0.1:{Port}/ready. Called by ticker. */
	void PollReady();

	/** HTTP response callback. */
	void OnReadyResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);

	FSlateColor GetDotColor() const;
	FText GetTooltipText() const;
	EVisibility GetCopyButtonVisibility() const;
	FReply OnCopyCliCommand();

	/** Ticker handle — cleaned up in destructor. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Tool count passed at construction time. */
	int32 ToolCount = 0;

	/** Last discovered port (0 = none). */
	int32 CurrentPort = 0;

	/** True when last /ready check returned {ready:true}. */
	bool bIsReady = false;
};
