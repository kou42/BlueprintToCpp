#include "BPNumericComparisonResolver.h"

#include "BPFunctionSignatureResolver.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetMathLibrary.h"

namespace
{
struct FComparisonOperatorDefinition
{
    const TCHAR* FunctionStem;
    const TCHAR* OperatorToken;
};

struct FComparisonTypeDefinition
{
    const TCHAR* FunctionSuffix;
    EBPSemanticTypeKind Type;
};

const FComparisonOperatorDefinition Operators[] = {
    { TEXT("Less"), TEXT("<") },
    { TEXT("LessEqual"), TEXT("<=") },
    { TEXT("Greater"), TEXT(">") },
    { TEXT("GreaterEqual"), TEXT(">=") },
    { TEXT("EqualEqual"), TEXT("==") },
    { TEXT("NotEqual"), TEXT("!=") }
};

const FComparisonTypeDefinition Types[] = {
    { TEXT("IntInt"), EBPSemanticTypeKind::Int32 },
    { TEXT("FloatFloat"), EBPSemanticTypeKind::Float },
    { TEXT("DoubleDouble"), EBPSemanticTypeKind::Double }
};

bool FindAllowedComparison(const FName FunctionName,
    FString& OutOperatorToken, EBPSemanticTypeKind& OutOperandType)
{
    const FString Name = FunctionName.ToString();
    for (const FComparisonOperatorDefinition& Operator : Operators)
    {
        for (const FComparisonTypeDefinition& Type : Types)
        {
            if (Name == FString::Printf(TEXT("%s_%s"),
                Operator.FunctionStem, Type.FunctionSuffix))
            {
                OutOperatorToken = Operator.OperatorToken;
                OutOperandType = Type.Type;
                return true;
            }
        }
    }
    return false;
}
}

bool FBPNumericComparisonResolver::Resolve(const FString& FunctionOwnerPath,
    const FName FunctionName, FBPNumericComparison& OutComparison,
    TArray<FString>& OutDiagnostics)
{
    OutComparison = FBPNumericComparison();
    const FString ExpectedOwnerPath = UKismetMathLibrary::StaticClass()->GetPathName();
    if (FunctionOwnerPath != ExpectedOwnerPath)
    {
        OutDiagnostics.Add(TEXT("Numeric comparison owner is not KismetMathLibrary"));
        return false;
    }

    FString OperatorToken;
    EBPSemanticTypeKind OperandType = EBPSemanticTypeKind::Unresolved;
    if (FindAllowedComparison(FunctionName, OperatorToken, OperandType) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Unsupported numeric comparison function: %s"),
            *FunctionName.ToString()));
        return false;
    }

    const UFunction* Function = UKismetMathLibrary::StaticClass()->FindFunctionByName(FunctionName);
    if (Function == nullptr)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Reflected numeric comparison function was not found: %s"),
            *FunctionName.ToString()));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> SignatureDiagnostics;
    if (FBPFunctionSignatureResolver::Resolve(
        Function, Signature, SignatureDiagnostics) == false)
    {
        for (const FString& Message : SignatureDiagnostics)
        {
            OutDiagnostics.Add(FString::Printf(
                TEXT("Numeric comparison signature is unsupported: %s"), *Message));
        }
        return false;
    }

    const bool bSignatureMatches =
        Signature.FunctionName == FunctionName &&
        Signature.OwnerClassPath == ExpectedOwnerPath &&
        Signature.bStatic &&
        Function->HasAnyFunctionFlags(FUNC_BlueprintPure) &&
        Signature.ReturnType.Kind == EBPSemanticTypeKind::Bool &&
        Signature.ReturnType.bIsReference == false &&
        Signature.ReturnType.bIsConst == false &&
        Signature.ParameterNames.Num() == 2 &&
        Signature.ParameterTypes.Num() == 2 &&
        Signature.OutputParameters.Num() == 2 &&
        Signature.ParameterNames[0] == TEXT("A") &&
        Signature.ParameterNames[1] == TEXT("B") &&
        Signature.ParameterTypes[0].Kind == OperandType &&
        Signature.ParameterTypes[1].Kind == OperandType &&
        Signature.ParameterTypes[0].bIsReference == false &&
        Signature.ParameterTypes[1].bIsReference == false &&
        Signature.ParameterTypes[0].bIsConst == false &&
        Signature.ParameterTypes[1].bIsConst == false &&
        Signature.OutputParameters[0] == false &&
        Signature.OutputParameters[1] == false;
    if (bSignatureMatches == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Numeric comparison signature does not match the verified binary layout: %s"),
            *FunctionName.ToString()));
        return false;
    }

    OutComparison.FunctionName = FunctionName;
    OutComparison.FunctionOwnerPath = ExpectedOwnerPath;
    OutComparison.OperatorToken = MoveTemp(OperatorToken);
    OutComparison.OperandType = OperandType;
    return true;
}

bool FBPNumericComparisonResolver::MatchesPinType(const FName Category,
    const FName SubCategory, const EBPSemanticTypeKind ExpectedType)
{
    switch (ExpectedType)
    {
    case EBPSemanticTypeKind::Int32:
        return Category == UEdGraphSchema_K2::PC_Int && SubCategory.IsNone();
    case EBPSemanticTypeKind::Float:
        return (Category == UEdGraphSchema_K2::PC_Real &&
            SubCategory == UEdGraphSchema_K2::PC_Float) ||
            (Category == UEdGraphSchema_K2::PC_Float && SubCategory.IsNone());
    case EBPSemanticTypeKind::Double:
        return Category == UEdGraphSchema_K2::PC_Real &&
            SubCategory == UEdGraphSchema_K2::PC_Double;
    default:
        return false;
    }
}

bool FBPNumericComparisonResolver::CanonicalizeLiteral(
    const EBPSemanticTypeKind Type, const FString& Value,
    FString& OutCanonicalValue)
{
    OutCanonicalValue.Reset();
    if (Type == EBPSemanticTypeKind::Int32)
    {
        int32 Parsed = 0;
        if (LexTryParseString(Parsed, *Value) == false)
        {
            return false;
        }
        OutCanonicalValue = LexToString(Parsed);
        return true;
    }
    if (Type == EBPSemanticTypeKind::Float)
    {
        float Parsed = 0.0f;
        if (LexTryParseString(Parsed, *Value) == false || FMath::IsFinite(Parsed) == false)
        {
            return false;
        }
        OutCanonicalValue = LexToString(Parsed);
        return true;
    }
    if (Type == EBPSemanticTypeKind::Double)
    {
        double Parsed = 0.0;
        if (LexTryParseString(Parsed, *Value) == false || FMath::IsFinite(Parsed) == false)
        {
            return false;
        }
        OutCanonicalValue = LexToString(Parsed);
        return true;
    }
    return false;
}
