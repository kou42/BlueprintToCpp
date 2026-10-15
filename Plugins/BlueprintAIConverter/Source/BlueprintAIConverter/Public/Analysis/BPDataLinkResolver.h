#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"
#include "BPSemanticIR.h"

// Graph IRのデータエッジをピンGUIDで解決する。
// 現段階では単一入力に対する単一接続だけを受理する。
class BLUEPRINTAICONVERTER_API FBPDataLinkResolver
{
public:
    static bool ResolveInput(const FBPIRGraph& Graph, const FGuid& TargetNodeId,
        const FGuid& TargetPinId, const FBPIRNode*& OutSourceNode,
        const FBPIRPin*& OutSourcePin, TArray<FString>& OutDiagnostics);
};
