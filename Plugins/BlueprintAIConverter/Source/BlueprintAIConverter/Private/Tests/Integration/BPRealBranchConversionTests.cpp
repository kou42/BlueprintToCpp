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
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
template <typename NodeType>
NodeType* AddBranchNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddPrintStringNode(UEdGraph* Graph, UFunction* PrintFunction,
    const FString& Text, const FString& Key)
{
    UK2Node_CallFunction* Print = AddBranchNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Text;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.2,G=0.4,B=0.6,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("2.5");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = Key;
    return Print;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealBranchConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.Branch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealBranchConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_RealBranch"));
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

    UK2Node_Event* BeginPlay = AddBranchNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UK2Node_IfThenElse* Branch = AddBranchNode<UK2Node_IfThenElse>(EventGraph);
    Branch->AllocateDefaultPins();
    Branch->FindPinChecked(TEXT("Condition"))->DefaultValue = TEXT("true");

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* TruePrint = AddPrintStringNode(
        EventGraph, PrintFunction, TEXT("Branch True"), TEXT("branch-true"));
    UK2Node_CallFunction* FalsePrint = AddPrintStringNode(
        EventGraph, PrintFunction, TEXT("Branch False"), TEXT("branch-false"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    UEdGraphPin* BeginThen = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* BranchExecute = Branch->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* BranchThen = Branch->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* BranchElse = Branch->FindPin(TEXT("else"));
    UEdGraphPin* TrueExecute = TruePrint->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* FalseExecute = FalsePrint->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (TestNotNull(TEXT("K2 schema"), Schema) == false ||
        TestNotNull(TEXT("BeginPlay Then"), BeginThen) == false ||
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
    if (TestTrue(TEXT("実BlueprintからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("通常経路でBranchをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }

    int32 EventGraphIndex = INDEX_NONE;
    int32 BranchNodeIndex = INDEX_NONE;
    int32 ConditionPinIndex = INDEX_NONE;
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
            if (Node.ClassPath != TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
            {
                continue;
            }
            BranchNodeIndex = NodeIndex;
            for (int32 PinIndex = 0; PinIndex < Node.Pins.Num(); ++PinIndex)
            {
                if (Node.Pins[PinIndex].Name == TEXT("Condition") &&
                    Node.Pins[PinIndex].Direction == EGPD_Input)
                {
                    ConditionPinIndex = PinIndex;
                    break;
                }
            }
            break;
        }
        break;
    }
    if (TestTrue(TEXT("拒否系検証用のBranch Conditionを特定"),
        EventGraphIndex != INDEX_NONE && BranchNodeIndex != INDEX_NONE &&
        ConditionPinIndex != INDEX_NONE) == false)
    {
        return false;
    }

    FBPGraphIR InvalidConditionIR = GraphIR;
    InvalidConditionIR.Graphs[EventGraphIndex].Nodes[BranchNodeIndex]
        .Pins[ConditionPinIndex].Category = UEdGraphSchema_K2::PC_String;
    FBPSemanticIR RejectedSemanticIR;
    TestFalse(TEXT("不正なCondition型をLoweringで拒否"),
        FBPSemanticLowering::Lower(Blueprint, InvalidConditionIR, RejectedSemanticIR));
    TestTrue(TEXT("不正Condition型の診断を保持"), RejectedSemanticIR.HasErrors());

    FBPGraphIR MissingBranchIR = GraphIR;
    MissingBranchIR.Graphs[EventGraphIndex].ExecEdges.Pop();
    RejectedSemanticIR = FBPSemanticIR();
    TestFalse(TEXT("枝が欠落したExec接続をLoweringで拒否"),
        FBPSemanticLowering::Lower(Blueprint, MissingBranchIR, RejectedSemanticIR));
    TestTrue(TEXT("枝欠落の診断を保持"), RejectedSemanticIR.HasErrors());

    FBPGraphIR ExtraDataIR = GraphIR;
    ExtraDataIR.Graphs[EventGraphIndex].DataEdges.Add(FBPIRLink());
    RejectedSemanticIR = FBPSemanticIR();
    TestFalse(TEXT("余分なData接続をLoweringで拒否"),
        FBPSemanticLowering::Lower(Blueprint, ExtraDataIR, RejectedSemanticIR));
    TestTrue(TEXT("余分なData接続の診断を保持"), RejectedSemanticIR.HasErrors());

    if (SemanticIR.Functions.Num() != 1 || SemanticIR.Functions[0].Blocks.Num() != 1 ||
        SemanticIR.Functions[0].Blocks[0].Statements.Num() != 1)
    {
        AddError(TEXT("Branch Semantic IRの関数構造が不正です"));
        return false;
    }
    const FBPSemanticStatement& BranchStatement =
        SemanticIR.Functions[0].Blocks[0].Statements[0];
    TestTrue(TEXT("Branch文として保持"),
        BranchStatement.Kind == EBPSemanticStatementKind::Branch);
    TestEqual(TEXT("True側の文数"), BranchStatement.ThenStatements.Num(), 1);
    TestEqual(TEXT("False側の文数"), BranchStatement.ElseStatements.Num(), 1);
    if (BranchStatement.ThenStatements.Num() != 1 || BranchStatement.ElseStatements.Num() != 1)
    {
        return false;
    }
    TestEqual(TEXT("True側のPrintString"),
        BranchStatement.ThenStatements[0].Expression.FunctionName, FName(TEXT("PrintString")));
    TestEqual(TEXT("False側のPrintString"),
        BranchStatement.ElseStatements[0].Expression.FunctionName, FName(TEXT("PrintString")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Branch C++を生成"), FBPSemanticCodeGenerator::Generate(
        SemanticIR, TEXT("ARealBranch"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("bool条件をifへ生成"), Files.SourceContent.Contains(TEXT("if (true)")));
    TestTrue(TEXT("elseを生成"), Files.SourceContent.Contains(TEXT("else")));
    TestTrue(TEXT("True側本文を生成"), Files.SourceContent.Contains(TEXT("Branch True")));
    TestTrue(TEXT("False側本文を生成"), Files.SourceContent.Contains(TEXT("Branch False")));

    FBPSemanticIR Tampered = SemanticIR;
    Tampered.Functions[0].Blocks[0].Statements[0].Expression.Type.Kind =
        EBPSemanticTypeKind::String;
    FBPGeneratedActorFiles RejectedFiles;
    Diagnostics.Reset();
    TestFalse(TEXT("改ざんされた条件型をGeneratorで拒否"),
        FBPSemanticCodeGenerator::Generate(Tampered,
            TEXT("ARealBranch"), FString(), RejectedFiles, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), RejectedFiles.HeaderContent.IsEmpty());

    const FString OutputDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("BlueprintToCppGenerated"), TEXT("RealBranch"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("Branchヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("Branchソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_BRANCH_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_BRANCH_SOURCE=%s"), *SourcePath));
    return true;
}

#endif
