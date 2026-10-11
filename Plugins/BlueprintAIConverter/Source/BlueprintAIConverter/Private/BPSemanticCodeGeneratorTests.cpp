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
#endif
