#pragma once

#include "CoreMinimal.h"

// K2_SetActorLocationのC++引数順序を固定し、文字列の任意挿入を避ける。
// Locationは既存のGetActorLocation式、boolは正規化済みのリテラルに限定する。
struct FBPSetActorLocationCallArguments
{
    static bool Emit(const FString& Location, const FString& Sweep,
        const FString& HitVariable, const FString& Teleport, FString& OutArguments)
    {
        OutArguments.Reset();
        if (Location != TEXT("GetActorLocation()") ||
            (Sweep != TEXT("true") && Sweep != TEXT("false")) ||
            (Teleport != TEXT("true") && Teleport != TEXT("false")) ||
            HitVariable.StartsWith(TEXT("BP_Hit_")) == false ||
            HitVariable.Len() != 39)
        {
            return false;
        }
        // FHitResultはK2_SetActorLocationの参照引数へ値として渡す。
        // SetActorLocationのポインタ引数とは区別する。
        OutArguments = FString::Printf(TEXT("%s, %s, %s, %s"),
            *Location, *Sweep, *HitVariable, *Teleport);
        return true;
    }
};
