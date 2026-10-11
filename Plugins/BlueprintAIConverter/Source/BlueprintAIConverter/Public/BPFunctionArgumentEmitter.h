#pragma once

#include "CoreMinimal.h"
#include "BPFunctionSignatureResolver.h"
#include "BPSemanticIR.h"

// 署名の順序に従い、検証済みの引数式をC++呼び出しの引数列へ変換する。
// 現時点ではGetActorLocation式のみを受理する限定実装。
class BLUEPRINTAICONVERTER_API FBPFunctionArgumentEmitter
{
public:
    static bool Emit(const FBPResolvedFunctionSignature& Signature,
        const TArray<FBPSemanticExpression>& Arguments, FString& OutArguments,
        TArray<FString>& OutDiagnostics);
};
