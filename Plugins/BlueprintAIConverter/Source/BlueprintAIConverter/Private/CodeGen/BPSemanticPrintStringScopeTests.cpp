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
    Call.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::Name, TEXT("")));
    return Statement;
}

// Concat -> Conv_IntToString -> Add_IntInt の複合式を構築する。
// 単純なConvと同じスコープ規則がネストした式にも適用されることを検証する。
FBPSemanticStatement MakeNestedPrintIndex(const FName IndexName)
{
    FBPSemanticStatement Statement = MakePrintIndex(IndexName);
    FBPSemanticExpression& Conversion = Statement.Expression.Arguments[1];
    FBPSemanticExpression Addition;
    Addition.Kind = EBPSemanticExpressionKind::FunctionCall;
    Addition.Type.Kind = EBPSemanticTypeKind::Int32;
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.Arguments.Add(Conversion.Arguments[0]);
    Addition.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::Int32, TEXT("1")));
    Conversion.Arguments.Reset();
    Conversion.Arguments.Add(Addition);

    FBPSemanticExpression Concat;
    Concat.Kind = EBPSemanticExpressionKind::FunctionCall;
    Concat.Type.Kind = EBPSemanticTypeKind::String;
    Concat.FunctionName = TEXT("Concat_StrStr");
    Concat.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Concat.Arguments.Add(MakeLiteral(EBPSemanticTypeKind::String, TEXT("Index: ")));
    Concat.Arguments.Add(Conversion);
    Statement.Expression.Arguments[1] = MoveTemp(Concat);
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
FBPSemanticIR MakeForEachIR(const FName PrintedName)
{
    FBPSemanticIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticVariable Array;
    Array.Name = TEXT("Numbers");
    Array.Type.Kind = EBPSemanticTypeKind::Int32;
    Array.Type.ContainerKind = TEXT("array");
    Array.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Array);

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = Array.Name;
    Loop.LoopArray.Type = Array.Type;
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopIndexVariable = TEXT("LoopIndex");
    Loop.LoopElementVariable = TEXT("LoopElement");
    Loop.LoopBodyStatements.Add(MakeNestedPrintIndex(PrintedName));
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
    // ネスト式でもループIndexを有効なローカル参照として扱う。
    FBPSemanticIR Nested = MakeLoopIR(TEXT("Index"));
    Nested.Functions[0].Blocks[0].Statements[0].LoopBodyStatements[0] =
        MakeNestedPrintIndex(TEXT("Index"));
    TestTrue(TEXT("Concat内の整数加算とIndexを生成可能"),
        FBPSemanticCodeGenerator::Generate(Nested, TEXT("AScopeTestActor"),
            TEXT(""), Files, Diagnostics));
    TestTrue(TEXT("ネスト式の加算を出力"),
        Files.SourceContent.Contains(TEXT("Conv_IntToString((Index + 1))")));

    Nested.Functions[0].Blocks[0].Statements[0].LoopBodyStatements[0] =
        MakeNestedPrintIndex(TEXT("UnknownIndex"));
    TestFalse(TEXT("Concat内の未宣言Indexを拒否"),
        FBPSemanticCodeGenerator::Generate(Nested, TEXT("AScopeTestActor"),
            TEXT(""), Files, Diagnostics));

    // SourcePinIdを偽装しても、存在しないint32メンバー参照は許可しない。
    Nested.Functions[0].Blocks[0].Statements[0].LoopBodyStatements[0] =
        MakeNestedPrintIndex(TEXT("MissingMember"));
    FBPSemanticExpression& Member = Nested.Functions[0].Blocks[0].Statements[0]
        .LoopBodyStatements[0].Expression.Arguments[1].Arguments[1].Arguments[0].Arguments[0];
    Member.SourcePinId = FGuid::NewGuid();
    TestFalse(TEXT("Concat内の未宣言int32メンバーを拒否"),
        FBPSemanticCodeGenerator::Generate(Nested, TEXT("AScopeTestActor"),
            TEXT(""), Files, Diagnostics));

    // ForEachのネスト式ではElement参照を再帰検出し、必要時のみ宣言する。
    FBPSemanticIR ForEach = MakeForEachIR(TEXT("LoopElement"));
    if (TestTrue(TEXT("ForEach内のConcatとElement加算を生成"),
        FBPSemanticCodeGenerator::Generate(ForEach, TEXT("AForEachScopeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)))
    {
        TestTrue(TEXT("ネストElementのローカル宣言を生成"),
            Files.SourceContent.Contains(TEXT("const int32 LoopElement = Numbers[LoopIndex];")));
        TestTrue(TEXT("Elementの加算式を生成"),
            Files.SourceContent.Contains(TEXT("Conv_IntToString((LoopElement + 1))")));
    }
    // Indexだけを参照する場合、不要なElementのコピーを生成しない。
    ForEach = MakeForEachIR(TEXT("LoopIndex"));
    if (TestTrue(TEXT("ForEach内のネストIndexを生成"),
        FBPSemanticCodeGenerator::Generate(ForEach, TEXT("AForEachScopeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)))
    {
        TestFalse(TEXT("Element未使用なら宣言しない"),
            Files.SourceContent.Contains(TEXT("const int32 LoopElement =")));
    }
    ForEach = MakeForEachIR(TEXT("UnknownIndex"));
    TestFalse(TEXT("ForEach内の未宣言ローカルを拒否"),
        FBPSemanticCodeGenerator::Generate(ForEach, TEXT("AForEachScopeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));

    ForEach = MakeForEachIR(TEXT("LoopElement"));
    ForEach.Functions[0].Blocks[0].Statements[0].CompletedStatements.Add(
        MakeNestedPrintIndex(TEXT("LoopElement")));
    TestFalse(TEXT("ForEach CompletedからElementを参照できない"),
        FBPSemanticCodeGenerator::Generate(ForEach, TEXT("AForEachScopeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));

    // 不正なC++識別子をSourcePinId付きメンバーとして偽装しても拒否する。
    // ネスト整数式から未検証の識別子がC++へ漏れることを防ぐ。
    ForEach = MakeForEachIR(TEXT("Invalid-Member"));
    FBPSemanticExpression& InvalidMember = ForEach.Functions[0].Blocks[0].Statements[0]
        .LoopBodyStatements[0].Expression.Arguments[1].Arguments[1].Arguments[0].Arguments[0];
    InvalidMember.SourcePinId = FGuid::NewGuid();
    TestFalse(TEXT("Concat内の不正なメンバー識別子を拒否"),
        FBPSemanticCodeGenerator::Generate(ForEach, TEXT("AForEachScopeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));

    return true;
}
#endif
