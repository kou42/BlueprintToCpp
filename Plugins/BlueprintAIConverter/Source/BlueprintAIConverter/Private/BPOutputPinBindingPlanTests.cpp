#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPOutputPinBindingPlan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPOutputPinBindingPlanTest,
    "BlueprintAIConverter.SemanticIR.OutputPinBindingPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPOutputPinBindingPlanTest::RunTest(const FString& Parameters)
{
    FBPIRGraph Graph;
    FBPIRNode Source;
    Source.Id = FGuid::NewGuid();
    FBPIRPin Output;
    Output.Id = FGuid::NewGuid();
    Output.Name = TEXT("SweepHitResult");
    Output.Direction = EGPD_Output;
    Source.Pins.Add(Output);
    FBPIRNode Target;
    Target.Id = FGuid::NewGuid();
    FBPIRPin Input;
    Input.Id = FGuid::NewGuid();
    Input.Direction = EGPD_Input;
    Target.Pins.Add(Input);
    Graph.Nodes.Add(Source);
    Graph.Nodes.Add(Target);

    FBPSemanticType Type;
    Type.Kind = EBPSemanticTypeKind::Struct;
    Type.TypePath = TEXT("/Script/Engine.HitResult");
    FBPOutputPinBinding Binding;
    FString Error;
    const FString Name = TEXT("BP_Hit_0123456789ABCDEF0123456789ABCDEF");
    TestTrue(TEXT("Unconnected output can be bound"),
        FBPOutputPinBindingPlan::Build(Graph, Source.Id, Output.Id, Name,
            Type, Binding, Error));
    TestEqual(TEXT("No consumers"), Binding.ConsumerPinIds.Num(), 0);
    FBPIRLink Edge;
    Edge.SourceNode = Source.Id;
    Edge.SourcePin = Output.Id;
    Edge.TargetNode = Target.Id;
    Edge.TargetPin = Input.Id;
    Graph.DataEdges.Add(Edge);
    TestTrue(TEXT("Connected output records consumer"),
        FBPOutputPinBindingPlan::Build(Graph, Source.Id, Output.Id, Name,
            Type, Binding, Error));
    TestEqual(TEXT("One consumer"), Binding.ConsumerPinIds.Num(), 1);
    TestEqual(TEXT("Variable retained"), Binding.VariableName, Name);
    Graph.DataEdges.Add(Edge);
    TestFalse(TEXT("Duplicate consumer rejected"),
        FBPOutputPinBindingPlan::Build(Graph, Source.Id, Output.Id, Name,
            Type, Binding, Error));
    TestTrue(TEXT("Failed plan clears consumers"), Binding.ConsumerPinIds.IsEmpty());
    Graph.DataEdges.Pop();
    Graph.DataEdges[0].TargetPin = FGuid::NewGuid();
    TestFalse(TEXT("Dangling consumer rejected"),
        FBPOutputPinBindingPlan::Build(Graph, Source.Id, Output.Id, Name,
            Type, Binding, Error));
    return true;
}
#endif
