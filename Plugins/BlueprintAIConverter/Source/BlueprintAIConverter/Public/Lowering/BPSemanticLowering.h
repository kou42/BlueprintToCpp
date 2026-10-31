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

    // BeginPlay／通常関数／Custom Eventからboolリテラルまたは反射検証済み
    // 数値比較のBranchを通り、両枝でPrintStringまたはリテラル入力のnative void Callを呼ぶLowering。
    // ネストBranchはbool条件に限定。通常関数では全経路が同じ末尾Resultへ到達するvoid形状を検証する。
    static bool LowerBranch(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

    // BeginPlay／Function Entry／Custom Event -> Sequence -> 複数のvoid呼び出しを、
    // 出力ピン順にLoweringする。PrintStringと、リテラル入力のstatic/self native callが対象。
    static bool LowerSequence(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

    // BeginPlay／Custom EventのForLoop -> PrintString、または通常関数の
    // ForLoop -> 反射検証済みvoid関数呼び出しを、両端を含むfor文へLoweringする。
    static bool LowerForLoop(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR);

    // メンバー変数を参照するループでは、Blueprintから反射済みの変数定義を取得する。
    static bool LowerForLoop(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);

private:
    static bool LowerImpl(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
        FBPSemanticIR& OutIR);
};
