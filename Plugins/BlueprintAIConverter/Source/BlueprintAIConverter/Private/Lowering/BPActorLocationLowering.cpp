#include "BPActorLocationLowering.h"

#include "BPFunctionCallValidator.h"
#include "BPCallTargetResolver.h"
#include "BPParentClassResolver.h"
#include "GameFramework/Actor.h"

namespace
{
void Report(TArray<FBPSemanticDiagnostic>& Diagnostics, const FString& GraphName,
    const FGuid& NodeId, const FString& Message)
{
    FBPSemanticDiagnostic Diagnostic;
    Diagnostic.GraphName = GraphName;
    Diagnostic.NodeId = NodeId;
    Diagnostic.Message = Message;
    Diagnostics.Add(MoveTemp(Diagnostic));
}
}

bool FBPActorLocationLowering::LowerGetActorLocation(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, FBPSemanticExpression& OutExpression,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutExpression = FBPSemanticExpression();
    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        Report(OutDiagnostics, FString(), CallNodeId, TEXT("Graph IR contains diagnostics"));
        return false;
    }
    FBPResolvedParentClass ParentClass;
    TArray<FString> ParentDiagnostics;
    if (FBPParentClassResolver::ResolveActorParent(
        GraphIR.ParentClassPath, ParentClass, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            Report(OutDiagnostics, FString(), CallNodeId, Message);
        }
        return false;
    }

    const FBPIRNode* Target = nullptr;
    const FBPIRGraph* OwnerGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == CallNodeId)
            {
                if (Target != nullptr)
                {
                    Report(OutDiagnostics, Graph.Name, CallNodeId, TEXT("Duplicate node GUID"));
                    return false;
                }
                Target = &Node;
                OwnerGraph = &Graph;
            }
        }
    }
    if (Target == nullptr || OwnerGraph == nullptr)
    {
        Report(OutDiagnostics, FString(), CallNodeId, TEXT("Call node was not found"));
        return false;
    }

    const UFunction* Function = AActor::StaticClass()->FindFunctionByName(TEXT("K2_GetActorLocation"));
    if (Function == nullptr)
    {
        Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, TEXT("GetActorLocation UFunction not found"));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> Messages;
    if (FBPFunctionCallValidator::Validate(*Target, Function, Signature, Messages) == false)
    {
        for (const FString& Message : Messages)
        {
            Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, Message);
        }
        return false;
    }
    if (Signature.bStatic || Signature.ParameterNames.IsEmpty() == false ||
        Signature.ReturnType.Kind != EBPSemanticTypeKind::Struct ||
        Signature.ReturnType.TypePath != TEXT("/Script/CoreUObject.Vector"))
    {
        Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, TEXT("Unexpected GetActorLocation signature"));
        return false;
    }

    // self以外の入力や制御フロー接続は今回の純粋式Loweringの対象外。
    // 戻り値の接続は式を使用する側が後続の段階で解決するため保持する。
    for (const FBPIRLink& Link : OwnerGraph->ExecEdges)
    {
        if (Link.SourceNode == CallNodeId || Link.TargetNode == CallNodeId)
        {
            Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, TEXT("Pure call cannot have exec edges"));
            return false;
        }
    }
    for (const FBPIRLink& Link : OwnerGraph->DataEdges)
    {
        if (Link.TargetNode == CallNodeId &&
            Target->Pins.ContainsByPredicate([&Link](const FBPIRPin& Pin)
            {
                return Pin.Id == Link.TargetPin && Pin.Name != TEXT("self") && Pin.Name != TEXT("Target");
            }))
        {
            Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, TEXT("Unsupported connected call input"));
            return false;
        }
    }

    for (const FBPIRPin& Pin : Target->Pins)
    {
        if (Pin.Name == TEXT("self") && Pin.Direction != EGPD_Input)
        {
            Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, TEXT("Invalid self pin direction"));
            return false;
        }
        if (Pin.Name == TEXT("ReturnValue") && Pin.Direction == EGPD_Output)
        {
            FBPSemanticExpression Result;
            Result.Kind = EBPSemanticExpressionKind::FunctionCall;
            Result.Type = Signature.ReturnType;
            Result.SourceNodeId = CallNodeId;
            Result.SourcePinId = Pin.Id;
            Result.FunctionName = Signature.FunctionName;
            Result.FunctionOwnerPath = Signature.OwnerClassPath;

            // 接続されたTargetを型付き式として保持し、後段のEmitterで検証する。
            FBPSemanticExpression Self;
            if (FBPCallTargetResolver::Resolve(GraphIR, CallNodeId,
                Self, OutDiagnostics) == false)
            {
                return false;
            }
            Result.Arguments.Add(MoveTemp(Self));
            OutExpression = MoveTemp(Result);
            return true;
        }
    }
    Report(OutDiagnostics, OwnerGraph->Name, CallNodeId, TEXT("ReturnValue pin is missing"));
    return false;
}
