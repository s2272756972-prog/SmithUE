#pragma once

#include "CoreMinimal.h"

class UEAGENT_API IUEAgentTransport
{
public:
	virtual ~IUEAgentTransport() = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsRunning() const = 0;
};
