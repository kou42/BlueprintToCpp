#include "BPNativeClassDependencyReport.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPNativeClassDependencyReportTest,
    "BlueprintAIConverter.SemanticClass.NativeDependencyReport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPNativeClassDependencyReportTest::RunTest(const FString& Parameters)
{
    FBPSemanticClassInfo Info;
    Info.Origin = EBPSemanticClassOrigin::Native;
    Info.ClassPath = TEXT("/Script/Engine.Actor");
    FString Report;
    FString Error;
    TestTrue(TEXT("異なるモジュールの依存情報"),
        FBPNativeClassDependencyReport::Build(Info, TEXT("MyGame"), Report, Error));
    TestTrue(TEXT("Engineへの依存を表示"), Report.Contains(TEXT("\"Engine\"")));
    TestTrue(TEXT("PublicDependencyの案内"), Report.Contains(TEXT("PublicDependencyModuleNames")));
    TestTrue(TEXT("同一モジュールの場合"),
        FBPNativeClassDependencyReport::Build(Info, TEXT("Engine"), Report, Error));
    TestTrue(TEXT("同一モジュールの扱い"), Report.Contains(TEXT("No cross-module dependency")));
    TestFalse(TEXT("不正なモジュール名を拒否"),
        FBPNativeClassDependencyReport::Build(Info, TEXT("MyGame;Bad"), Report, Error));
    return true;
}

#endif
