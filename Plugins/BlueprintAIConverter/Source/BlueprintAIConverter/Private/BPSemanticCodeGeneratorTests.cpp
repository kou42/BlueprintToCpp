#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorCodeGenerator.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"

namespace
{
FBPGraphIR MakeGeneratorTestGraph()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
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
    Text.Category = TEXT("string");
    Text.Direction = EGPD_Input;
    Text.DefaultValue = TEXT("Hello \"UE\"");
    Print.Pins.Add(Text);

    FBPIRPin Color;
    Color.Id = FGuid::NewGuid();
    Color.Name = TEXT("TextColor");
    Color.Direction = EGPD_Input;
    Color.DefaultValue = TEXT("(R=1,G=0.25,B=0,A=1)");
    Print.Pins.Add(Color);

    FBPIRPin Duration;
    Duration.Id = FGuid::NewGuid();
    Duration.Name = TEXT("Duration");
    Duration.Direction = EGPD_Input;
    Duration.DefaultValue = TEXT("3.5");
    Print.Pins.Add(Duration);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorParityTest,
    "BlueprintAIConverter.SemanticIR.Generator.LegacyParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorParityTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPGeneratedActorFiles LegacyFiles;
    TArray<FString> LegacyDiagnostics;
    if (TestTrue(TEXT("旧Generator成功"),
        FBPActorCodeGenerator::Generate(Graph, TEXT("AParityActor"), TEXT("TEST_API"), LegacyFiles, LegacyDiagnostics)) == false)
    {
        return false;
    }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles NewFiles;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("新Generator成功"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AParityActor"), TEXT("TEST_API"), NewFiles, Diagnostics)) == false)
    {
        return false;
    }
    TestEqual(TEXT("ヘッダー名一致"), NewFiles.HeaderFileName, LegacyFiles.HeaderFileName);
    TestEqual(TEXT("ソース名一致"), NewFiles.SourceFileName, LegacyFiles.SourceFileName);
    TestEqual(TEXT("ヘッダー一致"), NewFiles.HeaderContent, LegacyFiles.HeaderContent);
    TestEqual(TEXT("ソース一致"), NewFiles.SourceContent, LegacyFiles.SourceContent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorRejectTest,
    "BlueprintAIConverter.SemanticIR.Generator.RejectUnsupported",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorRejectTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    Semantic.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Statement.Expression.FunctionName = TEXT("UnsupportedFunction");
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    Semantic.Functions.Add(Function);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("未対応関数を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AParityActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("診断あり"), Diagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時にソースを公開しない"), Files.SourceContent.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorOptionsParityTest,
    "BlueprintAIConverter.SemanticIR.Generator.OptionsParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorOptionsParityTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPIRNode& Print = Graph.Graphs[0].Nodes[1];

    // エスケープ文字と既定値以外のオプションを旧経路と比較する。
    Print.Pins[0].DefaultValue = TEXT("Line 1\\nLine 2\\t\\\\ \\"quote\\"");
    FBPIRPin Screen;
    Screen.Name = TEXT("bPrintToScreen");
    Screen.Direction = EGPD_Input;
    Screen.DefaultValue = TEXT("false");
    Print.Pins.Add(Screen);
    FBPIRPin Log;
    Log.Name = TEXT("bPrintToLog");
    Log.Direction = EGPD_Input;
    Log.DefaultValue = TEXT("true");
    Print.Pins.Add(Log);
    FBPIRPin Key;
    Key.Name = TEXT("Key");
    Key.Direction = EGPD_Input;
    Key.DefaultValue = TEXT("debug-key");
    Print.Pins.Add(Key);

    FBPGeneratedActorFiles LegacyFiles;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("旧経路成功"), FBPActorCodeGenerator::Generate(
        Graph, TEXT("AOptionsActor"), FString(), LegacyFiles, Diagnostics)) == false)
    {
        return false;
    }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles NewFiles;
    if (TestTrue(TEXT("新経路成功"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AOptionsActor"), FString(), NewFiles, Diagnostics)) == false)
    {
        return false;
    }
    TestEqual(TEXT("オプション付きヘッダー一致"), NewFiles.HeaderContent, LegacyFiles.HeaderContent);
    TestEqual(TEXT("オプション付きソース一致"), NewFiles.SourceContent, LegacyFiles.SourceContent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorInvalidLiteralTest,
    "BlueprintAIConverter.SemanticIR.Generator.RejectInvalidLiteral",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorInvalidLiteralTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }

    // 手動構築されたSemantic IRでも、不正な値をC++へ流さない。
    FBPSemanticExpression& Duration = Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[5];
    Duration.LiteralValue = TEXT("not-a-number");
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("不正Durationを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AInvalidActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("診断あり"), Diagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時はファイルを空にする"), Files.SourceContent.IsEmpty());

    // 未対応の式を黙ってリテラルへ変換しない。
    Duration.LiteralValue = TEXT("2.0");
    Duration.Kind = EBPSemanticExpressionKind::VariableReference;
    TestFalse(TEXT("未対応式を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AInvalidActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("失敗時はヘッダーも空"), Files.HeaderContent.IsEmpty());
    return true;
}

#endif
