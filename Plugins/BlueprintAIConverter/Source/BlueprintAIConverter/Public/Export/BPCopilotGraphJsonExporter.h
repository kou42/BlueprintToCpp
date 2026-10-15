#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"

struct FBPSemanticIR;

// Semantic JSON v2を変更せず、生グラフ情報をCopilot向けの別ファイルとして保存する。
class BLUEPRINTAICONVERTER_API FBPCopilotGraphJsonExporter
{
public:
    static bool Export(const FBPGraphIR& IR, const FString& EngineVersion,
        FString& OutPath, FString& OutError,
        const FBPSemanticIR* SemanticIR = nullptr);
};
