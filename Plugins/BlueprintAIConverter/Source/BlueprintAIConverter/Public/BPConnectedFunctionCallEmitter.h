#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

class UFunction;

// 検証済みの接続を限定されたActor関数呼び出し文に変換する。
class BLUEPRINTAICONVERTER_API FBPConnectedFunctionCallEmitter
{
public:
    static bool Emit(const FBPGraphIR& GraphIR, const FGuid& CallNodeId,
        const UFunction* Function, FString& OutStatement,
        TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
