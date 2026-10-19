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
    // 対応済みのグラフ形状を判定し、通常のSemantic Preview経路へ振り分ける。
    static bool Lower(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

    static bool LowerPrintString(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR);

    // 実Blueprintを入力にする経路では、対応済みメンバー変数を同じSemantic IRへ取り込む。
    // Graph IRだけではCDO値を検証できないため、従来オーバーロードの拒否動作は維持する。
    static bool LowerPrintString(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

    // BeginPlayでGetActorLocationの結果をK2_SetActorLocationへ渡す限定縦断Lowering。
    static bool LowerActorLocation(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

    // BeginPlayからboolリテラルまたは反射検証済み数値比較のBranchを通り、
    // 両枝でPrintStringを呼ぶ限定縦断Lowering。
    static bool LowerBranch(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

    // BeginPlay -> ForLoop -> PrintString の限定形状を、両端を含むfor文へLoweringする。
    static bool LowerForLoop(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR);

    // メンバー変数を参照するループでは、Blueprintから反射済みの変数定義を取得する。
    static bool LowerForLoop(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);
};
