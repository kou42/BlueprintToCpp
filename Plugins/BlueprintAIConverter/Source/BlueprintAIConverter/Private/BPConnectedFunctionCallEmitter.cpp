#include "BPConnectedFunctionCallEmitter.h"

#include "BPConnectedFunctionArguments.h"
#include "BPFunctionSignatureResolver.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"

bool FBPConnectedFunctionCallEmitter::Emit(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, const UFunction* Function, FString& OutStatement,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutStatement.Reset();

    // 任意の反射名をそのままC++へ出力せず、変換規約が確認済みのAPIに限定する。
    if (Function == nullptr ||
        Function->GetOwnerClass() != AActor::StaticClass() ||
        Function->GetFName() != TEXT("K2_SetActorLocation"))
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Unsupported function for C++ statement emission");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> Messages;
    if (FBPFunctionSignatureResolver::Resolve(Function, Signature, Messages) == false)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = FString::Join(Messages, TEXT("; "));
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // 出力パラメータに対応するピンの接続先を検証するまでは生成しない。
    // 引数数と型を事前に確認し、エラーメッセージを明確にする。
    if (Signature.ParameterNames.Num() != Signature.ParameterTypes.Num() ||
        Signature.ParameterNames.Num() != Signature.OutputParameters.Num())
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Inconsistent function signature");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // 出力パラメータを省略したC++呼び出しは生成しない。
    if (Signature.OutputParameters.Contains(true))
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Output parameters are not supported");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    FString Arguments;
    if (FBPConnectedFunctionArguments::Emit(GraphIR, CallNodeId, Function,
        Arguments, OutDiagnostics) == false)
    {
        return false;
    }

    // K2_SetActorLocationにはSweepHitResultなどの出力引数があり、
    // 現在の引数Emitterでは未対応のため安全に拒否される。
    OutStatement = FString::Printf(TEXT("SetActorLocation(%s);"), *Arguments);
    return true;
}
