#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"
#include "BPGeneratedCode.h"

// 元クラスを再定義せず、指定した新規名で最小限の派生UCLASSを生成する。
// リフレクションの宣言メンバーを複製しないため、元の実装は継承で維持する。
class BLUEPRINTAICONVERTER_API FBPNativeSubclassGenerator
{
public:
    static bool Generate(const FBPSemanticClassInfo& Info, const FString& NewClassName,
        const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
        TArray<FString>& OutDiagnostics);

    // AActorの既知の仮想関数BeginPlayだけを明示的に追加する安全な限定経路。
    static bool GenerateWithBeginPlayOverride(const FBPSemanticClassInfo& Info,
        const FString& NewClassName, const FString& ModuleApiMacro,
        FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics);
};
