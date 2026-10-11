#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPSemanticLowering.h"
#include "BPPrintStringAnalyzer.h"

namespace
{
FBPGraphIR MakeSemanticTestGraph()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    IR.AssetPath = TEXT("/Game/BP_Test");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
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
    Text.Direction = EGPD_Input;
    Text.Category = TEXT("string");
    Text.DefaultValue = TEXT("Hello Semantic IR");
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringLiteralTest,
    "BlueprintAIConverter.SemanticIR.Lowering.PrintString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringLiteralTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPPrintStringCall LegacyCall;
    TArray<FString> LegacyDiagnostics;
    TestTrue(TEXT("既存Analyzerが成功"), FBPPrintStringAnalyzer::Analyze(IR, LegacyCall, LegacyDiagnostics));

    FBPSemanticIR Semantic;
    TestTrue(TEXT("Semantic Loweringが成功"), FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestFalse(TEXT("診断がない"), Semantic.HasErrors());
    TestEqual(TEXT("関数数"), Semantic.Functions.Num(), 1);
    if (Semantic.Functions.Num() != 1)
    {
        return false;
    }
    const FBPSemanticFunction& Function = Semantic.Functions[0];
    TestEqual(TEXT("関数名"), Function.Name, FName(TEXT("BeginPlay")));
    if (Function.Blocks.Num() != 1 || Function.Blocks[0].Statements.Num() != 1)
    {
        AddError(TEXT("Expected exactly one block and statement"));
        return false;
    }
    const FBPSemanticStatement& Statement = Function.Blocks[0].Statements[0];
    TestEqual(TEXT("ノードIDを保持"), Statement.SourceNodeId, IR.Graphs[0].Nodes[1].Id);
    const TArray<FBPSemanticExpression>& Args = Statement.Expression.Arguments;
    TestEqual(TEXT("引数数"), Args.Num(), 7);
    if (Args.Num() != 7)
    {
        return false;
    }
    TestEqual(TEXT("既存Analyzerと文字列一致"), Args[1].LiteralValue, LegacyCall.InString);
    TestEqual(TEXT("ピンIDを保持"), Args[1].SourcePinId, IR.Graphs[0].Nodes[1].Pins[0].Id);
    TestEqual(TEXT("Duration一致"), Args[5].LiteralValue, FString::SanitizeFloat(LegacyCall.Duration));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringRejectTest,
    "BlueprintAIConverter.SemanticIR.Lowering.RejectConnectedData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringRejectTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPIRLink Data;
    Data.TargetNode = IR.Graphs[0].Nodes[1].Id;
    Data.TargetPin = IR.Graphs[0].Nodes[1].Pins[0].Id;
    IR.Graphs[0].DataEdges.Add(Data);

    FBPSemanticIR Semantic;
    TestFalse(TEXT("未対応データ接続は拒否"), FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("診断が存在"), Semantic.HasErrors());
    TestEqual(TEXT("失敗時に関数を生成しない"), Semantic.Functions.Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringStandardGraphsTest,
    "BlueprintAIConverter.SemanticIR.Lowering.StandardGraphs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringStandardGraphsTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPIRGraph Construction;
    Construction.Name = TEXT("UserConstructionScript");
    Construction.Kind = TEXT("function");
    FBPIRNode Entry;
    Entry.Id = FGuid::NewGuid();
    Entry.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_FunctionEntry");
    Construction.Nodes.Add(Entry);
    IR.Graphs.Add(Construction);

    FBPIRNode Tick;
    Tick.Id = FGuid::NewGuid();
    Tick.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Tick.FunctionName = TEXT("ReceiveTick");
    IR.Graphs[0].Nodes.Add(Tick);

    FBPSemanticIR Semantic;
    TestTrue(TEXT("空Construction Scriptと未接続Tickを許容"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestEqual(TEXT("BeginPlayだけ生成"), Semantic.Functions.Num(), 1);

    // Construction Scriptに実行エッジが存在する場合は、意味を落とさず拒否する。
    FBPIRLink Unsupported;
    Unsupported.SourceNode = Entry.Id;
    Unsupported.TargetNode = FGuid::NewGuid();
    IR.Graphs[1].ExecEdges.Add(Unsupported);
    TestFalse(TEXT("実行可能Construction Scriptを拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("失敗理由を保持"), Semantic.HasErrors());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringVariableTest,
    "BlueprintAIConverter.SemanticIR.Lowering.RejectMemberVariable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringVariableTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    IR.BlueprintVariableCount = 1;
    FBPSemanticIR Semantic;
    TestFalse(TEXT("未対応メンバー変数を拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("診断を保持"), Semantic.HasErrors());
    TestTrue(TEXT("部分的な関数を公開しない"), Semantic.Functions.IsEmpty());
    return true;
}

#endif
