#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorCodeGenerator.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "GameFramework/Pawn.h"

namespace
{
FBPGraphIR MakeGeneratorTestGraph()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Event;
    Event.Id = FGuid::NewGuid();
    Event.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Event.FunctionName = TEXT("ReceiveBeginPlay");
    FBPIRNode Print;
    Print.Id = FGuid::NewGuid();
    Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Print.FunctionName = TEXT("PrintString");

    FBPIRPin Text;
    Text.Id = FGuid::NewGuid();
    Text.Name = TEXT("InString");
    Text.Category = TEXT("string");
    Text.Direction = EGPD_Input;
    Text.DefaultValue = TEXT("Hello \"UE\"");
    Print.Pins.Add(Text);

    FBPIRPin Color;
    Color.Id = FGuid::NewGuid();
    Color.Name = TEXT("TextColor");
    Color.Direction = EGPD_Input;
    Color.DefaultValue = TEXT("(R=1,G=0.25,B=0,A=1)");
    Print.Pins.Add(Color);

    FBPIRPin Duration;
    Duration.Id = FGuid::NewGuid();
    Duration.Name = TEXT("Duration");
    Duration.Direction = EGPD_Input;
    Duration.DefaultValue = TEXT("3.5");
    Print.Pins.Add(Duration);

    FBPIRLink Edge;
    Edge.SourceNode = Event.Id;
    Edge.TargetNode = Print.Id;
    Graph.ExecEdges.Add(Edge);
    Graph.Nodes.Add(MoveTemp(Event));
    Graph.Nodes.Add(MoveTemp(Print));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorParityTest,
    "BlueprintAIConverter.SemanticIR.Generator.LegacyParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorParityTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPGeneratedActorFiles LegacyFiles;
    TArray<FString> LegacyDiagnostics;
    if (TestTrue(TEXT("旧Generator成功"),
        FBPActorCodeGenerator::Generate(Graph, TEXT("AParityActor"), TEXT("TEST_API"), LegacyFiles, LegacyDiagnostics)) == false)
    {
        return false;
    }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles NewFiles;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("新Generator成功"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AParityActor"), TEXT("TEST_API"), NewFiles, Diagnostics)) == false)
    {
        return false;
    }
    TestEqual(TEXT("ヘッダー名一致"), NewFiles.HeaderFileName, LegacyFiles.HeaderFileName);
    TestEqual(TEXT("ソース名一致"), NewFiles.SourceFileName, LegacyFiles.SourceFileName);
    TestEqual(TEXT("ヘッダー一致"), NewFiles.HeaderContent, LegacyFiles.HeaderContent);
    TestEqual(TEXT("ソース一致"), NewFiles.SourceContent, LegacyFiles.SourceContent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorRejectTest,
    "BlueprintAIConverter.SemanticIR.Generator.RejectUnsupported",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorRejectTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    Semantic.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Statement.Expression.FunctionName = TEXT("UnsupportedFunction");
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    Semantic.Functions.Add(Function);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("未対応関数を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AParityActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("診断あり"), Diagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時にソースを公開しない"), Files.SourceContent.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorOptionsParityTest,
    "BlueprintAIConverter.SemanticIR.Generator.OptionsParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorOptionsParityTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPIRNode& Print = Graph.Graphs[0].Nodes[1];

    // エスケープ文字と既定値以外のオプションを旧経路と比較する。
    Print.Pins[0].DefaultValue = TEXT("Line 1\nLine 2\t\\ \"quote\"");
    FBPIRPin Screen;
    Screen.Name = TEXT("bPrintToScreen");
    Screen.Direction = EGPD_Input;
    Screen.DefaultValue = TEXT("false");
    Print.Pins.Add(Screen);
    FBPIRPin Log;
    Log.Name = TEXT("bPrintToLog");
    Log.Direction = EGPD_Input;
    Log.DefaultValue = TEXT("true");
    Print.Pins.Add(Log);
    FBPIRPin Key;
    Key.Name = TEXT("Key");
    Key.Direction = EGPD_Input;
    Key.DefaultValue = TEXT("debug-key");
    Print.Pins.Add(Key);

    FBPGeneratedActorFiles LegacyFiles;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("旧経路成功"), FBPActorCodeGenerator::Generate(
        Graph, TEXT("AOptionsActor"), FString(), LegacyFiles, Diagnostics)) == false)
    {
        return false;
    }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles NewFiles;
    if (TestTrue(TEXT("新経路成功"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AOptionsActor"), FString(), NewFiles, Diagnostics)) == false)
    {
        return false;
    }
    TestEqual(TEXT("オプション付きヘッダー一致"), NewFiles.HeaderContent, LegacyFiles.HeaderContent);
    TestEqual(TEXT("オプション付きソース一致"), NewFiles.SourceContent, LegacyFiles.SourceContent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorInvalidLiteralTest,
    "BlueprintAIConverter.SemanticIR.Generator.RejectInvalidLiteral",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorInvalidLiteralTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }

    // 手動構築されたSemantic IRでも、不正な値をC++へ流さない。
    FBPSemanticExpression& Duration = Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[5];
    Duration.LiteralValue = TEXT("not-a-number");
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("不正Durationを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AInvalidActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("診断あり"), Diagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時はファイルを空にする"), Files.SourceContent.IsEmpty());

    // 未対応の式を黙ってリテラルへ変換しない。
    Duration.LiteralValue = TEXT("2.0");
    Duration.Kind = EBPSemanticExpressionKind::VariableReference;
    TestFalse(TEXT("未対応式を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AInvalidActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("失敗時はヘッダーも空"), Files.HeaderContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorParentClassTest,
    "BlueprintAIConverter.SemanticIR.Generator.ParentClass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorParentClassTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    Graph.ParentClassPath = APawn::StaticClass()->GetPathName();
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Pawn派生をLowering"),
        FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Pawn派生C++を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AGeneratedPawn"), FString(), Files, Diagnostics));
    TestTrue(TEXT("Pawnヘッダーを生成"),
        Files.HeaderContent.Contains(TEXT("#include \"GameFramework/Pawn.h\"")));
    TestTrue(TEXT("Pawn継承を生成"),
        Files.HeaderContent.Contains(TEXT("class AGeneratedPawn : public APawn")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorVariablesTest,
    "BlueprintAIConverter.SemanticIR.Generator.MemberVariables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorVariablesTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("変数テスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticVariable Enabled;
    Enabled.Name = TEXT("Enabled");
    Enabled.Type.Kind = EBPSemanticTypeKind::Bool;
    Enabled.ReflectedCppType = TEXT("bool");
    Enabled.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Enabled.bDefaultValueAvailable = true;
    Enabled.DefaultValueSource = TEXT("cdo");
    Enabled.DefaultValue = TEXT("true");
    Semantic.Variables.Add(Enabled);

    FBPSemanticVariable Health;
    Health.Name = TEXT("Health");
    Health.Type.Kind = EBPSemanticTypeKind::Int32;
    Health.ReflectedCppType = TEXT("int32");
    Health.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Health.bDefaultValueAvailable = true;
    Health.DefaultValueSource = TEXT("cdo");
    Health.DefaultValue = TEXT("-17");
    Semantic.Variables.Add(Health);

    FBPSemanticVariable Title;
    Title.Name = TEXT("Title");
    Title.Type.Kind = EBPSemanticTypeKind::String;
    Title.ReflectedCppType = TEXT("FString");
    Title.InitializationStrategy = EBPVariableInitializationStrategy::StringLiteral;
    Title.bDefaultValueAvailable = true;
    Title.DefaultValueSource = TEXT("cdo");
    Title.DefaultValue = TEXT("Line 1\n\"Ready\"");
    Semantic.Variables.Add(Title);

    FBPSemanticVariable LargeCount;
    LargeCount.Name = TEXT("LargeCount");
    LargeCount.Type.Kind = EBPSemanticTypeKind::Int64;
    LargeCount.ReflectedCppType = TEXT("int64");
    LargeCount.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    LargeCount.bDefaultValueAvailable = true;
    LargeCount.DefaultValueSource = TEXT("cdo");
    LargeCount.DefaultValue = TEXT("9000000000");
    Semantic.Variables.Add(LargeCount);

    FBPSemanticVariable Speed;
    Speed.Name = TEXT("Speed");
    Speed.Type.Kind = EBPSemanticTypeKind::Float;
    Speed.ReflectedCppType = TEXT("float");
    Speed.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Speed.bDefaultValueAvailable = true;
    Speed.DefaultValueSource = TEXT("cdo");
    Speed.DefaultValue = TEXT("1.25");
    Semantic.Variables.Add(Speed);

    FBPSemanticVariable Precision;
    Precision.Name = TEXT("Precision");
    Precision.Type.Kind = EBPSemanticTypeKind::Double;
    Precision.ReflectedCppType = TEXT("double");
    Precision.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Precision.bDefaultValueAvailable = true;
    Precision.DefaultValueSource = TEXT("cdo");
    Precision.DefaultValue = TEXT("2.5");
    Semantic.Variables.Add(Precision);

    FBPSemanticVariable Tag;
    Tag.Name = TEXT("Tag");
    Tag.Type.Kind = EBPSemanticTypeKind::Name;
    Tag.ReflectedCppType = TEXT("FName");
    Tag.InitializationStrategy = EBPVariableInitializationStrategy::NameLiteral;
    Tag.bDefaultValueAvailable = true;
    Tag.DefaultValueSource = TEXT("cdo");
    Tag.DefaultValue = TEXT("Runner");
    Semantic.Variables.Add(Tag);

    FBPSemanticVariable Label;
    Label.Name = TEXT("Label");
    Label.Type.Kind = EBPSemanticTypeKind::Text;
    Label.ReflectedCppType = TEXT("FText");
    Label.InitializationStrategy = EBPVariableInitializationStrategy::TextLiteral;
    Label.bDefaultValueAvailable = true;
    Label.DefaultValueSource = TEXT("cdo");
    Label.DefaultValue = TEXT("Ready Text");
    Label.bTextCultureInvariant = true;
    Semantic.Variables.Add(Label);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("対応済みメンバー変数を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVariableActor"), FString(), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("bool初期値"), Files.HeaderContent.Contains(TEXT("bool Enabled = true;")));
    TestTrue(TEXT("int32初期値"), Files.HeaderContent.Contains(TEXT("int32 Health = -17;")));
    TestTrue(TEXT("FString初期値をエスケープ"), Files.HeaderContent.Contains(
        TEXT("FString Title = TEXT(\"Line 1\\n\\\"Ready\\\"\");")));
    TestTrue(TEXT("int64初期値"), Files.HeaderContent.Contains(
        TEXT("int64 LargeCount = 9000000000LL;")));
    TestTrue(TEXT("float初期値"), Files.HeaderContent.Contains(TEXT("float Speed = 1.25f;")));
    TestTrue(TEXT("double初期値"), Files.HeaderContent.Contains(TEXT("double Precision = 2.5")));
    TestTrue(TEXT("FName初期値"), Files.HeaderContent.Contains(
        TEXT("FName Tag = FName(TEXT(\"Runner\"));")));
    TestTrue(TEXT("FText初期値"), Files.HeaderContent.Contains(
        TEXT("FText Label = FText::AsCultureInvariant(TEXT(\"Ready Text\"));")));
    FString HeaderWithoutProperties = Files.HeaderContent;
    TestEqual(TEXT("UPROPERTYを8件生成"),
        HeaderWithoutProperties.ReplaceInline(TEXT("UPROPERTY()"), TEXT("")), 8);

    Semantic.Variables[0].bDefaultValueAvailable = false;
    TestFalse(TEXT("CDO値を取得できない変数を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVariableActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.HeaderContent.IsEmpty());

    Semantic.Variables[0].bDefaultValueAvailable = true;
    Semantic.Variables[4].DefaultValue = TEXT("nan");
    TestFalse(TEXT("非有限floatを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVariableActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("数値拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorVariableFallbackTest,
    "BlueprintAIConverter.SemanticIR.Generator.VariableDeclarationFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorVariableFallbackTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("フォールバックテスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticVariable Variable;
    Variable.Name = TEXT("CustomValue");
    Variable.Guid = FGuid(1, 2, 3, 4);
    Variable.Type.Kind = EBPSemanticTypeKind::Unresolved;
    Variable.ReflectedCppType = TEXT("FCustomValue");
    Variable.ReflectedPropertyClass = TEXT("StructProperty");
    Variable.PropertyFlags = TEXT("0");
    Semantic.Variables.Add(Variable);

    FBPSemanticDiagnostic Warning;
    Warning.Severity = EBPSemanticDiagnosticSeverity::Warning;
    Warning.Message = TEXT("CustomValue requires type resolution");
    Semantic.Diagnostics.Add(Warning);
    TestFalse(TEXT("警告だけではSemantic IRをエラー扱いしない"), Semantic.HasErrors());

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("Strictでは未対応変数を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AFallbackActor"), FString(), Files, Diagnostics));

    if (TestTrue(TEXT("DeclarationOnlyでは宣言を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AFallbackActor"), FString(), Files, Diagnostics,
        EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        return false;
    }
    TestTrue(TEXT("既定値欠落コメント"), Files.HeaderContent.Contains(
        TEXT("部分変換: Blueprintのデフォルト値とプロパティメタデータは保持されていません。")));
    TestTrue(TEXT("依存関係確認コメント"), Files.HeaderContent.Contains(
        TEXT("必要なヘッダーとモジュール依存関係を確認してください。")));
    TestTrue(TEXT("元変数GUIDコメント"), Files.HeaderContent.Contains(
        TEXT("00000001-0000-0002-0000-000300000004")));
    TestTrue(TEXT("反射型で値初期化宣言"), Files.HeaderContent.Contains(
        TEXT("FCustomValue CustomValue{};")));
    TestTrue(TEXT("partial診断を返す"), Diagnostics.ContainsByPredicate(
        [](const FString& Diagnostic)
        {
            return Diagnostic.Contains(TEXT("Partial variable conversion: CustomValue"));
        }));

    Semantic.Variables[0].ReflectedCppType = TEXT("FVector; #error unsafe");
    TestFalse(TEXT("危険な反射型文字列を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AFallbackActor"), FString(), Files, Diagnostics,
        EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("危険な型の生成物を公開しない"), Files.HeaderContent.IsEmpty());
    return true;
}

#endif
