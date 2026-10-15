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
NodeType* AddNumericComparisonNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddNumericComparisonPrint(UEdGraph* Graph,
    UFunction* PrintFunction, const FString& Text, const FString& Key)
{
    UK2Node_CallFunction* Print = AddNumericComparisonNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Text;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.2,G=0.7,B=0.9,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("3.0");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = Key;
    return Print;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealNumericComparisonBranchConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.NumericComparisonBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealNumericComparisonBranchConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(),
        UBlueprint::StaticClass(), TEXT("BP_RealNumericComparisonBranch"));
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
    // テンプレート由来ノードを除外し、比較データフローだけを明示的に構築する。
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddNumericComparisonNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UFunction* ComparisonFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, LessEqual_DoubleDouble));
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("LessEqual_DoubleDouble UFunctionを取得"), ComparisonFunction) == false ||
        TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }

    UK2Node_CallFunction* Comparison =
        AddNumericComparisonNode<UK2Node_CallFunction>(EventGraph);
    Comparison->SetFromFunction(ComparisonFunction);
    Comparison->AllocateDefaultPins();
    Comparison->FindPinChecked(TEXT("A"))->DefaultValue = TEXT("7.5");
    Comparison->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("2.25");

    UK2Node_IfThenElse* Branch = AddNumericComparisonNode<UK2Node_IfThenElse>(EventGraph);
    Branch->AllocateDefaultPins();
    UK2Node_CallFunction* TruePrint = AddNumericComparisonPrint(
        EventGraph, PrintFunction, TEXT("Double Comparison True"), TEXT("double-true"));
    UK2Node_CallFunction* FalsePrint = AddNumericComparisonPrint(
        EventGraph, PrintFunction, TEXT("Double Comparison False"), TEXT("double-false"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    if (TestNotNull(TEXT("K2 schema"), Schema) == false)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからBranchへ接続"), Schema->TryCreateConnection(
        BeginPlay->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Branch->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("double比較結果をConditionへ接続"), Schema->TryCreateConnection(
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
    if (TestTrue(TEXT("double比較BranchからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }

    const FBPIRNode* ComparisonIR = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        ComparisonIR = Graph.Nodes.FindByPredicate(
            [](const FBPIRNode& Node)
            {
                return Node.FunctionName == TEXT("LessEqual_DoubleDouble");
            });
        if (ComparisonIR != nullptr)
        {
            break;
        }
    }
    if (TestNotNull(TEXT("比較ノードをGraph IRに保持"), ComparisonIR) == false)
    {
        return false;
    }
    TestEqual(TEXT("関数所有クラスをGraph IRに保持"),
        ComparisonIR->FunctionOwnerPath,
        UKismetMathLibrary::StaticClass()->GetPathName());

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("反射検証済みdouble比較をLowering"),
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
    TestEqual(TEXT("比較関数名を保持"), Condition.FunctionName,
        FName(TEXT("LessEqual_DoubleDouble")));
    TestEqual(TEXT("doubleオペランド型を保持"),
        static_cast<uint8>(Condition.Arguments[0].Type.Kind),
        static_cast<uint8>(EBPSemanticTypeKind::Double));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("double比較Branch C++を生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR,
            TEXT("ARealNumericComparisonBranch"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("double比較式をif条件へ生成"),
        Files.SourceContent.Contains(TEXT("if (7.500000 <= 2.250000)")));

    FBPGraphIR TamperedOwnerIR = GraphIR;
    for (FBPIRGraph& Graph : TamperedOwnerIR.Graphs)
    {
        if (FBPIRNode* Node = Graph.Nodes.FindByPredicate(
            [](const FBPIRNode& Candidate)
            {
                return Candidate.FunctionName == TEXT("LessEqual_DoubleDouble");
            }))
        {
            Node->FunctionOwnerPath = TEXT("/Script/Engine.Actor");
        }
    }
    FBPSemanticIR RejectedIR;
    TestFalse(TEXT("改ざんされた関数所有クラスを拒否"),
        FBPSemanticLowering::Lower(Blueprint, TamperedOwnerIR, RejectedIR));

    FBPSemanticIR TamperedSemantic = SemanticIR;
    TamperedSemantic.Functions[0].Blocks[0].Statements[0]
        .Expression.Arguments[1].Type.Kind = EBPSemanticTypeKind::Float;
    FBPGeneratedActorFiles RejectedFiles;
    Diagnostics.Reset();
    TestFalse(TEXT("暗黙のfloat/double変換をGeneratorで拒否"),
        FBPSemanticCodeGenerator::Generate(TamperedSemantic,
            TEXT("ARealNumericComparisonBranch"), FString(),
            RejectedFiles, Diagnostics));

    const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("BlueprintToCppGenerated"), TEXT("RealNumericComparisonBranch"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("数値比較Branchヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("数値比較Branchソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_NUMERIC_COMPARISON_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_NUMERIC_COMPARISON_SOURCE=%s"), *SourcePath));
    return true;
}

#endif
