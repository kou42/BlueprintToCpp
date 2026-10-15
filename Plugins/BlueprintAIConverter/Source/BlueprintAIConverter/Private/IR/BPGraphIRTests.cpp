#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPGraphIR.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGraphIRNullTest,
    "BlueprintAIConverter.IR.NullInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGraphIRNullTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("stale");
    TestFalse(TEXT("Null Blueprint is rejected"), FBPGraphIRBuilder::Build(nullptr, IR));
    TestTrue(TEXT("Stale IR data is cleared"), IR.AssetPath.IsEmpty());
    TestEqual(TEXT("One diagnostic is emitted"), IR.Diagnostics.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGraphIREmptyTest,
    "BlueprintAIConverter.IR.EmptyBlueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGraphIREmptyTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage());
    TestNotNull(TEXT("Transient Blueprint"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    Blueprint->ParentClass = AActor::StaticClass();

    FBPGraphIR IR;
    TestTrue(TEXT("IR build succeeds"), FBPGraphIRBuilder::Build(Blueprint, IR));
    TestEqual(TEXT("Parent class is preserved"), IR.ParentClassPath, AActor::StaticClass()->GetPathName());
    TestEqual(TEXT("Empty Blueprint has no graphs"), IR.Graphs.Num(), 0);
    TestEqual(TEXT("No diagnostics"), IR.Diagnostics.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGraphIRLinkTest,
    "BlueprintAIConverter.IR.ExecAndDataEdges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGraphIRLinkTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage());
    UEdGraph* Graph = NewObject<UEdGraph>(Blueprint);
    Blueprint->UbergraphPages.Add(Graph);

    UEdGraphNode* Source = NewObject<UEdGraphNode>(Graph);
    UEdGraphNode* Target = NewObject<UEdGraphNode>(Graph);
    Source->CreateNewGuid();
    Target->CreateNewGuid();
    Graph->Nodes.Add(Source);
    Graph->Nodes.Add(Target);

    // 実行接続とデータ接続を同じグラフに作り、分類と向きを検証する。
    UEdGraphPin* ExecOut = Source->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("ExecOut"));
    UEdGraphPin* ExecIn = Target->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("ExecIn"));
    UEdGraphPin* DataOut = Source->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("DataOut"));
    UEdGraphPin* DataIn = Target->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, TEXT("DataIn"));
    ExecOut->MakeLinkTo(ExecIn);
    DataOut->MakeLinkTo(DataIn);
    DataIn->DefaultValue = TEXT("Hello");

    FBPGraphIR IR;
    TestTrue(TEXT("IR build succeeds"), FBPGraphIRBuilder::Build(Blueprint, IR));
    if (IR.Graphs.Num() != 1)
    {
        AddError(TEXT("Expected exactly one graph"));
        return false;
    }

    const FBPIRGraph& Result = IR.Graphs[0];
    TestEqual(TEXT("Two nodes"), Result.Nodes.Num(), 2);
    TestEqual(TEXT("One Exec edge"), Result.ExecEdges.Num(), 1);
    TestEqual(TEXT("One Data edge"), Result.DataEdges.Num(), 1);
    if (Result.ExecEdges.Num() == 1)
    {
        TestEqual(TEXT("Exec source node"), Result.ExecEdges[0].SourceNode, Source->NodeGuid);
        TestEqual(TEXT("Exec target node"), Result.ExecEdges[0].TargetNode, Target->NodeGuid);
    }
    if (Result.DataEdges.Num() == 1)
    {
        TestEqual(TEXT("Data source pin"), Result.DataEdges[0].SourcePin, DataOut->PinId);
        TestEqual(TEXT("Data target pin"), Result.DataEdges[0].TargetPin, DataIn->PinId);
    }
    TestEqual(TEXT("Input default value preserved"), Result.Nodes[1].Pins[1].DefaultValue, FString(TEXT("Hello")));
    return true;
}
// 実際のUFunctionを参照するCallFunctionノードで、解決済みの正式な参照情報を確認する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGraphIRFunctionReferenceTest,
    "BlueprintAIConverter.IR.FunctionReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGraphIRFunctionReferenceTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_GraphIRFunctionReference"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName, BPTYPE_Normal, NAME_None);
    TestNotNull(TEXT("一時Blueprintを生成"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    TestNotNull(TEXT("EventGraphを取得"), Graph);
    if (Graph == nullptr)
    {
        return false;
    }
    Graph->Nodes.Reset();

    UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
    Graph->AddNode(Call, false, false);
    Call->CreateNewGuid();
    Call->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));

    FBPGraphIR IR;
    TestTrue(TEXT("関数参照付きIR生成"), FBPGraphIRBuilder::Build(Blueprint, IR));
    const FBPIRGraph* EventGraphIR = IR.Graphs.FindByPredicate(
        [Graph](const FBPIRGraph& Candidate)
        {
            return Candidate.Path == Graph->GetPathName();
        });
    TestNotNull(TEXT("対象EventGraphを取得"), EventGraphIR);
    if (EventGraphIR == nullptr)
    {
        return false;
    }
    const FBPIRNode* Node = EventGraphIR->Nodes.FindByPredicate(
        [Call](const FBPIRNode& Candidate)
        {
            return Candidate.Id == Call->NodeGuid;
        });
    TestNotNull(TEXT("関数ノードを取得"), Node);
    if (Node == nullptr)
    {
        return false;
    }
    TestTrue(TEXT("関数参照を解決"), Node->bFunctionResolved);
    TestEqual(TEXT("関数名"), Node->FunctionName, FName(TEXT("PrintString")));
    TestEqual(TEXT("所有クラス"), Node->FunctionOwnerPath,
        UKismetSystemLibrary::StaticClass()->GetPathName());
    TestTrue(TEXT("関数パス"), Node->FunctionPath.IsEmpty() == false);
    TestTrue(TEXT("Native属性"), Node->bFunctionNative);
    TestTrue(TEXT("Static属性"), Node->bFunctionStatic);
    return true;
}
#endif
