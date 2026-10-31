#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "BPGraphIR.h"
#include "BPSemanticClassExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "BPSemanticJsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace BPSemanticTestUtils
{
// Copy the successful IR so export metadata cannot affect subsequent generator checks.
inline bool ExportRoundtrip(FAutomationTestBase& Test, const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR IR)
{
    FBPSemanticGraphTraceExtractor::Append(GraphIR, IR);
    if (Test.TestTrue(TEXT("クラス情報を抽出"),
            FBPSemanticClassExtractor::Append(Blueprint, IR)) == false) { return false; }
    const FString EngineVersion = FEngineVersion::Current().ToString();
    FString Serialized;
    if (Test.TestTrue(TEXT("実IRをシリアライズ"),
            FBPSemanticJsonSerializer::Serialize(IR, EngineVersion, Serialized)) == false) { return false; }
    FString ExportPath;
    FString Error;
    if (Test.TestTrue(TEXT("実JSONをSavedへ保存"),
            FBPSemanticJsonExporter::Export(IR, EngineVersion, ExportPath, Error)) == false)
    {
        Test.AddError(Error);
        return false;
    }
    FString Exported;
    if (Test.TestTrue(TEXT("保存JSONを再読込"),
            FFileHelper::LoadFileToString(Exported, *ExportPath)) == false) { return false; }
    if (Test.TestEqual(TEXT("保存前後でJSONを維持"), Exported, Serialized) == false) { return false; }
    TSharedPtr<FJsonObject> Root;
    if (Test.TestTrue(TEXT("保存JSONを再解析"), FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Exported), Root)) == false || Root.IsValid() == false) { return false; }
    bool bValid = Test.TestEqual(TEXT("JSONに関数数を保持"),
        Root->GetArrayField(TEXT("functions")).Num(), IR.Functions.Num());
    bValid &= Test.TestEqual(TEXT("JSONに変数数を保持"),
        Root->GetArrayField(TEXT("variables")).Num(), IR.Variables.Num());
    bValid &= Test.TestEqual(TEXT("JSONに全グラフの由来を保持"),
        Root->GetArrayField(TEXT("graph_traces")).Num(), GraphIR.Graphs.Num());
    bValid &= Test.TestEqual(TEXT("JSONに親クラスを保持"),
        Root->GetObjectField(TEXT("source"))->GetStringField(TEXT("parent_class_path")), GraphIR.ParentClassPath);
    if (bValid)
    {
        Test.AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));
    }
    return bValid;
}
}

#endif
