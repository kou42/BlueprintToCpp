#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPGeneratedCode.h"

// 非推奨のDirect Graph IR経路。移行期間中のParityテスト専用。
// Editorの製品導線からは参照せず、Semantic経路の安定後に削除する。
class BLUEPRINTAICONVERTER_API FBPActorCodeGenerator
{
public:
    // ClassNameはAで始まるC++クラス名、ModuleApiMacroは空文字またはMODULE_API形式。
    static bool Generate(const FBPGraphIR& IR, const FString& ClassName,
        const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
        TArray<FString>& OutDiagnostics);
};
