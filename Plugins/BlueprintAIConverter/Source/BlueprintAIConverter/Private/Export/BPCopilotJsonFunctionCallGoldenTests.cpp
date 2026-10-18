#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPCopilotGraphJsonExporter.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCopilotJsonLocalFunctionCallGoldenTest,
    "BlueprintAIConverter.CopilotJson.CJ0.LocalFunctionCallGolden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPCopilotJsonLocalFunctionCallGoldenTest::RunTest(const FString& Parameters)
{
    // CJ0-G04: 同一Blueprint内に宣言した関数を別の関数から呼び出す。
    // Native関数へのCallとは異なり、生成クラス上の自作関数参照を追跡できるかを確認する。
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CJ0_LocalCall"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName, BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Create local call Blueprint"), Blueprint) == false)
    {
        return false;
    }

    UEdGraph* CalleeGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, TEXT("GoldenCallee"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    UEdGraph* CallerGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, TEXT("GoldenCaller"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (TestNotNull(TEXT("Create callee graph"), CalleeGraph) == false
        || TestNotNull(TEXT("Create caller graph"), CallerGraph) == false)
    {
        return false;
    }
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, CalleeGraph, true, nullptr);
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, CallerGraph, true, nullptr);

    UK2Node_FunctionEntry* CallerEntry = nullptr;
    for (UEdGraphNode* Node : CallerGraph->Nodes)
    {
        if (CallerEntry == nullptr)
        {
            CallerEntry = Cast<UK2Node_FunctionEntry>(Node);
        }
    }
    if (TestNotNull(TEXT("Find caller entry"), CallerEntry) == false)
    {
        return false;
    }

    // 先にコンパイルしてGeneratedClassの自作UFunctionを確定させる。
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("Generated class"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    UFunction* CalleeFunction = Blueprint->GeneratedClass->FindFunctionByName(TEXT("GoldenCallee"));
    if (TestNotNull(TEXT("Resolve local Blueprint function"), CalleeFunction) == false)
    {
        return false;
    }
    const FGuid CallId(0x91000001, 0x91000002, 0x91000003, 0x91000004);
    UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(CallerGraph);
    CallerGraph->AddNode(Call, false, false);
    Call->PostPlacedNewNode();
    Call->SetFromFunction(CalleeFunction);
    Call->NodeGuid = CallId;
    Call->AllocateDefaultPins();

    UEdGraphPin* SourceExec = CallerEntry->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* TargetExec = Call->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (TestNotNull(TEXT("Caller entry exec"), SourceExec) == false
        || TestNotNull(TEXT("Local call exec"), TargetExec) == false)
    {
        return false;
    }
    const FGuid SourcePinId(0x92000001, 0x92000002, 0x92000003, 0x92000004);
    const FGuid TargetPinId(0x93000001, 0x93000002, 0x93000003, 0x93000004);
    SourceExec->PinId = SourcePinId;
    TargetExec->PinId = TargetPinId;
    if (TestTrue(TEXT("Connect caller to local function"),
        GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(SourceExec, TargetExec)) == false)
    {
        return false;
    }

    FBPGraphIR IR;
    if (TestTrue(TEXT("Build local call Graph IR"), FBPGraphIRBuilder::Build(Blueprint, IR)) == false)
    {
        return false;
    }
    const FBPIRGraph* CallerIR = IR.Graphs.FindByPredicate(
        [CallerGraph](const FBPIRGraph& Graph) { return Graph.Path == CallerGraph->GetPathName(); });
    if (TestNotNull(TEXT("Find caller Graph IR"), CallerIR) == false)
    {
        return false;
    }
    const FBPIRNode* CallIR = CallerIR->Nodes.FindByPredicate(
        [CallId](const FBPIRNode& Node) { return Node.Id == CallId; });
    if (TestNotNull(TEXT("Find local function call node"), CallIR) == false)
    {
        return false;
    }
    TestEqual(TEXT("Local function name"), CallIR->FunctionName, FName(TEXT("GoldenCallee")));
    TestEqual(TEXT("Local function path"), CallIR->FunctionPath, CalleeFunction->GetPathName());
    TestEqual(TEXT("Local function owner"), CallIR->FunctionOwnerPath,
        Blueprint->GeneratedClass->GetPathName());
    TestEqual(TEXT("Caller exec edge count"), CallerIR->ExecEdges.Num(), 1);

    FString Path;
    FString Error;
    if (TestTrue(TEXT("Export local function Copilot JSON"),
        FBPCopilotGraphJsonExporter::Export(IR, TEXT("5.7.4-cj0-test"), Path, Error)) == false)
    {
        AddError(Error);
        return false;
    }
    FString Text;
    const bool bLoaded = FFileHelper::LoadFileToString(Text, *Path);
    // Goldenテストの一時成果物は再読込後に削除する。
    IFileManager::Get().Delete(*Path);
    if (TestTrue(TEXT("Reload local call JSON"), bLoaded) == false)
    {
        return false;
    }
    TSharedPtr<FJsonObject> Json;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    if (TestTrue(TEXT("Parse local call JSON"),
        FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid()) == false)
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>& Graphs = Json->GetArrayField(TEXT("graphs"));
    TSharedPtr<FJsonObject> ExportedCaller;
    for (const TSharedPtr<FJsonValue>& Value : Graphs)
    {
        const TSharedPtr<FJsonObject> Graph = Value.IsValid() ? Value->AsObject() : nullptr;
        if (Graph.IsValid() && Graph->GetStringField(TEXT("path")) == CallerGraph->GetPathName())
        {
            ExportedCaller = Graph;
            break;
        }
    }
    if (TestNotNull(TEXT("Exported caller graph"), ExportedCaller.Get()) == false)
    {
        return false;
    }
    TSharedPtr<FJsonObject> ExportedCall;
    for (const TSharedPtr<FJsonValue>& Value : ExportedCaller->GetArrayField(TEXT("nodes")))
    {
        const TSharedPtr<FJsonObject> Node = Value.IsValid() ? Value->AsObject() : nullptr;
        if (Node.IsValid() && Node->GetStringField(TEXT("id")) ==
            CallId.ToString(EGuidFormats::DigitsWithHyphens))
        {
            ExportedCall = Node;
            break;
        }
    }
    if (TestNotNull(TEXT("Exported local call node"), ExportedCall.Get()) == false)
    {
        return false;
    }
    TestEqual(TEXT("Exported local function path"),
        ExportedCall->GetStringField(TEXT("function_path")), CalleeFunction->GetPathName());
    TestEqual(TEXT("Exported local function owner"),
        ExportedCall->GetStringField(TEXT("function_owner_path")),
        Blueprint->GeneratedClass->GetPathName());
    TestTrue(TEXT("Exported local function resolved"),
        ExportedCall->GetBoolField(TEXT("function_resolved")));
    const TArray<TSharedPtr<FJsonValue>>& Edges = ExportedCaller->GetArrayField(TEXT("exec_edges"));
    TestEqual(TEXT("Exported caller edge count"), Edges.Num(), 1);
    if (Edges.Num() == 1)
    {
        const TSharedPtr<FJsonObject> Edge = Edges[0]->AsObject();
        TestEqual(TEXT("Exported caller source pin"), Edge->GetStringField(TEXT("source_pin_id")),
            SourcePinId.ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Exported caller target pin"), Edge->GetStringField(TEXT("target_pin_id")),
            TargetPinId.ToString(EGuidFormats::DigitsWithHyphens));
    }
    TestEqual(TEXT("Copilot semantic completeness remains partial"),
        Json->GetStringField(TEXT("semantic_status")), FString(TEXT("partial")));
    return true;
}

#endif
