#pragma once

#include "BPSemanticIR.h"

class UBlueprint;
class UFunction;

// Blueprint自身が定義したUFunctionだけを反射し、既存Loweringの関数本体は変更しない。
class FBPSemanticFunctionExtractor
{
public:
    static void Append(const UBlueprint* Blueprint, FBPSemanticIR& IR);

#if WITH_DEV_AUTOMATION_TESTS
    // 本番のInterface照合と同一の判定をAutomation Testから検証する。
    static bool MatchesInterfaceSignatureForTest(const UFunction* Implementation, const UFunction* Declaration);
#endif
};
