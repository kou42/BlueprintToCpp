#pragma once

#include "CoreMinimal.h"
#include "BPGeneratedCode.h"
#include "BPGraphIR.h"

// 非推奨Direct GeneratorとSemantic Generatorの移行期テスト専用比較器。
// Editorの製品導線からは参照せず、Direct Generatorと同時に削除する。
class BLUEPRINTAICONVERTER_API FBPSemanticParityChecker
{
public:
    static bool Compare(const FBPGraphIR& GraphIR, const FString& ClassName,
        const FString& ModuleApiMacro, const FBPGeneratedActorFiles& LegacyFiles,
        TArray<FString>& OutDiagnostics);
};
