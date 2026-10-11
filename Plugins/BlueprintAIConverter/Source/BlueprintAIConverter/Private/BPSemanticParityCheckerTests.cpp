#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPSemanticParityChecker.h"

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
#endif
