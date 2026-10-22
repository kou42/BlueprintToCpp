#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
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
#include "K2Node_ExecutionSequence.h"
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
    ExtraDataIR.Graphs[EventGraphIndex].DataEdges.Add(FBPIRLink());
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

#endif
