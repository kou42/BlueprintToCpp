#include "BPSemanticClassExtractor.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticClassExtractorTest,
    "BlueprintAIConverter.SemanticClass.NativeReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticClassExtractorTest::RunTest(const FString& Parameters)
{
    FBPSemanticClassInfo Info;
    TestFalse(TEXT("nullクラスを拒否"), FBPSemanticClassExtractor::Extract(nullptr, Info));

    TestTrue(TEXT("ネイティブActorの抽出"), FBPSemanticClassExtractor::Extract(AActor::StaticClass(), Info));
    TestTrue(TEXT("ネイティブクラス判定"), Info.Origin == EBPSemanticClassOrigin::Native);
    TestEqual(TEXT("クラスパス"), Info.ClassPath, FString(TEXT("/Script/Engine.Actor")));
    TestEqual(TEXT("クラス名"), Info.ClassName, FString(TEXT("Actor")));
    TestTrue(TEXT("親クラスの情報"), Info.NativeParentClassPath.IsEmpty() == false);
    TestTrue(TEXT("宣言プロパティの型情報が取得できる"),
        Info.DeclaredProperties.ContainsByPredicate([](const FBPSemanticClassMember& Member)
        {
            return Member.Name.IsEmpty() == false && Member.CppType.IsEmpty() == false;
        }));
    TestEqual(TEXT("宣言関数と署名の件数"), Info.DeclaredFunctions.Num(),
        Info.DeclaredFunctionSignatures.Num());
    TestTrue(TEXT("宣言関数のパスが取得できる"),
        Info.DeclaredFunctions.ContainsByPredicate([](const FBPSemanticClassMember& Member)
        {
            return Member.Name.IsEmpty() == false && Member.ReflectionPath.IsEmpty() == false;
        }));
    return true;
}

#endif
