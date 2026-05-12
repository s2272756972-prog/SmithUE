#pragma once

#include "CoreMinimal.h"

class SMITHUE_API ISmithUETransport
{
public:
	virtual ~ISmithUETransport() = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsRunning() const = 0;
};
