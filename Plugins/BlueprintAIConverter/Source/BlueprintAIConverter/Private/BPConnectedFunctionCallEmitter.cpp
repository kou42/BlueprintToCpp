#include "BPConnectedFunctionCallEmitter.h"

#include "BPConnectedFunctionArguments.h"
#include "BPFunctionSignatureResolver.h"
#include "BPFunctionCallValidator.h"
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

    // 呼び出しノードが存在しない場合は署名の詳細より先に構造エラーを報告する。
    int32 CallMatches = 0;
    const FBPIRNode* CallNode = nullptr;
    FString CallGraphName;
    const FBPIRGraph* CallGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == CallNodeId)
            {
                ++CallMatches;
                CallNode = &Node;
                CallGraphName = Graph.Name;
                CallGraph = &Graph;
            }
        }
    }
    if (CallMatches != 1)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Call node missing or ambiguous");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // 引数の接続を調べる前に、ノードのピン方向と型を署名と照合する。
    FBPResolvedFunctionSignature ValidatedSignature;
    TArray<FString> ValidationMessages;
    if (FBPFunctionCallValidator::Validate(*CallNode, Function,
        ValidatedSignature, ValidationMessages) == false)
    {
        for (const FString& Message : ValidationMessages)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = CallGraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = Message;
            OutDiagnostics.Add(MoveTemp(Diagnostic));
        }
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

    // 出力ピンはC++呼び出しの参照引数に対応するため、
    // 現時点では名前を診断して生成を止める。
    for (int32 Index = 0; Index < Signature.OutputParameters.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index])
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = FString::Printf(TEXT("Unsupported output parameter: %s"),
                *Signature.ParameterNames[Index].ToString());
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }
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
