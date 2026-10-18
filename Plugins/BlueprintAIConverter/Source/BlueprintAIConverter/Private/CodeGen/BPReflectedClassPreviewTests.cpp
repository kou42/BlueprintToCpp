#include "BPReflectedClassPreview.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPReflectedClassPreviewTest,
    "BlueprintAIConverter.SemanticClass.CppReferencePreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPReflectedClassPreviewTest::RunTest(const FString& Parameters)
{
    FBPSemanticClassInfo Info;
    FString Preview;
    FString Error;
    TestFalse(TEXT("不明なクラスを拒否"), FBPReflectedClassPreview::Generate(Info, Preview, Error));

    Info.Origin = EBPSemanticClassOrigin::Native;
    Info.ClassName = TEXT("AMyActor");
    Info.ClassPath = TEXT("/Script/MyGame.MyActor");
    FBPSemanticClassMember Property;
    Property.Name = TEXT("Health");
    Property.CppType = TEXT("float");
    Info.DeclaredProperties.Add(Property);
    FBPSemanticReflectedFunction Function;
    Function.Name = TEXT("SetHealth");
    Function.ReturnCppType = TEXT("void");
    FBPSemanticReflectedParameter Parameter;
    Parameter.Name = TEXT("Value");
    Parameter.CppType = TEXT("float");
    Function.Parameters.Add(Parameter);
    Info.DeclaredFunctionSignatures.Add(Function);

    TestTrue(TEXT("参照プレビュー生成"), FBPReflectedClassPreview::Generate(Info, Preview, Error));
    TestTrue(TEXT("ビルド不可を明示"), Preview.Contains(TEXT("NOT COMPILABLE")));
    TestTrue(TEXT("プロパティの宣言を保持"), Preview.Contains(TEXT("float Health")));
    TestTrue(TEXT("関数の署名を保持"), Preview.Contains(TEXT("void SetHealth(float Value)")));
    TestFalse(TEXT("UCLASS再定義を避ける"), Preview.Contains(TEXT("UCLASS()")));
    return true;
}

#endif
