#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// 既存UCLASSの宣言を参照するためのC++風プレビュー。ビルド可能なソース生成とは区別する。
class BLUEPRINTAICONVERTER_API FBPReflectedClassPreview
{
public:
    static bool Generate(const FBPSemanticClassInfo& Info, FString& OutText, FString& OutError);
};
