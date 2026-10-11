#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

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
                Consumers.Contains(Edge.TargetPin))
            {
                OutError = TEXT("Output target missing, ambiguous or duplicated");
                return false;
            }
            Consumers.Add(Edge.TargetPin);
        }
        // 接続先の型適合性は既存のFunctionPinTypeMatcherが担う。
        // ここではGUIDに基づく対応と重複・方向の検証のみを担当する。
        OutBinding.SourceNodeId = SourceNodeId;
        OutBinding.SourcePinId = SourcePinId;
        OutBinding.VariableName = VariableName;
        OutBinding.TypeKind = Type.Kind;
        OutBinding.TypePath = Type.TypePath;
        OutBinding.ConsumerPinIds = MoveTemp(Consumers);
        return true;
    }
};
