#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

class UFunction;

// UFunctionの署名を反映した結果。失敗時には部分的な署名を利用させない。
struct FBPResolvedFunctionSignature
{
    FName FunctionName;
    FString OwnerClassPath;
    bool bStatic = false;
    bool bConst = false;
    FBPSemanticType ReturnType;
    TArray<FName> ParameterNames;
    TArray<FBPSemanticType> ParameterTypes;
    TArray<bool> OutputParameters;
};

class BLUEPRINTAICONVERTER_API FBPFunctionSignatureResolver
{
public:
    static bool Resolve(const UFunction* Function, FBPResolvedFunctionSignature& OutSignature,
        TArray<FString>& OutDiagnostics);
};
