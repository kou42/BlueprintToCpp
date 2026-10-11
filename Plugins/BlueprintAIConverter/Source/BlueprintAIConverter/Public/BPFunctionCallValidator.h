#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPFunctionSignatureResolver.h"

// Graph IRのCallFunctionノードと実際のUFunction署名を突き合わせる。
// 既定値の式変換はまだ行わず、署名に不整合があれば必ず失敗させる。
class BLUEPRINTAICONVERTER_API FBPFunctionCallValidator
{
public:
    static bool Validate(const FBPIRNode& Node, const UFunction* Function,
        FBPResolvedFunctionSignature& OutSignature, TArray<FString>& OutDiagnostics);
};
