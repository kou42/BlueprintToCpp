#include "BPFunctionArgumentEmitter.h"

#include "BPActorLocationExpressionEmitter.h"

bool FBPFunctionArgumentEmitter::Emit(const FBPResolvedFunctionSignature& Signature,
    const TArray<FBPSemanticExpression>& Arguments, FString& OutArguments,
    TArray<FString>& OutDiagnostics)
{
    OutArguments.Reset();
    const int32 Count = Signature.ParameterNames.Num();
    if (Signature.FunctionName.IsNone() ||
        Signature.ParameterTypes.Num() != Count ||
        Signature.OutputParameters.Num() != Count ||
        Arguments.Num() != Count)
    {
        OutDiagnostics.Add(TEXT("Function argument count or signature is inconsistent"));
        return false;
    }

    TArray<FString> Parts;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FBPSemanticType& Expected = Signature.ParameterTypes[Index];
        const FBPSemanticExpression& Argument = Arguments[Index];
        // boolの既定値はLoweringで正規化したtrue/falseだけを受理する。
        // 任意のLiteralValueをC++コードとして流用しない。
        if (Signature.OutputParameters[Index] == false &&
            Expected.Kind == EBPSemanticTypeKind::Bool &&
            Argument.Type.Kind == EBPSemanticTypeKind::Bool &&
            Argument.Type.bIsReference == false &&
            Argument.Kind == EBPSemanticExpressionKind::Literal &&
            (Argument.LiteralValue == TEXT("true") || Argument.LiteralValue == TEXT("false")))
        {
            Parts.Add(Argument.LiteralValue);
            continue;
        }
        if (Signature.OutputParameters[Index] ||
            Expected.Kind != EBPSemanticTypeKind::Struct ||
            Expected.TypePath != TEXT("/Script/CoreUObject.Vector") ||
            Expected.bIsReference ||
            Argument.Type.Kind != Expected.Kind ||
            Argument.Type.TypePath != Expected.TypePath ||
            Argument.Type.bIsReference)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Unsupported argument type: %s"),
                *Signature.ParameterNames[Index].ToString()));
            return false;
        }

        FString Part;
        // 式の種類を制限し、入力をC++文字列として直接連結しない。
        if (FBPActorLocationExpressionEmitter::Emit(Argument, Part, OutDiagnostics) == false)
        {
            return false;
        }
        Parts.Add(MoveTemp(Part));
    }

    OutArguments = FString::Join(Parts, TEXT(", "));
    return true;
}
