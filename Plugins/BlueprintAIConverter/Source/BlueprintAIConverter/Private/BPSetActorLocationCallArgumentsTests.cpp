#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPSetActorLocationCallArguments.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSetActorLocationCallArgumentsTest,
    "BlueprintAIConverter.SemanticIR.Emitter.SetActorLocationArguments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSetActorLocationCallArgumentsTest::RunTest(const FString& Parameters)
{
    FString Arguments;
    const FString Variable = TEXT("BP_Hit_0123456789ABCDEF0123456789ABCDEF");
    TestTrue(TEXT("Supported arguments emitted"),
        FBPSetActorLocationCallArguments::Emit(TEXT("GetActorLocation()"),
            TEXT("true"), Variable, TEXT("false"), Arguments));
    TestEqual(TEXT("K2 call uses reference argument order"), Arguments,
        FString(TEXT("GetActorLocation(), true, BP_Hit_0123456789ABCDEF0123456789ABCDEF, false")));
    TestFalse(TEXT("Arbitrary location expression rejected"),
        FBPSetActorLocationCallArguments::Emit(TEXT("Unknown()"), TEXT("true"),
            Variable, TEXT("false"), Arguments));
    TestTrue(TEXT("Rejected arguments cleared"), Arguments.IsEmpty());
    TestFalse(TEXT("Injected identifier rejected"),
        FBPSetActorLocationCallArguments::Emit(TEXT("GetActorLocation()"), TEXT("true"),
            TEXT("BP_Hit_0123456789ABCDEF0123456789ABCDE;"), TEXT("false"), Arguments));
    TestFalse(TEXT("Unnormalized bool rejected"),
        FBPSetActorLocationCallArguments::Emit(TEXT("GetActorLocation()"), TEXT("1"),
            Variable, TEXT("false"), Arguments));
    return true;
}
#endif
