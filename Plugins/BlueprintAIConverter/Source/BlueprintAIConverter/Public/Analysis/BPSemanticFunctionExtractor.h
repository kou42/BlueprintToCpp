#pragma once

#include "BPSemanticIR.h"

class UBlueprint;
class UFunction;

// Blueprint自身が定義したUFunctionだけを反射し、既存Loweringの関数本体は変更しない。
class FBPSemanticFunctionExtractor
{
public:
    static void Append(const UBlueprint* Blueprint, FBPSemanticIR& IR);
    static EBPSemanticFunctionVisibility ResolveVisibility(const UFunction* Function);

    // Interface候補を名前だけで判定せず、Reflection上の宣言が一致するか確認する。
    // Automation Testからも同じ判定規則を直接検証できるよう公開する。
    static bool HasMatchingInterfaceSignature(
        const UFunction* Implementation, const UFunction* Declaration);
};
