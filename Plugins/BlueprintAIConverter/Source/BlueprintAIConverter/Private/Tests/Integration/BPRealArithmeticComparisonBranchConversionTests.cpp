#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
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
NodeType* AddArithmeticNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddArithmeticPrint(UEdGraph* Graph,
    UFunction* PrintFunction, const FString& Text, const FString& Key)
{
    UK2Node_CallFunction* Print = AddArithmeticNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Text;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.2,G=0.6,B=1.0,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("3.0");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = Key;
    return Print;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealArithmeticComparisonBranchConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.ArithmeticComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealArithmeticComparisonBranchConversionTest::RunTest(
    const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(),
        UBlueprint::StaticClass(), TEXT("BP_RealArithmeticComparisonBranch"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Actor派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    FEdGraphPinType ScoreType;
    ScoreType.PinCategory = UEdGraphSchema_K2::PC_Int;
    if (TestTrue(TEXT("Scoreメンバー変数を追加"),
        FBlueprintEditorUtils::AddMemberVariable(
            Blueprint, TEXT("Score"), ScoreType, TEXT("42"))) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("EventGraphを取得"), EventGraph) == false)
    {
        return false;
    }
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddArithmeticNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(
        TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UK2Node_VariableGet* ScoreGet =
        AddArithmeticNode<UK2Node_VariableGet>(EventGraph);
    ScoreGet->VariableReference.SetSelfMember(TEXT("Score"));
    ScoreGet->AllocateDefaultPins();

    UFunction* AddFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
    UFunction* SubtractFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Subtract_IntInt));
    UFunction* MultiplyFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_IntInt));
    UFunction* DivideFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Divide_IntInt));
    UFunction* GreaterFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Greater_IntInt));
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("Add_IntInt UFunctionを取得"), AddFunction) == false ||
        TestNotNull(TEXT("Subtract_IntInt UFunctionを取得"), SubtractFunction) == false ||
        TestNotNull(TEXT("Multiply_IntInt UFunctionを取得"), MultiplyFunction) == false ||
        TestNotNull(TEXT("Divide_IntInt UFunctionを取得"), DivideFunction) == false ||
        TestNotNull(TEXT("Greater_IntInt UFunctionを取得"), GreaterFunction) == false ||
        TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }

    UK2Node_CommutativeAssociativeBinaryOperator* Add =
        AddArithmeticNode<UK2Node_CommutativeAssociativeBinaryOperator>(EventGraph);
    Add->SetFromFunction(AddFunction);
    Add->AllocateDefaultPins();
    Add->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("8");

    UK2Node_CallFunction* Subtract =
        AddArithmeticNode<UK2Node_CallFunction>(EventGraph);
    Subtract->SetFromFunction(SubtractFunction);
    Subtract->AllocateDefaultPins();
    Subtract->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("2");

    UK2Node_CommutativeAssociativeBinaryOperator* Multiply =
        AddArithmeticNode<UK2Node_CommutativeAssociativeBinaryOperator>(EventGraph);
    Multiply->SetFromFunction(MultiplyFunction);
    Multiply->AllocateDefaultPins();
    Multiply->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("3");

    UK2Node_CallFunction* Divide =
        AddArithmeticNode<UK2Node_CallFunction>(EventGraph);
    Divide->SetFromFunction(DivideFunction);
    Divide->AllocateDefaultPins();
    Divide->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("4");

    UK2Node_CallFunction* Comparison =
        AddArithmeticNode<UK2Node_CallFunction>(EventGraph);
    Comparison->SetFromFunction(GreaterFunction);
    Comparison->AllocateDefaultPins();
    Comparison->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("35");

    UK2Node_IfThenElse* Branch =
        AddArithmeticNode<UK2Node_IfThenElse>(EventGraph);
    Branch->AllocateDefaultPins();
    UK2Node_CallFunction* TruePrint = AddArithmeticPrint(EventGraph,
        PrintFunction, TEXT("Arithmetic True"), TEXT("arithmetic-true"));
    UK2Node_CallFunction* FalsePrint = AddArithmeticPrint(EventGraph,
        PrintFunction, TEXT("Arithmetic False"), TEXT("arithmetic-false"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(
        EventGraph->GetSchema());
    if (TestNotNull(TEXT("K2 schema"), Schema) == false)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからBranchへ接続"), Schema->TryCreateConnection(
        BeginPlay->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Branch->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Score Getを加算Aへ接続"), Schema->TryCreateConnection(
        ScoreGet->GetValuePin(), Add->FindPinChecked(TEXT("A"))));
    TestTrue(TEXT("加算結果を減算Aへ接続"), Schema->TryCreateConnection(
        Add->FindPinChecked(TEXT("ReturnValue")),
        Subtract->FindPinChecked(TEXT("A"))));
    TestTrue(TEXT("減算結果を乗算Aへ接続"), Schema->TryCreateConnection(
        Subtract->FindPinChecked(TEXT("ReturnValue")),
        Multiply->FindPinChecked(TEXT("A"))));
    TestTrue(TEXT("乗算結果を除算Aへ接続"), Schema->TryCreateConnection(
        Multiply->FindPinChecked(TEXT("ReturnValue")),
        Divide->FindPinChecked(TEXT("A"))));
    TestTrue(TEXT("除算結果を比較Aへ接続"), Schema->TryCreateConnection(
        Divide->FindPinChecked(TEXT("ReturnValue")),
        Comparison->FindPinChecked(TEXT("A"))));
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
    if (TestNotNull(TEXT("Blueprintをコンパイル"),
        Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("算術比較BranchからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    int32 EventGraphIndex = INDEX_NONE;
    int32 ArithmeticNodeIndex = INDEX_NONE;
    for (int32 GraphIndex = 0; GraphIndex < GraphIR.Graphs.Num(); ++GraphIndex)
    {
        FBPIRGraph& Graph = GraphIR.Graphs[GraphIndex];
        if (Graph.Kind != TEXT("event"))
        {
            continue;
        }
        EventGraphIndex = GraphIndex;
        ArithmeticNodeIndex = Graph.Nodes.IndexOfByPredicate(
            [](const FBPIRNode& Node)
            {
                return Node.FunctionName == TEXT("Add_IntInt");
            });
        break;
    }
    if (TestTrue(TEXT("加算ノードをGraph IRに保持"),
        EventGraphIndex != INDEX_NONE &&
        ArithmeticNodeIndex != INDEX_NONE) == false)
    {
        return false;
    }

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("算術比較BranchをLowering"),
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
    if (TestEqual(TEXT("比較引数数"), Condition.Arguments.Num(), 2) == false ||
        Condition.Arguments.Num() != 2)
    {
        return false;
    }
    const FBPSemanticExpression& Arithmetic = Condition.Arguments[0];
    TestTrue(TEXT("左辺を算術式として保持"),
        Arithmetic.Kind == EBPSemanticExpressionKind::FunctionCall);
    TestEqual(TEXT("最上位の除算関数名を保持"), Arithmetic.FunctionName,
        FName(TEXT("Divide_IntInt")));
    TestEqual(TEXT("除算引数数"), Arithmetic.Arguments.Num(), 2);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("算術比較Branch C++を生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR,
            TEXT("ARealArithmeticComparisonBranch"), FString(),
            Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("Scoreメンバー宣言を生成"),
        Files.HeaderContent.Contains(TEXT("int32 Score = 42")));
    TestTrue(TEXT("算術式を比較条件へ一度だけ生成"),
        Files.SourceContent.Contains(
            TEXT("if (UKismetMathLibrary::Divide_IntInt(UKismetMathLibrary::Multiply_IntInt(UKismetMathLibrary::Subtract_IntInt(UKismetMathLibrary::Add_IntInt(Score, 8), 2), 3), 4) > 35)")));

    FBPGraphIR TamperedIR = GraphIR;
    TamperedIR.Graphs[EventGraphIndex].Nodes[ArithmeticNodeIndex]
        .FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    FBPSemanticIR RejectedIR;
    TestFalse(TEXT("算術所有クラス改ざんを拒否"),
        FBPSemanticLowering::Lower(Blueprint, TamperedIR, RejectedIR));

    FBPSemanticIR TamperedSemantic = SemanticIR;
    TamperedSemantic.Functions[0].Blocks[0].Statements[0]
        .Expression.Arguments[0].Type.Kind = EBPSemanticTypeKind::Double;
    FBPGeneratedActorFiles RejectedFiles;
    Diagnostics.Reset();
    TestFalse(TEXT("算術Semantic型改ざんをGeneratorで拒否"),
        FBPSemanticCodeGenerator::Generate(TamperedSemantic,
            TEXT("ARealArithmeticComparisonBranch"), FString(),
            RejectedFiles, Diagnostics));

    const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("BlueprintToCppGenerated"),
        TEXT("RealArithmeticComparisonBranch"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(
        OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(
        OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("算術比較Branchヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("算術比較Branchソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_ARITHMETIC_COMPARISON_HEADER=%s"),
        *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_ARITHMETIC_COMPARISON_SOURCE=%s"),
        *SourcePath));
    return true;
}

#endif
