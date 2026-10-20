#include "BPGenericFunctionExpressionEmitter.h"
#include "BPFunctionSignatureResolver.h"
#include "Misc/LexFromString.h"
#include "Misc/LexToString.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace
{
// Semanticリテラルを数値として再解析し、任意の文字列をC++へ挿入しない。
bool EmitLiteral(const FBPSemanticExpression& Expression, FString& Out)
{
    if (Expression.Kind != EBPSemanticExpressionKind::Literal ||
        Expression.Type.ContainerKind != TEXT("none") ||
        Expression.Type.bIsReference)
    {
        return false;
    }
    if (Expression.Type.Kind == EBPSemanticTypeKind::Bool)
    {
        if (Expression.LiteralValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
        {
            Out = TEXT("true");
            return true;
        }
        if (Expression.LiteralValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
        {
            Out = TEXT("false");
            return true;
        }
        return false;
    }
    if (Expression.Type.Kind == EBPSemanticTypeKind::Int32)
    {
        int32 Value = 0;
        if (LexTryParseString(Value, *Expression.LiteralValue) == false)
        {
            return false;
        }
        Out = LexToString(Value);
        return true;
    }
    if (Expression.Type.Kind == EBPSemanticTypeKind::Int64)
    {
        int64 Value = 0;
        if (LexTryParseString(Value, *Expression.LiteralValue) == false)
        {
            return false;
        }
        Out = FString::Printf(TEXT("static_cast<int64>(%s)"), *LexToString(Value));
        return true;
    }
    if (Expression.Type.Kind == EBPSemanticTypeKind::Float ||
        Expression.Type.Kind == EBPSemanticTypeKind::Double)
    {
        double Value = 0.0;
        if (LexTryParseString(Value, *Expression.LiteralValue) == false ||
            FMath::IsFinite(Value) == false)
        {
            return false;
        }
        if (Expression.Type.Kind == EBPSemanticTypeKind::Float)
        {
            const float Narrowed = static_cast<float>(Value);
            if (FMath::IsFinite(Narrowed) == false)
            {
                return false;
            }
            Out = FString::Printf(TEXT("static_cast<float>(%s)"), *LexToString(Narrowed));
        }
        else
        {
            Out = LexToString(Value);
        }
        return true;
    }
    return false;
}
}

namespace
{
bool EmitRecursive(const FBPSemanticExpression& Expression, const UClass* OwnerClass,
    FString& OutCpp, TArray<FString>& OutDiagnostics, int32 Depth)
{
    OutCpp.Reset();
    // 深い式ツリーを出力しない。Lowering側と同じ上限を適用する。
    if (Depth >= 32)
    {
        OutDiagnostics.Add(TEXT("Pure expression depth limit"));
        return false;
    }
    if (OwnerClass == nullptr ||
        Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Expression.Arguments.IsEmpty())
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
        Signature.ParameterNames.Num() + 1 != Expression.Arguments.Num() ||
        Signature.ParameterNames.Num() != Signature.ParameterTypes.Num() ||
        Signature.ParameterNames.Num() != Signature.OutputParameters.Num() ||
        Signature.ReturnType.Kind == EBPSemanticTypeKind::Void ||
        Signature.ReturnType.Kind != Expression.Type.Kind ||
        Signature.ReturnType.TypePath != Expression.Type.TypePath ||
        Signature.ReturnType.ContainerKind != Expression.Type.ContainerKind ||
        Signature.ReturnType.bIsReference != Expression.Type.bIsReference ||
        Signature.ReturnType.bIsConst != Expression.Type.bIsConst)
    {
        OutDiagnostics.Add(TEXT("Pure function signature mismatch"));
        return false;
    }

    const FBPSemanticExpression& Target = Expression.Arguments[0];
    if (Target.Kind != EBPSemanticExpressionKind::VariableReference ||
        Target.VariableName != TEXT("this") ||
        Target.Type.Kind != EBPSemanticTypeKind::Object ||
        Target.Type.TypePath.IsEmpty() ||
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
    TArray<FString> Arguments;
    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index])
        {
            OutDiagnostics.Add(TEXT("Output parameters cannot be expression arguments"));
            return false;
        }
        const FBPSemanticExpression& Argument = Expression.Arguments[Index + 1];
        const FBPSemanticType& Expected = Signature.ParameterTypes[Index];
        if (Argument.Type.Kind != Expected.Kind ||
            Argument.Type.TypePath != Expected.TypePath ||
            Argument.Type.ContainerKind != Expected.ContainerKind ||
            Argument.Type.bIsReference != Expected.bIsReference ||
            Argument.Type.bIsConst != Expected.bIsConst)
        {
            OutDiagnostics.Add(TEXT("Pure function argument type mismatch"));
            return false;
        }
        FString CppArgument;
        const bool bEmitted = Argument.Kind == EBPSemanticExpressionKind::FunctionCall
            ? EmitRecursive(Argument, OwnerClass, CppArgument, OutDiagnostics, Depth + 1)
            : EmitLiteral(Argument, CppArgument);
        if (bEmitted == false)
        {
            OutDiagnostics.Add(TEXT("Unsupported pure function argument literal"));
            return false;
        }
        Arguments.Add(MoveTemp(CppArgument));
    }
    OutCpp = FString::Printf(TEXT("%s(%s)"), *Name,
        *FString::Join(Arguments, TEXT(", ")));
    return true;
}

}

bool FBPGenericFunctionExpressionEmitter::Emit(
    const FBPSemanticExpression& Expression, const UClass* OwnerClass,
    FString& OutCpp, TArray<FString>& OutDiagnostics)
{
    return EmitRecursive(Expression, OwnerClass, OutCpp, OutDiagnostics, 0);
}
