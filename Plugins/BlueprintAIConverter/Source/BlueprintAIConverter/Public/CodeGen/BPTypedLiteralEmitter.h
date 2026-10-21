#pragma once

#include "CoreMinimal.h"
#include "BPSemanticIR.h"
//#include "Misc/LexFromString.h" UE5では使えないみたい
//#include "Misc/LexToString.h" 

// Blueprintの既定値を型検証したうえで安全なC++リテラルへ変換する。
struct FBPTypedLiteralEmitter
{
    static bool Emit(const FBPSemanticType& Type, const FString& Value, FString& Out)
    {
        Out.Reset();
        if (Type.bIsReference || Type.ContainerKind != TEXT("none"))
        {
            return false;
        }
        if (Type.Kind == EBPSemanticTypeKind::Bool)
        {
            if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
            {
                Out = TEXT("true");
                return true;
            }
            if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
            {
                Out = TEXT("false");
                return true;
            }
            return false;
        }
        if (Type.Kind == EBPSemanticTypeKind::Int32)
        {
            int32 Parsed = 0;
            if (LexTryParseString(Parsed, *Value) == false)
            {
                return false;
            }
            Out = LexToString(Parsed);
            return true;
        }
        if (Type.Kind == EBPSemanticTypeKind::Int64)
        {
            int64 Parsed = 0;
            if (LexTryParseString(Parsed, *Value) == false)
            {
                return false;
            }
            Out = FString::Printf(TEXT("static_cast<int64>(%s)"), *LexToString(Parsed));
            return true;
        }
        if (Type.Kind == EBPSemanticTypeKind::Float || Type.Kind == EBPSemanticTypeKind::Double)
        {
            double Parsed = 0.0;
            if (LexTryParseString(Parsed, *Value) == false || FMath::IsFinite(Parsed) == false)
            {
                return false;
            }
            if (Type.Kind == EBPSemanticTypeKind::Float)
            {
                const float Narrowed = static_cast<float>(Parsed);
                if (FMath::IsFinite(Narrowed) == false)
                {
                    return false;
                }
                Out = FString::Printf(TEXT("static_cast<float>(%s)"), *LexToString(Narrowed));
            }
            else
            {
                Out = LexToString(Parsed);
            }
            return true;
        }
        // FTextはローカライズ識別子を復元できないため対象外。
        if (Type.Kind != EBPSemanticTypeKind::String && Type.Kind != EBPSemanticTypeKind::Name)
        {
            return false;
        }
        FString Escaped;
        for (const TCHAR Character : Value)
        {
            switch (Character)
            {
            case TEXT('\\'): Escaped += TEXT("\\\\"); break;
            case TEXT('"'): Escaped += TEXT("\\\""); break;
            case TEXT('\n'): Escaped += TEXT("\\n"); break;
            case TEXT('\r'): Escaped += TEXT("\\r"); break;
            case TEXT('\t'): Escaped += TEXT("\\t"); break;
            default:
                if (Character < 0x20)
                {
                    return false;
                }
                Escaped.AppendChar(Character);
                break;
            }
        }
        const FString Literal = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
        Out = Type.Kind == EBPSemanticTypeKind::String
            ? FString::Printf(TEXT("FString(%s)"), *Literal)
            : FString::Printf(TEXT("FName(%s)"), *Literal);
        return true;
    }
};
