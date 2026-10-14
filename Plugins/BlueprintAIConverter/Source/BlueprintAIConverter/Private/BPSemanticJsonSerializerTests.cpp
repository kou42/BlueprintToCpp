#include "Misc/AutomationTest.h"
#include "BPSemanticJsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticJsonSerializerTest,
    "BlueprintAIConverter.SemanticJson.SerializeV2",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticJsonSerializerTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR IR;
    IR.AssetPath = TEXT("/Game/BP_Test.BP_Test");
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Bool;
    Function.OwnerClassPath = TEXT("/Game/BP_Test.BP_Test_C");
    Function.SignatureSource = TEXT("generated_class_reflection");
    Function.bIsNative = true;
    Function.bIsStatic = true;
    FBPSemanticParameter Input;
    Input.Name = TEXT("Amount");
    Input.Type.Kind = EBPSemanticTypeKind::Int32;
    Input.bHasDefaultValue = true;
    Input.DefaultValue = TEXT("0");
    Function.Parameters.Add(Input);
    FBPSemanticParameter Output;
    Output.Name = TEXT("ReturnValue");
    Output.Type.Kind = EBPSemanticTypeKind::Bool;
    Output.bIsOut = true;
    Output.bIsReturn = true;
    Function.Parameters.Add(Output);
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = FGuid(1, 2, 3, 4);
    FBPSemanticStatement Statement;
    Statement.SourceNodeId = FGuid(5, 6, 7, 8);
    Statement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Statement.Expression.Type.Kind = EBPSemanticTypeKind::Void;
    Statement.Expression.FunctionName = TEXT("PrintString");

    FBPSemanticExpression Argument;
    Argument.Kind = EBPSemanticExpressionKind::Literal;
    Argument.Type.Kind = EBPSemanticTypeKind::String;
    Argument.LiteralValue = TEXT("");
    Argument.SourcePinId = FGuid(9, 10, 11, 12);
    Statement.Expression.Arguments.Add(Argument);
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);

    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Health");
    Variable.Guid = FGuid(10, 11, 12, 13);
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.PinCategory = TEXT("int");
    Variable.PropertyFlags = TEXT("0");
    IR.Variables.Add(Variable);
    FBPSemanticVariable Earlier;
    Earlier.Name = TEXT("Ammo");
    Earlier.Type.Kind = EBPSemanticTypeKind::Bool;
    Earlier.PinCategory = TEXT("bool");
    IR.Variables.Add(Earlier);

    FBPSemanticGraphTrace Trace;
    Trace.Name = TEXT("EventGraph");
    Trace.Path = TEXT("/Game/BP_Test.EventGraph");
    Trace.Kind = TEXT("ubergraph");
    Trace.NodeIds.Add(FGuid(1, 2, 3, 4));
    Trace.NodeIds.Add(FGuid(5, 6, 7, 8));
    Trace.PinIds.Add(FGuid(9, 10, 11, 12));
    Trace.PinIds.Add(FGuid(13, 14, 15, 16));
    FBPIRLink Link;
    Link.SourceNode = Trace.NodeIds[0];
    Link.TargetNode = Trace.NodeIds[1];
    Link.SourcePin = Trace.PinIds[0];
    Link.TargetPin = Trace.PinIds[1];
    Trace.ExecEdges.Add(Link);
    Trace.DataEdges.Add(Link);
    IR.GraphTraces.Add(MoveTemp(Trace));

    FString JsonText;
    TestTrue(TEXT("JSON v2を生成"), FBPSemanticJsonSerializer::Serialize(IR, TEXT("5.7.4"), JsonText));
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    TestTrue(TEXT("JSONとして再解析可能"), FJsonSerializer::Deserialize(Reader, Root));
    if (Root == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("スキーマバージョン"), static_cast<int32>(Root->GetNumberField(TEXT("schema_version"))), 2);
    TestEqual(TEXT("未検証範囲をpartialとする"), Root->GetStringField(TEXT("semantic_status")),
        FString(TEXT("partial")));

    const TArray<TSharedPtr<FJsonValue>>& Variables = Root->GetArrayField(TEXT("variables"));
    TestEqual(TEXT("変数数"), Variables.Num(), 2);
    TestEqual(TEXT("変数の安定ソート"), Variables[0]->AsObject()->GetStringField(TEXT("name")),
        FString(TEXT("Ammo")));
    TestEqual(TEXT("変数名"), Variables[1]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("Health")));
    TestEqual(TEXT("初期値の出典"), Variables[0]->AsObject()->GetStringField(TEXT("default_value_source")),
        FString(TEXT("unavailable")));
    TestFalse(TEXT("CDO初期値は未取得"), Variables[0]->AsObject()->GetBoolField(TEXT("default_value_available")));
    TestTrue(TEXT("未取得値はnull"),
        Variables[0]->AsObject()->GetField<EJson::Null>(TEXT("default_value"))->IsNull());

    const TArray<TSharedPtr<FJsonValue>>& GraphTraces = Root->GetArrayField(TEXT("graph_traces"));
    TestEqual(TEXT("グラフ追跡数"), GraphTraces.Num(), 1);
    TestEqual(TEXT("Exec接続数"), GraphTraces[0]->AsObject()->GetArrayField(TEXT("exec_edges")).Num(), 1);
    TestEqual(TEXT("Data接続数"), GraphTraces[0]->AsObject()->GetArrayField(TEXT("data_edges")).Num(), 1);

    const TArray<TSharedPtr<FJsonValue>>& Functions = Root->GetArrayField(TEXT("functions"));
    TestEqual(TEXT("関数数"), Functions.Num(), 1);
    const TSharedPtr<FJsonObject> FunctionJson = Functions[0]->AsObject();
    TestEqual(TEXT("所有クラス"), FunctionJson->GetStringField(TEXT("owner_class_path")),
        FString(TEXT("/Game/BP_Test.BP_Test_C")));
    const TArray<TSharedPtr<FJsonValue>>& JsonParameters = FunctionJson->GetArrayField(TEXT("parameters"));
    TestEqual(TEXT("引数と戻り値"), JsonParameters.Num(), 2);
    TestTrue(TEXT("Native属性"), FunctionJson->GetBoolField(TEXT("is_native")));
    TestTrue(TEXT("Static属性"), FunctionJson->GetBoolField(TEXT("is_static")));
    TestTrue(TEXT("引数既定値の存在"), JsonParameters[0]->AsObject()->GetBoolField(TEXT("has_default_value")));
    TestEqual(TEXT("引数既定値"), JsonParameters[0]->AsObject()->GetStringField(TEXT("default_value")), FString(TEXT("0")));
    TestFalse(TEXT("戻り値には既定値なし"), JsonParameters[1]->AsObject()->GetBoolField(TEXT("has_default_value")));
    TestTrue(TEXT("既定値なしはnull"), JsonParameters[1]->AsObject()->GetField<EJson::Null>(TEXT("default_value"))->IsNull());
    TestEqual(TEXT("入力型"), JsonParameters[0]->AsObject()->GetObjectField(TEXT("type"))->GetStringField(TEXT("kind")),
        FString(TEXT("int32")));
    TestTrue(TEXT("戻り値フラグ"), JsonParameters[1]->AsObject()->GetBoolField(TEXT("is_return")));
    const TSharedPtr<FJsonObject> Expression = Functions[0]->AsObject()
        ->GetArrayField(TEXT("blocks"))[0]->AsObject()
        ->GetArrayField(TEXT("statements"))[0]->AsObject()
        ->GetObjectField(TEXT("expression"));
    TestEqual(TEXT("関数名"), Expression->GetStringField(TEXT("function_name")), FString(TEXT("PrintString")));
    TestEqual(TEXT("空文字列リテラルを保持"),
        Expression->GetArrayField(TEXT("arguments"))[0]->AsObject()->GetStringField(TEXT("literal_value")),
        FString(TEXT("")));
    TestTrue(TEXT("無効GUIDはnull"),
        Expression->GetField<EJson::Null>(TEXT("source_pin_id"))->IsNull());

    FBPSemanticDiagnostic Diagnostic;
    Diagnostic.Message = TEXT("Unsupported node");
    IR.Diagnostics.Add(Diagnostic);
    const TSharedRef<FJsonObject> Failed = FBPSemanticJsonSerializer::ToJson(IR, TEXT("5.7.4"));
    TestEqual(TEXT("診断時の状態"), Failed->GetStringField(TEXT("semantic_status")),
        FString(TEXT("unsupported")));
    TestEqual(TEXT("診断重大度"), Failed->GetArrayField(TEXT("diagnostics"))[0]->AsObject()
        ->GetStringField(TEXT("severity")), FString(TEXT("error")));
    return true;
}

#endif
