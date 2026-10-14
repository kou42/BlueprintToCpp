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
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        Type.ContainerKind = TEXT("array");
        Type.InnerType = Array->Inner != nullptr ? Array->Inner->GetCPPType() : TEXT("");
    }
    else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        Type.ContainerKind = TEXT("set");
        Type.InnerType = Set->ElementProp != nullptr ? Set->ElementProp->GetCPPType() : TEXT("");
    }
    else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        Type.ContainerKind = TEXT("map");
        Type.KeyType = Map->KeyProp != nullptr ? Map->KeyProp->GetCPPType() : TEXT("");
        Type.ValueType = Map->ValueProp != nullptr ? Map->ValueProp->GetCPPType() : TEXT("");
    }
    else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Enum;
        Type.TypePath = GetPathNameSafe(Enum->GetEnum());
    }
    else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
    {
        Type.Kind = Byte->Enum != nullptr ? EBPSemanticTypeKind::Enum : EBPSemanticTypeKind::Byte;
        Type.TypePath = GetPathNameSafe(Byte->Enum);
    }
    else if (CastField<FBoolProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Bool; }
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
    // Delegate等は型情報を推測せず未解決とする。コンテナは構造を保持し、
    // 要素型の安全な再帰解決は後続のCJ1で行う。
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
        int32 DeclarationIndex = 0;
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
            if (Parameter.bIsReturn == false)
            {
                Parameter.DeclarationIndex = DeclarationIndex++;
            }
            // CPP_Default_<引数名> は存在する場合のみ取得する。存在しない値を空文字と混同しない。
            if (Parameter.bIsReturn == false)
            {
                const FString DefaultKey = FString::Printf(TEXT("CPP_Default_%s"), *Property->GetName());
                Parameter.bHasDefaultValue = Function->HasMetaData(*DefaultKey);
                // CPP_Defaultメタデータがないだけでは、Blueprint側の既定値が
                // 存在しないと断定できないため、未取得として記録する。
                Parameter.DefaultValueStatus = Parameter.bHasDefaultValue
                    ? TEXT("specified") : TEXT("unavailable");
                if (Parameter.bHasDefaultValue)
                {
                    Parameter.DefaultValue = Function->GetMetaData(*DefaultKey);
                }
            }
            if (Parameter.bIsReturn)
            {
                Parameter.DefaultValueStatus = TEXT("absent");
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
