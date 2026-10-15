#pragma once

#include "CoreMinimal.h"
#include "BPActorCodeGenerator.h"

// 既存生成経路を維持したまま、新Semantic経路との差異を観測する。
// 比較が失敗しても旧Generatorの出力・保存経路は変更しない。
class BLUEPRINTAICONVERTER_API FBPSemanticParityChecker
{
public:
    static bool Compare(const FBPGraphIR& GraphIR, const FString& ClassName,
        const FString& ModuleApiMacro, const FBPGeneratedActorFiles& LegacyFiles,
        TArray<FString>& OutDiagnostics);
};
