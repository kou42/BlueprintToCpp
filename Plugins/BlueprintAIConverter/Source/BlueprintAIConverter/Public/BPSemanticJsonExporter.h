#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// 既存Snapshot JSON v1とは独立したSemantic JSON v2の保存機能。
class FBPSemanticJsonExporter
{
public:
    static bool Export(const FBPSemanticIR& IR, const FString& EngineVersion, FString& OutPath, FString& OutError);
};
