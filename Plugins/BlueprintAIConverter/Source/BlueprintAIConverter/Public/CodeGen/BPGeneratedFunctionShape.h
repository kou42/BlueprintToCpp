#pragma once

#include "CoreMinimal.h"

// Semantic IR由来の関数と検証済みNative overrideを、同じC++出力単位に正規化する。
// Declarationはアクセス指定子を含むクラス内宣言、Definitionはクラス修飾済みの署名、
// Prologueは検証済みの親関数呼び出しなど、関数本体より前に置く処理を保持する。
// 関数本体そのものはSemantic Statement Emitterの責務とし、ここでは推測しない。
struct FBPGeneratedFunctionShape
{
    FString Declaration;
    FString Definition;
    FString Prologue;
};
