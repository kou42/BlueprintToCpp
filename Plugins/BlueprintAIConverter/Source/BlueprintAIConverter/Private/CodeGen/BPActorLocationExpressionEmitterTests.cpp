#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPActorLocationExpressionEmitter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPActorLocationEmitterTest,
    "BlueprintAIConverter.SemanticIR.Emitter.ActorLocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPActorLocationEmitterTest::RunTest(const FString& Parameters)
{
    FBPSemanticExpression Call;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.FunctionName = TEXT("K2_GetActorLocation");
    Call.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.Type.Kind = EBPSemanticTypeKind::Struct;
    Call.Type.TypePath = TEXT("/Script/CoreUObject.Vector");
    FBPSemanticExpression Self;
    Self.Kind = EBPSemanticExpressionKind::VariableReference;
    Self.VariableName = TEXT("this");
    Self.Type.Kind = EBPSemanticTypeKind::Object;
    Self.Type.TypePath = TEXT("/Script/Engine.Actor");
    Call.Arguments.Add(Self);

    FString Cpp;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Emit supported Actor location"), FBPActorLocationExpressionEmitter::Emit(Call, Cpp, Diagnostics));
    TestEqual(TEXT("C++ expression"), Cpp, FString(TEXT("GetActorLocation()")));

    Call.Type.TypePath = TEXT("/Script/CoreUObject.Rotator");
    TestFalse(TEXT("Reject wrong struct"), FBPActorLocationExpressionEmitter::Emit(Call, Cpp, Diagnostics));
    TestTrue(TEXT("Clear failed output"), Cpp.IsEmpty());

    Call.Type.TypePath = TEXT("/Script/CoreUObject.Vector");
    Call.Arguments[0].VariableName = TEXT("OtherActor");
    Diagnostics.Reset();
    TestTrue(TEXT("Emit typed Actor target"),
        FBPActorLocationExpressionEmitter::Emit(Call, Cpp, Diagnostics));
    TestEqual(TEXT("Null-safe target expression"), Cpp, FString(
        TEXT("(OtherActor != nullptr ? OtherActor->GetActorLocation() : FVector::ZeroVector)")));
    Call.Arguments[0].VariableName = TEXT("Other-Actor");
    TestFalse(TEXT("Reject invalid target identifier"),
        FBPActorLocationExpressionEmitter::Emit(Call, Cpp, Diagnostics));
    TestTrue(TEXT("Report rejected identifier"), Diagnostics.IsEmpty() == false);
    return true;
}
#endif
