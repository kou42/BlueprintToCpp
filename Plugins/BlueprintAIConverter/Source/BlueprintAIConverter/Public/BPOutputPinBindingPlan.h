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
