#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPFunctionSignatureResolver.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPFunctionSignatureResolverTest,
    "BlueprintAIConverter.SemanticIR.FunctionSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPFunctionSignatureResolverTest::RunTest(const FString& Parameters)
{
    FBPResolvedFunctionSignature Signature;
    TArray<FString> Diagnostics;

    TestFalse(TEXT("Null UFunction rejected"),
        FBPFunctionSignatureResolver::Resolve(nullptr, Signature, Diagnostics));
    TestTrue(TEXT("Null function produces diagnostics"), Diagnostics.IsEmpty() == false);

    Diagnostics.Reset();
    const UFunction* Location = AActor::StaticClass()->FindFunctionByName(TEXT("K2_GetActorLocation"));
    if (TestNotNull(TEXT("Actor location UFunction exists"), Location))
    {
        TestTrue(TEXT("Actor location signature resolved"),
            FBPFunctionSignatureResolver::Resolve(Location, Signature, Diagnostics));
        TestEqual(TEXT("Location return type"), Signature.ReturnType.Kind, EBPSemanticTypeKind::Struct);
        TestFalse(TEXT("Location is not static"), Signature.bStatic);
    }

    Diagnostics.Reset();
    const UFunction* Print = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
    if (TestNotNull(TEXT("PrintString UFunction exists"), Print))
    {
        TestFalse(TEXT("WorldContext metadata rejected"),
            FBPFunctionSignatureResolver::Resolve(Print, Signature, Diagnostics));
        TestTrue(TEXT("Rejected function has diagnostics"), Diagnostics.IsEmpty() == false);
        TestTrue(TEXT("Rejected signature is reset"), Signature.FunctionName.IsNone());
    }
    return true;
}
#endif
