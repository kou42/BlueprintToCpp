#include "BPConnectedActorLocationLowering.h"

#include "BPActorLocationLowering.h"
#include "BPDataLinkResolver.h"

bool FBPConnectedActorLocationLowering::LowerInput(const FBPGraphIR& GraphIR,
    const FGuid& TargetNodeId, const FGuid& TargetPinId,
    FBPSemanticExpression& OutExpression,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutExpression = FBPSemanticExpression();

    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = TargetNodeId;
        Diagnostic.PinId = TargetPinId;
        Diagnostic.Message = TEXT("Graph IR has unresolved diagnostics");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    const FBPIRGraph* SelectedGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == TargetNodeId)
            {
                if (SelectedGraph != nullptr)
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.NodeId = TargetNodeId;
                    Diagnostic.PinId = TargetPinId;
                    Diagnostic.Message = TEXT("Target node GUID is ambiguous");
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
                SelectedGraph = &Graph;
            }
        }
    }
    if (SelectedGraph == nullptr)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = TargetNodeId;
        Diagnostic.PinId = TargetPinId;
        Diagnostic.Message = TEXT("Target node does not exist");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    const FBPIRNode* SourceNode = nullptr;
    const FBPIRPin* SourcePin = nullptr;
    TArray<FString> Messages;
    if (FBPDataLinkResolver::ResolveInput(*SelectedGraph, TargetNodeId, TargetPinId,
        SourceNode, SourcePin, Messages) == false)
    {
        for (const FString& Message : Messages)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = SelectedGraph->Name;
            Diagnostic.NodeId = TargetNodeId;
            Diagnostic.PinId = TargetPinId;
            Diagnostic.Message = Message;
            OutDiagnostics.Add(MoveTemp(Diagnostic));
        }
        return false;
    }

    // 接続元の戻り値ピン以外を式の出力として誤認しない。
    if (SourceNode == nullptr || SourcePin == nullptr ||
        SourceNode->FunctionName != TEXT("K2_GetActorLocation") ||
        SourcePin->Name != TEXT("ReturnValue"))
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.GraphName = SelectedGraph->Name;
        Diagnostic.NodeId = TargetNodeId;
        Diagnostic.PinId = TargetPinId;
        Diagnostic.Message = TEXT("Unsupported source expression");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    FBPSemanticExpression Candidate;
    if (FBPActorLocationLowering::LowerGetActorLocation(GraphIR, SourceNode->Id,
        Candidate, OutDiagnostics) == false)
    {
        return false;
    }
    if (Candidate.SourcePinId != SourcePin->Id)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.GraphName = SelectedGraph->Name;
        Diagnostic.NodeId = TargetNodeId;
        Diagnostic.PinId = TargetPinId;
        Diagnostic.Message = TEXT("Resolved expression pin does not match data edge");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }
    OutExpression = MoveTemp(Candidate);
    return true;
}
