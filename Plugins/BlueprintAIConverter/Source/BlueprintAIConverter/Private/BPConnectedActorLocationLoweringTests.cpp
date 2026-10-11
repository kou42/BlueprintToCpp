#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPConnectedActorLocationLowering.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPConnectedActorLocationTest,
    "BlueprintAIConverter.SemanticIR.Lowering.ConnectedActorLocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPConnectedActorLocationTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");

    FBPIRNode Source;
    Source.Id = FGuid::NewGuid();
    Source.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Source.FunctionName = TEXT("K2_GetActorLocation");
    FBPIRPin Return;
    Return.Id = FGuid::NewGuid();
    Return.Name = TEXT("ReturnValue");
    Return.Direction = EGPD_Output;
    Return.Category = TEXT("struct");
    Return.SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
    Source.Pins.Add(Return);

    FBPIRNode Target;
    Target.Id = FGuid::NewGuid();
    FBPIRPin Input;
    Input.Id = FGuid::NewGuid();
    Input.Name = TEXT("Location");
    Input.Direction = EGPD_Input;
    Input.Category = TEXT("struct");
    Input.SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
    Target.Pins.Add(Input);

    FBPIRLink Edge;
    Edge.SourceNode = Source.Id;
    Edge.SourcePin = Return.Id;
    Edge.TargetNode = Target.Id;
    Edge.TargetPin = Input.Id;
    Graph.Nodes.Add(Source);
    Graph.Nodes.Add(Target);
    Graph.DataEdges.Add(Edge);
    IR.Graphs.Add(Graph);

    FBPSemanticExpression Expression;
    TArray<FBPSemanticDiagnostic> Diagnostics;
    TestTrue(TEXT("Connected GetActorLocation lowered"),
        FBPConnectedActorLocationLowering::LowerInput(IR, Target.Id, Input.Id,
            Expression, Diagnostics));
    TestEqual(TEXT("Resolved source pin"), Expression.SourcePinId, Return.Id);
    TestEqual(TEXT("Resolved call name"), Expression.FunctionName, FName(TEXT("K2_GetActorLocation")));

    IR.Graphs[0].Nodes[1].Pins[0].SubCategoryObjectPath = TEXT("/Script/CoreUObject.Rotator");
    Diagnostics.Reset();
    TestFalse(TEXT("Mismatched input type rejected"),
        FBPConnectedActorLocationLowering::LowerInput(IR, Target.Id, Input.Id,
            Expression, Diagnostics));
    TestTrue(TEXT("Failure clears semantic expression"),
        Expression.Type.Kind == EBPSemanticTypeKind::Unresolved);
    return true;
}
#endif
