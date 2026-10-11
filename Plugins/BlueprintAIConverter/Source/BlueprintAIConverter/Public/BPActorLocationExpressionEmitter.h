#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// 純粋なGetActorLocation式だけをC++式へ変換する限定Emitter。
// 既存のPrintString専用Generatorを変更せず、後続の式ツリー生成に利用する。
class BLUEPRINTAICONVERTER_API FBPActorLocationExpressionEmitter
{
public:
    static bool Emit(const FBPSemanticExpression& Expression, FString& OutCpp,
        TArray<FString>& OutDiagnostics);
};
