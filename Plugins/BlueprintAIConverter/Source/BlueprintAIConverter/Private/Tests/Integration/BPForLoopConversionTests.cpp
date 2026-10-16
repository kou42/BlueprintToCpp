#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPSemanticCodeGenerator.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticLowering.h"

namespace
{
FBPIRPin MakeLoopPin(const TCHAR* Name, EEdGraphPinDirection Direction,
    const TCHAR* Category, const TCHAR* DefaultValue = TEXT(""))
{
    FBPIRPin Pin;
    Pin.Id = FGuid::NewGuid();
    Pin.Name = Name;
    Pin.Direction = Direction;
    Pin.Category = Category;
    Pin.DefaultValue = DefaultValue;
    return Pin;
}

FBPGraphIR MakeForLoopGraph()
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_ForLoop");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Path = TEXT("/Game/BP_ForLoop.BP_ForLoop:EventGraph");
    Graph.Kind = TEXT("event");

    FBPIRNode BeginPlay;
    BeginPlay.Id = FGuid::NewGuid();
    BeginPlay.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    BeginPlay.FunctionName = TEXT("ReceiveBeginPlay");
    BeginPlay.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));

    FBPIRNode Loop;
    Loop.Id = FGuid::NewGuid();
    Loop.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Loop.MacroGraphName = TEXT("ForLoop");
    Loop.MacroGraphPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop");
    Loop.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Loop.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("1")));
    Loop.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("3")));
    Loop.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Loop.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Loop.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));

    FBPIRNode Print;
    Print.Id = FGuid::NewGuid();
    Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Print.FunctionName = TEXT("PrintString");
    Print.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Print.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Print.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), TEXT("Loop body")));

    FBPIRNode SecondBodyPrint;
    SecondBodyPrint.Id = FGuid::NewGuid();
    SecondBodyPrint.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    SecondBodyPrint.FunctionName = TEXT("PrintString");
    SecondBodyPrint.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    SecondBodyPrint.Pins.Add(MakeLoopPin(
        TEXT("InString"), EGPD_Input, TEXT("string"), TEXT("Second body")));

    FBPIRNode IndexToString;
    IndexToString.Id = FGuid::NewGuid();
    IndexToString.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    IndexToString.FunctionName = TEXT("Conv_IntToString");
    IndexToString.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    IndexToString.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    IndexToString.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));

    FBPIRNode CompletedPrint;
    CompletedPrint.Id = FGuid::NewGuid();
    CompletedPrint.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    CompletedPrint.FunctionName = TEXT("PrintString");
    CompletedPrint.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    CompletedPrint.Pins.Add(MakeLoopPin(
        TEXT("InString"), EGPD_Input, TEXT("string"), TEXT("Loop completed")));

    FBPIRLink BeginToLoop;
    BeginToLoop.SourceNode = BeginPlay.Id;
    BeginToLoop.SourcePin = BeginPlay.Pins[0].Id;
    BeginToLoop.TargetNode = Loop.Id;
    BeginToLoop.TargetPin = Loop.Pins[0].Id;
    Graph.ExecEdges.Add(BeginToLoop);
    FBPIRLink LoopToPrint;
    LoopToPrint.SourceNode = Loop.Id;
    LoopToPrint.SourcePin = Loop.Pins[3].Id;
    LoopToPrint.TargetNode = Print.Id;
    LoopToPrint.TargetPin = Print.Pins[0].Id;
    Graph.ExecEdges.Add(LoopToPrint);
    FBPIRLink FirstToSecond;
    FirstToSecond.SourceNode = Print.Id;
    FirstToSecond.SourcePin = Print.Pins[1].Id;
    FirstToSecond.TargetNode = SecondBodyPrint.Id;
    FirstToSecond.TargetPin = SecondBodyPrint.Pins[0].Id;
    Graph.ExecEdges.Add(FirstToSecond);
    FBPIRLink LoopToCompleted;
    LoopToCompleted.SourceNode = Loop.Id;
    LoopToCompleted.SourcePin = Loop.Pins[5].Id;
    LoopToCompleted.TargetNode = CompletedPrint.Id;
    LoopToCompleted.TargetPin = CompletedPrint.Pins[0].Id;
    Graph.ExecEdges.Add(LoopToCompleted);
    FBPIRLink IndexToConverter;
    IndexToConverter.SourceNode = Loop.Id;
    IndexToConverter.SourcePin = Loop.Pins[4].Id;
    IndexToConverter.TargetNode = IndexToString.Id;
    IndexToConverter.TargetPin = IndexToString.Pins[0].Id;
    Graph.DataEdges.Add(IndexToConverter);
    FBPIRLink ConverterToPrint;
    ConverterToPrint.SourceNode = IndexToString.Id;
    ConverterToPrint.SourcePin = IndexToString.Pins[1].Id;
    ConverterToPrint.TargetNode = Print.Id;
    ConverterToPrint.TargetPin = Print.Pins[2].Id;
    Graph.DataEdges.Add(ConverterToPrint);

    Graph.Nodes.Add(MoveTemp(BeginPlay));
    Graph.Nodes.Add(MoveTemp(Loop));
    Graph.Nodes.Add(MoveTemp(Print));
    Graph.Nodes.Add(MoveTemp(SecondBodyPrint));
    Graph.Nodes.Add(MoveTemp(IndexToString));
    Graph.Nodes.Add(MoveTemp(CompletedPrint));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}

