#include "BPFunctionSignatureResolver.h"

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
bool ResolvePropertyType(const FProperty* Property, FBPSemanticType& OutType, int32 Depth = 0)
{
    if (Property == nullptr || Depth > 16)
    {
        return false;
    }

    OutType.bIsConst = Property->HasAnyPropertyFlags(CPF_ConstParm);
    OutType.bIsReference = Property->HasAnyPropertyFlags(CPF_ReferenceParm);

    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        if (Array->Inner == nullptr) { return false; }
        OutType.ContainerKind = TEXT("array");
        OutType.InnerType = Array->Inner->GetCPPType();
        OutType.ElementType = MakeShared<FBPSemanticType>();
        return ResolvePropertyType(Array->Inner, *OutType.ElementType, Depth + 1);
    }
    if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
    {
        if (Byte->Enum != nullptr) { return false; }
        OutType.Kind = EBPSemanticTypeKind::Byte;
    }
    else if (CastField<FBoolProperty>(Property) != nullptr) { OutType.Kind = EBPSemanticTypeKind::Bool; }
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
    // WorldContextは先頭のUObject*入力引数としてReflectionで確認できる場合のみ許可する。
    // DefaultToSelfも同じ引数を指す場合に限定し、暗黙の引数補完は行わない。
    const bool bHasWorldContext = Function->HasMetaData(TEXT("WorldContext"));
    const bool bHasDefaultToSelf = Function->HasMetaData(TEXT("DefaultToSelf"));
    if (bHasWorldContext || bHasDefaultToSelf)
    {
        const FString ContextName = Function->GetMetaData(TEXT("WorldContext"));
        const FString DefaultName = Function->GetMetaData(TEXT("DefaultToSelf"));
        const FProperty* FirstParameter = nullptr;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            if ((*It)->HasAnyPropertyFlags(CPF_Parm) &&
                (*It)->HasAnyPropertyFlags(CPF_ReturnParm) == false)
            {
                FirstParameter = *It;
                break;
            }
        }
        const FObjectProperty* ContextProperty = CastField<FObjectProperty>(FirstParameter);
        if (bHasWorldContext == false || ContextName.IsEmpty() ||
            (bHasDefaultToSelf && DefaultName != ContextName) ||
            Function->HasAnyFunctionFlags(FUNC_Native) == false ||
            Function->HasAnyFunctionFlags(FUNC_Static) == false ||
            FirstParameter == nullptr || FirstParameter->GetName() != ContextName ||
            ContextProperty == nullptr || ContextProperty->PropertyClass == nullptr ||
            FirstParameter->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm))
        {
            Diagnostics.Add(TEXT("Unsupported WorldContext/DefaultToSelf signature"));
            return false;
        }
    }
    static const TCHAR* UnsupportedKeys[] = {
        TEXT("AutoCreateRefTerm"),
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
        const FName DefaultMetadataKey(*FString::Printf(
            TEXT("CPP_Default_%s"), *Property->GetName()));
        const bool bHasDefault = Function->HasMetaData(DefaultMetadataKey);
        Candidate.bHasParameterDefaultValues.Add(bHasDefault);
        Candidate.ParameterDefaultValues.Add(
            bHasDefault ? Function->GetMetaData(DefaultMetadataKey) : FString());
    }

    if (bValid == false)
    {
        return false;
    }
    OutSignature = MoveTemp(Candidate);
    return true;
}
