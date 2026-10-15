#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPNumericComparisonResolver.h"
#include "Kismet/KismetMathLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPNumericComparisonResolverTest,
    "BlueprintAIConverter.Semantic.NumericComparisonResolver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPNumericComparisonResolverTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        const TCHAR* FunctionName;
        const TCHAR* OperatorToken;
        EBPSemanticTypeKind OperandType;
    };
    const FCase Cases[] = {
        { TEXT("Less_IntInt"), TEXT("<"), EBPSemanticTypeKind::Int32 },
        { TEXT("LessEqual_IntInt"), TEXT("<="), EBPSemanticTypeKind::Int32 },
        { TEXT("Greater_IntInt"), TEXT(">"), EBPSemanticTypeKind::Int32 },
        { TEXT("GreaterEqual_IntInt"), TEXT(">="), EBPSemanticTypeKind::Int32 },
        { TEXT("EqualEqual_IntInt"), TEXT("=="), EBPSemanticTypeKind::Int32 },
        { TEXT("NotEqual_IntInt"), TEXT("!="), EBPSemanticTypeKind::Int32 },
        { TEXT("Less_DoubleDouble"), TEXT("<"), EBPSemanticTypeKind::Double },
        { TEXT("LessEqual_DoubleDouble"), TEXT("<="), EBPSemanticTypeKind::Double },
        { TEXT("Greater_DoubleDouble"), TEXT(">"), EBPSemanticTypeKind::Double },
        { TEXT("GreaterEqual_DoubleDouble"), TEXT(">="), EBPSemanticTypeKind::Double },
        { TEXT("EqualEqual_DoubleDouble"), TEXT("=="), EBPSemanticTypeKind::Double },
        { TEXT("NotEqual_DoubleDouble"), TEXT("!="), EBPSemanticTypeKind::Double }
    };

    const FString OwnerPath = UKismetMathLibrary::StaticClass()->GetPathName();
    for (const FCase& Case : Cases)
    {
        FBPNumericComparison Comparison;
        TArray<FString> Diagnostics;
        const FString Label = FString::Printf(TEXT("%sを反射署名から解決"), Case.FunctionName);
        if (TestTrue(*Label, FBPNumericComparisonResolver::Resolve(
            OwnerPath, FName(Case.FunctionName), Comparison, Diagnostics)) == false)
        {
            for (const FString& Diagnostic : Diagnostics)
            {
                AddError(Diagnostic);
            }
            continue;
        }
        TestEqual(TEXT("演算子を解決"), Comparison.OperatorToken,
            FString(Case.OperatorToken));
        TestEqual(TEXT("オペランド型を解決"),
            static_cast<uint8>(Comparison.OperandType),
            static_cast<uint8>(Case.OperandType));
    }

    FBPNumericComparison Rejected;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("所有クラスの改ざんを拒否"),
        FBPNumericComparisonResolver::Resolve(TEXT("/Script/Engine.Actor"),
            TEXT("Greater_IntInt"), Rejected, Diagnostics));
    Diagnostics.Reset();
    TestFalse(TEXT("比較ではない数値関数を拒否"),
        FBPNumericComparisonResolver::Resolve(OwnerPath,
            TEXT("Add_IntInt"), Rejected, Diagnostics));

    FString Canonical;
    TestTrue(TEXT("doubleリテラルを正規化"),
        FBPNumericComparisonResolver::CanonicalizeLiteral(
            EBPSemanticTypeKind::Double, TEXT("7.5000"), Canonical));
    TestFalse(TEXT("非有限doubleを拒否"),
        FBPNumericComparisonResolver::CanonicalizeLiteral(
            EBPSemanticTypeKind::Double, TEXT("nan"), Canonical));
    return true;
}

#endif
