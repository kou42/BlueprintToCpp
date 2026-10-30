#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "BPSemanticJsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
template <typename NodeType>
NodeType* AddSequenceTestNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddSequencePrint(UEdGraph* Graph, UFunction* PrintFunction,
    const FString& Text)
{
    UK2Node_CallFunction* Print = AddSequenceTestNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Text;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.1,G=0.3,B=0.8,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("1.5");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = Text;
    return Print;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealSequenceConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.Sequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealSequenceConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_RealSequence"));
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
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddSequenceTestNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UK2Node_ExecutionSequence* Sequence =
        AddSequenceTestNode<UK2Node_ExecutionSequence>(EventGraph);
    Sequence->AllocateDefaultPins();

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* FirstPrint = AddSequencePrint(
        EventGraph, PrintFunction, TEXT("Sequence First"));
    UK2Node_CallFunction* SecondPrint = AddSequencePrint(
        EventGraph, PrintFunction, TEXT("Sequence Second"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    if (TestNotNull(TEXT("K2 schema"), Schema) == false ||
        TestNotNull(TEXT("Then_0"), Sequence->FindPin(TEXT("Then_0"))) == false ||
        TestNotNull(TEXT("Then_1"), Sequence->FindPin(TEXT("Then_1"))) == false)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからSequenceへ接続"), Schema->TryCreateConnection(
        BeginPlay->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Sequence->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Then_0から最初のPrintStringへ接続"), Schema->TryCreateConnection(
        Sequence->FindPinChecked(TEXT("Then_0")),
        FirstPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Then_1から次のPrintStringへ接続"), Schema->TryCreateConnection(
        Sequence->FindPinChecked(TEXT("Then_1")),
        SecondPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));

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
    if (TestTrue(TEXT("通常経路でSequenceをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (SemanticIR.Functions.Num() != 1 || SemanticIR.Functions[0].Blocks.Num() != 1)
    {
        AddError(TEXT("Sequence Semantic IRの関数構造が不正です"));
        return false;
    }
    const TArray<FBPSemanticStatement>& Statements =
        SemanticIR.Functions[0].Blocks[0].Statements;
    if (TestEqual(TEXT("Sequenceの文数"), Statements.Num(), 2) == false)
    {
        return false;
    }
    TestEqual(TEXT("Then_0の文を先に保持"),
        Statements[0].Expression.Arguments[1].LiteralValue, FString(TEXT("Sequence First")));
    TestEqual(TEXT("Then_1の文を後に保持"),
        Statements[1].Expression.Arguments[1].LiteralValue, FString(TEXT("Sequence Second")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Sequence C++を生成"), FBPSemanticCodeGenerator::Generate(
        SemanticIR, TEXT("ARealSequence"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 FirstPosition = Files.SourceContent.Find(TEXT("Sequence First"));
    const int32 SecondPosition = Files.SourceContent.Find(TEXT("Sequence Second"));
    TestTrue(TEXT("生成C++でもSequence順を保持"),
        FirstPosition != INDEX_NONE && SecondPosition > FirstPosition);

    int32 EventGraphIndex = INDEX_NONE;
    for (int32 Index = 0; Index < GraphIR.Graphs.Num(); ++Index)
    {
        if (GraphIR.Graphs[Index].Kind == TEXT("event"))
        {
            EventGraphIndex = Index;
            break;
        }
    }
    if (TestTrue(TEXT("拒否系検証用のEvent Graphを特定"),
        EventGraphIndex != INDEX_NONE) == false)
    {
        return false;
    }

    // Graph IRの配列順は意味上の実行順ではない。エッジを逆順にしても
    // Sequenceのピン番号から同じ順序を復元できることを確認する。
    FBPGraphIR ReorderedIR = GraphIR;
    Algo::Reverse(ReorderedIR.Graphs[EventGraphIndex].ExecEdges);
    FBPSemanticIR ReorderedSemantic;
    TestTrue(TEXT("Execエッジ配列順に依存しない"),
        FBPSemanticLowering::Lower(Blueprint, ReorderedIR, ReorderedSemantic));
    if (ReorderedSemantic.Functions.Num() == 1 &&
        ReorderedSemantic.Functions[0].Blocks.Num() == 1 &&
        ReorderedSemantic.Functions[0].Blocks[0].Statements.Num() == 2)
    {
        TestEqual(TEXT("並べ替え後もThen_0が先"),
            ReorderedSemantic.Functions[0].Blocks[0].Statements[0]
                .Expression.Arguments[1].LiteralValue,
            FString(TEXT("Sequence First")));
    }

    FBPGraphIR MissingOutputIR = GraphIR;
    MissingOutputIR.Graphs[EventGraphIndex].ExecEdges.Pop();
    FBPSemanticIR RejectedSemantic;
    TestFalse(TEXT("未接続Sequence出力を拒否"),
        FBPSemanticLowering::Lower(Blueprint, MissingOutputIR, RejectedSemantic));
    TestTrue(TEXT("未接続出力の診断を保持"), RejectedSemantic.HasErrors());

    FBPGraphIR ExtraDataIR = GraphIR;
    FBPIRNode* ReachablePrint = ExtraDataIR.Graphs[EventGraphIndex].Nodes.FindByPredicate(
        [](const FBPIRNode& Node)
        {
            return Node.FunctionName == TEXT("PrintString");
        });
    const FBPIRPin* ReachableTextPin = ReachablePrint != nullptr
        ? ReachablePrint->Pins.FindByPredicate(
            [](const FBPIRPin& Pin)
            {
                return Pin.Name == TEXT("InString") && Pin.Direction == EGPD_Input;
            })
        : nullptr;
    if (TestNotNull(TEXT("Data接続拒否用PrintString"), ReachablePrint) == false ||
        TestNotNull(TEXT("Data接続拒否用InString"), ReachableTextPin) == false)
    {
        return false;
    }
    FBPIRLink ReachableDataEdge;
    ReachableDataEdge.SourceNode = FGuid::NewGuid();
    ReachableDataEdge.SourcePin = FGuid::NewGuid();
    ReachableDataEdge.TargetNode = ReachablePrint->Id;
    ReachableDataEdge.TargetPin = ReachableTextPin->Id;
    ExtraDataIR.Graphs[EventGraphIndex].DataEdges.Add(ReachableDataEdge);
    RejectedSemantic = FBPSemanticIR();
    TestFalse(TEXT("余分なData接続を拒否"),
        FBPSemanticLowering::Lower(Blueprint, ExtraDataIR, RejectedSemantic));
    TestTrue(TEXT("余分なData接続の診断を保持"), RejectedSemantic.HasErrors());

    const FString OutputDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("BlueprintToCppGenerated"), TEXT("RealSequence"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("Sequenceヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("Sequenceソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_SEQUENCE_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_SEQUENCE_SOURCE=%s"), *SourcePath));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealFunctionSequenceConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealFunctionSequenceConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_FunctionSequence"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("UObject派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    const FName FunctionName(TEXT("RunSequence"));
    UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, FunctionName, UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
        Blueprint, FunctionGraph, true, nullptr);

    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
    {
        Entry = Entry != nullptr ? Entry : Cast<UK2Node_FunctionEntry>(Node);
        Result = Result != nullptr ? Result : Cast<UK2Node_FunctionResult>(Node);
    }
    if (TestNotNull(TEXT("Function Entry"), Entry) == false)
    {
        return false;
    }
    if (Result == nullptr)
    {
        Result = AddSequenceTestNode<UK2Node_FunctionResult>(FunctionGraph);
        Result->FunctionReference.SetSelfMember(FunctionName);
        Result->AllocateDefaultPins();
    }

    UK2Node_ExecutionSequence* Sequence =
        AddSequenceTestNode<UK2Node_ExecutionSequence>(FunctionGraph);
    Sequence->AllocateDefaultPins();
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* FirstPrint = AddSequencePrint(
        FunctionGraph, PrintFunction, TEXT("Function Sequence First"));
    UK2Node_CallFunction* SecondPrint = AddSequencePrint(
        FunctionGraph, PrintFunction, TEXT("Function Sequence Second"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(FunctionGraph->GetSchema());
    UEdGraphPin* EntryThen = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ResultExecute = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (TestNotNull(TEXT("K2 schema"), Schema) == false ||
        TestNotNull(TEXT("Entry Then"), EntryThen) == false ||
        TestNotNull(TEXT("Result Execute"), ResultExecute) == false ||
        TestNotNull(TEXT("Then_0"), Sequence->FindPin(TEXT("Then_0"))) == false ||
        TestNotNull(TEXT("Then_1"), Sequence->FindPin(TEXT("Then_1"))) == false)
    {
        return false;
    }
    EntryThen->BreakAllPinLinks();
    TestTrue(TEXT("Function EntryからSequenceへ接続"), Schema->TryCreateConnection(
        EntryThen, Sequence->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Then_0から最初のPrintStringへ接続"), Schema->TryCreateConnection(
        Sequence->FindPinChecked(TEXT("Then_0")),
        FirstPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Then_1から次のPrintStringへ接続"), Schema->TryCreateConnection(
        Sequence->FindPinChecked(TEXT("Then_1")),
        SecondPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("最後のPrintStringからResultへ接続"), Schema->TryCreateConnection(
        SecondPrint->FindPinChecked(UEdGraphSchema_K2::PN_Then), ResultExecute));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("関数Blueprintのコンパイル成功"), Blueprint->Status != BS_Error) == false ||
        TestNotNull(TEXT("生成クラス"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("関数SequenceのGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    FBPIRGraph* FunctionGraphIR = GraphIR.Graphs.FindByPredicate(
        [FunctionName](const FBPIRGraph& Graph)
        {
            return Graph.Kind == TEXT("function") &&
                Graph.Name == FunctionName.ToString();
        });
    if (TestNotNull(TEXT("到達可能性検証用の関数Graph"), FunctionGraphIR) == false)
    {
        return false;
    }
    FBPIRNode DisconnectedBranch;
    DisconnectedBranch.Id = FGuid::NewGuid();
    DisconnectedBranch.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
    FunctionGraphIR->Nodes.Add(DisconnectedBranch);
    FBPIRNode DisconnectedLoop;
    DisconnectedLoop.Id = FGuid::NewGuid();
    DisconnectedLoop.MacroGraphName = TEXT("ForLoop");
    FunctionGraphIR->Nodes.Add(DisconnectedLoop);
    FBPIRNode DisconnectedDataSource;
    DisconnectedDataSource.Id = FGuid::NewGuid();
    DisconnectedDataSource.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_VariableGet");
    FunctionGraphIR->Nodes.Add(DisconnectedDataSource);
    FBPIRNode DisconnectedDataTarget;
    DisconnectedDataTarget.Id = FGuid::NewGuid();
    DisconnectedDataTarget.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    FunctionGraphIR->Nodes.Add(DisconnectedDataTarget);
    FBPIRLink DisconnectedDataEdge;
    DisconnectedDataEdge.SourceNode = DisconnectedDataSource.Id;
    DisconnectedDataEdge.SourcePin = FGuid::NewGuid();
    DisconnectedDataEdge.TargetNode = DisconnectedDataTarget.Id;
    DisconnectedDataEdge.TargetPin = FGuid::NewGuid();
    FunctionGraphIR->DataEdges.Add(DisconnectedDataEdge);

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("関数単位でSequenceをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (TestEqual(TEXT("Semantic関数数"), SemanticIR.Functions.Num(), 1) == false ||
        TestEqual(TEXT("Semantic関数名"), SemanticIR.Functions[0].Name, FunctionName) == false ||
        TestFalse(TEXT("通常関数として保持"), SemanticIR.Functions[0].bIsEventGraph) == false ||
        TestEqual(TEXT("Sequence文数"),
            SemanticIR.Functions[0].Blocks[0].Statements.Num(), 2) == false)
    {
        return false;
    }

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("UObject通常関数のSequence C++を生成"),
        FBPSemanticCodeGenerator::Generate(
            SemanticIR, TEXT("UFunctionSequenceObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const int32 FirstPosition = Files.SourceContent.Find(TEXT("Function Sequence First"));
    const int32 SecondPosition = Files.SourceContent.Find(TEXT("Function Sequence Second"));
    TestTrue(TEXT("関数Sequenceの生成順を保持"),
        FirstPosition != INDEX_NONE && SecondPosition > FirstPosition);
    TestTrue(TEXT("通常関数定義を生成"),
        Files.SourceContent.Contains(TEXT("UFunctionSequenceObject::RunSequence(")));

    // Function Resultは最後のSequence出力からだけ到達できる形に限定する。
    FBPGraphIR InvalidResultIR = GraphIR;
    FBPIRGraph* SemanticGraph = InvalidResultIR.Graphs.FindByPredicate(
        [FunctionName](const FBPIRGraph& Graph)
        {
            return Graph.Kind == TEXT("function") && Graph.Name == FunctionName.ToString();
        });
    if (TestNotNull(TEXT("拒否系検証用の関数Graph"), SemanticGraph) == false)
    {
        return false;
    }
    const FBPIRNode* FirstPrintIR = SemanticGraph->Nodes.FindByPredicate(
        [](const FBPIRNode& Node)
        {
            return Node.FunctionName == TEXT("PrintString") &&
                Node.Pins.ContainsByPredicate([](const FBPIRPin& Pin)
                {
                    return Pin.Name == TEXT("InString") &&
                        Pin.DefaultValue == TEXT("Function Sequence First");
                });
        });
    const FBPIRNode* ResultIR = SemanticGraph->Nodes.FindByPredicate(
        [](const FBPIRNode& Node)
        {
            return Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_FunctionResult");
        });
    if (TestNotNull(TEXT("先頭PrintString IR"), FirstPrintIR) == false ||
        TestNotNull(TEXT("Function Result IR"), ResultIR) == false)
    {
        return false;
    }
    FBPIRLink* ResultEdge = SemanticGraph->ExecEdges.FindByPredicate(
        [ResultIR](const FBPIRLink& Edge)
        {
            return Edge.TargetNode == ResultIR->Id;
        });
    const FBPIRPin* FirstThenIR = FirstPrintIR->Pins.FindByPredicate(
        [](const FBPIRPin& Pin)
        {
            return Pin.Name == UEdGraphSchema_K2::PN_Then && Pin.Direction == EGPD_Output;
        });
    if (TestNotNull(TEXT("Result接続"), ResultEdge) == false ||
        TestNotNull(TEXT("先頭PrintString Then"), FirstThenIR) == false)
    {
        return false;
    }
    ResultEdge->SourceNode = FirstPrintIR->Id;
    ResultEdge->SourcePin = FirstThenIR->Id;
    FBPSemanticIR RejectedSemantic;
    TestFalse(TEXT("途中のSequence出力からResultへ進む形を拒否"),
        FBPSemanticLowering::Lower(Blueprint, InvalidResultIR, RejectedSemantic));
    TestTrue(TEXT("不正なResult経路の診断を保持"), RejectedSemantic.HasErrors());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealCustomEventSequenceConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.CustomEventSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealCustomEventSequenceConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CustomEventSequence"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("UObject派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("Event Graph"), EventGraph) == false)
    {
        return false;
    }
    EventGraph->Nodes.Reset();

    const FName EventName(TEXT("RunEventSequence"));
    UK2Node_CustomEvent* Event = AddSequenceTestNode<UK2Node_CustomEvent>(EventGraph);
    Event->CustomFunctionName = EventName;
    Event->AllocateDefaultPins();
    UK2Node_ExecutionSequence* Sequence =
        AddSequenceTestNode<UK2Node_ExecutionSequence>(EventGraph);
    Sequence->AllocateDefaultPins();
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* FirstPrint = AddSequencePrint(
        EventGraph, PrintFunction, TEXT("Custom Event Sequence First"));
    UK2Node_CallFunction* SecondPrint = AddSequencePrint(
        EventGraph, PrintFunction, TEXT("Custom Event Sequence Second"));
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    if (TestNotNull(TEXT("K2 schema"), Schema) == false)
    {
        return false;
    }
    TestTrue(TEXT("Custom EventからSequenceへ接続"), Schema->TryCreateConnection(
        Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Sequence->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Then_0から最初のPrintStringへ接続"), Schema->TryCreateConnection(
        Sequence->FindPinChecked(TEXT("Then_0")),
        FirstPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("Then_1から次のPrintStringへ接続"), Schema->TryCreateConnection(
        Sequence->FindPinChecked(TEXT("Then_1")),
        SecondPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("Custom Event Blueprintのコンパイル成功"),
        Blueprint->Status != BS_Error) == false ||
        TestNotNull(TEXT("生成クラス"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("Custom Event SequenceのGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("Custom Event単位でSequenceをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (TestEqual(TEXT("Semantic関数数"), SemanticIR.Functions.Num(), 1) == false ||
        TestEqual(TEXT("Custom Event名"), SemanticIR.Functions[0].Name, EventName) == false ||
        TestTrue(TEXT("Event Graphとして保持"), SemanticIR.Functions[0].bIsEventGraph) == false ||
        TestEqual(TEXT("Sequence文数"),
            SemanticIR.Functions[0].Blocks[0].Statements.Num(), 2) == false)
    {
        return false;
    }
    const TArray<FBPSemanticStatement>& Statements =
        SemanticIR.Functions[0].Blocks[0].Statements;
    TestEqual(TEXT("Then_0を先に保持"), Statements[0].Expression.Arguments[1].LiteralValue,
        FString(TEXT("Custom Event Sequence First")));
    TestEqual(TEXT("Then_1を後に保持"), Statements[1].Expression.Arguments[1].LiteralValue,
        FString(TEXT("Custom Event Sequence Second")));
    return true;
}

namespace
{
bool RunNativeSequenceTest(FAutomationTestBase& Test, int32 Mode)
{
    const bool bSelfCall = Mode == 1 || Mode == 3;
    const bool bNumericCall = Mode == 3;
    const bool bMixedPrint = Mode == 2;
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(),
        TEXT("BP_NativeSequence"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        bSelfCall ? AActor::StaticClass() : UObject::StaticClass(),
        GetTransientPackage(), Name, BPTYPE_Normal, NAME_None);
    if (Test.TestNotNull(TEXT("ネイティブSequence用Blueprint"), Blueprint) == false) { return false; }
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("RunNativeSequence"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* NativeResult = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node)) { Entry = Candidate; }
        if (UK2Node_FunctionResult* Candidate = Cast<UK2Node_FunctionResult>(Node)) { NativeResult = Candidate; }
    }
    if (Test.TestNotNull(TEXT("通常関数Entry"), Entry) == false) { return false; }
    UK2Node_FunctionResult* Result = NativeResult;
    if (Result == nullptr)
    {
        Result = AddSequenceTestNode<UK2Node_FunctionResult>(Graph);
        Result->FunctionReference.SetSelfMember(TEXT("RunNativeSequence"));
        Result->AllocateDefaultPins();
    }
    UK2Node_ExecutionSequence* Sequence = AddSequenceTestNode<UK2Node_ExecutionSequence>(Graph);
    Sequence->AllocateDefaultPins();
    UFunction* StaticFunction = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetSubtitlesEnabled"));
    UFunction* SecondFunction = bSelfCall
        ? AActor::StaticClass()->FindFunctionByName(bNumericCall
            ? TEXT("SetActorTickInterval") : TEXT("SetActorTickEnabled")) : StaticFunction;
    if (Test.TestNotNull(TEXT("静的関数署名"), StaticFunction) == false ||
        Test.TestNotNull(TEXT("後続関数署名"), SecondFunction) == false) { return false; }
    const auto AddCall = [Graph](UFunction* Function, const FName ArgumentName, const TCHAR* Value)
    {
        UK2Node_CallFunction* Call = AddSequenceTestNode<UK2Node_CallFunction>(Graph);
        Call->SetFromFunction(Function);
        Call->AllocateDefaultPins();
        Call->FindPinChecked(ArgumentName)->DefaultValue = Value;
        return Call;
    };
    UK2Node_CallFunction* First = AddCall(StaticFunction, TEXT("bEnabled"), TEXT("true"));
    UK2Node_CallFunction* Second = bMixedPrint ? AddSequencePrint(Graph,
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")), TEXT("Mixed sequence"))
        : AddCall(SecondFunction, bNumericCall ? FName(TEXT("TickInterval")) : FName(TEXT("bEnabled")),
            bNumericCall ? TEXT("0.25") : TEXT("false"));
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    bool bConnected = Schema->TryCreateConnection(Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Sequence->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    bConnected &= Schema->TryCreateConnection(Sequence->FindPinChecked(TEXT("Then_0")),
        First->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    bConnected &= Schema->TryCreateConnection(Sequence->FindPinChecked(TEXT("Then_1")),
        Second->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    bConnected &= Schema->TryCreateConnection(Second->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    if (Test.TestTrue(TEXT("Sequenceの実Exec接続"), bConnected) == false) { return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Test.TestNotNull(TEXT("コンパイル済みクラス"), Blueprint->GeneratedClass.Get()) == false) { return false; }
    FBPGraphIR GraphIR;
    if (Test.TestTrue(TEXT("実Graph IRを構築"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false) { return false; }
    FBPSemanticIR IR;
    if (Test.TestTrue(TEXT("PrintStringに依存せずSequenceをLowering"),
            FBPSemanticLowering::Lower(Blueprint, GraphIR, IR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics) { Test.AddError(Diagnostic.Message); }
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (Test.TestTrue(TEXT("共通CallからC++生成"), FBPSemanticCodeGenerator::Generate(IR,
            bSelfCall ? TEXT("ANativeSequence") : TEXT("UNativeSequence"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { Test.AddError(Diagnostic); }
        return false;
    }
    const int32 FirstPosition = Files.SourceContent.Find(TEXT("UGameplayStatics::SetSubtitlesEnabled(true);"));
    const int32 SecondPosition = Files.SourceContent.Find(bMixedPrint ? TEXT("Mixed sequence") : bNumericCall
        ? TEXT("this->SetActorTickInterval(0.25f);") : bSelfCall
        ? TEXT("this->SetActorTickEnabled(false);") : TEXT("UGameplayStatics::SetSubtitlesEnabled(false);"));
    Test.TestTrue(TEXT("Sequence出力ピン順を生成"), FirstPosition != INDEX_NONE && SecondPosition > FirstPosition);
    Test.TestTrue(TEXT("静的関数のincludeを収集"), Files.SourceContent.Contains(TEXT("Kismet/GameplayStatics.h")));
    const TSharedRef<FJsonObject> Json = FBPSemanticJsonSerializer::ToJson(IR, TEXT("5.7"));
    const TArray<TSharedPtr<FJsonValue>>& Statements = Json->GetArrayField(TEXT("functions"))[0]->AsObject()
        ->GetArrayField(TEXT("blocks"))[0]->AsObject()->GetArrayField(TEXT("statements"));
    Test.TestEqual(TEXT("JSONに2つのCall文を保持"), Statements.Num(), 2);
    Test.TestEqual(TEXT("JSONにReflectionの所有者を保持"), Statements[0]->AsObject()->GetObjectField(TEXT("expression"))
        ->GetStringField(TEXT("function_owner_path")), UGameplayStatics::StaticClass()->GetPathName());

    for (int32 CaseIndex = 0; CaseIndex < 6; ++CaseIndex)
    {
        FBPGraphIR Invalid = GraphIR;
        FBPIRGraph* FunctionGraph = Invalid.Graphs.FindByPredicate([](const FBPIRGraph& Candidate)
            { return Candidate.Name == TEXT("RunNativeSequence"); });
        if (Test.TestNotNull(TEXT("拒否確認用Graph"), FunctionGraph) == false) { return false; }
        FBPIRNode* Call = FunctionGraph->Nodes.FindByPredicate([First](const FBPIRNode& Node)
            { return Node.Id == First->NodeGuid; });
        if (Test.TestNotNull(TEXT("拒否確認用Call"), Call) == false) { return false; }
        FBPIRPin* Argument = Call->Pins.FindByPredicate([](const FBPIRPin& Pin)
            { return Pin.Name == TEXT("bEnabled"); });
        if (Test.TestNotNull(TEXT("拒否確認用引数"), Argument) == false) { return false; }
        switch (CaseIndex)
        {
        case 0: Call->FunctionPath += TEXT("Missing"); break;
        case 1: Argument->Category = UEdGraphSchema_K2::PC_Int; break;
        case 2: Argument->DefaultValue = TEXT("not-a-bool"); break;
        case 3: Call->bFunctionStatic = false; break;
        case 4:
        {
            FBPIRPin Duplicate = *Argument;
            Call->Pins.Add(MoveTemp(Duplicate));
            break;
        }
        case 5: Call->bFunctionNative = false; break;
        }
        FBPSemanticIR Rejected;
        Test.TestFalse(TEXT("不正なCallを診断付きで拒否"), FBPSemanticLowering::Lower(Blueprint, Invalid, Rejected));
        Test.TestTrue(TEXT("失敗時に関数を公開しない"), Rejected.Functions.IsEmpty());
        Test.TestTrue(TEXT("関数名・ノード由来の診断を保持"), Rejected.Diagnostics.ContainsByPredicate(
            [First](const FBPSemanticDiagnostic& Diagnostic)
            { return Diagnostic.GraphName == TEXT("RunNativeSequence") && Diagnostic.NodeId == First->NodeGuid; }));
    }
    if (bSelfCall)
    {
        FBPGraphIR Invalid = GraphIR;
        for (FBPIRGraph& FunctionGraph : Invalid.Graphs)
        {
            for (FBPIRNode& Node : FunctionGraph.Nodes)
            {
                if (Node.Id != Second->NodeGuid) { continue; }
                for (FBPIRPin& Pin : Node.Pins)
                {
                    if (Pin.Name == UEdGraphSchema_K2::PN_Self)
                    {
                        Pin.DefaultObjectPath = TEXT("/Game/UnsupportedTarget");
                    }
                }
            }
        }
        FBPSemanticIR Rejected;
        Test.TestFalse(TEXT("self以外の明示的オブジェクトTargetを拒否"),
            FBPSemanticLowering::Lower(Blueprint, Invalid, Rejected));
        Test.TestTrue(TEXT("Target拒否時に関数を公開しない"), Rejected.Functions.IsEmpty());
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealNativeSequenceTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionSequenceNativeCalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealNativeSequenceTest::RunTest(const FString& Parameters) { return RunNativeSequenceTest(*this, 0); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealSelfSequenceTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionSequenceStaticAndSelf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealSelfSequenceTest::RunTest(const FString& Parameters) { return RunNativeSequenceTest(*this, 1); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealMixedSequenceTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionSequenceMixedPrint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealMixedSequenceTest::RunTest(const FString& Parameters) { return RunNativeSequenceTest(*this, 2); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealNumericSequenceTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionSequenceNumericCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealNumericSequenceTest::RunTest(const FString& Parameters) { return RunNativeSequenceTest(*this, 3); }

#endif
