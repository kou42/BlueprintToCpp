#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPSemanticCodeGenerator.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticLowering.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedIndexAdditionTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedIndexAddition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedIndexAdditionTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.RemoveAt(5);
    Graph.Nodes.RemoveAt(4);
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0, false);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0, false);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0, false);
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    Connect(Graph.Nodes[1], 4, Addition, 0, true);
    Connect(Graph.Nodes[2], 4, Addition, 1, true);
    Connect(Addition, 2, Converter, 0, true);
    Connect(Converter, 1, Graph.Nodes[3], 1, true);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Converter));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二重ForLoop Index加算Lowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoop Index加算CodeGen"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopIndexSumActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("外側と内側Indexの加算"),
        Files.SourceContent.Contains(TEXT(
            "UKismetStringLibrary::Conv_IntToString(UKismetMathLibrary::Add_IntInt(LoopIndex, InnerIndex))")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedArithmeticTreeTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedArithmeticTree",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedArithmeticTreeTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.RemoveAt(5);
    Graph.Nodes.RemoveAt(4);
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0, false);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0, false);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0, false);
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    Connect(Graph.Nodes[1], 4, Addition, 0, true);
    Connect(Graph.Nodes[2], 4, Addition, 1, true);
    FBPIRNode Multiplication = Addition;
    Multiplication.Id = FGuid::NewGuid();
    Multiplication.FunctionName = TEXT("Multiply_IntInt");
    for (FBPIRPin& Pin : Multiplication.Pins)
    {
        Pin.Id = FGuid::NewGuid();
    }
    Connect(Addition, 2, Multiplication, 0, true);
    Connect(Graph.Nodes[2], 4, Multiplication, 1, true);
    Connect(Multiplication, 2, Converter, 0, true);
    Connect(Converter, 1, Graph.Nodes[3], 1, true);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Multiplication));
    Graph.Nodes.Add(MoveTemp(Converter));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二重ForLoop 多段Index演算Lowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoop 多段Index演算CodeGen"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopArithmeticTreeActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("外側と内側Indexの加算"),
        Files.SourceContent.Contains(TEXT(
            "UKismetStringLibrary::Conv_IntToString(UKismetMathLibrary::Multiply_IntInt(UKismetMathLibrary::Add_IntInt(LoopIndex, InnerIndex), InnerIndex))")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedIndexSubtractionTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedIndexSubtraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedIndexSubtractionTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.RemoveAt(5);
    Graph.Nodes.RemoveAt(4);
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0, false);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0, false);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0, false);
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Subtract_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    Connect(Graph.Nodes[1], 4, Addition, 0, true);
    Connect(Graph.Nodes[2], 4, Addition, 1, true);
    Connect(Addition, 2, Converter, 0, true);
    Connect(Converter, 1, Graph.Nodes[3], 1, true);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Converter));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二重ForLoop Index減算Lowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoop Index減算CodeGen"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopIndexSubtractActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("外側と内側Indexの減算"),
        Files.SourceContent.Contains(TEXT(
            "UKismetStringLibrary::Conv_IntToString(UKismetMathLibrary::Subtract_IntInt(LoopIndex, InnerIndex))")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedIndexMultiplicationTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedIndexMultiplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedIndexMultiplicationTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.RemoveAt(5);
    Graph.Nodes.RemoveAt(4);
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0, false);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0, false);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0, false);
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Multiply_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    Connect(Graph.Nodes[1], 4, Addition, 0, true);
    Connect(Graph.Nodes[2], 4, Addition, 1, true);
    Connect(Addition, 2, Converter, 0, true);
    Connect(Converter, 1, Graph.Nodes[3], 1, true);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Converter));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二重ForLoop Index乗算Lowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoop Index乗算CodeGen"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopIndexMultiplyActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("外側と内側Indexの乗算"),
        Files.SourceContent.Contains(TEXT(
            "UKismetStringLibrary::Conv_IntToString(UKismetMathLibrary::Multiply_IntInt(LoopIndex, InnerIndex))")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedMultipleIndexConvertersTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedMultipleIndexConverters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedMultipleIndexConvertersTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.RemoveAt(5);
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0, false);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0, false);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0, false);
    Connect(Graph.Nodes[2], 5, Graph.Nodes[4], 0, false);
    for (int32 I = 0; I < 2; ++I)
    {
        FBPIRNode Converter;
        Converter.Id = FGuid::NewGuid();
        Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
        Converter.FunctionName = TEXT("Conv_IntToString");
        Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
        Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
        Connect(Graph.Nodes[I == 0 ? 2 : 1], 4, Converter, 0, true);
        Connect(Converter, 1, Graph.Nodes[I == 0 ? 3 : 4], 1, true);
        Graph.Nodes.Add(MoveTemp(Converter));
    }
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("複数Index変換のLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("複数Index変換のコード生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopMultiIndexActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("内側Index"), Files.SourceContent.Contains(
        TEXT("UKismetStringLibrary::Conv_IntToString(InnerIndex)")));
    TestTrue(TEXT("外側Index"), Files.SourceContent.Contains(
        TEXT("UKismetStringLibrary::Conv_IntToString(LoopIndex)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedIndexToStringTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedIndexToString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedIndexToStringTest::RunTest(const FString& Parameters)
{
    for (int32 IndexSource = 0; IndexSource < 2; ++IndexSource)
    {
        FBPGraphIR GraphIR = MakeForLoopBranchGraph();
        FBPIRGraph& Graph = GraphIR.Graphs[0];
        FBPIRNode& Inner = Graph.Nodes[2];
        Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
        Inner.MacroGraphName = TEXT("ForLoop");
        Inner.Pins.Reset();
        Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
        Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
        Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
        Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
        Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
        Graph.Nodes.RemoveAt(5);
        Graph.Nodes.RemoveAt(4);
        Graph.ExecEdges.Reset();
        const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
            const FBPIRNode& Target, int32 Input, bool bData)
        {
            FBPIRLink Edge;
            Edge.SourceNode = Source.Id;
            Edge.SourcePin = Source.Pins[Output].Id;
            Edge.TargetNode = Target.Id;
            Edge.TargetPin = Target.Pins[Input].Id;
            if (bData)
            {
                Graph.DataEdges.Add(Edge);
            }
            else
            {
                Graph.ExecEdges.Add(Edge);
            }
        };
        Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0, false);
        Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0, false);
        Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0, false);
        FBPIRNode Converter;
        Converter.Id = FGuid::NewGuid();
        Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
        Converter.FunctionName = TEXT("Conv_IntToString");
        Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
        Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
        Connect(Graph.Nodes[IndexSource == 0 ? 1 : 2], IndexSource == 0 ? 4 : 4,
            Converter, 0, true);
        Connect(Converter, 1, Graph.Nodes[3], 1, true);
        Graph.Nodes.Add(MoveTemp(Converter));
        FBPSemanticIR SemanticIR;
        if (TestTrue(TEXT("二重ループのIndex変換Lowering"),
            FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
        {
            return false;
        }
        FBPGeneratedActorFiles Files;
        TArray<FString> Diagnostics;
        if (TestTrue(TEXT("二重ループのIndex変換C++生成"),
            FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopIndexActor"),
                TEXT("TEST_API"), Files, Diagnostics)) == false)
        {
            return false;
        }
        const FString Name = IndexSource == 0 ? TEXT("LoopIndex") : TEXT("InnerIndex");
        TestTrue(TEXT("対応するIndexの文字列変換"),
            Files.SourceContent.Contains(FString::Printf(
                TEXT("UKismetStringLibrary::Conv_IntToString(%s)"), *Name)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedLoopCompletedTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedLoopCompleted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedLoopCompletedTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    // 既存のTrue/False/Completed PrintStringをそれぞれBody/内側Completed/外側Completedへ割り当てる。
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        Graph.ExecEdges.Add(Edge);
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0);
    Connect(Graph.Nodes[2], 5, Graph.Nodes[4], 0);
    Connect(Graph.Nodes[1], 5, Graph.Nodes[5], 0);

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二重ForLoopのCompletedをLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer =
        SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("内側Completed"), Outer.LoopBodyStatements[0].CompletedStatements.Num(), 1);
    TestEqual(TEXT("外側Completed"), Outer.CompletedStatements.Num(), 1);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopのCompletedをC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopCompletedActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 InnerLoop = Files.SourceContent.Find(TEXT("for (int32 InnerIndex"));
    const int32 InnerCompleted = Files.SourceContent.Find(TEXT("False body"));
    const int32 OuterCompleted = Files.SourceContent.Find(TEXT("Branch completed"));
    TestTrue(TEXT("内側Completedは内側ループの後"), InnerCompleted > InnerLoop);
    TestTrue(TEXT("外側Completedは内側Completedの後"), OuterCompleted > InnerCompleted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedLoopLoweringTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedLoopLowering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedLoopLoweringTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    // 既存のBranch例を二重ループの最小Exec構造に組み替える。
    FBPIRNode& Inner = Graph.Nodes[2];
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
    Inner.MacroGraphName = TEXT("ForLoop");
    Inner.Pins.Reset();
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("4")));
    Inner.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.RemoveAt(5);
    Graph.Nodes.RemoveAt(4);
    Graph.ExecEdges.Reset();
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        Graph.ExecEdges.Add(Edge);
    };
    Connect(Graph.Nodes[0], 0, Graph.Nodes[1], 0);
    Connect(Graph.Nodes[1], 3, Graph.Nodes[2], 0);
    Connect(Graph.Nodes[2], 3, Graph.Nodes[3], 0);
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二重ForLoopのLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer =
        SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("内側のForLoop"), Outer.LoopBodyStatements[0].Kind,
        EBPSemanticStatementKind::ForLoop);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopのC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopNestedGraphActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("内側のIndex変数"), Files.SourceContent.Contains(
        TEXT("for (int32 InnerIndex = 2; InnerIndex <= 4; ++InnerIndex)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedLoopCodegenTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedLoopCodegen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedLoopCodegenTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("外側ForLoopのLowering"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), SemanticIR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Outer =
        SemanticIR.Functions[0].Blocks[0].Statements[0];
    FBPSemanticStatement Inner;
    Inner.Kind = EBPSemanticStatementKind::ForLoop;
    Inner.LoopIndexVariable = TEXT("InnerIndex");
    Inner.LoopFirstIndex = Outer.LoopFirstIndex;
    Inner.LoopLastIndex = Outer.LoopLastIndex;
    Inner.LoopBodyStatements.Add(Outer.LoopBodyStatements[0]);
    Inner.CompletedStatements.Add(Outer.LoopBodyStatements[0]);
    Outer.LoopBodyStatements.Reset();
    Outer.LoopBodyStatements.Add(MoveTemp(Inner));
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopのC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopNestedActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("外側ループ"), Files.SourceContent.Contains(
        TEXT("for (int32 LoopIndex = 1; LoopIndex <= 3; ++LoopIndex)")));
    TestTrue(TEXT("内側ループ"), Files.SourceContent.Contains(
        TEXT("for (int32 InnerIndex = 1; InnerIndex <= 3; ++InnerIndex)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopMultipleNestedComparisonsTest,
    "BlueprintAIConverter.Conversion.ForLoop.MultipleNestedComparisons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopMultipleNestedComparisonsTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode Inner;
    Inner.Id = FGuid::NewGuid();
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Condition"), EGPD_Input, TEXT("bool")));
    Inner.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("else"), EGPD_Output, TEXT("exec")));
    FBPIRNode InnerFalse = Graph.Nodes[4];
    InnerFalse.Id = FGuid::NewGuid();
    InnerFalse.Pins[0].Id = FGuid::NewGuid();
    InnerFalse.Pins[1].Id = FGuid::NewGuid();
    InnerFalse.Pins[1].DefaultValue = TEXT("Inner false");
    const auto MakeComparison = [](const TCHAR* Name, const TCHAR* Value)
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Node.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
        Node.FunctionName = Name;
        Node.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), Value));
        Node.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("bool")));
        return Node;
    };
    FBPIRNode OuterComparison = MakeComparison(TEXT("Less_IntInt"), TEXT("5"));
    FBPIRNode InnerComparison = MakeComparison(TEXT("Greater_IntInt"), TEXT("1"));
    for (FBPIRLink& Edge : Graph.ExecEdges)
    {
        if (Edge.SourceNode == Graph.Nodes[2].Id &&
            Edge.TargetNode == Graph.Nodes[3].Id)
        {
            Edge.TargetNode = Inner.Id;
            Edge.TargetPin = Inner.Pins[0].Id;
        }
    }
    const auto Connect = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    Connect(Inner, 2, Graph.Nodes[3], 0, false);
    Connect(Inner, 3, InnerFalse, 0, false);
    Connect(OuterComparison, 2, Graph.Nodes[2], 1, true);
    Connect(InnerComparison, 2, Inner, 1, true);
    Connect(Graph.Nodes[1], 4, OuterComparison, 0, true);
    // 内側条件は (LoopIndex + 1) * 2 > 1 とし、多段演算を検証する。
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("1")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    FBPIRNode Multiplication;
    Multiplication.Id = FGuid::NewGuid();
    Multiplication.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Multiplication.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Multiplication.FunctionName = TEXT("Multiply_IntInt");
    Multiplication.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Multiplication.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("2")));
    Multiplication.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    Connect(Graph.Nodes[1], 4, Addition, 0, true);
    Connect(Addition, 2, Multiplication, 0, true);
    Connect(Multiplication, 2, InnerComparison, 0, true);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Multiplication));
    Graph.Nodes.Add(MoveTemp(Inner));
    Graph.Nodes.Add(MoveTemp(InnerFalse));
    Graph.Nodes.Add(MoveTemp(OuterComparison));
    Graph.Nodes.Add(MoveTemp(InnerComparison));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("二つのネスト比較をLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二つのネスト比較をC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopMultipleCompareActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("外側比較"), Files.SourceContent.Contains(TEXT("if (LoopIndex < 5)")));
    TestTrue(TEXT("内側の多段算術比較"),
        Files.SourceContent.Contains(TEXT("UKismetMathLibrary::Multiply_IntInt(UKismetMathLibrary::Add_IntInt(LoopIndex, 1), 2) > 1")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedIndexComparisonTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedIndexComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedIndexComparisonTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode Inner;
    Inner.Id = FGuid::NewGuid();
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Condition"), EGPD_Input, TEXT("bool")));
    Inner.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("else"), EGPD_Output, TEXT("exec")));
    FBPIRNode InnerFalse;
    InnerFalse.Id = FGuid::NewGuid();
    InnerFalse.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    InnerFalse.FunctionName = TEXT("PrintString");
    InnerFalse.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    InnerFalse.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"),
        TEXT("Inner comparison false")));
    FBPIRNode Comparison;
    Comparison.Id = FGuid::NewGuid();
    Comparison.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Comparison.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Comparison.FunctionName = TEXT("Less_IntInt");
    Comparison.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("2")));
    Comparison.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("bool")));
    for (FBPIRLink& Edge : Graph.ExecEdges)
    {
        if (Edge.SourceNode == Graph.Nodes[2].Id && Edge.TargetNode == Graph.Nodes[3].Id)
        {
            Edge.TargetNode = Inner.Id;
            Edge.TargetPin = Inner.Pins[0].Id;
        }
    }
    const auto AddEdge = [&Graph](const FBPIRNode& Source, int32 Output,
        const FBPIRNode& Target, int32 Input, bool bData)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Source.Id;
        Edge.SourcePin = Source.Pins[Output].Id;
        Edge.TargetNode = Target.Id;
        Edge.TargetPin = Target.Pins[Input].Id;
        if (bData)
        {
            Graph.DataEdges.Add(Edge);
        }
        else
        {
            Graph.ExecEdges.Add(Edge);
        }
    };
    AddEdge(Inner, 2, Graph.Nodes[3], 0, false);
    AddEdge(Inner, 3, InnerFalse, 0, false);
    AddEdge(Comparison, 2, Inner, 1, true);
    AddEdge(Graph.Nodes[1], 4, Comparison, 0, true);
    Graph.Nodes.Add(MoveTemp(Inner));
    Graph.Nodes.Add(MoveTemp(InnerFalse));
    Graph.Nodes.Add(MoveTemp(Comparison));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("ネスト内Index比較のLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ネスト内Index比較のC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopNestedCompareActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("ネストしたIndex比較を生成"),
        Files.SourceContent.Contains(TEXT("if (LoopIndex < 2)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedBranchLoweringTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedBranchLowering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedBranchLoweringTest::RunTest(const FString& Parameters)
{
    FBPGraphIR GraphIR = MakeForLoopBranchGraph();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode Inner;
    Inner.Id = FGuid::NewGuid();
    Inner.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
    Inner.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("Condition"), EGPD_Input, TEXT("bool"), TEXT("false")));
    Inner.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Inner.Pins.Add(MakeLoopPin(TEXT("else"), EGPD_Output, TEXT("exec")));
    FBPIRNode InnerFalse;
    InnerFalse.Id = FGuid::NewGuid();
    InnerFalse.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    InnerFalse.FunctionName = TEXT("PrintString");
    InnerFalse.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    InnerFalse.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"),
        TEXT("Inner false")));
    // 既存の外側True経路に内側Branchを挿入し、True/Falseの葉を分離する。
    for (FBPIRLink& Edge : Graph.ExecEdges)
    {
        if (Edge.SourceNode == Graph.Nodes[2].Id &&
            Edge.TargetNode == Graph.Nodes[3].Id)
        {
            Edge.TargetNode = Inner.Id;
            Edge.TargetPin = Inner.Pins[0].Id;
        }
    }
    FBPIRLink InnerTrue;
    InnerTrue.SourceNode = Inner.Id;
    InnerTrue.SourcePin = Inner.Pins[2].Id;
    InnerTrue.TargetNode = Graph.Nodes[3].Id;
    InnerTrue.TargetPin = Graph.Nodes[3].Pins[0].Id;
    Graph.ExecEdges.Add(InnerTrue);
    FBPIRLink InnerElse;
    InnerElse.SourceNode = Inner.Id;
    InnerElse.SourcePin = Inner.Pins[3].Id;
    InnerElse.TargetNode = InnerFalse.Id;
    InnerElse.TargetPin = InnerFalse.Pins[0].Id;
    Graph.ExecEdges.Add(InnerElse);
    Graph.Nodes.Add(MoveTemp(Inner));
    Graph.Nodes.Add(MoveTemp(InnerFalse));
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("ネストBranchのLowering"),
        FBPSemanticLowering::LowerForLoop(GraphIR, SemanticIR)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer =
        SemanticIR.Functions[0].Blocks[0].Statements[0].LoopBodyStatements[0];
    TestEqual(TEXT("外側TrueにBranchを保持"), Outer.ThenStatements[0].Kind,
        EBPSemanticStatementKind::Branch);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ネストBranchのC++生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR, TEXT("AForLoopNestedGraphActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("内側Falseの出力"), Files.SourceContent.Contains(TEXT("Inner false")));
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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachUnsupportedDiagnosticTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.UnresolvedArrayDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachUnsupportedDiagnosticTest::RunTest(const FString& Parameters)
{
    // 配列要素型が未解決のForEachを成功扱いせず、元ノードGUIDまで報告する。
    for (const FString MacroName : { FString(TEXT("ForEachLoop")),
                                     FString(TEXT("ForEachLoopWithBreak")) })
    {
        FBPGraphIR GraphIR;
        GraphIR.AssetPath = TEXT("/Game/BP_ForEach");
        GraphIR.ParentClassPath = TEXT("/Script/Engine.Actor");
        FBPIRGraph Graph;
        Graph.Name = TEXT("EventGraph");
        Graph.Kind = TEXT("event");
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.MacroGraphName = MacroName;
        Graph.Nodes.Add(Node);
        GraphIR.Graphs.Add(Graph);

        FBPSemanticIR SemanticIR;
        TestFalse(TEXT("未解決ForEachを拒否"), FBPSemanticLowering::Lower(nullptr, GraphIR, SemanticIR));
        TestTrue(TEXT("未解決ForEachに診断あり"), SemanticIR.HasErrors());
        if (SemanticIR.Diagnostics.IsEmpty() == false)
        {
            TestEqual(TEXT("元ノードGUIDを保持"), SemanticIR.Diagnostics[0].NodeId, Node.Id);
            TestTrue(TEXT("Macro名を診断に保持"),
                SemanticIR.Diagnostics[0].Message.Contains(MacroName));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachJsonContractTest,
    "BlueprintAIConverter.Export.ForEachLoop.SemanticJsonContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachJsonContractTest::RunTest(const FString& Parameters)
{
    // 実際のLowering完了前でも、専用の配列・要素情報がJSON契約で失われないことを確認する。
    FBPSemanticIR IR;
    IR.AssetPath = TEXT("/Game/BP_ForEach");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::ForEachLoop;
    Statement.LoopElementVariable = TEXT("Element");
    Statement.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Statement.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Statement.LoopArray.VariableName = TEXT("Numbers");
    Statement.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticStatement BreakStatement;
    BreakStatement.Kind = EBPSemanticStatementKind::Break;
    Statement.LoopBodyStatements.Add(BreakStatement);
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);

    FString Json;
    TestTrue(TEXT("ForEach JSONをシリアライズ"), FBPSemanticJsonSerializer::Serialize(IR, TEXT("5.7.4"), Json));
    TestTrue(TEXT("ForEach文種別"), Json.Contains(TEXT("for_each_loop")));
    TestTrue(TEXT("配列入力"), Json.Contains(TEXT("loop_array")));
    TestTrue(TEXT("要素変数"), Json.Contains(TEXT("loop_element_variable")) && Json.Contains(TEXT("Element")));
    TestTrue(TEXT("要素型"), Json.Contains(TEXT("loop_element_type")));
    TestTrue(TEXT("Break文種別"), Json.Contains(TEXT("\"kind\": \"break\"")));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachCodeGenRejectsUnresolvedTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.RejectsUnresolvedArray",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachCodeGenRejectsUnresolvedTest::RunTest(const FString& Parameters)
{
    // 型が未確定の配列に対して、推測でC++を生成しないことを確認する。
    FBPSemanticIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ArrayIndex");
    Loop.LoopElementVariable = TEXT("ArrayElement");
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("MissingArray");
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    Block.Statements.Add(Loop);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("未解決配列を拒否"),
        FBPSemanticCodeGenerator::Generate(
            IR, TEXT("AForEachActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("拒否理由を診断"), Diagnostics.IsEmpty() == false);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachIndexCodeGenTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.IndexToStringCodeGen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachIndexCodeGenTest::RunTest(const FString& Parameters)
{
    // 既存のForLoopテストから検証済みPrintString式を再利用する。
    // Index参照がForEachローカル変数として生成されることを確認する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintString式を含むIRの生成"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Loop = IR.Functions[0].Blocks[0].Statements[0];
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Numbers");
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.Type.ContainerKind = TEXT("array");
    Variable.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Variable);
    // 元のForLoopのIndex式をForEachローカルIndexへ置換する。
    FBPSemanticExpression& IndexText = Loop.LoopBodyStatements[0].Expression.Arguments[1];
    if (TestTrue(TEXT("Index文字列変換式が存在"),
        IndexText.Kind == EBPSemanticExpressionKind::FunctionCall &&
        IndexText.Arguments.Num() == 1) == false)
    {
        return false;
    }
    IndexText.Arguments[0].VariableName = Loop.LoopIndexVariable;
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ForEach Indexのコード生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachIndexActor"),
            TEXT("TEST_API"), Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("配列長で反復"), Files.SourceContent.Contains(TEXT("Numbers.Num()")));
    TestTrue(TEXT("Indexを文字列化"),
        Files.SourceContent.Contains(TEXT("Conv_IntToString(ForEachArrayIndex)")));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachBreakCodeGenTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.BreakCodeGen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachBreakCodeGenTest::RunTest(const FString& Parameters)
{
    // ForEachLoopWithBreakのLoweringに先立ち、Semantic IRの明示的なBreakを
    // ループ本体の順序どおりにC++へ生成できることを固定する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintString式を含むIRの生成"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Loop = IR.Functions[0].Blocks[0].Statements[0];
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Numbers");
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.Type.ContainerKind = TEXT("array");
    Variable.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Variable);
    FBPSemanticExpression& IndexText = Loop.LoopBodyStatements[0].Expression.Arguments[1];
    if (TestTrue(TEXT("BreakテストのIndex文字列変換式が存在"),
        IndexText.Kind == EBPSemanticExpressionKind::FunctionCall &&
        IndexText.Arguments.Num() == 1) == false)
    {
        return false;
    }
    IndexText.Arguments[0].VariableName = Loop.LoopIndexVariable;
    FBPSemanticStatement BreakStatement;
    BreakStatement.Kind = EBPSemanticStatementKind::Break;
    Loop.LoopBodyStatements.Add(MoveTemp(BreakStatement));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ForEach Breakのコード生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachBreakActor"),
            TEXT("TEST_API"), Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    const int32 PrintPosition = Files.SourceContent.Find(TEXT("PrintString"));
    const int32 BreakPosition = Files.SourceContent.Find(TEXT("break;"));
    TestTrue(TEXT("Breakを本体のPrintString後へ生成"),
        PrintPosition != INDEX_NONE && BreakPosition > PrintPosition);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopGenericExpressionCodeGenTest,
    "BlueprintAIConverter.Conversion.ForLoop.GenericExpressionCodeGen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopGenericExpressionCodeGenTest::RunTest(const FString& Parameters)
{
    // ループ本体がPrintStringという関数名に依存せず、共通の検証済み式文Emitterを
    // 利用することをSetActorLocation経路で確認する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("ForLoop IRを生成"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Loop = IR.Functions[0].Blocks[0].Statements[0];
    Loop.LoopBodyStatements.Reset();
    Loop.CompletedStatements.Reset();

    FBPSemanticStatement MoveStatement;
    MoveStatement.Kind = EBPSemanticStatementKind::Expression;
    FBPSemanticExpression& MoveCall = MoveStatement.Expression;
    MoveCall.Kind = EBPSemanticExpressionKind::FunctionCall;
    MoveCall.Type.Kind = EBPSemanticTypeKind::Bool;
    MoveCall.FunctionName = TEXT("K2_SetActorLocation");
    MoveCall.FunctionOwnerPath = TEXT("/Script/Engine.Actor");

    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = IR.ParentClassPath;
    Target.VariableName = TEXT("this");
    FBPSemanticExpression Location;
    Location.Kind = EBPSemanticExpressionKind::FunctionCall;
    Location.Type.Kind = EBPSemanticTypeKind::Struct;
    Location.Type.TypePath = TEXT("/Script/CoreUObject.Vector");
    Location.FunctionName = TEXT("K2_GetActorLocation");
    Location.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Location.Arguments.Add(Target);
    FBPSemanticExpression Sweep;
    Sweep.Kind = EBPSemanticExpressionKind::Literal;
    Sweep.Type.Kind = EBPSemanticTypeKind::Bool;
    Sweep.LiteralValue = TEXT("false");
    FBPSemanticExpression HitResult;
    HitResult.Kind = EBPSemanticExpressionKind::VariableReference;
    HitResult.Type.Kind = EBPSemanticTypeKind::Struct;
    HitResult.Type.TypePath = TEXT("/Script/Engine.HitResult");
    HitResult.VariableName = TEXT("LoopHitResult");
    FBPSemanticExpression Teleport;
    Teleport.Kind = EBPSemanticExpressionKind::Literal;
    Teleport.Type.Kind = EBPSemanticTypeKind::Bool;
    Teleport.LiteralValue = TEXT("false");
    MoveCall.Arguments.Add(MoveTemp(Target));
    MoveCall.Arguments.Add(MoveTemp(Location));
    MoveCall.Arguments.Add(MoveTemp(Sweep));
    MoveCall.Arguments.Add(MoveTemp(HitResult));
    MoveCall.Arguments.Add(MoveTemp(Teleport));
    Loop.LoopBodyStatements.Add(MoveTemp(MoveStatement));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ForLoop本体の汎用式文を生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForLoopGenericActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 LoopPosition = Files.SourceContent.Find(TEXT("for (int32 LoopIndex"));
    const int32 HitPosition = Files.SourceContent.Find(TEXT("FHitResult LoopHitResult{};"));
    const int32 MovePosition = Files.SourceContent.Find(
        TEXT("K2_SetActorLocation(GetActorLocation(), false, LoopHitResult, false);"));
    TestTrue(TEXT("SetActorLocationをループ本体へ順序どおり生成"),
        LoopPosition != INDEX_NONE && HitPosition > LoopPosition && MovePosition > HitPosition);
    TestFalse(TEXT("PrintString専用経路を通さない"),
        Files.SourceContent.Contains(TEXT("UKismetSystemLibrary::PrintString")));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachElementCodeGenTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.ElementToStringCodeGen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachElementCodeGenTest::RunTest(const FString& Parameters)
{
    // 既存のForLoopテストから検証済みPrintString式を再利用する。
    // Index参照がForEachローカル変数として生成されることを確認する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintString式を含むIRの生成"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Loop = IR.Functions[0].Blocks[0].Statements[0];
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Numbers");
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.Type.ContainerKind = TEXT("array");
    Variable.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Variable);
    // 元のForLoopのIndex式をForEachローカルIndexへ置換する。
    FBPSemanticExpression& IndexText = Loop.LoopBodyStatements[0].Expression.Arguments[1];
    if (TestTrue(TEXT("Index文字列変換式が存在"),
        IndexText.Kind == EBPSemanticExpressionKind::FunctionCall &&
        IndexText.Arguments.Num() == 1) == false)
    {
        return false;
    }
    IndexText.Arguments[0].VariableName = Loop.LoopElementVariable;
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ForEach Indexのコード生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachIndexActor"),
            TEXT("TEST_API"), Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("配列長で反復"), Files.SourceContent.Contains(TEXT("Numbers.Num()")));
    TestTrue(TEXT("Elementを文字列化"),
        Files.SourceContent.Contains(TEXT("Conv_IntToString(ForEachArrayElement)")));
    TestTrue(TEXT("要素変数を生成"),
        Files.SourceContent.Contains(TEXT("const int32 ForEachArrayElement = Numbers[ForEachArrayIndex];")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachIndexElementTogetherCodeGenTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.IndexAndElementCodeGen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachIndexElementTogetherCodeGenTest::RunTest(const FString& Parameters)
{
    // 同じループ内でIndexとElementを使った場合、両方が正しいスコープに
    // 出力されることを確認する。Printfの引数対応ずれも検出する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("元のForLoopをLowering"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Loop = IR.Functions[0].Blocks[0].Statements[0];
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Numbers");
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.Type.ContainerKind = TEXT("array");
    Variable.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Variable);

    if (TestTrue(TEXT("2個のBody PrintStringが存在"),
        Loop.LoopBodyStatements.Num() >= 2) == false)
    {
        return false;
    }
    FBPSemanticExpression& IndexText = Loop.LoopBodyStatements[0].Expression.Arguments[1];
    if (TestTrue(TEXT("Index変換式が存在"),
        IndexText.Kind == EBPSemanticExpressionKind::FunctionCall &&
        IndexText.Arguments.Num() == 1) == false)
    {
        return false;
    }
    IndexText.Arguments[0].VariableName = Loop.LoopIndexVariable;
    FBPSemanticExpression& ElementText = Loop.LoopBodyStatements[1].Expression.Arguments[1];
    ElementText = IndexText;
    ElementText.Arguments[0].VariableName = Loop.LoopElementVariable;

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("IndexとElementのコード生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachBothActor"),
            TEXT("TEST_API"), Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("正しいループヘッダ"),
        Files.SourceContent.Contains(TEXT("for (int32 ForEachArrayIndex = 0; ForEachArrayIndex < Numbers.Num(); ++ForEachArrayIndex)")));
    const int32 Declaration = Files.SourceContent.Find(
        TEXT("const int32 ForEachArrayElement = Numbers[ForEachArrayIndex];"));
    const int32 IndexPrint = Files.SourceContent.Find(
        TEXT("Conv_IntToString(ForEachArrayIndex)"));
    const int32 ElementPrint = Files.SourceContent.Find(
        TEXT("Conv_IntToString(ForEachArrayElement)"));
    TestTrue(TEXT("要素宣言と2個のPrintStringが正しい順序"),
        Declaration != INDEX_NONE && IndexPrint > Declaration &&
        ElementPrint > IndexPrint);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachTypeMismatchCodeGenTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.RejectsMismatchedArrayType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachTypeMismatchCodeGenTest::RunTest(const FString& Parameters)
{
    // 配列の型がSemantic IR内で食い違う場合は、型推測で生成しない。
    FBPSemanticIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::String;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Numbers");
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.Type.ContainerKind = TEXT("array");
    Variable.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Variable);
    Block.Statements.Add(MoveTemp(Loop));
    Function.Blocks.Add(MoveTemp(Block));
    IR.Functions.Add(MoveTemp(Function));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("矛盾する配列型は拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachTypeActor"),
            TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("型矛盾の診断を出力"), Diagnostics.IsEmpty() == false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachLocalNameCollisionTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.RejectsLocalNameCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachLocalNameCollisionTest::RunTest(const FString& Parameters)
{
    // Blueprintメンバー名が生成予定のIndex/Elementと重なると、
    // C++のローカル変数による名前隠蔽が起きるため生成を拒否する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("元のForLoopをLowering"),
        FBPSemanticLowering::LowerForLoop(MakeForLoopGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Loop = IR.Functions[0].Blocks[0].Statements[0];
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    FBPSemanticVariable Array;
    Array.Name = TEXT("Numbers");
    Array.Type.Kind = EBPSemanticTypeKind::Int32;
    Array.Type.ContainerKind = TEXT("array");
    Array.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Array);
    FBPSemanticVariable Colliding;
    Colliding.Name = Loop.LoopElementVariable;
    Colliding.Type.Kind = EBPSemanticTypeKind::Int32;
    Colliding.ReflectedCppType = TEXT("int32");
    IR.Variables.Add(Colliding);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("要素ローカル変数との名前衝突を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachCollisionActor"),
            TEXT("TEST_API"), Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("拒否理由がある"), Diagnostics.IsEmpty() == false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForEachCompiledBlueprintLoweringTest,
    "BlueprintAIConverter.Conversion.ForEachLoop.CompiledBlueprintLowering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForEachCompiledBlueprintLoweringTest::RunTest(const FString& Parameters)
{
    // Graphのノード・リンクは既存のForLoop fixtureをForEach形状へ組み替える。
    // 配列変数だけは実際にコンパイルしたBlueprintから反射情報を取得する。
    // このテストはGraph IR以降の縦断検証であり、実ノードの配置テストとは区別する。
    const FName Name = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_ForEachLowering"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), Name, BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("一時Blueprint"), Blueprint) == false)
    {
        return false;
    }
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Int;
    Type.ContainerType = EPinContainerType::Array;
    if (TestTrue(TEXT("配列変数を追加"),
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Numbers"), Type)) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("GeneratedClass"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    FBPGraphIR GraphIR = MakeForLoopGraph();
    GraphIR.AssetPath = Blueprint->GetPathName();
    GraphIR.BlueprintVariableCount = Blueprint->NewVariables.Num();
    FBPIRGraph& Graph = GraphIR.Graphs[0];
    FBPIRNode& Loop = Graph.Nodes[1];
    Loop.MacroGraphName = TEXT("ForEachLoop");
    Loop.MacroGraphPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForEachLoop");
    // 既存fixtureのExec/Dataリンクが参照するPinIdは変更しない。
    // ForLoop由来のピンをForEachの名前・方向に置き換える。
    // 実ノードのピン配置と一致するかは別途Editor Golden Testで確認する。
    Loop.Pins[0].Name = TEXT("Exec");
    Loop.Pins[1].Name = TEXT("Array");
    Loop.Pins[1].DefaultValue.Reset();
    Loop.Pins[2].Name = TEXT("Array Element");
    Loop.Pins[2].Direction = EGPD_Output;
    Loop.Pins[2].DefaultValue.Reset();
    Loop.Pins[4].Name = TEXT("Array Index");
    // Completedは元のPins[5]、LoopBodyはPins[3]に保持される。
    // 実ノードではピン配列順に意味がないため、名前とGUIDで検証する。
    FBPIRNode Getter;
    Getter.Id = FGuid::NewGuid();
    Getter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_VariableGet");
    Getter.VariableName = TEXT("Numbers");
    Getter.bVariableSelfContext = true;
    Getter.Pins.Add(MakeLoopPin(TEXT("Numbers"), EGPD_Output, TEXT("int")));
    FBPIRLink ArrayEdge;
    ArrayEdge.SourceNode = Getter.Id;
    ArrayEdge.SourcePin = Getter.Pins[0].Id;
    ArrayEdge.TargetNode = Loop.Id;
    ArrayEdge.TargetPin = Loop.Pins[1].Id;
    Graph.DataEdges.Add(ArrayEdge);
    // GUIDの参照整合性が崩れると、Loweringは接続のない配列として拒否する。
    // fixtureのピン差し替えによる回帰をここで検出する。
    TestTrue(TEXT("配列入力のPinIdがデータリンクと一致"),
        Graph.DataEdges.Last().TargetPin == Loop.Pins[1].Id);
    TestTrue(TEXT("Index出力のPinIdが変換ノードへのリンクと一致"),
        Graph.DataEdges[0].SourcePin == Loop.Pins[4].Id);
    Graph.Nodes.Add(MoveTemp(Getter));
    for (FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.FunctionName == TEXT("PrintString"))
        {
            Node.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");
        }
    }

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("コンパイル済みBlueprintからForEachをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    TestTrue(TEXT("ForEach文へ構造化"),
        SemanticIR.Functions.Num() == 1 &&
        SemanticIR.Functions[0].Blocks.Num() == 1 &&
        SemanticIR.Functions[0].Blocks[0].Statements.Num() == 1 &&
        SemanticIR.Functions[0].Blocks[0].Statements[0].Kind ==
            EBPSemanticStatementKind::ForEachLoop);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    const bool bGenerated = FBPSemanticCodeGenerator::Generate(
        SemanticIR, TEXT("AForEachCompiledActor"), TEXT("TEST_API"),
        Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly);
    TestTrue(TEXT("Lowering結果からC++を生成"), bGenerated);
    if (bGenerated)
    {
        TestTrue(TEXT("配列長による反復"),
            Files.SourceContent.Contains(TEXT("Numbers.Num()")));
        TestTrue(TEXT("Indexの文字列変換"),
            Files.SourceContent.Contains(TEXT("Conv_IntToString(ForEachArrayIndex)")));
    }
    else
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
    }
    if (bGenerated == false)
    {
        return false;
    }

    // 実Blueprintで現れる BeginPlay -> PrintString -> ForEachLoop を再現する。
    // ループ前の文を保持しつつ、続くForEachを同じBlockへLoweringできることを検証する。
    FBPGraphIR EntryPrintGraph = GraphIR;
    FBPIRGraph& EntryPrintEvent = EntryPrintGraph.Graphs[0];
    FBPIRNode EntryPrint;
    EntryPrint.Id = FGuid::NewGuid();
    EntryPrint.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    EntryPrint.FunctionName = TEXT("PrintString");
    EntryPrint.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");
    EntryPrint.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
    EntryPrint.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    EntryPrint.Pins.Add(MakeLoopPin(
        TEXT("InString"), EGPD_Input, TEXT("string"), TEXT("Before loop")));
    FBPIRLink& OriginalEntry = EntryPrintEvent.ExecEdges[0];
    const FGuid LoopNodeId = OriginalEntry.TargetNode;
    const FGuid LoopExecPinId = OriginalEntry.TargetPin;
    OriginalEntry.TargetNode = EntryPrint.Id;
    OriginalEntry.TargetPin = EntryPrint.Pins[0].Id;
    FBPIRLink EntryPrintToLoop;
    EntryPrintToLoop.SourceNode = EntryPrint.Id;
    EntryPrintToLoop.SourcePin = EntryPrint.Pins[1].Id;
    EntryPrintToLoop.TargetNode = LoopNodeId;
    EntryPrintToLoop.TargetPin = LoopExecPinId;
    EntryPrintEvent.ExecEdges.Add(EntryPrintToLoop);
    EntryPrintEvent.Nodes.Add(MoveTemp(EntryPrint));

    FBPSemanticIR EntryPrintIR;
    if (TestTrue(TEXT("ループ前PrintStringを含むForEachをLowering"),
        FBPSemanticLowering::Lower(Blueprint, EntryPrintGraph, EntryPrintIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : EntryPrintIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    const TArray<FBPSemanticStatement>& EntryStatements =
        EntryPrintIR.Functions[0].Blocks[0].Statements;
    TestTrue(TEXT("ループ前文とForEachを順序どおり保持"),
        EntryStatements.Num() == 2 &&
        EntryStatements[0].Kind == EBPSemanticStatementKind::Expression &&
        EntryStatements[1].Kind == EBPSemanticStatementKind::ForEachLoop);
    FBPGeneratedActorFiles EntryPrintFiles;
    TArray<FString> EntryPrintDiagnostics;
    if (TestTrue(TEXT("ループ前文を含むForEachのC++生成"),
        FBPSemanticCodeGenerator::Generate(
            EntryPrintIR, TEXT("AForEachEntryPrintActor"), TEXT("TEST_API"),
            EntryPrintFiles, EntryPrintDiagnostics,
            EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : EntryPrintDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 EntryPrintPosition = EntryPrintFiles.SourceContent.Find(TEXT("Before loop"));
    const int32 EntryLoopPosition = EntryPrintFiles.SourceContent.Find(TEXT("Numbers.Num()"));
    TestTrue(TEXT("生成C++でもPrintStringがForEachより前"),
        EntryPrintPosition != INDEX_NONE && EntryLoopPosition > EntryPrintPosition);

    // Break入力を持つ標準マクロの限定形状を同じ実Blueprint型情報で縦断する。
    // 本体末尾からBreakへ戻るExecエッジを、暗黙に捨てずSemantic Breakへ変換する。
    FBPGraphIR BreakGraph = GraphIR;
    FBPIRGraph& BreakEvent = BreakGraph.Graphs[0];
    FBPIRNode& BreakLoop = BreakEvent.Nodes[1];
    BreakLoop.MacroGraphName = TEXT("ForEachLoopWithBreak");
    BreakLoop.MacroGraphPath =
        TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForEachLoopWithBreak");
    BreakLoop.Pins.Add(MakeLoopPin(TEXT("Break"), EGPD_Input, TEXT("exec")));
    FBPIRNode& LastBodyPrint = BreakEvent.Nodes[3];
    LastBodyPrint.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    FBPIRLink BreakEdge;
    BreakEdge.SourceNode = LastBodyPrint.Id;
    BreakEdge.SourcePin = LastBodyPrint.Pins.Last().Id;
    BreakEdge.TargetNode = BreakLoop.Id;
    BreakEdge.TargetPin = BreakLoop.Pins.Last().Id;
    BreakEvent.ExecEdges.Add(BreakEdge);

    FBPSemanticIR BreakIR;
    if (TestTrue(TEXT("Break付きForEachをLowering"),
        FBPSemanticLowering::Lower(Blueprint, BreakGraph, BreakIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : BreakIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    const TArray<FBPSemanticStatement>& BreakBody =
        BreakIR.Functions[0].Blocks[0].Statements[0].LoopBodyStatements;
    TestTrue(TEXT("本体末尾にBreak文を保持"),
        BreakBody.IsEmpty() == false &&
        BreakBody.Last().Kind == EBPSemanticStatementKind::Break);
    FBPGeneratedActorFiles BreakFiles;
    TArray<FString> BreakDiagnostics;
    if (TestTrue(TEXT("Break付きForEachのC++生成"),
        FBPSemanticCodeGenerator::Generate(
            BreakIR, TEXT("AForEachBreakCompiledActor"), TEXT("TEST_API"),
            BreakFiles, BreakDiagnostics, EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : BreakDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 BreakPosition = BreakFiles.SourceContent.Find(TEXT("break;"));
    const int32 CompletedPosition = BreakFiles.SourceContent.Find(TEXT("Loop completed"));
    TestTrue(TEXT("Breakをループ内かつCompletedより前へ生成"),
        BreakPosition != INDEX_NONE && CompletedPosition > BreakPosition);

    // 配列Getterから同じ入力への二重リンクは、リンク数が増えた時点で拒否する。
    // 正常なIRをコピーして破壊するため、以降のケース間で状態を共有しない。
    FBPGraphIR DuplicateGraph = GraphIR;
    DuplicateGraph.Graphs[0].DataEdges.Add(ArrayEdge);
    FBPSemanticIR DuplicateIR;
    TestFalse(TEXT("配列入力への重複リンクを拒否"),
        FBPSemanticLowering::Lower(Blueprint, DuplicateGraph, DuplicateIR));
    TestTrue(TEXT("重複リンクの拒否理由を保持"),
        DuplicateIR.HasErrors());

    // リンク数が正常でも、配列Getterが別の入力ピンへ接続されていれば
    // 正当な配列供給元と見なさない。リンク数だけの検証への退行を防ぐ。
    FBPGraphIR WrongPinGraph = GraphIR;
    WrongPinGraph.Graphs[0].DataEdges.Last().TargetPin = FGuid::NewGuid();
    FBPSemanticIR WrongPinIR;
    TestFalse(TEXT("配列入力の接続先GUID不一致を拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongPinGraph, WrongPinIR));
    TestTrue(TEXT("接続先GUID不一致の診断を保持"),
        WrongPinIR.HasErrors());

    // 同名のユーザー定義マクロや、別の標準マクロのGraphを
    // ForEachLoopと誤認してC++化しないことを検証する。
    FBPGraphIR WrongMacroPath = GraphIR;
    WrongMacroPath.Graphs[0].Nodes[1].MacroGraphPath =
        TEXT("/Game/Macros.CustomMacros:ForEachLoop");
    FBPSemanticIR WrongMacroPathIR;
    TestFalse(TEXT("同名のユーザーマクロを拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongMacroPath, WrongMacroPathIR));
    TestTrue(TEXT("ユーザーマクロの拒否理由を保持"),
        WrongMacroPathIR.HasErrors());

    FBPGraphIR WrongStandardMacro = GraphIR;
    WrongStandardMacro.Graphs[0].Nodes[1].MacroGraphPath =
        TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop");
    FBPSemanticIR WrongStandardMacroIR;
    TestFalse(TEXT("異なる標準マクロのGraphを拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongStandardMacro, WrongStandardMacroIR));
    TestTrue(TEXT("標準マクロGraph不一致の診断を保持"),
        WrongStandardMacroIR.HasErrors());

    // ForEach本体のElement/Index型も反射済みint32配列と整合する必要がある。
    // 変換ノード側がint32でも、Macroの出力型が異なれば拒否する。
    FBPGraphIR WrongElementType = GraphIR;
    WrongElementType.Graphs[0].Nodes[1].Pins[2].Category = TEXT("float");
    FBPSemanticIR WrongElementTypeIR;
    TestFalse(TEXT("ForEach Element出力の型不一致を拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongElementType, WrongElementTypeIR));
    TestTrue(TEXT("Element型不一致の診断を保持"),
        WrongElementTypeIR.HasErrors());

    FBPGraphIR WrongIndexType = GraphIR;
    WrongIndexType.Graphs[0].Nodes[1].Pins[4].Category = TEXT("float");
    FBPSemanticIR WrongIndexTypeIR;
    TestFalse(TEXT("ForEach Index出力の型不一致を拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongIndexType, WrongIndexTypeIR));
    TestTrue(TEXT("Index型不一致の診断を保持"),
        WrongIndexTypeIR.HasErrors());

    // 接続GUIDが正しくても、Conv_IntToStringのピン型が不正なら拒否する。
    // 型の違うノードを関数名だけで誤変換しないための回帰検証。
    FBPGraphIR WrongConverterInput = GraphIR;
    WrongConverterInput.Graphs[0].Nodes[4].Pins[0].Category = TEXT("float");
    FBPSemanticIR WrongConverterInputIR;
    TestFalse(TEXT("変換ノードの非int32入力を拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongConverterInput, WrongConverterInputIR));
    TestTrue(TEXT("変換入力型不一致の診断を保持"),
        WrongConverterInputIR.HasErrors());

    FBPGraphIR WrongConverterOutput = GraphIR;
    WrongConverterOutput.Graphs[0].Nodes[4].Pins[1].Category = TEXT("text");
    FBPSemanticIR WrongConverterOutputIR;
    TestFalse(TEXT("変換ノードの非string出力を拒否"),
        FBPSemanticLowering::Lower(Blueprint, WrongConverterOutput, WrongConverterOutputIR));
    TestTrue(TEXT("変換出力型不一致の診断を保持"),
        WrongConverterOutputIR.HasErrors());

    // 変換ノードへのIndexリンクをElementリンクに変更した場合は
    // 誤ってIndex式として出力せず、実際の接続元に従って型付けする。
    FBPGraphIR ElementGraph = GraphIR;
    ElementGraph.Graphs[0].DataEdges[0].SourcePin =
        ElementGraph.Graphs[0].Nodes[1].Pins[2].Id;
    FBPSemanticIR ElementIR;
    const bool bElementLowered =
        FBPSemanticLowering::Lower(Blueprint, ElementGraph, ElementIR);
    TestTrue(TEXT("Array Elementの接続元を識別"), bElementLowered);
    if (bElementLowered)
    {
        FBPGeneratedActorFiles ElementFiles;
        TArray<FString> ElementDiagnostics;
        const bool bElementGenerated = FBPSemanticCodeGenerator::Generate(
            ElementIR, TEXT("AForEachElementCompiledActor"), TEXT("TEST_API"),
            ElementFiles, ElementDiagnostics,
            EBPVariableFallbackMode::DeclarationOnly);
        TestTrue(TEXT("Element参照のC++生成"), bElementGenerated);
        if (bElementGenerated)
        {
            TestTrue(TEXT("Elementを配列から取得"),
                ElementFiles.SourceContent.Contains(
                    TEXT("const int32 ForEachArrayElement = Numbers[ForEachArrayIndex];")));
            TestTrue(TEXT("Elementを文字列へ変換"),
                ElementFiles.SourceContent.Contains(
                    TEXT("Conv_IntToString(ForEachArrayElement)")));
        }
        else
        {
            for (const FString& Diagnostic : ElementDiagnostics)
            {
                AddError(Diagnostic);
            }
        }
    }
    else
    {
        for (const FBPSemanticDiagnostic& Diagnostic : ElementIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
    }
    return bElementLowered;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedSequentialBodyTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedSequentialBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedSequentialBodyTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_NestedSequential");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Begin;
    Begin.Id = FGuid::NewGuid();
    Begin.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Begin.FunctionName = TEXT("ReceiveBeginPlay");
    Begin.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.Add(MoveTemp(Begin));
    const auto MakeLoop = []()
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
        Node.MacroGraphName = TEXT("ForLoop");
        Node.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("0")));
        Node.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
        Node.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
        return Node;
    };
    Graph.Nodes.Add(MakeLoop());
    Graph.Nodes.Add(MakeLoop());
    const TCHAR* Names[] = {
        TEXT("Process1"), TEXT("Process2"), TEXT("Process3"),
        TEXT("Process4"), TEXT("Process5"), TEXT("Process6")
    };
    for (const TCHAR* Name : Names)
    {
        FBPIRNode Print;
        Print.Id = FGuid::NewGuid();
        Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Print.FunctionName = TEXT("PrintString");
        Print.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), Name));
        Graph.Nodes.Add(MoveTemp(Print));
    }
    const auto Connect = [&Graph](int32 From, int32 Output, int32 To)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Graph.Nodes[From].Id;
        Edge.SourcePin = Graph.Nodes[From].Pins[Output].Id;
        Edge.TargetNode = Graph.Nodes[To].Id;
        Edge.TargetPin = Graph.Nodes[To].Pins[0].Id;
        Graph.ExecEdges.Add(Edge);
    };
    // 外側: 処理1 -> 処理2 -> 内側ForLoop -> 処理5 -> 処理6。
    // 内側: 処理3 -> 処理4。Completedは外側本体の後続処理に接続する。
    Connect(0, 0, 1);
    Connect(1, 3, 3);
    Connect(3, 1, 4);
    Connect(4, 1, 2);
    Connect(2, 3, 5);
    Connect(5, 1, 6);
    Connect(2, 5, 7);
    Connect(7, 1, 8);
    IR.Graphs.Add(MoveTemp(Graph));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("二重ForLoopの直列処理をLowering"),
        FBPSemanticLowering::LowerForLoop(IR, Semantic)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("外側の本体は5文"), Outer.LoopBodyStatements.Num(), 5);
    TestEqual(TEXT("内側ループは3番目"), Outer.LoopBodyStatements[2].Kind,
        EBPSemanticStatementKind::ForLoop);
    const FBPSemanticStatement& Inner = Outer.LoopBodyStatements[2];
    TestEqual(TEXT("内側の本体は2文"), Inner.LoopBodyStatements.Num(), 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopの直列処理をC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedSequentialActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    // 内側ループ終了後も外側ループ内で処理5・処理6が順に生成される。
    int32 Previous = INDEX_NONE;
    for (const TCHAR* Name : Names)
    {
        const int32 Position = Files.SourceContent.Find(Name);
        TestTrue(FString::Printf(TEXT("%s の出力順"), Name),
            Position != INDEX_NONE && Position > Previous);
        Previous = Position;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedIndexPrintTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedIndexPrint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedIndexPrintTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_NestedSequential");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Begin;
    Begin.Id = FGuid::NewGuid();
    Begin.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Begin.FunctionName = TEXT("ReceiveBeginPlay");
    Begin.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.Add(MoveTemp(Begin));
    const auto MakeLoop = []()
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
        Node.MacroGraphName = TEXT("ForLoop");
        Node.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("0")));
        Node.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
        Node.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
        return Node;
    };
    Graph.Nodes.Add(MakeLoop());
    Graph.Nodes.Add(MakeLoop());
    const TCHAR* Names[] = {
        TEXT("Process1"), TEXT("Process2"), TEXT("Process3"),
        TEXT("Process4"), TEXT("Process5"), TEXT("Process6")
    };
    for (const TCHAR* Name : Names)
    {
        FBPIRNode Print;
        Print.Id = FGuid::NewGuid();
        Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Print.FunctionName = TEXT("PrintString");
        Print.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), Name));
        Graph.Nodes.Add(MoveTemp(Print));
    }
    const auto Connect = [&Graph](int32 From, int32 Output, int32 To)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Graph.Nodes[From].Id;
        Edge.SourcePin = Graph.Nodes[From].Pins[Output].Id;
        Edge.TargetNode = Graph.Nodes[To].Id;
        Edge.TargetPin = Graph.Nodes[To].Pins[0].Id;
        Graph.ExecEdges.Add(Edge);
    };
    // 外側: 処理1 -> 処理2 -> 内側ForLoop -> 処理5 -> 処理6。
    // 内側: 処理3 -> 処理4。Completedは外側本体の後続処理に接続する。
    Connect(0, 0, 1);
    Connect(1, 3, 3);
    Connect(3, 1, 4);
    Connect(4, 1, 2);
    Connect(2, 3, 5);
    Connect(5, 1, 6);
    Connect(2, 5, 7);
    Connect(7, 1, 8);
    // 内側Indexを文字列に変換し、処理3のPrintStringへ接続する。
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    FBPIRLink IndexLink;
    IndexLink.SourceNode = Graph.Nodes[2].Id;
    IndexLink.SourcePin = Graph.Nodes[2].Pins[4].Id;
    IndexLink.TargetNode = Converter.Id;
    IndexLink.TargetPin = Converter.Pins[0].Id;
    Graph.DataEdges.Add(IndexLink);
    FBPIRLink StringLink;
    StringLink.SourceNode = Converter.Id;
    StringLink.SourcePin = Converter.Pins[1].Id;
    StringLink.TargetNode = Graph.Nodes[5].Id;
    StringLink.TargetPin = Graph.Nodes[5].Pins[2].Id;
    Graph.DataEdges.Add(StringLink);
    Graph.Nodes.Add(MoveTemp(Converter));
    IR.Graphs.Add(MoveTemp(Graph));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("二重ForLoopの直列処理をLowering"),
        FBPSemanticLowering::LowerForLoop(IR, Semantic)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("外側の本体は5文"), Outer.LoopBodyStatements.Num(), 5);
    TestEqual(TEXT("内側ループは3番目"), Outer.LoopBodyStatements[2].Kind,
        EBPSemanticStatementKind::ForLoop);
    const FBPSemanticStatement& Inner = Outer.LoopBodyStatements[2];
    TestEqual(TEXT("内側の本体は2文"), Inner.LoopBodyStatements.Num(), 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopの直列処理をC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedSequentialActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("内側Indexの文字列変換を生成"),
        Files.SourceContent.Contains(TEXT("InnerIndex")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedArithmeticPrintTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedArithmeticPrint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedArithmeticPrintTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_NestedSequential");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Begin;
    Begin.Id = FGuid::NewGuid();
    Begin.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Begin.FunctionName = TEXT("ReceiveBeginPlay");
    Begin.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.Add(MoveTemp(Begin));
    const auto MakeLoop = []()
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
        Node.MacroGraphName = TEXT("ForLoop");
        Node.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("0")));
        Node.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
        Node.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
        return Node;
    };
    Graph.Nodes.Add(MakeLoop());
    Graph.Nodes.Add(MakeLoop());
    const TCHAR* Names[] = {
        TEXT("Process1"), TEXT("Process2"), TEXT("Process3"),
        TEXT("Process4"), TEXT("Process5"), TEXT("Process6")
    };
    for (const TCHAR* Name : Names)
    {
        FBPIRNode Print;
        Print.Id = FGuid::NewGuid();
        Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Print.FunctionName = TEXT("PrintString");
        Print.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), Name));
        Graph.Nodes.Add(MoveTemp(Print));
    }
    const auto Connect = [&Graph](int32 From, int32 Output, int32 To)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Graph.Nodes[From].Id;
        Edge.SourcePin = Graph.Nodes[From].Pins[Output].Id;
        Edge.TargetNode = Graph.Nodes[To].Id;
        Edge.TargetPin = Graph.Nodes[To].Pins[0].Id;
        Graph.ExecEdges.Add(Edge);
    };
    // 外側: 処理1 -> 処理2 -> 内側ForLoop -> 処理5 -> 処理6。
    // 内側: 処理3 -> 処理4。Completedは外側本体の後続処理に接続する。
    Connect(0, 0, 1);
    Connect(1, 3, 3);
    Connect(3, 1, 4);
    Connect(4, 1, 2);
    Connect(2, 3, 5);
    Connect(5, 1, 6);
    Connect(2, 5, 7);
    Connect(7, 1, 8);
    // 内側Indexを文字列に変換し、処理3のPrintStringへ接続する。
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    FBPIRLink IndexLink;
    IndexLink.SourceNode = Graph.Nodes[2].Id;
    IndexLink.SourcePin = Graph.Nodes[2].Pins[4].Id;
    IndexLink.TargetNode = Converter.Id;
    IndexLink.TargetPin = Converter.Pins[0].Id;
    Graph.DataEdges.Add(IndexLink);
    FBPIRLink StringLink;
    StringLink.SourceNode = Converter.Id;
    StringLink.SourcePin = Converter.Pins[1].Id;
    StringLink.TargetNode = Graph.Nodes[5].Id;
    StringLink.TargetPin = Graph.Nodes[5].Pins[2].Id;
    Graph.DataEdges.Add(StringLink);
    // InnerIndex + 10をConv_IntToStringへ渡す。
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("10")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    Graph.DataEdges[0].TargetNode = Addition.Id;
    Graph.DataEdges[0].TargetPin = Addition.Pins[0].Id;
    FBPIRLink ArithmeticLink;
    ArithmeticLink.SourceNode = Addition.Id;
    ArithmeticLink.SourcePin = Addition.Pins[2].Id;
    ArithmeticLink.TargetNode = Converter.Id;
    ArithmeticLink.TargetPin = Converter.Pins[0].Id;
    Graph.DataEdges.Add(ArithmeticLink);
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Converter));
    IR.Graphs.Add(MoveTemp(Graph));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("二重ForLoopの直列処理をLowering"),
        FBPSemanticLowering::LowerForLoop(IR, Semantic)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("外側の本体は5文"), Outer.LoopBodyStatements.Num(), 5);
    TestEqual(TEXT("内側ループは3番目"), Outer.LoopBodyStatements[2].Kind,
        EBPSemanticStatementKind::ForLoop);
    const FBPSemanticStatement& Inner = Outer.LoopBodyStatements[2];
    TestEqual(TEXT("内側の本体は2文"), Inner.LoopBodyStatements.Num(), 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopの直列処理をC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedSequentialActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("内側Indexの文字列変換を生成"),
        Files.SourceContent.Contains(TEXT("Add_IntInt(InnerIndex, 10)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopNestedConcatArithmeticPrintTest,
    "BlueprintAIConverter.Conversion.ForLoop.NestedConcatArithmeticPrint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopNestedConcatArithmeticPrintTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_NestedSequential");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Begin;
    Begin.Id = FGuid::NewGuid();
    Begin.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Begin.FunctionName = TEXT("ReceiveBeginPlay");
    Begin.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.Add(MoveTemp(Begin));
    const auto MakeLoop = []()
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
        Node.MacroGraphName = TEXT("ForLoop");
        Node.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("0")));
        Node.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
        Node.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
        return Node;
    };
    Graph.Nodes.Add(MakeLoop());
    Graph.Nodes.Add(MakeLoop());
    const TCHAR* Names[] = {
        TEXT("Process1"), TEXT("Process2"), TEXT("Process3"),
        TEXT("Process4"), TEXT("Process5"), TEXT("Process6")
    };
    for (const TCHAR* Name : Names)
    {
        FBPIRNode Print;
        Print.Id = FGuid::NewGuid();
        Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Print.FunctionName = TEXT("PrintString");
        Print.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), Name));
        Graph.Nodes.Add(MoveTemp(Print));
    }
    const auto Connect = [&Graph](int32 From, int32 Output, int32 To)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Graph.Nodes[From].Id;
        Edge.SourcePin = Graph.Nodes[From].Pins[Output].Id;
        Edge.TargetNode = Graph.Nodes[To].Id;
        Edge.TargetPin = Graph.Nodes[To].Pins[0].Id;
        Graph.ExecEdges.Add(Edge);
    };
    // 外側: 処理1 -> 処理2 -> 内側ForLoop -> 処理5 -> 処理6。
    // 内側: 処理3 -> 処理4。Completedは外側本体の後続処理に接続する。
    Connect(0, 0, 1);
    Connect(1, 3, 3);
    Connect(3, 1, 4);
    Connect(4, 1, 2);
    Connect(2, 3, 5);
    Connect(5, 1, 6);
    Connect(2, 5, 7);
    Connect(7, 1, 8);
    // 内側Indexを文字列に変換し、処理3のPrintStringへ接続する。
    FBPIRNode Converter;
    Converter.Id = FGuid::NewGuid();
    Converter.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Converter.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Converter.FunctionName = TEXT("Conv_IntToString");
    Converter.Pins.Add(MakeLoopPin(TEXT("InInt"), EGPD_Input, TEXT("int")));
    Converter.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    FBPIRLink IndexLink;
    IndexLink.SourceNode = Graph.Nodes[2].Id;
    IndexLink.SourcePin = Graph.Nodes[2].Pins[4].Id;
    IndexLink.TargetNode = Converter.Id;
    IndexLink.TargetPin = Converter.Pins[0].Id;
    Graph.DataEdges.Add(IndexLink);
    FBPIRLink StringLink;
    StringLink.SourceNode = Converter.Id;
    StringLink.SourcePin = Converter.Pins[1].Id;
    StringLink.TargetNode = Graph.Nodes[5].Id;
    StringLink.TargetPin = Graph.Nodes[5].Pins[2].Id;
    Graph.DataEdges.Add(StringLink);
    // InnerIndex + 10をConv_IntToStringへ渡す。
    FBPIRNode Addition;
    Addition.Id = FGuid::NewGuid();
    Addition.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("int")));
    Addition.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("int"), TEXT("10")));
    Addition.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("int")));
    Graph.DataEdges[0].TargetNode = Addition.Id;
    Graph.DataEdges[0].TargetPin = Addition.Pins[0].Id;
    FBPIRLink ArithmeticLink;
    ArithmeticLink.SourceNode = Addition.Id;
    ArithmeticLink.SourcePin = Addition.Pins[2].Id;
    ArithmeticLink.TargetNode = Converter.Id;
    ArithmeticLink.TargetPin = Converter.Pins[0].Id;
    Graph.DataEdges.Add(ArithmeticLink);
    // "Index = " + Conv_IntToString(InnerIndex + 10) をPrintStringに接続する。
    FBPIRNode Concat;
    Concat.Id = FGuid::NewGuid();
    Concat.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Concat.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Concat.FunctionName = TEXT("Concat_StrStr");
    Concat.Pins.Add(MakeLoopPin(TEXT("A"), EGPD_Input, TEXT("string"),
        TEXT("Index = ")));
    Concat.Pins.Add(MakeLoopPin(TEXT("B"), EGPD_Input, TEXT("string")));
    Concat.Pins.Add(MakeLoopPin(TEXT("ReturnValue"), EGPD_Output, TEXT("string")));
    Graph.DataEdges[1].TargetNode = Concat.Id;
    Graph.DataEdges[1].TargetPin = Concat.Pins[1].Id;
    FBPIRLink ConcatLink;
    ConcatLink.SourceNode = Concat.Id;
    ConcatLink.SourcePin = Concat.Pins[2].Id;
    ConcatLink.TargetNode = Graph.Nodes[5].Id;
    ConcatLink.TargetPin = Graph.Nodes[5].Pins[2].Id;
    Graph.DataEdges.Add(ConcatLink);
    Graph.Nodes.Add(MoveTemp(Concat));
    Graph.Nodes.Add(MoveTemp(Addition));
    Graph.Nodes.Add(MoveTemp(Converter));
    IR.Graphs.Add(MoveTemp(Graph));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("二重ForLoopの直列処理をLowering"),
        FBPSemanticLowering::LowerForLoop(IR, Semantic)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("外側の本体は5文"), Outer.LoopBodyStatements.Num(), 5);
    TestEqual(TEXT("内側ループは3番目"), Outer.LoopBodyStatements[2].Kind,
        EBPSemanticStatementKind::ForLoop);
    const FBPSemanticStatement& Inner = Outer.LoopBodyStatements[2];
    TestEqual(TEXT("内側の本体は2文"), Inner.LoopBodyStatements.Num(), 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("二重ForLoopの直列処理をC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedSequentialActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("内側Indexの文字列変換を生成"),
        Files.SourceContent.Contains(TEXT("Concat_StrStr(")) &&
        Files.SourceContent.Contains(TEXT("Add_IntInt(InnerIndex, 10)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPForLoopTripleSequentialBodyTest,
    "BlueprintAIConverter.Conversion.ForLoop.TripleSequentialBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPForLoopTripleSequentialBodyTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_NestedSequential");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Begin;
    Begin.Id = FGuid::NewGuid();
    Begin.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Begin.FunctionName = TEXT("ReceiveBeginPlay");
    Begin.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
    Graph.Nodes.Add(MoveTemp(Begin));
    const auto MakeLoop = []()
    {
        FBPIRNode Node;
        Node.Id = FGuid::NewGuid();
        Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_MacroInstance");
        Node.MacroGraphName = TEXT("ForLoop");
        Node.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("FirstIndex"), EGPD_Input, TEXT("int"), TEXT("0")));
        Node.Pins.Add(MakeLoopPin(TEXT("LastIndex"), EGPD_Input, TEXT("int"), TEXT("2")));
        Node.Pins.Add(MakeLoopPin(TEXT("LoopBody"), EGPD_Output, TEXT("exec")));
        Node.Pins.Add(MakeLoopPin(TEXT("Index"), EGPD_Output, TEXT("int")));
        Node.Pins.Add(MakeLoopPin(TEXT("Completed"), EGPD_Output, TEXT("exec")));
        return Node;
    };
    Graph.Nodes.Add(MakeLoop());
    Graph.Nodes.Add(MakeLoop());
    Graph.Nodes.Add(MakeLoop());
    const TCHAR* Names[] = {
        TEXT("Process1"), TEXT("Process2"), TEXT("Process3"),
        TEXT("Process4"), TEXT("Process5"), TEXT("Process6")
    };
    for (const TCHAR* Name : Names)
    {
        FBPIRNode Print;
        Print.Id = FGuid::NewGuid();
        Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        Print.FunctionName = TEXT("PrintString");
        Print.Pins.Add(MakeLoopPin(TEXT("execute"), EGPD_Input, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("then"), EGPD_Output, TEXT("exec")));
        Print.Pins.Add(MakeLoopPin(TEXT("InString"), EGPD_Input, TEXT("string"), Name));
        Graph.Nodes.Add(MoveTemp(Print));
    }
    const auto Connect = [&Graph](int32 From, int32 Output, int32 To)
    {
        FBPIRLink Edge;
        Edge.SourceNode = Graph.Nodes[From].Id;
        Edge.SourcePin = Graph.Nodes[From].Pins[Output].Id;
        Edge.TargetNode = Graph.Nodes[To].Id;
        Edge.TargetPin = Graph.Nodes[To].Pins[0].Id;
        Graph.ExecEdges.Add(Edge);
    };
    // 三重ループでCompletedが一つ外側の本体へ戻ることを確認する。
    Connect(0, 0, 1);
    Connect(1, 3, 4);
    Connect(4, 1, 2);
    Connect(2, 3, 5);
    Connect(5, 1, 3);
    Connect(3, 3, 6);
    Connect(6, 1, 7);
    Connect(3, 5, 8);
    Connect(8, 1, 9);
    IR.Graphs.Add(MoveTemp(Graph));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("二重ForLoopの直列処理をLowering"),
        FBPSemanticLowering::LowerForLoop(IR, Semantic)) == false)
    {
        return false;
    }
    const FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    TestEqual(TEXT("外側本体"), Outer.LoopBodyStatements.Num(), 2);
    const FBPSemanticStatement& Middle = Outer.LoopBodyStatements[1];
    TestEqual(TEXT("中間ループ"), Middle.Kind, EBPSemanticStatementKind::ForLoop);
    TestEqual(TEXT("中間本体"), Middle.LoopBodyStatements.Num(), 4);
    const FBPSemanticStatement& Inner = Middle.LoopBodyStatements[1];
    TestEqual(TEXT("内側ループ"), Inner.Kind, EBPSemanticStatementKind::ForLoop);
    TestEqual(TEXT("内側本体"), Inner.LoopBodyStatements.Num(), 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("三重ForLoopのC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ATripleSequentialActor"),
            TEXT("TEST_API"), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("三段目のIndex名"), Files.SourceContent.Contains(TEXT("LoopIndex2")));
    return true;
}

#endif
