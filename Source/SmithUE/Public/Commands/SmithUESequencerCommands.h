// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUESequencerCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleSeqCreate(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSeqRead(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSeqAddBinding(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSeqAddTrack(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSeqAddKeyframe(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSeqSetRange(const TSharedPtr<FJsonObject>& Params);
};
