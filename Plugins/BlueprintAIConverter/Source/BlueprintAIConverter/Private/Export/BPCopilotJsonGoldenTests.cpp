#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticLowering.h"
#include "BPSnapshotExporter.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
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

#endif
