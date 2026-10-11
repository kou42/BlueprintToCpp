#include "BPConnectedFunctionCallEmitter.h"

#include "BPConnectedFunctionArguments.h"
#include "BPFunctionSignatureResolver.h"
#include "BPFunctionCallValidator.h"
#include "GameFramework/Actor.h"
#include "EdGraphSchema_K2.h"
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

    // 出力引数はC++側で書き込み可能な変数を必要とする。
    // 接続先がある場合は後続のデータフローと変数寿命の解決が必要なため、
    // 未接続の場合と区別して診断する。どちらも現段階では安全に拒否する。
    for (int32 Index = 0; Index < Signature.OutputParameters.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index] == false)
        {
            continue;
        }

        const FName ParameterName = Signature.ParameterNames[Index];
        const FBPIRPin* OutputPin = nullptr;
        for (const FBPIRPin& Pin : CallNode->Pins)
        {
            if (Pin.Name == ParameterName && Pin.Direction == EGPD_Output)
            {
                OutputPin = &Pin;
                break;
            }
        }

        // Validatorが成功した場合、出力ピンは必ず一つ存在する。
        // IRが不整合でも誤ったC++を出さないよう再確認する。
        if (OutputPin == nullptr || CallGraph == nullptr)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = CallGraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = FString::Printf(TEXT("Output parameter pin missing: %s"),
                *ParameterName.ToString());
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }

        int32 ConnectedEdges = 0;
        for (const FBPIRLink& Edge : CallGraph->DataEdges)
        {
            if (Edge.SourceNode == CallNodeId && Edge.SourcePin == OutputPin->Id)
            {
                // 接続先が同じグラフに存在し、入力データピンであることを確認する。
                // 存在しないノードやExecピンを参照するIRを有効なデータフローと数えない。
                const FBPIRPin* TargetPin = nullptr;
                int32 TargetMatches = 0;
                for (const FBPIRNode& TargetNode : CallGraph->Nodes)
                {
                    if (TargetNode.Id == Edge.TargetNode)
                    {
                        for (const FBPIRPin& Pin : TargetNode.Pins)
                        {
                            if (Pin.Id == Edge.TargetPin)
                            {
                                ++TargetMatches;
                                TargetPin = &Pin;
                            }
                        }
                    }
                }
                if (TargetMatches != 1 || TargetPin == nullptr ||
                    TargetPin->Direction != EGPD_Input ||
                    TargetPin->Category == UEdGraphSchema_K2::PC_Exec)
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.GraphName = CallGraphName;
                    Diagnostic.NodeId = CallNodeId;
                    Diagnostic.PinId = OutputPin->Id;
                    Diagnostic.Message = FString::Printf(
                        TEXT("Invalid output parameter data edge target: %s"),
                        *ParameterName.ToString());
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
                ++ConnectedEdges;
            }
            // 逆方向のリンクも不正なIRとして拒否する。
            if (Edge.TargetNode == CallNodeId && Edge.TargetPin == OutputPin->Id)
            {
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.GraphName = CallGraphName;
                Diagnostic.NodeId = CallNodeId;
                Diagnostic.PinId = OutputPin->Id;
                Diagnostic.Message = FString::Printf(TEXT("Invalid incoming data edge on output parameter: %s"),
                    *ParameterName.ToString());
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
        }

        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.GraphName = CallGraphName;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.PinId = OutputPin->Id;
        Diagnostic.Message = ConnectedEdges > 0
            ? FString::Printf(TEXT("Connected output parameter requires data-flow lowering: %s (%d edges)"),
                *ParameterName.ToString(), ConnectedEdges)
            : FString::Printf(TEXT("Unconnected output parameter requires writable C++ storage: %s"),
                *ParameterName.ToString());
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
