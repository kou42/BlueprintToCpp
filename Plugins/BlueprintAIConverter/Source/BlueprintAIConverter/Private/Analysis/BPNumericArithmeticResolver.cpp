#include "BPNumericArithmeticResolver.h"

#include "Kismet/KismetMathLibrary.h"
#include "UObject/UnrealType.h"

namespace
{
struct FArithmeticDefinition
{
    const TCHAR* FunctionName;
    EBPSemanticTypeKind Type;
};

const FArithmeticDefinition Definitions[] = {
    { TEXT("Add_IntInt"), EBPSemanticTypeKind::Int32 },
    { TEXT("Subtract_IntInt"), EBPSemanticTypeKind::Int32 },
    { TEXT("Multiply_IntInt"), EBPSemanticTypeKind::Int32 },
    { TEXT("Divide_IntInt"), EBPSemanticTypeKind::Int32 },
    { TEXT("Add_DoubleDouble"), EBPSemanticTypeKind::Double },
    { TEXT("Subtract_DoubleDouble"), EBPSemanticTypeKind::Double },
    { TEXT("Multiply_DoubleDouble"), EBPSemanticTypeKind::Double },
    { TEXT("Divide_DoubleDouble"), EBPSemanticTypeKind::Double }
};

bool FindDefinition(const FName FunctionName, EBPSemanticTypeKind& OutType)
{
    for (const FArithmeticDefinition& Definition : Definitions)
    {
        if (FunctionName == Definition.FunctionName)
        {
            OutType = Definition.Type;
            return true;
        }
    }
    return false;
}

bool MatchesPropertyType(const FProperty* Property,
    const EBPSemanticTypeKind ExpectedType)
{
    if (Property == nullptr ||
        Property->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_ConstParm))
    {
        return false;
    }
    if (ExpectedType == EBPSemanticTypeKind::Int32)
    {
        return CastField<FIntProperty>(Property) != nullptr;
    }
    if (ExpectedType == EBPSemanticTypeKind::Double)
    {
        return CastField<FDoubleProperty>(Property) != nullptr;
    }
    return false;
}
}

bool FBPNumericArithmeticResolver::Resolve(const FString& FunctionOwnerPath,
    const FName FunctionName, FBPNumericArithmeticOperation& OutOperation,
    TArray<FString>& OutDiagnostics)
{
    OutOperation = FBPNumericArithmeticOperation();
    const FString ExpectedOwnerPath = UKismetMathLibrary::StaticClass()->GetPathName();
    if (FunctionOwnerPath != ExpectedOwnerPath)
    {
        OutDiagnostics.Add(TEXT("Numeric arithmetic owner is not KismetMathLibrary"));
        return false;
    }

    EBPSemanticTypeKind OperandType = EBPSemanticTypeKind::Unresolved;
    if (FindDefinition(FunctionName, OperandType) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Unsupported numeric arithmetic function: %s"),
            *FunctionName.ToString()));
        return false;
    }

    const UFunction* Function = UKismetMathLibrary::StaticClass()
        ->FindFunctionByName(FunctionName);
    if (Function == nullptr ||
        Function->HasAnyFunctionFlags(FUNC_Static) == false ||
        Function->HasAnyFunctionFlags(FUNC_BlueprintPure) == false ||
        Function->HasAnyFunctionFlags(
            FUNC_Net | FUNC_Delegate | FUNC_MulticastDelegate))
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Reflected numeric arithmetic function flags are invalid: %s"),
            *FunctionName.ToString()));
        return false;
    }

    static const TCHAR* UnsupportedMetadata[] = {
        TEXT("WorldContext"), TEXT("DefaultToSelf"), TEXT("AutoCreateRefTerm"),
        TEXT("ExpandEnumAsExecs"), TEXT("Latent"), TEXT("LatentInfo"),
        TEXT("BlueprintInternalUseOnly"), TEXT("CustomStructureParam"),
        TEXT("ArrayParm"), TEXT("ArrayTypeDependentParams"),
        TEXT("DeterminesOutputType"), TEXT("DynamicOutputParam"),
        TEXT("CustomThunk")
    };
    for (const TCHAR* Key : UnsupportedMetadata)
    {
        if (Function->HasMetaData(FName(Key)))
        {
            OutDiagnostics.Add(FString::Printf(
                TEXT("Unsupported numeric arithmetic metadata: %s"), Key));
            return false;
        }
    }

    const FProperty* A = nullptr;
    const FProperty* B = nullptr;
    const FProperty* ReturnValue = nullptr;
    int32 ParameterCount = 0;
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Parm) == false)
        {
            continue;
        }
        ++ParameterCount;
        if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            ReturnValue = Property;
        }
        else if (Property->GetFName() == TEXT("A"))
        {
            A = Property;
        }
        else if (Property->GetFName() == TEXT("B"))
        {
            B = Property;
        }
    }
    if (ParameterCount != 3 ||
        MatchesPropertyType(A, OperandType) == false ||
        MatchesPropertyType(B, OperandType) == false ||
        MatchesPropertyType(ReturnValue, OperandType) == false ||
        A->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm) ||
        B->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm) ||
        ReturnValue->HasAnyPropertyFlags(CPF_ReturnParm) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Numeric arithmetic signature does not match the verified binary layout: %s"),
            *FunctionName.ToString()));
        return false;
    }

    OutOperation.FunctionName = FunctionName;
    OutOperation.FunctionOwnerPath = ExpectedOwnerPath;
    OutOperation.OperandType = OperandType;
    return true;
}
