#include "Misc/AutomationTest.h"
#include "BPSemanticIR.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticIRFoundationTest,
    "BlueprintAIConverter.SemanticIR.Foundation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticIRFoundationTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR IR;
    IR.AssetPath = TEXT("/Game/BP_Test");

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;

    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Statement.Expression.FunctionName = TEXT("PrintString");
    Statement.Expression.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");
    Statement.Expression.Type.Kind = EBPSemanticTypeKind::Void;

    FBPSemanticExpression Argument;
    Argument.Kind = EBPSemanticExpressionKind::Literal;
    Argument.Type.Kind = EBPSemanticTypeKind::String;
    Argument.LiteralValue = TEXT("Hello");
    Statement.Expression.Arguments.Add(Argument);
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);

    TestFalse(TEXT("正常なSemantic IRには診断がない"), IR.HasErrors());
    TestEqual(TEXT("関数数"), IR.Functions.Num(), 1);
    TestEqual(TEXT("基本ブロック数"), IR.Functions[0].Blocks.Num(), 1);
    TestEqual(TEXT("呼び出し引数数"), IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments.Num(), 1);
    TestEqual(TEXT("文字列リテラル"), IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[0].LiteralValue, FString(TEXT("Hello")));

    // 不明なノードを成功扱いしないため、診断が存在すれば明示的に失敗状態とする。
    FBPSemanticDiagnostic Diagnostic;
    Diagnostic.GraphName = TEXT("EventGraph");
    Diagnostic.Message = TEXT("Unsupported node");
    IR.Diagnostics.Add(Diagnostic);
    TestTrue(TEXT("未対応ノードの診断を検出"), IR.HasErrors());
    return true;
}

#endif
