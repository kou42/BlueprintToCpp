#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

class UClass;
class UBlueprint;

// UClassリフレクションからBlueprint/Native共通のクラス情報を抽出する。
class BLUEPRINTAICONVERTER_API FBPSemanticClassExtractor
{
public:
    static bool Extract(const UClass* Class, FBPSemanticClassInfo& OutInfo);
    static bool Append(const UBlueprint* Blueprint, FBPSemanticIR& IR);
};
