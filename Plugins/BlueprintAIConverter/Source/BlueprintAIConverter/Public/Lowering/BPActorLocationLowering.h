#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

// Phase 2の限定Lowering。GetActorLocationの純粋な呼び出しだけを検証して
// Semantic IRの式に変換する。未使用の出力や不明な接続を推測で処理しない。
class BLUEPRINTAICONVERTER_API FBPActorLocationLowering
{
public:
    static bool LowerGetActorLocation(const FBPGraphIR& GraphIR,
        const FGuid& CallNodeId, FBPSemanticExpression& OutExpression,
        TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
