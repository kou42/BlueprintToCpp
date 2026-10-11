#pragma once

#include "CoreMinimal.h"
#include "BPActorCodeGenerator.h"

// 生成コードの保存をプレビュー表示から分離する。
// 既存ファイルを上書きせず、二つのファイルの片方だけが残らないよう処理する。
class FBPGeneratedCodeSaver
{
public:
    // プロジェクト名と同名のゲームモジュールに限り、Public/Privateへ分割保存する。
    // モジュール名が異なるプロジェクトは明示的に失敗させる。
    static bool SaveToProjectModule(const FBPGraphIR& IR, const FString& ClassName,
        FString& OutError);

    static bool SaveNewFiles(const FBPGeneratedActorFiles& Files,
        const FString& Directory, FString& OutError);
};
