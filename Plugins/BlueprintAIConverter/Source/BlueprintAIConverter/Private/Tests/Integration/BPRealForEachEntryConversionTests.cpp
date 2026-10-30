#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "BPSemanticJsonExporter.h"
#include "BPSemanticJsonSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
template <typename T>
T* AddNode(UEdGraph* Graph)
{
    T* Node = NewObject<T>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

bool RunForEachEntryTest(FAutomationTestBase& Test, bool bFunction, bool bWithBreak)
{
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_ForEachEntry")),
        BPTYPE_Normal, NAME_None);
    if (Test.TestNotNull(TEXT("UObject Blueprint"), Blueprint) == false) { return false; }
    FEdGraphPinType ArrayType;
    ArrayType.PinCategory = UEdGraphSchema_K2::PC_Int;
    ArrayType.ContainerType = EPinContainerType::Array;
    if (Test.TestTrue(TEXT("int32配列を追加"), FBlueprintEditorUtils::AddMemberVariable(
            Blueprint, TEXT("Numbers"), ArrayType, TEXT("(10,20,30)"))) == false) { return false; }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UEdGraph* Graph = nullptr;
    UK2Node* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    if (bFunction)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("RunForEach"),
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node)) { Entry = Candidate; }
            if (UK2Node_FunctionResult* Candidate = Cast<UK2Node_FunctionResult>(Node)) { Result = Candidate; }
        }
        if (Result == nullptr)
        {
            Result = AddNode<UK2Node_FunctionResult>(Graph);
            Result->FunctionReference.SetSelfMember(TEXT("RunForEach"));
            Result->AllocateDefaultPins();
        }
    }
    else
    {
        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        if (Test.TestNotNull(TEXT("Event Graph"), Graph) == false) { return false; }
        Graph->Nodes.Reset();
        UK2Node_CustomEvent* Event = AddNode<UK2Node_CustomEvent>(Graph);
        Event->CustomFunctionName = TEXT("OnForEach");
        Event->AllocateDefaultPins();
        Entry = Event;
    }
    if (Test.TestNotNull(TEXT("Entry"), Entry) == false) { return false; }
    Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    const FString MacroPath = FString::Printf(
        TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:%s"),
        bWithBreak ? TEXT("ForEachLoopWithBreak") : TEXT("ForEachLoop"));
    UEdGraph* Macro = LoadObject<UEdGraph>(nullptr, *MacroPath);
    if (Test.TestNotNull(TEXT("標準ForEachマクロ"), Macro) == false) { return false; }
    UK2Node_MacroInstance* Loop = AddNode<UK2Node_MacroInstance>(Graph);
    Loop->SetMacroGraph(Macro);
    Loop->AllocateDefaultPins();
    UK2Node_VariableGet* Getter = AddNode<UK2Node_VariableGet>(Graph);
    Getter->VariableReference.SetSelfMember(TEXT("Numbers"));
    Getter->AllocateDefaultPins();
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
    UFunction* ConvertFunction = UKismetStringLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_IntToString"));
    if (Test.TestNotNull(TEXT("PrintString署名"), PrintFunction) == false ||
        Test.TestNotNull(TEXT("int32変換署名"), ConvertFunction) == false) { return false; }
    const auto Print = [Graph, PrintFunction](const TCHAR* Value)
    {
        UK2Node_CallFunction* Node = AddNode<UK2Node_CallFunction>(Graph);
        Node->SetFromFunction(PrintFunction);
        Node->AllocateDefaultPins();
        Node->FindPinChecked(TEXT("InString"))->DefaultValue = Value;
        return Node;
    };
    UK2Node_CallFunction* Before = Print(TEXT("Before ForEach"));
    UK2Node_CallFunction* Body = Print(TEXT(""));
    UK2Node_CallFunction* Done = Print(TEXT("After ForEach"));
    UK2Node_CallFunction* Convert = AddNode<UK2Node_CallFunction>(Graph);
    Convert->SetFromFunction(ConvertFunction);
    Convert->AllocateDefaultPins();
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    const auto Connect = [Schema](UK2Node* From, FName Output, UK2Node* To, FName Input)
    {
        return Schema->TryCreateConnection(From->FindPinChecked(Output), To->FindPinChecked(Input));
    };
    bool bConnected = Connect(Entry, UEdGraphSchema_K2::PN_Then, Before, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(Before, UEdGraphSchema_K2::PN_Then, Loop, TEXT("Exec"));
    bConnected &= Connect(Getter, TEXT("Numbers"), Loop, TEXT("Array"));
    bConnected &= Connect(Loop, TEXT("LoopBody"), Body, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(Loop, TEXT("Completed"), Done, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(Loop, bFunction ? FName(TEXT("Array Element")) : FName(TEXT("Array Index")), Convert, TEXT("InInt"));
    bConnected &= Connect(Convert, TEXT("ReturnValue"), Body, TEXT("InString"));
    if (bWithBreak) { bConnected &= Connect(Body, UEdGraphSchema_K2::PN_Then, Loop, TEXT("Break")); }
    if (Result != nullptr) { bConnected &= Connect(Done, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute); }
    if (Test.TestTrue(TEXT("配列型と実Exec接続を構築"), bConnected) == false) { return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Test.TestTrue(TEXT("Blueprintコンパイル成功"), Blueprint->Status != BS_Error) == false) { return false; }
    FBPGraphIR GraphIR;
    if (Test.TestTrue(TEXT("実Graph IRを生成"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false) { return false; }
    FBPSemanticIR IR;
    if (Test.TestTrue(TEXT("共通EntryでForEachをLowering"), FBPSemanticLowering::Lower(Blueprint, GraphIR, IR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics) { Test.AddError(Diagnostic.Message); }
        return false;
    }
    if (Test.TestEqual(TEXT("関数数"), IR.Functions.Num(), 1) == false) { return false; }
    Test.TestEqual(TEXT("関数名"), IR.Functions[0].Name, bFunction ? FName(TEXT("RunForEach")) : FName(TEXT("OnForEach")));
    Test.TestEqual(TEXT("Entry種別"), IR.Functions[0].bIsEventGraph, bFunction == false);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (Test.TestTrue(TEXT("UObject派生C++へ生成"), FBPSemanticCodeGenerator::Generate(IR, TEXT("UForEachEntry"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { Test.AddError(Diagnostic); }
        return false;
    }
    const int32 BeforePosition = Files.SourceContent.Find(TEXT("Before ForEach"));
    const int32 LoopPosition = Files.SourceContent.Find(TEXT("for ("));
    const int32 DonePosition = Files.SourceContent.Find(TEXT("After ForEach"));
    Test.TestTrue(TEXT("Entry→Loop→Completedの順序"), BeforePosition != INDEX_NONE && LoopPosition > BeforePosition && DonePosition > LoopPosition);
    Test.TestTrue(TEXT("ループローカルを保持"), Files.SourceContent.Contains(bFunction
        ? TEXT("Conv_IntToString(ForEachArrayElement)") : TEXT("Conv_IntToString(ForEachArrayIndex)")));
    Test.TestEqual(TEXT("Breakの有無"), Files.SourceContent.Contains(TEXT("break;")), bWithBreak);
    Test.TestFalse(TEXT("Actor lifecycleを混入しない"), Files.SourceContent.Contains(TEXT("Super::BeginPlay")));

    for (int32 CaseIndex = 0; CaseIndex < (bFunction ? 8 : 4); ++CaseIndex)
    {
        FBPGraphIR Invalid = GraphIR;
        FBPIRGraph* Target = Invalid.Graphs.FindByPredicate([Graph](const FBPIRGraph& Candidate)
            { return Candidate.Name == Graph->GetName(); });
        if (Test.TestNotNull(TEXT("拒否確認用Graph"), Target) == false) { return false; }
        if (CaseIndex == 0)
        {
            for (FBPIRNode& Node : Target->Nodes) { if (Node.Id == Loop->NodeGuid) { Node.MacroGraphPath += TEXT("Missing"); } }
        }
        if (CaseIndex == 1) { Invalid.ParentClassPath = TEXT("/Script/Engine.Actor"); }
        if (CaseIndex == 2)
        {
            for (FBPIRLink& Edge : Target->DataEdges)
            {
                if (Edge.SourceNode == Convert->NodeGuid)
                {
                    Edge.TargetNode = Done->NodeGuid; Edge.TargetPin = Done->FindPinChecked(TEXT("InString"))->PinId;
                }
            }
        }
        if (CaseIndex == 3)
        {
            FBPIRLink Extra;
            Extra.SourceNode = FGuid::NewGuid(); Extra.SourcePin = FGuid::NewGuid();
            Extra.TargetNode = Body->NodeGuid; Extra.TargetPin = Body->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId;
            Target->ExecEdges.Add(Extra);
        }
        if (CaseIndex == 4)
        {
            Target->ExecEdges.RemoveAll([Done](const FBPIRLink& Edge) { return Edge.SourceNode == Done->NodeGuid; });
        }
        if (CaseIndex == 5)
        {
            Target->ExecEdges.RemoveAll([Body](const FBPIRLink& Edge) { return Edge.SourceNode == Body->NodeGuid; });
            FBPIRLink Early;
            Early.SourceNode = Body->NodeGuid; Early.SourcePin = Body->FindPinChecked(UEdGraphSchema_K2::PN_Then)->PinId;
            Early.TargetNode = Result->NodeGuid; Early.TargetPin = Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId;
            Target->ExecEdges.Add(Early);
        }
        if (CaseIndex == 6)
        {
            const FBPIRNode* Original = Target->Nodes.FindByPredicate([Result](const FBPIRNode& Node) { return Node.Id == Result->NodeGuid; });
            if (Test.TestNotNull(TEXT("末尾Result"), Original) == false) { return false; }
            FBPIRNode Other = *Original;
            Other.Id = FGuid::NewGuid();
            FGuid ExecuteId;
            for (FBPIRPin& Pin : Other.Pins)
            {
                Pin.Id = FGuid::NewGuid();
                if (Pin.Name == UEdGraphSchema_K2::PN_Execute) { ExecuteId = Pin.Id; }
            }
            FBPIRLink Extra;
            Extra.SourceNode = Done->NodeGuid; Extra.SourcePin = Done->FindPinChecked(UEdGraphSchema_K2::PN_Then)->PinId;
            Extra.TargetNode = Other.Id; Extra.TargetPin = ExecuteId;
            Target->Nodes.Add(MoveTemp(Other)); Target->ExecEdges.Add(Extra);
        }
        if (CaseIndex == 7)
        {
            FBPIRLink Extra;
            Extra.SourceNode = Result->NodeGuid; Extra.SourcePin = FGuid::NewGuid();
            Extra.TargetNode = Loop->NodeGuid; Extra.TargetPin = Loop->FindPinChecked(TEXT("Exec"))->PinId;
            Target->ExecEdges.Add(Extra);
        }
        FBPSemanticIR Rejected;
        Test.TestFalse(TEXT("不正なForEach Entry／スコープ／Resultを拒否"), FBPSemanticLowering::Lower(Blueprint, Invalid, Rejected));
        Test.TestTrue(TEXT("拒否時に部分関数を公開しない"), Rejected.Functions.IsEmpty() && Rejected.HasErrors());
    }
    FString Json;
    Test.TestTrue(TEXT("ForEachのJSONを生成"), FBPSemanticJsonSerializer::Serialize(IR, TEXT("5.7.4"), Json));
    Test.TestTrue(TEXT("JSONにForEachと型付き配列を保持"), Json.Contains(TEXT("for_each_loop")) && Json.Contains(TEXT("loop_array")));
    FString ExportPath;
    FString ExportError;
    if (Test.TestTrue(TEXT("実Blueprint由来JSONを保存"), FBPSemanticJsonExporter::Export(IR, TEXT("5.7.4"), ExportPath, ExportError)) == false)
    {
        Test.AddError(ExportError);
        return false;
    }
    Test.AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealFunctionForEachTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionForEach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealFunctionForEachTest::RunTest(const FString& Parameters) { return RunForEachEntryTest(*this, true, false); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealCustomEventForEachTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.CustomEventForEach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealCustomEventForEachTest::RunTest(const FString& Parameters) { return RunForEachEntryTest(*this, false, false); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealFunctionForEachBreakTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionForEachWithBreak",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealFunctionForEachBreakTest::RunTest(const FString& Parameters) { return RunForEachEntryTest(*this, true, true); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealCustomEventForEachBreakTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.CustomEventForEachWithBreak",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealCustomEventForEachBreakTest::RunTest(const FString& Parameters) { return RunForEachEntryTest(*this, false, true); }

#endif
