#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorLocationLowering.h"
#include "GameFramework/Actor.h"

namespace
{
FBPGraphIR MakeLocationGraph()
{
    FBPGraphIR GraphIR;
    GraphIR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Call;
    Call.Id = FGuid::NewGuid();
    Call.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Call.FunctionName = TEXT("K2_GetActorLocation");
    FBPIRPin Return;
    Return.Id = FGuid::NewGuid();
    Return.Name = TEXT("ReturnValue");
    Return.Direction = EGPD_Output;
    Return.Category = TEXT("struct");
    Return.SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
    Call.Pins.Add(Return);
    Graph.Nodes.Add(MoveTemp(Call));
    GraphIR.Graphs.Add(MoveTemp(Graph));
    return GraphIR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPActorLocationLoweringTest,
    "BlueprintAIConverter.SemanticIR.Lowering.GetActorLocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPActorLocationLoweringTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeLocationGraph();
    const FGuid NodeId = IR.Graphs[0].Nodes[0].Id;
    FBPSemanticExpression Expression;
    TArray<FBPSemanticDiagnostic> Diagnostics;

    TestTrue(TEXT("GetActorLocation lowers to semantic expression"),
        FBPActorLocationLowering::LowerGetActorLocation(IR, NodeId, Expression, Diagnostics));
    TestEqual(TEXT("Function name"), Expression.FunctionName, FName(TEXT("K2_GetActorLocation")));
    TestEqual(TEXT("Source node ID"), Expression.SourceNodeId, NodeId);
    TestEqual(TEXT("Return pin ID"), Expression.SourcePinId, IR.Graphs[0].Nodes[0].Pins[0].Id);
    TestEqual(TEXT("Vector type"), Expression.Type.TypePath, FString(TEXT("/Script/CoreUObject.Vector")));
    TestEqual(TEXT("Self argument count"), Expression.Arguments.Num(), 1);

    IR.Graphs[0].Nodes[0].Pins[0].SubCategoryObjectPath = TEXT("/Script/CoreUObject.Rotator");
    Diagnostics.Reset();
    TestFalse(TEXT("Mismatched struct type rejected"),
        FBPActorLocationLowering::LowerGetActorLocation(IR, NodeId, Expression, Diagnostics));
    TestTrue(TEXT("Type mismatch diagnostic emitted"), Diagnostics.IsEmpty() == false);

    IR = MakeLocationGraph();
    FBPIRLink Unsupported;
    Unsupported.TargetNode = IR.Graphs[0].Nodes[0].Id;
    IR.Graphs[0].DataEdges.Add(Unsupported);
    Diagnostics.Reset();
    TestFalse(TEXT("Connected input rejected"),
        FBPActorLocationLowering::LowerGetActorLocation(IR, IR.Graphs[0].Nodes[0].Id, Expression, Diagnostics));
    TestTrue(TEXT("Connected input diagnostic emitted"), Diagnostics.IsEmpty() == false);
    return true;
}
#endif
