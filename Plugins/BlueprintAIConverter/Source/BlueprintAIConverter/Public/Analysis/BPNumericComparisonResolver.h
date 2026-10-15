#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

struct FBPNumericComparison
{
    FName FunctionName;
    FString FunctionOwnerPath;
    FString OperatorToken;
    EBPSemanticTypeKind OperandType = EBPSemanticTypeKind::Unresolved;
};

// 許可した比較関数を実際のUFunction署名と照合し、LoweringとGeneratorで共有する。
class BLUEPRINTAICONVERTER_API FBPNumericComparisonResolver
{
public:
    static bool Resolve(const FString& FunctionOwnerPath, FName FunctionName,
        FBPNumericComparison& OutComparison, TArray<FString>& OutDiagnostics);

    static bool MatchesPinType(FName Category, FName SubCategory,
        EBPSemanticTypeKind ExpectedType);

    static bool CanonicalizeLiteral(EBPSemanticTypeKind Type,
        const FString& Value, FString& OutCanonicalValue);
};
