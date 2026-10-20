#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

// 呼び出し先オブジェクトの解決をGraph IRとSemantic IRの境界で一元化する。
// 現段階ではselfまたは明示的なオブジェクト変数参照のみを扱う。
class BLUEPRINTAICONVERTER_API FBPCallTargetResolver
{
public:
    static bool Resolve(const FBPGraphIR& GraphIR, const FGuid& CallNodeId,
        FBPSemanticExpression& OutTarget,
        TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
