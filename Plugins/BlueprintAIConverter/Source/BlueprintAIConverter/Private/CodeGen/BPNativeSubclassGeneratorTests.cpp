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
    TestTrue(TEXT("Tickの明示的override生成"),
        FBPNativeSubclassGenerator::GenerateWithTickOverride(
            Info, TEXT("AReflectionGeneratedActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("Tickの宣言"),
        Files.HeaderContent.Contains(TEXT("virtual void Tick(float DeltaTime) override;")));
    TestTrue(TEXT("TickのSuper呼び出し"),
        Files.SourceContent.Contains(TEXT("Super::Tick(DeltaTime);")));
    TestFalse(TEXT("Tick設定を勝手に変更しない"),
        Files.SourceContent.Contains(TEXT("bCanEverTick")));
    TestTrue(TEXT("BeginPlayとTickを同時生成"),
        FBPNativeSubclassGenerator::GenerateWithLifecycleOverrides(
            Info, TEXT("AReflectionGeneratedActor"), FString(), true, true, Files, Diagnostics));
    TestTrue(TEXT("同時生成でBeginPlay宣言"),
        Files.HeaderContent.Contains(TEXT("virtual void BeginPlay() override;")));
    TestTrue(TEXT("同時生成でTick宣言"),
        Files.HeaderContent.Contains(TEXT("virtual void Tick(float DeltaTime) override;")));
    TestTrue(TEXT("同時生成で両方のSuper呼び出し"),
        Files.SourceContent.Contains(TEXT("Super::BeginPlay();")) &&
        Files.SourceContent.Contains(TEXT("Super::Tick(DeltaTime);")));
    TestTrue(TEXT("同時生成でUCLASSは一つ"),
        Files.HeaderContent.Find(TEXT("UCLASS()")) ==
        Files.HeaderContent.Find(TEXT("UCLASS()"), ESearchCase::CaseSensitive, ESearchDir::FromEnd));
    TArray<FBPVerifiedOverrideFunction> Selected;
    TestTrue(TEXT("複数の検証済み関数を解析"),
        FBPNativeSubclassGenerator::ParseVerifiedActorOverrides(TEXT("Tick,EndPlay,BeginPlay"), Selected, Diagnostics));
    TestEqual(TEXT("選択された関数数"), Selected.Num(), 3);
    TestTrue(TEXT("EndPlayを含む共通生成"),
        FBPNativeSubclassGenerator::GenerateWithVerifiedOverrides(
            Info, TEXT("AReflectionGeneratedActor"), FString(), Selected, Files, Diagnostics));
    TestTrue(TEXT("EndPlayの引数とSuper"),
        Files.HeaderContent.Contains(TEXT("EndPlay(const EEndPlayReason::Type EndPlayReason)")) &&
        Files.SourceContent.Contains(TEXT("Super::EndPlay(EndPlayReason);")));
    TestFalse(TEXT("重複指定を拒否"),
        FBPNativeSubclassGenerator::ParseVerifiedActorOverrides(TEXT("Tick,Tick"), Selected, Diagnostics));
    TestFalse(TEXT("未知の関数を拒否"),
        FBPNativeSubclassGenerator::ParseVerifiedActorOverrides(TEXT("Unknown"), Selected, Diagnostics));
    return true;
}

#endif
