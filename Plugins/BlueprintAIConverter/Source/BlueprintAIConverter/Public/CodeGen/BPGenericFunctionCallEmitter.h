#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

class UFunction;
class UClass;

// 反射で安全性を検証できる通常のネイティブインスタンス関数を文として出力する。
// 特殊なBlueprint thunk、出力引数、戻り値利用は対象外とする。
class BLUEPRINTAICONVERTER_API FBPGenericFunctionCallEmitter
{
public:
    static bool Emit(const FBPGraphIR& GraphIR, const FGuid& CallNodeId,
        const UFunction* Function, const UClass* OwnerClass,
        FString& OutStatement, TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
