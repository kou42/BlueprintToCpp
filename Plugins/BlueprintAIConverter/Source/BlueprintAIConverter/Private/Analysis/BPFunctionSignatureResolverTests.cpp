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
        TestTrue(TEXT("検証可能なWorldContext metadataを受理"),
            FBPFunctionSignatureResolver::Resolve(Print, Signature, Diagnostics));
        TestTrue(TEXT("WorldContext解決に診断なし"), Diagnostics.IsEmpty());
        TestEqual(TEXT("PrintString関数名"), Signature.FunctionName, FName(TEXT("PrintString")));
        TestTrue(TEXT("PrintStringはstatic"), Signature.bStatic);
        TestTrue(TEXT("WorldContext引数を保持"), Signature.ParameterNames.IsEmpty() == false);
        if (Signature.ParameterNames.IsEmpty() == false)
        {
            TestEqual(TEXT("先頭引数はWorldContextObject"),
                Signature.ParameterNames[0], FName(TEXT("WorldContextObject")));
            TestEqual(TEXT("WorldContextはObject型"), Signature.ParameterTypes[0].Kind,
                EBPSemanticTypeKind::Object);
        }
        const int32 ContextIndex = Signature.ParameterNames.IndexOfByKey(
            FName(TEXT("WorldContextObject")));
        const int32 DurationIndex = Signature.ParameterNames.IndexOfByKey(
            FName(TEXT("Duration")));
        const int32 KeyIndex = Signature.ParameterNames.IndexOfByKey(FName(TEXT("Key")));
        TestTrue(TEXT("既定値配列は引数配列と整列"),
            Signature.bHasParameterDefaultValues.Num() == Signature.ParameterNames.Num() &&
            Signature.ParameterDefaultValues.Num() == Signature.ParameterNames.Num());
        TestTrue(TEXT("WorldContextには暗黙の既定値を捏造しない"),
            ContextIndex != INDEX_NONE &&
            Signature.bHasParameterDefaultValues[ContextIndex] == false);
        TestTrue(TEXT("DurationのReflection既定値を保持"),
            DurationIndex != INDEX_NONE &&
            Signature.bHasParameterDefaultValues[DurationIndex]);
        TestTrue(TEXT("KeyのReflection既定値を保持"),
            KeyIndex != INDEX_NONE && Signature.bHasParameterDefaultValues[KeyIndex]);
    }
    return true;
}
#endif
