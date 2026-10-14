#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPNumericArithmeticResolver.h"
#include "Kismet/KismetMathLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPNumericArithmeticResolverTest,
    "BlueprintAIConverter.Semantic.NumericArithmeticResolver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPNumericArithmeticResolverTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        const TCHAR* FunctionName;
        EBPSemanticTypeKind Type;
    };
    const FCase Cases[] = {
        { TEXT("Add_IntInt"), EBPSemanticTypeKind::Int32 },
        { TEXT("Subtract_IntInt"), EBPSemanticTypeKind::Int32 },
        { TEXT("Multiply_IntInt"), EBPSemanticTypeKind::Int32 },
        { TEXT("Divide_IntInt"), EBPSemanticTypeKind::Int32 },
        { TEXT("Add_DoubleDouble"), EBPSemanticTypeKind::Double },
        { TEXT("Subtract_DoubleDouble"), EBPSemanticTypeKind::Double },
        { TEXT("Multiply_DoubleDouble"), EBPSemanticTypeKind::Double },
        { TEXT("Divide_DoubleDouble"), EBPSemanticTypeKind::Double }
    };
    const FString OwnerPath = UKismetMathLibrary::StaticClass()->GetPathName();
    for (const FCase& Case : Cases)
    {
        FBPNumericArithmeticOperation Operation;
        TArray<FString> Diagnostics;
        const FString Label = FString::Printf(
            TEXT("%sを反射署名から解決"), Case.FunctionName);
        if (TestTrue(*Label, FBPNumericArithmeticResolver::Resolve(
            OwnerPath, FName(Case.FunctionName), Operation, Diagnostics)) == false)
        {
            for (const FString& Diagnostic : Diagnostics)
            {
                AddError(Diagnostic);
            }
            continue;
        }
        TestEqual(TEXT("算術型を保持"),
            static_cast<uint8>(Operation.OperandType),
            static_cast<uint8>(Case.Type));
    }

    FBPNumericArithmeticOperation Rejected;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("所有クラス改ざんを拒否"),
        FBPNumericArithmeticResolver::Resolve(TEXT("/Script/Engine.Actor"),
            TEXT("Add_IntInt"), Rejected, Diagnostics));
    Diagnostics.Reset();
    TestFalse(TEXT("未許可の剰余を拒否"),
        FBPNumericArithmeticResolver::Resolve(OwnerPath,
            TEXT("Percent_IntInt"), Rejected, Diagnostics));
    return true;
}

#endif
