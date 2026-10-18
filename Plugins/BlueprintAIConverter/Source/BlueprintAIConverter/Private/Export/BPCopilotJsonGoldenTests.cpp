#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPCopilotGraphJsonExporter.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticLowering.h"
#include "BPSnapshotExporter.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
template <typename NodeType>
NodeType* AddGoldenNode(UEdGraph* Graph, const FGuid& Id)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->PostPlacedNewNode();
    Node->NodeGuid = Id;
    return Node;
}

TSharedPtr<FJsonObject> ParseJson(const FString& Text)
{
    TSharedPtr<FJsonObject> Result;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    FJsonSerializer::Deserialize(Reader, Result);
    return Result;
}

TSharedPtr<FJsonObject> FindObjectByStringField(
    const TArray<TSharedPtr<FJsonValue>>& Values, const TCHAR* Field, const FString& Expected)
{
    for (const TSharedPtr<FJsonValue>& Value : Values)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Actual;
        if (Object.IsValid() && Object->TryGetStringField(Field, Actual) && Actual == Expected)
        {
            return Object;
        }
    }
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCopilotJsonMinimalNativeCallGoldenTest,
    "BlueprintAIConverter.CopilotJson.CJ0.MinimalNativeCallGolden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPCopilotJsonMinimalNativeCallGoldenTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CJ0_Golden"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName, BPTYPE_Normal, NAME_None);
    TestNotNull(TEXT("Create transient golden Blueprint"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }

    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    TestNotNull(TEXT("Find event graph"), EventGraph);
    if (EventGraph == nullptr)
    {
        return false;
    }
    EventGraph->Nodes.Reset();
    EventGraph->GraphGuid = FGuid(0x10000001, 0x10000002, 0x10000003, 0x10000004);

    const FGuid BeginPlayId(0x20000001, 0x20000002, 0x20000003, 0x20000004);
    const FGuid PrintStringId(0x30000001, 0x30000002, 0x30000003, 0x30000004);
    const FGuid ThenPinId(0x40000001, 0x40000002, 0x40000003, 0x40000004);
    const FGuid ExecutePinId(0x50000001, 0x50000002, 0x50000003, 0x50000004);
    const FGuid StringPinId(0x60000001, 0x60000002, 0x60000003, 0x60000004);

    UK2Node_Event* BeginPlay = AddGoldenNode<UK2Node_Event>(EventGraph, BeginPlayId);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    TestNotNull(TEXT("Resolve native PrintString function"), PrintFunction);
    if (PrintFunction == nullptr)
    {
        return false;
    }
    UK2Node_CallFunction* Print = AddGoldenNode<UK2Node_CallFunction>(EventGraph, PrintStringId);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();

    UEdGraphPin* ThenPin = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ExecutePin = Print->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* StringPin = Print->FindPin(TEXT("InString"));
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    TestNotNull(TEXT("BeginPlay Then pin"), ThenPin);
    TestNotNull(TEXT("PrintString Execute pin"), ExecutePin);
    TestNotNull(TEXT("PrintString InString pin"), StringPin);
    TestNotNull(TEXT("K2 schema"), Schema);
    if (ThenPin == nullptr || ExecutePin == nullptr || StringPin == nullptr || Schema == nullptr)
    {
        return false;
    }
    ThenPin->PinId = ThenPinId;
    ExecutePin->PinId = ExecutePinId;
    StringPin->PinId = StringPinId;
    StringPin->DefaultValue = TEXT("CJ0 golden");
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue = TEXT("(R=1.0,G=0.5,B=0.25,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("2.0");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = TEXT("cj0-key");
    TestTrue(TEXT("Connect BeginPlay to PrintString"),
        Schema->TryCreateConnection(ThenPin, ExecutePin));

    FString SnapshotText;
    TestTrue(TEXT("Serialize Snapshot v1"), FBPSnapshotExporter::Serialize(Blueprint, SnapshotText));
    const TSharedPtr<FJsonObject> Snapshot = ParseJson(SnapshotText);
    TestNotNull(TEXT("Parse Snapshot v1"), Snapshot.Get());
    if (Snapshot == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Snapshot schema remains v1"), Snapshot->GetIntegerField(TEXT("schema_version")), 1);
    TestEqual(TEXT("Snapshot asset path"), Snapshot->GetStringField(TEXT("asset_path")), Blueprint->GetPathName());
    const TArray<TSharedPtr<FJsonValue>>& SnapshotGraphs = Snapshot->GetArrayField(TEXT("graphs"));
    const TSharedPtr<FJsonObject> SnapshotGraph = FindObjectByStringField(
        SnapshotGraphs, TEXT("guid"), EventGraph->GraphGuid.ToString());
    TestNotNull(TEXT("Snapshot contains event graph"), SnapshotGraph.Get());
    if (SnapshotGraph == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Snapshot graph GUID"), SnapshotGraph->GetStringField(TEXT("guid")),
        EventGraph->GraphGuid.ToString());
    const TSharedPtr<FJsonObject> SnapshotPrint = FindObjectByStringField(
        SnapshotGraph->GetArrayField(TEXT("nodes")), TEXT("id"), PrintStringId.ToString());
    TestNotNull(TEXT("Snapshot contains PrintString node"), SnapshotPrint.Get());
    if (SnapshotPrint == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Snapshot native function path"), SnapshotPrint->GetStringField(TEXT("function_path")),
        GetPathNameSafe(PrintFunction));
    const TSharedPtr<FJsonObject> SnapshotStringPin = FindObjectByStringField(
        SnapshotPrint->GetArrayField(TEXT("pins")), TEXT("id"), StringPinId.ToString());
    TestNotNull(TEXT("Snapshot contains source pin GUID"), SnapshotStringPin.Get());
    if (SnapshotStringPin.IsValid())
    {
        TestEqual(TEXT("Snapshot preserves unconnected default"),
            SnapshotStringPin->GetStringField(TEXT("default_value")), FString(TEXT("CJ0 golden")));
    }

    FBPGraphIR GraphIR;
    TestTrue(TEXT("Build Graph IR"), FBPGraphIRBuilder::Build(Blueprint, GraphIR));
    const FBPIRGraph* IRGraph = GraphIR.Graphs.FindByPredicate(
        [EventGraph](const FBPIRGraph& Graph) { return Graph.Path == EventGraph->GetPathName(); });
    TestNotNull(TEXT("Graph IR contains event graph"), IRGraph);
    if (IRGraph == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Graph IR exec edge count"), IRGraph->ExecEdges.Num(), 1);
    if (IRGraph->ExecEdges.Num() == 1)
    {
        TestEqual(TEXT("Graph IR exec source pin"), IRGraph->ExecEdges[0].SourcePin, ThenPinId);
        TestEqual(TEXT("Graph IR exec target pin"), IRGraph->ExecEdges[0].TargetPin, ExecutePinId);
    }
    const FBPIRNode* IRPrint = IRGraph->Nodes.FindByPredicate(
        [PrintStringId](const FBPIRNode& Node) { return Node.Id == PrintStringId; });
    TestNotNull(TEXT("Graph IR contains PrintString node"), IRPrint);
    if (IRPrint != nullptr)
    {
        TestEqual(TEXT("Graph IR native owner"), IRPrint->FunctionOwnerPath,
            UKismetSystemLibrary::StaticClass()->GetPathName());
    }

    // CJ0-G01: 実際のEditor BlueprintからCopilot Graph JSON v1をファイル出力し、
    // Graph IRだけでなく保存結果でも元Node/Pin GUIDとExec接続が保持されることを確認する。
    FString CopilotPath;
    FString CopilotError;
    const bool bCopilotExported = FBPCopilotGraphJsonExporter::Export(
        GraphIR, TEXT("5.7.4-cj0-test"), CopilotPath, CopilotError);
    TestTrue(TEXT("Export Copilot Graph JSON from transient Blueprint"), bCopilotExported);
    if (bCopilotExported == false)
    {
        AddError(CopilotError);
        return false;
    }

    FString CopilotText;
    const bool bCopilotLoaded = FFileHelper::LoadFileToString(CopilotText, *CopilotPath);
    // Goldenテストの保存結果は読み込み後に削除し、Savedに不要なファイルを残さない。
    IFileManager::Get().Delete(*CopilotPath);
    TestTrue(TEXT("Read exported Copilot Graph JSON"), bCopilotLoaded);
    if (bCopilotLoaded == false)
    {
        return false;
    }
    const TSharedPtr<FJsonObject> Copilot = ParseJson(CopilotText);
    TestNotNull(TEXT("Parse exported Copilot Graph JSON"), Copilot.Get());
    if (Copilot == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Copilot schema version"), Copilot->GetIntegerField(TEXT("schema_version")), 1);
    TestEqual(TEXT("Copilot format"), Copilot->GetStringField(TEXT("format")),
        FString(TEXT("blueprint_copilot_graph")));
    TestEqual(TEXT("Copilot source asset"), Copilot->GetStringField(TEXT("asset_path")),
        Blueprint->GetPathName());
    TestEqual(TEXT("Copilot completeness is not overstated"),
        Copilot->GetStringField(TEXT("semantic_status")), FString(TEXT("partial")));
    const TSharedPtr<FJsonObject> CopilotGraph = FindObjectByStringField(
        Copilot->GetArrayField(TEXT("graphs")), TEXT("path"), EventGraph->GetPathName());
    TestNotNull(TEXT("Copilot contains source event graph"), CopilotGraph.Get());
    if (CopilotGraph == nullptr)
    {
        return false;
    }
    const TSharedPtr<FJsonObject> CopilotPrint = FindObjectByStringField(
        CopilotGraph->GetArrayField(TEXT("nodes")), TEXT("id"),
        PrintStringId.ToString(EGuidFormats::DigitsWithHyphens));
    TestNotNull(TEXT("Copilot contains original PrintString GUID"), CopilotPrint.Get());
    if (CopilotPrint == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Copilot native function path"),
        CopilotPrint->GetStringField(TEXT("function_path")), GetPathNameSafe(PrintFunction));
    TestEqual(TEXT("Copilot native function owner"),
        CopilotPrint->GetStringField(TEXT("function_owner_path")),
        UKismetSystemLibrary::StaticClass()->GetPathName());
    TestTrue(TEXT("Copilot native function resolved"),
        CopilotPrint->GetBoolField(TEXT("function_resolved")));
    const TSharedPtr<FJsonObject> CopilotStringPin = FindObjectByStringField(
        CopilotPrint->GetArrayField(TEXT("pins")), TEXT("id"),
        StringPinId.ToString(EGuidFormats::DigitsWithHyphens));
    TestNotNull(TEXT("Copilot preserves source string pin GUID"), CopilotStringPin.Get());
    if (CopilotStringPin.IsValid())
    {
        TestEqual(TEXT("Copilot preserves unconnected literal"),
            CopilotStringPin->GetStringField(TEXT("default_value")), FString(TEXT("CJ0 golden")));
    }
    const TArray<TSharedPtr<FJsonValue>>& CopilotExecEdges =
        CopilotGraph->GetArrayField(TEXT("exec_edges"));
    TestEqual(TEXT("Copilot exec edge count"), CopilotExecEdges.Num(), 1);
    if (CopilotExecEdges.Num() == 1)
    {
        const TSharedPtr<FJsonObject> Edge = CopilotExecEdges[0]->AsObject();
        TestEqual(TEXT("Copilot exec source node"),
            Edge->GetStringField(TEXT("source_node_id")),
            BeginPlayId.ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Copilot exec source pin"),
            Edge->GetStringField(TEXT("source_pin_id")),
            ThenPinId.ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Copilot exec target node"),
            Edge->GetStringField(TEXT("target_node_id")),
            PrintStringId.ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Copilot exec target pin"),
            Edge->GetStringField(TEXT("target_pin_id")),
            ExecutePinId.ToString(EGuidFormats::DigitsWithHyphens));
    }
    TestEqual(TEXT("Copilot has no data edge for literal input"),
        CopilotGraph->GetArrayField(TEXT("data_edges")).Num(), 0);

    FBPSemanticIR SemanticIR;
    TestTrue(TEXT("Lower supported native call"),
        FBPSemanticLowering::LowerPrintString(Blueprint, GraphIR, SemanticIR));
    FBPSemanticGraphTraceExtractor::Append(GraphIR, SemanticIR);
    FString SemanticText;
    TestTrue(TEXT("Serialize Semantic JSON v2"),
        FBPSemanticJsonSerializer::Serialize(SemanticIR, TEXT("5.7.4-cj0-test"), SemanticText));
    const TSharedPtr<FJsonObject> Semantic = ParseJson(SemanticText);
    TestNotNull(TEXT("Parse Semantic JSON v2"), Semantic.Get());
    if (Semantic == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("Semantic schema remains v2"), Semantic->GetIntegerField(TEXT("schema_version")), 2);
    TestEqual(TEXT("Supported subset is still reported partial"),
        Semantic->GetStringField(TEXT("semantic_status")), FString(TEXT("partial")));
    const TArray<TSharedPtr<FJsonValue>>& Functions = Semantic->GetArrayField(TEXT("functions"));
    TestEqual(TEXT("One lowered function"), Functions.Num(), 1);
    if (Functions.Num() != 1)
    {
        return false;
    }
    const TSharedPtr<FJsonObject> CallExpression = Functions[0]->AsObject()
        ->GetArrayField(TEXT("blocks"))[0]->AsObject()
        ->GetArrayField(TEXT("statements"))[0]->AsObject()
        ->GetObjectField(TEXT("expression"));
    TestEqual(TEXT("Semantic native call name"),
        CallExpression->GetStringField(TEXT("function_name")), FString(TEXT("PrintString")));
    TestEqual(TEXT("Semantic native call owner"),
        CallExpression->GetStringField(TEXT("function_owner_path")),
        FString(TEXT("/Script/Engine.KismetSystemLibrary")));
    const TArray<TSharedPtr<FJsonValue>>& Arguments = CallExpression->GetArrayField(TEXT("arguments"));
    TestTrue(TEXT("Semantic call contains string argument"), Arguments.Num() >= 2);
    if (Arguments.Num() >= 2)
    {
        TestEqual(TEXT("Semantic literal value"),
            Arguments[1]->AsObject()->GetStringField(TEXT("literal_value")), FString(TEXT("CJ0 golden")));
        TestEqual(TEXT("Semantic literal source pin"),
            Arguments[1]->AsObject()->GetStringField(TEXT("source_pin_id")),
            StringPinId.ToString(EGuidFormats::DigitsWithHyphens));
    }
    const TArray<TSharedPtr<FJsonValue>>& Traces = Semantic->GetArrayField(TEXT("graph_traces"));
    const TSharedPtr<FJsonObject> EventTrace = FindObjectByStringField(
        Traces, TEXT("path"), EventGraph->GetPathName());
    TestNotNull(TEXT("Semantic traces contain event graph"), EventTrace.Get());
    if (EventTrace.IsValid())
    {
        TestEqual(TEXT("Trace exec edge count"),
            EventTrace->GetArrayField(TEXT("exec_edges")).Num(), 1);
    }

    FBPSemanticDiagnostic Unsupported;
    Unsupported.NodeId = PrintStringId;
    Unsupported.Message = TEXT("CJ0 unsupported sentinel");
    SemanticIR.Diagnostics.Add(MoveTemp(Unsupported));
    const TSharedRef<FJsonObject> UnsupportedJson =
        FBPSemanticJsonSerializer::ToJson(SemanticIR, TEXT("5.7.4-cj0-test"));
    TestEqual(TEXT("Unresolved input cannot be reported as successful"),
        UnsupportedJson->GetStringField(TEXT("semantic_status")), FString(TEXT("unsupported")));
    TestEqual(TEXT("Unsupported diagnostic retains source node"),
        UnsupportedJson->GetArrayField(TEXT("diagnostics"))[0]->AsObject()
            ->GetStringField(TEXT("node_id")),
        PrintStringId.ToString(EGuidFormats::DigitsWithHyphens));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCopilotJsonVoidFunctionGoldenTest,
    "BlueprintAIConverter.CopilotJson.CJ0.VoidFunctionGolden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPCopilotJsonVoidFunctionGoldenTest::RunTest(const FString& Parameters)
{
    // CJ0-G02: Blueprintの自作関数をEditor APIで作り、反射署名と生グラフを別々に検証する。
    // 関数本体のC++変換成功をこのGoldenテストだけで保証したことにはしない。
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CJ0_VoidFunction"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName, BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Create CJ0-G02 Blueprint"), Blueprint) == false)
    {
        return false;
    }

    UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, TEXT("RunGoldenFunction"), UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (TestNotNull(TEXT("Create CJ0-G02 function graph"), FunctionGraph) == false)
    {
        return false;
    }
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, FunctionGraph, true, nullptr);

    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
    {
        if (Entry == nullptr)
        {
            Entry = Cast<UK2Node_FunctionEntry>(Node);
        }
        if (Result == nullptr)
        {
            Result = Cast<UK2Node_FunctionResult>(Node);
        }
    }
    if (TestNotNull(TEXT("Find function entry"), Entry) == false)
    {
        return false;
    }

    // 固定GUIDを付けることで、Export後に元Blueprintの識別子と一致することを確認できる。
    const FGuid EntryId(0x71000001, 0x71000002, 0x71000003, 0x71000004);
    const FGuid PrintId(0x72000001, 0x72000002, 0x72000003, 0x72000004);
    const FGuid EntryThenId(0x73000001, 0x73000002, 0x73000003, 0x73000004);
    const FGuid PrintExecuteId(0x74000001, 0x74000002, 0x74000003, 0x74000004);
    Entry->NodeGuid = EntryId;

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("Resolve PrintString"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* Print = AddGoldenNode<UK2Node_CallFunction>(FunctionGraph, PrintId);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();
    UEdGraphPin* EntryThen = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* PrintExecute = Print->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* PrintText = Print->FindPin(TEXT("InString"));
    if (TestNotNull(TEXT("Function entry exec pin"), EntryThen) == false
        || TestNotNull(TEXT("PrintString exec pin"), PrintExecute) == false
        || TestNotNull(TEXT("PrintString text pin"), PrintText) == false)
    {
        return false;
    }
    EntryThen->PinId = EntryThenId;
    PrintExecute->PinId = PrintExecuteId;
    PrintText->DefaultValue = TEXT("CJ0 G02 function");
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (TestTrue(TEXT("Connect function entry to PrintString"),
        Schema->TryCreateConnection(EntryThen, PrintExecute)) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("Compile Blueprint generated class"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    const UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(TEXT("RunGoldenFunction"));
    if (TestNotNull(TEXT("Reflect declared Blueprint function"), Function) == false)
    {
        return false;
    }
    TestEqual(TEXT("Function is declared on generated class"),
        Function->GetOuterUClass(), Blueprint->GeneratedClass.Get());
    TestEqual(TEXT("Function has no input or return parameters"), Function->NumParms, 0);

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("Build CJ0-G02 Graph IR"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    const FBPIRGraph* IRGraph = GraphIR.Graphs.FindByPredicate(
        [FunctionGraph](const FBPIRGraph& Graph) { return Graph.Path == FunctionGraph->GetPathName(); });
    if (TestNotNull(TEXT("Find CJ0-G02 function graph IR"), IRGraph) == false)
    {
        return false;
    }
    const FBPIRNode* IREntry = IRGraph->Nodes.FindByPredicate(
        [EntryId](const FBPIRNode& Node) { return Node.Id == EntryId; });
    TestNotNull(TEXT("Function entry retains node GUID"), IREntry);
    TestEqual(TEXT("Function graph exec edge count"), IRGraph->ExecEdges.Num(), 1);

    FBPSemanticIR SemanticIR;
    SemanticIR.AssetPath = GraphIR.AssetPath;
    SemanticIR.ParentClassPath = GraphIR.ParentClassPath;
    FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
    const FBPSemanticFunction* Declaration = SemanticIR.Functions.FindByPredicate(
        [](const FBPSemanticFunction& Candidate)
        {
            return Candidate.Name == TEXT("RunGoldenFunction");
        });
    if (TestNotNull(TEXT("Extract Blueprint function declaration"), Declaration) == false)
    {
        return false;
    }
    TestEqual(TEXT("Function signature from reflection"), Declaration->SignatureSource,
        FString(TEXT("generated_class_reflection")));
    TestEqual(TEXT("Function owner path"), Declaration->OwnerClassPath,
        Blueprint->GeneratedClass->GetPathName());
    TestEqual(TEXT("Function has no declared arguments"), Declaration->Parameters.Num(), 0);
    TestEqual(TEXT("Function returns void"), Declaration->ReturnType.Kind,
        EBPSemanticTypeKind::Void);

    FString ExportPath;
    FString ExportError;
    if (TestTrue(TEXT("Export CJ0-G02 Copilot Graph JSON"),
        FBPCopilotGraphJsonExporter::Export(
            GraphIR, TEXT("5.7.4-cj0-test"), ExportPath, ExportError, &SemanticIR)) == false)
    {
        AddError(ExportError);
        return false;
    }
    FString JsonText;
    const bool bLoaded = FFileHelper::LoadFileToString(JsonText, *ExportPath);
    // Golden用の一時JSONは残さず、保存と再読込だけを検証する。
    IFileManager::Get().Delete(*ExportPath);
    if (TestTrue(TEXT("Read CJ0-G02 JSON"), bLoaded) == false)
    {
        return false;
    }
    const TSharedPtr<FJsonObject> Json = ParseJson(JsonText);
    if (TestNotNull(TEXT("Parse CJ0-G02 JSON"), Json.Get()) == false)
    {
        return false;
    }
    TestEqual(TEXT("CJ0-G02 remains partial"), Json->GetStringField(TEXT("semantic_status")),
        FString(TEXT("partial")));
    const TSharedPtr<FJsonObject> ExportedGraph = FindObjectByStringField(
        Json->GetArrayField(TEXT("graphs")), TEXT("path"), FunctionGraph->GetPathName());
    if (TestNotNull(TEXT("Exported function graph"), ExportedGraph.Get()) == false)
    {
        return false;
    }
    const TSharedPtr<FJsonObject> ExportedEntry = FindObjectByStringField(
        ExportedGraph->GetArrayField(TEXT("nodes")), TEXT("id"),
        EntryId.ToString(EGuidFormats::DigitsWithHyphens));
    TestNotNull(TEXT("Exported function entry GUID"), ExportedEntry.Get());
    const TArray<TSharedPtr<FJsonValue>>& Edges = ExportedGraph->GetArrayField(TEXT("exec_edges"));
    TestEqual(TEXT("Exported function exec edge count"), Edges.Num(), 1);
    if (Edges.Num() == 1)
    {
        const TSharedPtr<FJsonObject> Edge = Edges[0]->AsObject();
        TestEqual(TEXT("Function entry exec pin GUID"), Edge->GetStringField(TEXT("source_pin_id")),
            EntryThenId.ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Function call exec pin GUID"), Edge->GetStringField(TEXT("target_pin_id")),
            PrintExecuteId.ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Function call node GUID"), Edge->GetStringField(TEXT("target_node_id")),
            PrintId.ToString(EGuidFormats::DigitsWithHyphens));
    }
    const TSharedPtr<FJsonObject> Declarations = Json->GetObjectField(TEXT("semantic_declarations"));
    const TSharedPtr<FJsonObject> ExportedFunction = FindObjectByStringField(
        Declarations->GetArrayField(TEXT("functions")), TEXT("name"), TEXT("RunGoldenFunction"));
    TestNotNull(TEXT("Exported Blueprint function declaration"), ExportedFunction.Get());
    return true;
}

#endif
