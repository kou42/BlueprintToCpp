#include "BPConnectedFunctionCallEmitter.h"

#include "BPConnectedFunctionArguments.h"
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
