// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

/**
 * Shared asset property path resolver.
 * Moved out of SmithUEAssetCommands.cpp anonymous namespace so that
 * SmithUEAssetAuditCommands.cpp can reference it across translation units.
 */
struct FAssetResolvedPropertyPath
{
    FEditPropertyChain Chain;
    FProperty*         TopLevelProperty = nullptr;
    FProperty*         LeafProperty     = nullptr;
    void*              LeafValuePtr     = nullptr;
};

/**
 * Resolve a dotted / indexed property path on a UObject.
 * Supports "Prop", "Struct.Nested", "Array[0]", "Array[0].Field".
 * Returns true on success, false with OutError describing the failure.
 */
bool ResolveAssetObjectPropertyPath(
    UObject*                      Object,
    const FString&                PropertyPath,
    FAssetResolvedPropertyPath&   OutResolved,
    FString&                      OutError);
