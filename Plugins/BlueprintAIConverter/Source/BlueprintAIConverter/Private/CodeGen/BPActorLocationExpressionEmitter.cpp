#include "BPActorLocationExpressionEmitter.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

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
        Target.Type.Kind != EBPSemanticTypeKind::Object ||
        Target.Type.bIsReference ||
        Target.Arguments.IsEmpty() == false)
    {
        OutDiagnostics.Add(TEXT("Unsupported Actor location call target"));
        return false;
    }

    const UClass* TargetClass = FindObject<UClass>(nullptr, *Target.Type.TypePath);
    const UClass* ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.Actor"));
    if (TargetClass == nullptr || ActorClass == nullptr ||
        TargetClass->IsChildOf(ActorClass) == false)
    {
        OutDiagnostics.Add(TEXT("Target must be a reflected Actor class"));
        return false;
    }
    if (Target.VariableName == TEXT("this"))
    {
        OutCpp = TEXT("GetActorLocation()");
        return true;
    }
    // 対象変数のC++識別子を検証し、未検証の文字列をコードへ挿入しない。
    const FString Name = Target.VariableName.ToString();
    if (Name.IsEmpty())
    {
        OutDiagnostics.Add(TEXT("Empty target variable name"));
        return false;
    }
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Ch = Name[Index];
        const bool bAlpha = (Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
            (Ch >= TEXT('a') && Ch <= TEXT('z'));
        const bool bDigit = Ch >= TEXT('0') && Ch <= TEXT('9');
        if (bAlpha == false && Ch != TEXT('_') && (Index == 0 || bDigit == false))
        {
            OutDiagnostics.Add(TEXT("Invalid target variable identifier"));
            return false;
        }
    }
    // nullの可能性がある外部オブジェクトはゼロベクトルへフォールバックする。
    OutCpp = FString::Printf(TEXT("(%s != nullptr ? %s->GetActorLocation() : FVector::ZeroVector)"),
        *Name, *Name);
    return true;
}
