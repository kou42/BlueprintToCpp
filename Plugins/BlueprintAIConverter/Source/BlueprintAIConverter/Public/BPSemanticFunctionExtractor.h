#pragma once

#include "BPSemanticIR.h"

class UBlueprint;

// Blueprint自身が定義したUFunctionだけを反射し、既存Loweringの関数本体は変更しない。
class FBPSemanticFunctionExtractor
{
public:
    static void Append(const UBlueprint* Blueprint, FBPSemanticIR& IR);
};
