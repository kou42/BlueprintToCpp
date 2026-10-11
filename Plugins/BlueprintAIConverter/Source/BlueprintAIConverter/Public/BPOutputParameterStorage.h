#pragma once
#include "CoreMinimal.h"
#include "BPSemanticIR.h"

// 出力引数の変数名はノードGUIDから決定し、異なるノード間の衝突を避ける。
struct FBPOutputParameterStorage
{
    static bool Emit(const FBPSemanticType& Type, const FGuid& NodeId,
        FString& OutName, FString& OutDeclaration)
    {
        OutName.Reset();
        OutDeclaration.Reset();
        if (NodeId.IsValid() == false ||
            Type.Kind != EBPSemanticTypeKind::Struct ||
            Type.TypePath != TEXT("/Script/Engine.HitResult") ||
            Type.bIsConst)
        {
            return false;
        }
        OutName = FString::Printf(TEXT("BP_Hit_%s"), *NodeId.ToString(EGuidFormats::Digits));
        OutDeclaration = FString::Printf(TEXT("FHitResult %s{};"), *OutName);
        return true;
    }
};
