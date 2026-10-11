#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"
#include "BPActorCodeGenerator.h"

// Graph IRを参照せずSemantic IRからC++を生成する。
// M1ではPrintStringのみを明示的にサポートし、他の式・文は拒否する。
class BLUEPRINTAICONVERTER_API FBPSemanticCodeGenerator
{
public:
    static bool Generate(const FBPSemanticIR& IR, const FString& ClassName,
        const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
        TArray<FString>& OutDiagnostics);
};
