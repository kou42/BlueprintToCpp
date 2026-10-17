#pragma once

#include "CoreMinimal.h"
#include "BPGeneratedCode.h"

// 生成ファイルをディスクへ書き込まず、Editor内で確認するためのプレビュー。
class FBPGeneratedCodePreview
{
public:
    static void Show(const FBPGeneratedActorFiles& Files,
        const FString& ClassName, const FString& ModuleApiMacro);
};
