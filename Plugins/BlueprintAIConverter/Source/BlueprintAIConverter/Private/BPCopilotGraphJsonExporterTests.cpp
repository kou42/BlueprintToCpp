#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPCopilotGraphJsonExporter.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCopilotGraphJsonExporterTest,
    "BlueprintAIConverter.CopilotJson.GraphExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPCopilotGraphJsonExporterTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_CopilotExportTest.BP_CopilotExportTest");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Path = TEXT("/Game/BP_CopilotExportTest.EventGraph");
    Graph.Kind = TEXT("event");

    FBPIRNode Node;
    Node.Id = FGuid(1, 2, 3, 4);
    Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Node.FunctionName = TEXT("PrintString");
    Node.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");
    Node.FunctionPath = TEXT("/Script/Engine.KismetSystemLibrary:PrintString");
    Node.bFunctionResolved = true;
    Node.bFunctionNative = true;
    FBPIRPin Pin;
    Pin.Id = FGuid(5, 6, 7, 8);
    Pin.Name = TEXT("InString");
    Pin.Category = TEXT("string");
    Pin.DefaultValue = TEXT("");
    Node.Pins.Add(Pin);
    Graph.Nodes.Add(Node);
    IR.Graphs.Add(Graph);

    FString Path;
    FString Error;
    const bool bExported = FBPCopilotGraphJsonExporter::Export(
        IR, TEXT("5.7.4"), Path, Error);
    TestTrue(TEXT("Copilot JSONを書き出す"), bExported);
    if (bExported == false)
    {
        AddError(Error);
        return false;
    }

    FString Json;
    const bool bLoaded = FFileHelper::LoadFileToString(Json, *Path);
    // テスト生成物は読み込み後に削除し、ユーザーのSavedフォルダに残さない。
    IFileManager::Get().Delete(*Path);
    TestTrue(TEXT("出力したJSONを読み込める"), bLoaded);
    if (bLoaded == false)
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    const bool bParsed = FJsonSerializer::Deserialize(Reader, Root);
    TestTrue(TEXT("JSON構文が正しい"), bParsed);
    if (bParsed == false || Root.IsValid() == false)
    {
        return false;
    }
    TestEqual(TEXT("形式"), Root->GetStringField(TEXT("format")),
        FString(TEXT("blueprint_copilot_graph")));
    TestEqual(TEXT("解析状態"), Root->GetStringField(TEXT("semantic_status")),
        FString(TEXT("partial")));
    const TArray<TSharedPtr<FJsonValue>>& Graphs = Root->GetArrayField(TEXT("graphs"));
    if (Graphs.Num() != 1)
    {
        AddError(TEXT("グラフ数が一致しません"));
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>& Nodes = Graphs[0]->AsObject()->GetArrayField(TEXT("nodes"));
    if (Nodes.Num() != 1)
    {
        AddError(TEXT("ノード数が一致しません"));
        return false;
    }
    const TSharedPtr<FJsonObject> SavedNode = Nodes[0]->AsObject();
    TestEqual(TEXT("正式な関数参照"), SavedNode->GetStringField(TEXT("function_path")),
        Node.FunctionPath);
    TestTrue(TEXT("解決状態"), SavedNode->GetBoolField(TEXT("function_resolved")));
    const TArray<TSharedPtr<FJsonValue>>& Pins = SavedNode->GetArrayField(TEXT("pins"));
    if (Pins.Num() != 1)
    {
        AddError(TEXT("Pin数が一致しません"));
        return false;
    }
    TestEqual(TEXT("Pinの空文字既定値"), Pins[0]->AsObject()->GetStringField(TEXT("default_value")),
        FString(TEXT("")));
    return true;
}

#endif
