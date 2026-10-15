#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPSemanticFunctionExtractor.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticInterfaceSignatureTest,
    "BlueprintAIConverter.SemanticIR.InterfaceSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticInterfaceSignatureTest::RunTest(const FString& Parameters)
{
    const UFunction* Location = AActor::StaticClass()->FindFunctionByName(TEXT("K2_GetActorLocation"));
    const UFunction* Rotation = AActor::StaticClass()->FindFunctionByName(TEXT("K2_GetActorRotation"));
    const UFunction* SetLocation = AActor::StaticClass()->FindFunctionByName(TEXT("K2_SetActorLocation"));

    if (TestNotNull(TEXT("Location function"), Location) == false
        || TestNotNull(TEXT("Rotation function"), Rotation) == false
        || TestNotNull(TEXT("SetLocation function"), SetLocation) == false)
    {
        return false;
    }

    // 同じReflection署名は一致する。名前ではなく引数・戻り値の構造を比較する。
    TestTrue(TEXT("Identical signature"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(Location, Location));
    TestFalse(TEXT("Different return type"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(Location, Rotation));
    TestFalse(TEXT("Different parameter count"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(Location, SetLocation));
    TestFalse(TEXT("Null implementation"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(nullptr, Location));
    TestFalse(TEXT("Null declaration"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(Location, nullptr));

    // 実在するReflection関数を複製し、const/staticフラグ差分を検証する。
    // 元のUFunctionを変更するとEditor全体に影響するため、複製のみ変更する。
    UFunction* Modified = DuplicateObject<UFunction>(Location, GetTransientPackage());
    if (TestNotNull(TEXT("Duplicated function"), Modified) == false)
    {
        return false;
    }
    Modified->FunctionFlags ^= FUNC_Static;
    TestFalse(TEXT("Static flag mismatch"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(Location, Modified));
    Modified->FunctionFlags ^= FUNC_Static;
    Modified->FunctionFlags ^= FUNC_Const;
    TestFalse(TEXT("Const flag mismatch"), FBPSemanticFunctionExtractor::MatchesInterfaceSignatureForTest(Location, Modified));

    return true;
}
#endif
