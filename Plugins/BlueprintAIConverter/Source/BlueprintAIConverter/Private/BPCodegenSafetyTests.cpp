#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorCodeGenerator.h"
#include "BPPrintStringAnalyzer.h"

namespace
{
FBPGraphIR MakeMinimalIR()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Kind = TEXT("event");

    FBPIRNode Event;
    Event.Id = FGuid::NewGuid();
    Event.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Event.FunctionName = TEXT("ReceiveBeginPlay");

    FBPIRNode Print;
    Print.Id = FGuid::NewGuid();
    Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Print.FunctionName = TEXT("PrintString");
    FBPIRPin Text;
    Text.Id = FGuid::NewGuid();
    Text.Name = TEXT("InString");
    Text.Category = TEXT("string");
    Print.Pins.Add(Text);

    FBPIRLink Link;
    Link.SourceNode = Event.Id;
    Link.TargetNode = Print.Id;
    Graph.ExecEdges.Add(Link);
    Graph.Nodes.Add(MoveTemp(Event));
    Graph.Nodes.Add(MoveTemp(Print));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}

bool IsRejected(const FBPGraphIR& IR)
{
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    return FBPActorCodeGenerator::Generate(IR, TEXT("ASafeActor"), TEXT(""), Files, Diagnostics) == false &&
        Diagnostics.IsEmpty() == false && Files.HeaderContent.IsEmpty();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCodegenSafetyTest,
    "BlueprintAIConverter.CodeGenerator.RejectUnsupportedSemantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPCodegenSafetyTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeMinimalIR();
    IR.BlueprintVariableCount = 1;
    TestTrue(TEXT("Member variables rejected"), IsRejected(IR));

    IR = MakeMinimalIR();
    FBPIRGraph FunctionGraph;
    FunctionGraph.Kind = TEXT("function");
    IR.Graphs.Add(FunctionGraph);
    TestTrue(TEXT("Additional function graph rejected"), IsRejected(IR));

    IR = MakeMinimalIR();
    FBPIRPin Unknown;
    Unknown.Name = TEXT("UnexpectedOption");
    IR.Graphs[0].Nodes[1].Pins.Add(Unknown);
    TestTrue(TEXT("Unknown PrintString option rejected"), IsRejected(IR));

    IR = MakeMinimalIR();
    FBPIRPin Context;
    Context.Name = TEXT("WorldContextObject");
    Context.DefaultValue = TEXT("ExplicitContext");
    IR.Graphs[0].Nodes[1].Pins.Add(Context);
    TestTrue(TEXT("Explicit world context rejected"), IsRejected(IR));

    IR = MakeMinimalIR();
    FBPIRPin Duration;
    Duration.Name = TEXT("Duration");
    Duration.DefaultValue = TEXT("nan");
    IR.Graphs[0].Nodes[1].Pins.Add(Duration);
    TestTrue(TEXT("Non-finite duration rejected"), IsRejected(IR));
    return true;
}
#endif
