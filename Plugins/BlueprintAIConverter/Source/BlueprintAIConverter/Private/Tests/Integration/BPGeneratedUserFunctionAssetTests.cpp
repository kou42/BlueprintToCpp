#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPCopilotGraphJsonExporter.h"
#include "BPGraphIR.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticIR.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedUserFunctionAssetTest,
    "BlueprintAIConverter.GeneratedAssets.UserFunction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedUserFunctionAssetTest::RunTest(const FString& Parameters)
{
    const FString AssetName = TEXT("BP_Test_UserFunction");
    const FString PackagePath = TEXT("/Game/BlueprintToCppTests/") + AssetName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (TestNotNull(TEXT("保存先パッケージ"), Package) == false)
    {
        return false;
    }
    // 既存BPを再利用し、ユーザー定義関数を重複作成しない。
    UBlueprint* Blueprint = LoadObject<UBlueprint>(
        nullptr, *(PackagePath + TEXT(".") + AssetName));
    if (Blueprint == nullptr)
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(), Package, FName(*AssetName),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }
    if (TestNotNull(TEXT("Blueprint"), Blueprint) == false)
    {
        return false;
    }

    const FName FunctionName(TEXT("ComputeValue"));
    UEdGraph* FunctionGraph = nullptr;
    for (UEdGraph* Existing : Blueprint->FunctionGraphs)
    {
        if (Existing != nullptr && Existing->GetFName() == FunctionName)
        {
            FunctionGraph = Existing;
            break;
        }
    }
    if (FunctionGraph == nullptr)
    {
        FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint, FunctionName, UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
            Blueprint, FunctionGraph, true, nullptr);
    }
    if (TestNotNull(TEXT("ユーザー定義関数Graph"), FunctionGraph) == false)
    {
        return false;
    }

    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
    {
        if (Entry == nullptr)
        {
            Entry = Cast<UK2Node_FunctionEntry>(Node);
        }
        if (Result == nullptr)
        {
            Result = Cast<UK2Node_FunctionResult>(Node);
        }
    }
    if (TestNotNull(TEXT("Function Entry"), Entry) == false)
    {
        return false;
    }
    if (Result == nullptr)
    {
        Result = NewObject<UK2Node_FunctionResult>(FunctionGraph);
        FunctionGraph->AddNode(Result, false, false);
        Result->CreateNewGuid();
        Result->PostPlacedNewNode();
        Result->FunctionReference.SetSelfMember(FunctionName);
        Result->AllocateDefaultPins();
    }

    FEdGraphPinType IntegerType;
    IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
    // ユーザー定義PinはFunction Entryが入力引数、Resultが戻り値を定義する。
    if (Entry->FindPin(TEXT("InputValue")) == nullptr)
    {
        Entry->CreateUserDefinedPin(TEXT("InputValue"), IntegerType, EGPD_Output);
    }
    if (Result->FindPin(TEXT("ReturnValue")) == nullptr)
    {
        Result->CreateUserDefinedPin(TEXT("ReturnValue"), IntegerType, EGPD_Input);
    }
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(FunctionGraph->GetSchema());
    if (TestNotNull(TEXT("K2 Schema"), Schema) == false)
    {
        return false;
    }
    UEdGraphPin* EntryThen = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ResultExec = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* Input = Entry->FindPin(TEXT("InputValue"));
    UEdGraphPin* Output = Result->FindPin(TEXT("ReturnValue"));
    if (TestNotNull(TEXT("Entry Then"), EntryThen) == false ||
        TestNotNull(TEXT("Result Exec"), ResultExec) == false ||
        TestNotNull(TEXT("InputValue"), Input) == false ||
        TestNotNull(TEXT("ReturnValue"), Output) == false)
    {
        return false;
    }
    if (EntryThen->LinkedTo.Contains(ResultExec) == false)
    {
        if (TestTrue(TEXT("Entry -> Result"),
            Schema->TryCreateConnection(EntryThen, ResultExec)) == false)
        {
            return false;
        }
    }
    if (Input->LinkedTo.Contains(Output) == false)
    {
        if (TestTrue(TEXT("入力値を戻り値へ接続"),
            Schema->TryCreateConnection(Input, Output)) == false)
        {
            return false;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("関数BPコンパイルエラーなし"),
        Blueprint->Status != BS_Error) == false ||
        TestNotNull(TEXT("生成クラス"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    const UFunction* Reflected = Blueprint->GeneratedClass->FindFunctionByName(FunctionName);
    if (TestNotNull(TEXT("生成クラスにComputeValueが存在"), Reflected) == false)
    {
        return false;
    }

    // ユーザー関数のシグネチャが確定した後にEvent Graphへ呼び出しを配置する。
    // 先に反射済みUFunctionを取得することで、引数・戻り値Pinの型を手動複製しない。
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("Event Graph"), EventGraph) == false)
    {
        return false;
    }
    UK2Node_Event* BeginPlay = nullptr;
    UK2Node_CallFunction* Call = nullptr;
    for (UEdGraphNode* Node : EventGraph->Nodes)
    {
        UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
        if (Event != nullptr && Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
        {
            BeginPlay = Event;
        }
        UK2Node_CallFunction* Candidate = Cast<UK2Node_CallFunction>(Node);
        if (Candidate != nullptr && Candidate->FunctionReference.GetMemberName() == FunctionName)
        {
            Call = Candidate;
        }
    }
    if (BeginPlay == nullptr)
    {
        BeginPlay = NewObject<UK2Node_Event>(EventGraph);
        EventGraph->AddNode(BeginPlay, false, false);
        BeginPlay->CreateNewGuid();
        BeginPlay->PostPlacedNewNode();
        BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
        BeginPlay->bOverrideFunction = true;
        BeginPlay->AllocateDefaultPins();
    }
    if (Call == nullptr)
    {
        Call = NewObject<UK2Node_CallFunction>(EventGraph);
        EventGraph->AddNode(Call, false, false);
        Call->CreateNewGuid();
        Call->PostPlacedNewNode();
        Call->SetFromFunction(Reflected);
        Call->AllocateDefaultPins();
    }
    const UEdGraphSchema_K2* EventSchema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    UEdGraphPin* BeginThen = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* CallExec = Call->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* CallInput = Call->FindPin(TEXT("InputValue"));
    UEdGraphPin* CallReturn = Call->FindPin(TEXT("ReturnValue"));
    if (TestNotNull(TEXT("Event Schema"), EventSchema) == false ||
        TestNotNull(TEXT("BeginPlay Then"), BeginThen) == false ||
        TestNotNull(TEXT("ComputeValue Exec"), CallExec) == false ||
        TestNotNull(TEXT("ComputeValue InputValue"), CallInput) == false ||
        TestNotNull(TEXT("ComputeValue ReturnValue"), CallReturn) == false)
    {
        return false;
    }
    if (BeginThen->LinkedTo.Contains(CallExec) == false)
    {
        if (TestTrue(TEXT("BeginPlay -> ComputeValue"),
            EventSchema->TryCreateConnection(BeginThen, CallExec)) == false)
        {
            return false;
        }
    }
    // 戻り値をメンバー変数に格納し、呼び出し結果が未使用にならない実BPを作る。
    // 再実行時は既存変数・Setterを再利用して同じ接続を維持する。
    const FName ResultVariable(TEXT("LastComputedValue"));
    if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, ResultVariable) == INDEX_NONE)
    {
        FEdGraphPinType ResultType;
        ResultType.PinCategory = UEdGraphSchema_K2::PC_Int;
        if (TestTrue(TEXT("戻り値格納変数を追加"),
            FBlueprintEditorUtils::AddMemberVariable(
                Blueprint, ResultVariable, ResultType, TEXT("0"))) == false)
        {
            return false;
        }
    }
    UK2Node_VariableSet* Setter = nullptr;
    for (UEdGraphNode* Node : EventGraph->Nodes)
    {
        UK2Node_VariableSet* Candidate = Cast<UK2Node_VariableSet>(Node);
        if (Candidate != nullptr &&
            Candidate->VariableReference.GetMemberName() == ResultVariable)
        {
            Setter = Candidate;
            break;
        }
    }
    if (Setter == nullptr)
    {
        Setter = NewObject<UK2Node_VariableSet>(EventGraph);
        EventGraph->AddNode(Setter, false, false);
        Setter->CreateNewGuid();
        Setter->PostPlacedNewNode();
        Setter->VariableReference.SetSelfMember(ResultVariable);
        Setter->AllocateDefaultPins();
    }
    UEdGraphPin* CallThen = Call->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* SetExec = Setter->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* SetValue = Setter->FindPin(ResultVariable);
    if (TestNotNull(TEXT("ComputeValue Then"), CallThen) == false ||
        TestNotNull(TEXT("Set LastComputedValue Exec"), SetExec) == false ||
        TestNotNull(TEXT("Set LastComputedValue入力"), SetValue) == false)
    {
        return false;
    }
    if (CallThen->LinkedTo.Contains(SetExec) == false)
    {
        if (TestTrue(TEXT("ComputeValue -> Setter"),
            EventSchema->TryCreateConnection(CallThen, SetExec)) == false)
        {
            return false;
        }
    }
    if (CallReturn->LinkedTo.Contains(SetValue) == false)
    {
        if (TestTrue(TEXT("ReturnValue -> LastComputedValue"),
            EventSchema->TryCreateConnection(CallReturn, SetValue)) == false)
        {
            return false;
        }
    }
    // 引数は明示的な整数リテラルとして渡す。戻り値は型とPinの存在を
    // 検証し、後続の消費ノードを追加する段階でデータリンクも検証する。
    CallInput->DefaultValue = TEXT("42");
    TestEqual(TEXT("戻り値Pin型"), CallReturn->PinType.PinCategory,
        UEdGraphSchema_K2::PC_Int);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("関数呼び出し追加後のコンパイル"),
        Blueprint->Status != BS_Error) == false)
    {
        return false;
    }

    FBPSemanticIR Semantic;
    FBPSemanticFunctionExtractor::Append(Blueprint, Semantic);
    const FBPSemanticFunction* Function = Semantic.Functions.FindByPredicate(
        [FunctionName](const FBPSemanticFunction& Item)
        {
            return Item.Name == FunctionName;
        });
    if (TestNotNull(TEXT("Semantic IRにユーザー関数が存在"), Function) == false)
    {
        return false;
    }
    TestEqual(TEXT("関数パラメータ数（戻り値を含む）"), Function->Parameters.Num(), 2);
    const FBPSemanticParameter* InputParameter = Function->Parameters.FindByPredicate(
        [](const FBPSemanticParameter& Parameter)
        {
            return Parameter.bIsReturn == false;
        });
    const FBPSemanticParameter* ReturnParameter = Function->Parameters.FindByPredicate(
        [](const FBPSemanticParameter& Parameter)
        {
            return Parameter.bIsReturn;
        });
    if (TestNotNull(TEXT("関数入力パラメータ"), InputParameter) != false)
    {
        TestEqual(TEXT("関数引数名"), InputParameter->Name, FName(TEXT("InputValue")));
    }
    TestNotNull(TEXT("関数戻り値パラメータ"), ReturnParameter);

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("関数Graph IR生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    const FBPIRGraph* IRFunction = GraphIR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Item)
        {
            return Item.Kind == TEXT("function") && Item.Name == TEXT("ComputeValue");
        });
    if (TestNotNull(TEXT("関数Graphを抽出"), IRFunction) == false)
    {
        return false;
    }
    TestEqual(TEXT("Entry/Resultノード数"), IRFunction->Nodes.Num(), 2);
    TestEqual(TEXT("関数Exec接続数"), IRFunction->ExecEdges.Num(), 1);
    TestEqual(TEXT("引数から戻り値への接続数"), IRFunction->DataEdges.Num(), 1);
    const FBPIRGraph* IREvent = GraphIR.Graphs.FindByPredicate(
        [](const FBPIRGraph& Item)
        {
            return Item.Kind == TEXT("event");
        });
    if (TestNotNull(TEXT("Event Graph IR"), IREvent) == false)
    {
        return false;
    }
    TestTrue(TEXT("Event GraphにComputeValue呼び出しが存在"),
        IREvent->Nodes.ContainsByPredicate(
            [FunctionName](const FBPIRNode& Node)
            {
                return Node.FunctionName == FunctionName;
            }));
    TestEqual(TEXT("BeginPlayから関数・SetterへのExec接続数"), IREvent->ExecEdges.Num(), 2);
    TestEqual(TEXT("戻り値からSetterへのData接続数"), IREvent->DataEdges.Num(), 1);


    // Copilot JSONでも関数呼び出しのExec/Data接続が失われていないことを確認する。
    FString JsonPath;
    FString JsonError;
    if (TestTrue(TEXT("ユーザー関数BPのCopilot Graph JSONを出力"),
        FBPCopilotGraphJsonExporter::Export(
            GraphIR, FEngineVersion::Current().ToString(), JsonPath, JsonError)) == false)
    {
        AddError(JsonError);
        return false;
    }
    FString JsonText;
    TSharedPtr<FJsonObject> JsonRoot;
    if (TestTrue(TEXT("JSONファイル読込"),
        FFileHelper::LoadFileToString(JsonText, *JsonPath)) == false ||
        TestTrue(TEXT("JSONパース"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), JsonRoot) &&
            JsonRoot.IsValid()) == false)
    {
        return false;
    }
    bool bFoundEvent = false;
    bool bFoundFunction = false;
    for (const TSharedPtr<FJsonValue>& GraphValue : JsonRoot->GetArrayField(TEXT("graphs")))
    {
        const TSharedPtr<FJsonObject> JsonGraph = GraphValue->AsObject();
        if (JsonGraph.IsValid() == false)
        {
            continue;
        }
        const FString Name = JsonGraph->GetStringField(TEXT("name"));
        const FBPIRGraph* Expected = GraphIR.Graphs.FindByPredicate(
            [&Name](const FBPIRGraph& Graph)
            {
                return Graph.Name == Name;
            });
        if (Expected == nullptr)
        {
            AddError(TEXT("JSONに不明なGraphが含まれています"));
            return false;
        }
        const auto CheckEdges = [&JsonGraph](
            const TCHAR* Field, const TArray<FBPIRLink>& Edges)
        {
            const TArray<TSharedPtr<FJsonValue>>& JsonEdges =
                JsonGraph->GetArrayField(Field);
            if (JsonEdges.Num() != Edges.Num())
            {
                return false;
            }
            TSet<FString> Keys;
            for (const TSharedPtr<FJsonValue>& Value : JsonEdges)
            {
                const TSharedPtr<FJsonObject> Edge = Value->AsObject();
                if (Edge.IsValid() == false)
                {
                    return false;
                }
                Keys.Add(Edge->GetStringField(TEXT("source_node_id")) + TEXT("/") +
                    Edge->GetStringField(TEXT("source_pin_id")) + TEXT("/") +
                    Edge->GetStringField(TEXT("target_node_id")) + TEXT("/") +
                    Edge->GetStringField(TEXT("target_pin_id")));
            }
            for (const FBPIRLink& Edge : Edges)
            {
                const FString Key =
                    Edge.SourceNode.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("/") +
                    Edge.SourcePin.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("/") +
                    Edge.TargetNode.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("/") +
                    Edge.TargetPin.ToString(EGuidFormats::DigitsWithHyphens);
                if (Keys.Contains(Key) == false)
                {
                    return false;
                }
            }
            return Keys.Num() == Edges.Num();
        };
        if (TestTrue(TEXT("JSON ExecエッジGUID一致"),
            CheckEdges(TEXT("exec_edges"), Expected->ExecEdges)) == false ||
            TestTrue(TEXT("JSON DataエッジGUID一致"),
                CheckEdges(TEXT("data_edges"), Expected->DataEdges)) == false)
        {
            return false;
        }
        bFoundEvent |= Expected == IREvent;
        bFoundFunction |= Expected == IRFunction;
    }
    if (TestTrue(TEXT("JSONにEvent/Function両Graphを保持"),
        bFoundEvent && bFoundFunction) == false)
    {
        return false;
    }
    Blueprint->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (TestTrue(TEXT("ユーザー定義関数BP保存"),
        UPackage::SavePackage(Package, Blueprint, *Filename, Args)) == false)
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("GENERATED_BLUEPRINT=%s"), *Filename));
    AddInfo(FString::Printf(TEXT("GENERATED_JSON=%s"), *JsonPath));
    return true;
}

#endif
