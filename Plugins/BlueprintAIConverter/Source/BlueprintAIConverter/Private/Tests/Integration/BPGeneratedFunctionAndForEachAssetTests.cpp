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
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

bool ConnectPins(UEdGraph* Graph, UEdGraphNode* Source, FName Output,
    UEdGraphNode* Target, FName Input)
{
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    return Schema != nullptr && Source->FindPin(Output) != nullptr &&
        Target->FindPin(Input) != nullptr &&
        Schema->TryCreateConnection(Source->FindPin(Output), Target->FindPin(Input));
}

UK2Node_CallFunction* AddPrint(UEdGraph* Graph, UFunction* Function, const TCHAR* Message)
{
    UK2Node_CallFunction* Node = AddFixtureNode<UK2Node_CallFunction>(Graph);
    Node->SetFromFunction(Function);
    Node->AllocateDefaultPins();
    Node->FindPinChecked(TEXT("InString"))->DefaultValue = Message;
    Node->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Node->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Node->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("2.5");
    Node->FindPinChecked(TEXT("TextColor"))->DefaultValue =
        TEXT("(R=0.2,G=0.4,B=0.6,A=1.0)");
    return Node;
}

// GUIDの文字列表現だけでなく、Node/Pinの4要素が全て一致することを検証する。
// これにより、接続本数だけが一致する誤ったJSONを見逃さない。
bool CheckJsonLinks(const FBPIRGraph& Graph, const TSharedPtr<FJsonObject>& JsonGraph,
    const TCHAR* Field)
{
    const TArray<FBPIRLink>& Expected = FCString::Strcmp(Field, TEXT("exec_edges")) == 0
        ? Graph.ExecEdges : Graph.DataEdges;
    const TArray<TSharedPtr<FJsonValue>>& Actual = JsonGraph->GetArrayField(Field);
    if (Expected.Num() != Actual.Num())
    {
        return false;
    }
    TSet<FString> ActualKeys;
    for (const TSharedPtr<FJsonValue>& Value : Actual)
    {
        const TSharedPtr<FJsonObject> Link = Value->AsObject();
        if (Link.IsValid() == false)
        {
            return false;
        }
        ActualKeys.Add(Link->GetStringField(TEXT("source_node_id")) + TEXT("/") +
            Link->GetStringField(TEXT("source_pin_id")) + TEXT("/") +
            Link->GetStringField(TEXT("target_node_id")) + TEXT("/") +
            Link->GetStringField(TEXT("target_pin_id")));
    }
    if (ActualKeys.Num() != Expected.Num())
    {
        return false;
    }
    for (const FBPIRLink& Link : Expected)
    {
        const FString Key = Link.SourceNode.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("/") +
            Link.SourcePin.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("/") +
            Link.TargetNode.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("/") +
            Link.TargetPin.ToString(EGuidFormats::DigitsWithHyphens);
        if (ActualKeys.Contains(Key) == false)
        {
            return false;
        }
    }
    return true;
}

// JSONのPin配列について、IR側のID・名前・方向・型・既定値を照合する。
// 並び順はExporterがGUID順に正規化するため、位置ではなくIDで比較する。
bool CheckJsonNodesAndPins(const FBPIRGraph& Graph, const TSharedPtr<FJsonObject>& JsonGraph)
{
    const TArray<TSharedPtr<FJsonValue>>& Nodes = JsonGraph->GetArrayField(TEXT("nodes"));
    if (Nodes.Num() != Graph.Nodes.Num())
    {
        return false;
    }
    TMap<FString, const FBPIRNode*> ExpectedNodes;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        ExpectedNodes.Add(Node.Id.ToString(EGuidFormats::DigitsWithHyphens), &Node);
    }
    TSet<FString> SeenNodes;
    for (const TSharedPtr<FJsonValue>& Value : Nodes)
    {
        const TSharedPtr<FJsonObject> JsonNode = Value->AsObject();
        if (JsonNode.IsValid() == false)
        {
            return false;
        }
        const FString NodeId = JsonNode->GetStringField(TEXT("id"));
        const FBPIRNode* const* ExpectedNode = ExpectedNodes.Find(NodeId);
        if (ExpectedNode == nullptr || SeenNodes.Contains(NodeId))
        {
            return false;
        }
        SeenNodes.Add(NodeId);
        if (JsonNode->GetStringField(TEXT("class_path")) != (*ExpectedNode)->ClassPath ||
            JsonNode->GetStringField(TEXT("function_name")) != (*ExpectedNode)->FunctionName.ToString() ||
            JsonNode->GetStringField(TEXT("macro_graph_name")) != (*ExpectedNode)->MacroGraphName)
        {
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>& Pins = JsonNode->GetArrayField(TEXT("pins"));
        if (Pins.Num() != (*ExpectedNode)->Pins.Num())
        {
            return false;
        }
        TMap<FString, const FBPIRPin*> ExpectedPins;
        for (const FBPIRPin& Pin : (*ExpectedNode)->Pins)
        {
            ExpectedPins.Add(Pin.Id.ToString(EGuidFormats::DigitsWithHyphens), &Pin);
        }
        TSet<FString> SeenPins;
        for (const TSharedPtr<FJsonValue>& PinValue : Pins)
        {
            const TSharedPtr<FJsonObject> JsonPin = PinValue->AsObject();
            if (JsonPin.IsValid() == false)
            {
                return false;
            }
            const FString PinId = JsonPin->GetStringField(TEXT("id"));
            const FBPIRPin* const* ExpectedPin = ExpectedPins.Find(PinId);
            if (ExpectedPin == nullptr || SeenPins.Contains(PinId))
            {
                return false;
            }
            SeenPins.Add(PinId);
            if (JsonPin->GetStringField(TEXT("name")) != (*ExpectedPin)->Name.ToString() ||
                JsonPin->GetStringField(TEXT("category")) != (*ExpectedPin)->Category.ToString() ||
                JsonPin->GetStringField(TEXT("subcategory")) != (*ExpectedPin)->SubCategory.ToString() ||
                JsonPin->GetStringField(TEXT("default_value")) != (*ExpectedPin)->DefaultValue ||
                JsonPin->GetStringField(TEXT("direction")) !=
                    ((*ExpectedPin)->Direction == EGPD_Input ? TEXT("input") : TEXT("output")))
            {
                return false;
            }
        }
    }
    return SeenNodes.Num() == Graph.Nodes.Num();
}

