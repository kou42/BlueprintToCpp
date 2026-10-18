#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"
#include "BPGeneratedCode.h"

// C++宣言を検証済みの関数情報として渡す。Reflectionだけではvirtual属性を確定できないため、
// 未検証のUFunctionをoverrideとして自動生成しない。
struct FBPVerifiedOverrideFunction
{
    FString Name;
    FString Declaration;
    FString DefinitionSignature;
    FString SuperCall;
    FString AccessSpecifier;
};

// 元クラスを再定義せず、指定した新規名で最小限の派生UCLASSを生成する。
// リフレクションの宣言メンバーを複製しないため、元の実装は継承で維持する。
class BLUEPRINTAICONVERTER_API FBPNativeSubclassGenerator
{
public:
    static bool GenerateWithVerifiedOverrides(const FBPSemanticClassInfo& Info,
        const FString& NewClassName, const FString& ModuleApiMacro,
        const TArray<FBPVerifiedOverrideFunction>& Functions,
        FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics);

    static bool ParseVerifiedActorOverrides(const FString& Selection,
        TArray<FBPVerifiedOverrideFunction>& OutFunctions, TArray<FString>& OutDiagnostics);

    static bool Generate(const FBPSemanticClassInfo& Info, const FString& NewClassName,
        const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
        TArray<FString>& OutDiagnostics);

    // AActorの既知の仮想関数BeginPlayだけを明示的に追加する安全な限定経路。
    // TickはAActorの既知のvirtual関数に限定する。元のTick設定は変更しない。
    // 複数の既知ライフサイクル関数を一度の生成で安全に組み合わせる。
    static bool GenerateWithLifecycleOverrides(const FBPSemanticClassInfo& Info,
        const FString& NewClassName, const FString& ModuleApiMacro,
        bool bBeginPlay, bool bTick, FBPGeneratedActorFiles& OutFiles,
        TArray<FString>& OutDiagnostics);

    static bool GenerateWithTickOverride(const FBPSemanticClassInfo& Info,
        const FString& NewClassName, const FString& ModuleApiMacro,
        FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics);

    static bool GenerateWithBeginPlayOverride(const FBPSemanticClassInfo& Info,
        const FString& NewClassName, const FString& ModuleApiMacro,
        FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics);
};
