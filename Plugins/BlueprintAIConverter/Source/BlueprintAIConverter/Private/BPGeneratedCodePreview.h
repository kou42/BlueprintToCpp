#pragma once

#include "CoreMinimal.h"
#include "BPActorCodeGenerator.h"

// 生成ファイルをディスクへ書き込まず、Editor内で確認するためのプレビュー。
class FBPGeneratedCodePreview
{
public:
    static void Show(const FBPGeneratedActorFiles& Files);
};
