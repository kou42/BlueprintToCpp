#include "BPSemanticFunctionExtractor.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_FunctionEntry.h"
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
    // コンテナは子Propertyを再帰的に解決する。未対応の子型を成功扱いしない。
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Array;
        Type.ElementType = MakeShared<FBPSemanticType>(ResolvePropertyType(Array->Inner));
    }
    else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Set;
        Type.ElementType = MakeShared<FBPSemanticType>(ResolvePropertyType(Set->ElementProp));
    }
    else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Map;
        Type.KeyType = MakeShared<FBPSemanticType>(ResolvePropertyType(Map->KeyProp));
        Type.ValueType = MakeShared<FBPSemanticType>(ResolvePropertyType(Map->ValueProp));
    }
    else if (CastField<FBoolProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Bool; }
    else if (CastField<FIntProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Int32; }
    else if (CastField<FInt64Property>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Int64; }
    else if (CastField<FFloatProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Float; }
    else if (CastField<FDoubleProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Double; }
    else if (CastField<FStrProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::String; }
    else if (CastField<FNameProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Name; }
    else if (CastField<FTextProperty>(Property) != nullptr) { Type.Kind = EBPSemanticTypeKind::Text; }
    else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        // Enumの整数基底型だけでなく、UEnumの正式パスを保持する。
        Type.Kind = EBPSemanticTypeKind::Enum;
        Type.TypePath = GetPathNameSafe(Enum->GetEnum());
    }
    else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
    {
        // 旧形式のTEnumAsByteと通常のuint8を混同しない。
        if (Byte->Enum != nullptr)
        {
            Type.Kind = EBPSemanticTypeKind::Enum;
            Type.TypePath = GetPathNameSafe(Byte->Enum);
        }
    }
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
    // Delegate等の未対応型は推測せず未解決とする。
    return Type;
}

// 名前だけの一致をInterface実装と誤認しないため、Reflectionの宣言順・型・方向を照合する。
// SameTypeはコンテナの子型やEnum/Struct/Objectの具体型も比較する。
bool HasMatchingSignature(const UFunction* Implementation, const UFunction* Declaration)
{
    if (Implementation == nullptr || Declaration == nullptr)
    {
        return false;
    }
    if (Implementation->HasAnyFunctionFlags(FUNC_Static) != Declaration->HasAnyFunctionFlags(FUNC_Static)
        || Implementation->HasAnyFunctionFlags(FUNC_Const) != Declaration->HasAnyFunctionFlags(FUNC_Const))
    {
        return false;
    }
    TArray<const FProperty*> ImplementationParams;
    TArray<const FProperty*> DeclarationParams;
    for (TFieldIterator<FProperty> It(Implementation); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Parm))
        {
            ImplementationParams.Add(*It);
        }
    }
    for (TFieldIterator<FProperty> It(Declaration); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Parm))
        {
            DeclarationParams.Add(*It);
        }
    }
    if (ImplementationParams.Num() != DeclarationParams.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < ImplementationParams.Num(); ++Index)
    {
        const FProperty* Actual = ImplementationParams[Index];
        const FProperty* Expected = DeclarationParams[Index];
        if (Actual->GetFName() != Expected->GetFName()
            || Actual->SameType(Expected) == false
            || Actual->HasAnyPropertyFlags(CPF_ReturnParm) != Expected->HasAnyPropertyFlags(CPF_ReturnParm)
            || Actual->HasAnyPropertyFlags(CPF_OutParm) != Expected->HasAnyPropertyFlags(CPF_OutParm)
            || Actual->HasAnyPropertyFlags(CPF_ReferenceParm) != Expected->HasAnyPropertyFlags(CPF_ReferenceParm)
            || Actual->HasAnyPropertyFlags(CPF_ConstParm) != Expected->HasAnyPropertyFlags(CPF_ConstParm))
        {
            return false;
        }
    }
    return true;
}

