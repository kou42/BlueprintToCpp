#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"

// 生成コードはファイル保存とは分離し、プレビューやテストから利用できるようにする。
struct FBPGeneratedActorFiles
{
    FString HeaderFileName;
    FString HeaderContent;
    FString SourceFileName;
    FString SourceContent;
};

class BLUEPRINTAICONVERTER_API FBPActorCodeGenerator
{
public:
    // ClassNameはAで始まるC++クラス名、ModuleApiMacroは空文字またはMODULE_API形式。
    static bool Generate(const FBPGraphIR& IR, const FString& ClassName,
        const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
        TArray<FString>& OutDiagnostics);
};
