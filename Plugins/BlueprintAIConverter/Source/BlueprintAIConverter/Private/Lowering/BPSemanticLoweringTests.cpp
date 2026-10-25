#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPGraphIR.h"
#include "BPSemanticLowering.h"
#include "BPPrintStringAnalyzer.h"
#include "BPSemanticCodeGenerator.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
FBPGraphIR MakeSemanticTestGraph()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    IR.AssetPath = TEXT("/Game/BP_Test");
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
    Text.Direction = EGPD_Input;
    Text.Category = TEXT("string");
    Text.DefaultValue = TEXT("Hello Semantic IR");
    Print.Pins.Add(Text);

    FBPIRLink Link;
    Link.SourceNode = Event.Id;
    Link.TargetNode = Print.Id;
    Graph.ExecEdges.Add(Link);
    Graph.Nodes.Add(MoveTemp(Event));
    Graph.Nodes.Add(MoveTemp(Print));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringLiteralTest,
    "BlueprintAIConverter.SemanticIR.Lowering.PrintString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringLiteralTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPPrintStringCall LegacyCall;
    TArray<FString> LegacyDiagnostics;
    TestTrue(TEXT("既存Analyzerが成功"), FBPPrintStringAnalyzer::Analyze(IR, LegacyCall, LegacyDiagnostics));

    FBPSemanticIR Semantic;
    TestTrue(TEXT("Semantic Loweringが成功"), FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestFalse(TEXT("診断がない"), Semantic.HasErrors());
    TestEqual(TEXT("関数数"), Semantic.Functions.Num(), 1);
    if (Semantic.Functions.Num() != 1)
    {
        return false;
    }
    const FBPSemanticFunction& Function = Semantic.Functions[0];
    TestEqual(TEXT("関数名"), Function.Name, FName(TEXT("BeginPlay")));
    if (Function.Blocks.Num() != 1 || Function.Blocks[0].Statements.Num() != 1)
    {
        AddError(TEXT("Expected exactly one block and statement"));
        return false;
    }
    const FBPSemanticStatement& Statement = Function.Blocks[0].Statements[0];
    TestEqual(TEXT("ノードIDを保持"), Statement.SourceNodeId, IR.Graphs[0].Nodes[1].Id);
    const TArray<FBPSemanticExpression>& Args = Statement.Expression.Arguments;
    TestEqual(TEXT("引数数"), Args.Num(), 7);
    if (Args.Num() != 7)
    {
        return false;
    }
    TestEqual(TEXT("既存Analyzerと文字列一致"), Args[1].LiteralValue, LegacyCall.InString);
    TestEqual(TEXT("ピンIDを保持"), Args[1].SourcePinId, IR.Graphs[0].Nodes[1].Pins[0].Id);
    TestEqual(TEXT("Duration一致"), Args[5].LiteralValue, FString::SanitizeFloat(LegacyCall.Duration));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringRejectTest,
    "BlueprintAIConverter.SemanticIR.Lowering.RejectConnectedData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringRejectTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPIRLink Data;
    Data.TargetNode = IR.Graphs[0].Nodes[1].Id;
    Data.TargetPin = IR.Graphs[0].Nodes[1].Pins[0].Id;
    IR.Graphs[0].DataEdges.Add(Data);

    FBPSemanticIR Semantic;
    TestFalse(TEXT("未対応データ接続は拒否"), FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("診断が存在"), Semantic.HasErrors());
    TestEqual(TEXT("失敗時に関数を生成しない"), Semantic.Functions.Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringStandardGraphsTest,
    "BlueprintAIConverter.SemanticIR.Lowering.StandardGraphs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringStandardGraphsTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPIRGraph Construction;
    Construction.Name = TEXT("UserConstructionScript");
    Construction.Kind = TEXT("function");
    FBPIRNode Entry;
    Entry.Id = FGuid::NewGuid();
    Entry.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_FunctionEntry");
    Construction.Nodes.Add(Entry);
    IR.Graphs.Add(Construction);

    FBPIRNode Tick;
    Tick.Id = FGuid::NewGuid();
    Tick.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Tick.FunctionName = TEXT("ReceiveTick");
    IR.Graphs[0].Nodes.Add(Tick);

    FBPSemanticIR Semantic;
    TestTrue(TEXT("空Construction Scriptと未接続Tickを許容"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestEqual(TEXT("BeginPlayだけ生成"), Semantic.Functions.Num(), 1);

    // Construction Scriptに実行エッジが存在する場合は、意味を落とさず拒否する。
    FBPIRLink Unsupported;
    Unsupported.SourceNode = Entry.Id;
    Unsupported.TargetNode = FGuid::NewGuid();
    IR.Graphs[1].ExecEdges.Add(Unsupported);
    TestFalse(TEXT("実行可能Construction Scriptを拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("失敗理由を保持"), Semantic.HasErrors());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringVariableTest,
    "BlueprintAIConverter.SemanticIR.Lowering.RejectMemberVariable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringVariableTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    IR.BlueprintVariableCount = 1;
    FBPSemanticIR Semantic;
    TestFalse(TEXT("未対応メンバー変数を拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("診断を保持"), Semantic.HasErrors());
    TestTrue(TEXT("部分的な関数を公開しない"), Semantic.Functions.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringInvalidOptionsTest,
    "BlueprintAIConverter.SemanticIR.Lowering.RejectInvalidOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringInvalidOptionsTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    FBPIRPin Color;
    Color.Name = TEXT("TextColor");
    Color.Direction = EGPD_Input;
    Color.DefaultValue = TEXT("(R=nan,G=0,B=0,A=1)");
    IR.Graphs[0].Nodes[1].Pins.Add(Color);

    FBPSemanticIR Semantic;
    TestFalse(TEXT("非有限な色を拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("色エラーの診断あり"), Semantic.HasErrors());
    TestTrue(TEXT("失敗時に関数を生成しない"), Semantic.Functions.IsEmpty());

    IR.Graphs[0].Nodes[1].Pins.Last().DefaultValue = TEXT("(R=1,G=0.5,B=0,A=1)");
    FBPIRPin Screen;
    Screen.Name = TEXT("bPrintToScreen");
    Screen.Direction = EGPD_Input;
    Screen.DefaultValue = TEXT("maybe");
    IR.Graphs[0].Nodes[1].Pins.Add(Screen);
    TestFalse(TEXT("不正な真偽値を拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("真偽値エラーの診断あり"), Semantic.HasErrors());
    TestTrue(TEXT("失敗時に関数を生成しない"), Semantic.Functions.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticLoweringGraphDiagnosticsTest,
    "BlueprintAIConverter.SemanticIR.Lowering.RejectGraphDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticLoweringGraphDiagnosticsTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeSemanticTestGraph();
    IR.Diagnostics.Add(TEXT("Graph IR contains unsupported data"));
    FBPSemanticIR Semantic;
    TestFalse(TEXT("Graph IR診断がある場合は拒否"),
        FBPSemanticLowering::LowerPrintString(IR, Semantic));
    TestTrue(TEXT("診断をSemantic IRへ引き継ぐ"), Semantic.HasErrors());
    TestTrue(TEXT("不完全な関数を残さない"), Semantic.Functions.IsEmpty());
    TestFalse(TEXT("共通LowerもGraph IR診断を振り分け前に拒否"),
        FBPSemanticLowering::Lower(nullptr, IR, Semantic));
    TestTrue(TEXT("共通Lowerが元の診断を保持"),
        Semantic.Diagnostics.ContainsByPredicate(
            [](const FBPSemanticDiagnostic& Diagnostic)
            {
                return Diagnostic.Message == TEXT("Graph IR contains unsupported data");
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticExecutableEntryAuditTest,
    "BlueprintAIConverter.SemanticIR.Lowering.ExecutableEntryAudit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticExecutableEntryAuditTest::RunTest(const FString& Parameters)
{
    auto HasDiagnostic = [](const FBPSemanticIR& IR, const FString& Text)
    {
        return IR.Diagnostics.ContainsByPredicate(
            [&Text](const FBPSemanticDiagnostic& Diagnostic)
            {
                return Diagnostic.Message.Contains(Text);
            });
    };

    FBPSemanticIR Semantic;
    FBPGraphIR MissingEntry = MakeSemanticTestGraph();
    MissingEntry.Graphs[0].Nodes.RemoveAt(0);
    TestFalse(TEXT("Entryのない実行グラフを拒否"),
        FBPSemanticLowering::Lower(nullptr, MissingEntry, Semantic));
    TestTrue(TEXT("Entry欠落の具体的診断"),
        HasDiagnostic(Semantic, TEXT("no active execution entry")));

    FBPGraphIR MultipleEntries = MakeSemanticTestGraph();
    FBPIRNode SecondEntry;
    SecondEntry.Id = FGuid::NewGuid();
    SecondEntry.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CustomEvent");
    SecondEntry.FunctionName = TEXT("SecondEvent");
    FBPIRNode SecondPrint;
    SecondPrint.Id = FGuid::NewGuid();
    SecondPrint.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    SecondPrint.FunctionName = TEXT("PrintString");
    FBPIRLink SecondEdge;
    SecondEdge.SourceNode = SecondEntry.Id;
    SecondEdge.TargetNode = SecondPrint.Id;
    MultipleEntries.Graphs[0].Nodes.Add(MoveTemp(SecondEntry));
    MultipleEntries.Graphs[0].Nodes.Add(MoveTemp(SecondPrint));
    MultipleEntries.Graphs[0].ExecEdges.Add(SecondEdge);
    TestFalse(TEXT("同一グラフの複数active Entryを拒否"),
        FBPSemanticLowering::Lower(nullptr, MultipleEntries, Semantic));
    TestTrue(TEXT("複数Entryの具体的診断"),
        HasDiagnostic(Semantic, TEXT("multiple active execution entries")));

    FBPGraphIR MultipleGraphs = MakeSemanticTestGraph();
    FBPGraphIR Other = MakeSemanticTestGraph();
    Other.Graphs[0].Name = TEXT("OtherEventGraph");
    MultipleGraphs.Graphs.Add(MoveTemp(Other.Graphs[0]));
    TestFalse(TEXT("複数の実行Entryグラフを拒否"),
        FBPSemanticLowering::Lower(nullptr, MultipleGraphs, Semantic));
    TestTrue(TEXT("複数実行グラフの名前付き診断"),
        HasDiagnostic(Semantic, TEXT("EventGraph and OtherEventGraph")));

    FBPGraphIR WrongKind = MakeSemanticTestGraph();
    WrongKind.Graphs[0].Kind = TEXT("function");
    TestFalse(TEXT("EntryとGraph Kindの不一致を拒否"),
        FBPSemanticLowering::Lower(nullptr, WrongKind, Semantic));
    TestTrue(TEXT("Graph Kind不一致の具体的診断"),
        HasDiagnostic(Semantic, TEXT("does not match its graph kind")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticFunctionGraphRoundtripTest,
    "BlueprintAIConverter.SemanticIR.Lowering.FunctionGraphPrintString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticFunctionGraphRoundtripTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_FunctionGraphLowering"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("UObject Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, TEXT("RunConversion"), UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (TestNotNull(TEXT("Function Graphを生成"), FunctionGraph) == false)
    {
        return false;
    }
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
        Blueprint, FunctionGraph, true, nullptr);

    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
    {
        Entry = Entry != nullptr ? Entry : Cast<UK2Node_FunctionEntry>(Node);
        Result = Result != nullptr ? Result : Cast<UK2Node_FunctionResult>(Node);
    }
    if (TestNotNull(TEXT("Function Entryを取得"), Entry) == false)
    {
        return false;
    }

    UK2Node_CallFunction* Print = NewObject<UK2Node_CallFunction>(FunctionGraph);
    Print->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
    Print->AllocateDefaultPins();
    FunctionGraph->AddNode(Print, false, false);
    UEdGraphPin* TextPin = Print->FindPin(TEXT("InString"));
    if (TestNotNull(TEXT("PrintString text pin"), TextPin) == false)
    {
        return false;
    }
    TextPin->DefaultValue = TEXT("Function graph");
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (TestTrue(TEXT("EntryからPrintStringを接続"), Schema->TryCreateConnection(
            Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            Print->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("Function Graph IRを構築"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }

    // 同じグラフ内または別の未接続グラフに残った制御ノードは、実行Entryを持つ
    // RunConversionのStatement Lowering選択へ影響してはならない。
    const auto AddDisconnectedPatterns = [](FBPIRGraph& Target)
    {
        FBPIRNode InactiveBranch;
        InactiveBranch.Id = FGuid::NewGuid();
        InactiveBranch.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
        Target.Nodes.Add(MoveTemp(InactiveBranch));
        FBPIRNode InactiveSequence;
        InactiveSequence.Id = FGuid::NewGuid();
        InactiveSequence.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence");
        Target.Nodes.Add(MoveTemp(InactiveSequence));
        FBPIRNode InactiveForLoop;
        InactiveForLoop.Id = FGuid::NewGuid();
        InactiveForLoop.MacroGraphName = TEXT("ForLoop");
        Target.Nodes.Add(MoveTemp(InactiveForLoop));
        FBPIRNode InactiveForEach;
        InactiveForEach.Id = FGuid::NewGuid();
        InactiveForEach.MacroGraphName = TEXT("ForEachLoop");
        Target.Nodes.Add(MoveTemp(InactiveForEach));
    };
    FBPIRGraph* ActiveFunctionGraph = GraphIR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Graph)
        {
            return Graph.Name == TEXT("RunConversion");
        });
    if (TestNotNull(TEXT("到達可能性テストのFunction Graph"), ActiveFunctionGraph) == false)
    {
        return false;
    }
    AddDisconnectedPatterns(*ActiveFunctionGraph);

    FBPIRGraph InactiveGraph;
    InactiveGraph.Name = TEXT("InactivePatterns");
    InactiveGraph.Kind = TEXT("function");
    AddDisconnectedPatterns(InactiveGraph);
    GraphIR.Graphs.Add(MoveTemp(InactiveGraph));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Function GraphをSemantic Lowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (TestEqual(TEXT("通常関数を1つ生成"), Semantic.Functions.Num(), 1) == false)
    {
        return false;
    }
    TestEqual(TEXT("関数名を保持"), Semantic.Functions[0].Name, FName(TEXT("RunConversion")));
    TestEqual(TEXT("Reflection署名を保持"), Semantic.Functions[0].SignatureSource,
        FString(TEXT("generated_class_reflection")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("通常関数をC++生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UFunctionGraphGenerated"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("通常関数定義を生成"), Files.SourceContent.Contains(
        TEXT("void UFunctionGraphGenerated::RunConversion()")));
    TestFalse(TEXT("BeginPlay定義を生成しない"), Files.SourceContent.Contains(TEXT("BeginPlay")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticCustomEventRoundtripTest,
    "BlueprintAIConverter.SemanticIR.Lowering.CustomEventPrintString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticCustomEventRoundtripTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CustomEventLowering"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Custom Event用Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("Event Graphを取得"), EventGraph) == false)
    {
        return false;
    }
    EventGraph->Nodes.Reset();

    UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(EventGraph);
    EventGraph->AddNode(Event, false, false);
    Event->CreateNewGuid();
    Event->PostPlacedNewNode();
    Event->CustomFunctionName = TEXT("OnConverted");
    Event->AllocateDefaultPins();

    UK2Node_CallFunction* Print = NewObject<UK2Node_CallFunction>(EventGraph);
    EventGraph->AddNode(Print, false, false);
    Print->CreateNewGuid();
    Print->PostPlacedNewNode();
    Print->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = TEXT("Custom event");
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    if (TestNotNull(TEXT("K2 schemaを取得"), Schema) == false ||
        TestTrue(TEXT("Custom EventからPrintStringを接続"), Schema->TryCreateConnection(
            Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            Print->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("Custom Event Graph IRを構築"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    const FBPIRNode* EventIR = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        EventIR = Graph.Nodes.FindByPredicate([](const FBPIRNode& Node)
        {
            return Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CustomEvent");
        });
        if (EventIR != nullptr)
        {
            break;
        }
    }
    if (TestNotNull(TEXT("Custom Event IRを取得"), EventIR) == false)
    {
        return false;
    }
    TestEqual(TEXT("Custom Event名をGraph IRへ保持"),
        EventIR->FunctionName, FName(TEXT("OnConverted")));

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Custom EventをSemantic Lowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (TestEqual(TEXT("Custom Eventを1つ生成"), Semantic.Functions.Num(), 1) == false)
    {
        return false;
    }
    TestTrue(TEXT("Event Graph由来を保持"), Semantic.Functions[0].bIsEventGraph);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Custom EventをC++生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UCustomEventGenerated"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("BlueprintNativeEventとして宣言"), Files.HeaderContent.Contains(
        TEXT("UFUNCTION(BlueprintNativeEvent, BlueprintCallable)")));
    TestTrue(TEXT("Custom Event本体をImplementationへ生成"), Files.SourceContent.Contains(
        TEXT("void UCustomEventGenerated::OnConverted_Implementation()")));
    return true;
}

#endif
