#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPSemanticParityChecker.h"
#include "BPActorCodeGenerator.h"

namespace
{
FBPGraphIR MakeParityGraph()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Kind = TEXT("event");
    Graph.Name = TEXT("EventGraph");
    FBPIRNode Event;
    Event.Id = FGuid::NewGuid();
    Event.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Event.FunctionName = TEXT("ReceiveBeginPlay");
    FBPIRNode Print;
    Print.Id = FGuid::NewGuid();
    Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Print.FunctionName = TEXT("PrintString");
    FBPIRPin Input;
    Input.Id = FGuid::NewGuid();
    Input.Name = TEXT("InString");
    Input.Direction = EGPD_Input;
    Input.Category = TEXT("string");
    Input.DefaultValue = TEXT("Shadow parity");
    Print.Pins.Add(Input);
    FBPIRLink Edge;
    Edge.SourceNode = Event.Id;
    Edge.TargetNode = Print.Id;
    Graph.ExecEdges.Add(Edge);
    Graph.Nodes.Add(MoveTemp(Event));
    Graph.Nodes.Add(MoveTemp(Print));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticParitySuccessTest,
    "BlueprintAIConverter.SemanticIR.Parity.MatchesLegacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticParitySuccessTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR Graph = MakeParityGraph();
    FBPGeneratedActorFiles Legacy;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("旧Generator成功"), FBPActorCodeGenerator::Generate(
        Graph, TEXT("AShadowActor"), TEXT("GAME_API"), Legacy, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("Shadow比較成功"), FBPSemanticParityChecker::Compare(
        Graph, TEXT("AShadowActor"), TEXT("GAME_API"), Legacy, Diagnostics));
    TestTrue(TEXT("診断なし"), Diagnostics.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticParityMismatchTest,
    "BlueprintAIConverter.SemanticIR.Parity.DetectsMismatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticParityMismatchTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR Graph = MakeParityGraph();
    FBPGeneratedActorFiles Legacy;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("旧Generator成功"), FBPActorCodeGenerator::Generate(
        Graph, TEXT("AShadowActor"), FString(), Legacy, Diagnostics)) == false)
    {
        return false;
    }
    Legacy.SourceContent += TEXT("// injected mismatch");
    TestFalse(TEXT("改変されたソースを検出"), FBPSemanticParityChecker::Compare(
        Graph, TEXT("AShadowActor"), FString(), Legacy, Diagnostics));
    TestTrue(TEXT("差異を診断"), Diagnostics.Contains(TEXT("Source content differs")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticParityFailureIsolationTest,
    "BlueprintAIConverter.SemanticIR.Parity.FailureIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticParityFailureIsolationTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeParityGraph();
    FBPGeneratedActorFiles Legacy;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("旧経路の生成成功"), FBPActorCodeGenerator::Generate(
        Graph, TEXT("AShadowActor"), FString(), Legacy, Diagnostics)) == false)
    {
        return false;
    }
    const FString OriginalHeader = Legacy.HeaderContent;
    const FString OriginalSource = Legacy.SourceContent;

    // Shadow経路が失敗してもプレビュー・保存に使う旧経路の出力を変更しない。
    Graph.BlueprintVariableCount = 1;
    TestFalse(TEXT("Semantic経路の拒否を検出"), FBPSemanticParityChecker::Compare(
        Graph, TEXT("AShadowActor"), FString(), Legacy, Diagnostics));
    TestTrue(TEXT("比較診断を返す"), Diagnostics.IsEmpty() == false);
    TestEqual(TEXT("旧ヘッダーを維持"), Legacy.HeaderContent, OriginalHeader);
    TestEqual(TEXT("旧ソースを維持"), Legacy.SourceContent, OriginalSource);
    return true;
}

#endif
