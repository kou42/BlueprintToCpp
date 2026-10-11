#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

// 入力ピンに接続されたGetActorLocationの戻り値をSemantic式として解決する。
// 一般の式ツリーはまだ扱わず、未対応の接続は診断付きで拒否する。
class BLUEPRINTAICONVERTER_API FBPConnectedActorLocationLowering
{
public:
    static bool LowerInput(const FBPGraphIR& GraphIR, const FGuid& TargetNodeId,
        const FGuid& TargetPinId, FBPSemanticExpression& OutExpression,
        TArray<FBPSemanticDiagnostic>& OutDiagnostics);
};
