#pragma once
#include "CoreMinimal.h"
#include "BPSemanticIR.h"
class UClass;
class BLUEPRINTAICONVERTER_API FBPGenericFunctionExpressionEmitter
{
public:
    static bool Emit(const FBPSemanticExpression& Expression, const UClass* OwnerClass,
        FString& OutCpp, TArray<FString>& OutDiagnostics);
};
