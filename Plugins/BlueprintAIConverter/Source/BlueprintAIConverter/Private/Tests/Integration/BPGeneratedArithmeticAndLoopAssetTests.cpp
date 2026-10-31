#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../BPSemanticRoundtripTestUtils.h"

#include "BPCopilotGraphJsonExporter.h"
#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

namespace
{
template <typename TNode>
TNode* AddFixtureNode(UEdGraph* Graph)
{
    TNode* Node = NewObject<TNode>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

bool ConnectFixture(UEdGraph* Graph, UEdGraphNode* Source, FName Output,
    UEdGraphNode* Target, FName Input)
{
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    return Schema != nullptr && Source->FindPin(Output) != nullptr &&
        Target->FindPin(Input) != nullptr &&
        Schema->TryCreateConnection(Source->FindPin(Output), Target->FindPin(Input));
}

UK2Node_CallFunction* AddFixturePrint(UEdGraph* Graph, UFunction* Function,
    const TCHAR* Message)
{
    UK2Node_CallFunction* Print = AddFixtureNode<UK2Node_CallFunction>(Graph);
    Print->SetFromFunction(Function);
    Print->AllocateDefaultPins();
    Print->FindPinChecked(TEXT("InString"))->DefaultValue = Message;
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("2.5");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.2,G=0.4,B=0.6,A=1.0)");
    return Print;
}

bool GenerateFixture(const FString& AssetName, bool bLoop, FAutomationTestBase& Test, bool bNested = false)
{
    const FString PackagePath = TEXT("/Game/BlueprintToCppTests/") + AssetName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (Test.TestNotNull(TEXT("保存先パッケージ"), Package) == false)
    {
        return false;
    }
    // 固定パスのアセットを先にロードし、複数回のテスト実行で同じBPを更新する。
    UBlueprint* Blueprint = LoadObject<UBlueprint>(
        nullptr, *(PackagePath + TEXT(".") + AssetName));
    if (Blueprint == nullptr)
    {
        Blueprint = FindObject<UBlueprint>(Package, *AssetName);
    }
    if (Blueprint == nullptr)
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(), Package, FName(*AssetName),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }
    if (Test.TestNotNull(TEXT("生成Blueprint"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (Test.TestNotNull(TEXT("EventGraph"), Graph) == false)
    {
        return false;
    }

    // 再実行時には既存ノードを取り除き、接続とGUIDを含む新しいテストグラフを生成する。
    const TArray<UEdGraphNode*> Existing = Graph->Nodes;
    for (UEdGraphNode* Node : Existing)
    {
        if (Node != nullptr)
        {
            Graph->RemoveNode(Node);
        }
    }

    UK2Node_Event* Begin = AddFixtureNode<UK2Node_Event>(Graph);
    Begin->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    Begin->bOverrideFunction = true;
    Begin->AllocateDefaultPins();

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (Test.TestNotNull(TEXT("PrintString関数"), PrintFunction) == false)
    {
        return false;
    }

    const int32 ExpectedNodes = bNested ? 6 : (bLoop ? 4 : 10);
    const int32 ExpectedExecEdges = bNested ? 5 : 3;
    const int32 ExpectedDataEdges = bLoop ? 0 : 6;

    if (bLoop)
    {
        // UE標準のForLoopマクロを使用し、実アセットのPin定義も検証対象に含める。
        UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr,
            TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop"));
        if (Test.TestNotNull(TEXT("標準ForLoopマクロ"), MacroGraph) == false)
        {
            return false;
        }
        UK2Node_MacroInstance* Loop = AddFixtureNode<UK2Node_MacroInstance>(Graph);
        Loop->SetMacroGraph(MacroGraph);
        Loop->AllocateDefaultPins();
        Loop->FindPinChecked(TEXT("FirstIndex"))->DefaultValue = TEXT("1");
        Loop->FindPinChecked(TEXT("LastIndex"))->DefaultValue = TEXT("3");
        UK2Node_CallFunction* Body = AddFixturePrint(Graph, PrintFunction, TEXT("Loop body"));
        UK2Node_CallFunction* Done = AddFixturePrint(Graph, PrintFunction, TEXT("Loop completed"));
        UK2Node_MacroInstance* BodyLoop = Loop;
        if (bNested)
        {
            BodyLoop = AddFixtureNode<UK2Node_MacroInstance>(Graph);
            BodyLoop->SetMacroGraph(MacroGraph);
            BodyLoop->AllocateDefaultPins();
            BodyLoop->FindPinChecked(TEXT("FirstIndex"))->DefaultValue = TEXT("2");
            BodyLoop->FindPinChecked(TEXT("LastIndex"))->DefaultValue = TEXT("4");
            UK2Node_CallFunction* InnerDone = AddFixturePrint(Graph, PrintFunction, TEXT("Inner completed"));
            if (Test.TestTrue(TEXT("外側LoopBodyから内側ForLoop"), ConnectFixture(Graph, Loop,
                    TEXT("LoopBody"), BodyLoop, UEdGraphSchema_K2::PN_Execute)) == false ||
                Test.TestTrue(TEXT("内側CompletedからPrint"), ConnectFixture(Graph, BodyLoop,
                    TEXT("Completed"), InnerDone, UEdGraphSchema_K2::PN_Execute)) == false)
            {
                return false;
            }
        }
        if (Test.TestTrue(TEXT("BeginPlayからForLoop"), ConnectFixture(Graph, Begin,
                UEdGraphSchema_K2::PN_Then, Loop, UEdGraphSchema_K2::PN_Execute)) == false ||
            Test.TestTrue(TEXT("LoopBodyからPrint"), ConnectFixture(Graph, BodyLoop,
                TEXT("LoopBody"), Body, UEdGraphSchema_K2::PN_Execute)) == false ||
            Test.TestTrue(TEXT("CompletedからPrint"), ConnectFixture(Graph, Loop,
                TEXT("Completed"), Done, UEdGraphSchema_K2::PN_Execute)) == false)
        {
            return false;
        }
    }
    else
    {
        // 既存の算術比較Branch変換テストと同じ演算列を、保存可能なBPとして再構築する。
        FEdGraphPinType ScoreType;
        ScoreType.PinCategory = UEdGraphSchema_K2::PC_Int;
        if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, TEXT("Score")) == INDEX_NONE)
        {
            if (Test.TestTrue(TEXT("Score変数"), FBlueprintEditorUtils::AddMemberVariable(
                Blueprint, TEXT("Score"), ScoreType, TEXT("42"))) == false)
            {
                return false;
            }
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        UK2Node_VariableGet* Score = AddFixtureNode<UK2Node_VariableGet>(Graph);
        Score->VariableReference.SetSelfMember(TEXT("Score"));
        Score->AllocateDefaultPins();

        const FName Functions[] = { TEXT("Add_IntInt"), TEXT("Subtract_IntInt"),
            TEXT("Multiply_IntInt"), TEXT("Divide_IntInt"), TEXT("Greater_IntInt") };
        const TCHAR* Defaults[] = { TEXT("8"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("35") };
        UK2Node_CallFunction* Operations[5] = {};
        for (int32 Index = 0; Index < 5; ++Index)
        {
            UFunction* Function = UKismetMathLibrary::StaticClass()->FindFunctionByName(Functions[Index]);
            if (Test.TestNotNull(TEXT("算術・比較UFunction"), Function) == false)
            {
                return false;
            }
            // 加算と乗算はCommutativeAssociativeBinaryOperatorを使う既存仕様に合わせる。
            if (Index == 0 || Index == 2)
            {
                UK2Node_CommutativeAssociativeBinaryOperator* Node =
                    AddFixtureNode<UK2Node_CommutativeAssociativeBinaryOperator>(Graph);
                Node->SetFromFunction(Function);
                Node->AllocateDefaultPins();
                Operations[Index] = Node;
            }
            else
            {
                UK2Node_CallFunction* Node = AddFixtureNode<UK2Node_CallFunction>(Graph);
                Node->SetFromFunction(Function);
                Node->AllocateDefaultPins();
                Operations[Index] = Node;
            }
            Operations[Index]->FindPinChecked(TEXT("B"))->DefaultValue = Defaults[Index];
        }

        UK2Node_IfThenElse* Branch = AddFixtureNode<UK2Node_IfThenElse>(Graph);
        Branch->AllocateDefaultPins();
        UK2Node_CallFunction* TruePrint = AddFixturePrint(Graph, PrintFunction, TEXT("Arithmetic True"));
        UK2Node_CallFunction* FalsePrint = AddFixturePrint(Graph, PrintFunction, TEXT("Arithmetic False"));
        if (Test.TestTrue(TEXT("BeginPlayからBranch"), ConnectFixture(Graph, Begin,
                UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute)) == false ||
            Test.TestTrue(TEXT("True側Print"), ConnectFixture(Graph, Branch,
                UEdGraphSchema_K2::PN_Then, TruePrint, UEdGraphSchema_K2::PN_Execute)) == false ||
            Test.TestTrue(TEXT("False側Print"), ConnectFixture(Graph, Branch,
                TEXT("else"), FalsePrint, UEdGraphSchema_K2::PN_Execute)) == false ||
            Test.TestTrue(TEXT("Scoreから加算"), ConnectFixture(Graph, Score,
                TEXT("Score"), Operations[0], TEXT("A"))) == false)
        {
            return false;
        }
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (Test.TestTrue(TEXT("算術演算結果を次の演算へ接続"),
                ConnectFixture(Graph, Operations[Index], TEXT("ReturnValue"),
                    Operations[Index + 1], TEXT("A"))) == false)
            {
                return false;
            }
        }
        if (Test.TestTrue(TEXT("比較結果をConditionへ接続"),
            ConnectFixture(Graph, Operations[4], TEXT("ReturnValue"),
                Branch, TEXT("Condition"))) == false)
        {
            return false;
        }
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Test.TestTrue(TEXT("Blueprintコンパイルエラーなし"),
        Blueprint->Status != BS_Error) == false)
    {
        return false;
    }
    if (Test.TestNotNull(TEXT("生成クラス"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    FBPGraphIR IR;
    if (Test.TestTrue(TEXT("Graph IR生成"), FBPGraphIRBuilder::Build(Blueprint, IR)) == false)
    {
        return false;
    }
    const FBPIRGraph* EventIR = IR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Item) { return Item.Kind == TEXT("event"); });
    if (Test.TestNotNull(TEXT("Event IR"), EventIR) == false)
    {
        return false;
    }
    Test.TestEqual(TEXT("ノード数"), EventIR->Nodes.Num(), ExpectedNodes);
    Test.TestEqual(TEXT("実行接続数"), EventIR->ExecEdges.Num(), ExpectedExecEdges);
    Test.TestEqual(TEXT("データ接続数"), EventIR->DataEdges.Num(), ExpectedDataEdges);

    FString JsonPath;
    FString Error;
    if (Test.TestTrue(TEXT("Graph JSON出力"), FBPCopilotGraphJsonExporter::Export(
        IR, FEngineVersion::Current().ToString(), JsonPath, Error)) == false)
    {
        Test.AddError(Error);
        return false;
    }

    FBPSemanticIR Semantic;
    const bool bLowered = FBPSemanticLowering::Lower(Blueprint, IR, Semantic);
    if (Test.TestTrue(TEXT("Semantic Lowering"), bLowered) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
        {
            Test.AddError(Diagnostic.Message);
        }
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (Test.TestTrue(TEXT("C++生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, bLoop ? TEXT("AGeneratedForLoop") : TEXT("AGeneratedArithmetic"),
        FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            Test.AddError(Diagnostic);
        }
        return false;
    }
    Test.TestTrue(TEXT("生成コードの主要構文"),
        bLoop ? Files.SourceContent.Contains(TEXT("for (int32")) :
            Files.SourceContent.Contains(TEXT("if (")) &&
            Files.SourceContent.Contains(TEXT("Arithmetic True")));

    if (bNested)
    {
        if (Test.TestEqual(TEXT("入れ子Loopの関数数"), Semantic.Functions.Num(), 1) == false ||
            Test.TestEqual(TEXT("入れ子Loopの構造化Block数"), Semantic.Functions[0].Blocks.Num(), 1) == false ||
            Test.TestEqual(TEXT("外側Loop文数"), Semantic.Functions[0].Blocks[0].Statements.Num(), 1) == false)
        {
            return false;
        }
        const FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
        if (Test.TestTrue(TEXT("外側Bodyに内側LoopとそのCompleted後続を保持"), Outer.LoopBodyStatements.Num() == 2 &&
                Outer.LoopBodyStatements[0].Kind == EBPSemanticStatementKind::ForLoop) == false) { return false; }
        const FBPSemanticStatement& Inner = Outer.LoopBodyStatements[0];
        Test.TestEqual(TEXT("内側Body文数"), Inner.LoopBodyStatements.Num(), 1);
        Test.TestTrue(TEXT("内側Completedを外側Bodyの後続へ正規化"), Inner.CompletedStatements.IsEmpty() &&
            Outer.LoopBodyStatements[1].Kind == EBPSemanticStatementKind::Expression);
        Test.TestEqual(TEXT("外側Completed文数"), Outer.CompletedStatements.Num(), 1);
        Test.TestTrue(TEXT("内側Indexと境界値を生成"),
            Files.SourceContent.Contains(TEXT("for (int32 InnerIndex = 2; InnerIndex <= 4; ++InnerIndex)")));
        const int32 BodyPosition = Files.SourceContent.Find(TEXT("Loop body"));
        const int32 InnerDonePosition = Files.SourceContent.Find(TEXT("Inner completed"));
        const int32 OuterDonePosition = Files.SourceContent.Find(TEXT("Loop completed"));
        Test.TestTrue(TEXT("内側Body、内側Completed、外側Completedの順序を保持"),
            BodyPosition != INDEX_NONE && InnerDonePosition > BodyPosition && OuterDonePosition > InnerDonePosition);
        for (int32 CaseIndex = 0; CaseIndex < 2; ++CaseIndex)
        {
            FBPGraphIR Invalid = IR;
            FBPIRGraph* Target = Invalid.Graphs.FindByPredicate(
                [](const FBPIRGraph& Candidate) { return Candidate.Kind == TEXT("event"); });
            if (Test.TestNotNull(TEXT("入れ子拒否系Graph"), Target) == false) { return false; }
            FBPIRNode* Loop = Target->Nodes.FindByPredicate(
                [](const FBPIRNode& Node) { return Node.MacroGraphName == TEXT("ForLoop"); });
            if (Test.TestNotNull(TEXT("入れ子拒否系Loop"), Loop) == false) { return false; }
            if (CaseIndex == 0)
            {
                FBPIRPin* Bound = Loop->Pins.FindByPredicate(
                    [](const FBPIRPin& Pin) { return Pin.Name == TEXT("LastIndex") && Pin.Direction == EGPD_Input; });
                if (Test.TestNotNull(TEXT("入れ子拒否系境界Pin"), Bound) == false) { return false; }
                Bound->DefaultValue = TEXT("invalid");
            }
            else
            {
                const FBPIRLink* Edge = Target->ExecEdges.FindByPredicate(
                    [Loop](const FBPIRLink& Link) { return Link.SourceNode == Loop->Id; });
                if (Test.TestNotNull(TEXT("入れ子拒否系Exec edge"), Edge) == false) { return false; }
                const FBPIRLink Duplicate = *Edge;
                Target->ExecEdges.Add(Duplicate);
            }
            FBPSemanticIR Rejected;
            Test.TestFalse(TEXT("不正な境界値と重複Exec接続を拒否"),
                FBPSemanticLowering::Lower(Blueprint, Invalid, Rejected));
            Test.TestTrue(TEXT("入れ子拒否の診断と部分関数非公開"),
                Rejected.HasErrors() && Rejected.Functions.IsEmpty());
        }
    }

    // JSON/C++を生成してからアセットを保存することで、検証失敗時の不完全なBP保存を防ぐ。
    Blueprint->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    if (Test.TestTrue(TEXT("BPアセット保存"),
        UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs)) == false)
    {
        return false;
    }
    Test.AddInfo(FString::Printf(TEXT("GENERATED_BLUEPRINT=%s"), *Filename));
    Test.AddInfo(FString::Printf(TEXT("GENERATED_GRAPH_JSON=%s"), *JsonPath));
    return BPSemanticTestUtils::ExportRoundtrip(Test, Blueprint, IR, Semantic);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedArithmeticAssetTest,
    "BlueprintAIConverter.GeneratedAssets.ArithmeticComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedArithmeticAssetTest::RunTest(const FString& Parameters)
{
    return GenerateFixture(TEXT("BP_Test_ArithmeticComparison"), false, *this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedForLoopAssetTest,
    "BlueprintAIConverter.GeneratedAssets.ForLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedForLoopAssetTest::RunTest(const FString& Parameters)
{
    return GenerateFixture(TEXT("BP_Test_ForLoop"), true, *this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedNestedForLoopAssetTest,
    "BlueprintAIConverter.GeneratedAssets.NestedForLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedNestedForLoopAssetTest::RunTest(const FString& Parameters)
{
    return GenerateFixture(TEXT("BP_Test_NestedForLoop"), true, *this, true);
}

#endif
