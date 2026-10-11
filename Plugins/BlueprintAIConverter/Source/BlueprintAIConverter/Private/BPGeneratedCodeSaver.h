#pragma once

#include "CoreMinimal.h"
#include "BPActorCodeGenerator.h"

// 生成コードの保存をプレビュー表示から分離する。
// 既存ファイルを上書きせず、二つのファイルの片方だけが残らないよう処理する。
class FBPGeneratedCodeSaver
{
public:
    // 既存の二つのディレクトリへ分割保存する。上書きと片方だけの保存を防止。
    static bool SaveNewFilesSplit(const FBPGeneratedActorFiles& Files,
        const FString& HeaderDirectory, const FString& SourceDirectory, FString& OutError);

    static bool SaveNewFiles(const FBPGeneratedActorFiles& Files,
        const FString& Directory, FString& OutError);
};
