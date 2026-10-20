#include "BPGenericFunctionExpressionEmitter.h"
#include "BPFunctionSignatureResolver.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

bool FBPGenericFunctionExpressionEmitter::Emit(
    const FBPSemanticExpression& Expression, const UClass* OwnerClass,
    FString& OutCpp, TArray<FString>& OutDiagnostics)
{
    OutCpp.Reset();
    if (OwnerClass == nullptr ||
        Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Expression.Arguments.Num() != 1)
    {
        OutDiagnostics.Add(TEXT("Invalid function expression"));
        return false;
    }

    const UClass* FunctionOwner = FindObject<UClass>(
        nullptr, *Expression.FunctionOwnerPath);
    const UFunction* Function = FunctionOwner != nullptr
        ? FunctionOwner->FindFunctionByName(Expression.FunctionName) : nullptr;
    if (Function == nullptr || Function->GetOwnerClass() != FunctionOwner ||
        Function->HasAnyFunctionFlags(FUNC_Native) == false ||
        Function->HasAnyFunctionFlags(FUNC_BlueprintPure) == false ||
        Function->HasAnyFunctionFlags(FUNC_Static | FUNC_BlueprintEvent))
    {
        OutDiagnostics.Add(TEXT("Unsupported native pure function"));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    if (FBPFunctionSignatureResolver::Resolve(Function, Signature, OutDiagnostics) == false ||
        Signature.ParameterNames.IsEmpty() == false ||
        Signature.ReturnType.Kind == EBPSemanticTypeKind::Void ||
        Signature.ReturnType.Kind != Expression.Type.Kind ||
        Signature.ReturnType.TypePath != Expression.Type.TypePath ||
        Signature.ReturnType.ContainerKind != Expression.Type.ContainerKind ||
        Signature.ReturnType.bIsReference != Expression.Type.bIsReference)
    {
        OutDiagnostics.Add(TEXT("Pure function signature mismatch"));
        return false;
    }

    const FBPSemanticExpression& Target = Expression.Arguments[0];
    if (Target.Kind != EBPSemanticExpressionKind::VariableReference ||
        Target.VariableName != TEXT("this") ||
        Target.Type.Kind != EBPSemanticTypeKind::Object ||
        OwnerClass->IsChildOf(FunctionOwner) == false)
    {
        // 外部Targetはnull時の戻り値を推測できないため、現段階では拒否する。
        OutDiagnostics.Add(TEXT("Pure function expression requires self target"));
        return false;
    }
    const FString Name = Function->GetName();
    if (Name.IsEmpty())
    {
        OutDiagnostics.Add(TEXT("Empty function name"));
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
            OutDiagnostics.Add(TEXT("Invalid function identifier"));
            return false;
        }
    }
    OutCpp = FString::Printf(TEXT("%s()"), *Name);
    return true;
}
