#include "BPConnectedFunctionArguments.h"

#include "BPConnectedActorLocationLowering.h"
#include "BPFunctionArgumentEmitter.h"
#include "BPFunctionCallValidator.h"
#include "BPBooleanPinDefaultEmitter.h"

bool FBPConnectedFunctionArguments::Emit(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, const UFunction* Function, FString& OutArguments,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutArguments.Reset();
    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Graph IR contains diagnostics");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // GetActorLocationをthisに変換するため、直接Actor派生のみを受理する。
    if (GraphIR.ParentClassPath != TEXT("/Script/Engine.Actor"))
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Only direct Actor parent is supported");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    const FBPIRNode* Call = nullptr;
    FString GraphName;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == CallNodeId)
            {
                if (Call != nullptr)
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.NodeId = CallNodeId;
                    Diagnostic.Message = TEXT("Ambiguous call node GUID");
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
                Call = &Node;
                GraphName = Graph.Name;
            }
        }
    }
    if (Call == nullptr)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Call node not found");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> Messages;
    if (FBPFunctionCallValidator::Validate(*Call, Function, Signature, Messages) == false)
    {
        for (const FString& Message : Messages)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = GraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = Message;
            OutDiagnostics.Add(MoveTemp(Diagnostic));
        }
        return false;
    }

    TArray<FBPSemanticExpression> Arguments;
    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index])
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = GraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = TEXT("Output arguments are not supported");
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }

        const FBPIRPin* Input = nullptr;
        for (const FBPIRPin& Pin : Call->Pins)
        {
            if (Pin.Name == Signature.ParameterNames[Index] && Pin.Direction == EGPD_Input)
            {
                Input = &Pin;
                break;
            }
        }
        if (Input == nullptr)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = GraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = TEXT("Parameter input pin is missing");
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }

        FBPSemanticExpression Expression;
        if (Signature.ParameterTypes[Index].Kind == EBPSemanticTypeKind::Bool)
        {
            // 接続済み入力を既定値に置き換えると意味が変わるため、必ず拒否する。
            bool bConnected = false;
            for (const FBPIRGraph& Graph : GraphIR.Graphs)
            {
                for (const FBPIRLink& Link : Graph.DataEdges)
                {
                    if (Link.TargetNode == CallNodeId && Link.TargetPin == Input->Id)
                    {
                        bConnected = true;
                    }
                }
            }
            FString Literal;
            if (bConnected || FBPBooleanPinDefaultEmitter::Emit(*Input, Literal) == false)
            {
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.GraphName = GraphName;
                Diagnostic.NodeId = CallNodeId;
                Diagnostic.PinId = Input->Id;
                Diagnostic.Message = TEXT("Unsupported connected or invalid bool default input");
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
            Expression.Kind = EBPSemanticExpressionKind::Literal;
            Expression.Type = Signature.ParameterTypes[Index];
            Expression.LiteralValue = MoveTemp(Literal);
            Expression.SourceNodeId = CallNodeId;
            Expression.SourcePinId = Input->Id;
        }
        else if (FBPConnectedActorLocationLowering::LowerInput(GraphIR, CallNodeId,
            Input->Id, Expression, OutDiagnostics) == false)
        {
            return false;
        }
        Arguments.Add(MoveTemp(Expression));
    }

    FString Candidate;
    if (FBPFunctionArgumentEmitter::Emit(Signature, Arguments, Candidate, Messages) == false)
    {
        for (const FString& Message : Messages)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = GraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = Message;
            OutDiagnostics.Add(MoveTemp(Diagnostic));
        }
        return false;
    }
    OutArguments = MoveTemp(Candidate);
    return true;
}
