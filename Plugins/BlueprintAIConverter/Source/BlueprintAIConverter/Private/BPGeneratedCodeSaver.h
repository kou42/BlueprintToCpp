#pragma once

#include "CoreMinimal.h"
#include "BPActorCodeGenerator.h"

// 生成コードの保存をプレビュー表示から分離する。
// 既存ファイルを上書きせず、二つのファイルの片方だけが残らないよう処理する。
class FBPGeneratedCodeSaver
{
public:
    static bool SaveNewFiles(const FBPGeneratedActorFiles& Files,
        const FString& Directory, FString& OutError);
};