bool GenerateFixture(FAutomationTestBase& Test, const bool bForEach)
{
    const FString AssetName = bForEach ? TEXT("BP_Test_ForEachLoop") : TEXT("BP_Test_FunctionCall");
    const FString PackagePath = TEXT("/Game/BlueprintToCppTests/") + AssetName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (Test.TestNotNull(TEXT("パッケージ"), Package) == false)
    {
        return false;
    }
    UBlueprint* Blueprint = FindObject<UBlueprint>(Package, *AssetName);
    if (Blueprint == nullptr)
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(), Package, FName(*AssetName),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }
    if (Test.TestNotNull(TEXT("Blueprint"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (Test.TestNotNull(TEXT("EventGraph"), Graph) == false)
    {
        return false;
    }

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
    if (Test.TestNotNull(TEXT("PrintString UFunction"), PrintFunction) == false)
    {
        return false;
    }

    if (bForEach)
    {
        // ForEachLoopはWildcard配列入力の型確定が必要なため、int32配列変数を接続する。
        FEdGraphPinType ArrayType;
        ArrayType.PinCategory = UEdGraphSchema_K2::PC_Int;
        ArrayType.ContainerType = EPinContainerType::Array;
        if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, TEXT("Numbers")) == INDEX_NONE)
        {
            if (Test.TestTrue(TEXT("Numbers配列を追加"),
                FBlueprintEditorUtils::AddMemberVariable(
                    Blueprint, TEXT("Numbers"), ArrayType, TEXT("(10,20,30)"))) == false)
            {
                return false;
            }
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr,
            TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForEachLoop"));
        if (Test.TestNotNull(TEXT("標準ForEachLoop"), MacroGraph) == false)
        {
            return false;
        }
        UK2Node_MacroInstance* Loop = AddFixtureNode<UK2Node_MacroInstance>(Graph);
        Loop->SetMacroGraph(MacroGraph);
        Loop->AllocateDefaultPins();
        UK2Node_VariableGet* Numbers = AddFixtureNode<UK2Node_VariableGet>(Graph);
        Numbers->VariableReference.SetSelfMember(TEXT("Numbers"));
        Numbers->AllocateDefaultPins();
        UK2Node_CallFunction* Body = AddPrint(Graph, PrintFunction, TEXT("ForEach body"));
        UK2Node_CallFunction* Done = AddPrint(Graph, PrintFunction, TEXT("ForEach completed"));
        if (Test.TestTrue(TEXT("BeginPlay -> ForEach"), ConnectPins(Graph, Begin,
                UEdGraphSchema_K2::PN_Then, Loop, TEXT("Exec"))) == false ||
            Test.TestTrue(TEXT("Numbers -> Array"), ConnectPins(Graph, Numbers,
                TEXT("Numbers"), Loop, TEXT("Array"))) == false ||
            Test.TestTrue(TEXT("LoopBody -> Print"), ConnectPins(Graph, Loop,
                TEXT("LoopBody"), Body, UEdGraphSchema_K2::PN_Execute)) == false ||
            Test.TestTrue(TEXT("Completed -> Print"), ConnectPins(Graph, Loop,
                TEXT("Completed"), Done, UEdGraphSchema_K2::PN_Execute)) == false)
        {
            return false;
        }
    }
    else
    {
        // 引数付きネイティブ関数呼び出しを検証する最小の実Blueprint。
        UK2Node_CallFunction* Print = AddPrint(
            Graph, PrintFunction, TEXT("Function call fixture"));
        Print->FindPinChecked(TEXT("Key"))->DefaultValue = TEXT("function-call");
        if (Test.TestTrue(TEXT("BeginPlay -> PrintString"), ConnectPins(Graph, Begin,
            UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute)) == false)
        {
            return false;
        }
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Test.TestNotNull(TEXT("GeneratedClass"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    FBPGraphIR IR;
    if (Test.TestTrue(TEXT("Graph IR生成"), FBPGraphIRBuilder::Build(Blueprint, IR)) == false)
    {
        return false;
    }
    const FBPIRGraph* Event = IR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Item) { return Item.Kind == TEXT("event"); });
    if (Test.TestNotNull(TEXT("Event IR"), Event) == false)
    {
        return false;
    }
    Test.TestEqual(TEXT("期待ノード数"), Event->Nodes.Num(), bForEach ? 5 : 2);
    Test.TestEqual(TEXT("期待Exec接続数"), Event->ExecEdges.Num(), bForEach ? 3 : 1);
    Test.TestEqual(TEXT("期待Data接続数"), Event->DataEdges.Num(), bForEach ? 1 : 0);

    FString JsonPath;
    FString Error;
    if (Test.TestTrue(TEXT("Graph JSON出力"), FBPCopilotGraphJsonExporter::Export(
        IR, FEngineVersion::Current().ToString(), JsonPath, Error)) == false)
    {
        Test.AddError(Error);
        return false;
    }
    FString Json;
    TSharedPtr<FJsonObject> Root;
    if (Test.TestTrue(TEXT("JSON読込"), FFileHelper::LoadFileToString(Json, *JsonPath)) == false ||
        Test.TestTrue(TEXT("JSON解析"), FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Json), Root)) == false || Root.IsValid() == false)
    {
        return false;
    }
    bool bFoundGraph = false;
    for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("graphs")))
    {
        const TSharedPtr<FJsonObject> JsonGraph = Value->AsObject();
        if (JsonGraph.IsValid() == false || JsonGraph->GetStringField(TEXT("kind")) != TEXT("event"))
        {
            continue;
        }
        bFoundGraph = true;
        Test.TestEqual(TEXT("JSONノード数"), JsonGraph->GetArrayField(TEXT("nodes")).Num(),
            bForEach ? 5 : 2);
        Test.TestTrue(TEXT("ノードとPinの属性完全一致"),
            CheckJsonNodesAndPins(*Event, JsonGraph));
        Test.TestTrue(TEXT("Exec接続のNode/Pin完全一致"),
            CheckJsonLinks(*Event, JsonGraph, TEXT("exec_edges")));
        Test.TestTrue(TEXT("Data接続のNode/Pin完全一致"),
            CheckJsonLinks(*Event, JsonGraph, TEXT("data_edges")));
    }
    if (Test.TestTrue(TEXT("Event Graph JSON存在"), bFoundGraph) == false)
    {
        return false;
    }

    if (bForEach == false)
    {
        // 実際のBP由来の関数呼び出しをLoweringからC++生成まで通す。
        // ForEachは専用Lowering経路を使用するため、関数呼び出しとは検証を分離する。
        FBPSemanticIR Semantic;
        if (Test.TestTrue(TEXT("関数呼び出しLowering"),
            FBPSemanticLowering::Lower(Blueprint, IR, Semantic)) == false)
        {
            for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics)
            {
                Test.AddError(Diagnostic.Message);
            }
            return false;
        }
        FBPGeneratedActorFiles Generated;
        TArray<FString> Diagnostics;
        if (Test.TestTrue(TEXT("関数呼び出しC++生成"),
            FBPSemanticCodeGenerator::Generate(Semantic,
                TEXT("AGeneratedFunctionCall"), FString(), Generated, Diagnostics)) == false)
        {
            for (const FString& Diagnostic : Diagnostics)
            {
                Test.AddError(Diagnostic);
            }
            return false;
        }
        Test.TestTrue(TEXT("PrintString呼び出しを保持"),
            Generated.SourceContent.Contains(TEXT("UKismetSystemLibrary::PrintString")));
        Test.TestTrue(TEXT("文字列引数を保持"),
            Generated.SourceContent.Contains(TEXT("Function call fixture")));
    }

    // 既存のForEach Loweringは対応範囲が限定的なため、ここでは保存とGraph JSONの
    // 厳密検証を保証し、Semantic/C++の網羅は独立した変換テストに委ねる。
    Blueprint->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (Test.TestTrue(TEXT("uasset保存"),
        UPackage::SavePackage(Package, Blueprint, *Filename, Args)) == false)
    {
        return false;
    }
    Test.AddInfo(FString::Printf(TEXT("GENERATED_BLUEPRINT=%s"), *Filename));
    Test.AddInfo(FString::Printf(TEXT("GENERATED_GRAPH_JSON=%s"), *JsonPath));
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedFunctionCallAssetTest,
    "BlueprintAIConverter.GeneratedAssets.FunctionCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedFunctionCallAssetTest::RunTest(const FString& Parameters)
{
    return GenerateFixture(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedForEachAssetTest,
    "BlueprintAIConverter.GeneratedAssets.ForEachLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedForEachAssetTest::RunTest(const FString& Parameters)
{
    return GenerateFixture(*this, true);
}

#endif
