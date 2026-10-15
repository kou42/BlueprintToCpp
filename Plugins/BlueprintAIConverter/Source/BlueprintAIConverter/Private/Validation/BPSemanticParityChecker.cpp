#include "BPSemanticParityChecker.h"
#include "BPSemanticLowering.h"
#include "BPSemanticCodeGenerator.h"

bool FBPSemanticParityChecker::Compare(const FBPGraphIR& GraphIR, const FString& ClassName,
    const FString& ModuleApiMacro, const FBPGeneratedActorFiles& LegacyFiles,
    TArray<FString>& OutDiagnostics)
{
    OutDiagnostics.Reset();
    FBPSemanticIR Semantic;
    if (FBPSemanticLowering::LowerPrintString(GraphIR, Semantic) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Semantic Lowering: %s"), *Diagnostic.Message));
        }
        if (OutDiagnostics.IsEmpty())
        {
            OutDiagnostics.Add(TEXT("Semantic Lowering failed without diagnostics"));
        }
        return false;
    }

    FBPGeneratedActorFiles SemanticFiles;
    if (FBPSemanticCodeGenerator::Generate(Semantic, ClassName, ModuleApiMacro,
        SemanticFiles, OutDiagnostics) == false)
    {
        return false;
    }

    // 文字列全体を比較してエスケープ、色、Duration、APIマクロの差異も検出する。
    // 生成内容をログへ出すとBlueprintの機密文字列が漏れるため、差異の種類のみ報告する。
    if (SemanticFiles.HeaderFileName != LegacyFiles.HeaderFileName)
    {
        OutDiagnostics.Add(TEXT("Header filename differs"));
    }
    if (SemanticFiles.SourceFileName != LegacyFiles.SourceFileName)
    {
        OutDiagnostics.Add(TEXT("Source filename differs"));
    }
    if (SemanticFiles.HeaderContent != LegacyFiles.HeaderContent)
    {
        OutDiagnostics.Add(TEXT("Header content differs"));
    }
    if (SemanticFiles.SourceContent != LegacyFiles.SourceContent)
    {
        OutDiagnostics.Add(TEXT("Source content differs"));
    }
    return OutDiagnostics.IsEmpty();
}
