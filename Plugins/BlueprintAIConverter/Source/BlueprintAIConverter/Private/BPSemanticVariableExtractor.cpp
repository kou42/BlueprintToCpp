#include "BPSemanticVariableExtractor.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"

namespace
{
EBPSemanticTypeKind ResolveType(const FEdGraphPinType& PinType)
{
    const FString Name = PinType.PinCategory.ToString();
    if (Name == TEXT("bool")) { return EBPSemanticTypeKind::Bool; }
    if (Name == TEXT("int")) { return EBPSemanticTypeKind::Int32; }
    if (Name == TEXT("int64")) { return EBPSemanticTypeKind::Int64; }
    // UE5のrealピンはサブカテゴリでfloat/doubleを区別する。
    if (Name == TEXT("real"))
    {
        if (PinType.PinSubCategory == TEXT("float")) { return EBPSemanticTypeKind::Float; }
        if (PinType.PinSubCategory == TEXT("double")) { return EBPSemanticTypeKind::Double; }
        return EBPSemanticTypeKind::Unresolved;
    }
    if (Name == TEXT("float")) { return EBPSemanticTypeKind::Float; }
    if (Name == TEXT("double")) { return EBPSemanticTypeKind::Double; }
    if (Name == TEXT("string")) { return EBPSemanticTypeKind::String; }
    if (Name == TEXT("name")) { return EBPSemanticTypeKind::Name; }
    if (Name == TEXT("text")) { return EBPSemanticTypeKind::Text; }
    if (Name == TEXT("object") || Name == TEXT("class")) { return EBPSemanticTypeKind::Object; }
    if (Name == TEXT("struct")) { return EBPSemanticTypeKind::Struct; }
    return EBPSemanticTypeKind::Unresolved;
}
    
// リフレクション型とIR型が一致するスカラーだけをCDOから読み取る。
// ExportTextItemなどの汎用文字列化は型の意味を失うためここでは使用しない。
bool ReadScalarDefault(const FProperty* Property, const UObject* CDO,
    EBPSemanticTypeKind Kind, FString& OutValue)
{
    if (Property == nullptr || CDO == nullptr)
    {
        return false;
    }
    const void* Address = Property->ContainerPtrToValuePtr<void>(CDO);
    if (Address == nullptr)
    {
        return false;
    }
    switch (Kind)
    {
    case EBPSemanticTypeKind::Bool:
        if (const FBoolProperty* Typed = CastField<FBoolProperty>(Property))
        {
            OutValue = Typed->GetPropertyValue(Address) ? TEXT("true") : TEXT("false");
            return true;
        }
        break;
    case EBPSemanticTypeKind::Int32:
        if (const FIntProperty* Typed = CastField<FIntProperty>(Property))
        {
            OutValue = LexToString(Typed->GetPropertyValue(Address));
            return true;
        }
        break;
    case EBPSemanticTypeKind::Int64:
        if (const FInt64Property* Typed = CastField<FInt64Property>(Property))
        {
            OutValue = LexToString(Typed->GetPropertyValue(Address));
            return true;
        }
        break;
    case EBPSemanticTypeKind::Float:
        if (const FFloatProperty* Typed = CastField<FFloatProperty>(Property))
        {
            OutValue = LexToString(Typed->GetPropertyValue(Address));
            return true;
        }
        break;
    case EBPSemanticTypeKind::Double:
        if (const FDoubleProperty* Typed = CastField<FDoubleProperty>(Property))
        {
            OutValue = LexToString(Typed->GetPropertyValue(Address));
            return true;
        }
        break;
    case EBPSemanticTypeKind::String:
        if (const FStrProperty* Typed = CastField<FStrProperty>(Property))
        {
            OutValue = Typed->GetPropertyValue(Address);
            return true;
        }
        break;
    case EBPSemanticTypeKind::Name:
        if (const FNameProperty* Typed = CastField<FNameProperty>(Property))
        {
            OutValue = Typed->GetPropertyValue(Address).ToString();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}
}

void FBPSemanticVariableExtractor::Append(const UBlueprint* Blueprint, FBPSemanticIR& IR)
{
    if (Blueprint == nullptr)
    {
        return;
    }

    // GeneratedClassがない未コンパイルBlueprintでは取得不可として記録する。
    UClass* GeneratedClass = Blueprint->GeneratedClass;
    const UObject* CDO = GeneratedClass != nullptr ? GeneratedClass->GetDefaultObject() : nullptr;

    for (const FBPVariableDescription& Description : Blueprint->NewVariables)
    {
        FBPSemanticVariable Variable;
        Variable.Name = Description.VarName;
        Variable.Guid = Description.VarGuid;
        Variable.PinCategory = Description.VarType.PinCategory.ToString();
        Variable.TypePath = GetPathNameSafe(Description.VarType.PinSubCategoryObject.Get());
        Variable.PropertyFlags = FString::Printf(TEXT("%llu"), static_cast<uint64>(Description.PropertyFlags));
        Variable.Type.Kind = ResolveType(Description.VarType);
        // コンテナをスカラーとして誤表現しない。IRの型体系にコンテナを追加するまで未解決扱い。
        if (Description.VarType.ContainerType != EPinContainerType::None)
        {
            Variable.Type.Kind = EBPSemanticTypeKind::Unresolved;
        }
        Variable.Type.TypePath = Variable.TypePath;
        Variable.Type.bIsReference = Description.VarType.bIsReference;
        Variable.Type.bIsConst = Description.VarType.bIsConst;

        // J2-2でCDOから型安全に取得するまで、宣言のDefaultValueをCDO値と誤認しない。
        // コンテナやreal型のサブカテゴリなど未解決情報は推測せず診断する。
        if (Description.VarType.ContainerType != EPinContainerType::None ||
            Variable.Type.Kind == EBPSemanticTypeKind::Unresolved)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.Message = FString::Printf(TEXT("Variable %s requires type resolution: %s"),
                *Variable.Name.ToString(), *Variable.PinCategory);
            IR.Diagnostics.Add(MoveTemp(Diagnostic));
        }
        // 宣言名に対応する生成クラスのPropertyのみ対象とする。
        // 配列・Map・Set・参照型・未解決型はJ2-2の安全なスカラー対象外。
        if (CDO != nullptr &&
            Description.VarType.ContainerType == EPinContainerType::None &&
            Description.VarType.bIsReference == false &&
            Variable.Type.Kind != EBPSemanticTypeKind::Unresolved)
        {
            const FProperty* Property = FindFProperty<FProperty>(GeneratedClass, Description.VarName);
            if (ReadScalarDefault(Property, CDO, Variable.Type.Kind, Variable.DefaultValue))
            {
                Variable.bDefaultValueAvailable = true;
                Variable.DefaultValueSource = TEXT("cdo");
            }
        }
        IR.Variables.Add(MoveTemp(Variable));
    }
}
