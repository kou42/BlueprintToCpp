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
    // FVector引数としてGetActorLocationのSemantic式を検証する。
    FBPSemanticType Vector;
    Vector.Kind = EBPSemanticTypeKind::Struct;
    Vector.TypePath = TEXT("/Script/CoreUObject.Vector");
    Signature.ParameterTypes.Add(Vector);
    Signature.OutputParameters.Add(false);
    FBPSemanticExpression Location;
    Location.Kind = EBPSemanticExpressionKind::FunctionCall;
    Location.FunctionName = TEXT("K2_GetActorLocation");
    Location.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Location.Type = Vector;
    FBPSemanticExpression Self;
    Self.Kind = EBPSemanticExpressionKind::VariableReference;
    Self.VariableName = TEXT("this");
    Self.Type.Kind = EBPSemanticTypeKind::Object;
    Self.Type.TypePath = TEXT("/Script/Engine.Actor");
    Location.Arguments.Add(Self);
    Arguments.Add(Location);
    Diagnostics.Reset();
    TestTrue(TEXT("Vector input emitted"),
        FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Output, Diagnostics));
    TestEqual(TEXT("Generated expression"), Output, FString(TEXT("GetActorLocation()")));
    Signature.OutputParameters[0] = true;
    TestFalse(TEXT("Output argument rejected"),
        FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Output, Diagnostics));
    TestTrue(TEXT("Failed output cleared"), Output.IsEmpty());
    return true;
}
#endif
