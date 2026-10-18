#include "BPNativeSubclassGenerator.h"
#include "BPSemanticClassExtractor.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPNativeSubclassGeneratorTest,
    "BlueprintAIConverter.SemanticClass.NativeSubclassGenerator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPNativeSubclassGeneratorTest::RunTest(const FString& Parameters)
{
    FBPSemanticClassInfo Info;
    TestTrue(TEXT("AActorのリフレクション"), FBPSemanticClassExtractor::Extract(AActor::StaticClass(), Info));
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("元クラスと同名は拒否"),
        FBPNativeSubclassGenerator::Generate(Info, TEXT("AActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("新規派生クラスの生成"),
        FBPNativeSubclassGenerator::Generate(Info, TEXT("AReflectionGeneratedActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("正しい親クラス"),
        Files.HeaderContent.Contains(TEXT("public AActor")));
    TestTrue(TEXT("generatedヘッダー"),
        Files.HeaderContent.Contains(TEXT("ReflectionGeneratedActor.generated.h")));
    TestTrue(TEXT("元クラスのメンバーを複製しない"),
        Files.HeaderContent.Contains(TEXT("UPROPERTY(")) == false);
    TestTrue(TEXT("BeginPlayの明示的override生成"),
        FBPNativeSubclassGenerator::GenerateWithBeginPlayOverride(
            Info, TEXT("AReflectionGeneratedActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("BeginPlayの宣言"),
        Files.HeaderContent.Contains(TEXT("virtual void BeginPlay() override;")));
    TestTrue(TEXT("Super呼び出し"),
        Files.SourceContent.Contains(TEXT("Super::BeginPlay();")));
    return true;
}

#endif
