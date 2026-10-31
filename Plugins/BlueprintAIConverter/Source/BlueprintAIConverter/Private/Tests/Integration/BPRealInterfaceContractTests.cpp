#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../BPSemanticRoundtripTestUtils.h"
#include "BPInterfaceContractTestTypes.h"
#include "BPSemanticLowering.h"
#include "BPSemanticCodeGenerator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
template <typename T>
T* AddInterfaceFixtureNode(UEdGraph* Graph)
{
    T* Node = NewObject<T>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealInterfaceContractTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.InterfaceContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealInterfaceContractTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_InterfaceContract")),
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Interface実装Blueprint"), Blueprint) == false) { return false; }
    if (TestTrue(TEXT("新規native Interfaceを実装"), FBlueprintEditorUtils::ImplementNewInterface(
            Blueprint, UBPNotifyContract::StaticClass()->GetClassPathName())) == false) { return false; }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("Interface EventGraph"), EventGraph) == false) { return false; }
    EventGraph->Nodes.Reset();
    UK2Node_Event* Entry = AddInterfaceFixtureNode<UK2Node_Event>(EventGraph);
    Entry->EventReference.SetExternalMember(TEXT("NotifyValue"), UBPNotifyContract::StaticClass());
    Entry->bOverrideFunction = true;
    Entry->AllocateDefaultPins();
    UFunction* Native = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetSubtitlesEnabled"));
    if (TestNotNull(TEXT("共通Callの署名"), Native) == false) { return false; }
    const auto AddCall = [Native](UEdGraph* Graph)
    {
        UK2Node_CallFunction* Call = AddInterfaceFixtureNode<UK2Node_CallFunction>(Graph);
        Call->SetFromFunction(Native);
        Call->AllocateDefaultPins();
        Call->FindPinChecked(TEXT("bEnabled"))->DefaultValue = TEXT("true");
        return Call;
    };
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    UK2Node_CallFunction* EventCall = AddCall(EventGraph);
    if (TestTrue(TEXT("Interface本文のExec接続"), Schema->TryCreateConnection(
        Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        EventCall->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false) { return false; }

    const FName FunctionName(TEXT("RunOrdinary"));
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FunctionName,
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
    UK2Node_FunctionEntry* FunctionEntry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node)) { FunctionEntry = Candidate; }
        if (UK2Node_FunctionResult* Candidate = Cast<UK2Node_FunctionResult>(Node)) { Result = Candidate; }
    }
    if (TestNotNull(TEXT("通常関数Entry"), FunctionEntry) == false) { return false; }
    if (Result == nullptr)
    {
        Result = AddInterfaceFixtureNode<UK2Node_FunctionResult>(Graph);
        Result->FunctionReference.SetSelfMember(FunctionName);
        Result->AllocateDefaultPins();
    }
    FunctionEntry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    UK2Node_CallFunction* OrdinaryCall = AddCall(Graph);
    if (TestTrue(TEXT("通常関数の本文"), Schema->TryCreateConnection(
            FunctionEntry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            OrdinaryCall->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false ||
        TestTrue(TEXT("通常関数Result"), Schema->TryCreateConnection(
            OrdinaryCall->FindPinChecked(UEdGraphSchema_K2::PN_Then),
            Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false) { return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("Interface Blueprintのコンパイル"), Blueprint->Status != BS_Error) == false) { return false; }
    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("Interfaceの実Graph IR"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false) { return false; }
    FBPSemanticIR IR;
    if (TestTrue(TEXT("Interface実装と通常関数を一括Lowering"), FBPSemanticLowering::Lower(Blueprint, GraphIR, IR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics) { AddError(Diagnostic.Message); }
        return false;
    }
    TestEqual(TEXT("2つの関数を保持"), IR.Functions.Num(), 2);
    TestTrue(TEXT("通常LoweringでInterface継承情報を保持"),
        IR.ClassInfo.InterfacePaths.Contains(UBPNotifyContract::StaticClass()->GetPathName()));
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Interfaceと通常関数を一つのUCLASSへ生成"), FBPSemanticCodeGenerator::Generate(
            IR, TEXT("UInterfaceContractObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics) { AddError(Message); }
        return false;
    }
    TestTrue(TEXT("新規Interfaceを継承"), Files.HeaderContent.Contains(TEXT(", public IBPNotifyContract")));
    TestTrue(TEXT("Reflection由来のInterface実装宣言"), Files.HeaderContent.Contains(
        TEXT("NotifyValue_Implementation(int32 Value) override;")));
    TestTrue(TEXT("通常関数も同じクラスで定義"), Files.SourceContent.Contains(TEXT("UInterfaceContractObject::RunOrdinary(")));
    TestFalse(TEXT("新規InterfaceのSuper呼び出しなし"), Files.SourceContent.Contains(TEXT("Super::NotifyValue")));
    return BPSemanticTestUtils::ExportRoundtrip(*this, Blueprint, GraphIR, IR);
}
#endif
