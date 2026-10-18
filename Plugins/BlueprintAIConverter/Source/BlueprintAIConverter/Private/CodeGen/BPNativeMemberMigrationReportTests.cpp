#include "BPNativeMemberMigrationReport.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPNativeMemberMigrationReportTest,
    "BlueprintAIConverter.SemanticClass.NativeMemberMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPNativeMemberMigrationReportTest::RunTest(const FString& Parameters)
{
    FBPSemanticClassInfo Info;
    FString Report;
    FString Error;
    TestFalse(TEXT("出自不明のクラスを拒否"),
        FBPNativeMemberMigrationReport::Build(Info, Report, Error));
    Info.Origin = EBPSemanticClassOrigin::Native;
    Info.ClassPath = TEXT("/Script/MyGame.MyActor");
    FBPSemanticClassMember Property;
    Property.Name = TEXT("Health");
    Property.CppType = TEXT("float");
    Info.DeclaredProperties.Add(Property);
    FBPSemanticReflectedFunction Function;
    Function.Name = TEXT("GetHealth");
    Function.ReturnCppType = TEXT("float");
    Info.DeclaredFunctionSignatures.Add(Function);
    TestTrue(TEXT("診断レポート生成"),
        FBPNativeMemberMigrationReport::Build(Info, Report, Error));
    TestTrue(TEXT("継承するプロパティを表示"),
        Report.Contains(TEXT("INHERITED / DO NOT REDECLARE] property Health")));
    TestTrue(TEXT("本体不明の関数を表示"),
        Report.Contains(TEXT("BODY UNAVAILABLE] function GetHealth")));
    return true;
}

#endif
