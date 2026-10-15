#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPFunctionSignatureResolver.h"
#include "BPSemanticIR.h"

// 検証済みの関数署名に沿って、接続された入力ピンからC++引数列を組み立てる。
// 対象は現時点ではGetActorLocation由来のFVector入力のみ。
class BLUEPRINTAICONVERTER_API FBPConnectedFunctionArguments
{
public:
    static bool Emit(const FBPGraphIR& GraphIR, const FGuid& CallNodeId,
        const UFunction* Function, FString& OutArguments,
        TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
