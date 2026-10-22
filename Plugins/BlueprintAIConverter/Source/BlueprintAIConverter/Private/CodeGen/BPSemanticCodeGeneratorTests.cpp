#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorCodeGenerator.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "BPReflectedCallResolver.h"
#include "Components/ActorComponent.h"
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

FBPSemanticExpression MakeActorLocationCall(const FString& ParentClassPath,
    const FName HitResultName)
{
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = ParentClassPath;
    Target.VariableName = TEXT("this");

    FBPSemanticExpression Location;
    Location.Kind = EBPSemanticExpressionKind::FunctionCall;
    Location.Type.Kind = EBPSemanticTypeKind::Struct;
    Location.Type.TypePath = TEXT("/Script/CoreUObject.Vector");
    Location.FunctionName = TEXT("K2_GetActorLocation");
    Location.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Location.Arguments.Add(Target);

    FBPSemanticExpression BoolLiteral;
    BoolLiteral.Kind = EBPSemanticExpressionKind::Literal;
    BoolLiteral.Type.Kind = EBPSemanticTypeKind::Bool;
    BoolLiteral.LiteralValue = TEXT("false");

    FBPSemanticExpression HitResult;
    HitResult.Kind = EBPSemanticExpressionKind::VariableReference;
    HitResult.Type.Kind = EBPSemanticTypeKind::Struct;
    HitResult.Type.TypePath = TEXT("/Script/Engine.HitResult");
    HitResult.VariableName = HitResultName;

    FBPSemanticExpression Call;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Bool;
    Call.FunctionName = TEXT("K2_SetActorLocation");
    Call.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.Arguments.Add(MoveTemp(Target));
    Call.Arguments.Add(MoveTemp(Location));
    Call.Arguments.Add(BoolLiteral);
    Call.Arguments.Add(MoveTemp(HitResult));
    Call.Arguments.Add(MoveTemp(BoolLiteral));
    return Call;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorGenericBranchStatementsTest,
    "BlueprintAIConverter.SemanticIR.Generator.GenericBranchStatements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorGenericBranchStatementsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("テスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticStatement& Statement = Semantic.Functions[0].Blocks[0].Statements[0];
    Statement.Kind = EBPSemanticStatementKind::Branch;
    Statement.Expression = FBPSemanticExpression();
    Statement.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Statement.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Statement.Expression.LiteralValue = TEXT("true");

    FBPSemanticStatement ThenStatement;
    ThenStatement.Kind = EBPSemanticStatementKind::Expression;
    ThenStatement.Expression = MakeActorLocationCall(
        Semantic.ParentClassPath, TEXT("ThenHitResult"));
    Statement.ThenStatements.Add(MoveTemp(ThenStatement));

    FBPSemanticStatement ElseStatement;
    ElseStatement.Kind = EBPSemanticStatementKind::Expression;
    ElseStatement.Expression = MakeActorLocationCall(
        Semantic.ParentClassPath, TEXT("ElseHitResult"));
    Statement.ElseStatements.Add(MoveTemp(ElseStatement));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Branch末端を共通式Emitterで生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AGenericBranchActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("必要なHitResultヘッダーを収集"),
        Files.SourceContent.Contains(TEXT("#include \"Engine/HitResult.h\"")));
    TestTrue(TEXT("True側の汎用式文"), Files.SourceContent.Contains(
        TEXT("FHitResult ThenHitResult{};")));
    TestTrue(TEXT("False側の汎用式文"), Files.SourceContent.Contains(
        TEXT("FHitResult ElseHitResult{};")));
    TestFalse(TEXT("Branch生成がPrintStringに依存しない"),
        Files.SourceContent.Contains(TEXT("UKismetSystemLibrary::PrintString")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorNestedBranchStatementsTest,
    "BlueprintAIConverter.SemanticIR.Generator.NestedBranchStatements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorNestedBranchStatementsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("テスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    Outer.Kind = EBPSemanticStatementKind::Branch;
    Outer.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Outer.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Outer.Expression.LiteralValue = TEXT("true");

    // 処理1、処理2、内側if、処理5、処理6の順序とスコープを確認する。
    auto MakeCall = [&Semantic](const FName HitResultName)
    {
        FBPSemanticStatement Statement;
        Statement.Kind = EBPSemanticStatementKind::Expression;
        Statement.Expression = MakeActorLocationCall(Semantic.ParentClassPath, HitResultName);
        return Statement;
    };
    Outer.ThenStatements.Add(MakeCall(TEXT("Step1")));
    Outer.ThenStatements.Add(MakeCall(TEXT("Step2")));
    FBPSemanticStatement Inner;
    Inner.Kind = EBPSemanticStatementKind::Branch;
    Inner.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Inner.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Inner.Expression.LiteralValue = TEXT("false");
    Inner.ThenStatements.Add(MakeCall(TEXT("Step3")));
    Inner.ThenStatements.Add(MakeCall(TEXT("Step4")));
    Outer.ThenStatements.Add(MoveTemp(Inner));
    Outer.ThenStatements.Add(MakeCall(TEXT("Step5")));
    Outer.ThenStatements.Add(MakeCall(TEXT("Step6")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ネストしたBranchのC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedBranchActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const FString& Source = Files.SourceContent;
    const int32 Step1 = Source.Find(TEXT("FHitResult Step1{};"));
    const int32 Step2 = Source.Find(TEXT("FHitResult Step2{};"));
    const int32 Step3 = Source.Find(TEXT("FHitResult Step3{};"));
    const int32 Step4 = Source.Find(TEXT("FHitResult Step4{};"));
    const int32 Step5 = Source.Find(TEXT("FHitResult Step5{};"));
    const int32 Step6 = Source.Find(TEXT("FHitResult Step6{};"));
    TestTrue(TEXT("処理1から処理6の生成順を維持"),
        Step1 != INDEX_NONE && Step1 < Step2 && Step2 < Step3 &&
        Step3 < Step4 && Step4 < Step5 && Step5 < Step6);
    TestTrue(TEXT("内側ifを出力"), Source.Contains(TEXT("if (false)")));
    TestTrue(TEXT("外側ifを出力"), Source.Contains(TEXT("if (true)")));

    // Branchの両枝が合流した後の文は、ifの外側へ一度だけ出力する。
    Semantic.Functions[0].Blocks[0].Statements.Add(MakeCall(TEXT("AfterJoin")));
    FBPGeneratedActorFiles JoinedFiles;
    TArray<FString> JoinedDiagnostics;
    if (TestTrue(TEXT("Branch後続文のC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedBranchActor"),
            FString(), JoinedFiles, JoinedDiagnostics)) == false)
    {
        for (const FString& Diagnostic : JoinedDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const FString& JoinedSource = JoinedFiles.SourceContent;
    const int32 JoinPosition = JoinedSource.Find(TEXT("FHitResult AfterJoin{};"));
    TestTrue(TEXT("合流後の文は内側のifより後ろ"),
        JoinPosition > JoinedSource.Find(TEXT("FHitResult Step6{};")));
    TestTrue(TEXT("合流後の文を重複出力しない"),
        JoinedSource.Contains(TEXT("FHitResult AfterJoin{};")) &&
        JoinedSource.Find(TEXT("FHitResult AfterJoin{};"), ESearchCase::CaseSensitive,
            ESearchDir::FromEnd) == JoinPosition);
    return true;
}

// 関数名やクラス別の分岐ではなく、リフレクションと継承関係で解決する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPReflectedCallResolverInheritanceTest,
    "BlueprintAIConverter.SemanticIR.Resolver.Inheritance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPReflectedCallResolverInheritanceTest::RunTest(const FString& Parameters)
{
    FBPReflectedCall Call;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Pawn派生のselfからActorの関数を解決"),
        FBPReflectedCallResolver::Resolve(TEXT("/Script/Engine.Actor"),
            TEXT("SetActorTickEnabled"), TEXT("/Script/Engine.Pawn"),
            false, Call, Diagnostics));
    TestNotNull(TEXT("反射で解決された関数"), Call.Function);
    TestEqual(TEXT("所有クラス"), Call.Signature.OwnerClassPath,
        FString(TEXT("/Script/Engine.Actor")));

    // ActorはComponentの派生ではないためself呼び出しに変換してはいけない。
    Diagnostics.Reset();
    TestFalse(TEXT("継承関係のない所有クラスを拒否"),
        FBPReflectedCallResolver::Resolve(TEXT("/Script/Engine.ActorComponent"),
            TEXT("SetComponentTickEnabled"), TEXT("/Script/Engine.Actor"),
            false, Call, Diagnostics));
    TestNull(TEXT("失敗時に古い関数参照を残さない"), Call.Function);
    Diagnostics.Reset();
    TestTrue(TEXT("TargetがActor派生ならクラス名によらず関数を解決"),
        FBPReflectedCallResolver::ResolveForTarget(TEXT("/Script/Engine.Actor"),
            TEXT("SetActorTickEnabled"), TEXT("/Script/Engine.Pawn"),
            false, Call, Diagnostics));
    Diagnostics.Reset();
    TestFalse(TEXT("Component型TargetにActor関数を適用しない"),
        FBPReflectedCallResolver::ResolveForTarget(TEXT("/Script/Engine.Actor"),
            TEXT("SetActorTickEnabled"), TEXT("/Script/Engine.ActorComponent"),
            false, Call, Diagnostics));

    // WorldContextを持つ静的関数も、Reflectionの所有クラスと署名から解決する。
    // 専用PrintString Emitterの存在に依存しないResolverの回帰テスト。
    Diagnostics.Reset();
    TestTrue(TEXT("PrintStringの静的UFunctionをReflectionから解決"),
        FBPReflectedCallResolver::Resolve(TEXT("/Script/Engine.KismetSystemLibrary"),
            TEXT("PrintString"), TEXT("/Script/Engine.Actor"),
            true, Call, Diagnostics));
    if (Call.Function != nullptr)
    {
        TestTrue(TEXT("WorldContextメタデータを持つ"),
            Call.Function->HasMetaData(TEXT("WorldContext")));
        TestTrue(TEXT("WorldContext引数が署名に存在"),
            Call.Signature.ParameterNames.Contains(FName(TEXT("WorldContextObject"))));
        TestTrue(TEXT("WorldContextが先頭引数"),
            Call.Signature.ParameterNames.IsEmpty() == false &&
            Call.Signature.ParameterNames[0] == FName(TEXT("WorldContextObject")));
        TestTrue(TEXT("WorldContextがオブジェクト入力引数"),
            Call.Signature.ParameterTypes.IsEmpty() == false &&
            Call.Signature.ParameterTypes[0].Kind == EBPSemanticTypeKind::Object &&
            Call.Signature.OutputParameters[0] == false);
        if (Call.Signature.ParameterTypes.IsEmpty() == false)
        {
            // 参照引数として扱うとthisの値渡し規約が変わるため、
            // 共通Emitterの受理条件と同じ型制約を検証する。
            TestFalse(TEXT("WorldContextは参照引数ではない"),
                Call.Signature.ParameterTypes[0].bIsReference);
            TestEqual(TEXT("WorldContextの期待クラス"),
                Call.Signature.ParameterTypes[0].TypePath,
                FString(TEXT("/Script/CoreUObject.Object")));
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorActorNoArgBranchTest,
    "BlueprintAIConverter.SemanticIR.Generator.ActorNoArgBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorActorNoArgBranchTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("テスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    Outer.Kind = EBPSemanticStatementKind::Branch;
    Outer.Expression = FBPSemanticExpression();
    Outer.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Outer.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Outer.Expression.LiteralValue = TEXT("true");

    // PrintString以外のActor関数をBranch内部に配置できることを確認する。
    FBPSemanticStatement Call;
    Call.Kind = EBPSemanticStatementKind::Expression;
    Call.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Expression.Type.Kind = EBPSemanticTypeKind::Void;
    Call.Expression.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.Expression.FunctionName = TEXT("K2_DestroyActor");
    FBPSemanticExpression Self;
    Self.Kind = EBPSemanticExpressionKind::VariableReference;
    Self.Type.Kind = EBPSemanticTypeKind::Object;
    Self.Type.TypePath = Semantic.ParentClassPath;
    Self.VariableName = TEXT("this");
    Call.Expression.Arguments.Add(MoveTemp(Self));
    Outer.ThenStatements.Add(MoveTemp(Call));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("引数なしActor関数の生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AActorNoArgBranch"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("Actor関数をif内部に出力"),
        Files.SourceContent.Contains(TEXT("this->K2_DestroyActor();")));

    // リテラル引数を持つActor関数のC++呼び出しを検証する。
    FBPSemanticStatement& ActorCall = Outer.ThenStatements[0];
    ActorCall.Expression.FunctionName = TEXT("SetActorTickEnabled");
    FBPSemanticExpression Enabled;
    Enabled.Kind = EBPSemanticExpressionKind::Literal;
    Enabled.Type.Kind = EBPSemanticTypeKind::Bool;
    Enabled.LiteralValue = TEXT("false");
    ActorCall.Expression.Arguments.Add(MoveTemp(Enabled));
    FBPGeneratedActorFiles WithArgument;
    TArray<FString> ArgumentDiagnostics;
    if (TestTrue(TEXT("bool引数付きActor関数の生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AActorNoArgBranch"),
            FString(), WithArgument, ArgumentDiagnostics)) == false)
    {
        for (const FString& Diagnostic : ArgumentDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("bool引数を生成"),
        WithArgument.SourceContent.Contains(TEXT("this->SetActorTickEnabled(false);")));

    // Reflectionのbool引数へFString変数を誤って渡さないことを検証する。
    // 失敗時には部分的なC++ファイルを公開しない。
    FBPSemanticExpression& InvalidArgument = ActorCall.Expression.Arguments.Last();
    InvalidArgument.Kind = EBPSemanticExpressionKind::VariableReference;
    InvalidArgument.Type.Kind = EBPSemanticTypeKind::String;
    InvalidArgument.VariableName = TEXT("UnverifiedString");
    FBPGeneratedActorFiles InvalidFiles;
    TArray<FString> InvalidDiagnostics;
    TestFalse(TEXT("Reflectionと一致しないFString変数を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AActorNoArgBranch"),
            FString(), InvalidFiles, InvalidDiagnostics));
    TestTrue(TEXT("型不一致の診断を返す"), InvalidDiagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時にソースを公開しない"), InvalidFiles.SourceContent.IsEmpty());
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

    Graph = MakeGeneratorTestGraph();
    Graph.ParentClassPath = UActorComponent::StaticClass()->GetPathName();
    if (TestTrue(TEXT("ActorComponent派生をLowering"),
        FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    Diagnostics.Reset();
    TestTrue(TEXT("ActorComponent派生C++を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UGeneratedComponent"), FString(), Files, Diagnostics));
    TestTrue(TEXT("ActorComponentヘッダーを生成"),
        Files.HeaderContent.Contains(TEXT("#include \"Components/ActorComponent.h\"")));
    TestTrue(TEXT("ActorComponent継承を生成"),
        Files.HeaderContent.Contains(TEXT("class UGeneratedComponent : public UActorComponent")));
    TestEqual(TEXT("U prefixを除いたファイル名"), Files.HeaderFileName,
        FString(TEXT("GeneratedComponent.h")));

    Diagnostics.Reset();
    TestFalse(TEXT("親と異なるprefixを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AGeneratedComponent"), FString(), Files, Diagnostics));
    TestTrue(TEXT("prefix拒否理由を返す"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("U-prefixed"));
        }));

    Semantic.ParentClassPath = UObject::StaticClass()->GetPathName();
    Diagnostics.Reset();
    TestFalse(TEXT("BeginPlayを持たない親を安全に拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UGeneratedObject"), FString(), Files, Diagnostics));
    TestTrue(TEXT("lifecycle拒否理由を返す"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("BeginPlay lifecycle"));
        }));

    FBPSemanticFunction& ObjectFunction = Semantic.Functions[0];
    ObjectFunction.Name = TEXT("RunConversion");
    ObjectFunction.bIsOverride = false;
    ObjectFunction.bIsBlueprintEvent = false;
    ObjectFunction.SignatureSource = TEXT("generated_class_reflection");
    ObjectFunction.OverriddenFunctionPath.Reset();
    ObjectFunction.Parameters.Reset();
    FBPSemanticParameter CountParameter;
    CountParameter.Name = TEXT("Count");
    CountParameter.Type.Kind = EBPSemanticTypeKind::Int32;
    CountParameter.DeclarationIndex = 0;
    ObjectFunction.Parameters.Add(CountParameter);
    ObjectFunction.Blocks[0].Statements[0].Expression.Arguments[0].Type.TypePath =
        UObject::StaticClass()->GetPathName();
    Diagnostics.Reset();
    if (TestTrue(TEXT("UObjectの通常関数を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UGeneratedObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("通常関数をUFUNCTIONとして宣言"), Files.HeaderContent.Contains(
        TEXT("UFUNCTION(BlueprintCallable)\n    void RunConversion(int32 Count);")));
    TestTrue(TEXT("通常関数定義を生成"), Files.SourceContent.Contains(
        TEXT("void UGeneratedObject::RunConversion(int32 Count)")));
    TestFalse(TEXT("通常関数ではSuperを呼ばない"), Files.SourceContent.Contains(
        TEXT("Super::RunConversion")));

    ObjectFunction.Name = TEXT("OnConverted");
    ObjectFunction.bIsBlueprintEvent = true;
    ObjectFunction.bIsEventGraph = true;
    Diagnostics.Reset();
    if (TestTrue(TEXT("UObjectのBlueprintイベントを生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("UGeneratedObject"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("BlueprintNativeEvent宣言"), Files.HeaderContent.Contains(
        TEXT("UFUNCTION(BlueprintNativeEvent, BlueprintCallable)")));
    TestTrue(TEXT("イベントImplementation宣言"), Files.HeaderContent.Contains(
        TEXT("virtual void OnConverted_Implementation(int32 Count);")));
    TestTrue(TEXT("イベントImplementation本体"), Files.SourceContent.Contains(
        TEXT("void UGeneratedObject::OnConverted_Implementation(int32 Count)")));

    ObjectFunction.Name = TEXT("BeginDestroy");
    ObjectFunction.Parameters.Reset();
    ObjectFunction.bIsOverride = true;
    ObjectFunction.bIsBlueprintEvent = false;
    ObjectFunction.bIsEventGraph = false;
    ObjectFunction.OverriddenFunctionPath = TEXT("/Script/CoreUObject.Object.BeginDestroy");
    Diagnostics.Reset();
    if (TestTrue(TEXT("反射検証済みのクラス固有lifecycleを生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("UGeneratedObject"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("lifecycle override宣言"), Files.HeaderContent.Contains(
        TEXT("virtual void BeginDestroy() override;")));
    TestTrue(TEXT("lifecycleのSuper呼び出し"), Files.SourceContent.Contains(
        TEXT("Super::BeginDestroy();")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorImportTextVariableTest,
    "BlueprintAIConverter.SemanticIR.Generator.VerifiedImportTextVariable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorImportTextVariableTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("ImportTextテスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticVariable Variable;
    Variable.Name = TEXT("SpawnOffset");
    Variable.Type.Kind = EBPSemanticTypeKind::Struct;
    Variable.TypePath = TEXT("/Script/CoreUObject.Vector");
    Variable.ReflectedCppType = TEXT("FVector");
    Variable.ReflectedPropertyClass = TEXT("StructProperty");
    Variable.PropertyFlags = TEXT("0");
    Variable.InitializationStrategy = EBPVariableInitializationStrategy::ImportText;
    Variable.bDefaultValueAvailable = true;
    Variable.DefaultValueSource = TEXT("cdo_export_text");
    Variable.DefaultValue = TEXT("(X=1.0,Y=2.0,Z=3.0)");
    Variable.bExportedDefaultValueAvailable = true;
    Variable.ExportedDefaultValue = Variable.DefaultValue;
    Variable.bExportTextRoundTripVerified = true;
    Semantic.Variables.Add(Variable);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("往復検証済みImportText変数を生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AImportTextActor"),
            FString(), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("値初期化した反射型宣言"),
        Files.HeaderContent.Contains(TEXT("FVector SpawnOffset{};")));
    TestTrue(TEXT("コンストラクタ宣言"),
        Files.HeaderContent.Contains(TEXT("AImportTextActor();")));
    TestTrue(TEXT("UnrealTypeヘッダー"),
        Files.SourceContent.Contains(TEXT("#include \"UObject/UnrealType.h\"")));
    TestTrue(TEXT("日本語の復元コメント"), Files.SourceContent.Contains(
        TEXT("往復検証済み値からBlueprintのデフォルト値を復元します。")));
    TestTrue(TEXT("反射プロパティを名前で解決"), Files.SourceContent.Contains(
        TEXT("GET_MEMBER_NAME_CHECKED(AImportTextActor, SpawnOffset)")));
    TestTrue(TEXT("ExportText値をImportTextへ渡す"), Files.SourceContent.Contains(
        TEXT("TEXT(\"(X=1.0,Y=2.0,Z=3.0)\")")));

    Semantic.Variables[0].bExportTextRoundTripVerified = false;
    TestFalse(TEXT("往復未検証値は拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AImportTextActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.HeaderContent.IsEmpty());
    return true;
}

#endif
