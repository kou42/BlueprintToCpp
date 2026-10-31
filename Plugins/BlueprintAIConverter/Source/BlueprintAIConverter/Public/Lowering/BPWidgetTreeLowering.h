#pragma once

#include "BPSemanticIR.h"

class UBlueprint;

class BLUEPRINTAICONVERTER_API FBPWidgetTreeLowering
{
public:
    // Ordinary Blueprints produce Kind=None. Unsupported widget data fails
    // atomically; no payload may be mistaken for an audited empty tree.
    static bool Extract(const UBlueprint* Blueprint, FBPSemanticWidgetTree& OutTree,
        TArray<FString>& OutDiagnostics);
};
