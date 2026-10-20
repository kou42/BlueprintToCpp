#include "BPCallTargetResolver.h"

#include "BPDataLinkResolver.h"
#include "EdGraphSchema_K2.h"

namespace
{
void Report(TArray<FBPSemanticDiagnostic>& Diagnostics, const FBPIRGraph* Graph,
    const FGuid& NodeId, const FGuid& PinId, const FString& Message)
{
    FBPSemanticDiagnostic Diagnostic;
    Diagnostic.GraphName = Graph != nullptr ? Graph->Name : FString();
    Diagnostic.NodeId = NodeId;
    Diagnostic.PinId = PinId;
    Diagnostic.Message = Message;
    Diagnostics.Add(MoveTemp(Diagnostic));
}
}

bool FBPCallTargetResolver::Resolve(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, FBPSemanticExpression& OutTarget,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutTarget = FBPSemanticExpression();
    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        Report(OutDiagnostics, nullptr, CallNodeId, FGuid(),
            TEXT("Graph IR contains unresolved diagnostics"));
        return false;
    }

    const FBPIRGraph* Graph = nullptr;
    const FBPIRNode* Call = nullptr;
    for (const FBPIRGraph& CandidateGraph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : CandidateGraph.Nodes)
        {
            if (Node.Id == CallNodeId)
            {
                if (Call != nullptr)
                {
                    Report(OutDiagnostics, &CandidateGraph, CallNodeId, FGuid(),
                        TEXT("Ambiguous call node GUID"));
                    return false;
                }
                Call = &Node;
                Graph = &CandidateGraph;
            }
        }
    }
    if (Graph == nullptr || Call == nullptr)
    {
        Report(OutDiagnostics, nullptr, CallNodeId, FGuid(), TEXT("Call node not found"));
        return false;
    }

    const FBPIRPin* TargetPin = nullptr;
    for (const FBPIRPin& Pin : Call->Pins)
    {
        if (Pin.Name == TEXT("self") || Pin.Name == TEXT("Target"))
        {
            if (TargetPin != nullptr || Pin.Direction != EGPD_Input ||
                Pin.Category != UEdGraphSchema_K2::PC_Object)
            {
                Report(OutDiagnostics, Graph, CallNodeId, Pin.Id,
                    TEXT("Ambiguous or invalid object target pin"));
                return false;
            }
            TargetPin = &Pin;
        }
    }
    if (TargetPin == nullptr)
    {
        Report(OutDiagnostics, Graph, CallNodeId, FGuid(),
            TEXT("Object target pin is missing"));
        return false;
    }

    int32 Incoming = 0;
    for (const FBPIRLink& Link : Graph->DataEdges)
    {
        if (Link.TargetNode == CallNodeId && Link.TargetPin == TargetPin->Id)
        {
            ++Incoming;
        }
    }
    if (Incoming > 1)
    {
        Report(OutDiagnostics, Graph, CallNodeId, TargetPin->Id,
            TEXT("Multiple object target connections"));
        return false;
    }

    FBPSemanticExpression Candidate;
    Candidate.Kind = EBPSemanticExpressionKind::VariableReference;
    Candidate.Type.Kind = EBPSemanticTypeKind::Object;
    Candidate.SourceNodeId = CallNodeId;
    Candidate.SourcePinId = TargetPin->Id;

    if (Incoming == 0)
    {
        // 未接続selfは現在のBlueprintインスタンスを指す。
        // 明示Targetの未接続は暗黙のthisとして扱わない。
        if (TargetPin->Name != TEXT("self"))
        {
            Report(OutDiagnostics, Graph, CallNodeId, TargetPin->Id,
                TEXT("Unconnected explicit Target is unsupported"));
            return false;
        }
        Candidate.VariableName = TEXT("this");
        Candidate.Type.TypePath = GraphIR.ParentClassPath;
    }
    else
    {
        const FBPIRNode* SourceNode = nullptr;
        const FBPIRPin* SourcePin = nullptr;
        TArray<FString> Messages;
        if (FBPDataLinkResolver::ResolveInput(*Graph, CallNodeId, TargetPin->Id,
            SourceNode, SourcePin, Messages) == false)
        {
            for (const FString& Message : Messages)
            {
                Report(OutDiagnostics, Graph, CallNodeId, TargetPin->Id, Message);
            }
            return false;
        }
        // 変数Get以外の純粋式を名前だけで推測してはいけない。
        if (SourceNode == nullptr || SourcePin == nullptr ||
            SourceNode->ClassPath != TEXT("/Script/BlueprintGraph.K2Node_VariableGet") ||
            SourceNode->VariableName.IsNone() ||
            SourcePin->Category != UEdGraphSchema_K2::PC_Object ||
            SourcePin->SubCategoryObjectPath.IsEmpty())
        {
            Report(OutDiagnostics, Graph, CallNodeId, TargetPin->Id,
                TEXT("Connected target must be a typed object VariableGet"));
            return false;
        }
        Candidate.VariableName = SourceNode->VariableName;
        Candidate.Type.TypePath = SourcePin->SubCategoryObjectPath;
        Candidate.SourceNodeId = SourceNode->Id;
        Candidate.SourcePinId = SourcePin->Id;
    }

    OutTarget = MoveTemp(Candidate);
    return true;
}
