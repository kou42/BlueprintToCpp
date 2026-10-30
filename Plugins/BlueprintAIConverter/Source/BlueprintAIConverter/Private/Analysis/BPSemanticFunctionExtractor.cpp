#include "BPSemanticFunctionExtractor.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_FunctionEntry.h"
#include "UObject/UnrealType.h"

namespace
{
// コンテナはkind=unresolvedでもよいため、子要素まで検査する。
bool HasUnresolvedType(const FBPSemanticType& Type, int32 Depth = 0)
{
    if (Depth >= 16)
    {
        return true;
    }
    if (Type.ContainerKind == TEXT("array") || Type.ContainerKind == TEXT("set"))
    {
        return Type.ElementType.IsValid() == false || HasUnresolvedType(*Type.ElementType, Depth + 1);
    }
    if (Type.ContainerKind == TEXT("map"))
    {
        return Type.KeyTypeInfo.IsValid() == false || Type.ValueTypeInfo.IsValid() == false ||
            HasUnresolvedType(*Type.KeyTypeInfo, Depth + 1) ||
            HasUnresolvedType(*Type.ValueTypeInfo, Depth + 1);
    }
    // 型パスを持つ種別は、参照先が空なら解決済みとはみなさない。
    if (Type.Kind == EBPSemanticTypeKind::Object ||
        Type.Kind == EBPSemanticTypeKind::Struct ||
        Type.Kind == EBPSemanticTypeKind::Enum ||
        Type.Kind == EBPSemanticTypeKind::Delegate ||
        Type.Kind == EBPSemanticTypeKind::MulticastDelegate)
    {
        return Type.TypePath.IsEmpty();
    }
    return Type.Kind == EBPSemanticTypeKind::Unresolved;
}

FBPSemanticType ResolvePropertyType(const FProperty* Property, int32 Depth = 0)
{
    FBPSemanticType Type;
    if (Property == nullptr)
    {
        return Type;
    }
    // 不正な循環参照・過度なネストを防止する。
    if (Depth >= 16)
    {
        return Type;
    }
    Type.bIsReference = Property->HasAnyPropertyFlags(CPF_ReferenceParm);
    Type.bIsConst = Property->HasAnyPropertyFlags(CPF_ConstParm);
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        Type.ContainerKind = TEXT("array");
        if (Array->Inner != nullptr)
        {
            Type.InnerType = Array->Inner->GetCPPType();
            Type.ElementType = MakeShared<FBPSemanticType>(ResolvePropertyType(Array->Inner, Depth + 1));
        }
    }
    else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        Type.ContainerKind = TEXT("set");
        if (Set->ElementProp != nullptr)
        {
            Type.InnerType = Set->ElementProp->GetCPPType();
            Type.ElementType = MakeShared<FBPSemanticType>(ResolvePropertyType(Set->ElementProp, Depth + 1));
        }
    }
    else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        Type.ContainerKind = TEXT("map");
        if (Map->KeyProp != nullptr)
        {
            Type.KeyType = Map->KeyProp->GetCPPType();
            Type.KeyTypeInfo = MakeShared<FBPSemanticType>(ResolvePropertyType(Map->KeyProp, Depth + 1));
        }
        if (Map->ValueProp != nullptr)
        {
            Type.ValueType = Map->ValueProp->GetCPPType();
            Type.ValueTypeInfo = MakeShared<FBPSemanticType>(ResolvePropertyType(Map->ValueProp, Depth + 1));
        }
    }
    else if (const FMulticastDelegateProperty* Multicast = CastField<FMulticastDelegateProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::MulticastDelegate;
        Type.TypePath = GetPathNameSafe(Multicast->SignatureFunction);
    }
    else if (const FDelegateProperty* Delegate = CastField<FDelegateProperty>(Property))
    {
        Type.Kind = EBPSemanticTypeKind::Delegate;
        Type.TypePath = GetPathNameSafe(Delegate->SignatureFunction);
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
    // 未知のPropertyは未解決として保持し、型名から推測しない。
    return Type;
}
}

bool FBPSemanticFunctionExtractor::HasMatchingInterfaceSignature(
    const UFunction* Implementation, const UFunction* Declaration)
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

EBPSemanticFunctionVisibility FBPSemanticFunctionExtractor::ResolveVisibility(const UFunction* Function)
{
    if (Function == nullptr)
    {
        return EBPSemanticFunctionVisibility::Unspecified;
    }
    const bool bPublic = Function->HasAnyFunctionFlags(FUNC_Public);
    const bool bProtected = Function->HasAnyFunctionFlags(FUNC_Protected);
    const bool bPrivate = Function->HasAnyFunctionFlags(FUNC_Private);
    if (int32(bPublic) + int32(bProtected) + int32(bPrivate) != 1)
    {
        return EBPSemanticFunctionVisibility::Unspecified;
    }
    return bPublic ? EBPSemanticFunctionVisibility::Public
        : bProtected ? EBPSemanticFunctionVisibility::Protected
        : EBPSemanticFunctionVisibility::Private;
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
        Target->Visibility = ResolveVisibility(Function);
        if (Target->Visibility == EBPSemanticFunctionVisibility::Unspecified)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = Function->GetName();
            Diagnostic.Message = FString::Printf(
                TEXT("Function %s has missing or conflicting Reflection access flags"),
                *Function->GetName());
            IR.Diagnostics.Add(MoveTemp(Diagnostic));
        }
        Target->bIsNative = Function->HasAnyFunctionFlags(FUNC_Native);
        Target->bIsStatic = Function->HasAnyFunctionFlags(FUNC_Static);
        Target->bIsConst = Function->HasAnyFunctionFlags(FUNC_Const);
        Target->bIsBlueprintEvent = Function->HasAnyFunctionFlags(FUNC_BlueprintEvent);
        // 親クラスに同名の宣言がある場合のみOverrideとする。Interface実装とは別概念。
        const UClass* SuperClass = GeneratedClass->GetSuperClass();
        const UFunction* ParentFunction = SuperClass != nullptr
            ? SuperClass->FindFunctionByName(Function->GetFName()) : nullptr;
        Target->bIsOverride = ParentFunction != nullptr;
        Target->OverriddenFunctionPath = ParentFunction != nullptr ? ParentFunction->GetPathName() : FString();
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
            if (HasMatchingInterfaceSignature(Function, InterfaceFunction) == false)
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
        // 再抽出時に古いコンテナ要素型・型パスを残さない。
        Target->ReturnType = FBPSemanticType();
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
                    Parameter.DefaultValueSource = TEXT("cpp_default_metadata");
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
                Diagnostic.GraphName = Function->GetName();
                Diagnostic.Message = FString::Printf(TEXT("Function %s parameter %s has unresolved type"),
                    *Function->GetName(), *Property->GetName());
                IR.Diagnostics.Add(MoveTemp(Diagnostic));
            }
            Target->Parameters.Add(MoveTemp(Parameter));
        }
    }
}
