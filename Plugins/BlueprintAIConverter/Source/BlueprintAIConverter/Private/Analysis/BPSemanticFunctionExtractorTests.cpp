#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPSemanticFunctionExtractor.h"
#include "BPEmitterTestActor.h"
#include "Engine/Blueprint.h"
#include "UObject/Class.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticInterfaceSignatureTest,
    "BlueprintAIConverter.SemanticIR.Function.InterfaceSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticInterfaceSignatureTest::RunTest(const FString& Parameters)
{
    const UFunction* Declaration = UBPFunctionSignatureTestInterface::StaticClass()
        ->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(IBPFunctionSignatureTestInterface, ResolveValue));
    const UClass* TestClass = ABPEmitterTestActor::StaticClass();
    TestNotNull(TEXT("Interface宣言を取得"), Declaration);
    TestNotNull(TEXT("テストクラスを取得"), TestClass);
    if (Declaration == nullptr || TestClass == nullptr)
    {
        return false;
    }

    const UFunction* Matching = TestClass->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(ABPEmitterTestActor, MatchingSignature));
    const UFunction* MismatchedType = TestClass->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(ABPEmitterTestActor, MismatchedType));
    const UFunction* MismatchedDirection = TestClass->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(ABPEmitterTestActor, MismatchedDirection));
    const UFunction* MismatchedConst = TestClass->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(ABPEmitterTestActor, MismatchedConst));

    TestNotNull(TEXT("一致候補を取得"), Matching);
    TestNotNull(TEXT("型不一致候補を取得"), MismatchedType);
    TestNotNull(TEXT("方向不一致候補を取得"), MismatchedDirection);
    TestNotNull(TEXT("const不一致候補を取得"), MismatchedConst);
    if (Matching == nullptr || MismatchedType == nullptr ||
        MismatchedDirection == nullptr || MismatchedConst == nullptr)
    {
        return false;
    }

    TestFalse(TEXT("null実装は不一致"),
        FBPSemanticFunctionExtractor::HasMatchingInterfaceSignature(nullptr, Declaration));
    TestTrue(TEXT("宣言順・型・方向・constが一致"),
        FBPSemanticFunctionExtractor::HasMatchingInterfaceSignature(Matching, Declaration));
    TestFalse(TEXT("引数型の不一致を拒否"),
        FBPSemanticFunctionExtractor::HasMatchingInterfaceSignature(MismatchedType, Declaration));
    TestFalse(TEXT("out/ref方向の不一致を拒否"),
        FBPSemanticFunctionExtractor::HasMatchingInterfaceSignature(MismatchedDirection, Declaration));
    TestFalse(TEXT("関数const属性の不一致を拒否"),
        FBPSemanticFunctionExtractor::HasMatchingInterfaceSignature(MismatchedConst, Declaration));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticFunctionVisibilityExtractionTest,
    "BlueprintAIConverter.SemanticIR.Function.VisibilityExtraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticFunctionVisibilityExtractionTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = NewObject<UBlueprint>();
    Blueprint->GeneratedClass = ABPEmitterTestActor::StaticClass();
    FBPSemanticIR IR;
    FBPSemanticFunctionExtractor::Append(Blueprint, IR);
    const auto CheckVisibility = [&](const TCHAR* Name, EBPSemanticFunctionVisibility Expected)
    {
        const FBPSemanticFunction* Function = IR.Functions.FindByPredicate(
            [Name](const FBPSemanticFunction& Candidate) { return Candidate.Name == FName(Name); });
        if (TestNotNull(Name, Function)) { TestEqual(Name, Function->Visibility, Expected); }
    };
    CheckVisibility(TEXT("VerifiedOverride"), EBPSemanticFunctionVisibility::Public);
    CheckVisibility(TEXT("ProtectedVisibility"), EBPSemanticFunctionVisibility::Protected);
    CheckVisibility(TEXT("PrivateVisibility"), EBPSemanticFunctionVisibility::Private);
    TestEqual(TEXT("nullからアクセスを推測しない"),
        FBPSemanticFunctionExtractor::ResolveVisibility(nullptr),
        EBPSemanticFunctionVisibility::Unspecified);
    UFunction* Invalid = NewObject<UFunction>();
    Invalid->FunctionFlags = FUNC_Public | FUNC_Private;
    TestEqual(TEXT("矛盾したアクセスフラグを拒否"),
        FBPSemanticFunctionExtractor::ResolveVisibility(Invalid),
        EBPSemanticFunctionVisibility::Unspecified);
    Invalid->FunctionFlags = FUNC_None;
    TestEqual(TEXT("アクセスフラグなしを推測しない"),
        FBPSemanticFunctionExtractor::ResolveVisibility(Invalid),
        EBPSemanticFunctionVisibility::Unspecified);
    return true;
}

#endif