// 子型が未解決のコンテナも、診断を残して意味的な完全性を主張しない。
bool HasUnresolvedType(const FBPSemanticType& Type)
{
    if (Type.Kind == EBPSemanticTypeKind::Unresolved)
    {
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Array || Type.Kind == EBPSemanticTypeKind::Set)
    {
        return Type.ElementType.IsValid() == false || HasUnresolvedType(*Type.ElementType);
    }
    if (Type.Kind == EBPSemanticTypeKind::Map)
    {
        return Type.KeyType.IsValid() == false || Type.ValueType.IsValid() == false
            || HasUnresolvedType(*Type.KeyType) || HasUnresolvedType(*Type.ValueType);
    }
    return false;
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
        // 親クラスに同名の宣言がある場合のみOverrideとする。Interface実装とは別概念。
        const UClass* SuperClass = GeneratedClass->GetSuperClass();
        const UFunction* ParentFunction = SuperClass != nullptr
            ? SuperClass->FindFunctionByName(Function->GetFName()) : nullptr;
        Target->bIsOverride = ParentFunction != nullptr;
        Target->OverriddenFunctionPath = GetPathNameSafe(ParentFunction);
        // Blueprintが直接実装を宣言したInterfaceだけを照合する。
        // 親クラスから継承したInterfaceや同名の無関係な関数は推測で扱わない。
        Target->bIsInterfaceImplementation = false;
        Target->InterfaceClassPath.Reset();
        Target->InterfaceFunctionPath.Reset();
        bool bAmbiguousInterface = false;
        for (const FBPInterfaceDescription& Description : Blueprint->ImplementedInterfaces)
        {
            const UClass* InterfaceClass = Description.Interface;
            if (InterfaceClass == nullptr)
            {
                continue;
            }
            const UFunction* InterfaceFunction = InterfaceClass->FindFunctionByName(Function->GetFName());
            if (InterfaceFunction == nullptr)
            {
                continue;
            }
            if (HasMatchingSignature(Function, InterfaceFunction) == false)
            {
                // 名前だけ一致した候補は実装確定せず、後続の一致候補を探索する。
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.Severity = EBPSemanticDiagnosticSeverity::Warning;
                Diagnostic.Message = FString::Printf(
                    TEXT("Function %s has a mismatched interface signature in %s"),
                    *Function->GetName(), *InterfaceClass->GetName());
                IR.Diagnostics.Add(MoveTemp(Diagnostic));
                continue;
            }
            // 複数Interfaceに同名関数がある場合、単一の所有者を確定しない。
            if (Target->bIsInterfaceImplementation)
            {
                bAmbiguousInterface = true;
                Target->InterfaceClassPath.Reset();
                Target->InterfaceFunctionPath.Reset();
                Target->bIsInterfaceImplementation = false;
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.Message = FString::Printf(
                    TEXT("Function %s matches multiple implemented interfaces"), *Function->GetName());
                IR.Diagnostics.Add(MoveTemp(Diagnostic));
                break;
            }
            if (bAmbiguousInterface)
            {
                break;
            }
            Target->bIsInterfaceImplementation = true;
            Target->InterfaceClassPath = InterfaceClass->GetPathName();
            Target->InterfaceFunctionPath = InterfaceFunction->GetPathName();
        }
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
                    Parameter.DefaultValueSource = TEXT("cpp_default_metadata");
                    Parameter.DefaultValueStatus = TEXT("specified");
                }
            }
            if (Parameter.bIsReturn)
            {
                Target->ReturnType = Parameter.Type;
            }
            else if (Parameter.bHasDefaultValue == false)
            {
                // FunctionGraphのEntry出力Pinから取得する。空文字は未指定か空値か
                // 区別できないため、推測でspecifiedにしない。
                for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
                {
                    if (Graph == nullptr || Graph->GetFName() != Function->GetFName())
                    {
                        continue;
                    }
                    for (const UEdGraphNode* Node : Graph->Nodes)
                    {
                        const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node);
                        if (Entry == nullptr)
                        {
                            continue;
                        }
                        for (const UEdGraphPin* Pin : Entry->Pins)
                        {
                            if (Pin == nullptr || Pin->Direction != EGPD_Output ||
                                Pin->PinName != Parameter.Name)
                            {
                                continue;
                            }
                            // 接続中のPinの値は実行時入力により決まるため、既定値と断定しない。
                            if (Pin->LinkedTo.IsEmpty() == false)
                            {
                                continue;
                            }
                            if (Pin->DefaultValue.IsEmpty() == false)
                            {
                                Parameter.bHasDefaultValue = true;
                                Parameter.DefaultValue = Pin->DefaultValue;
                                Parameter.DefaultValueSource = TEXT("function_entry_pin");
                                Parameter.DefaultValueStatus = TEXT("specified");
                                Parameter.DefaultValueSourceNodeId = Entry->NodeGuid;
                                Parameter.DefaultValueSourcePinId = Pin->PinId;
                            }
                        }
                    }
                }
            }
            if (HasUnresolvedType(Parameter.Type))
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
