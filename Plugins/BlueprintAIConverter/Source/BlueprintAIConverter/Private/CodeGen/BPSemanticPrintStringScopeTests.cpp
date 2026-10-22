#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPSemanticCodeGenerator.h"

namespace
{
// 生成器の公開入口を通して、PrintStringのローカルIndex検証を回帰テストする。
FBPSemanticExpression MakeLiteral(const EBPSemanticTypeKind Kind, const FString& Value)
{
    FBPSemanticExpression Expression;
    Expression.Kind = EBPSemanticExpressionKind::Literal;
    Expression.Type.Kind = Kind;
    Expression.LiteralValue = Value;
    return Expression;
}

FBPSemanticStatement MakePrintIndex(const FName IndexName)
{
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    FBPSemanticExpression& Call = Statement.Expression;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Void;
    Call.FunctionName = TEXT("PrintString");
    Call.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");

    FBPSemanticExpression Context;
    Context.Kind = EBPSemanticExpressionKind::VariableReference;
    Context.VariableName = TEXT("this");
    Context.Type.Kind = EBPSemanticTypeKind::Object;
    Context.Type.TypePath = TEXT("/Script/Engine.Actor");
    Call.Arguments.Add(Context);

    FBPSemanticExpression Conversion;
    Conversion.Kind = EBPSemanticExpressionKind::FunctionCall;
    Conversion.Type.Kind = EBPSemanticTypeKind::String;
    Conversion.FunctionName = TEXT("Conv_IntToString");
    Conversion.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");

    FBPSemanticExpression Index;
    Index.Kind = EBPSemanticExpressionKind::VariableReference;
    Index.Type.Kind = EBPSemanticTypeKind::Int32;
    Index.Type.ContainerKind.Reset();
    Index.VariableName = IndexName;
    Conversion.Arguments.Add(Index);
    Call.Arguments.Add(Conversion);
    Call.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::Bool, TEXT("true")));
    Call.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::Bool, TEXT("true")));
    FBPSemanticExpression Color = MakeLiteral(EBPSemanticTypeKind::Struct,
        TEXT("(R=0.000000,G=0.660000,B=1.000000,A=1.000000)"));
    Color.Type.TypePath = TEXT("/Script/CoreUObject.LinearColor");
    Call.Arguments.Add(Color);
    Call.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::Float, TEXT("2.0")));
    Call.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::String, TEXT("")));
    return Statement;
}

FBPSemanticIR MakeLoopIR(const FName PrintedName)
{
    FBPSemanticIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForLoop;
    Loop.LoopIndexVariable = TEXT("Index");
    Loop.LoopFirstIndex = MakeLiteral(EBPSemanticTypeKind::Int32, TEXT("0"));
    Loop.LoopLastIndex = MakeLiteral(EBPSemanticTypeKind::Int32, TEXT("2"));
    Loop.LoopBodyStatements.Add(MakePrintIndex(PrintedName));
    Block.Statements.Add(Loop);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticPrintStringScopeTest,
    "BlueprintAIConverter.SemanticCodeGenerator.PrintStringLocalIndexScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticPrintStringScopeTest::RunTest(const FString& Parameters)
{
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;

    const FBPSemanticIR Valid = MakeLoopIR(TEXT("Index"));
    TestTrue(TEXT("ループ内のIndex参照は生成可能"),
        FBPSemanticCodeGenerator::Generate(Valid, TEXT("AScopeTestActor"),
            TEXT(""), Files, Diagnostics));
    TestTrue(TEXT("Indexをローカル参照として出力"),
        Files.SourceContent.Contains(TEXT("Conv_IntToString(Index)")));

    const FBPSemanticIR Unknown = MakeLoopIR(TEXT("UnknownIndex"));
    TestFalse(TEXT("宣言のないローカル整数参照は拒否"),
        FBPSemanticCodeGenerator::Generate(Unknown, TEXT("AScopeTestActor"),
            TEXT(""), Files, Diagnostics));
    TestTrue(TEXT("失敗時に診断が残る"), Diagnostics.IsEmpty() == false);

    FBPSemanticIR Completed = MakeLoopIR(TEXT("Index"));
    Completed.Functions[0].Blocks[0].Statements[0].CompletedStatements.Add(
        MakePrintIndex(TEXT("Index")));
    TestFalse(TEXT("ループ完了後のIndex参照は拒否"),
        FBPSemanticCodeGenerator::Generate(Completed, TEXT("AScopeTestActor"),
            TEXT(""), Files, Diagnostics));
    return true;
}
#endif
