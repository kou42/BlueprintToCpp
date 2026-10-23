#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
template <typename NodeType>
NodeType* AddFunctionLoopNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealFunctionForLoopConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.FunctionForLoopCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealFunctionForLoopConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_FunctionForLoopCall"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("UObject派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    const FName FunctionName(TEXT("RunLoopCall"));
    UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
        Blueprint, FunctionGraph, true, nullptr);

    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
    {
        Entry = Entry != nullptr ? Entry : Cast<UK2Node_FunctionEntry>(Node);
        Result = Result != nullptr ? Result : Cast<UK2Node_FunctionResult>(Node);
    }
    if (TestNotNull(TEXT("Function Entry"), Entry) == false)
    {
        return false;
    }
    if (Result == nullptr)
    {
        Result = AddFunctionLoopNode<UK2Node_FunctionResult>(FunctionGraph);
        Result->FunctionReference.SetSelfMember(FunctionName);
        Result->AllocateDefaultPins();
    }

    UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr,
        TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop"));
    if (TestNotNull(TEXT("標準ForLoopマクロ"), MacroGraph) == false)
    {
        return false;
    }
    UK2Node_MacroInstance* Loop = AddFunctionLoopNode<UK2Node_MacroInstance>(FunctionGraph);
    Loop->SetMacroGraph(MacroGraph);
    Loop->AllocateDefaultPins();
    Loop->FindPinChecked(TEXT("FirstIndex"))->DefaultValue = TEXT("1");
    Loop->FindPinChecked(TEXT("LastIndex"))->DefaultValue = TEXT("3");

    UFunction* BodyFunction = UGameplayStatics::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UGameplayStatics, SetSubtitlesEnabled));
    if (TestNotNull(TEXT("SetSubtitlesEnabled UFunctionを取得"), BodyFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* BodyCall = AddFunctionLoopNode<UK2Node_CallFunction>(FunctionGraph);
    BodyCall->SetFromFunction(BodyFunction);
    BodyCall->AllocateDefaultPins();
    BodyCall->FindPinChecked(TEXT("bEnabled"))->DefaultValue = TEXT("true");

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(FunctionGraph->GetSchema());
    UEdGraphPin* EntryThen = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ResultExecute = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (TestNotNull(TEXT("K2 schema"), Schema) == false ||
        TestNotNull(TEXT("Entry Then"), EntryThen) == false ||
        TestNotNull(TEXT("Result Execute"), ResultExecute) == false)
    {
        return false;
    }
    EntryThen->BreakAllPinLinks();
    TestTrue(TEXT("Function EntryからForLoopへ接続"), Schema->TryCreateConnection(
        EntryThen, Loop->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("LoopBodyから関数呼び出しへ接続"), Schema->TryCreateConnection(
        Loop->FindPinChecked(TEXT("LoopBody")),
        BodyCall->FindPinChecked(UEdGraphSchema_K2::PN_Execute)));
    TestTrue(TEXT("CompletedからFunction Resultへ接続"), Schema->TryCreateConnection(
        Loop->FindPinChecked(TEXT("Completed")), ResultExecute));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("関数Blueprintのコンパイル成功"), Blueprint->Status != BS_Error) == false ||
        TestNotNull(TEXT("生成クラス"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("実BlueprintからFunction ForLoopのGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("通常関数のForLoopをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    if (TestEqual(TEXT("Semantic関数数"), SemanticIR.Functions.Num(), 1) == false ||
        TestEqual(TEXT("Semantic関数名"), SemanticIR.Functions[0].Name, FunctionName) == false ||
        TestFalse(TEXT("通常関数として保持"), SemanticIR.Functions[0].bIsEventGraph) == false ||
        TestEqual(TEXT("Semantic文数"),
            SemanticIR.Functions[0].Blocks[0].Statements.Num(), 1) == false)
    {
        return false;
    }
    const FBPSemanticStatement& LoopStatement =
        SemanticIR.Functions[0].Blocks[0].Statements[0];
    if (TestTrue(TEXT("ForLoop文として保持"),
            LoopStatement.Kind == EBPSemanticStatementKind::ForLoop) == false ||
        TestEqual(TEXT("ループ本体の文数"), LoopStatement.LoopBodyStatements.Num(), 1) == false)
    {
        return false;
    }
    const FBPSemanticExpression& Call = LoopStatement.LoopBodyStatements[0].Expression;
    TestEqual(TEXT("反射検証済み関数名"),
        Call.FunctionName, FName(TEXT("SetSubtitlesEnabled")));
    TestEqual(TEXT("呼び出し所有クラス"),
        Call.FunctionOwnerPath, FString(TEXT("/Script/Engine.GameplayStatics")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("UObject通常関数のForLoop C++を生成"),
        FBPSemanticCodeGenerator::Generate(
            SemanticIR, TEXT("UFunctionForLoopObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("通常関数定義を生成"),
        Files.SourceContent.Contains(TEXT("UFunctionForLoopObject::RunLoopCall(")));
    TestTrue(TEXT("両端を含むForLoopを生成"),
        Files.SourceContent.Contains(TEXT("LoopIndex <= 3")));
    TestTrue(TEXT("共通Emitterで関数呼び出しを生成"),
        Files.SourceContent.Contains(TEXT("UGameplayStatics::SetSubtitlesEnabled(true);")));
    TestTrue(TEXT("所有クラスのincludeを生成"),
        Files.SourceContent.Contains(TEXT("#include \"Kismet/GameplayStatics.h\"")));

    FBPGraphIR MissingResultIR = GraphIR;
    FBPIRGraph* TargetGraph = MissingResultIR.Graphs.FindByPredicate(
        [FunctionName](const FBPIRGraph& Graph)
        {
            return Graph.Kind == TEXT("function") && Graph.Name == FunctionName.ToString();
        });
    if (TestNotNull(TEXT("拒否系検証用の関数Graph"), TargetGraph) == false)
    {
        return false;
    }
    const int32 ResultEdgeIndex = TargetGraph->ExecEdges.IndexOfByPredicate(
        [TargetGraph](const FBPIRLink& Edge)
        {
            const FBPIRNode* Target = TargetGraph->Nodes.FindByPredicate(
                [&Edge](const FBPIRNode& Node) { return Node.Id == Edge.TargetNode; });
            return Target != nullptr &&
                Target->ClassPath == TEXT("/Script/BlueprintGraph.K2Node_FunctionResult");
        });
    if (TestTrue(TEXT("CompletedのResult接続を特定"), ResultEdgeIndex != INDEX_NONE) == false)
    {
        return false;
    }
    TargetGraph->ExecEdges.RemoveAt(ResultEdgeIndex);
    FBPSemanticIR RejectedIR;
    TestFalse(TEXT("Function Resultへ到達しないCompletedを拒否"),
        FBPSemanticLowering::Lower(Blueprint, MissingResultIR, RejectedIR));
    TestTrue(TEXT("Result欠落の診断を保持"), RejectedIR.HasErrors());

    FBPGraphIR UnresolvedCallIR = GraphIR;
    FBPIRGraph* UnresolvedGraph = UnresolvedCallIR.Graphs.FindByPredicate(
        [FunctionName](const FBPIRGraph& Graph)
        {
            return Graph.Kind == TEXT("function") && Graph.Name == FunctionName.ToString();
        });
    if (TestNotNull(TEXT("未解決呼び出し検証用のGraph"), UnresolvedGraph) == false)
    {
        return false;
    }
    FBPIRNode* CallNode = UnresolvedGraph->Nodes.FindByPredicate(
        [](const FBPIRNode& Node)
        {
            return Node.FunctionName == TEXT("SetSubtitlesEnabled");
        });
    if (TestNotNull(TEXT("反射呼び出しNode"), CallNode) == false)
    {
        return false;
    }
    CallNode->bFunctionResolved = false;
    RejectedIR = FBPSemanticIR();
    TestFalse(TEXT("未解決のループ本体関数を拒否"),
        FBPSemanticLowering::Lower(Blueprint, UnresolvedCallIR, RejectedIR));
    TestTrue(TEXT("未解決関数の診断を保持"), RejectedIR.HasErrors());
    return true;
}

#endif