FBPGraphIR MakeForLoopBranchGraph()
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_ForLoopBranch");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");

    FBPIRNode Begin;
    Begin.Id = FGuid::NewGuid();
    Begin.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Begin.FunctionName = TEXT("ReceiveBeginPlay");
    Begin.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    FBPIRNode Loop;
    Loop.Id = FGuid::NewGuid();
    Loop.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Loop.MacroGraphName = TEXT("ForLoop");
    Loop.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Loop.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("0")));
    Loop.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("1")));
    Loop.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Loop.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Loop.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    FBPIRNode Branch;
    Branch.Id = FGuid::NewGuid();
    Branch.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
    Branch.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Branch.Pins.Add(MakeLoopPin(TEXT("Condition"), EGPD_Input, TEXT("bool"), TEXT("true")));
    Branch.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Branch.Pins.Add(MakeLoopPin(TEXT("else"), EGPD_Output, TEXT("exec")));
    const auto MakePrint = [](const TCHAR* Text)
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Node.FunctionName = TEXT("PrintString");
        Node.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), Text));
        return Node;
    };
    FBPIRNode TruePrint = MakePrint(TEXT("True body"));
    FBPIRNode FalsePrint = MakePrint(TEXT("False body"));
    FBPIRNode DonePrint = MakePrint(TEXT("Branch completed"));
    const auto AddEdge = [&Graph](const FBPIRNode& Source, int32 SourcePin,
        const FBPIRNode& Target, int32 TargetPin)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[SourcePin].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[TargetPin].Id;
        Graph.ExecEdges.Add(Edge);
    };
    AddEdge(Begin, 0, Loop, 0);
    AddEdge(Loop, 3, Branch, 0);
    AddEdge(Branch, 2, TruePrint, 0);
    AddEdge(Branch, 3, FalsePrint, 0);
    AddEdge(Loop, 5, DonePrint, 0);
    Graph.Nodes.Add(MoveTemp(Begin));
    Graph.Nodes.Add(MoveTemp(Loop));
    Graph.Nodes.Add(MoveTemp(Branch));
    Graph.Nodes.Add(MoveTemp(TruePrint));
    Graph.Nodes.Add(MoveTemp(FalsePrint));
    Graph.Nodes.Add(MoveTemp(DonePrint));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopConversionTest,
    "BlueprintAIConverter.Conversion.ForLoop.LiteralBoundsPrintBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopConversionTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR GraphIR = MakeForLoopGraph();
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("ForLoop Loweringに成功"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Loop = SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("ForLoop文種別"), Loop.Kind, EBPSemanticStatementKind::ForLoop);
    TestEqual(TEXT("開始Index"), Loop.LoopFirstIndex.LiteralValue, FString(TEXT("1")));
    TestEqual(TEXT("終了Index"), Loop.LoopLastIndex.LiteralValue, FString(TEXT("3")));
    TestEqual(TEXT("LoopBody文数"), Loop.LoopBodyStatements.Num(), 2);
    TestEqual(TEXT("Completed文数"), Loop.CompletedStatements.Num(), 1);

    FString Json;
    TestTrue(TEXT("Semantic JSONを生成"),
        FBPSemanticJsonSerializer::Serialize(SemanticIR, TEXT("5.7"), Json));
    TestTrue(TEXT("JSONにfor_loopを保持"), Json.Contains(TEXT("\"kind\": \"for_loop\"")));
    TestTrue(TEXT("JSONにLoopBodyを保持"), Json.Contains(TEXT("loop_body_statements")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ForLoop C++生成に成功"),
        FBPSemanticCodeGenerator::Generate(
            SemanticIR, TEXT("AForLoopActor"), TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("両端を含むfor文を生成"), Files.SourceContent.Contains(
        TEXT("for (int32 LoopIndex = 1; LoopIndex <= 3; ++LoopIndex)")));
    TestTrue(TEXT("LoopBodyをfor文内へ生成"), Files.SourceContent.Contains(
        TEXT("UKismetSystemLibrary::PrintString")));
    TestTrue(TEXT("Indexを文字列変換して使用"), Files.SourceContent.Contains(
        TEXT("UKismetStringLibrary::Conv_IntToString(LoopIndex)")));
    TestTrue(TEXT("2番目のBody文を生成"), Files.SourceContent.Contains(TEXT("Second body")));
    TestTrue(TEXT("Body文の接続順を維持"),
        Files.SourceContent.Find(TEXT("Second body")) > Files.SourceContent.Find(TEXT("Conv_IntToString")));
    TestTrue(TEXT("Completedをfor文後へ生成"), Files.SourceContent.Contains(TEXT("Loop completed")));
    TestTrue(TEXT("Completedの生成順"),
        Files.SourceContent.Find(TEXT("Loop completed")) > Files.SourceContent.Find(TEXT("Conv_IntToString")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopBranchConversionTest,
    "BlueprintAIConverter.Conversion.ForLoop.BranchBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopBranchConversionTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("Branch付きForLoop Loweringに成功"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Loop = SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("LoopBodyはBranch 1件"), Loop.LoopBodyStatements.Num(), 1);
    TestEqual(TEXT("LoopBodyのBranch種別"),
        Loop.LoopBodyStatements[0].Kind, EBPSemanticStatementKind::Branch);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Branch付きForLoop C++生成に成功"),
        FBPSemanticCodeGenerator::Generate(
            SemanticIR, TEXT("AForLoopBranchActor"), TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("for文内にif文を生成"), Files.SourceContent.Contains(TEXT("if (true)")));
    TestTrue(TEXT("True側を生成"), Files.SourceContent.Contains(TEXT("True body")));
    TestTrue(TEXT("False側を生成"), Files.SourceContent.Contains(TEXT("False body")));
    TestTrue(TEXT("Completedを生成"), Files.SourceContent.Contains(TEXT("Branch completed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedBranchCodegenTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedBranchCodegen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedBranchCodegenTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("基本BranchのLowering"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopBranchGraph(), SemanticIR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Outer =
        SemanticIR.Functions[0].Blocks[0].Statements[0].LoopBodyStatements[0];
    FBPSemanticStatement Inner;
    Inner.Kind = EBPSemanticStatementKind::Branch;
    Inner.Expression = Outer.Expression;
    Inner.ThenStatements.Add(Outer.ThenStatements[0]);
    Inner.ElseStatements.Add(Outer.ElseStatements[0]);
    Outer.ThenStatements[0] = MoveTemp(Inner);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ネストBranchのC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopNestedBranchActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 OuterIf = Files.SourceContent.Find(TEXT("if (true)"));
    const int32 InnerIf = Files.SourceContent.Find(TEXT("if (true)"), ESearchCase::CaseSensitive,
        ESearchDir::FromStart, OuterIf + 1);
    TestTrue(TEXT("二段のif文を生成"), OuterIf != INDEX_NONE && InnerIf > OuterIf);
    TestTrue(TEXT("ネストのTrue/False文を生成"),
        Files.SourceContent.Contains(TEXT("True body")) &&
        Files.SourceContent.Contains(TEXT("False body")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedArithmeticComparisonTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedArithmeticComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedArithmeticComparisonTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    const auto MakeMath = [](const TCHAR* Function, const TCHAR* DefaultB)
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Node.FunctionName = Function;
        Node.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
        Node.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), DefaultB));
        Node.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
        return Node;
    };
    FBPIRNode Addition = MakeMath(TEXT("Add_IntInt"), TEXT("1"));
    FBPIRNode Multiplication = MakeMath(TEXT("Multiply_IntInt"), TEXT("2"));
    FBPIRNode Comparison = MakeMath(TEXT("Less_IntInt"), TEXT("10"));
    Comparison.Pins[2].Category = TEXT("bool");
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        Graph.DataEdges.Add(Edge);
    };
    Connect(Graph.Nodes[1], 4, Addition, 0);
    Connect(Addition, 2, Multiplication, 0);
    Connect(Multiplication, 2, Comparison, 0);
    Connect(Comparison, 2, Graph.Nodes[2], 1);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Multiplication));
    Graph.Nodes.Add(MoveTemp(Comparison));

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("多段算術比較のLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("多段算術比較のC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopNestedMathActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("多段算術式を保持"),
        Files.SourceContent.Contains(TEXT("UKismetMathLibrary::Multiply_IntInt(UKismetMathLibrary::Add_IntInt(LoopIndex, 1), 2) < 10")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopArithmeticComparisonBranchTest,
    "BlueprintAIConverter.Conversion.ForLoop.ArithmeticComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopArithmeticComparisonBranchTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode Comparison;
    Comparison.Id = FGuid::NewGuid();
    Comparison.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Comparison.FunctionName = TEXT("Less_IntInt");
    Comparison.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Comparison.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("5")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("bool")));
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("1")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    const auto AddDataEdge = [&Graph](const FBPIRNode& Source, int32 SourcePin,
        const FBPIRNode& Target, int32 TargetPin)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[SourcePin].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[TargetPin].Id;
        Graph.DataEdges.Add(Edge);
    };
    AddDataEdge(Comparison, 2, Graph.Nodes[2], 1);
    AddDataEdge(Addition, 2, Comparison, 0);
    AddDataEdge(Graph.Nodes[1], 4, Addition, 0);
    Graph.Nodes.Add(MoveTemp(Comparison));
    Graph.Nodes.Add(MoveTemp(Addition));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("Index加算比較のLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Index加算比較のC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopArithmeticActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("Indexの加算を比較条件へ生成"),
        Files.SourceContent.Contains(TEXT("UKismetMathLibrary::Add_IntInt(LoopIndex, 1) < 5")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopIndexComparisonBranchTest,
    "BlueprintAIConverter.Conversion.ForLoop.IndexComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopIndexComparisonBranchTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode Comparison;
    Comparison.Id = FGuid::NewGuid();
    Comparison.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Comparison.FunctionName = TEXT("Less_IntInt");
    Comparison.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Comparison.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("3")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("bool")));
    FBPIRLink ConditionEdge;
    ConditionEdge.SourceNode = Comparison.Id;
    ConditionEdge.SourcePin = Comparison.Pins[2].Id;
    ConditionEdge.TargetNode = Graph.Nodes[2].Id;
    ConditionEdge.TargetPin = Graph.Nodes[2].Pins[1].Id;
    Graph.DataEdges.Add(ConditionEdge);
    FBPIRLink IndexEdge;
    IndexEdge.SourceNode = Graph.Nodes[1].Id;
    IndexEdge.SourcePin = Graph.Nodes[1].Pins[4].Id;
    IndexEdge.TargetNode = Comparison.Id;
    IndexEdge.TargetPin = Comparison.Pins[0].Id;
    Graph.DataEdges.Add(IndexEdge);
    Graph.Nodes.Add(MoveTemp(Comparison));

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("LoopIndex比較のLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("LoopIndex比較のC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopIndexActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("LoopIndexを条件式へ生成"),
        Files.SourceContent.Contains(TEXT("if (LoopIndex < 3)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopComparisonBranchTest,
    "BlueprintAIConverter.Conversion.ForLoop.NumericComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopComparisonBranchTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Branch = Graph.Nodes[2];
    FBPIRNode Comparison;
    Comparison.Id = FGuid::NewGuid();
    Comparison.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Comparison.FunctionName = TEXT("Less_IntInt");
    Comparison.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Comparison.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int"), TEXT("1")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("3")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("bool")));
    FBPIRLink ConditionEdge;
    ConditionEdge.SourceNode = Comparison.Id;
    ConditionEdge.SourcePin = Comparison.Pins[2].Id;
    ConditionEdge.TargetNode = Branch.Id;
    ConditionEdge.TargetPin = Branch.Pins[1].Id;
    Graph.DataEdges.Add(ConditionEdge);
    Graph.Nodes.Add(MoveTemp(Comparison));

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("比較条件BranchのLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Loop = SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("比較条件の関数名"),
        Loop.LoopBodyStatements[0].Expression.FunctionName, FName(TEXT("Less_IntInt")));
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("比較条件BranchのC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopCompareActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("比較式をif文として生成"),
        Files.SourceContent.Contains(TEXT("if (1 < 3)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopCompletedChainLoweringTest,
    "BlueprintAIConverter.Conversion.ForLoop.CompletedChainLowering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopCompletedChainLoweringTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& FirstCompleted = Graph.Nodes.Last();
    FirstCompleted.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    FBPIRNode SecondCompleted;
    SecondCompleted.Id = FGuid::NewGuid();
    SecondCompleted.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    SecondCompleted.FunctionName = TEXT("PrintString");
    SecondCompleted.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    SecondCompleted.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"),
        TEXT("Second completed")));
    FBPIRLink Next;
    Next.SourceNode = FirstCompleted.Id;
    Next.SourcePin = FirstCompleted.Pins.Last().Id;
    Next.TargetNode = SecondCompleted.Id;
    Next.TargetPin = SecondCompleted.Pins[0].Id;
    Graph.ExecEdges.Add(Next);
    Graph.Nodes.Add(MoveTemp(SecondCompleted));

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("Completed直列チェーンのLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Loop = SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("Completedは2文"), Loop.CompletedStatements.Num(), 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Completed直列チェーンのC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopChainActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("Completedの順序を維持"),
        Files.SourceContent.Find(TEXT("Second completed")) >
        Files.SourceContent.Find(TEXT("Loop completed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopMultipleCompletedCodegenTest,
    "BlueprintAIConverter.Conversion.ForLoop.MultipleCompletedStatements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopMultipleCompletedCodegenTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR GraphIR = MakeForLoopGraph();
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("ForLoop Loweringに成功"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }

    FBPSemanticStatement& Loop = SemanticIR.Functions[0].Blocks[0].Statements[0];
    // 既存のCompleted文を複製して、Semantic IRの複数文生成を検証する。
    // Graph IRの複数Completed接続のLoweringは別途対応が必要。
    FBPSemanticStatement SecondCompleted = Loop.CompletedStatements[0];
    SecondCompleted.Expression.Arguments[1].LiteralValue = TEXT("Second completed");
    Loop.CompletedStatements.Add(MoveTemp(SecondCompleted));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("複数Completed文のC++生成に成功"),
        FBPSemanticCodeGenerator::Generate(
            SemanticIR, TEXT("AForLoopCompletedActor"), TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 FirstPosition = Files.SourceContent.Find(TEXT("Loop completed"));
    const int32 SecondPosition = Files.SourceContent.Find(TEXT("Second completed"));
    TestTrue(TEXT("Completed文が順番に生成される"),
        FirstPosition != INDEX_NONE && SecondPosition > FirstPosition);
    TestTrue(TEXT("Completed文はfor文の後に生成される"),
        FirstPosition > Files.SourceContent.Find(TEXT("++LoopIndex")));
    return true;
}

#endif
