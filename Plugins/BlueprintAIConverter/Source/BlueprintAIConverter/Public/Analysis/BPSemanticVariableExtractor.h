#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

class UBlueprint;

/** Blueprint宣言変数をSemantic JSON向けに抽出する。既存Snapshot v1には影響しない。 */
class FBPSemanticVariableExtractor
{
public:
    static void Append(const UBlueprint* Blueprint, FBPSemanticIR& IR);
};
