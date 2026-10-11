#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPPrintStringAnalyzer.h"

namespace
{
FBPIRNode MakeNode(const TCHAR* ClassPath, const TCHAR* FunctionName)
{
    FBPIRNode Node;
    Node.Id = FGuid::NewGuid();
    Node.ClassPath = ClassPath;
    Node.FunctionName = FunctionName;
    return Node;
}

FBPGraphIR MakeGraph()
{
    FBPGraphIR IR;
    FBPIRGraph Graph;
    Graph.Kind = TEXT("event");
    FBPIRNode BeginPlay = MakeNode(TEXT("/Script/BlueprintGraph.K2Node_Event"), TEXT("ReceiveBeginPlay"));
    FBPIRNode PrintString = MakeNode(TEXT("/Script/BlueprintGraph.K2Node_CallFunction"), TEXT("PrintString"));

    FBPIRPin StringPin;
    StringPin.Id = FGuid::NewGuid();
    StringPin.Name = TEXT("InString");
    StringPin.Category = TEXT("string");
    StringPin.Direction = EGPD_Input;
    StringPin.DefaultValue = TEXT("Hello Blueprint");
    PrintString.Pins.Add(StringPin);

    FBPIRLink Link;
    Link.SourceNode = BeginPlay.Id;
    Link.TargetNode = PrintString.Id;
    Graph.ExecEdges.Add(Link);
    Graph.Nodes.Add(MoveTemp(BeginPlay));
    Graph.Nodes.Add(MoveTemp(PrintString));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPPrintStringLiteralTest,
    "BlueprintAIConverter.PrintString.Literal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPPrintStringLiteralTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR IR = MakeGraph();
    FBPPrintStringCall Call;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Literal PrintString is accepted"), FBPPrintStringAnalyzer::Analyze(IR, Call, Diagnostics));
    TestEqual(TEXT("String literal preserved"), Call.InString, FString(TEXT("Hello Blueprint")));
    TestTrue(TEXT("Screen output default"), Call.bPrintToScreen);
    TestTrue(TEXT("Log output default"), Call.bPrintToLog);
    TestEqual(TEXT("No diagnostics"), Diagnostics.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPPrintStringConnectedTest,
    "BlueprintAIConverter.PrintString.RejectConnectedInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPPrintStringConnectedTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeGraph();
    FBPIRLink DataLink;
    DataLink.TargetNode = IR.Graphs[0].Nodes[1].Id;
    DataLink.TargetPin = IR.Graphs[0].Nodes[1].Pins[0].Id;
    IR.Graphs[0].DataEdges.Add(DataLink);

    FBPPrintStringCall Call;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("Connected input rejected"), FBPPrintStringAnalyzer::Analyze(IR, Call, Diagnostics));
    TestTrue(TEXT("Diagnostic reported"), Diagnostics.IsEmpty() == false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPPrintStringUnsupportedTest,
    "BlueprintAIConverter.PrintString.RejectColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPPrintStringUnsupportedTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeGraph();
    FBPIRPin Color;
    Color.Id = FGuid::NewGuid();
    Color.Name = TEXT("TextColor");
    Color.Direction = EGPD_Input;
    Color.DefaultValue = TEXT("(R=1,G=0,B=0,A=1)");
    IR.Graphs[0].Nodes[1].Pins.Add(Color);

    FBPPrintStringCall Call;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("Explicit color rejected"), FBPPrintStringAnalyzer::Analyze(IR, Call, Diagnostics));
    TestTrue(TEXT("Diagnostic reported"), Diagnostics.IsEmpty() == false);
    return true;
}
#endif
