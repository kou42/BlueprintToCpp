#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// 生成先モジュールに必要なBuild.cs依存を、クラスの所属モジュールから抽出する。
// 実際のBuild.csは編集せず、依存の追加が必要かは利用者が確認する。
class BLUEPRINTAICONVERTER_API FBPNativeClassDependencyReport
{
public:
    static bool Build(const FBPSemanticClassInfo& Info, const FString& TargetModule,
        FString& OutReport, FString& OutError);
};
