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

// 宣言・定義の組み立てを入力元に依存させない共通Emitter。
// BodyはSemantic Statement Emitterが生成したC++、または検証済みNativeの空本体。
// 未検証のC++断片を安全化する機能ではないため、呼び出し側で必ず検証する。
struct FBPFunctionDefinitionEmitter
{
    static FString RenderBodyPrefix(const FBPGeneratedFunctionShape& Shape)
    {
        return Shape.Prologue;
    }

    static FString RenderDefinition(const FBPGeneratedFunctionShape& Shape, const FString& Body)
    {
        return FString::Printf(TEXT("\\n%s\\n{\\n%s%s}\\n"),
            *Shape.Definition, *RenderBodyPrefix(Shape), *Body);
    }
};
