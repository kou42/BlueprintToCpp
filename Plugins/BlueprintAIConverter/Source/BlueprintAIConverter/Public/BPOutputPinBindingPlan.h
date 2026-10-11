#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"
#include "BPFunctionPinTypeMatcher.h"

// 出力ピンからC++変数への対応を、実際の式生成より先に確定する。
// データフローが未実装の段階では接続先を検証し、使用可能な変数名だけを保持する。
struct FBPOutputPinBinding
{
    FGuid SourceNodeId;
    FGuid SourcePinId;
    FString VariableName;
    EBPSemanticTypeKind TypeKind = EBPSemanticTypeKind::Unresolved;
    FString TypePath;
    TArray<FGuid> ConsumerPinIds;
    TArray<FGuid> ConsumerNodeIds;
};

struct FBPOutputPinBindingPlan
{
    static bool Build(const FBPIRGraph& Graph, const FGuid& SourceNodeId,
        const FGuid& SourcePinId, const FString& VariableName,
        const FBPSemanticType& Type, FBPOutputPinBinding& OutBinding,
        FString& OutError)
    {
        OutBinding = FBPOutputPinBinding();
        OutError.Reset();
        if (SourceNodeId.IsValid() == false || SourcePinId.IsValid() == false ||
            VariableName.IsEmpty() || Type.Kind == EBPSemanticTypeKind::Unresolved)
        {
            OutError = TEXT("Invalid output binding identity or type");
            return false;
        }
        const FBPIRPin* SourcePin = nullptr;
        int32 SourceMatches = 0;
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == SourceNodeId)
            {
                for (const FBPIRPin& Pin : Node.Pins)
                {
                    if (Pin.Id == SourcePinId && Pin.Direction == EGPD_Output)
                    {
                        ++SourceMatches;
                        SourcePin = &Pin;
                    }
                }
            }
        }
        if (SourceMatches != 1 || SourcePin == nullptr)
        {
            OutError = TEXT("Output source pin missing or ambiguous");
            return false;
        }
        TArray<FGuid> Consumers;
        TArray<FGuid> ConsumerNodes;
        for (const FBPIRLink& Edge : Graph.DataEdges)
        {
            if (Edge.TargetNode == SourceNodeId && Edge.TargetPin == SourcePinId)
            {
                OutError = TEXT("Incoming edge on output pin");
                return false;
            }
            if (Edge.SourceNode != SourceNodeId || Edge.SourcePin != SourcePinId)
            {
                continue;
            }
            int32 Matches = 0;
            const FBPIRPin* TargetPin = nullptr;
            for (const FBPIRNode& Node : Graph.Nodes)
            {
                if (Node.Id == Edge.TargetNode)
                {
                    for (const FBPIRPin& Pin : Node.Pins)
                    {
                        if (Pin.Id == Edge.TargetPin && Pin.Direction == EGPD_Input)
                        {
                            ++Matches;
                            TargetPin = &Pin;
                        }
                    }
                }
            }
            if (Matches != 1 || TargetPin == nullptr ||
                FBPFunctionPinTypeMatcher::Matches(*TargetPin, Type) == false ||
                Consumers.Contains(Edge.TargetPin))
            {
                OutError = TEXT("Output target missing, ambiguous or duplicated");
                return false;
            }
            Consumers.Add(Edge.TargetPin);
            ConsumerNodes.Add(Edge.TargetNode);
        }
        // 既存のFunctionPinTypeMatcherで型を照合し、型の異なる接続を拒否する。
        OutBinding.SourceNodeId = SourceNodeId;
        OutBinding.SourcePinId = SourcePinId;
        OutBinding.VariableName = VariableName;
        OutBinding.TypeKind = Type.Kind;
        OutBinding.TypePath = Type.TypePath;
        OutBinding.ConsumerPinIds = MoveTemp(Consumers);
        OutBinding.ConsumerNodeIds = MoveTemp(ConsumerNodes);
        return true;
    }

    // 宣言と呼び出しを同じC++ブロックに置き、参照先がそのブロックの
    // 後続文である場合だけ寿命を保証する。異なるブロック間の共有は扱わない。
    static bool ValidateVariableScope(const FBPOutputPinBinding& Binding,
        const TArray<FGuid>& EmittedStatementNodeIds, FString& OutError)
    {
        OutError.Reset();
        if (Binding.VariableName.IsEmpty() ||
            Binding.ConsumerNodeIds.Num() != Binding.ConsumerPinIds.Num())
        {
            OutError = TEXT("Invalid binding for variable scope validation");
            return false;
        }
        const int32 SourceIndex = EmittedStatementNodeIds.IndexOfByKey(Binding.SourceNodeId);
        if (SourceIndex == INDEX_NONE ||
            EmittedStatementNodeIds.FindLast(Binding.SourceNodeId) != SourceIndex)
        {
            OutError = TEXT("Output source is missing or repeated in the statement block");
            return false;
        }
        for (const FGuid& ConsumerId : Binding.ConsumerNodeIds)
        {
            const int32 ConsumerIndex = EmittedStatementNodeIds.IndexOfByKey(ConsumerId);
            if (ConsumerIndex <= SourceIndex ||
                EmittedStatementNodeIds.FindLast(ConsumerId) != ConsumerIndex)
            {
                OutError = TEXT("Output consumer is outside the source variable lifetime");
                return false;
            }
        }
        return true;
    }

    // 後続入力ピンをC++変数参照に解決する際は、ピンIDだけでなく
    // 所属ノード・Exec順序・宣言スコープをまとめて検証する。
    // 不正なIRで誤った変数名を返さないよう、失敗時の出力は必ず空にする。
    static bool ResolveConsumerVariable(const FBPIRGraph& Graph,
        const FBPOutputPinBinding& Binding, const FGuid& ConsumerNodeId,
        const FGuid& ConsumerPinId, const TArray<FGuid>& EmittedStatementNodeIds,
        FString& OutVariableName, FString& OutError)
    {
        OutVariableName.Reset();
        OutError.Reset();
        if (ConsumerNodeId.IsValid() == false || ConsumerPinId.IsValid() == false)
        {
            OutError = TEXT("Invalid output consumer identity");
            return false;
        }
        int32 ConsumerMatches = 0;
        for (int32 Index = 0; Index < Binding.ConsumerPinIds.Num(); ++Index)
        {
            if (Binding.ConsumerPinIds[Index] == ConsumerPinId &&
                Binding.ConsumerNodeIds.IsValidIndex(Index) &&
                Binding.ConsumerNodeIds[Index] == ConsumerNodeId)
            {
                ++ConsumerMatches;
            }
        }
        if (ConsumerMatches != 1)
        {
            OutError = TEXT("Output consumer is missing or ambiguous");
            return false;
        }
        if (ValidateExecOrder(Graph, Binding, OutError) == false ||
            ValidateVariableScope(Binding, EmittedStatementNodeIds, OutError) == false)
        {
            return false;
        }
        OutVariableName = Binding.VariableName;
        return true;
    }

    // 出力を参照するノードは、生成元のExec実行後に必ず評価されなければならない。
    // 現段階では分岐・ループ・合流を安全に証明できないため、
    // 単一のExec後続チェーンだけを受理する保守的な判定とする。
    static bool ValidateExecOrder(const FBPIRGraph& Graph,
        const FBPOutputPinBinding& Binding, FString& OutError)
    {
        OutError.Reset();
        if (Binding.SourceNodeId.IsValid() == false ||
            Binding.SourcePinId.IsValid() == false ||
            Binding.ConsumerNodeIds.Num() != Binding.ConsumerPinIds.Num())
        {
            OutError = TEXT("Invalid binding for Exec order validation");
            return false;
        }
        for (const FGuid& ConsumerNodeId : Binding.ConsumerNodeIds)
        {
            FGuid CurrentNodeId = Binding.SourceNodeId;
            TArray<FGuid> Visited;
            bool bReached = false;
            while (Visited.Contains(CurrentNodeId) == false)
            {
                Visited.Add(CurrentNodeId);
                int32 NextCount = 0;
                FGuid NextNodeId;
                for (const FBPIRLink& Edge : Graph.ExecEdges)
                {
                    if (Edge.SourceNode == CurrentNodeId)
                    {
                        ++NextCount;
                        NextNodeId = Edge.TargetNode;
                    }
                }
                if (NextCount != 1)
                {
                    break;
                }
                // 合流点には生成元を通らない別経路があり得るため拒否する。
                int32 IncomingCount = 0;
                for (const FBPIRLink& Edge : Graph.ExecEdges)
                {
                    if (Edge.TargetNode == NextNodeId)
                    {
                        ++IncomingCount;
                    }
                }
                if (IncomingCount != 1)
                {
                    break;
                }
                if (NextNodeId == ConsumerNodeId)
                {
                    bReached = true;
                    break;
                }
                CurrentNodeId = NextNodeId;
            }
            if (bReached == false)
            {
                OutError = TEXT("Output consumer is not on a unique downstream Exec chain");
                return false;
            }
        }
        return true;
    }
};
