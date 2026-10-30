#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPGraphIR.h"
#include "BPSemanticLowering.h"
#include "BPPrintStringAnalyzer.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticClassExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "BPSemanticJsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "K2Node_Event.h"
#include "GameFramework/Actor.h"
#include "Algo/Reverse.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticMultipleFunctionGraphRoundtripTest,
    "BlueprintAIConverter.SemanticIR.Lowering.MultipleFunctionGraphs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticMultipleFunctionGraphRoundtripTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_MultipleFunctionLowering"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("複数関数用UObject Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    const auto AddPrintFunction = [this, Blueprint, Schema](
        const FName FunctionName, const FString& Message) -> bool
    {
        UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint, FunctionName, UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (FunctionGraph == nullptr)
        {
            AddError(FString::Printf(TEXT("Failed to create function graph: %s"),
                *FunctionName.ToString()));
            return false;
        }
        FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
            Blueprint, FunctionGraph, true, nullptr);

        UK2Node_FunctionEntry* Entry = nullptr;
        for (UEdGraphNode* Node : FunctionGraph->Nodes)
        {
            Entry = Entry != nullptr ? Entry : Cast<UK2Node_FunctionEntry>(Node);
        }
        if (Entry == nullptr)
        {
            AddError(FString::Printf(TEXT("Function entry is missing: %s"),
                *FunctionName.ToString()));
            return false;
        }

        UK2Node_CallFunction* Print = NewObject<UK2Node_CallFunction>(FunctionGraph);
        FunctionGraph->AddNode(Print, false, false);
        Print->CreateNewGuid();
        Print->PostPlacedNewNode();
        Print->SetFromFunction(
            UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
        Print->AllocateDefaultPins();
        Print->FindPinChecked(TEXT("InString"))->DefaultValue = Message;
        return Schema->TryCreateConnection(
            Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            Print->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    };

    if (TestTrue(TEXT("First関数を構築"),
            AddPrintFunction(TEXT("RunFirst"), TEXT("First function"))) == false ||
        TestTrue(TEXT("Second関数を構築"),
            AddPrintFunction(TEXT("RunSecond"), TEXT("Second function"))) == false)
    {
        return false;
    }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("複数関数BlueprintのEvent Graph"), EventGraph) == false)
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
    UK2Node_CallFunction* EventPrint = NewObject<UK2Node_CallFunction>(EventGraph);
    EventGraph->AddNode(EventPrint, false, false);
    EventPrint->CreateNewGuid();
    EventPrint->PostPlacedNewNode();
    EventPrint->SetFromFunction(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
    EventPrint->AllocateDefaultPins();
    EventPrint->FindPinChecked(TEXT("InString"))->DefaultValue = TEXT("Converted event");
    if (TestTrue(TEXT("Custom Eventを接続"), Schema->TryCreateConnection(
            Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            EventPrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false)
    {
        return false;
    }
    FEdGraphPinType CountType;
    CountType.PinCategory = UEdGraphSchema_K2::PC_Int;
    if (TestTrue(TEXT("共有メンバーを追加"), FBlueprintEditorUtils::AddMemberVariable(
            Blueprint, TEXT("Count"), CountType, TEXT("42"))) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("複数Function Graph IRを構築"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("複数Function Graphを一括Lowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (TestEqual(TEXT("通常関数2つとイベントを保持"), Semantic.Functions.Num(), 3) == false)
    {
        return false;
    }
    TestTrue(TEXT("RunFirstを保持"), Semantic.Functions.ContainsByPredicate(
        [](const FBPSemanticFunction& Function)
        {
            return Function.Name == TEXT("RunFirst");
        }));
    TestTrue(TEXT("RunSecondを保持"), Semantic.Functions.ContainsByPredicate(
        [](const FBPSemanticFunction& Function)
        {
            return Function.Name == TEXT("RunSecond");
        }));

    TestEqual(TEXT("共有メンバーを重複なく保持"), Semantic.Variables.Num(), 1);
    TestTrue(TEXT("Custom Event由来を保持"), Semantic.Functions.ContainsByPredicate(
        [](const FBPSemanticFunction& Function)
        {
            return Function.Name == TEXT("OnConverted") && Function.bIsEventGraph;
        }));
    for (const FBPSemanticFunction& Function : Semantic.Functions)
    {
        TestEqual(TEXT("実Reflectionのpublicを保持"), Function.Visibility,
            EBPSemanticFunctionVisibility::Public);
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("複数関数を一つのUCLASSへ生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("UMultipleFunctionGenerated"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("RunFirst定義を生成"), Files.SourceContent.Contains(
        TEXT("void UMultipleFunctionGenerated::RunFirst()")));
    TestTrue(TEXT("RunSecond定義を生成"), Files.SourceContent.Contains(
        TEXT("void UMultipleFunctionGenerated::RunSecond()")));

    TestTrue(TEXT("Custom Event本体を同じクラスへ生成"), Files.SourceContent.Contains(
        TEXT("void UMultipleFunctionGenerated::OnConverted_Implementation()")));
    TestTrue(TEXT("UObject親を保持"), Files.HeaderContent.Contains(
        TEXT("class UMultipleFunctionGenerated : public UObject")));
    FBPGraphIR DuplicateGraphIR = GraphIR;
    const FBPIRGraph* FirstGraph = GraphIR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Graph) { return Graph.Name == TEXT("RunFirst"); });
    if (TestNotNull(TEXT("重複確認用RunFirst"), FirstGraph) == false) { return false; }
    DuplicateGraphIR.Graphs.Add(*FirstGraph);
    FBPSemanticIR DuplicateSemantic;
    TestFalse(TEXT("同名関数グラフを統合時に拒否"),
        FBPSemanticLowering::Lower(Blueprint, DuplicateGraphIR, DuplicateSemantic));
    TestTrue(TEXT("重複拒否時に部分関数・変数を公開しない"),
        DuplicateSemantic.Functions.IsEmpty() && DuplicateSemantic.Variables.IsEmpty());
    TestTrue(TEXT("重複の関数名とグラフ名を診断"),
        DuplicateSemantic.Diagnostics.ContainsByPredicate([](const FBPSemanticDiagnostic& Diagnostic)
        {
            return Diagnostic.GraphName == TEXT("RunFirst") &&
                Diagnostic.Message.Contains(TEXT("Duplicate semantic function"));
        }));
    FBPGraphIR UnsupportedGraphIR = GraphIR;
    FBPIRGraph* SecondGraph = UnsupportedGraphIR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Graph)
        {
            return Graph.Name == TEXT("RunSecond");
        });
    if (TestNotNull(TEXT("拒否確認用RunSecond Graph"), SecondGraph) == false)
    {
        return false;
    }
    FBPIRNode* SecondPrint = SecondGraph->Nodes.FindByPredicate(
        [](const FBPIRNode& Node)
        {
            return Node.FunctionName == TEXT("PrintString");
        });
    if (TestNotNull(TEXT("拒否確認用RunSecond PrintString"), SecondPrint) == false)
    {
        return false;
    }
    SecondPrint->FunctionName = TEXT("UnsupportedCall");
    FBPSemanticIR RejectedSemantic;
    TestFalse(TEXT("一つでも未対応の関数があれば一括Loweringを拒否"),
        FBPSemanticLowering::Lower(Blueprint, UnsupportedGraphIR, RejectedSemantic));
    TestTrue(TEXT("失敗時に部分的な関数集合を公開しない"),
        RejectedSemantic.Functions.IsEmpty());
    TestTrue(TEXT("失敗理由を保持"), RejectedSemantic.HasErrors());
    TestTrue(TEXT("失敗時に部分的なメンバーも公開しない"), RejectedSemantic.Variables.IsEmpty());

    FBPSemanticGraphTraceExtractor::Append(GraphIR, Semantic);
    TestTrue(TEXT("クラス情報を抽出"), FBPSemanticClassExtractor::Append(Blueprint, Semantic));
    FString Serialized;
    const FString EngineVersion = FEngineVersion::Current().ToString();
    if (TestTrue(TEXT("複数関数をJSONへ変換"),
            FBPSemanticJsonSerializer::Serialize(Semantic, EngineVersion, Serialized)) == false)
    {
        return false;
    }
    FString ExportPath;
    FString ExportError;
    if (TestTrue(TEXT("複数関数とCustom Eventの実JSONを保存"),
            FBPSemanticJsonExporter::Export(Semantic, EngineVersion, ExportPath, ExportError)) == false)
    {
        AddError(ExportError);
        return false;
    }
    FString Exported;
    if (TestTrue(TEXT("保存JSONを再読込"),
            FFileHelper::LoadFileToString(Exported, *ExportPath)) == false) { return false; }
    TestEqual(TEXT("再シリアライズ時も出力を維持"), Exported, Serialized);
    TSharedPtr<FJsonObject> Root;
    if (TestTrue(TEXT("保存JSONを再解析"), FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Exported), Root)) == false || Root.IsValid() == false)
    {
        return false;
    }
    TestEqual(TEXT("JSONに関数3つを保持"), Root->GetArrayField(TEXT("functions")).Num(), 3);
    TestEqual(TEXT("JSONに共有変数1つを保持"), Root->GetArrayField(TEXT("variables")).Num(), 1);
    TestEqual(TEXT("JSONに全グラフの由来を保持"),
        Root->GetArrayField(TEXT("graph_traces")).Num(), GraphIR.Graphs.Num());
    TestEqual(TEXT("JSONにUObject親を保持"),
        Root->GetObjectField(TEXT("source"))->GetStringField(TEXT("parent_class_path")),
        UObject::StaticClass()->GetPathName());
    bool bFoundEvent = false;
    for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("functions")))
    {
        const TSharedPtr<FJsonObject> FunctionJson = Value->AsObject();
        TestEqual(TEXT("JSONにpublicを保持"), FunctionJson->GetStringField(TEXT("visibility")),
            FString(TEXT("public")));
        if (FunctionJson->GetStringField(TEXT("name")) == TEXT("OnConverted"))
        {
            bFoundEvent = FunctionJson->GetBoolField(TEXT("is_event_graph"));
        }
    }
    TestTrue(TEXT("JSONにCustom Event由来を保持"), bFoundEvent);
    AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));
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

