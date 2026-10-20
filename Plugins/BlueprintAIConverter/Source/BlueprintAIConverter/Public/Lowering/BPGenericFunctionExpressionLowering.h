#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

class UFunction;

// 純粋なネイティブ関数の戻り値を型付きSemantic Expressionへ変換する。
// 引数・出力引数がある呼び出しは、値の欠落を避けるため現段階では拒否する。
class BLUEPRINTAICONVERTER_API FBPGenericFunctionExpressionLowering
{
public:
    static bool Lower(const FBPGraphIR& GraphIR, const FGuid& CallNodeId,
        const UFunction* Function, FBPSemanticExpression& OutExpression,
        TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
