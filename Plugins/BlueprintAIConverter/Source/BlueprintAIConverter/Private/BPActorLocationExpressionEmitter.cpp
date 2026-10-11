#include "BPActorLocationExpressionEmitter.h"

bool FBPActorLocationExpressionEmitter::Emit(const FBPSemanticExpression& Expression,
    FString& OutCpp, TArray<FString>& OutDiagnostics)
{
    OutCpp.Reset();

    // 反射名とC++ API名の対応を明示的に限定し、未知の関数を出力しない。
    if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Expression.FunctionName != TEXT("K2_GetActorLocation") ||
        Expression.FunctionOwnerPath != TEXT("/Script/Engine.Actor") ||
        Expression.Type.Kind != EBPSemanticTypeKind::Struct ||
        Expression.Type.TypePath != TEXT("/Script/CoreUObject.Vector") ||
        Expression.Type.bIsReference || Expression.Type.bIsConst ||
        Expression.Arguments.Num() != 1)
    {
        OutDiagnostics.Add(TEXT("Unsupported Actor location expression"));
        return false;
    }

    const FBPSemanticExpression& Target = Expression.Arguments[0];
    if (Target.Kind != EBPSemanticExpressionKind::VariableReference ||
        Target.VariableName != TEXT("this") ||
        Target.Type.Kind != EBPSemanticTypeKind::Object ||
        Target.Type.TypePath != TEXT("/Script/Engine.Actor") ||
        Target.Type.bIsReference ||
        Target.Arguments.IsEmpty() == false)
    {
        OutDiagnostics.Add(TEXT("Unsupported Actor location call target"));
        return false;
    }

    OutCpp = TEXT("GetActorLocation()");
    return true;
}
