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
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
template <typename NodeType>
NodeType* AddComparisonBranchNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddComparisonPrintNode(UEdGraph* Graph,
    UFunction* PrintFunction, const FString& Text, const FString& Key)
{
    UK2Node_CallFunction* Print = AddComparisonBranchNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Text;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.8,G=0.6,B=0.2,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("3.0");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = Key;
    return Print;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealComparisonBranchConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.ComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealComparisonBranchConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_RealComparisonBranch"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Actor派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("EventGraphを取得"), EventGraph) == false)
    {
        return false;
    }
    // テンプレート由来ノードの有無にテスト結果を依存させない。
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddComparisonBranchNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UFunction* GreaterFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Greater_IntInt));
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("Greater_IntInt UFunctionを取得"), GreaterFunction) == false ||
        TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* Comparison =
        AddComparisonBranchNode<UK2Node_CallFunction>(EventGraph);
    Comparison->SetFromFunction(GreaterFunction);
    Comparison->AllocateDefaultPins();
    Comparison->FindPinChecked(TEXT("A"))->DefaultValue = TEXT("7");
    Comparison->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("3");

    UK2Node_IfThenElse* Branch = AddComparisonBranchNode<UK2Node_IfThenElse>(EventGraph);
    Branch->AllocateDefaultPins();
    UK2Node_CallFunction* TruePrint = AddComparisonPrintNode(
        EventGraph, PrintFunction, TEXT("Comparison True"), TEXT("comparison-true"));
    UK2Node_CallFunction* FalsePrint = AddComparisonPrintNode(
        EventGraph, PrintFunction, TEXT("Comparison False"), TEXT("comparison-false"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    UEdGraphPin* BeginThen = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ComparisonResult = Comparison->FindPin(TEXT("ReturnValue"));
    UEdGraphPin* Condition = Branch->FindPin(TEXT("Condition"));
    UEdGraphPin* BranchExecute = Branch->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* BranchThen = Branch->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* BranchElse = Branch->FindPin(TEXT("else"));
    UEdGraphPin* TrueExecute = TruePrint->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* FalseExecute = FalsePrint->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (TestNotNull(TEXT("K2 schema"), Schema) == false ||
        TestNotNull(TEXT("BeginPlay Then"), BeginThen) == false ||
        TestNotNull(TEXT("比較結果"), ComparisonResult) == false ||
        TestNotNull(TEXT("Branch Condition"), Condition) == false ||
        TestNotNull(TEXT("Branch Execute"), BranchExecute) == false ||
        TestNotNull(TEXT("Branch Then"), BranchThen) == false ||
        TestNotNull(TEXT("Branch Else"), BranchElse) == false ||
        TestNotNull(TEXT("True側PrintString Execute"), TrueExecute) == false ||
        TestNotNull(TEXT("False側PrintString Execute"), FalseExecute) == false)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからBranchへ接続"),
        Schema->TryCreateConnection(BeginThen, BranchExecute));
    TestTrue(TEXT("整数比較結果をConditionへ接続"),
        Schema->TryCreateConnection(ComparisonResult, Condition));
    TestTrue(TEXT("True側をPrintStringへ接続"),
        Schema->TryCreateConnection(BranchThen, TrueExecute));
    TestTrue(TEXT("False側をPrintStringへ接続"),
        Schema->TryCreateConnection(BranchElse, FalseExecute));

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("Blueprintをコンパイル"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("比較Branch BlueprintからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("通常経路で比較BranchをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (SemanticIR.Functions.Num() != 1 || SemanticIR.Functions[0].Blocks.Num() != 3 ||
        SemanticIR.Functions[0].Blocks[0].Statements.Num() != 1)
    {
        AddError(TEXT("比較Branch Semantic IRの関数構造が不正です"));
        return false;
    }
    const FBPSemanticExpression& ConditionExpression =
        SemanticIR.Functions[0].Blocks[0].Statements[0].Expression;
    TestTrue(TEXT("比較条件を関数呼び出し式として保持"),
        ConditionExpression.Kind == EBPSemanticExpressionKind::FunctionCall);
    TestEqual(TEXT("比較関数名"), ConditionExpression.FunctionName,
        FName(TEXT("Greater_IntInt")));
    TestEqual(TEXT("比較結果型"), static_cast<uint8>(ConditionExpression.Type.Kind),
        static_cast<uint8>(EBPSemanticTypeKind::Bool));
    TestEqual(TEXT("比較引数数"), ConditionExpression.Arguments.Num(), 2);
    if (ConditionExpression.Arguments.Num() != 2)
    {
        return false;
    }
    TestEqual(TEXT("左辺を保持"), ConditionExpression.Arguments[0].LiteralValue,
        FString(TEXT("7")));
    TestEqual(TEXT("右辺を保持"), ConditionExpression.Arguments[1].LiteralValue,
        FString(TEXT("3")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("比較Branch C++を生成"), FBPSemanticCodeGenerator::Generate(
        SemanticIR, TEXT("ARealComparisonBranch"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("比較式をif条件へ生成"),
        Files.SourceContent.Contains(TEXT("if (7 > 3)")));
    TestTrue(TEXT("True側本文を生成"),
        Files.SourceContent.Contains(TEXT("Comparison True")));
    TestTrue(TEXT("False側本文を生成"),
        Files.SourceContent.Contains(TEXT("Comparison False")));

    int32 EventGraphIndex = INDEX_NONE;
    int32 ComparisonNodeIndex = INDEX_NONE;
    int32 AInputIndex = INDEX_NONE;
    for (int32 GraphIndex = 0; GraphIndex < GraphIR.Graphs.Num(); ++GraphIndex)
    {
        if (GraphIR.Graphs[GraphIndex].Kind != TEXT("event"))
        {
            continue;
        }
        EventGraphIndex = GraphIndex;
        for (int32 NodeIndex = 0; NodeIndex < GraphIR.Graphs[GraphIndex].Nodes.Num(); ++NodeIndex)
        {
            const FBPIRNode& Node = GraphIR.Graphs[GraphIndex].Nodes[NodeIndex];
            if (Node.FunctionName != TEXT("Greater_IntInt"))
            {
                continue;
            }
            ComparisonNodeIndex = NodeIndex;
            for (int32 PinIndex = 0; PinIndex < Node.Pins.Num(); ++PinIndex)
            {
                if (Node.Pins[PinIndex].Name == TEXT("A") &&
                    Node.Pins[PinIndex].Direction == EGPD_Input)
                {
                    AInputIndex = PinIndex;
                    break;
                }
            }
            break;
        }
        break;
    }
    if (TestTrue(TEXT("拒否系検証用の比較入力を特定"),
        EventGraphIndex != INDEX_NONE && ComparisonNodeIndex != INDEX_NONE &&
        AInputIndex != INDEX_NONE) == false)
    {
        return false;
    }

    FBPGraphIR InvalidTypeIR = GraphIR;
    InvalidTypeIR.Graphs[EventGraphIndex].Nodes[ComparisonNodeIndex]
        .Pins[AInputIndex].Category = UEdGraphSchema_K2::PC_String;
    FBPSemanticIR RejectedSemanticIR;
    TestFalse(TEXT("数値型不一致をLoweringで拒否"),
        FBPSemanticLowering::Lower(Blueprint, InvalidTypeIR, RejectedSemanticIR));
    TestTrue(TEXT("数値型不一致の診断を保持"), RejectedSemanticIR.HasErrors());

    FBPGraphIR MultipleConsumerIR = GraphIR;
    const FBPIRLink DuplicateConsumer =
        MultipleConsumerIR.Graphs[EventGraphIndex].DataEdges[0];
    MultipleConsumerIR.Graphs[EventGraphIndex].DataEdges.Add(DuplicateConsumer);
    RejectedSemanticIR = FBPSemanticIR();
    TestFalse(TEXT("比較結果の複数ConsumerをLoweringで拒否"),
        FBPSemanticLowering::Lower(Blueprint, MultipleConsumerIR, RejectedSemanticIR));
    TestTrue(TEXT("複数Consumerの診断を保持"), RejectedSemanticIR.HasErrors());

    FBPSemanticIR Tampered = SemanticIR;
    Tampered.Functions[0].Blocks[0].Statements[0]
        .Expression.Arguments[0].Type.Kind = EBPSemanticTypeKind::Float;
    FBPGeneratedActorFiles RejectedFiles;
    Diagnostics.Reset();
    TestFalse(TEXT("改ざんされた比較引数型をGeneratorで拒否"),
        FBPSemanticCodeGenerator::Generate(Tampered,
            TEXT("ARealComparisonBranch"), FString(), RejectedFiles, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), RejectedFiles.HeaderContent.IsEmpty());

    const FString OutputDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("BlueprintToCppGenerated"),
        TEXT("RealComparisonBranch"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("比較Branchヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("比較Branchソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_COMPARISON_BRANCH_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_COMPARISON_BRANCH_SOURCE=%s"), *SourcePath));
    return true;
}

#endif
