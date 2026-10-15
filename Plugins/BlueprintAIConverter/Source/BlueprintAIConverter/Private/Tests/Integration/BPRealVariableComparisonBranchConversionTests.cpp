#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
template <typename NodeType>
NodeType* AddVariableComparisonNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddVariableComparisonPrint(UEdGraph* Graph,
    UFunction* PrintFunction, const FString& Text, const FString& Key)
{
    UK2Node_CallFunction* Print = AddVariableComparisonNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Text;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.3,G=0.9,B=0.4,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("3.0");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = Key;
    return Print;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealVariableComparisonBranchConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.VariableComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealVariableComparisonBranchConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(),
        UBlueprint::StaticClass(), TEXT("BP_RealVariableComparisonBranch"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Actor派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    FEdGraphPinType HealthType;
    HealthType.PinCategory = UEdGraphSchema_K2::PC_Int;
    if (TestTrue(TEXT("Healthメンバー変数を追加"),
        FBlueprintEditorUtils::AddMemberVariable(
            Blueprint, TEXT("Health"), HealthType, TEXT("42"))) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("EventGraphを取得"), EventGraph) == false)
    {
        return false;
    }
    // テンプレート由来ノードを除外し、Variable Getのデータ依存を明示的に構築する。
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddVariableComparisonNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UK2Node_VariableGet* HealthGet =
        AddVariableComparisonNode<UK2Node_VariableGet>(EventGraph);
    HealthGet->VariableReference.SetSelfMember(TEXT("Health"));
    HealthGet->AllocateDefaultPins();

    UFunction* ComparisonFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Greater_IntInt));
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("Greater_IntInt UFunctionを取得"), ComparisonFunction) == false ||
        TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* Comparison =
        AddVariableComparisonNode<UK2Node_CallFunction>(EventGraph);
    Comparison->SetFromFunction(ComparisonFunction);
    Comparison->AllocateDefaultPins();
    Comparison->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("10");

    UK2Node_IfThenElse* Branch = AddVariableComparisonNode<UK2Node_IfThenElse>(EventGraph);
    Branch->AllocateDefaultPins();
    UK2Node_CallFunction* TruePrint = AddVariableComparisonPrint(
        EventGraph, PrintFunction, TEXT("Health Above Threshold"), TEXT("health-true"));
    UK2Node_CallFunction* FalsePrint = AddVariableComparisonPrint(
        EventGraph, PrintFunction, TEXT("Health Below Threshold"), TEXT("health-false"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    if (TestNotNull(TEXT("K2 schema"), Schema) == false)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからBranchへ接続"), Schema->TryCreateConnection(
        BeginPlay->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Branch->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Health Getを比較Aへ接続"), Schema->TryCreateConnection(
        HealthGet->GetValuePin(), Comparison->FindPinChecked(TEXT("A"))));
    TestTrue(TEXT("比較結果をConditionへ接続"), Schema->TryCreateConnection(
        Comparison->FindPinChecked(TEXT("ReturnValue")),
        Branch->FindPinChecked(TEXT("Condition"))));
    TestTrue(TEXT("True側をPrintStringへ接続"), Schema->TryCreateConnection(
        Branch->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        TruePrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("False側をPrintStringへ接続"), Schema->TryCreateConnection(
        Branch->FindPinChecked(TEXT("else")),
        FalsePrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("Blueprintをコンパイル"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("変数比較BranchからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    int32 EventGraphIndex = INDEX_NONE;
    int32 VariableNodeIndex = INDEX_NONE;
    for (int32 GraphIndex = 0; GraphIndex < GraphIR.Graphs.Num(); ++GraphIndex)
    {
        FBPIRGraph& Graph = GraphIR.Graphs[GraphIndex];
        if (Graph.Kind != TEXT("event"))
        {
            continue;
        }
        EventGraphIndex = GraphIndex;
        VariableNodeIndex = Graph.Nodes.IndexOfByPredicate(
            [](const FBPIRNode& Node)
            {
                return Node.VariableName == TEXT("Health");
            });
        break;
    }
    if (TestTrue(TEXT("Health GetをGraph IRに保持"),
        EventGraphIndex != INDEX_NONE && VariableNodeIndex != INDEX_NONE) == false)
    {
        return false;
    }
    TestTrue(TEXT("selfメンバー参照を保持"),
        GraphIR.Graphs[EventGraphIndex].Nodes[VariableNodeIndex].bVariableSelfContext);

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("変数比較BranchをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    const FBPSemanticExpression& Condition =
        SemanticIR.Functions[0].Blocks[0].Statements[0].Expression;
    TestEqual(TEXT("比較引数数"), Condition.Arguments.Num(), 2);
    if (Condition.Arguments.Num() != 2)
    {
        return false;
    }
    TestTrue(TEXT("左辺を変数参照として保持"),
        Condition.Arguments[0].Kind == EBPSemanticExpressionKind::VariableReference);
    TestEqual(TEXT("変数名を保持"), Condition.Arguments[0].VariableName,
        FName(TEXT("Health")));
    TestTrue(TEXT("右辺をリテラルとして保持"),
        Condition.Arguments[1].Kind == EBPSemanticExpressionKind::Literal);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("変数比較Branch C++を生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR,
            TEXT("ARealVariableComparisonBranch"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("Healthメンバー宣言を生成"),
        Files.HeaderContent.Contains(TEXT("int32 Health = 42")));
    TestTrue(TEXT("変数参照をif条件へ生成"),
        Files.SourceContent.Contains(TEXT("if (Health > 10)")));

    FBPGraphIR NonSelfIR = GraphIR;
    NonSelfIR.Graphs[EventGraphIndex].Nodes[VariableNodeIndex]
        .bVariableSelfContext = false;
    FBPSemanticIR RejectedIR;
    TestFalse(TEXT("self以外の変数参照をLoweringで拒否"),
        FBPSemanticLowering::Lower(Blueprint, NonSelfIR, RejectedIR));

    FBPGraphIR MultipleConsumerIR = GraphIR;
    const FGuid VariableNodeId = MultipleConsumerIR.Graphs[EventGraphIndex]
        .Nodes[VariableNodeIndex].Id;
    const FBPIRLink* VariableEdge = MultipleConsumerIR.Graphs[EventGraphIndex]
        .DataEdges.FindByPredicate(
            [VariableNodeId](const FBPIRLink& Edge)
            {
                return Edge.SourceNode == VariableNodeId;
            });
    if (TestNotNull(TEXT("Variable GetのData edgeを特定"), VariableEdge) == false)
    {
        return false;
    }
    const FBPIRLink DuplicateVariableEdge = *VariableEdge;
    MultipleConsumerIR.Graphs[EventGraphIndex].DataEdges.Add(DuplicateVariableEdge);
    RejectedIR = FBPSemanticIR();
    TestFalse(TEXT("Variable Getの複数Consumerを拒否"),
        FBPSemanticLowering::Lower(Blueprint, MultipleConsumerIR, RejectedIR));

    FBPSemanticIR TamperedSemantic = SemanticIR;
    TamperedSemantic.Functions[0].Blocks[0].Statements[0]
        .Expression.Arguments[0].VariableName = TEXT("MissingHealth");
    FBPGeneratedActorFiles RejectedFiles;
    Diagnostics.Reset();
    TestFalse(TEXT("存在しないSemantic変数参照をGeneratorで拒否"),
        FBPSemanticCodeGenerator::Generate(TamperedSemantic,
            TEXT("ARealVariableComparisonBranch"), FString(),
            RejectedFiles, Diagnostics));

    const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("BlueprintToCppGenerated"), TEXT("RealVariableComparisonBranch"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("変数比較Branchヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("変数比較Branchソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_VARIABLE_COMPARISON_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_VARIABLE_COMPARISON_SOURCE=%s"), *SourcePath));
    return true;
}

#endif
