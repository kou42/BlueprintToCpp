#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"
#include "EdGraphSchema_K2.h"

// 型の暗黙変換は行わず、Blueprintピンの型情報と解決済みの型を照合する。
// コンテナや未解決の型は現在のSemantic IRでは表現できないため拒否する。
struct FBPFunctionPinTypeMatcher
{
    static bool Matches(const FBPIRPin& Pin, const FBPSemanticType& Type)
    {
        switch (Type.Kind)
        {
        case EBPSemanticTypeKind::Bool:
            return Pin.Category == UEdGraphSchema_K2::PC_Boolean;
        case EBPSemanticTypeKind::Int32:
            return Pin.Category == UEdGraphSchema_K2::PC_Int;
        case EBPSemanticTypeKind::Int64:
            return Pin.Category == UEdGraphSchema_K2::PC_Int64;
        case EBPSemanticTypeKind::Float:
            return Pin.Category == UEdGraphSchema_K2::PC_Real &&
                Pin.SubCategory == UEdGraphSchema_K2::PC_Float;
        case EBPSemanticTypeKind::Double:
            return Pin.Category == UEdGraphSchema_K2::PC_Real &&
                Pin.SubCategory == UEdGraphSchema_K2::PC_Double;
        case EBPSemanticTypeKind::String:
            return Pin.Category == UEdGraphSchema_K2::PC_String;
        case EBPSemanticTypeKind::Name:
            return Pin.Category == UEdGraphSchema_K2::PC_Name;
        case EBPSemanticTypeKind::Text:
            return Pin.Category == UEdGraphSchema_K2::PC_Text;
        case EBPSemanticTypeKind::Object:
            return Pin.Category == UEdGraphSchema_K2::PC_Object &&
                Type.TypePath.IsEmpty() == false &&
                Pin.SubCategoryObjectPath == Type.TypePath;
        case EBPSemanticTypeKind::Struct:
            return Pin.Category == UEdGraphSchema_K2::PC_Struct &&
                Type.TypePath.IsEmpty() == false &&
                Pin.SubCategoryObjectPath == Type.TypePath;
        default:
            return false;
        }
    }
};
