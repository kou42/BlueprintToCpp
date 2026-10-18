#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// Native UCLASSの宣言を安全に移植できるか、個々のメンバー単位で診断する。
// 判定は情報提供のみ。既存のC++生成処理に未検証の宣言を注入しない。
class BLUEPRINTAICONVERTER_API FBPNativeMemberMigrationReport
{
public:
    static bool Build(const FBPSemanticClassInfo& Info, FString& OutReport, FString& OutError);
};
