#pragma once

#include "CoreMinimal.h"

// Generatorの実装方式に依存しない、プレビュー・保存用の生成ファイル一式。
struct FBPGeneratedActorFiles
{
    FString HeaderFileName;
    FString HeaderContent;
    FString SourceFileName;
    FString SourceContent;
};
