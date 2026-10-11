#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPBooleanPinDefaultEmitter.h"
#include "BPFunctionArgumentEmitter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPBooleanPinDefaultEmitterTest,
    "BlueprintAIConverter.SemanticIR.Emitter.BooleanPinDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPBooleanPinDefaultEmitterTest::RunTest(const FString& Parameters)
{
    FBPIRPin Pin;
    Pin.Direction = EGPD_Input;
    Pin.Category = UEdGraphSchema_K2::PC_Boolean;
    FString Cpp;

    Pin.DefaultValue = TEXT("True");
    TestTrue(TEXT("True accepted"), FBPBooleanPinDefaultEmitter::Emit(Pin, Cpp));
    TestEqual(TEXT("True normalized"), Cpp, FString(TEXT("true")));
    Pin.DefaultValue = TEXT("FALSE");
    TestTrue(TEXT("False accepted"), FBPBooleanPinDefaultEmitter::Emit(Pin, Cpp));
    TestEqual(TEXT("False normalized"), Cpp, FString(TEXT("false")));
    Pin.DefaultValue = TEXT("false); DangerousCall();");
    TestFalse(TEXT("Arbitrary C++ rejected"), FBPBooleanPinDefaultEmitter::Emit(Pin, Cpp));
    TestTrue(TEXT("Rejected value clears output"), Cpp.IsEmpty());
    Pin.DefaultValue = TEXT("true");
    Pin.Direction = EGPD_Output;
    TestFalse(TEXT("Output pin rejected"), FBPBooleanPinDefaultEmitter::Emit(Pin, Cpp));

    FBPResolvedFunctionSignature Signature;
    Signature.FunctionName = TEXT("Example");
    Signature.ParameterNames.Add(TEXT("Flag"));
    FBPSemanticType Type;
    Type.Kind = EBPSemanticTypeKind::Bool;
    Signature.ParameterTypes.Add(Type);
    Signature.OutputParameters.Add(false);
    FBPSemanticExpression Expression;
    Expression.Kind = EBPSemanticExpressionKind::Literal;
    Expression.Type = Type;
    Expression.LiteralValue = TEXT("true");
    TArray<FBPSemanticExpression> Arguments;
    Arguments.Add(Expression);
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Bool argument emitted"),
        FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Cpp, Diagnostics));
    TestEqual(TEXT("Bool argument C++"), Cpp, FString(TEXT("true")));
    Arguments[0].LiteralValue = TEXT("1");
    TestFalse(TEXT("Noncanonical bool literal rejected"),
        FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Cpp, Diagnostics));
    TestTrue(TEXT("Failure clears arguments"), Cpp.IsEmpty());
    return true;
}
#endif