namespace
{
bool RunEntryCallChainTest(FAutomationTestBase& Test, int32 Mode)
{
    const bool bFunction = Mode == 0 || Mode == 3 || Mode == 4;
    const bool bSingleCall = Mode == 4;
    const bool bBeginPlay = Mode == 2;
    const bool bMixedPrint = Mode == 3;
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        bBeginPlay ? AActor::StaticClass() : UObject::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CallChain")),
        BPTYPE_Normal, NAME_None);
    if (Test.TestNotNull(TEXT("Call列用Blueprint"), Blueprint) == false) { return false; }
    UEdGraph* Graph = nullptr;
    UK2Node* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    if (bFunction)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("RunCalls"),
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node)) { Entry = Candidate; }
            if (UK2Node_FunctionResult* Candidate = Cast<UK2Node_FunctionResult>(Node)) { Result = Candidate; }
        }
        if (Result == nullptr)
        {
            Result = NewObject<UK2Node_FunctionResult>(Graph);
            Graph->AddNode(Result, false, false);
            Result->CreateNewGuid();
            Result->PostPlacedNewNode();
            Result->FunctionReference.SetSelfMember(TEXT("RunCalls"));
            Result->AllocateDefaultPins();
        }
    }
    else
    {
        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        if (Test.TestNotNull(TEXT("Event Graph"), Graph) == false) { return false; }
        Graph->Nodes.Reset();
        if (bBeginPlay)
        {
            UK2Node_Event* Event = NewObject<UK2Node_Event>(Graph);
            Event->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
            Event->bOverrideFunction = true;
            Entry = Event;
        }
        else
        {
            UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
            Event->CustomFunctionName = TEXT("OnCalls");
            Entry = Event;
        }
        Graph->AddNode(Entry, false, false);
        Entry->CreateNewGuid();
        Entry->PostPlacedNewNode();
        Entry->AllocateDefaultPins();
    }
    if (Test.TestNotNull(TEXT("Entryを取得"), Entry) == false) { return false; }
    UFunction* Static = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetSubtitlesEnabled"));
    UFunction* LastFunction = bBeginPlay
        ? AActor::StaticClass()->FindFunctionByName(TEXT("SetActorTickEnabled")) : Static;
    if (Test.TestNotNull(TEXT("static署名"), Static) == false ||
        Test.TestNotNull(TEXT("後続署名"), LastFunction) == false) { return false; }
    const auto AddCall = [Graph](UFunction* Function, const TCHAR* Value)
    {
        UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
        Graph->AddNode(Call, false, false);
        Call->CreateNewGuid();
        Call->PostPlacedNewNode();
        Call->SetFromFunction(Function);
        Call->AllocateDefaultPins();
        Call->FindPinChecked(TEXT("bEnabled"))->DefaultValue = Value;
        return Call;
    };
    UK2Node_CallFunction* First = AddCall(Static, TEXT("true"));
    UK2Node_CallFunction* Last = bSingleCall ? First : AddCall(LastFunction, TEXT("false"));
    UK2Node_CallFunction* Middle = nullptr;
    if (bMixedPrint)
    {
        Middle = NewObject<UK2Node_CallFunction>(Graph);
        Graph->AddNode(Middle, false, false);
        Middle->CreateNewGuid();
        Middle->PostPlacedNewNode();
        Middle->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
        Middle->AllocateDefaultPins();
        Middle->FindPinChecked(TEXT("InString"))->DefaultValue = TEXT("Middle call");
    }
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    const auto Connect = [Schema](UK2Node* From, UK2Node* To)
    {
        return Schema->TryCreateConnection(From->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            To->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    };
    Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    bool bConnected = Connect(Entry, First);
    if (bSingleCall == false)
    {
        bConnected &= Middle != nullptr ? Connect(First, Middle) && Connect(Middle, Last) : Connect(First, Last);
    }
    if (Result != nullptr) { bConnected &= Connect(Last, Result); }
    if (Test.TestTrue(TEXT("実Call列を接続"), bConnected) == false) { return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FBPGraphIR GraphIR;
    if (Test.TestTrue(TEXT("Call列のGraph IRを構築"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false) { return false; }
    FBPIRGraph* Active = GraphIR.Graphs.FindByPredicate([Graph](const FBPIRGraph& Candidate)
        { return Candidate.Name == Graph->GetName(); });
    if (Test.TestNotNull(TEXT("実行Graph"), Active) == false) { return false; }
    // Storage order must not determine execution order.
    Algo::Reverse(Active->Nodes);
    Algo::Reverse(Active->ExecEdges);
    FBPSemanticIR IR;
    if (Test.TestTrue(TEXT("共通EntryからCall列をLowering"), FBPSemanticLowering::Lower(Blueprint, GraphIR, IR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics) { Test.AddError(Diagnostic.Message); }
        return false;
    }
    if (Test.TestEqual(TEXT("生成関数数"), IR.Functions.Num(), 1) == false) { return false; }
    const int32 ExpectedCalls = bSingleCall ? 1 : bMixedPrint ? 3 : 2;
    Test.TestEqual(TEXT("Call列の文数"), IR.Functions[0].Blocks[0].Statements.Num(), ExpectedCalls);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (Test.TestTrue(TEXT("Call列からC++を生成"), FBPSemanticCodeGenerator::Generate(IR,
            bBeginPlay ? TEXT("ACallChain") : TEXT("UCallChain"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { Test.AddError(Diagnostic); }
        return false;
    }
    const int32 FirstPosition = Files.SourceContent.Find(TEXT("UGameplayStatics::SetSubtitlesEnabled(true);"));
    const int32 LastPosition = bSingleCall ? FirstPosition : Files.SourceContent.Find(bBeginPlay
        ? TEXT("this->SetActorTickEnabled(false);") : TEXT("UGameplayStatics::SetSubtitlesEnabled(false);"));
    Test.TestTrue(TEXT("Exec順を配列順に依存せず保持"),
        FirstPosition != INDEX_NONE && (bSingleCall || LastPosition > FirstPosition));
    if (bMixedPrint)
    {
        const int32 MiddlePosition = Files.SourceContent.Find(TEXT("Middle call"));
        Test.TestTrue(TEXT("PrintString混在順を保持"), MiddlePosition > FirstPosition && LastPosition > MiddlePosition);
    }
    const TSharedRef<FJsonObject> Json = FBPSemanticJsonSerializer::ToJson(IR, TEXT("5.7"));
    Test.TestEqual(TEXT("JSONもCall列を保持"), Json->GetArrayField(TEXT("functions"))[0]->AsObject()
        ->GetArrayField(TEXT("blocks"))[0]->AsObject()->GetArrayField(TEXT("statements")).Num(), ExpectedCalls);
    for (int32 CaseIndex = 0; CaseIndex < 7; ++CaseIndex)
    {
        FBPGraphIR Invalid = GraphIR;
        FBPIRGraph* Target = Invalid.Graphs.FindByPredicate([Graph](const FBPIRGraph& Candidate)
            { return Candidate.Name == Graph->GetName(); });
        FBPIRLink* FirstEdge = Target->ExecEdges.FindByPredicate([First](const FBPIRLink& Edge)
            { return Edge.SourceNode == First->NodeGuid; });
        if (Test.TestNotNull(TEXT("拒否確認用Exec edge"), FirstEdge) == false) { return false; }
        switch (CaseIndex)
        {
        case 0: { FBPIRLink Duplicate = *FirstEdge; Target->ExecEdges.Add(Duplicate); break; }
        case 1: FirstEdge->SourcePin = FGuid::NewGuid(); break;
        case 2: FirstEdge->TargetPin = FGuid::NewGuid(); break;
        case 3:
        {
            Target->ExecEdges.RemoveAll([Last](const FBPIRLink& Edge) { return Edge.SourceNode == Last->NodeGuid; });
            FBPIRLink Cycle;
            Cycle.SourceNode = Last->NodeGuid;
            Cycle.SourcePin = Last->FindPinChecked(UEdGraphSchema_K2::PN_Then)->PinId;
            Cycle.TargetNode = First->NodeGuid;
            Cycle.TargetPin = First->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId;
            Target->ExecEdges.Add(Cycle);
            break;
        }
        case 4:
        {
            FBPIRLink Extra;
            Extra.SourceNode = FGuid::NewGuid(); Extra.SourcePin = FGuid::NewGuid();
            Extra.TargetNode = First->NodeGuid;
            Extra.TargetPin = First->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId;
            Target->ExecEdges.Add(Extra);
            break;
        }
        case 5:
        {
            FBPIRLink Data;
            Data.SourceNode = FGuid::NewGuid(); Data.SourcePin = FGuid::NewGuid();
            Data.TargetNode = First->NodeGuid; Data.TargetPin = First->FindPinChecked(TEXT("bEnabled"))->PinId;
            Target->DataEdges.Add(Data);
            break;
        }
        case 6:
            for (FBPIRNode& Node : Target->Nodes)
            {
                if (Node.Id == Last->NodeGuid) { Node.FunctionPath += TEXT("Missing"); }
            }
            break;
        }
        FBPSemanticIR Rejected;
        Test.TestFalse(TEXT("不正なCall列を拒否"), FBPSemanticLowering::Lower(Blueprint, Invalid, Rejected));
        Test.TestTrue(TEXT("失敗時に部分関数を公開しない"), Rejected.Functions.IsEmpty());
        Test.TestTrue(TEXT("Call列の拒否理由を保持"), Rejected.HasErrors());
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPFunctionCallChainTest,
    "BlueprintAIConverter.SemanticIR.Lowering.FunctionCallChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPFunctionCallChainTest::RunTest(const FString& Parameters) { return RunEntryCallChainTest(*this, 0); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCustomEventCallChainTest,
    "BlueprintAIConverter.SemanticIR.Lowering.CustomEventCallChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPCustomEventCallChainTest::RunTest(const FString& Parameters) { return RunEntryCallChainTest(*this, 1); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPBeginPlayCallChainTest,
    "BlueprintAIConverter.SemanticIR.Lowering.BeginPlayCallChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPBeginPlayCallChainTest::RunTest(const FString& Parameters) { return RunEntryCallChainTest(*this, 2); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPMixedCallChainTest,
    "BlueprintAIConverter.SemanticIR.Lowering.MixedPrintCallChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPMixedCallChainTest::RunTest(const FString& Parameters) { return RunEntryCallChainTest(*this, 3); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSingleNativeCallTest,
    "BlueprintAIConverter.SemanticIR.Lowering.SingleNativeCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPSingleNativeCallTest::RunTest(const FString& Parameters) { return RunEntryCallChainTest(*this, 4); }

#endif
