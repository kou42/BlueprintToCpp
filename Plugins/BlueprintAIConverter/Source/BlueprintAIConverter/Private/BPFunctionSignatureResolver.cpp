#include "BPFunctionSignatureResolver.h"

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
bool ResolvePropertyType(const FProperty* Property, FBPSemanticType& OutType)
{
    if (Property == nullptr)
    {
        return false;
    }

    OutType.bIsConst = Property->HasAnyPropertyFlags(CPF_ConstParm);
    OutType.bIsReference = Property->HasAnyPropertyFlags(CPF_ReferenceParm);

    if (CastField<FBoolProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Bool; }
    else if (CastField<FIntProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Int32; }
    else if (CastField<FInt64Property>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Int64; }
    else if (CastField<FFloatProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Float; }
    else if (CastField<FDoubleProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Double; }
    else if (CastField<FStrProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::String; }
    else if (CastField<FNameProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Name; }
    else if (CastField<FTextProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Text; }
    else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
    {
        // Class参照やソフト参照は通常のUObject*と同一視できないため拒否する。
        if (CastField<FObjectProperty>(Property) == nullptr || Object->PropertyClass == nullptr)
        {
            return false;
        }
        OutType.Kind = EBPSemanticTypeKind::Object;
        OutType.TypePath = Object->PropertyClass->GetPathName();
    }
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        if (Struct->Struct == nullptr)
        {
            return false;
        }
        OutType.Kind = EBPSemanticTypeKind::Struct;
        OutType.TypePath = Struct->Struct->GetPathName();
    }
    else
    {
        return false;
    }
    return true;
}

bool ValidateMetadata(const UFunction* Function, TArray<FString>& Diagnostics)
{
    // Blueprintの隠し引数・展開・特殊実行規約を通常のC++呼び出しと誤認しない。
    static const TCHAR* UnsupportedKeys[] = {
        TEXT("WorldContext"), TEXT("DefaultToSelf"), TEXT("AutoCreateRefTerm"),
        TEXT("ExpandEnumAsExecs"), TEXT("Latent"), TEXT("LatentInfo"),
        TEXT("BlueprintInternalUseOnly"), TEXT("CustomStructureParam"),
        TEXT("ArrayParm"), TEXT("ArrayTypeDependentParams"),
        TEXT("DeterminesOutputType"), TEXT("DynamicOutputParam"),
        TEXT("CommutativeAssociativeBinaryOperator")
    };
    bool bValid = true;
    for (const TCHAR* Key : UnsupportedKeys)
    {
        if (Function->HasMetaData(FName(Key)))
        {
            Diagnostics.Add(FString::Printf(TEXT("Unsupported function metadata: %s"), Key));
            bValid = false;
        }
    }
    if (Function->HasMetaData(TEXT("CustomThunk")) ||
        Function->HasAnyFunctionFlags(FUNC_Net | FUNC_Delegate | FUNC_MulticastDelegate))
    {
        Diagnostics.Add(TEXT("Unsupported custom thunk, network, or delegate function flags"));
        bValid = false;
    }
    return bValid;
}
}

bool FBPFunctionSignatureResolver::Resolve(const UFunction* Function,
    FBPResolvedFunctionSignature& OutSignature, TArray<FString>& OutDiagnostics)
{
    OutSignature = FBPResolvedFunctionSignature();
    if (Function == nullptr)
    {
        OutDiagnostics.Add(TEXT("UFunction is null"));
        return false;
    }

    FBPResolvedFunctionSignature Candidate;
    Candidate.FunctionName = Function->GetFName();
    const UClass* Owner = Function->GetOwnerClass();
    if (Owner == nullptr)
    {
        OutDiagnostics.Add(TEXT("UFunction has no owner class"));
        return false;
    }
    Candidate.OwnerClassPath = Owner->GetPathName();
    Candidate.bStatic = Function->HasAnyFunctionFlags(FUNC_Static);
    Candidate.bConst = Function->HasAnyFunctionFlags(FUNC_Const);
    Candidate.ReturnType.Kind = EBPSemanticTypeKind::Void;

    bool bValid = ValidateMetadata(Function, OutDiagnostics);
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Parm) == false)
        {
            continue;
        }
        FBPSemanticType Type;
        if (ResolvePropertyType(Property, Type) == false)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Unsupported parameter type: %s (%s)"),
                *Property->GetName(), *Property->GetClass()->GetName()));
            bValid = false;
            continue;
        }
        if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            Candidate.ReturnType = Type;
            continue;
        }
        // OutParmだけでは非const参照とは限らない。両フラグを独立して保持する。
        Candidate.ParameterNames.Add(Property->GetFName());
        Candidate.ParameterTypes.Add(Type);
        // const参照の入力引数はOutParmが立つ場合もあるため、出力とは区別する。
        Candidate.OutputParameters.Add(Property->HasAnyPropertyFlags(CPF_OutParm) &&
            Property->HasAnyPropertyFlags(CPF_ConstParm) == false);
    }

    if (bValid == false)
    {
        return false;
    }
    OutSignature = MoveTemp(Candidate);
    return true;
}
