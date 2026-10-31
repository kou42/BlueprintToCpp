#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "BPSemanticJsonExporter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MacroInstance.h"
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

bool RunCustomEventForLoopTest(FAutomationTestBase& Test, int32 Depth)
{
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CustomForLoop")),
        BPTYPE_Normal, NAME_None);
    if (Test.TestNotNull(TEXT("UObject Blueprint"), Blueprint) == false) { return false; }
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (Test.TestNotNull(TEXT("Event Graph"), Graph) == false) { return false; }
    Graph->Nodes.Reset();
    UK2Node_CustomEvent* Entry = AddNode<UK2Node_CustomEvent>(Graph);
    Entry->CustomFunctionName = TEXT("OnLoops");
    Entry->AllocateDefaultPins();
    UEdGraph* Macro = LoadObject<UEdGraph>(nullptr,
        TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop"));
    if (Test.TestNotNull(TEXT("標準ForLoop"), Macro) == false) { return false; }
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
    UFunction* ConvertFunction = UKismetStringLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_IntToString"));
    if (Test.TestNotNull(TEXT("PrintString署名"), PrintFunction) == false ||
        Test.TestNotNull(TEXT("変換署名"), ConvertFunction) == false) { return false; }
    const auto Print = [Graph, PrintFunction](const FString& Value)
    {
        UK2Node_CallFunction* Call = AddNode<UK2Node_CallFunction>(Graph);
        Call->SetFromFunction(PrintFunction);
        Call->AllocateDefaultPins();
        Call->FindPinChecked(TEXT("InString"))->DefaultValue = Value;
        return Call;
    };
    TArray<UK2Node_MacroInstance*> Loops;
    TArray<UK2Node_CallFunction*> Done;
    for (int32 Index = 0; Index < Depth; ++Index)
    {
        UK2Node_MacroInstance* Loop = AddNode<UK2Node_MacroInstance>(Graph);
        Loop->SetMacroGraph(Macro);
        Loop->AllocateDefaultPins();
        Loop->FindPinChecked(TEXT("FirstIndex"))->DefaultValue = TEXT("0");
        Loop->FindPinChecked(TEXT("LastIndex"))->DefaultValue = TEXT("2");
        Loops.Add(Loop);
        Done.Add(Print(FString::Printf(TEXT("Completed %d"), Index)));
    }
    UK2Node_CallFunction* Body = Print(TEXT(""));
    UK2Node_CallFunction* Tail = Print(TEXT("Body tail"));
    UK2Node_CallFunction* Before = Depth > 1 ? Print(TEXT("Before loops")) : nullptr;
    UK2Node_CallFunction* Convert = AddNode<UK2Node_CallFunction>(Graph);
    Convert->SetFromFunction(ConvertFunction);
    Convert->AllocateDefaultPins();
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    const auto Connect = [Schema](UK2Node* From, FName Output, UK2Node* To, FName Input)
    {
        return Schema->TryCreateConnection(From->FindPinChecked(Output), To->FindPinChecked(Input));
    };
    bool bConnected = true;
    if (Before != nullptr)
    {
        bConnected &= Connect(Entry, UEdGraphSchema_K2::PN_Then, Before, UEdGraphSchema_K2::PN_Execute);
        bConnected &= Connect(Before, UEdGraphSchema_K2::PN_Then, Loops[0], UEdGraphSchema_K2::PN_Execute);
    }
    else { bConnected &= Connect(Entry, UEdGraphSchema_K2::PN_Then, Loops[0], UEdGraphSchema_K2::PN_Execute); }
    for (int32 Index = 0; Index < Depth; ++Index)
    {
        UK2Node* NextBody = Index + 1 < Depth ? static_cast<UK2Node*>(Loops[Index + 1]) : static_cast<UK2Node*>(Body);
        bConnected &= Connect(Loops[Index], TEXT("LoopBody"), NextBody, UEdGraphSchema_K2::PN_Execute);
        bConnected &= Connect(Loops[Index], TEXT("Completed"), Done[Index], UEdGraphSchema_K2::PN_Execute);
    }
    bConnected &= Connect(Body, UEdGraphSchema_K2::PN_Then, Tail, UEdGraphSchema_K2::PN_Execute);
    bConnected &= Connect(Loops.Last(), TEXT("Index"), Convert, TEXT("InInt"));
    bConnected &= Connect(Convert, TEXT("ReturnValue"), Body, TEXT("InString"));
    if (Test.TestTrue(TEXT("実ループ接続"), bConnected) == false) { return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Test.TestTrue(TEXT("Blueprintコンパイル成功"), Blueprint->Status != BS_Error) == false) { return false; }
    FBPGraphIR GraphIR;
    if (Test.TestTrue(TEXT("Graph IRを構築"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false) { return false; }
    for (FBPIRGraph& Target : GraphIR.Graphs)
    {
        Algo::Reverse(Target.Nodes);
        Algo::Reverse(Target.ExecEdges);
    }
    FBPSemanticIR IR;
    if (Test.TestTrue(TEXT("Custom Eventの複雑ForLoopをLowering"), FBPSemanticLowering::Lower(Blueprint, GraphIR, IR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics) { Test.AddError(Diagnostic.Message); }
        return false;
    }
    if (Test.TestEqual(TEXT("関数数"), IR.Functions.Num(), 1) == false) { return false; }
    Test.TestEqual(TEXT("反射されたイベント名"), IR.Functions[0].Name, FName(TEXT("OnLoops")));
    Test.TestTrue(TEXT("Event Graph由来"), IR.Functions[0].bIsEventGraph);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (Test.TestTrue(TEXT("UObjectのイベント本体を生成"), FBPSemanticCodeGenerator::Generate(
            IR, TEXT("UCustomForLoop"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { Test.AddError(Diagnostic); }
        return false;
    }
    Test.TestTrue(TEXT("Custom Event定義"), Files.SourceContent.Contains(TEXT("UCustomForLoop::OnLoops_Implementation()")));
    Test.TestFalse(TEXT("lifecycleを混入しない"), Files.SourceContent.Contains(TEXT("Super::BeginPlay")));
    TArray<FString> Pieces;
    Files.SourceContent.ParseIntoArray(Pieces, TEXT("for ("), false);
    Test.TestEqual(TEXT("ループの深さを保持"), Pieces.Num(), Depth + 1);
    Test.TestTrue(TEXT("最内側Indexのスコープ"), Files.SourceContent.Contains(Depth == 1
        ? TEXT("Conv_IntToString(LoopIndex)") : Depth == 2
        ? TEXT("Conv_IntToString(InnerIndex)") : TEXT("Conv_IntToString(LoopIndex2)")));
    int32 Previous = Files.SourceContent.Find(TEXT("Body tail"));
    for (int32 Index = Depth - 1; Index >= 0; --Index)
    {
        const FString Marker = FString::Printf(TEXT("Completed %d"), Index);
        const int32 Position = Files.SourceContent.Find(Marker);
        Test.TestTrue(TEXT("内側→外側Completedの順序"), Previous != INDEX_NONE && Position > Previous);
        Files.SourceContent.ParseIntoArray(Pieces, *Marker, false);
        Test.TestEqual(TEXT("Completedを一度だけ生成"), Pieces.Num(), 2);
        Previous = Position;
    }
    if (Before != nullptr)
    {
        Test.TestTrue(TEXT("ループ先行文を保持"), Files.SourceContent.Find(TEXT("Before loops")) < Files.SourceContent.Find(TEXT("for (")));
    }
    for (int32 CaseIndex = 0; CaseIndex < 5; ++CaseIndex)
    {
        FBPGraphIR Invalid = GraphIR;
        FBPIRGraph* Target = Invalid.Graphs.FindByPredicate([Graph](const FBPIRGraph& Candidate)
            { return Candidate.Name == Graph->GetName(); });
        if (Test.TestNotNull(TEXT("拒否確認用Graph"), Target) == false) { return false; }
        if (CaseIndex == 0) { Invalid.ParentClassPath = TEXT("/Script/Engine.Actor"); }
        if (CaseIndex == 1)
        {
            for (FBPIRNode& Node : Target->Nodes)
            {
                if (Node.Id != Loops[0]->NodeGuid) { continue; }
                for (FBPIRPin& Pin : Node.Pins) { if (Pin.Name == TEXT("LastIndex")) { Pin.DefaultValue = TEXT("invalid"); } }
            }
        }
        if (CaseIndex == 2)
        {
            FBPIRLink Cycle;
            Cycle.SourceNode = Tail->NodeGuid; Cycle.SourcePin = Tail->FindPinChecked(UEdGraphSchema_K2::PN_Then)->PinId;
            Cycle.TargetNode = Body->NodeGuid; Cycle.TargetPin = Body->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId;
            Target->ExecEdges.Add(Cycle);
        }
        if (CaseIndex == 3)
        {
            for (FBPIRLink& Edge : Target->DataEdges)
            {
                if (Edge.SourceNode == Convert->NodeGuid)
                {
                    Edge.TargetNode = Done[0]->NodeGuid; Edge.TargetPin = Done[0]->FindPinChecked(TEXT("InString"))->PinId;
                }
            }
        }
        if (CaseIndex == 4)
        {
            FBPIRLink Extra;
            Extra.SourceNode = FGuid::NewGuid(); Extra.SourcePin = FGuid::NewGuid();
            Extra.TargetNode = Body->NodeGuid; Extra.TargetPin = Body->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId;
            Target->ExecEdges.Add(Extra);
        }
        FBPSemanticIR Rejected;
        Test.TestFalse(TEXT("不正な親・境界・循環・スコープ・流入を拒否"), FBPSemanticLowering::Lower(Blueprint, Invalid, Rejected));
        Test.TestTrue(TEXT("拒否時に部分関数を公開しない"), Rejected.Functions.IsEmpty() && Rejected.HasErrors());
    }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealCustomEventForLoopTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.CustomEventForLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealCustomEventForLoopTest::RunTest(const FString& Parameters) { return RunCustomEventForLoopTest(*this, 1); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealCustomEventNestedForLoopTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.CustomEventNestedForLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealCustomEventNestedForLoopTest::RunTest(const FString& Parameters) { return RunCustomEventForLoopTest(*this, 2); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealCustomEventTripleForLoopTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.CustomEventTripleForLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPRealCustomEventTripleForLoopTest::RunTest(const FString& Parameters) { return RunCustomEventForLoopTest(*this, 3); }

#endif
