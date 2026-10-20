#include "BPGenericFunctionExpressionLowering.h"

#include "BPCallTargetResolver.h"
#include "BPFunctionCallValidator.h"
#include "BPDataLinkResolver.h"
#include "UObject/UObjectGlobals.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"

namespace
{
void Report(TArray<FBPSemanticDiagnostic>& Diagnostics, const FGuid& NodeId,
    const FString& Message)
{
    FBPSemanticDiagnostic Diagnostic;
    Diagnostic.NodeId = NodeId;
    Diagnostic.Message = Message;
    Diagnostics.Add(MoveTemp(Diagnostic));
}
}

namespace
{
bool LowerRecursive(const FBPGraphIR& GraphIR, const FGuid& CallNodeId,
    const UFunction* Function, FBPSemanticExpression& OutExpression,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics, TSet<FGuid>& ActiveNodes,
    int32 Depth)

{
    OutExpression = FBPSemanticExpression();
    // グラフの循環と異常に深い式ツリーを、再帰呼び出し前に検出する。
    if (Depth >= 32 || ActiveNodes.Contains(CallNodeId))
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Pure expression cycle or depth limit"));
        return false;
    }
    ActiveNodes.Add(CallNodeId);
    struct FActiveNodeGuard
    {
        TSet<FGuid>& Nodes;
        FGuid NodeId;
        ~FActiveNodeGuard() { Nodes.Remove(NodeId); }
    } Guard{ActiveNodes, CallNodeId};
    if (Function == nullptr || GraphIR.Diagnostics.IsEmpty() == false ||
        Function->HasAnyFunctionFlags(FUNC_Native) == false ||
        Function->HasAnyFunctionFlags(FUNC_BlueprintPure) == false ||
        Function->HasAnyFunctionFlags(FUNC_Static | FUNC_BlueprintEvent) ||
        Function->GetOwnerClass() == nullptr)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Unsupported reflected expression function"));
        return false;
    }
    const FBPIRNode* Call = nullptr;
    const FBPIRGraph* Graph = nullptr;
    for (const FBPIRGraph& CandidateGraph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Candidate : CandidateGraph.Nodes)
        {
            if (Candidate.Id == CallNodeId)
            {
                if (Call != nullptr)
                {
                    Report(OutDiagnostics, CallNodeId, TEXT("Duplicate expression node GUID"));
                    return false;
                }
                Call = &Candidate;
                Graph = &CandidateGraph;
            }
        }
    }
    if (Call == nullptr || Graph == nullptr)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Expression node not found"));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> Messages;
    if (FBPFunctionCallValidator::Validate(*Call, Function, Signature, Messages) == false)
    {
        for (const FString& Message : Messages)
        {
            Report(OutDiagnostics, CallNodeId, Message);
        }
        return false;
    }
    if (Call->bFunctionPure == false ||
        Call->FunctionOwnerPath != Function->GetOwnerClass()->GetPathName() ||
        Signature.bStatic ||
        Signature.ReturnType.Kind == EBPSemanticTypeKind::Void ||
        Signature.ParameterNames.Num() != Signature.ParameterTypes.Num() ||
        Signature.ParameterNames.Num() != Signature.OutputParameters.Num())
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Unsupported pure value call signature"));
        return false;
    }
    const FBPIRPin* ReturnPin = Call->Pins.FindByPredicate(
        [](const FBPIRPin& Pin)
        {
            return Pin.Name == TEXT("ReturnValue") && Pin.Direction == EGPD_Output;
        });
    if (ReturnPin == nullptr)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("ReturnValue pin missing"));
        return false;
    }
    for (const FBPIRLink& Edge : Graph->ExecEdges)
    {
        if (Edge.SourceNode == CallNodeId || Edge.TargetNode == CallNodeId)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Pure expression has exec edges"));
            return false;
        }
    }
    for (const FBPIRLink& Edge : Graph->DataEdges)
    {
        if (Edge.TargetNode == CallNodeId &&
            Call->Pins.ContainsByPredicate([&Edge](const FBPIRPin& Pin)
            {
                return Pin.Id == Edge.TargetPin &&
                    ((Pin.Name == TEXT("self") || Pin.Name == TEXT("Target")) ||
                        Signature.ParameterNames.Contains(Pin.Name));
            }) == false)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unsupported expression input edge"));
            return false;
        }
        if (Edge.SourceNode == CallNodeId && Edge.SourcePin != ReturnPin->Id)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unexpected expression output edge"));
            return false;
        }
    }

    FBPSemanticExpression Target;
    if (FBPCallTargetResolver::Resolve(GraphIR, CallNodeId, Target, OutDiagnostics) == false)
    {
        return false;
    }
    // 引数の式ツリーを型付きで保持する。接続された純粋関数は循環を検出して再帰的に解決する。
    TArray<FBPSemanticExpression> Parameters;
    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index] ||
            Signature.ParameterTypes[Index].ContainerKind != TEXT("none") ||
            Signature.ParameterTypes[Index].bIsReference)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unsupported pure function parameter"));
            return false;
        }
        const FBPIRPin* Pin = Call->Pins.FindByPredicate(
            [Name = Signature.ParameterNames[Index]](const FBPIRPin& Candidate)
            {
                return Candidate.Name == Name && Candidate.Direction == EGPD_Input;
            });
        if (Pin == nullptr)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Pure function input pin missing"));
            return false;
        }
        const FBPIRLink* InputLink = nullptr;
        for (const FBPIRLink& Edge : Graph->DataEdges)
        {
            if (Edge.TargetNode == CallNodeId && Edge.TargetPin == Pin->Id)
            {
                if (InputLink != nullptr)
                {
                    Report(OutDiagnostics, CallNodeId, TEXT("Ambiguous pure function input"));
                    return false;
                }
                InputLink = &Edge;
            }
        }
        FBPSemanticExpression Argument;
        if (InputLink == nullptr)
        {
            Argument.Kind = EBPSemanticExpressionKind::Literal;
            Argument.Type = Signature.ParameterTypes[Index];
            Argument.LiteralValue = Pin->DefaultValue;
            Argument.SourceNodeId = CallNodeId;
            Argument.SourcePinId = Pin->Id;
        }
        else
        {
            const FBPIRNode* SourceNode = nullptr;
            const FBPIRPin* SourcePin = nullptr;
            TArray<FString> LinkMessages;
            if (FBPDataLinkResolver::ResolveInput(*Graph, CallNodeId, Pin->Id,
                SourceNode, SourcePin, LinkMessages) == false ||
                SourceNode == nullptr || SourcePin == nullptr ||
                SourcePin->Name != TEXT("ReturnValue"))
            {
                Report(OutDiagnostics, CallNodeId, TEXT("Invalid connected pure function argument"));
                return false;
            }
            const UClass* SourceOwner = FindObject<UClass>(
                nullptr, *SourceNode->FunctionOwnerPath);
            const UFunction* SourceFunction = SourceOwner != nullptr
                ? SourceOwner->FindFunctionByName(SourceNode->FunctionName) : nullptr;
            if (SourceNode->Id == CallNodeId ||
                LowerRecursive(
                    GraphIR, SourceNode->Id, SourceFunction,
                    Argument, OutDiagnostics, ActiveNodes, Depth + 1) == false)
            {
                Report(OutDiagnostics, CallNodeId, TEXT("Unsupported nested pure function"));
                return false;
            }
            const FBPSemanticType& Expected = Signature.ParameterTypes[Index];
            if (Argument.SourcePinId != SourcePin->Id ||
                Argument.Type.Kind != Expected.Kind ||
                Argument.Type.TypePath != Expected.TypePath ||
                Argument.Type.ContainerKind != Expected.ContainerKind ||
                Argument.Type.bIsReference != Expected.bIsReference)
            {
                Report(OutDiagnostics, CallNodeId, TEXT("Nested pure function type mismatch"));
                return false;
            }
        }
        Parameters.Add(MoveTemp(Argument));
    }
    FBPSemanticExpression Result;
    Result.Kind = EBPSemanticExpressionKind::FunctionCall;
    Result.Type = Signature.ReturnType;
    Result.SourceNodeId = CallNodeId;
    Result.SourcePinId = ReturnPin->Id;
    Result.FunctionName = Signature.FunctionName;
    Result.FunctionOwnerPath = Signature.OwnerClassPath;
    Result.Arguments.Add(MoveTemp(Target));
    Result.Arguments.Append(MoveTemp(Parameters));
    OutExpression = MoveTemp(Result);
    return true;
}

}

bool FBPGenericFunctionExpressionLowering::Lower(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, const UFunction* Function,
    FBPSemanticExpression& OutExpression,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    TSet<FGuid> ActiveNodes;
    return LowerRecursive(GraphIR, CallNodeId, Function,
        OutExpression, OutDiagnostics, ActiveNodes, 0);
}
