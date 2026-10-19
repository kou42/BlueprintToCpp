#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// 純粋なGetActorLocation式だけをC++式へ変換する限定Emitter。
// 検証済みの共通式文Generatorから、後続の式ツリー生成に利用する。
class BLUEPRINTAICONVERTER_API FBPActorLocationExpressionEmitter
{
public:
    static bool Emit(const FBPSemanticExpression& Expression, FString& OutCpp,
        TArray<FString>& OutDiagnostics);
};
