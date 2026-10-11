#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

// M1の最小Lowering。既存Analyzerの厳密な検証を再利用し、
// 検証されていないノードや接続をSemantic IRへ推測変換しない。
class BLUEPRINTAICONVERTER_API FBPSemanticLowering
{
public:
    static bool LowerPrintString(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR);
};
