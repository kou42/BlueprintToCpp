#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPCopilotGraphJsonExporter.h"
#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace
{
template <typename TNode>
TNode* AddGeneratedNode(UEdGraph* Graph)
{
    TNode* Node = NewObject<TNode>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

UK2Node_CallFunction* AddGeneratedPrint(UEdGraph* Graph, UFunction* Function,
    const TCHAR* Message, const TCHAR* Key)
{
    UK2Node_CallFunction* Node = AddGeneratedNode<UK2Node_CallFunction>(Graph);
    Node->SetFromFunction(Function);
    Node->AllocateDefaultPins();
    Node->FindPinChecked(TEXT("InString"))->DefaultValue = Message;
    Node->FindPinChecked(TEXT("Key"))->DefaultValue = Key;
    Node->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Node->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Node->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("2.5");
    Node->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.2,G=0.4,B=0.6,A=1.0)");
    return Node;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedBranchAssetTest,
    "BlueprintAIConverter.GeneratedAssets.Branch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedBranchAssetTest::RunTest(const FString& Parameters)
{
    const FString PackagePath = TEXT("/Game/BlueprintToCppTests/BP_Test_Branch");
    UPackage* Package = CreatePackage(*PackagePath);
    if (TestNotNull(TEXT("保存先パッケージ"), Package) == false)
    {
        return false;
    }
    UBlueprint* Blueprint = FindObject<UBlueprint>(Package, TEXT("BP_Test_Branch"));
    if (Blueprint == nullptr)
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(), Package, TEXT("BP_Test_Branch"),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }
    if (TestNotNull(TEXT("Branch BPを生成"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("EventGraph"), Graph) == false)
    {
        return false;
    }

    // 保存済みアセットを再実行した場合も、毎回同じ4ノード・3接続に再構築する。
    // Reset()のみではノードの所有関係が残るため、RemoveNodeでグラフから取り除く。
    const TArray<UEdGraphNode*> ExistingNodes = Graph->Nodes;
    for (UEdGraphNode* ExistingNode : ExistingNodes)
    {
        if (ExistingNode != nullptr)
        {
            Graph->RemoveNode(ExistingNode);
        }
    }

    UK2Node_Event* BeginPlay = AddGeneratedNode<UK2Node_Event>(Graph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UK2Node_IfThenElse* Branch = AddGeneratedNode<UK2Node_IfThenElse>(Graph);
    Branch->AllocateDefaultPins();
    Branch->FindPinChecked(TEXT("Condition"))->DefaultValue = TEXT("true");

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (TestNotNull(TEXT("PrintString関数"), PrintFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* TruePrint = AddGeneratedPrint(
        Graph, PrintFunction, TEXT("Branch True"), TEXT("branch-true"));
    UK2Node_CallFunction* FalsePrint = AddGeneratedPrint(
        Graph, PrintFunction, TEXT("Branch False"), TEXT("branch-false"));

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    if (TestNotNull(TEXT("K2 Schema"), Schema) == false)
    {
        return false;
    }
    // Pinを明示的に接続し、Graph IRが正しい実行順序を復元できるか検証する。
    if (TestTrue(TEXT("BeginPlay -> Branch"), Schema->TryCreateConnection(
        BeginPlay->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        Branch->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false ||
        TestTrue(TEXT("Branch True -> Print"), Schema->TryCreateConnection(
        Branch->FindPinChecked(UEdGraphSchema_K2::PN_Then),
        TruePrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false ||
        TestTrue(TEXT("Branch False -> Print"), Schema->TryCreateConnection(
        Branch->FindPinChecked(TEXT("else")),
        FalsePrint->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false)
    {
        return false;
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("コンパイル済みBP"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR IR;
    if (TestTrue(TEXT("Graph IRを生成"), FBPGraphIRBuilder::Build(Blueprint, IR)) == false)
    {
        return false;
    }
    const FBPIRGraph* EventIR = IR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Candidate) { return Candidate.Kind == TEXT("event"); });
    if (TestNotNull(TEXT("Event IR"), EventIR) == false)
    {
        return false;
    }
    TestEqual(TEXT("ノード数"), EventIR->Nodes.Num(), 4);
    TestEqual(TEXT("実行接続数"), EventIR->ExecEdges.Num(), 3);
    TestEqual(TEXT("データ接続数"), EventIR->DataEdges.Num(), 0);

    const FBPIRNode* BranchIR = EventIR->Nodes.FindByPredicate(
        [](const FBPIRNode& Node)
        {
            return Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
        });
    if (TestNotNull(TEXT("Branch IRノード"), BranchIR) == false)
    {
        return false;
    }
    const FBPIRPin* Condition = BranchIR->Pins.FindByPredicate(
        [](const FBPIRPin& Pin) { return Pin.Name == TEXT("Condition"); });
    if (TestNotNull(TEXT("Condition Pin"), Condition) == false)
    {
        return false;
    }
    TestEqual(TEXT("Condition型"), Condition->Category, UEdGraphSchema_K2::PC_Boolean);
    TestEqual(TEXT("Condition既定値"), Condition->DefaultValue, FString(TEXT("true")));

    // Copilot Graph JSONを再読込し、ノードと接続が出力で欠落しないことを確認する。
    FString JsonPath;
    FString Error;
    if (TestTrue(TEXT("Copilot Graph JSONを保存"),
        FBPCopilotGraphJsonExporter::Export(
            IR, FEngineVersion::Current().ToString(), JsonPath, Error)) == false)
    {
        AddError(Error);
        return false;
    }
    FString Json;
    if (TestTrue(TEXT("JSONを再読込"), FFileHelper::LoadFileToString(Json, *JsonPath)) == false)
    {
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    if (TestTrue(TEXT("JSONを解析"), FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(Json), Root)) == false || Root.IsValid() == false)
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>& Graphs = Root->GetArrayField(TEXT("graphs"));
    if (TestTrue(TEXT("JSONにグラフが存在"), Graphs.IsEmpty() == false) == false)
    {
        return false;
    }
    // Graph IRとの一致だけでなく、上で固定した4ノードという独立した期待値を使用する。
    const TSharedPtr<FJsonObject> JsonEventGraph = Graphs[0]->AsObject();
    TestEqual(TEXT("JSONノード数"), JsonEventGraph->GetArrayField(TEXT("nodes")).Num(), 4);
    TestEqual(TEXT("JSON実行接続数"), JsonEventGraph->GetArrayField(TEXT("exec_edges")).Num(), 3);
    TestEqual(TEXT("JSONデータ接続数"), JsonEventGraph->GetArrayField(TEXT("data_edges")).Num(), 0);

    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("BranchをLowering"), FBPSemanticLowering::Lower(
        Blueprint, IR, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("C++生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AGeneratedBranch"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("ifを出力"), Files.SourceContent.Contains(TEXT("if (true)")));
    TestTrue(TEXT("elseを出力"), Files.SourceContent.Contains(TEXT("else")));
    TestTrue(TEXT("True側を出力"), Files.SourceContent.Contains(TEXT("Branch True")));
    TestTrue(TEXT("False側を出力"), Files.SourceContent.Contains(TEXT("Branch False")));

    Blueprint->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (TestTrue(TEXT("BP_Test_Branch.uassetを保存"),
        UPackage::SavePackage(Package, Blueprint, *Filename, Args)) == false)
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("GENERATED_BRANCH=%s"), *Filename));
    AddInfo(FString::Printf(TEXT("GENERATED_GRAPH_JSON=%s"), *JsonPath));
    return true;
}

#endif
