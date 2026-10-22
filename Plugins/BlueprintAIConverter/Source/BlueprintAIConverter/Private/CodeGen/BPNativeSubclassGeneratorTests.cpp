#include "BPNativeSubclassGenerator.h"
#include "BPGeneratedFunctionShape.h"
#include "BPSemanticClassExtractor.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/Object.h"
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
    // Semantic/Native共通Emitterが署名・Super呼び出し・本体の順序を保持する。
    FBPGeneratedFunctionShape Shape;
    Shape.Declaration = TEXT("public:\n    void Test();\n");
    Shape.Definition = TEXT("void AExample::Test()");
    Shape.Prologue = TEXT("    Super::BeginPlay();\n");
    const FString Rendered = FBPFunctionDefinitionEmitter::RenderDefinition(
        Shape, TEXT("    DoWork();\n"));
    TestTrue(TEXT("共通Emitterは定義と本体を保持"),
        Rendered.Contains(TEXT("void AExample::Test()\n{\n")) &&
        Rendered.Contains(TEXT("Super::BeginPlay();\n    DoWork();")));
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
    TestFalse(TEXT("空の項目を拒否"),
        FBPNativeSubclassGenerator::ParseVerifiedActorOverrides(TEXT("Tick,"), Selected, Diagnostics));
    TestTrue(TEXT("空の項目の拒否で選択結果を消去"), Selected.IsEmpty());
    TestFalse(TEXT("未検証の関数情報を拒否"),
        FBPNativeSubclassGenerator::GenerateWithVerifiedOverrides(
            Info, TEXT("AReflectionGeneratedActor"), FString(),
            {FBPVerifiedOverrideFunction{TEXT("Tick"), TEXT("virtual void Tick() override;"),
                TEXT("void %s::Tick()"), TEXT("Super::Tick();"), TEXT("public")}},
            Files, Diagnostics));
    TestFalse(TEXT("未知の関数を拒否"),
        FBPNativeSubclassGenerator::ParseVerifiedActorOverrides(TEXT("Unknown"), Selected, Diagnostics));
    // ComponentはBeginPlayを共有するが、TickはTickComponentであり同じ署名ではない。
    FBPSemanticClassInfo ComponentInfo;
    if (TestTrue(TEXT("ActorComponentのリフレクション"),
        FBPSemanticClassExtractor::Extract(UActorComponent::StaticClass(), ComponentInfo)) == false)
    {
        return false;
    }
    TestTrue(TEXT("ComponentのBeginPlay override生成"),
        FBPNativeSubclassGenerator::GenerateWithBeginPlayOverride(
            ComponentInfo, TEXT("UReflectionGeneratedComponent"), FString(), Files, Diagnostics));
    TestTrue(TEXT("Component継承"),
        Files.HeaderContent.Contains(TEXT("public UActorComponent")));
    TestTrue(TEXT("Component BeginPlay宣言"),
        Files.HeaderContent.Contains(TEXT("virtual void BeginPlay() override;")));
    TestTrue(TEXT("Component Super呼び出し"),
        Files.SourceContent.Contains(TEXT("Super::BeginPlay();")));
    TestTrue(TEXT("Component固有Tickを生成"),
        FBPNativeSubclassGenerator::GenerateWithTickOverride(
            ComponentInfo, TEXT("UReflectionGeneratedComponent"), FString(), Files, Diagnostics));
    TestTrue(TEXT("TickComponent宣言を生成"),
        Files.HeaderContent.Contains(TEXT("virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;")));
    TestTrue(TEXT("TickComponentのSuper呼び出し"),
        Files.SourceContent.Contains(TEXT("Super::TickComponent(DeltaTime, TickType, ThisTickFunction);")));
    TestFalse(TEXT("Actor用Tick宣言を混入させない"),
        Files.HeaderContent.Contains(TEXT("virtual void Tick(float DeltaTime) override;")));
    TestTrue(TEXT("ComponentのBeginPlayとTickを同時生成"),
        FBPNativeSubclassGenerator::GenerateWithLifecycleOverrides(
            ComponentInfo, TEXT("UReflectionGeneratedComponent"), FString(), true, true, Files, Diagnostics));
    TestTrue(TEXT("両方のComponent lifecycleを含む"),
        Files.SourceContent.Contains(TEXT("Super::BeginPlay();")) &&
        Files.SourceContent.Contains(TEXT("Super::TickComponent(DeltaTime, TickType, ThisTickFunction);")));
    // ActorComponentのEndPlayは同じ引数形でも、親クラスごとの検証済み定義で照合する。
    TestTrue(TEXT("ComponentのEndPlayを生成"),
        FBPNativeSubclassGenerator::GenerateWithVerifiedOverrides(
            ComponentInfo, TEXT("UReflectionGeneratedComponent"), FString(),
            {FBPVerifiedOverrideFunction{TEXT("EndPlay"),
                TEXT("virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;"),
                TEXT("void %s::EndPlay(const EEndPlayReason::Type EndPlayReason)"),
                TEXT("Super::EndPlay(EndPlayReason);"), TEXT("protected")}},
            Files, Diagnostics));
    TestTrue(TEXT("Component EndPlayの宣言"),
        Files.HeaderContent.Contains(TEXT("virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;")));
    TestTrue(TEXT("Component EndPlayのSuper呼び出し"),
        Files.SourceContent.Contains(TEXT("Super::EndPlay(EndPlayReason);")));
    TestFalse(TEXT("Actor用Tick情報をComponentに流用しない"),
        FBPNativeSubclassGenerator::GenerateWithVerifiedOverrides(
            ComponentInfo, TEXT("UReflectionGeneratedComponent"), FString(),
            {FBPVerifiedOverrideFunction{TEXT("Tick"), TEXT("virtual void Tick(float DeltaTime) override;"),
                TEXT("void %s::Tick(float DeltaTime)"), TEXT("Super::Tick(DeltaTime);"), TEXT("public")}},
            Files, Diagnostics));
    TestTrue(TEXT("不正署名の拒否時は出力を残さない"), Files.HeaderContent.IsEmpty());

    // 基底型への直接比較ではなく、継承階層をたどって署名を解決する。
    FBPSemanticClassInfo SceneInfo;
    if (TestTrue(TEXT("SceneComponentのリフレクション"),
        FBPSemanticClassExtractor::Extract(USceneComponent::StaticClass(), SceneInfo)) == false)
    {
        return false;
    }
    TestTrue(TEXT("SceneComponentにもTickComponent署名を適用"),
        FBPNativeSubclassGenerator::GenerateWithTickOverride(
            SceneInfo, TEXT("UReflectionGeneratedScene"), FString(), Files, Diagnostics));
    TestTrue(TEXT("継承先をSceneComponentに維持"),
        Files.HeaderContent.Contains(TEXT("public USceneComponent")));
    TestTrue(TEXT("SceneComponentのTickComponentを生成"),
        Files.SourceContent.Contains(TEXT("UReflectionGeneratedScene::TickComponent(")));

    FBPSemanticClassInfo ObjectInfo;
    if (TestTrue(TEXT("UObjectのリフレクション"),
        FBPSemanticClassExtractor::Extract(UObject::StaticClass(), ObjectInfo)) == false)
    {
        return false;
    }
    // UObjectにはActor系ライフサイクルがない。通常の派生UCLASSは生成でき、
    // 未対応のoverrideだけを拒否することを確認する。
    TestTrue(TEXT("UObject派生クラスの基本生成"),
        FBPNativeSubclassGenerator::Generate(
            ObjectInfo, TEXT("UReflectionGeneratedObject"), FString(), Files, Diagnostics));
    TestTrue(TEXT("UObjectを親クラスとして維持"),
        Files.HeaderContent.Contains(TEXT("public UObject")));
    TestTrue(TEXT("UObjectの公開ヘッダーを含む"),
        Files.HeaderContent.Contains(TEXT("#include \"UObject/Object.h\"")));
    TestFalse(TEXT("UObjectにActorのBeginPlayを生成しない"),
        FBPNativeSubclassGenerator::GenerateWithBeginPlayOverride(
            ObjectInfo, TEXT("UReflectionGeneratedObject"), FString(), Files, Diagnostics));
    TestTrue(TEXT("未対応BeginPlayの拒否時に出力を消去"), Files.HeaderContent.IsEmpty());
    TestFalse(TEXT("未登録の親クラスにはTickを生成しない"),
        FBPNativeSubclassGenerator::GenerateWithTickOverride(
            ObjectInfo, TEXT("UReflectionGeneratedObject"), FString(), Files, Diagnostics));
    TestTrue(TEXT("未登録時に出力を消去"), Files.HeaderContent.IsEmpty());
    return true;
}

#endif
