#pragma once

#include "CoreMinimal.h"
#include "BPFunctionSignatureResolver.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

// 呼び出し先の所有クラスとUFunctionを、Actor等の具体的な型に依存せず解決する。
// この段階では静的関数とself上のネイティブ関数だけを対象とする。
// 任意のオブジェクトTargetは型付きTarget式のLoweringが実装されるまで拒否する。
struct FBPReflectedCall
{
    const UClass* Owner = nullptr;
    const UFunction* Function = nullptr;
    FBPResolvedFunctionSignature Signature;
    bool bStatic = false;
};

class FBPReflectedCallResolver
{
public:
    // Targetの実際のUClassを渡せば、特定のクラス名を列挙せず継承関係を検証できる。
    static bool ResolveForTarget(const FString& OwnerPath, const FName FunctionName,
        const FString& TargetClassPath, const bool bStatic,
        FBPReflectedCall& Out, TArray<FString>& Diagnostics)
    {
        return Resolve(OwnerPath, FunctionName, TargetClassPath,
            bStatic, Out, Diagnostics);
    }

    static bool Resolve(const FString& OwnerPath, const FName FunctionName,
        const FString& BlueprintParentPath, const bool bExpectStatic,
        FBPReflectedCall& Out, TArray<FString>& Diagnostics)
    {
        Out = FBPReflectedCall();
        if (OwnerPath.IsEmpty() || FunctionName.IsNone() || BlueprintParentPath.IsEmpty())
        {
            Diagnostics.Add(TEXT("Function owner, name or Blueprint parent is missing"));
            return false;
        }
        const UClass* Owner = FindObject<UClass>(nullptr, *OwnerPath);
        const UClass* Parent = FindObject<UClass>(nullptr, *BlueprintParentPath);
        if (Owner == nullptr || Parent == nullptr)
        {
            Diagnostics.Add(TEXT("Function owner or Blueprint parent class cannot be resolved"));
            return false;
        }
        const UFunction* Function = Owner->FindFunctionByName(FunctionName);
        if (Function == nullptr || Function->HasAnyFunctionFlags(FUNC_Native) == false ||
            Function->HasAnyFunctionFlags(FUNC_Static) != bExpectStatic)
        {
            Diagnostics.Add(TEXT("Native function or static flag does not match reflection"));
            return false;
        }
        if (bExpectStatic == false && Parent->IsChildOf(Owner) == false)
        {
            Diagnostics.Add(TEXT("Instance function cannot be invoked on Blueprint self"));
            return false;
        }
        // 静的関数はヘッダーを特定できる場合のみコード生成可能とする。
        if (bExpectStatic && Owner->GetMetaData(TEXT("IncludePath")).IsEmpty())
        {
            Diagnostics.Add(TEXT("Static function owner has no C++ IncludePath"));
            return false;
        }
        FBPResolvedFunctionSignature Signature;
        if (FBPFunctionSignatureResolver::Resolve(Function, Signature, Diagnostics) == false)
        {
            return false;
        }
        Out.Owner = Owner;
        Out.Function = Function;
        Out.Signature = MoveTemp(Signature);
        Out.bStatic = bExpectStatic;
        return true;
    }
};
