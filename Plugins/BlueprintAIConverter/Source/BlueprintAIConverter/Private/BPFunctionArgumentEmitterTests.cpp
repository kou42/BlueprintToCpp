#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPFunctionArgumentEmitter.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPArgumentEmitterTest, "BlueprintAIConverter.SemanticIR.Emitter.Arguments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBPArgumentEmitterTest::RunTest(const FString& Parameters)
{
    FBPResolvedFunctionSignature Signature;
    Signature.FunctionName = TEXT("Example");
    TArray<FBPSemanticExpression> Arguments;
    TArray<FString> Diagnostics;
    FString Output;
    TestTrue(TEXT("Empty parameter list"), FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Output, Diagnostics));
    TestTrue(TEXT("Empty output"), Output.IsEmpty());
    Signature.ParameterNames.Add(TEXT("Location"));
    TestFalse(TEXT("Incomplete signature rejected"), FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Output, Diagnostics));
    return true;
}
#endif
