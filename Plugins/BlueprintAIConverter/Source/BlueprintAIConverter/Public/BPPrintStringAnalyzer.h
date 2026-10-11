#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"

// Phase 3の前段階: BeginPlayから単一PrintStringへ進む実行経路のみを認識する。
// 未対応の接続やオプション値は黙って無視せず、診断として返す。
struct FBPPrintStringCall
{
    FString InString;
    bool bPrintToScreen = true;
    bool bPrintToLog = true;
    float Duration = 2.0f;
    FString Key;
};

class BLUEPRINTAICONVERTER_API FBPPrintStringAnalyzer
{
public:
    static bool Analyze(const FBPGraphIR& IR, FBPPrintStringCall& OutCall, TArray<FString>& OutDiagnostics);
};
