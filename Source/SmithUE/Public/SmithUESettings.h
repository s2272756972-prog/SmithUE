// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SmithUESettings.generated.h"

/**
 * SmithUE plugin settings — configurable via Editor → Project Settings → Plugins → SmithUE.
 * Changes take effect after restarting the Unreal Editor.
 */
UCLASS(config=EditorPerProjectUserSettings, defaultconfig, meta=(DisplayName="SmithUE"))
class SMITHUE_API USmithUESettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USmithUESettings();

	/** Port the SmithUE HTTP server binds to on startup.
	 *  Override precedence (highest to lowest): command-line SmithUEhttpport= > SMITHUE_BIND_PORT env var > this setting.
	 *  Valid range: 1024–65535. Default: 13721. Requires Editor restart. */
	UPROPERTY(config, EditAnywhere, Category="Server",
		meta=(ClampMin=1024, ClampMax=65535,
			  ToolTip="HTTP port SmithUE listens on. Overridden by SMITHUE_BIND_PORT env var or -SmithUEhttpport= command line. Requires Editor restart."))
	int32 HttpBindPort = 13721;

	/** If false, the SmithUE HTTP server will not start automatically when the Editor launches.
	 *  Requires Editor restart. */
	UPROPERTY(config, EditAnywhere, Category="Server",
		meta=(ToolTip="Start the SmithUE HTTP server automatically on Editor launch. Requires Editor restart."))
	bool bAutoStartServer = true;

	/** How often (in seconds) SmithUE re-writes the portfile to keep it alive.
	 *  Valid range: 1.0–30.0 seconds. Default: 4.0. Requires Editor restart. */
	UPROPERTY(config, EditAnywhere, Category="Server",
		meta=(ClampMin=1.0f, ClampMax=30.0f,
			  ToolTip="Portfile heartbeat interval in seconds. Requires Editor restart."))
	float PortfileHeartbeatInterval = 4.0f;

	/** If false, SmithUE will not check for plugin updates on Editor startup. */
	UPROPERTY(config, EditAnywhere, Category="General",
		meta=(ToolTip="Check for SmithUE plugin updates automatically on Editor startup."))
	bool bCheckForUpdatesOnStartup = true;

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override  { return TEXT("SmithUE"); }
};
