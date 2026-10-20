#include "BPGenericFunctionExpressionEmitter.h"
#include "BPFunctionSignatureResolver.h"
#include "BPTypedLiteralEmitter.h"
#include "Misc/LexFromString.h"
#include "Misc/LexToString.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

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
        Target.Type.Kind != EBPSemanticTypeKind::Object ||
        Target.Type.TypePath.IsEmpty())
    {
        // 外部Targetはnull時の戻り値を推測できないため、現段階では拒否する。
        OutDiagnostics.Add(TEXT("Pure function expression requires self target"));
        return false;
    }
    // 外部Targetは三項演算子でnullを検査する。戻り値の既定値が
    // 型安全に構築できる場合だけ対応し、評価時のnull参照を防ぐ。
    FString Prefix;
    FString Fallback;
    if (Target.VariableName == TEXT("this"))
    {
        if (OwnerClass->IsChildOf(FunctionOwner) == false)
        {
            OutDiagnostics.Add(TEXT("Self cannot call pure function"));
            return false;
        }
    }
    else
    {
        const FProperty* Property = OwnerClass->FindPropertyByName(Target.VariableName);
        const FObjectProperty* Object = CastField<FObjectProperty>(Property);
        const UClass* TargetClass = FindObject<UClass>(nullptr, *Target.Type.TypePath);
        if (Object == nullptr || Object->PropertyClass == nullptr ||
            TargetClass == nullptr ||
            Object->PropertyClass->GetPathName() != Target.Type.TypePath ||
            TargetClass->IsChildOf(FunctionOwner) == false)
        {
            OutDiagnostics.Add(TEXT("Unsupported external pure function target"));
            return false;
        }
        const FString PropertyName = Target.VariableName.ToString();
        if (PropertyName.IsEmpty())
        {
            return false;
        }
        for (int32 Index = 0; Index < PropertyName.Len(); ++Index)
        {
            const TCHAR Character = PropertyName[Index];
            const bool bAlpha = (Character >= TEXT('a') && Character <= TEXT('z')) ||
                (Character >= TEXT('A') && Character <= TEXT('Z'));
            const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
            if (bAlpha == false && Character != TEXT('_') &&
                (Index == 0 || bDigit == false))
            {
                OutDiagnostics.Add(TEXT("Invalid target property identifier"));
                return false;
            }
        }
        // 参照戻り値や複雑な型はnull時の安全な代替値を定義できない。
        if (Signature.ReturnType.bIsReference ||
            Signature.ReturnType.ContainerKind != TEXT("none"))
        {
            OutDiagnostics.Add(TEXT("External target return type is unsupported"));
            return false;
        }
        switch (Signature.ReturnType.Kind)
        {
        case EBPSemanticTypeKind::Bool: Fallback = TEXT("false"); break;
        case EBPSemanticTypeKind::Int32: Fallback = TEXT("int32{}"); break;
        case EBPSemanticTypeKind::Int64: Fallback = TEXT("int64{}"); break;
        case EBPSemanticTypeKind::Float: Fallback = TEXT("float{}"); break;
        case EBPSemanticTypeKind::Double: Fallback = TEXT("double{}"); break;
        case EBPSemanticTypeKind::String: Fallback = TEXT("FString{}"); break;
        case EBPSemanticTypeKind::Name: Fallback = TEXT("FName{}"); break;
        default:
            OutDiagnostics.Add(TEXT("External target return type has no safe fallback"));
            return false;
        }
        Prefix = PropertyName + TEXT("->");
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
            : (Argument.Kind == EBPSemanticExpressionKind::Literal &&
                FBPTypedLiteralEmitter::Emit(Argument.Type, Argument.LiteralValue, CppArgument));
        if (bEmitted == false)
        {
            OutDiagnostics.Add(TEXT("Unsupported pure function argument literal"));
            return false;
        }
        Arguments.Add(MoveTemp(CppArgument));
    }
    // C++の関数実引数の評価順序はBlueprintのピン順と一致するとは限らない。
    // 関数呼び出しを含む場合は即時実行ラムダ内で引数を順番に確定させる。
    FString InvocationArguments = FString::Join(Arguments, TEXT(", "));
    FString Preparation;
    bool bHasNestedCall = false;
    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        if (Expression.Arguments[Index + 1].Kind == EBPSemanticExpressionKind::FunctionCall)
        {
            bHasNestedCall = true;
        }
    }
    if (bHasNestedCall)
    {
        TArray<FString> LocalArguments;
        for (int32 Index = 0; Index < Arguments.Num(); ++Index)
        {
            const FString LocalName = FString::Printf(TEXT("BPArg%d"), Index);
            Preparation += FString::Printf(TEXT("const auto %s = %s;\n"),
                *LocalName, *Arguments[Index]);
            LocalArguments.Add(LocalName);
        }
        InvocationArguments = FString::Join(LocalArguments, TEXT(", "));
    }
    const FString CallCpp = FString::Printf(TEXT("%s%s(%s)"), *Prefix, *Name,
        *InvocationArguments);
    if (Target.VariableName != TEXT("this"))
    {
        // Targetプロパティを一度だけ読み取り、検査した同じポインタで呼び出す。
        const FString PropertyName = Target.VariableName.ToString();
        const FString TargetCall = FString::Printf(TEXT("BPTarget->%s(%s)"),
            *Name, *InvocationArguments);
        OutCpp = FString::Printf(
            TEXT("([&]() { auto* BPTarget = %s; if (BPTarget == nullptr) { return %s; }\n%sreturn %s; }())"),
            *PropertyName, *Fallback, *Preparation, *TargetCall);
    }
    else if (bHasNestedCall)
    {
        OutCpp = FString::Printf(TEXT("([&]() { %sreturn %s; }())"),
            *Preparation, *CallCpp);
    }
    else
    {
        OutCpp = CallCpp;
    }
    return true;
}

}

bool FBPGenericFunctionExpressionEmitter::Emit(
    const FBPSemanticExpression& Expression, const UClass* OwnerClass,
    FString& OutCpp, TArray<FString>& OutDiagnostics)
{
    return EmitRecursive(Expression, OwnerClass, OutCpp, OutDiagnostics, 0);
}
