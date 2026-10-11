#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

class FJsonObject;

/** Semantic IR専用のJSON v2変換。既存Snapshot v1の形式・出力先は変更しない。 */
class FBPSemanticJsonSerializer
{
public:
    static TSharedRef<FJsonObject> ToJson(const FBPSemanticIR& IR, const FString& EngineVersion);
    static bool Serialize(const FBPSemanticIR& IR, const FString& EngineVersion, FString& OutJson);
};
