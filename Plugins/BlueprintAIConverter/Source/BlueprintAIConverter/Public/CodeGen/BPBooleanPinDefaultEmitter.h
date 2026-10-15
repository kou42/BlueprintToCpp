#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "EdGraphSchema_K2.h"

// Blueprintのbool既定値をC++リテラルへ変換する。
// 入力文字列をC++へ直接埋め込まず、既知の表記のみを許可する。
struct FBPBooleanPinDefaultEmitter
{
    static bool Emit(const FBPIRPin& Pin, FString& OutCpp)
    {
        OutCpp.Reset();
        if (Pin.Direction != EGPD_Input || Pin.Category != UEdGraphSchema_K2::PC_Boolean)
        {
            return false;
        }

        if (Pin.DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
        {
            OutCpp = TEXT("true");
            return true;
        }
        if (Pin.DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
        {
            OutCpp = TEXT("false");
            return true;
        }
        return false;
    }
};
