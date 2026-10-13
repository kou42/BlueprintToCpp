#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

class UBlueprint;

// M1の最小Lowering。既存Analyzerの厳密な検証を再利用し、
// 検証されていないノードや接続をSemantic IRへ推測変換しない。
class BLUEPRINTAICONVERTER_API FBPSemanticLowering
{
public:
    static bool LowerPrintString(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR);

    // 実Blueprintを入力にする経路では、対応済みメンバー変数を同じSemantic IRへ取り込む。
    // Graph IRだけではCDO値を検証できないため、従来オーバーロードの拒否動作は維持する。
    static bool LowerPrintString(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);
};
