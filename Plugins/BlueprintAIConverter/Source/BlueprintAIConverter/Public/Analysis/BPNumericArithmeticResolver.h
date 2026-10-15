#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

struct FBPNumericArithmeticOperation
{
    FName FunctionName;
    FString FunctionOwnerPath;
    EBPSemanticTypeKind OperandType = EBPSemanticTypeKind::Unresolved;
};

// 許可した四則演算を実際のUFunction署名と照合し、Blueprint固有の除算動作も保持する。
class BLUEPRINTAICONVERTER_API FBPNumericArithmeticResolver
{
public:
    static bool Resolve(const FString& FunctionOwnerPath, FName FunctionName,
        FBPNumericArithmeticOperation& OutOperation,
        TArray<FString>& OutDiagnostics);
};
