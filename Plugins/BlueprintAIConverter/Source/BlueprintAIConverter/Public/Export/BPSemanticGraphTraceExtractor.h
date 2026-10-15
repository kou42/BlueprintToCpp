#pragma once

#include "BPSemanticIR.h"

// 元Graph IRの接続情報を、Loweringの成否にかかわらず保持する。
class FBPSemanticGraphTraceExtractor
{
public:
    static void Append(const FBPGraphIR& GraphIR, FBPSemanticIR& IR)
    {
        IR.GraphTraces.Reset();
        for (const FBPIRGraph& Graph : GraphIR.Graphs)
        {
            FBPSemanticGraphTrace Trace;
            Trace.Name = Graph.Name;
            Trace.Path = Graph.Path;
            Trace.Kind = Graph.Kind;
            Trace.ExecEdges = Graph.ExecEdges;
            Trace.DataEdges = Graph.DataEdges;
            for (const FBPIRNode& Node : Graph.Nodes)
            {
                Trace.NodeIds.Add(Node.Id);
                for (const FBPIRPin& Pin : Node.Pins)
                {
                    Trace.PinIds.Add(Pin.Id);
                }
            }
            IR.GraphTraces.Add(MoveTemp(Trace));
        }
    }
};
