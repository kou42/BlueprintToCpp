#include "BPSemanticVariableExtractor.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"

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
}

void FBPSemanticVariableExtractor::Append(const UBlueprint* Blueprint, FBPSemanticIR& IR)
{
    if (Blueprint == nullptr)
    {
        return;
    }

    for (const FBPVariableDescription& Description : Blueprint->NewVariables)
    {
        FBPSemanticVariable Variable;
        Variable.Name = Description.VarName;
        Variable.Guid = Description.VarGuid;
        Variable.PinCategory = Description.VarType.PinCategory.ToString();
        Variable.TypePath = GetPathNameSafe(Description.VarType.PinSubCategoryObject.Get());
        Variable.PropertyFlags = FString::Printf(TEXT("%llu"), static_cast<uint64>(Description.PropertyFlags));
        Variable.Type.Kind = ResolveType(Description.VarType);
        Variable.Type.TypePath = Variable.TypePath;
        Variable.Type.bIsReference = Description.VarType.bIsReference;
        Variable.Type.bIsConst = Description.VarType.bIsConst;

        // J2-2でCDOから型安全に取得するまで、宣言のDefaultValueをCDO値と誤認しない。
        // コンテナやreal型のサブカテゴリなど未解決情報は推測せず診断する。
        if (Description.VarType.ContainerType != EPinContainerType::None ||
            Variable.Type.Kind == EBPSemanticTypeKind::Unresolved ||
            (Variable.PinCategory == TEXT("real") && Variable.Type.Kind == EBPSemanticTypeKind::Unresolved))
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.Message = FString::Printf(TEXT("Variable %s requires type resolution: %s"),
                *Variable.Name.ToString(), *Variable.PinCategory);
            IR.Diagnostics.Add(MoveTemp(Diagnostic));
        }
        IR.Variables.Add(MoveTemp(Variable));
    }
}
