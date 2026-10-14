#include "BPSemanticFunctionExtractor.h"

#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"

namespace
{
FBPSemanticType ResolvePropertyType(const FProperty* Property)
{
    FBPSemanticType Type;
    if (Property == nullptr)
    {
        return Type;
    }
    Type.bIsReference = Property->HasAnyPropertyFlags(CPF_ReferenceParm);
    Type.bIsConst = Property->HasAnyPropertyFlags(CPF_ConstParm);
    if (CastField<FBoolProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Bool; }
    else if (CastField<FIntProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Int32; }
    else if (CastField<FInt64Property>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Int64; }
    else if (CastField<FFloatProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Float; }
    else if (CastField<FDoubleProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Double; }
    else if (CastField<FStrProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::String; }
    else if (CastField<FNameProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Name; }
    else if (CastField<FTextProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Text; }
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Struct;
        Type.TypePath = GetPathNameSafe(Struct->Struct);
    }
    else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Object;
        Type.TypePath = GetPathNameSafe(Object->PropertyClass);
    }
    // コンテナ・Enum・Delegate等は型情報を推測せず未解決とする。
    return Type;
}
}

void FBPSemanticFunctionExtractor::Append(const UBlueprint* Blueprint, FBPSemanticIR& IR)
{
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
    {
        return;
    }
    UClass* GeneratedClass = Blueprint->GeneratedClass;
    for (TFieldIterator<UFunction> Iterator(GeneratedClass, EFieldIterationFlags::None); Iterator; ++Iterator)
    {
        const UFunction* Function = *Iterator;
        if (Function == nullptr)
        {
            continue;
        }
        // Blueprintの生成クラスに直接属する関数のみ扱う。親クラス由来の関数は除外。
        if (Function->GetOuterUClass() != GeneratedClass)
        {
            continue;
        }
        FBPSemanticFunction* Target = IR.Functions.FindByPredicate(
            [Function](const FBPSemanticFunction& Candidate)
            {
                return Candidate.Name == Function->GetFName();
            });
        if (Target == nullptr)
        {
            FBPSemanticFunction NewFunction;
            NewFunction.Name = Function->GetFName();
            IR.Functions.Add(MoveTemp(NewFunction));
            Target = &IR.Functions.Last();
        }
        Target->OwnerClassPath = GeneratedClass->GetPathName();
        Target->SignatureSource = TEXT("generated_class_reflection");
        Target->bIsNative = Function->HasAnyFunctionFlags(FUNC_Native);
        Target->bIsStatic = Function->HasAnyFunctionFlags(FUNC_Static);
        Target->bIsConst = Function->HasAnyFunctionFlags(FUNC_Const);
        Target->bIsBlueprintEvent = Function->HasAnyFunctionFlags(FUNC_BlueprintEvent);
        Target->Parameters.Reset();
        Target->ReturnType.Kind = EBPSemanticTypeKind::Void;
        for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
        {
            const FProperty* Property = *ParamIt;
            if (Property->HasAnyPropertyFlags(CPF_Parm) == false)
            {
                continue;
            }
            FBPSemanticParameter Parameter;
            Parameter.Name = Property->GetFName();
            Parameter.Type = ResolvePropertyType(Property);
            Parameter.bIsOut = Property->HasAnyPropertyFlags(CPF_OutParm);
            Parameter.bIsReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
            // CPP_Default_<引数名> は存在する場合のみ取得する。存在しない値を空文字と混同しない。
            if (Parameter.bIsReturn == false)
            {
                const FString DefaultKey = FString::Printf(TEXT("CPP_Default_%s"), *Property->GetName());
                Parameter.bHasDefaultValue = Function->HasMetaData(*DefaultKey);
                if (Parameter.bHasDefaultValue)
                {
                    Parameter.DefaultValue = Function->GetMetaData(*DefaultKey);
                }
            }
            if (Parameter.bIsReturn)
            {
                Target->ReturnType = Parameter.Type;
            }
            if (Parameter.Type.Kind == EBPSemanticTypeKind::Unresolved)
            {
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.Message = FString::Printf(TEXT("Function %s parameter %s has unresolved type"),
                    *Function->GetName(), *Property->GetName());
                IR.Diagnostics.Add(MoveTemp(Diagnostic));
            }
            Target->Parameters.Add(MoveTemp(Parameter));
        }
    }
}
