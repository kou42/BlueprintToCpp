#include "BPSemanticLowering.h"
#include "BPParentClassResolver.h"
#include "BPPrintStringAnalyzer.h"
#include "BPSemanticVariableExtractor.h"

#include "Engine/Blueprint.h"

namespace
{
FBPSemanticExpression Literal(EBPSemanticTypeKind Type, const FString& Value, const FBPIRNode& Node, FName PinName)
{
    FBPSemanticExpression Result;
    Result.Kind = EBPSemanticExpressionKind::Literal;
    Result.Type.Kind = Type;
    Result.LiteralValue = Value;
    Result.SourceNodeId = Node.Id;
    for (const FBPIRPin& Pin : Node.Pins)
    {
        if (Pin.Name == PinName && Pin.Direction == EGPD_Input)
        {
            Result.SourcePinId = Pin.Id;
            break;
        }
    }
    return Result;
}

void AddDiagnostic(FBPSemanticIR& IR, const FString& GraphName, const FGuid& NodeId, const FString& Message)
{
    FBPSemanticDiagnostic Diagnostic;
    Diagnostic.GraphName = GraphName;
    Diagnostic.NodeId = NodeId;
    Diagnostic.Message = Message;
    IR.Diagnostics.Add(MoveTemp(Diagnostic));
}

bool LowerPrintStringImpl(const FBPGraphIR& GraphIR, const bool bAllowBlueprintVariables,
    FBPSemanticIR& OutIR)
{
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;

    FBPResolvedParentClass ParentClass;
    TArray<FString> ParentDiagnostics;
    if (FBPParentClassResolver::ResolveActorParent(
        GraphIR.ParentClassPath, ParentClass, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }
    if (GraphIR.BlueprintVariableCount != 0 && bAllowBlueprintVariables == false)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("MVP does not yet support Blueprint variables without a source Blueprint"));
        return false;
    }

    // 空の標準Construction Scriptのみを除外する。実行可能ノードは必ず拒否する。
    FBPGraphIR Filtered = GraphIR;
    Filtered.Graphs.Reset();
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        if (Graph.Kind == TEXT("function") && Graph.Name == TEXT("UserConstructionScript"))
        {
            bool bExecutable = false;
            for (const FBPIRNode& Node : Graph.Nodes)
            {
                if (Node.FunctionName.IsNone() == false || Node.VariableName.IsNone() == false)
                {
                    bExecutable = true;
                    break;
                }
            }
            if (Graph.ExecEdges.IsEmpty() && Graph.DataEdges.IsEmpty() && bExecutable == false)
            {
                continue;
            }
        }
        Filtered.Graphs.Add(Graph);
    }
    if (Filtered.Graphs.Num() != 1 || Filtered.Graphs[0].Kind != TEXT("event"))
    {
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("MVP requires exactly one event graph"));
        return false;
    }

    FBPPrintStringCall Call;
    TArray<FString> Diagnostics;
    if (FBPPrintStringAnalyzer::Analyze(Filtered, Call, Diagnostics) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddDiagnostic(OutIR, Filtered.Graphs[0].Name, FGuid(), Message);
        }
        return false;
    }

    const FBPIRGraph& Graph = Filtered.Graphs[0];
    const FBPIRNode* Event = nullptr;
    const FBPIRNode* Print = nullptr;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") &&
            Node.FunctionName == TEXT("ReceiveBeginPlay"))
        {
            Event = &Node;
        }
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("PrintString"))
        {
            Print = &Node;
        }
    }
    if (Event == nullptr || Print == nullptr)
    {
        AddDiagnostic(OutIR, Graph.Name, FGuid(), TEXT("Validated graph is missing BeginPlay or PrintString"));
        return false;
    }

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = Event->Id;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.SourceNodeId = Print->Id;
    FBPSemanticExpression& CallExpression = Statement.Expression;
    CallExpression.Kind = EBPSemanticExpressionKind::FunctionCall;
    CallExpression.Type.Kind = EBPSemanticTypeKind::Void;
    CallExpression.SourceNodeId = Print->Id;
    CallExpression.FunctionName = TEXT("PrintString");
    CallExpression.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");

    // C++関数の引数順序を固定し、WorldContextObjectはthisとして保持する。
    FBPSemanticExpression Context;
    Context.Kind = EBPSemanticExpressionKind::VariableReference;
    Context.Type.Kind = EBPSemanticTypeKind::Object;
    Context.Type.TypePath = GraphIR.ParentClassPath;
    Context.VariableName = TEXT("this");
    Context.SourceNodeId = Print->Id;
    CallExpression.Arguments.Add(MoveTemp(Context));
    CallExpression.Arguments.Add(Literal(EBPSemanticTypeKind::String, Call.InString, *Print, TEXT("InString")));
    CallExpression.Arguments.Add(Literal(EBPSemanticTypeKind::Bool,
        Call.bPrintToScreen ? TEXT("true") : TEXT("false"), *Print, TEXT("bPrintToScreen")));
    CallExpression.Arguments.Add(Literal(EBPSemanticTypeKind::Bool,
        Call.bPrintToLog ? TEXT("true") : TEXT("false"), *Print, TEXT("bPrintToLog")));
    FBPSemanticExpression Color = Literal(EBPSemanticTypeKind::Struct,
        Call.TextColor.ToString(), *Print, TEXT("TextColor"));
    Color.Type.TypePath = TEXT("/Script/CoreUObject.LinearColor");
    CallExpression.Arguments.Add(MoveTemp(Color));
    CallExpression.Arguments.Add(Literal(EBPSemanticTypeKind::Float,
        FString::SanitizeFloat(Call.Duration), *Print, TEXT("Duration")));
    CallExpression.Arguments.Add(Literal(EBPSemanticTypeKind::String, Call.Key, *Print, TEXT("Key")));
    Block.Statements.Add(MoveTemp(Statement));
    Function.Blocks.Add(MoveTemp(Block));
    OutIR.Functions.Add(MoveTemp(Function));
    return true;
}
}

bool FBPSemanticLowering::LowerPrintString(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    return LowerPrintStringImpl(GraphIR, false, OutIR);
}

bool FBPSemanticLowering::LowerPrintString(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    if (Blueprint == nullptr)
    {
        OutIR = FBPSemanticIR();
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("Blueprint is null"));
        return false;
    }
    if (Blueprint->NewVariables.Num() != GraphIR.BlueprintVariableCount)
    {
        OutIR = FBPSemanticIR();
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("Blueprint variables changed after Graph IR was built"));
        return false;
    }
    if (LowerPrintStringImpl(GraphIR, true, OutIR) == false)
    {
        return false;
    }
    FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
    return OutIR.HasErrors() == false;
}
