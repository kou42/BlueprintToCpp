#include "BPDataLinkResolver.h"

bool FBPDataLinkResolver::ResolveInput(const FBPIRGraph& Graph,
    const FGuid& TargetNodeId, const FGuid& TargetPinId,
    const FBPIRNode*& OutSourceNode, const FBPIRPin*& OutSourcePin,
    TArray<FString>& OutDiagnostics)
{
    OutSourceNode = nullptr;
    OutSourcePin = nullptr;

    const FBPIRPin* Target = nullptr;
    int32 TargetCount = 0;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.Id == TargetNodeId)
        {
            for (const FBPIRPin& Pin : Node.Pins)
            {
                if (Pin.Id == TargetPinId)
                {
                    ++TargetCount;
                    Target = &Pin;
                }
            }
        }
    }
    if (TargetCount != 1 || Target == nullptr || Target->Direction != EGPD_Input)
    {
        OutDiagnostics.Add(TEXT("Target input pin is missing, ambiguous, or has wrong direction"));
        return false;
    }

    const FBPIRLink* Selected = nullptr;
    for (const FBPIRLink& Link : Graph.DataEdges)
    {
        if (Link.TargetNode == TargetNodeId && Link.TargetPin == TargetPinId)
        {
            if (Selected != nullptr)
            {
                OutDiagnostics.Add(TEXT("Multiple data sources for one input pin"));
                return false;
            }
            Selected = &Link;
        }
    }
    if (Selected == nullptr)
    {
        OutDiagnostics.Add(TEXT("No data source for input pin"));
        return false;
    }

    int32 SourceCount = 0;
    const FBPIRNode* SourceNode = nullptr;
    const FBPIRPin* SourcePin = nullptr;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.Id == Selected->SourceNode)
        {
            for (const FBPIRPin& Pin : Node.Pins)
            {
                if (Pin.Id == Selected->SourcePin)
                {
                    ++SourceCount;
                    SourceNode = &Node;
                    SourcePin = &Pin;
                }
            }
        }
    }
    if (SourceCount != 1 || SourcePin == nullptr || SourcePin->Direction != EGPD_Output)
    {
        OutDiagnostics.Add(TEXT("Source output pin is missing, ambiguous, or has wrong direction"));
        return false;
    }

    // カテゴリと構造体/オブジェクトの実型を比較する。
    // 暗黙変換・ワイルドカードはこの段階で扱わない。
    if (SourcePin->Category.IsNone() ||
        SourcePin->Category != Target->Category ||
        SourcePin->SubCategory != Target->SubCategory ||
        SourcePin->SubCategoryObjectPath != Target->SubCategoryObjectPath)
    {
        OutDiagnostics.Add(TEXT("Connected data pin types do not match"));
        return false;
    }

    OutSourceNode = SourceNode;
    OutSourcePin = SourcePin;
    return true;
}
