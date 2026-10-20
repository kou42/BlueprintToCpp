#include "BPSemanticLowering.h"
#include "BPParentClassResolver.h"
#include "BPPrintStringAnalyzer.h"
#include "BPBooleanPinDefaultEmitter.h"
#include "BPConnectedActorLocationLowering.h"
#include "BPConnectedFunctionCallEmitter.h"
#include "BPFunctionSignatureResolver.h"
#include "BPNumericArithmeticResolver.h"
#include "BPNumericComparisonResolver.h"
#include "BPOutputParameterStorage.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticVariableExtractor.h"

#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

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
    if (FBPParentClassResolver::ResolveParent(
        GraphIR.ParentClassPath, ParentClass, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }
    if (ParentClass.bSupportsBeginPlay == false)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), FString::Printf(
            TEXT("Parent class does not provide an overridable BeginPlay lifecycle: %s"),
            *ParentClass.ClassPath));
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

const FBPIRPin* FindPin(const FBPIRNode& Node, const FName Name,
    const EEdGraphPinDirection Direction)
{
    return Node.Pins.FindByPredicate(
        [Name, Direction](const FBPIRPin& Pin)
        {
            return Pin.Name == Name && Pin.Direction == Direction;
        });
}

FBPSemanticExpression MakePrintStringExpression(const FBPPrintStringCall& Values,
    const FBPIRNode& Print, const FString& ParentClassPath)
{
    FBPSemanticExpression Call;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Void;
    Call.SourceNodeId = Print.Id;
    Call.FunctionName = TEXT("PrintString");
    Call.FunctionOwnerPath = TEXT("/Script/Engine.KismetSystemLibrary");

    FBPSemanticExpression Context;
    Context.Kind = EBPSemanticExpressionKind::VariableReference;
    Context.Type.Kind = EBPSemanticTypeKind::Object;
    Context.Type.TypePath = ParentClassPath;
    Context.VariableName = TEXT("this");
    Context.SourceNodeId = Print.Id;
    Call.Arguments.Add(MoveTemp(Context));
    Call.Arguments.Add(Literal(EBPSemanticTypeKind::String,
        Values.InString, Print, TEXT("InString")));
    Call.Arguments.Add(Literal(EBPSemanticTypeKind::Bool,
        Values.bPrintToScreen ? TEXT("true") : TEXT("false"), Print, TEXT("bPrintToScreen")));
    Call.Arguments.Add(Literal(EBPSemanticTypeKind::Bool,
        Values.bPrintToLog ? TEXT("true") : TEXT("false"), Print, TEXT("bPrintToLog")));
    FBPSemanticExpression Color = Literal(EBPSemanticTypeKind::Struct,
        Values.TextColor.ToString(), Print, TEXT("TextColor"));
    Color.Type.TypePath = TEXT("/Script/CoreUObject.LinearColor");
    Call.Arguments.Add(MoveTemp(Color));
    Call.Arguments.Add(Literal(EBPSemanticTypeKind::Float,
        FString::SanitizeFloat(Values.Duration), Print, TEXT("Duration")));
    Call.Arguments.Add(Literal(EBPSemanticTypeKind::String,
        Values.Key, Print, TEXT("Key")));
    return Call;
}

bool AnalyzeBranchPrint(const FBPGraphIR& GraphIR, const FBPIRGraph& Graph,
    const FBPIRNode& BeginPlay, const FBPIRNode& Print, FBPPrintStringCall& OutCall,
    FBPSemanticIR& OutIR)
{
    FBPGraphIR SingleCallIR;
    SingleCallIR.AssetPath = GraphIR.AssetPath;
    SingleCallIR.ParentClassPath = GraphIR.ParentClassPath;
    FBPIRGraph SingleGraph;
    SingleGraph.Name = Graph.Name;
    SingleGraph.Path = Graph.Path;
    SingleGraph.Kind = Graph.Kind;
    SingleGraph.Nodes.Add(BeginPlay);
    SingleGraph.Nodes.Add(Print);
    FBPIRLink Edge;
    Edge.SourceNode = BeginPlay.Id;
    Edge.TargetNode = Print.Id;
    SingleGraph.ExecEdges.Add(Edge);
    SingleCallIR.Graphs.Add(MoveTemp(SingleGraph));
    TArray<FString> Diagnostics;
    if (FBPPrintStringAnalyzer::Analyze(SingleCallIR, OutCall, Diagnostics) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddDiagnostic(OutIR, Graph.Name, Print.Id, Message);
        }
        return false;
    }
    return true;
}

// BeginPlay専用Analyzerで検証済みのPrintString既定値規則を再利用しつつ、
// Entryの実際の関数種別と名前は後段のSemantic Functionへ保持する。
bool AnalyzeEntryPrint(const FBPGraphIR& GraphIR, const FBPIRGraph& Graph,
    const FBPIRNode& Entry, const FBPIRNode& Print, FBPPrintStringCall& OutCall,
    FBPSemanticIR& OutIR)
{
    FBPIRNode SyntheticBeginPlay = Entry;
    SyntheticBeginPlay.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    SyntheticBeginPlay.FunctionName = TEXT("ReceiveBeginPlay");
    FBPIRGraph SyntheticEventGraph = Graph;
    SyntheticEventGraph.Kind = TEXT("event");
    return AnalyzeBranchPrint(
        GraphIR, SyntheticEventGraph, SyntheticBeginPlay, Print, OutCall, OutIR);
}

bool LowerSingleEntryPrintStringImpl(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("Function and custom-event conversion requires a compiled source Blueprint"));
        return false;
    }
    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        for (const FString& Message : GraphIR.Diagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }

    FBPResolvedParentClass Parent;
    TArray<FString> ParentDiagnostics;
    if (FBPParentClassResolver::ResolveParent(
        GraphIR.ParentClassPath, Parent, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }

    const FBPIRGraph* TargetGraph = nullptr;
    const FBPIRNode* Entry = nullptr;
    const FBPIRNode* Print = nullptr;
    const FBPIRNode* Result = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        const FBPIRNode* CandidateEntry = nullptr;
        const FBPIRNode* CandidatePrint = nullptr;
        const FBPIRNode* CandidateResult = nullptr;
        bool bUnsupportedNode = false;
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") ||
                Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CustomEvent") ||
                Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_FunctionEntry"))
            {
                if (CandidateEntry != nullptr)
                {
                    bUnsupportedNode = true;
                    break;
                }
                CandidateEntry = &Node;
            }
            else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
                Node.FunctionName == TEXT("PrintString") &&
                Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetSystemLibrary"))
            {
                if (CandidatePrint != nullptr)
                {
                    bUnsupportedNode = true;
                    break;
                }
                CandidatePrint = &Node;
            }
            else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_FunctionResult"))
            {
                if (CandidateResult != nullptr)
                {
                    bUnsupportedNode = true;
                    break;
                }
                CandidateResult = &Node;
            }
            else
            {
                bUnsupportedNode = true;
                break;
            }
        }
        if (bUnsupportedNode || CandidateEntry == nullptr || CandidatePrint == nullptr)
        {
            continue;
        }
        const bool bFunctionGraph = Graph.Kind == TEXT("function") &&
            CandidateEntry->ClassPath == TEXT("/Script/BlueprintGraph.K2Node_FunctionEntry");
        const bool bEventGraph = Graph.Kind == TEXT("event") &&
            (CandidateEntry->ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") ||
                CandidateEntry->ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CustomEvent")) &&
            CandidateEntry->FunctionName != TEXT("ReceiveBeginPlay");
        if ((bFunctionGraph == false && bEventGraph == false) ||
            (bEventGraph && CandidateResult != nullptr))
        {
            continue;
        }
        if (TargetGraph != nullptr)
        {
            AddDiagnostic(OutIR, Graph.Name, CandidateEntry->Id,
                TEXT("Multiple function or custom-event PrintString entry graphs are ambiguous"));
            return false;
        }
        TargetGraph = &Graph;
        Entry = CandidateEntry;
        Print = CandidatePrint;
        Result = CandidateResult;
    }
    if (TargetGraph == nullptr || Entry == nullptr || Print == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("No supported Function Entry or Custom Event -> PrintString graph was found"));
        return false;
    }
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        if (&Graph != TargetGraph &&
            (Graph.ExecEdges.IsEmpty() == false || Graph.DataEdges.IsEmpty() == false))
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(),
                TEXT("Additional executable graphs must be converted together and cannot be omitted"));
            return false;
        }
    }

    const bool bEntryToPrint = TargetGraph->ExecEdges.ContainsByPredicate(
        [Entry, Print](const FBPIRLink& Edge)
        {
            return Edge.SourceNode == Entry->Id && Edge.TargetNode == Print->Id;
        });
    const bool bPrintToResult = Result == nullptr || TargetGraph->ExecEdges.ContainsByPredicate(
        [Print, Result](const FBPIRLink& Edge)
        {
            return Edge.SourceNode == Print->Id && Edge.TargetNode == Result->Id;
        });
    const int32 ExpectedExecEdges = Result != nullptr ? 2 : 1;
    if (bEntryToPrint == false || bPrintToResult == false ||
        TargetGraph->ExecEdges.Num() != ExpectedExecEdges ||
        TargetGraph->DataEdges.IsEmpty() == false)
    {
        AddDiagnostic(OutIR, TargetGraph->Name, Entry->Id,
            TEXT("Function or custom-event conversion requires an unambiguous Entry -> PrintString flow"));
        return false;
    }

    FBPPrintStringCall Values;
    if (AnalyzeEntryPrint(
        GraphIR, *TargetGraph, *Entry, *Print, Values, OutIR) == false)
    {
        return false;
    }

    const FName FunctionName = TargetGraph->Kind == TEXT("function")
        ? FName(*TargetGraph->Name) : Entry->FunctionName;
    FBPSemanticIR Reflected;
    FBPSemanticFunctionExtractor::Append(Blueprint, Reflected);
    const FBPSemanticFunction* ReflectedFunction = Reflected.Functions.FindByPredicate(
        [FunctionName](const FBPSemanticFunction& Function)
        {
            return Function.Name == FunctionName;
        });
    if (FunctionName.IsNone() || ReflectedFunction == nullptr)
    {
        AddDiagnostic(OutIR, TargetGraph->Name, Entry->Id,
            TEXT("Function or custom-event signature could not be verified by generated-class reflection"));
        return false;
    }
    for (const FBPSemanticDiagnostic& Diagnostic : Reflected.Diagnostics)
    {
        if (Diagnostic.GraphName == FunctionName.ToString())
        {
            OutIR.Diagnostics.Add(Diagnostic);
        }
    }
    if (OutIR.HasErrors())
    {
        return false;
    }

    FBPSemanticFunction Function = *ReflectedFunction;
    Function.bIsEventGraph = TargetGraph->Kind == TEXT("event");
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = Entry->Id;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.SourceNodeId = Print->Id;
    Statement.Expression = MakePrintStringExpression(
        Values, *Print, GraphIR.ParentClassPath);
    Block.Statements.Add(MoveTemp(Statement));
    Function.Blocks.Add(MoveTemp(Block));
    OutIR.Functions.Add(MoveTemp(Function));
    FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
    return OutIR.HasErrors() == false;
}

const FBPSemanticVariable* FindSemanticVariable(
    const TArray<FBPSemanticVariable>& Variables, const FName Name)
{
    const FBPSemanticVariable* Match = nullptr;
    for (const FBPSemanticVariable& Variable : Variables)
    {
        if (Variable.Name != Name)
        {
            continue;
        }
        if (Match != nullptr)
        {
            return nullptr;
        }
        Match = &Variable;
    }
    return Match;
}

bool BuildNumericExpression(const FBPIRGraph& Graph,
    const FBPIRNode& Consumer, const FBPIRPin& OperandPin,
    const EBPSemanticTypeKind OperandType,
    const TArray<const FBPIRNode*>& VariableGets,
    const TArray<const FBPIRNode*>& ArithmeticNodes,
    const TArray<FBPSemanticVariable>& Variables, TSet<int32>& UsedDataEdges,
    TSet<FGuid>& ActiveArithmeticNodes, FBPSemanticExpression& OutExpression,
    FBPSemanticIR& OutIR)
{
    int32 IncomingEdgeIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Graph.DataEdges.Num(); ++Index)
    {
        const FBPIRLink& Edge = Graph.DataEdges[Index];
        if (Edge.TargetNode != Consumer.Id || Edge.TargetPin != OperandPin.Id)
        {
            continue;
        }
        if (IncomingEdgeIndex != INDEX_NONE)
        {
            AddDiagnostic(OutIR, Graph.Name, Consumer.Id,
                TEXT("Numeric expression operand has multiple producers"));
            return false;
        }
        IncomingEdgeIndex = Index;
    }

    if (IncomingEdgeIndex == INDEX_NONE)
    {
        FString CanonicalValue;
        if (FBPNumericComparisonResolver::CanonicalizeLiteral(
            OperandType, OperandPin.DefaultValue, CanonicalValue) == false)
        {
            AddDiagnostic(OutIR, Graph.Name, Consumer.Id,
                TEXT("Numeric expression requires finite literals of the reflected operand type"));
            return false;
        }
        OutExpression = Literal(OperandType, CanonicalValue,
            Consumer, OperandPin.Name);
        return true;
    }

    const FBPIRLink& Edge = Graph.DataEdges[IncomingEdgeIndex];
    const FBPIRNode* const* VariableMatch = VariableGets.FindByPredicate(
        [&Edge](const FBPIRNode* Node)
        {
            return Node != nullptr && Node->Id == Edge.SourceNode;
        });
    const FBPIRNode* VariableGet = VariableMatch != nullptr ? *VariableMatch : nullptr;
    if (VariableGet == nullptr)
    {
        const FBPIRNode* const* ArithmeticMatch = ArithmeticNodes.FindByPredicate(
            [&Edge](const FBPIRNode* Node)
            {
                return Node != nullptr && Node->Id == Edge.SourceNode;
            });
        const FBPIRNode* Arithmetic = ArithmeticMatch != nullptr
            ? *ArithmeticMatch : nullptr;
        if (Arithmetic == nullptr)
        {
            AddDiagnostic(OutIR, Graph.Name, Edge.SourceNode,
                TEXT("Numeric expression producer is unsupported"));
            return false;
        }
        if (ActiveArithmeticNodes.Contains(Arithmetic->Id))
        {
            AddDiagnostic(OutIR, Graph.Name, Arithmetic->Id,
                TEXT("Numeric arithmetic dependency contains a cycle"));
            return false;
        }

        FBPNumericArithmeticOperation Operation;
        TArray<FString> ResolverDiagnostics;
        if (FBPNumericArithmeticResolver::Resolve(Arithmetic->FunctionOwnerPath,
            Arithmetic->FunctionName, Operation, ResolverDiagnostics) == false ||
            Operation.OperandType != OperandType)
        {
            for (const FString& Message : ResolverDiagnostics)
            {
                AddDiagnostic(OutIR, Graph.Name, Arithmetic->Id, Message);
            }
            if (ResolverDiagnostics.IsEmpty())
            {
                AddDiagnostic(OutIR, Graph.Name, Arithmetic->Id,
                    TEXT("Numeric arithmetic result type does not match its consumer"));
            }
            return false;
        }

        const FBPIRPin* A = FindPin(*Arithmetic, TEXT("A"), EGPD_Input);
        const FBPIRPin* B = FindPin(*Arithmetic, TEXT("B"), EGPD_Input);
        const FBPIRPin* ReturnValue = FindPin(
            *Arithmetic, TEXT("ReturnValue"), EGPD_Output);
        const bool bUnexpectedPin = Arithmetic->Pins.ContainsByPredicate(
            [A, B, ReturnValue, &Operation](const FBPIRPin& Pin)
            {
                if (&Pin == A || &Pin == B || &Pin == ReturnValue)
                {
                    return false;
                }
                // 関数ライブラリ由来の非表示selfピンだけを追加ピンとして許可する。
                return Pin.Name != UEdGraphSchema_K2::PN_Self ||
                    Pin.Direction != EGPD_Input ||
                    Pin.Category != UEdGraphSchema_K2::PC_Object ||
                    Pin.SubCategoryObjectPath != Operation.FunctionOwnerPath;
            });
        if (A == nullptr || B == nullptr || ReturnValue == nullptr ||
            Edge.SourcePin != ReturnValue->Id || bUnexpectedPin ||
            FBPNumericComparisonResolver::MatchesPinType(
                A->Category, A->SubCategory, OperandType) == false ||
            FBPNumericComparisonResolver::MatchesPinType(
                B->Category, B->SubCategory, OperandType) == false ||
            FBPNumericComparisonResolver::MatchesPinType(
                ReturnValue->Category, ReturnValue->SubCategory,
                OperandType) == false ||
            (A->DefaultObjectPath.IsEmpty() == false &&
                A->DefaultObjectPath != TEXT("None")) ||
            (B->DefaultObjectPath.IsEmpty() == false &&
                B->DefaultObjectPath != TEXT("None")) ||
            A->DefaultText.IsEmpty() == false || B->DefaultText.IsEmpty() == false)
        {
            AddDiagnostic(OutIR, Graph.Name, Arithmetic->Id,
                TEXT("Numeric arithmetic pins do not match the reflected binary signature"));
            return false;
        }

        int32 ConsumerCount = 0;
        for (const FBPIRLink& Candidate : Graph.DataEdges)
        {
            ConsumerCount += Candidate.SourceNode == Arithmetic->Id &&
                Candidate.SourcePin == ReturnValue->Id ? 1 : 0;
        }
        if (ConsumerCount != 1)
        {
            AddDiagnostic(OutIR, Graph.Name, Arithmetic->Id,
                TEXT("Numeric arithmetic result must have exactly one consumer"));
            return false;
        }

        UsedDataEdges.Add(IncomingEdgeIndex);
        ActiveArithmeticNodes.Add(Arithmetic->Id);
        FBPSemanticExpression AExpression;
        FBPSemanticExpression BExpression;
        const bool bBuilt = BuildNumericExpression(Graph, *Arithmetic, *A,
                OperandType, VariableGets, ArithmeticNodes, Variables,
                UsedDataEdges, ActiveArithmeticNodes, AExpression, OutIR) &&
            BuildNumericExpression(Graph, *Arithmetic, *B,
                OperandType, VariableGets, ArithmeticNodes, Variables,
                UsedDataEdges, ActiveArithmeticNodes, BExpression, OutIR);
        ActiveArithmeticNodes.Remove(Arithmetic->Id);
        if (bBuilt == false)
        {
            return false;
        }

        OutExpression = FBPSemanticExpression();
        OutExpression.Kind = EBPSemanticExpressionKind::FunctionCall;
        OutExpression.Type.Kind = OperandType;
        OutExpression.SourceNodeId = Arithmetic->Id;
        OutExpression.SourcePinId = ReturnValue->Id;
        OutExpression.FunctionName = Operation.FunctionName;
        OutExpression.FunctionOwnerPath = Operation.FunctionOwnerPath;
        OutExpression.Arguments.Add(MoveTemp(AExpression));
        OutExpression.Arguments.Add(MoveTemp(BExpression));
        return true;
    }
    if (VariableGet->VariableName.IsNone() ||
        VariableGet->bVariableSelfContext == false)
    {
        AddDiagnostic(OutIR, Graph.Name, Edge.SourceNode,
            TEXT("Numeric expression variable must be a self member Variable Get"));
        return false;
    }
    const FBPIRPin* ValuePin = VariableGet->Pins.FindByPredicate(
        [&Edge](const FBPIRPin& Pin)
        {
            return Pin.Id == Edge.SourcePin && Pin.Direction == EGPD_Output;
        });
    const bool bUnexpectedPin = VariableGet->Pins.ContainsByPredicate(
        [ValuePin](const FBPIRPin& Pin)
        {
            if (&Pin == ValuePin)
            {
                return false;
            }
            // selfメンバーGetに付随する非表示selfピン以外は、依存関係を曖昧にするため拒否する。
            return Pin.Name != UEdGraphSchema_K2::PN_Self ||
                Pin.Direction != EGPD_Input ||
                Pin.Category != UEdGraphSchema_K2::PC_Object;
        });
    if (ValuePin == nullptr || ValuePin->Name != VariableGet->VariableName ||
        bUnexpectedPin ||
        FBPNumericComparisonResolver::MatchesPinType(
            ValuePin->Category, ValuePin->SubCategory, OperandType) == false)
    {
        AddDiagnostic(OutIR, Graph.Name, VariableGet->Id,
            TEXT("Variable Get pin does not match the reflected comparison operand type"));
        return false;
    }
    int32 ConsumerCount = 0;
    for (const FBPIRLink& Candidate : Graph.DataEdges)
    {
        ConsumerCount += Candidate.SourceNode == VariableGet->Id &&
            Candidate.SourcePin == ValuePin->Id ? 1 : 0;
    }
    if (ConsumerCount != 1)
    {
        AddDiagnostic(OutIR, Graph.Name, VariableGet->Id,
            TEXT("Variable Get comparison operand must have exactly one consumer"));
        return false;
    }

    const FBPSemanticVariable* Variable = FindSemanticVariable(
        Variables, VariableGet->VariableName);
    const FString ExpectedCppType = OperandType == EBPSemanticTypeKind::Int32
        ? TEXT("int32")
        : OperandType == EBPSemanticTypeKind::Float
            ? TEXT("float")
            : OperandType == EBPSemanticTypeKind::Double
                ? TEXT("double") : FString();
    if (Variable == nullptr || Variable->Type.Kind != OperandType ||
        Variable->Type.bIsReference || Variable->Type.bIsConst ||
        Variable->ReflectedCppType != ExpectedCppType)
    {
        AddDiagnostic(OutIR, Graph.Name, VariableGet->Id,
            TEXT("Variable Get does not resolve to one exact reflected numeric member"));
        return false;
    }

    OutExpression = FBPSemanticExpression();
    OutExpression.Kind = EBPSemanticExpressionKind::VariableReference;
    OutExpression.Type = Variable->Type;
    OutExpression.SourceNodeId = VariableGet->Id;
    OutExpression.SourcePinId = ValuePin->Id;
    OutExpression.VariableName = Variable->Name;
    UsedDataEdges.Add(IncomingEdgeIndex);
    return true;
}

bool LowerNumericComparisonCondition(const FBPIRGraph& Graph, const FBPIRNode& Comparison,
    const FBPIRNode& Branch, const FBPIRPin& Condition,
    const TArray<const FBPIRNode*>& VariableGets,
    const TArray<const FBPIRNode*>& ArithmeticNodes,
    const TArray<FBPSemanticVariable>& Variables,
    FBPSemanticExpression& OutExpression, FBPSemanticIR& OutIR)
{
    FBPNumericComparison ResolvedComparison;
    TArray<FString> ResolverDiagnostics;
    if (FBPNumericComparisonResolver::Resolve(Comparison.FunctionOwnerPath,
        Comparison.FunctionName, ResolvedComparison, ResolverDiagnostics) == false)
    {
        for (const FString& Message : ResolverDiagnostics)
        {
            AddDiagnostic(OutIR, Graph.Name, Comparison.Id, Message);
        }
        return false;
    }

    const FBPIRPin* A = FindPin(Comparison, TEXT("A"), EGPD_Input);
    const FBPIRPin* B = FindPin(Comparison, TEXT("B"), EGPD_Input);
    const FBPIRPin* ReturnValue = FindPin(Comparison, TEXT("ReturnValue"), EGPD_Output);
    const bool bHasUnexpectedPin = Comparison.Pins.ContainsByPredicate(
        [A, B, ReturnValue, &ResolvedComparison](const FBPIRPin& Pin)
        {
            if (&Pin == A || &Pin == B || &Pin == ReturnValue)
            {
                return false;
            }
            // Blueprint Function Libraryの非表示selfピンだけは、所有型まで一致する場合に許可する。
            return Pin.Name != UEdGraphSchema_K2::PN_Self ||
                Pin.Direction != EGPD_Input ||
                Pin.Category != UEdGraphSchema_K2::PC_Object ||
                Pin.SubCategoryObjectPath != ResolvedComparison.FunctionOwnerPath;
        });
    if (A == nullptr || B == nullptr || ReturnValue == nullptr || bHasUnexpectedPin ||
        FBPNumericComparisonResolver::MatchesPinType(
            A->Category, A->SubCategory, ResolvedComparison.OperandType) == false ||
        FBPNumericComparisonResolver::MatchesPinType(
            B->Category, B->SubCategory, ResolvedComparison.OperandType) == false ||
        ReturnValue->Category != UEdGraphSchema_K2::PC_Boolean ||
        ReturnValue->SubCategory.IsNone() == false ||
        Condition.Category != UEdGraphSchema_K2::PC_Boolean ||
        Condition.SubCategory.IsNone() == false ||
        (A->DefaultObjectPath.IsEmpty() == false && A->DefaultObjectPath != TEXT("None")) ||
        (B->DefaultObjectPath.IsEmpty() == false && B->DefaultObjectPath != TEXT("None")) ||
        A->DefaultText.IsEmpty() == false || B->DefaultText.IsEmpty() == false)
    {
        AddDiagnostic(OutIR, Graph.Name, Comparison.Id,
            FString::Printf(
                TEXT("Numeric comparison pins are invalid: A=%s/%s B=%s/%s ReturnValue=%s/%s Condition=%s/%s"),
                A != nullptr ? *A->Category.ToString() : TEXT("missing"),
                A != nullptr ? *A->SubCategory.ToString() : TEXT("missing"),
                B != nullptr ? *B->Category.ToString() : TEXT("missing"),
                B != nullptr ? *B->SubCategory.ToString() : TEXT("missing"),
                ReturnValue != nullptr ? *ReturnValue->Category.ToString() : TEXT("missing"),
                ReturnValue != nullptr ? *ReturnValue->SubCategory.ToString() : TEXT("missing"),
                *Condition.Category.ToString(), *Condition.SubCategory.ToString()));
        return false;
    }
    int32 ResultEdgeIndex = INDEX_NONE;
    int32 ResultConsumerCount = 0;
    for (int32 Index = 0; Index < Graph.DataEdges.Num(); ++Index)
    {
        const FBPIRLink& Candidate = Graph.DataEdges[Index];
        if (Candidate.SourceNode == Comparison.Id &&
            Candidate.SourcePin == ReturnValue->Id)
        {
            ++ResultConsumerCount;
            ResultEdgeIndex = Index;
        }
    }
    if (ResultConsumerCount != 1)
    {
        AddDiagnostic(OutIR, Graph.Name, Comparison.Id,
            TEXT("Numeric comparison must have exactly one result consumer"));
        return false;
    }
    const FBPIRLink& ResultEdge = Graph.DataEdges[ResultEdgeIndex];
    if (ResultEdge.TargetNode != Branch.Id || ResultEdge.TargetPin != Condition.Id)
    {
        AddDiagnostic(OutIR, Graph.Name, Comparison.Id,
            TEXT("Numeric comparison result must connect directly to Branch Condition"));
        return false;
    }

    TSet<int32> UsedDataEdges;
    TSet<FGuid> ActiveArithmeticNodes;
    UsedDataEdges.Add(ResultEdgeIndex);
    FBPSemanticExpression AExpression;
    FBPSemanticExpression BExpression;
    if (BuildNumericExpression(Graph, Comparison, *A,
            ResolvedComparison.OperandType, VariableGets, ArithmeticNodes,
            Variables, UsedDataEdges, ActiveArithmeticNodes,
            AExpression, OutIR) == false ||
        BuildNumericExpression(Graph, Comparison, *B,
            ResolvedComparison.OperandType, VariableGets, ArithmeticNodes,
            Variables, UsedDataEdges, ActiveArithmeticNodes,
            BExpression, OutIR) == false ||
        UsedDataEdges.Num() != Graph.DataEdges.Num())
    {
        if (OutIR.HasErrors() == false)
        {
            AddDiagnostic(OutIR, Graph.Name, Comparison.Id,
                TEXT("Numeric comparison contains an unsupported data dependency"));
        }
        return false;
    }
    for (const FBPIRNode* VariableGet : VariableGets)
    {
        if (VariableGet == nullptr ||
            Graph.DataEdges.ContainsByPredicate(
                [VariableGet](const FBPIRLink& Edge)
                {
                    return Edge.SourceNode == VariableGet->Id;
                }) == false)
        {
            AddDiagnostic(OutIR, Graph.Name,
                VariableGet != nullptr ? VariableGet->Id : FGuid(),
                TEXT("Unconnected Variable Get is not supported in a comparison Branch"));
            return false;
        }
    }
    for (const FBPIRNode* Arithmetic : ArithmeticNodes)
    {
        bool bConnected = false;
        for (int32 EdgeIndex = 0; Arithmetic != nullptr &&
            EdgeIndex < Graph.DataEdges.Num(); ++EdgeIndex)
        {
            const FBPIRLink& Edge = Graph.DataEdges[EdgeIndex];
            bConnected |= Edge.SourceNode == Arithmetic->Id &&
                UsedDataEdges.Contains(EdgeIndex);
        }
        if (Arithmetic == nullptr || bConnected == false)
        {
            AddDiagnostic(OutIR, Graph.Name,
                Arithmetic != nullptr ? Arithmetic->Id : FGuid(),
                TEXT("Unconnected numeric arithmetic node is not supported in a comparison Branch"));
            return false;
        }
    }

    // 比較をbool値へ畳み込まず、型付きの式として保持してC++側でも一度だけ評価する。
    OutExpression = FBPSemanticExpression();
    OutExpression.Kind = EBPSemanticExpressionKind::FunctionCall;
    OutExpression.Type.Kind = EBPSemanticTypeKind::Bool;
    OutExpression.SourceNodeId = Comparison.Id;
    OutExpression.SourcePinId = ReturnValue->Id;
    OutExpression.FunctionName = ResolvedComparison.FunctionName;
    OutExpression.FunctionOwnerPath = ResolvedComparison.FunctionOwnerPath;
    OutExpression.Arguments.Add(MoveTemp(AExpression));
    OutExpression.Arguments.Add(MoveTemp(BExpression));
    return true;
}

bool LowerBranchImpl(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
    FBPSemanticIR& OutIR)
{
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("Branch conversion requires a compiled source Blueprint"));
        return false;
    }
    if (Blueprint->NewVariables.Num() != GraphIR.BlueprintVariableCount)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("Blueprint variables changed after Graph IR was built"));
        return false;
    }
    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        for (const FString& Message : GraphIR.Diagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }

    FBPResolvedParentClass ParentClass;
    TArray<FString> ParentDiagnostics;
    if (FBPParentClassResolver::ResolveParent(
        GraphIR.ParentClassPath, ParentClass, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }
    if (ParentClass.bSupportsBeginPlay == false)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), FString::Printf(
            TEXT("Parent class does not provide an overridable BeginPlay lifecycle: %s"),
            *ParentClass.ClassPath));
        return false;
    }

    // 比較入力のVariable Getを検証できるよう、関数本体より先に反射済み変数IRを用意する。
    FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
    if (OutIR.HasErrors())
    {
        return false;
    }

    const FBPIRGraph* EventGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        if (Graph.Kind == TEXT("event"))
        {
            if (EventGraph != nullptr)
            {
                AddDiagnostic(OutIR, Graph.Name, FGuid(),
                    TEXT("Branch conversion requires exactly one event graph"));
                return false;
            }
            EventGraph = &Graph;
        }
        else if (Graph.Kind == TEXT("function") &&
            Graph.Name == TEXT("UserConstructionScript") &&
            Graph.ExecEdges.IsEmpty() && Graph.DataEdges.IsEmpty())
        {
            continue;
        }
        else
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(),
                TEXT("Branch conversion contains an unsupported graph"));
            return false;
        }
    }
    if (EventGraph == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("Event graph is missing"));
        return false;
    }

    const FBPIRGraph& Graph = *EventGraph;
    const FBPIRNode* BeginPlay = nullptr;
    const FBPIRNode* Branch = nullptr;
    const FBPIRNode* Comparison = nullptr;
    TArray<const FBPIRNode*> Prints;
    TArray<const FBPIRNode*> VariableGets;
    TArray<const FBPIRNode*> ArithmeticNodes;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") &&
            Node.FunctionName == TEXT("ReceiveBeginPlay"))
        {
            if (BeginPlay != nullptr)
            {
                AddDiagnostic(OutIR, Graph.Name, Node.Id, TEXT("Duplicate BeginPlay event"));
                return false;
            }
            BeginPlay = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
        {
            // ネストしたBranchは複数存在するため、最初の候補のみ保持する。
            // 単一Branchの既存検証は後続のExec接続検証で行う。
            if (Branch == nullptr)
            {
                Branch = &Node;
            }
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("PrintString"))
        {
            Prints.Add(&Node);
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") ||
            Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CommutativeAssociativeBinaryOperator"))
        {
            FBPNumericComparison ResolvedComparison;
            TArray<FString> ComparisonDiagnostics;
            if (FBPNumericComparisonResolver::Resolve(Node.FunctionOwnerPath,
                Node.FunctionName, ResolvedComparison, ComparisonDiagnostics))
            {
                if (Comparison != nullptr)
                {
                    AddDiagnostic(OutIR, Graph.Name, Node.Id,
                        TEXT("Duplicate numeric comparison node"));
                    return false;
                }
                Comparison = &Node;
                continue;
            }

            FBPNumericArithmeticOperation ResolvedArithmetic;
            TArray<FString> ArithmeticDiagnostics;
            if (FBPNumericArithmeticResolver::Resolve(Node.FunctionOwnerPath,
                Node.FunctionName, ResolvedArithmetic, ArithmeticDiagnostics))
            {
                ArithmeticNodes.Add(&Node);
                continue;
            }

            AddDiagnostic(OutIR, Graph.Name, Node.Id,
                TEXT("Branch conversion contains an unsupported function call"));
            return false;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_VariableGet"))
        {
            VariableGets.Add(&Node);
        }
        else
        {
            AddDiagnostic(OutIR, Graph.Name, Node.Id,
                TEXT("Branch conversion contains an unsupported node"));
            return false;
        }
    }
    // ネストBranchはExecの木構造に限定して先に解析する。
    // 共有ノード・循環・未接続ノードはC++のスコープを一意に復元できないため拒否する。
    int32 BranchCount = 0;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
        {
            ++BranchCount;
        }
    }
    if (BranchCount > 1)
    {
        if (BeginPlay == nullptr || Branch == nullptr || Comparison != nullptr ||
            VariableGets.IsEmpty() == false || ArithmeticNodes.IsEmpty() == false ||
            BranchCount > 16)
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(),
                TEXT("Nested Branch requires bool literal conditions and supported PrintString nodes"));
            return false;
        }
        const FBPIRPin* EntryThen = FindPin(*BeginPlay, UEdGraphSchema_K2::PN_Then, EGPD_Output);
        if (EntryThen == nullptr || Graph.DataEdges.IsEmpty() == false)
        {
            AddDiagnostic(OutIR, Graph.Name, BeginPlay->Id,
                TEXT("Nested Branch does not support linked data inputs"));
            return false;
        }
        TSet<FGuid> Visited;
        int32 ConsumedEdges = 0;
        // 出力ピンごとの後続は一意でなければならない。Exec入力への複数接続は
        // 合流点としてのみ認め、共有ノードの二重出力を防ぐ。
        auto FindSuccessor = [&](const FBPIRNode& Node, const FBPIRPin& Output,
            const FBPIRNode*& NextNode) -> bool
        {
            NextNode = nullptr;
            for (const FBPIRLink& Edge : Graph.ExecEdges)
            {
                if (Edge.SourceNode == Node.Id && Edge.SourcePin == Output.Id)
                {
                    if (NextNode != nullptr)
                    {
                        return false;
                    }
                    NextNode = Graph.Nodes.FindByPredicate(
                        [&Edge](const FBPIRNode& Candidate)
                        {
                            return Candidate.Id == Edge.TargetNode;
                        });
                    const FBPIRPin* Input = NextNode != nullptr
                        ? FindPin(*NextNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input)
                        : nullptr;
                    if (Input == nullptr || Input->Id != Edge.TargetPin)
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        // 各枝から到達可能なノードを列挙する。閉路や未対応Exec出力は拒否する。
        TFunction<bool(const FBPIRNode*, TSet<FGuid>&, TSet<FGuid>&)> CollectReachable;
        CollectReachable = [&](const FBPIRNode* Current, TSet<FGuid>& Reachable,
            TSet<FGuid>& Active) -> bool
        {
            if (Current == nullptr)
            {
                return true;
            }
            if (Active.Contains(Current->Id))
            {
                return false;
            }
            if (Reachable.Contains(Current->Id))
            {
                return true;
            }
            Active.Add(Current->Id);
            Reachable.Add(Current->Id);
            for (const FBPIRLink& Edge : Graph.ExecEdges)
            {
                if (Edge.SourceNode != Current->Id)
                {
                    continue;
                }
                const FBPIRPin* Output = Current->Pins.FindByPredicate(
                    [&Edge](const FBPIRPin& Pin)
                    {
                        return Pin.Id == Edge.SourcePin && Pin.Direction == EGPD_Output;
                    });
                const FBPIRNode* Child = nullptr;
                if (Output == nullptr || FindSuccessor(*Current, *Output, Child) == false ||
                    CollectReachable(Child, Reachable, Active) == false)
                {
                    return false;
                }
            }
            Active.Remove(Current->Id);
            return true;
        };
        // 共通到達点のうち最も手前の候補を選ぶ。
        // 候補が複数の独立した合流点を持つ場合は安全に構造化できないため拒否する。
        auto FindJoin = [&](const FBPIRNode* TrueStart, const FBPIRNode* FalseStart,
            const FBPIRNode*& Join) -> bool
        {
            Join = nullptr;
            if (TrueStart == nullptr || FalseStart == nullptr)
            {
                return true;
            }
            TSet<FGuid> TrueReachable;
            TSet<FGuid> FalseReachable;
            TSet<FGuid> Active;
            if (CollectReachable(TrueStart, TrueReachable, Active) == false ||
                CollectReachable(FalseStart, FalseReachable, Active) == false)
            {
                return false;
            }
            TSet<FGuid> Common;
            for (const FGuid& Id : TrueReachable)
            {
                if (FalseReachable.Contains(Id))
                {
                    Common.Add(Id);
                }
            }
            if (Common.IsEmpty())
            {
                return true;
            }
            // 他の共通候補から到達できない唯一の候補が最初の合流点。
            for (const FGuid& Id : Common)
            {
                bool bHasCommonPredecessor = false;
                for (const FGuid& Other : Common)
                {
                    if (Id == Other)
                    {
                        continue;
                    }
                    const FBPIRNode* OtherNode = Graph.Nodes.FindByPredicate(
                        [&Other](const FBPIRNode& Node) { return Node.Id == Other; });
                    TSet<FGuid> Reachable;
                    TSet<FGuid> Visiting;
                    if (CollectReachable(OtherNode, Reachable, Visiting) == false)
                    {
                        return false;
                    }
                    if (Reachable.Contains(Id))
                    {
                        bHasCommonPredecessor = true;
                        break;
                    }
                }
                if (bHasCommonPredecessor == false)
                {
                    if (Join != nullptr)
                    {
                        return false;
                    }
                    Join = Graph.Nodes.FindByPredicate(
                        [&Id](const FBPIRNode& Node) { return Node.Id == Id; });
                }
            }
            return Join != nullptr;
        };
        TFunction<bool(const FBPIRNode&, const FGuid&, int32,
            TArray<FBPSemanticStatement>&)> Walk;
        Walk = [&](const FBPIRNode& Current, const FGuid& Stop, int32 Depth,
            TArray<FBPSemanticStatement>& Statements) -> bool
        {
            if (Current.Id == Stop)
            {
                return true;
            }
            if (Depth > 32 || Visited.Contains(Current.Id))
            {
                return false;
            }
            Visited.Add(Current.Id);
            if (Current.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
            {
                const FBPIRPin* ConditionPin = FindPin(Current, TEXT("Condition"), EGPD_Input);
                const FBPIRPin* TruePin = FindPin(Current, UEdGraphSchema_K2::PN_Then, EGPD_Output);
                const FBPIRPin* FalsePin = FindPin(Current, TEXT("else"), EGPD_Output);
                if (ConditionPin == nullptr || TruePin == nullptr || FalsePin == nullptr)
                {
                    return false;
                }
                FString ConditionValue;
                if (FBPBooleanPinDefaultEmitter::Emit(*ConditionPin, ConditionValue) == false)
                {
                    return false;
                }
                const FBPIRNode* TrueNext = nullptr;
                const FBPIRNode* FalseNext = nullptr;
                if (FindSuccessor(Current, *TruePin, TrueNext) == false ||
                    FindSuccessor(Current, *FalsePin, FalseNext) == false)
                {
                    return false;
                }
                const FBPIRNode* Join = nullptr;
                if (FindJoin(TrueNext, FalseNext, Join) == false)
                {
                    return false;
                }
                // 合流点に至らず終了する枝があれば、共通後続を無条件実行すると
                // Blueprintと動作が変わるため、そのグラフは拒否する。
                if (Join != nullptr)
                {
                    TFunction<bool(const FBPIRNode*, TSet<FGuid>&)> ReachesJoin;
                    ReachesJoin = [&](const FBPIRNode* Node, TSet<FGuid>& Active) -> bool
                    {
                        if (Node == nullptr)
                        {
                            return false;
                        }
                        if (Node->Id == Join->Id)
                        {
                            return true;
                        }
                        if (Active.Contains(Node->Id))
                        {
                            return false;
                        }
                        Active.Add(Node->Id);
                        bool bHasSuccessor = false;
                        for (const FBPIRLink& Edge : Graph.ExecEdges)
                        {
                            if (Edge.SourceNode == Node->Id)
                            {
                                bHasSuccessor = true;
                                const FBPIRNode* Child = Graph.Nodes.FindByPredicate(
                                    [&Edge](const FBPIRNode& Candidate)
                                    {
                                        return Candidate.Id == Edge.TargetNode;
                                    });
                                if (ReachesJoin(Child, Active) == false)
                                {
                                    return false;
                                }
                            }
                        }
                        Active.Remove(Node->Id);
                        return bHasSuccessor;
                    };
                    TSet<FGuid> Active;
                    if (ReachesJoin(TrueNext, Active) == false ||
                        ReachesJoin(FalseNext, Active) == false)
                    {
                        return false;
                    }
                }
                FBPSemanticStatement Statement;
                Statement.Kind = EBPSemanticStatementKind::Branch;
                Statement.SourceNodeId = Current.Id;
                Statement.Expression = Literal(EBPSemanticTypeKind::Bool,
                    ConditionValue, Current, TEXT("Condition"));
                const FBPIRNode* Children[2] = { TrueNext, FalseNext };
                for (int32 Index = 0; Index < 2; ++Index)
                {
                    if (Children[Index] != nullptr)
                    {
                        ++ConsumedEdges;
                        TArray<FBPSemanticStatement>& Target = Index == 0
                            ? Statement.ThenStatements : Statement.ElseStatements;
                        if (Walk(*Children[Index], Join != nullptr ? Join->Id : Stop,
                                Depth + 1, Target) == false)
                        {
                            return false;
                        }
                    }
                }
                Statements.Add(MoveTemp(Statement));
                if (Join != nullptr && Join->Id != Stop)
                {
                    return Walk(*Join, Stop, Depth + 1, Statements);
                }
                return true;
            }
            if (Current.ClassPath != TEXT("/Script/BlueprintGraph.K2Node_CallFunction") ||
                Current.FunctionName != TEXT("PrintString"))
            {
                return false;
            }
            FBPPrintStringCall Values;
            if (AnalyzeBranchPrint(GraphIR, Graph, *BeginPlay, Current, Values, OutIR) == false)
            {
                return false;
            }
            FBPSemanticStatement PrintStatement;
            PrintStatement.Kind = EBPSemanticStatementKind::Expression;
            PrintStatement.SourceNodeId = Current.Id;
            PrintStatement.Expression = MakePrintStringExpression(
                Values, Current, GraphIR.ParentClassPath);
            Statements.Add(MoveTemp(PrintStatement));
            const FBPIRPin* ThenPin = FindPin(Current, UEdGraphSchema_K2::PN_Then, EGPD_Output);
            const FBPIRNode* Next = nullptr;
            if (ThenPin == nullptr || FindSuccessor(Current, *ThenPin, Next) == false)
            {
                return false;
            }
            if (Next != nullptr)
            {
                ++ConsumedEdges;
                return Walk(*Next, Stop, Depth + 1, Statements);
            }
            return true;
        };
        const FBPIRLink* Start = nullptr;
        for (const FBPIRLink& Edge : Graph.ExecEdges)
        {
            if (Edge.SourceNode == BeginPlay->Id && Edge.SourcePin == EntryThen->Id)
            {
                if (Start != nullptr)
                {
                    AddDiagnostic(OutIR, Graph.Name, BeginPlay->Id,
                        TEXT("Nested Branch has multiple entry successors"));
                    return false;
                }
                Start = &Edge;
            }
        }
        const FBPIRNode* Root = Start != nullptr
            ? Graph.Nodes.FindByPredicate(
                [Start](const FBPIRNode& Node) { return Node.Id == Start->TargetNode; })
            : nullptr;
        const FBPIRPin* RootExecute = Root != nullptr
            ? FindPin(*Root, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
        if (Root == nullptr || RootExecute == nullptr || Start->TargetPin != RootExecute->Id ||
            Root->ClassPath != TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
        {
            AddDiagnostic(OutIR, Graph.Name, BeginPlay->Id,
                TEXT("Nested Branch entry Exec edge is invalid"));
            return false;
        }
        FBPSemanticFunction Function;
        Function.Name = TEXT("BeginPlay");
        Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
        FBPSemanticBasicBlock Block;
        Block.SourceEntryNodeId = BeginPlay->Id;
        ++ConsumedEdges;
        if (Walk(*Root, FGuid(), 0, Block.Statements) == false ||
            ConsumedEdges != Graph.ExecEdges.Num() ||
            Visited.Num() != BranchCount + Prints.Num())
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(),
                TEXT("Nested Branch contains a shared, cyclic, disconnected or unsupported Exec path"));
            return false;
        }
        Function.Blocks.Add(MoveTemp(Block));
        OutIR.Functions.Add(MoveTemp(Function));
        return OutIR.HasErrors() == false;
    }
    if (BeginPlay == nullptr || Branch == nullptr || Prints.Num() != 2 ||
        Graph.ExecEdges.Num() != 3 ||
        (Comparison == nullptr &&
            (Graph.DataEdges.IsEmpty() == false || VariableGets.IsEmpty() == false)))
    {
        AddDiagnostic(OutIR, Graph.Name, FGuid(),
            TEXT("Branch conversion requires one supported condition, three Exec edges, and two PrintString calls"));
        return false;
    }
    const FBPIRPin* Condition = FindPin(*Branch, TEXT("Condition"), EGPD_Input);
    const FBPIRPin* BeginThen = FindPin(*BeginPlay, UEdGraphSchema_K2::PN_Then, EGPD_Output);
    const FBPIRPin* Execute = FindPin(*Branch, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    const FBPIRPin* Then = FindPin(*Branch, UEdGraphSchema_K2::PN_Then, EGPD_Output);
    const FBPIRPin* Else = FindPin(*Branch, TEXT("else"), EGPD_Output);
    if (Condition == nullptr || BeginThen == nullptr || Execute == nullptr ||
        Then == nullptr || Else == nullptr)
    {
        AddDiagnostic(OutIR, Graph.Name, Branch->Id,
            TEXT("Branch condition or Exec pins are invalid"));
        return false;
    }
    FBPSemanticExpression ConditionExpression;
    if (Comparison != nullptr)
    {
        if (LowerNumericComparisonCondition(Graph, *Comparison, *Branch,
            *Condition, VariableGets, ArithmeticNodes, OutIR.Variables,
            ConditionExpression, OutIR) == false)
        {
            return false;
        }
    }
    else
    {
        FString ConditionValue;
        if (FBPBooleanPinDefaultEmitter::Emit(*Condition, ConditionValue) == false)
        {
            AddDiagnostic(OutIR, Graph.Name, Branch->Id,
                TEXT("Branch bool literal condition is invalid"));
            return false;
        }
        ConditionExpression = Literal(EBPSemanticTypeKind::Bool,
            ConditionValue, *Branch, TEXT("Condition"));
    }
    const FBPIRNode* ThenPrint = nullptr;
    const FBPIRNode* ElsePrint = nullptr;
    bool bBeginEdge = false;
    for (const FBPIRLink& Edge : Graph.ExecEdges)
    {
        if (Edge.SourceNode == BeginPlay->Id && Edge.TargetNode == Branch->Id &&
            Edge.SourcePin == BeginThen->Id && Edge.TargetPin == Execute->Id)
        {
            bBeginEdge = true;
        }
        else if (Edge.SourceNode == Branch->Id && Edge.SourcePin == Then->Id)
        {
            const FBPIRNode* const* Match = Prints.FindByPredicate(
                [&Edge](const FBPIRNode* Node) { return Node->Id == Edge.TargetNode; });
            ThenPrint = Match != nullptr ? *Match : nullptr;
            const FBPIRPin* PrintExecute = ThenPrint != nullptr
                ? FindPin(*ThenPrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input)
                : nullptr;
            if (PrintExecute == nullptr || Edge.TargetPin != PrintExecute->Id)
            {
                AddDiagnostic(OutIR, Graph.Name, Branch->Id,
                    TEXT("Branch True output does not target a PrintString execute pin"));
                return false;
            }
        }
        else if (Edge.SourceNode == Branch->Id && Edge.SourcePin == Else->Id)
        {
            const FBPIRNode* const* Match = Prints.FindByPredicate(
                [&Edge](const FBPIRNode* Node) { return Node->Id == Edge.TargetNode; });
            ElsePrint = Match != nullptr ? *Match : nullptr;
            const FBPIRPin* PrintExecute = ElsePrint != nullptr
                ? FindPin(*ElsePrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input)
                : nullptr;
            if (PrintExecute == nullptr || Edge.TargetPin != PrintExecute->Id)
            {
                AddDiagnostic(OutIR, Graph.Name, Branch->Id,
                    TEXT("Branch False output does not target a PrintString execute pin"));
                return false;
            }
        }
        else
        {
            AddDiagnostic(OutIR, Graph.Name, Branch->Id,
                TEXT("Branch Exec flow does not match the supported shape"));
            return false;
        }
    }
    if (bBeginEdge == false || ThenPrint == nullptr || ElsePrint == nullptr || ThenPrint == ElsePrint)
    {
        AddDiagnostic(OutIR, Graph.Name, Branch->Id,
            TEXT("Branch True and False outputs must target distinct PrintString calls"));
        return false;
    }
    FBPPrintStringCall ThenValues;
    FBPPrintStringCall ElseValues;
    if (AnalyzeBranchPrint(GraphIR, Graph, *BeginPlay, *ThenPrint, ThenValues, OutIR) == false ||
        AnalyzeBranchPrint(GraphIR, Graph, *BeginPlay, *ElsePrint, ElseValues, OutIR) == false)
    {
        return false;
    }
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = BeginPlay->Id;
    FBPSemanticStatement BranchStatement;
    BranchStatement.Kind = EBPSemanticStatementKind::Branch;
    BranchStatement.SourceNodeId = Branch->Id;
    BranchStatement.Expression = MoveTemp(ConditionExpression);
    FBPSemanticStatement ThenStatement;
    ThenStatement.Kind = EBPSemanticStatementKind::Expression;
    ThenStatement.SourceNodeId = ThenPrint->Id;
    ThenStatement.Expression = MakePrintStringExpression(
        ThenValues, *ThenPrint, GraphIR.ParentClassPath);
    BranchStatement.ThenStatements.Add(MoveTemp(ThenStatement));
    FBPSemanticStatement ElseStatement;
    ElseStatement.Kind = EBPSemanticStatementKind::Expression;
    ElseStatement.SourceNodeId = ElsePrint->Id;
    ElseStatement.Expression = MakePrintStringExpression(
        ElseValues, *ElsePrint, GraphIR.ParentClassPath);
    BranchStatement.ElseStatements.Add(MoveTemp(ElseStatement));
    Block.Statements.Add(MoveTemp(BranchStatement));
    Function.Blocks.Add(MoveTemp(Block));
    OutIR.Functions.Add(MoveTemp(Function));
    FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
    return OutIR.HasErrors() == false;
}

bool LowerActorLocationImpl(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
    FBPSemanticIR& OutIR)
{
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("Actor location conversion requires a compiled source Blueprint"));
        return false;
    }
    if (Blueprint->NewVariables.Num() != GraphIR.BlueprintVariableCount)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("Blueprint variables changed after Graph IR was built"));
        return false;
    }
    if (GraphIR.Diagnostics.IsEmpty() == false)
    {
        for (const FString& Message : GraphIR.Diagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }

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

    const FBPIRGraph* EventGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        if (Graph.Kind == TEXT("event"))
        {
            if (EventGraph != nullptr)
            {
                AddDiagnostic(OutIR, Graph.Name, FGuid(),
                    TEXT("Actor location conversion requires exactly one event graph"));
                return false;
            }
            EventGraph = &Graph;
        }
        else if (Graph.Kind == TEXT("function") &&
            Graph.Name == TEXT("UserConstructionScript") &&
            Graph.ExecEdges.IsEmpty() && Graph.DataEdges.IsEmpty())
        {
            continue;
        }
        else
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(),
                TEXT("Actor location conversion contains an unsupported graph"));
            return false;
        }
    }
    if (EventGraph == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("Event graph is missing"));
        return false;
    }

    const FBPIRNode* BeginPlay = nullptr;
    const FBPIRNode* GetLocation = nullptr;
    const FBPIRNode* SetLocation = nullptr;
    for (const FBPIRNode& Node : EventGraph->Nodes)
    {
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") &&
            Node.FunctionName == TEXT("ReceiveBeginPlay"))
        {
            if (BeginPlay != nullptr)
            {
                AddDiagnostic(OutIR, EventGraph->Name, Node.Id, TEXT("Duplicate BeginPlay event"));
                return false;
            }
            BeginPlay = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("K2_GetActorLocation"))
        {
            if (GetLocation != nullptr)
            {
                AddDiagnostic(OutIR, EventGraph->Name, Node.Id, TEXT("Duplicate GetActorLocation call"));
                return false;
            }
            GetLocation = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("K2_SetActorLocation"))
        {
            if (SetLocation != nullptr)
            {
                AddDiagnostic(OutIR, EventGraph->Name, Node.Id, TEXT("Duplicate SetActorLocation call"));
                return false;
            }
            SetLocation = &Node;
        }
        else if (Node.FunctionName.IsNone() == false || Node.VariableName.IsNone() == false)
        {
            AddDiagnostic(OutIR, EventGraph->Name, Node.Id,
                TEXT("Actor location conversion contains an unsupported executable node"));
            return false;
        }
    }
    if (BeginPlay == nullptr || GetLocation == nullptr || SetLocation == nullptr)
    {
        AddDiagnostic(OutIR, EventGraph->Name, FGuid(),
            TEXT("BeginPlay, GetActorLocation, or SetActorLocation is missing"));
        return false;
    }

    const FBPIRPin* ThenPin = FindPin(*BeginPlay, UEdGraphSchema_K2::PN_Then, EGPD_Output);
    const FBPIRPin* ExecutePin = FindPin(*SetLocation, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    const FBPIRPin* ReturnPin = FindPin(*GetLocation, TEXT("ReturnValue"), EGPD_Output);
    const FBPIRPin* LocationPin = FindPin(*SetLocation, TEXT("NewLocation"), EGPD_Input);
    if (ThenPin == nullptr || ExecutePin == nullptr || ReturnPin == nullptr || LocationPin == nullptr ||
        EventGraph->ExecEdges.Num() != 1 || EventGraph->DataEdges.Num() != 1)
    {
        AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
            TEXT("Actor location conversion requires one Exec edge and one location data edge"));
        return false;
    }
    const FBPIRLink& ExecEdge = EventGraph->ExecEdges[0];
    const FBPIRLink& DataEdge = EventGraph->DataEdges[0];
    if (ExecEdge.SourceNode != BeginPlay->Id || ExecEdge.SourcePin != ThenPin->Id ||
        ExecEdge.TargetNode != SetLocation->Id || ExecEdge.TargetPin != ExecutePin->Id ||
        DataEdge.SourceNode != GetLocation->Id || DataEdge.SourcePin != ReturnPin->Id ||
        DataEdge.TargetNode != SetLocation->Id || DataEdge.TargetPin != LocationPin->Id)
    {
        AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
            TEXT("Actor location Exec or data flow does not match the supported shape"));
        return false;
    }

    const UFunction* SetFunction = AActor::StaticClass()->FindFunctionByName(
        TEXT("K2_SetActorLocation"));
    FString VerifiedStatement;
    if (FBPConnectedFunctionCallEmitter::Emit(GraphIR, SetLocation->Id,
        SetFunction, Blueprint->GeneratedClass, VerifiedStatement, OutIR.Diagnostics) == false)
    {
        return false;
    }
    if (VerifiedStatement.IsEmpty())
    {
        AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
            TEXT("Validated SetActorLocation statement is empty"));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> SignatureDiagnostics;
    if (FBPFunctionSignatureResolver::Resolve(
        SetFunction, Signature, SignatureDiagnostics) == false)
    {
        for (const FString& Message : SignatureDiagnostics)
        {
            AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id, Message);
        }
        return false;
    }

    FBPSemanticExpression LocationExpression;
    if (FBPConnectedActorLocationLowering::LowerInput(GraphIR, SetLocation->Id,
        LocationPin->Id, LocationExpression, OutIR.Diagnostics) == false)
    {
        return false;
    }

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = BeginPlay->Id;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.SourceNodeId = SetLocation->Id;
    FBPSemanticExpression& Call = Statement.Expression;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type = Signature.ReturnType;
    Call.SourceNodeId = SetLocation->Id;
    Call.FunctionName = Signature.FunctionName;
    Call.FunctionOwnerPath = Signature.OwnerClassPath;

    FBPSemanticExpression Self;
    Self.Kind = EBPSemanticExpressionKind::VariableReference;
    Self.Type.Kind = EBPSemanticTypeKind::Object;
    Self.Type.TypePath = GraphIR.ParentClassPath;
    Self.VariableName = TEXT("this");
    Self.SourceNodeId = SetLocation->Id;
    Call.Arguments.Add(MoveTemp(Self));

    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        const FName ParameterName = Signature.ParameterNames[Index];
        const FBPIRPin* ParameterPin = FindPin(*SetLocation, ParameterName,
            Signature.OutputParameters[Index] ? EGPD_Output : EGPD_Input);
        if (ParameterPin == nullptr)
        {
            AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
                FString::Printf(TEXT("Parameter pin is missing: %s"), *ParameterName.ToString()));
            return false;
        }
        if (ParameterName == TEXT("NewLocation"))
        {
            Call.Arguments.Add(LocationExpression);
        }
        else if (ParameterName == TEXT("bSweep") || ParameterName == TEXT("bTeleport"))
        {
            FString Value;
            if (FBPBooleanPinDefaultEmitter::Emit(*ParameterPin, Value) == false)
            {
                AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
                    FString::Printf(TEXT("Invalid bool default: %s"), *ParameterName.ToString()));
                return false;
            }
            FBPSemanticExpression LiteralValue;
            LiteralValue.Kind = EBPSemanticExpressionKind::Literal;
            LiteralValue.Type = Signature.ParameterTypes[Index];
            LiteralValue.LiteralValue = MoveTemp(Value);
            LiteralValue.SourceNodeId = SetLocation->Id;
            LiteralValue.SourcePinId = ParameterPin->Id;
            Call.Arguments.Add(MoveTemp(LiteralValue));
        }
        else if (ParameterName == TEXT("SweepHitResult") && Signature.OutputParameters[Index])
        {
            FString StorageName;
            FString StorageDeclaration;
            if (FBPOutputParameterStorage::Emit(Signature.ParameterTypes[Index],
                SetLocation->Id, StorageName, StorageDeclaration) == false)
            {
                AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
                    TEXT("SweepHitResult storage could not be represented"));
                return false;
            }
            FBPSemanticExpression Storage;
            Storage.Kind = EBPSemanticExpressionKind::VariableReference;
            Storage.Type = Signature.ParameterTypes[Index];
            Storage.VariableName = FName(*StorageName);
            Storage.SourceNodeId = SetLocation->Id;
            Storage.SourcePinId = ParameterPin->Id;
            Call.Arguments.Add(MoveTemp(Storage));
        }
        else
        {
            AddDiagnostic(OutIR, EventGraph->Name, SetLocation->Id,
                TEXT("Unsupported SetActorLocation parameter layout"));
            return false;
        }
    }
    Block.Statements.Add(MoveTemp(Statement));
    Function.Blocks.Add(MoveTemp(Block));
    OutIR.Functions.Add(MoveTemp(Function));
    FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
    return OutIR.HasErrors() == false;
}

// CJ2: int32配列のForEach縦断経路。接続の一意性を厳密に検証する。
// Index/Elementの文字列化だけを許可し、未知の式や制御フローは拒否する。
bool LowerForEachLoopImpl(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR,
    FBPSemanticIR& OutIR)
{
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr ||
        Blueprint->NewVariables.Num() != GraphIR.BlueprintVariableCount)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("ForEachLoop requires a compiled Blueprint with matching variables"));
        return false;
    }
    FBPResolvedParentClass Parent;
    TArray<FString> ParentErrors;
    if (FBPParentClassResolver::ResolveParent(
        GraphIR.ParentClassPath, Parent, ParentErrors) == false)
    {
        for (const FString& Error : ParentErrors)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Error);
        }
        return false;
    }
    if (Parent.bSupportsBeginPlay == false)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), FString::Printf(
            TEXT("Parent class does not provide an overridable BeginPlay lifecycle: %s"),
            *Parent.ClassPath));
        return false;
    }
    const FBPIRGraph* Event = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        if (Graph.Kind == TEXT("event"))
        {
            if (Event != nullptr)
            {
                AddDiagnostic(OutIR, Graph.Name, FGuid(), TEXT("ForEachLoop requires one event graph"));
                return false;
            }
            Event = &Graph;
        }
        else if (Graph.Name != TEXT("UserConstructionScript") ||
            Graph.ExecEdges.IsEmpty() == false || Graph.DataEdges.IsEmpty() == false)
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(), TEXT("ForEachLoop has an unsupported graph"));
            return false;
        }
    }
    if (Event == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("ForEachLoop event graph is missing"));
        return false;
    }
    const FBPIRNode* Begin = nullptr;
    const FBPIRNode* Loop = nullptr;
    const FBPIRNode* Getter = nullptr;
    TArray<const FBPIRNode*> Prints;
    const FBPIRNode* IndexConverter = nullptr;
    const FBPIRNode* ElementConverter = nullptr;
    for (const FBPIRNode& Node : Event->Nodes)
    {
        if (Node.FunctionName == TEXT("ReceiveBeginPlay") &&
            Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event"))
        {
            if (Begin != nullptr) { AddDiagnostic(OutIR, Event->Name, Node.Id, TEXT("Multiple BeginPlay nodes")); return false; }
            Begin = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_MacroInstance") &&
            (Node.MacroGraphName == TEXT("ForEachLoop") ||
                Node.MacroGraphName == TEXT("ForEachLoopWithBreak")) &&
            // 名前だけでなく標準マクロの正規Graphパスを照合する。
            // 同名のユーザーマクロを標準ForEachとして誤変換しない。
            Node.MacroGraphPath == FString::Printf(
                TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:%s"),
                *Node.MacroGraphName))
        {
            if (Loop != nullptr) { AddDiagnostic(OutIR, Event->Name, Node.Id, TEXT("Multiple ForEachLoop nodes")); return false; }
            Loop = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_VariableGet") &&
            Node.bVariableSelfContext)
        {
            if (Getter != nullptr) { AddDiagnostic(OutIR, Event->Name, Node.Id, TEXT("Multiple array getters")); return false; }
            Getter = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("Conv_IntToString") &&
            Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary"))
        {
            // 変換ノードの種類はリンクの接続元で後から判定する。
            // 先に見つけたノードをIndexと断定しない。
            if (IndexConverter == nullptr)
            {
                IndexConverter = &Node;
            }
            else if (ElementConverter == nullptr)
            {
                ElementConverter = &Node;
            }
            else
            {
                AddDiagnostic(OutIR, Event->Name, Node.Id,
                    TEXT("ForEachLoop supports at most two int32 converters"));
                return false;
            }
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("PrintString") &&
            Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetSystemLibrary"))
        {
            Prints.Add(&Node);
        }
        else
        {
            AddDiagnostic(OutIR, Event->Name, Node.Id, TEXT("Unsupported node in ForEachLoop"));
            return false;
        }
    }
    if (Begin == nullptr || Loop == nullptr || Getter == nullptr || Prints.IsEmpty())
    {
        AddDiagnostic(OutIR, Event->Name, Loop != nullptr ? Loop->Id : FGuid(),
            TEXT("ForEachLoop requires BeginPlay, array getter and PrintString"));
        return false;
    }
    const FBPIRPin* BeginThen = FindPin(*Begin, TEXT("then"), EGPD_Output);
    const FBPIRPin* Execute = FindPin(*Loop, TEXT("Exec"), EGPD_Input);
    const FBPIRPin* ArrayPin = FindPin(*Loop, TEXT("Array"), EGPD_Input);
    const FBPIRPin* BodyPin = FindPin(*Loop, TEXT("LoopBody"), EGPD_Output);
    const FBPIRPin* CompletedPin = FindPin(*Loop, TEXT("Completed"), EGPD_Output);
    const FBPIRPin* ElementPin = FindPin(*Loop, TEXT("Array Element"), EGPD_Output);
    const FBPIRPin* IndexPin = FindPin(*Loop, TEXT("Array Index"), EGPD_Output);
    const bool bWithBreak = Loop->MacroGraphName == TEXT("ForEachLoopWithBreak");
    const FBPIRPin* BreakPin = bWithBreak
        ? FindPin(*Loop, TEXT("Break"), EGPD_Input) : nullptr;
    const FBPIRPin* GetterValue = FindPin(*Getter, Getter->VariableName, EGPD_Output);
    if (BeginThen == nullptr || Execute == nullptr || ArrayPin == nullptr ||
        BodyPin == nullptr || CompletedPin == nullptr || ElementPin == nullptr ||
        IndexPin == nullptr || GetterValue == nullptr ||
        (bWithBreak && (BreakPin == nullptr || BreakPin->Category != TEXT("exec"))) ||
        ArrayPin->Category != TEXT("int") || ElementPin->Category != TEXT("int") ||
        IndexPin->Category != TEXT("int") || GetterValue->Category != TEXT("int"))
    {
        AddDiagnostic(OutIR, Event->Name, Loop->Id, TEXT("ForEachLoop pins or int32 array type are unresolved"));
        return false;
    }
    // 配列GetterとIndex→Conv_IntToString→PrintString以外のデータフローは拒否する。
    // Array Elementの暗黙使用を見落とすと生成コードが元のBlueprintと異なる。
    bool bArrayConnected = false;
    const FBPIRNode* IndexConsumerPrint = nullptr;
    const FBPIRNode* ElementConsumerPrint = nullptr;
    // 最大2つの変換ノードを、実際の接続元（Array Index / Array Element）で識別する。
    // ノード列挙順や関数名だけでは要素とIndexを区別できない。
    const FBPIRNode* Converters[2] = { IndexConverter, ElementConverter };
    int32 ExpectedDataEdges = 1;
    for (const FBPIRNode* Converter : Converters)
    {
        if (Converter != nullptr)
        {
            ExpectedDataEdges += 2;
        }
    }
    if (Event->DataEdges.Num() != ExpectedDataEdges)
    {
        AddDiagnostic(OutIR, Event->Name, Loop->Id, TEXT("ForEachLoop data link count is unsupported"));
        return false;
    }
    int32 ArrayLinks = 0;
    for (const FBPIRLink& Edge : Event->DataEdges)
    {
        if (Edge.SourceNode == Getter->Id && Edge.SourcePin == GetterValue->Id &&
            Edge.TargetNode == Loop->Id && Edge.TargetPin == ArrayPin->Id)
        {
            ++ArrayLinks;
        }
    }
    bArrayConnected = ArrayLinks == 1;
    // すべてのデータエッジが許可済み3種類の接続に属することを確認する。
    // リンク数だけの検証では、別の接続を同数だけ混入させた場合に見逃す。
    for (const FBPIRLink& Edge : Event->DataEdges)
    {
        bool bRecognized = Edge.SourceNode == Getter->Id &&
            Edge.SourcePin == GetterValue->Id && Edge.TargetNode == Loop->Id &&
            Edge.TargetPin == ArrayPin->Id;
        for (const FBPIRNode* Converter : Converters)
        {
            if (Converter == nullptr)
            {
                continue;
            }
            const FBPIRPin* Input = FindPin(*Converter, TEXT("InInt"), EGPD_Input);
            const FBPIRPin* Output = FindPin(*Converter, TEXT("ReturnValue"), EGPD_Output);
            if (Input == nullptr || Output == nullptr)
            {
                continue;
            }
            bRecognized |= Edge.SourceNode == Loop->Id &&
                (Edge.SourcePin == IndexPin->Id || Edge.SourcePin == ElementPin->Id) &&
                Edge.TargetNode == Converter->Id && Edge.TargetPin == Input->Id;
            if (Edge.SourceNode == Converter->Id && Edge.SourcePin == Output->Id)
            {
                for (const FBPIRNode* Print : Prints)
                {
                    const FBPIRPin* TextPin = FindPin(*Print, TEXT("InString"), EGPD_Input);
                    if (TextPin != nullptr && Edge.TargetNode == Print->Id &&
                        Edge.TargetPin == TextPin->Id)
                    {
                        bRecognized = true;
                    }
                }
            }
        }
        if (bRecognized == false)
        {
            AddDiagnostic(OutIR, Event->Name, Loop->Id,
                TEXT("ForEachLoop has an unexpected data connection"));
            return false;
        }
    }
    for (const FBPIRNode* Converter : Converters)
    {
        if (Converter == nullptr)
        {
            continue;
        }
        const FBPIRPin* Input = FindPin(*Converter, TEXT("InInt"), EGPD_Input);
        const FBPIRPin* Output = FindPin(*Converter, TEXT("ReturnValue"), EGPD_Output);
        // 関数名と接続GUIDが正しくても、ピンの型が異なれば
        // int32→FStringの変換として扱えない。破損IRや将来の型変更を拒否する。
        if (Input == nullptr || Output == nullptr ||
            Input->Category != TEXT("int") || Output->Category != TEXT("string"))
        {
            AddDiagnostic(OutIR, Event->Name, Converter->Id,
                TEXT("ForEachLoop converter requires int32 input and string output"));
            return false;
        }
        const FBPIRPin* OriginPin = nullptr;
        const FBPIRNode* Consumer = nullptr;
        int32 InputLinks = 0;
        int32 OutputLinks = 0;
        for (const FBPIRLink& Edge : Event->DataEdges)
        {
            if (Edge.TargetNode == Converter->Id && Edge.TargetPin == Input->Id &&
                Edge.SourceNode == Loop->Id)
            {
                if (Edge.SourcePin == IndexPin->Id || Edge.SourcePin == ElementPin->Id)
                {
                    OriginPin = Edge.SourcePin == IndexPin->Id ? IndexPin : ElementPin;
                    ++InputLinks;
                }
            }
            if (Edge.SourceNode == Converter->Id && Edge.SourcePin == Output->Id)
            {
                for (const FBPIRNode* Print : Prints)
                {
                    const FBPIRPin* TextPin = FindPin(*Print, TEXT("InString"), EGPD_Input);
                    if (TextPin != nullptr && Edge.TargetNode == Print->Id &&
                        Edge.TargetPin == TextPin->Id)
                    {
                        Consumer = Print;
                        ++OutputLinks;
                    }
                }
            }
        }
        // 同じConverterの入力・出力だけでなく、他のConverterや配列Getterへ
        // 同一ピンが二重接続されていないことをリンク数と併せて確認する。
        // ForEachは純粋式の複製が起こり得るため、曖昧な共有接続を許容しない。
        if (InputLinks != 1 || OutputLinks != 1 || Consumer == nullptr ||
            (IndexConsumerPrint != nullptr && Consumer == IndexConsumerPrint) ||
            (ElementConsumerPrint != nullptr && Consumer == ElementConsumerPrint))
        {
            AddDiagnostic(OutIR, Event->Name, Converter->Id, TEXT("ForEachLoop converter connections are ambiguous"));
            return false;
        }
        if (OriginPin == IndexPin)
        {
            if (IndexConsumerPrint != nullptr)
            {
                AddDiagnostic(OutIR, Event->Name, Converter->Id, TEXT("Multiple Array Index consumers"));
                return false;
            }
            IndexConsumerPrint = Consumer;
        }
        else
        {
            if (ElementConsumerPrint != nullptr)
            {
                AddDiagnostic(OutIR, Event->Name, Converter->Id, TEXT("Multiple Array Element consumers"));
                return false;
            }
            ElementConsumerPrint = Consumer;
        }
    }
    if (bArrayConnected == false)
    {
        AddDiagnostic(OutIR, Event->Name, Loop->Id, TEXT("ForEachLoop requires one direct array getter connection"));
        return false;
    }
    // Edge配列の列挙順はBlueprintの実行順序ではない。
    // BeginPlayからLoopまでのPrintString列も実行順に辿り、ループ前の処理を失わない。
    // 直結だけを要求すると、有効な BeginPlay -> PrintString -> ForEachLoop を拒否してしまう。
    TArray<const FBPIRNode*> EntryPrints;
    TArray<const FBPIRNode*> BodyPrints;
    TArray<const FBPIRNode*> CompletedPrints;
    TSet<FGuid> Visited;
    auto ReadEntryChain = [&]() -> bool
    {
        FGuid SourceNode = Begin->Id;
        FGuid SourcePin = BeginThen->Id;
        while (true)
        {
            const FBPIRLink* Next = nullptr;
            for (const FBPIRLink& Edge : Event->ExecEdges)
            {
                if (Edge.SourceNode == SourceNode && Edge.SourcePin == SourcePin)
                {
                    if (Next != nullptr) { return false; }
                    Next = &Edge;
                }
            }
            if (Next == nullptr) { return false; }
            if (Next->TargetNode == Loop->Id && Next->TargetPin == Execute->Id)
            {
                return true;
            }
            const FBPIRNode* Target = Event->Nodes.FindByPredicate(
                [Next](const FBPIRNode& Node) { return Node.Id == Next->TargetNode; });
            if (Target == nullptr || Target->FunctionName != TEXT("PrintString") ||
                Visited.Contains(Target->Id))
            {
                return false;
            }
            const FBPIRPin* TargetExec = FindPin(*Target, TEXT("execute"), EGPD_Input);
            const FBPIRPin* Then = FindPin(*Target, TEXT("then"), EGPD_Output);
            if (TargetExec == nullptr || TargetExec->Id != Next->TargetPin || Then == nullptr)
            {
                return false;
            }
            Visited.Add(Target->Id);
            EntryPrints.Add(Target);
            SourceNode = Target->Id;
            SourcePin = Then->Id;
        }
    };
    auto ReadChain = [&](const FBPIRPin* Start, const bool bAllowBreak,
        TArray<const FBPIRNode*>& OutPrints, bool& bOutEndsWithBreak) -> bool
    {
        bOutEndsWithBreak = false;
        FGuid SourceNode = Loop->Id;
        FGuid SourcePin = Start->Id;
        while (true)
        {
            const FBPIRLink* Next = nullptr;
            for (const FBPIRLink& Edge : Event->ExecEdges)
            {
                if (Edge.SourceNode == SourceNode && Edge.SourcePin == SourcePin)
                {
                    if (Next != nullptr) { return false; }
                    Next = &Edge;
                }
            }
            if (Next == nullptr) { return true; }
            if (bAllowBreak && BreakPin != nullptr &&
                Next->TargetNode == Loop->Id && Next->TargetPin == BreakPin->Id)
            {
                bOutEndsWithBreak = true;
                return true;
            }
            const FBPIRNode* Target = Event->Nodes.FindByPredicate(
                [Next](const FBPIRNode& Node) { return Node.Id == Next->TargetNode; });
            if (Target == nullptr || Target->FunctionName != TEXT("PrintString") ||
                Visited.Contains(Target->Id))
            {
                return false;
            }
            const FBPIRPin* TargetExec = FindPin(*Target, TEXT("execute"), EGPD_Input);
            if (TargetExec == nullptr || TargetExec->Id != Next->TargetPin) { return false; }
            Visited.Add(Target->Id);
            OutPrints.Add(Target);
            const FBPIRPin* Then = FindPin(*Target, TEXT("then"), EGPD_Output);
            if (Then == nullptr) { return true; }
            SourceNode = Target->Id;
            SourcePin = Then->Id;
        }
    };
    bool bBodyEndsWithBreak = false;
    bool bCompletedEndsWithBreak = false;
    if (ReadEntryChain() == false ||
        ReadChain(BodyPin, bWithBreak, BodyPrints, bBodyEndsWithBreak) == false ||
        ReadChain(CompletedPin, false, CompletedPrints, bCompletedEndsWithBreak) == false ||
        BodyPrints.IsEmpty() || Visited.Num() != Prints.Num() ||
        bCompletedEndsWithBreak || bBodyEndsWithBreak != bWithBreak ||
        Event->ExecEdges.Num() != Visited.Num() + 1 + (bBodyEndsWithBreak ? 1 : 0))
    {
        AddDiagnostic(OutIR, Event->Name, Loop->Id, TEXT("ForEachLoop Exec chains are ambiguous or disconnected"));
        return false;
    }
    FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
    const FBPSemanticVariable* Variable = OutIR.Variables.FindByPredicate(
        [Getter](const FBPSemanticVariable& Item) { return Item.Name == Getter->VariableName; });
    if (Variable == nullptr || Variable->Type.Kind != EBPSemanticTypeKind::Int32 ||
        Variable->Type.ContainerKind != TEXT("array") ||
        Variable->ReflectedCppType != TEXT("TArray<int32>"))
    {
        AddDiagnostic(OutIR, Event->Name, Getter->Id, TEXT("ForEachLoop array member type is not verified int32[]"));
        return false;
    }
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = Begin->Id;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::ForEachLoop;
    Statement.SourceNodeId = Loop->Id;
    Statement.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Statement.LoopArray.VariableName = Getter->VariableName;
    Statement.LoopArray.Type = Variable->Type;
    Statement.LoopArray.SourceNodeId = Getter->Id;
    Statement.LoopArray.SourcePinId = GetterValue->Id;
    Statement.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Statement.LoopElementVariable = TEXT("ForEachArrayElement");
    Statement.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    auto AppendPrints = [&](const TArray<const FBPIRNode*>& Nodes,
        TArray<FBPSemanticStatement>& Destination) -> bool
    {
        for (const FBPIRNode* Print : Nodes)
        {
            FBPPrintStringCall Values;
            if (AnalyzeBranchPrint(GraphIR, *Event, *Begin, *Print, Values, OutIR) == false)
            {
                return false;
            }
            FBPSemanticStatement Item;
            Item.Kind = EBPSemanticStatementKind::Expression;
            Item.SourceNodeId = Print->Id;
            Item.Expression = MakePrintStringExpression(Values, *Print, GraphIR.ParentClassPath);
            if (Print == IndexConsumerPrint || Print == ElementConsumerPrint)
            {
                // Completedからループ内のIndex/Elementを読む場合は値の寿命が異なるため拒否する。
                if (&Destination == &Statement.CompletedStatements)
                {
                    AddDiagnostic(OutIR, Event->Name, Print->Id,
                        TEXT("ForEachLoop Completed cannot reference Array Index"));
                    return false;
                }
                FBPSemanticExpression Index;
                Index.Kind = EBPSemanticExpressionKind::VariableReference;
                Index.Type.Kind = EBPSemanticTypeKind::Int32;
                Index.VariableName = Print == IndexConsumerPrint
                    ? Statement.LoopIndexVariable : Statement.LoopElementVariable;
                Index.SourceNodeId = Loop->Id;
                // ループローカルはメンバーGetterではないためSourcePinIdを持たせない。
                Index.SourcePinId = FGuid();
                FBPSemanticExpression Conversion;
                Conversion.Kind = EBPSemanticExpressionKind::FunctionCall;
                Conversion.Type.Kind = EBPSemanticTypeKind::String;
                Conversion.FunctionName = TEXT("Conv_IntToString");
                Conversion.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
                // 元の変換ノードGUIDは出典の追跡に必要。
                for (const FBPIRNode* Converter : Converters)
                {
                    if (Converter != nullptr)
                    {
                        for (const FBPIRLink& Edge : Event->DataEdges)
                        {
                            if (Edge.SourceNode == Converter->Id &&
                                Edge.TargetNode == Print->Id)
                            {
                                Conversion.SourceNodeId = Converter->Id;
                            }
                        }
                    }
                }
                Conversion.Arguments.Add(MoveTemp(Index));
                Item.Expression.Arguments[1] = MoveTemp(Conversion);
            }
            Destination.Add(MoveTemp(Item));
        }
        return true;
    };
    if (AppendPrints(EntryPrints, Block.Statements) == false ||
        AppendPrints(BodyPrints, Statement.LoopBodyStatements) == false ||
        AppendPrints(CompletedPrints, Statement.CompletedStatements) == false)
    {
        return false;
    }
    if (bBodyEndsWithBreak)
    {
        FBPSemanticStatement BreakStatement;
        BreakStatement.Kind = EBPSemanticStatementKind::Break;
        BreakStatement.SourceNodeId = Loop->Id;
        Statement.LoopBodyStatements.Add(MoveTemp(BreakStatement));
    }
    Block.Statements.Add(MoveTemp(Statement));
    Function.Blocks.Add(MoveTemp(Block));
    OutIR.Functions.Add(MoveTemp(Function));
    return OutIR.HasErrors() == false;
}

bool LowerForLoopImpl(const UBlueprint* Blueprint, const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;

    FBPResolvedParentClass ParentClass;
    TArray<FString> ParentDiagnostics;
    if (FBPParentClassResolver::ResolveParent(
        GraphIR.ParentClassPath, ParentClass, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
        return false;
    }
    if (ParentClass.bSupportsBeginPlay == false)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), FString::Printf(
            TEXT("Parent class does not provide an overridable BeginPlay lifecycle: %s"),
            *ParentClass.ClassPath));
        return false;
    }
    if (GraphIR.BlueprintVariableCount != 0)
    {
        if (Blueprint == nullptr)
        {
            AddDiagnostic(OutIR, FString(), FGuid(),
                TEXT("ForLoop member variables require a compiled Blueprint"));
            return false;
        }
        // 既存の変数抽出器を利用し、反射で検証された型と名前を保持する。
        FBPSemanticVariableExtractor::Append(Blueprint, OutIR);
        if (OutIR.HasErrors())
        {
            return false;
        }
    }

    const FBPIRGraph* EventGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        if (Graph.Kind == TEXT("event"))
        {
            if (EventGraph != nullptr)
            {
                AddDiagnostic(OutIR, Graph.Name, FGuid(),
                    TEXT("ForLoop conversion requires exactly one event graph"));
                return false;
            }
            EventGraph = &Graph;
        }
        else if (Graph.Name != TEXT("UserConstructionScript") ||
            Graph.ExecEdges.IsEmpty() == false || Graph.DataEdges.IsEmpty() == false)
        {
            AddDiagnostic(OutIR, Graph.Name, FGuid(),
                TEXT("ForLoop conversion contains an unsupported graph"));
            return false;
        }
    }
    if (EventGraph == nullptr)
    {
        AddDiagnostic(OutIR, FString(), FGuid(), TEXT("ForLoop event graph is missing"));
        return false;
    }

    // 二重ForLoopの最小構造を先に解析する。既存の単一ForLoop経路には影響させない。
    // 対応形状: BeginPlay -> 外側LoopBody -> 内側LoopBody -> PrintString。
    // 未知のExec/Data接続は推測せずに拒否する。
    TArray<const FBPIRNode*> NestedLoops;
    for (const FBPIRNode& Node : EventGraph->Nodes)
    {
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_MacroInstance") &&
            Node.MacroGraphName == TEXT("ForLoop"))
        {
            NestedLoops.Add(&Node);
        }
    }
    // PrintStringのみの多重ループでは、Execチェーンを順にたどり、
    // 内側Completedを外側ループ本体の後続処理として配置する。
    if (NestedLoops.Num() >= 2)
    {
        const FBPIRNode* Entry = nullptr;
        int32 PrintCount = 0;
        int32 ConverterCount = 0;
        int32 ArithmeticCount = 0;
        int32 AppendCount = 0;
        int32 GetterCount = 0;
        bool bSupportedNodes = true;
        for (const FBPIRNode& Node : EventGraph->Nodes)
        {
            if (Node.FunctionName == TEXT("ReceiveBeginPlay") &&
                Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event"))
            {
                Entry = &Node;
            }
            else if (Node.FunctionName == TEXT("PrintString") &&
                Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction"))
            {
                ++PrintCount;
            }
            else if (Node.FunctionName == TEXT("Conv_IntToString") &&
                Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary"))
            {
                ++ConverterCount;
            }
            else if (Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
                Node.FunctionName == TEXT("Concat_StrStr"))
            {
                ++AppendCount;
            }
            else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_VariableGet"))
            {
                ++GetterCount;
            }
            else if (Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetMathLibrary") &&
                (Node.FunctionName == TEXT("Add_IntInt") ||
                    Node.FunctionName == TEXT("Subtract_IntInt") ||
                    Node.FunctionName == TEXT("Multiply_IntInt")))
            {
                ++ArithmeticCount;
            }
            else if (Node.MacroGraphName != TEXT("ForLoop") ||
                Node.ClassPath != TEXT("/Script/BlueprintGraph.K2Node_MacroInstance"))
            {
                bSupportedNodes = false;
            }
        }
        if (bSupportedNodes && Entry != nullptr && PrintCount > 0 &&
            EventGraph->Nodes.Num() == PrintCount + ConverterCount + ArithmeticCount + AppendCount + GetterCount + NestedLoops.Num() + 1)
        {
            const auto NextExec = [EventGraph](const FBPIRNode& Source,
                const FName PinName, const FBPIRNode*& Next) -> bool
            {
                Next = nullptr;
                const FBPIRPin* Output = FindPin(Source, PinName, EGPD_Output);
                if (Output == nullptr)
                {
                    return false;
                }
                for (const FBPIRLink& Edge : EventGraph->ExecEdges)
                {
                    if (Edge.SourceNode != Source.Id || Edge.SourcePin != Output->Id)
                    {
                        continue;
                    }
                    if (Next != nullptr)
                    {
                        return false;
                    }
                    Next = EventGraph->Nodes.FindByPredicate(
                        [&Edge](const FBPIRNode& Node) { return Node.Id == Edge.TargetNode; });
                    if (Next == nullptr)
                    {
                        return false;
                    }
                    const FBPIRPin* Execute = FindPin(
                        *Next, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
                    if (Execute == nullptr || Execute->Id != Edge.TargetPin)
                    {
                        return false;
                    }
                }
                return true;
            };
            TSet<FGuid> Visited;
            TSet<FGuid> UsedConverters;
            TArray<const FBPIRNode*> ActiveLoops;
            TFunction<bool(const FBPIRNode*, TArray<FBPSemanticStatement>&, int32)> LowerChain;
            LowerChain = [&](const FBPIRNode* Current,
                TArray<FBPSemanticStatement>& Destination, int32 Depth) -> bool
            {
                while (Current != nullptr)
                {
                    if (Visited.Contains(Current->Id))
                    {
                        return false;
                    }
                    Visited.Add(Current->Id);
                    if (Current->FunctionName == TEXT("PrintString") &&
                        Current->ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction"))
                    {
                        FBPPrintStringCall Values;
                        if (AnalyzeBranchPrint(
                                GraphIR, *EventGraph, *Entry, *Current, Values, OutIR) == false)
                        {
                            return false;
                        }
                        FBPSemanticStatement Print;
                        Print.Kind = EBPSemanticStatementKind::Expression;
                        Print.SourceNodeId = Current->Id;
                        Print.Expression = MakePrintStringExpression(
                            Values, *Current, GraphIR.ParentClassPath);
                        // PrintStringのInStringに接続されたIndex -> IntToStringを解決する。
                        // 各ループのIndexはそのLoopBody内でのみ有効とし、スコープ外参照を拒否する。
                        const FBPIRPin* TextPin = FindPin(*Current, TEXT("InString"), EGPD_Input);
                        // InStringの供給元は必ず一つ以下とする。複数接続を
                        // 先勝ちで解決するとBlueprintと異なる値を生成してしまう。
                        int32 TextProducerCount = 0;
                        for (const FBPIRLink& Candidate : EventGraph->DataEdges)
                        {
                            if (TextPin != nullptr &&
                                Candidate.TargetNode == Current->Id &&
                                Candidate.TargetPin == TextPin->Id)
                            {
                                ++TextProducerCount;
                            }
                        }
                        if (TextProducerCount > 1)
                        {
                            return false;
                        }
                        for (const FBPIRLink& Edge : EventGraph->DataEdges)
                        {
                            if (TextPin == nullptr || Edge.TargetNode != Current->Id ||
                                Edge.TargetPin != TextPin->Id)
                            {
                                continue;
                            }
                            const FBPIRNode* Converter = EventGraph->Nodes.FindByPredicate(
                                [&Edge](const FBPIRNode& Node) { return Node.Id == Edge.SourceNode; });
                            if (Converter != nullptr &&
                                Converter->ClassPath ==
                                    TEXT("/Script/BlueprintGraph.K2Node_VariableGet"))
                            {
                                // PrintStringへのFString変数直接接続も反射型で検証する。
                                const FBPSemanticVariable* Variable =
                                    FindSemanticVariable(OutIR.Variables,
                                        Converter->VariableName);
                                const FBPIRPin* ValuePin = FindPin(
                                    *Converter, Converter->VariableName, EGPD_Output);
                                if (Variable == nullptr ||
                                    Variable->Type.Kind != EBPSemanticTypeKind::String ||
                                    Variable->ReflectedCppType != TEXT("FString") ||
                                    ValuePin == nullptr ||
                                    ValuePin->Id != Edge.SourcePin ||
                                    UsedConverters.Contains(Converter->Id))
                                {
                                    return false;
                                }
                                FBPSemanticExpression Reference;
                                Reference.Kind =
                                    EBPSemanticExpressionKind::VariableReference;
                                Reference.Type = Variable->Type;
                                Reference.VariableName = Variable->Name;
                                Reference.SourceNodeId = Converter->Id;
                                Reference.SourcePinId = ValuePin->Id;
                                Print.Expression.Arguments[1] = MoveTemp(Reference);
                                UsedConverters.Add(Converter->Id);
                                continue;
                            }
                            if (Converter != nullptr &&
                                Converter->FunctionName == TEXT("Concat_StrStr") &&
                                Converter->FunctionOwnerPath ==
                                    TEXT("/Script/Engine.KismetStringLibrary"))
                            {
                                // 文字列結合のリテラル入力を安全に変換する。
                                // 未対応の入力接続は、値を黙って無視せず拒否する。
                                const FBPIRPin* Output = FindPin(
                                    *Converter, TEXT("ReturnValue"), EGPD_Output);
                                const FBPIRPin* A = FindPin(*Converter, TEXT("A"), EGPD_Input);
                                const FBPIRPin* B = FindPin(*Converter, TEXT("B"), EGPD_Input);
                                if (Output == nullptr || A == nullptr || B == nullptr ||
                                    Edge.SourcePin != Output->Id ||
                                    UsedConverters.Contains(Converter->Id))
                                {
                                    return false;
                                }
                                // 結合の各入力を再帰的にたどり、結合ノードの入れ子を解決する。
                                // 循環・複数入力・不正な出力ピンは変換せずに拒否する。
                                TSet<FGuid> ConcatStack;
                                TFunction<bool(const FBPIRNode&, const FBPIRPin&,
                                    int32, FBPSemanticExpression&)> LowerString;
                                LowerString = [&](const FBPIRNode& Node, const FBPIRPin& Pin,
                                    int32 Depth, FBPSemanticExpression& Result) -> bool
                                {
                                    if (Depth > 16)
                                    {
                                        return false;
                                    }
                                    const FBPIRLink* Link = nullptr;
                                    for (const FBPIRLink& Candidate : EventGraph->DataEdges)
                                    {
                                        if (Candidate.TargetNode == Node.Id &&
                                            Candidate.TargetPin == Pin.Id)
                                        {
                                            if (Link != nullptr)
                                            {
                                                return false;
                                            }
                                            Link = &Candidate;
                                        }
                                    }
                                    if (Link == nullptr)
                                    {
                                        Result = Literal(EBPSemanticTypeKind::String,
                                            Pin.DefaultValue, Node, Pin.Name);
                                        return true;
                                    }
                                    const FBPIRNode* Source = EventGraph->Nodes.FindByPredicate(
                                        [Link](const FBPIRNode& Candidate)
                                        {
                                            return Candidate.Id == Link->SourceNode;
                                        });
                                    if (Source == nullptr)
                                    {
                                        return false;
                                    }
                                    if (Source->ClassPath ==
                                        TEXT("/Script/BlueprintGraph.K2Node_VariableGet"))
                                    {
                                        // 反射で検証されたFStringメンバーのみ許可する。
                                        const FBPSemanticVariable* Variable =
                                            FindSemanticVariable(OutIR.Variables,
                                                Source->VariableName);
                                        const FBPIRPin* ValuePin = FindPin(
                                            *Source, Source->VariableName, EGPD_Output);
                                        if (Variable == nullptr ||
                                            Variable->Type.Kind != EBPSemanticTypeKind::String ||
                                            Variable->ReflectedCppType != TEXT("FString") ||
                                            ValuePin == nullptr ||
                                            ValuePin->Id != Link->SourcePin ||
                                            UsedConverters.Contains(Source->Id))
                                        {
                                            return false;
                                        }
                                        Result.Kind =
                                            EBPSemanticExpressionKind::VariableReference;
                                        Result.Type = Variable->Type;
                                        Result.VariableName = Variable->Name;
                                        Result.SourceNodeId = Source->Id;
                                        Result.SourcePinId = ValuePin->Id;
                                        UsedConverters.Add(Source->Id);
                                        return true;
                                    }
                                    if (Source->FunctionName == TEXT("Conv_IntToString") &&
                                        Source->FunctionOwnerPath ==
                                            TEXT("/Script/Engine.KismetStringLibrary"))
                                    {
                                        // Index直結の文字列化をConcatの葉として許可する。
                                        const FBPIRPin* Output = FindPin(
                                            *Source, TEXT("ReturnValue"), EGPD_Output);
                                        const FBPIRPin* Input = FindPin(
                                            *Source, TEXT("InInt"), EGPD_Input);
                                        if (Output == nullptr || Input == nullptr ||
                                            Output->Id != Link->SourcePin ||
                                            UsedConverters.Contains(Source->Id))
                                        {
                                            return false;
                                        }
                                        // Concat内部のConv_IntToStringでも整数演算をたどる。
                                        TSet<FGuid> ArithmeticStack;
                                        TFunction<bool(const FBPIRNode&, const FBPIRPin&,
                                            int32, FBPSemanticExpression&)> LowerInteger;
                                        LowerInteger = [&](const FBPIRNode& Owner,
                                            const FBPIRPin& InputPin, int32 IntegerDepth,
                                            FBPSemanticExpression& Value) -> bool
                                        {
                                            if (IntegerDepth > 16)
                                            {
                                                return false;
                                            }
                                            // 入力が複数のデータエッジを持つ場合は曖昧なので拒否する。
                                            const FBPIRLink* InputLink = nullptr;
                                            for (const FBPIRLink& Edge : EventGraph->DataEdges)
                                            {
                                                if (Edge.TargetNode == Owner.Id &&
                                                    Edge.TargetPin == InputPin.Id)
                                                {
                                                    if (InputLink != nullptr)
                                                    {
                                                        return false;
                                                    }
                                                    InputLink = &Edge;
                                                }
                                            }
                                            if (InputLink == nullptr)
                                            {
                                                int32 Number = 0;
                                                if (LexTryParseString(Number,
                                                        *InputPin.DefaultValue) == false)
                                                {
                                                    return false;
                                                }
                                                Value = Literal(EBPSemanticTypeKind::Int32,
                                                    LexToString(Number), Owner, InputPin.Name);
                                                return true;
                                            }
                                            for (int32 Level = 0; Level < ActiveLoops.Num(); ++Level)
                                            {
                                                const FBPIRPin* Index = FindPin(
                                                    *ActiveLoops[Level], TEXT("Index"), EGPD_Output);
                                                if (Index != nullptr &&
                                                    InputLink->SourceNode == ActiveLoops[Level]->Id &&
                                                    InputLink->SourcePin == Index->Id)
                                                {
                                                    Value.Kind =
                                                        EBPSemanticExpressionKind::VariableReference;
                                                    Value.Type.Kind = EBPSemanticTypeKind::Int32;
                                                    Value.VariableName = Level == 0 ?
                                                        TEXT("LoopIndex") : Level == 1 ?
                                                        TEXT("InnerIndex") :
                                                        FName(*FString::Printf(
                                                            TEXT("LoopIndex%d"), Level));
                                                    Value.SourceNodeId = ActiveLoops[Level]->Id;
                                                    return true;
                                                }
                                            }
                                            const FBPIRNode* Math = EventGraph->Nodes.FindByPredicate(
                                                [InputLink](const FBPIRNode& Candidate)
                                                {
                                                    return Candidate.Id == InputLink->SourceNode;
                                                });
                                            if (Math != nullptr &&
                                                Math->ClassPath ==
                                                    TEXT("/Script/BlueprintGraph.K2Node_VariableGet"))
                                            {
                                                // int32の反射済みメンバーGetterのみ算術式へ渡す。
                                                const FBPSemanticVariable* Variable =
                                                    FindSemanticVariable(OutIR.Variables,
                                                        Math->VariableName);
                                                const FBPIRPin* ValuePin = FindPin(
                                                    *Math, Math->VariableName, EGPD_Output);
                                                if (Variable == nullptr ||
                                                    Variable->Type.Kind != EBPSemanticTypeKind::Int32 ||
                                                    Variable->ReflectedCppType != TEXT("int32") ||
                                                    ValuePin == nullptr ||
                                                    ValuePin->Id != InputLink->SourcePin ||
                                                    UsedConverters.Contains(Math->Id))
                                                {
                                                    return false;
                                                }
                                                Value.Kind =
                                                    EBPSemanticExpressionKind::VariableReference;
                                                Value.Type = Variable->Type;
                                                Value.VariableName = Variable->Name;
                                                Value.SourceNodeId = Math->Id;
                                                Value.SourcePinId = ValuePin->Id;
                                                UsedConverters.Add(Math->Id);
                                                return true;
                                            }
                                            if (Math == nullptr ||
                                                Math->FunctionOwnerPath !=
                                                    TEXT("/Script/Engine.KismetMathLibrary") ||
                                                (Math->FunctionName != TEXT("Add_IntInt") &&
                                                    Math->FunctionName != TEXT("Subtract_IntInt") &&
                                                    Math->FunctionName != TEXT("Multiply_IntInt")) ||
                                                ArithmeticStack.Contains(Math->Id))
                                            {
                                                return false;
                                            }
                                            const FBPIRPin* ReturnPin = FindPin(
                                                *Math, TEXT("ReturnValue"), EGPD_Output);
                                            const FBPIRPin* Left = FindPin(
                                                *Math, TEXT("A"), EGPD_Input);
                                            const FBPIRPin* Right = FindPin(
                                                *Math, TEXT("B"), EGPD_Input);
                                            if (ReturnPin == nullptr || Left == nullptr ||
                                                Right == nullptr ||
                                                ReturnPin->Id != InputLink->SourcePin)
                                            {
                                                return false;
                                            }
                                            ArithmeticStack.Add(Math->Id);
                                            FBPSemanticExpression LeftValue;
                                            FBPSemanticExpression RightValue;
                                            if (LowerInteger(*Math, *Left, IntegerDepth + 1,
                                                    LeftValue) == false ||
                                                LowerInteger(*Math, *Right, IntegerDepth + 1,
                                                    RightValue) == false)
                                            {
                                                ArithmeticStack.Remove(Math->Id);
                                                return false;
                                            }
                                            ArithmeticStack.Remove(Math->Id);
                                            Value.Kind = EBPSemanticExpressionKind::FunctionCall;
                                            Value.Type.Kind = EBPSemanticTypeKind::Int32;
                                            Value.FunctionName = Math->FunctionName;
                                            Value.FunctionOwnerPath = Math->FunctionOwnerPath;
                                            Value.SourceNodeId = Math->Id;
                                            Value.Arguments.Add(MoveTemp(LeftValue));
                                            Value.Arguments.Add(MoveTemp(RightValue));
                                            UsedConverters.Add(Math->Id);
                                            return true;
                                        };
                                        FBPSemanticExpression IndexReference;
                                        if (LowerInteger(*Source, *Input, 0,
                                                IndexReference) == false)
                                        {
                                            return false;
                                        }
                                        Result.Kind = EBPSemanticExpressionKind::FunctionCall;
                                        Result.Type.Kind = EBPSemanticTypeKind::String;
                                        Result.FunctionName = TEXT("Conv_IntToString");
                                        Result.FunctionOwnerPath =
                                            TEXT("/Script/Engine.KismetStringLibrary");
                                        Result.SourceNodeId = Source->Id;
                                        Result.Arguments.Add(MoveTemp(IndexReference));
                                        UsedConverters.Add(Source->Id);
                                        return true;
                                    }
                                    if (Source->FunctionName != TEXT("Concat_StrStr") ||
                                        Source->FunctionOwnerPath !=
                                            TEXT("/Script/Engine.KismetStringLibrary") ||
                                        ConcatStack.Contains(Source->Id) ||
                                        UsedConverters.Contains(Source->Id))
                                    {
                                        return false;
                                    }
                                    const FBPIRPin* ReturnPin = FindPin(
                                        *Source, TEXT("ReturnValue"), EGPD_Output);
                                    const FBPIRPin* Left = FindPin(
                                        *Source, TEXT("A"), EGPD_Input);
                                    const FBPIRPin* Right = FindPin(
                                        *Source, TEXT("B"), EGPD_Input);
                                    if (ReturnPin == nullptr || Left == nullptr ||
                                        Right == nullptr || ReturnPin->Id != Link->SourcePin)
                                    {
                                        return false;
                                    }
                                    ConcatStack.Add(Source->Id);
                                    Result.Kind = EBPSemanticExpressionKind::FunctionCall;
                                    Result.Type.Kind = EBPSemanticTypeKind::String;
                                    Result.FunctionName = TEXT("Concat_StrStr");
                                    Result.FunctionOwnerPath =
                                        TEXT("/Script/Engine.KismetStringLibrary");
                                    Result.SourceNodeId = Source->Id;
                                    FBPSemanticExpression LeftValue;
                                    FBPSemanticExpression RightValue;
                                    if (LowerString(*Source, *Left, Depth + 1, LeftValue) == false ||
                                        LowerString(*Source, *Right, Depth + 1, RightValue) == false)
                                    {
                                        ConcatStack.Remove(Source->Id);
                                        return false;
                                    }
                                    Result.Arguments.Add(MoveTemp(LeftValue));
                                    Result.Arguments.Add(MoveTemp(RightValue));
                                    ConcatStack.Remove(Source->Id);
                                    UsedConverters.Add(Source->Id);
                                    return true;
                                };
                                FBPSemanticExpression Concat;
                                Concat.Kind = EBPSemanticExpressionKind::FunctionCall;
                                Concat.Type.Kind = EBPSemanticTypeKind::String;
                                Concat.FunctionName = TEXT("Concat_StrStr");
                                Concat.FunctionOwnerPath =
                                    TEXT("/Script/Engine.KismetStringLibrary");
                                Concat.SourceNodeId = Converter->Id;
                                FBPSemanticExpression LeftValue;
                                FBPSemanticExpression RightValue;
                                ConcatStack.Add(Converter->Id);
                                if (LowerString(*Converter, *A, 0, LeftValue) == false ||
                                    LowerString(*Converter, *B, 0, RightValue) == false)
                                {
                                    return false;
                                }
                                Concat.Arguments.Add(MoveTemp(LeftValue));
                                Concat.Arguments.Add(MoveTemp(RightValue));
                                Print.Expression.Arguments[1] = MoveTemp(Concat);
                                UsedConverters.Add(Converter->Id);
                                continue;
                            }
                            if (Converter == nullptr ||
                                Converter->FunctionName != TEXT("Conv_IntToString") ||
                                UsedConverters.Contains(Converter->Id))
                            {
                                return false;
                            }
                            const FBPIRPin* Output = FindPin(
                                *Converter, TEXT("ReturnValue"), EGPD_Output);
                            const FBPIRPin* Input = FindPin(*Converter, TEXT("InInt"), EGPD_Input);
                            if (Output == nullptr || Input == nullptr ||
                                Edge.SourcePin != Output->Id)
                            {
                                return false;
                            }
                            // Index・整数リテラル・四則演算のうち安全に生成可能な
                            // Add/Subtract/Multiplyを再帰的に解決する。
                            TSet<FGuid> ArithmeticStack;
                            TFunction<bool(const FBPIRNode&, const FBPIRPin&,
                                int32, FBPSemanticExpression&)> LowerInteger;
                            LowerInteger = [&](const FBPIRNode& Node, const FBPIRPin& Pin,
                                int32 Depth, FBPSemanticExpression& Result) -> bool
                            {
                                if (Depth > 16)
                                {
                                    return false;
                                }
                                // 整数演算の各入力も一意な供給元だけを許可する。
                                const FBPIRLink* InputLink = nullptr;
                                for (const FBPIRLink& Link : EventGraph->DataEdges)
                                {
                                    if (Link.TargetNode == Node.Id &&
                                        Link.TargetPin == Pin.Id)
                                    {
                                        if (InputLink != nullptr)
                                        {
                                            return false;
                                        }
                                        InputLink = &Link;
                                    }
                                }
                                if (InputLink == nullptr)
                                {
                                    int32 Number = 0;
                                    if (LexTryParseString(Number, *Pin.DefaultValue) == false)
                                    {
                                        return false;
                                    }
                                    Result = Literal(EBPSemanticTypeKind::Int32,
                                        LexToString(Number), Node, Pin.Name);
                                    return true;
                                }
                                for (int32 Level = 0; Level < ActiveLoops.Num(); ++Level)
                                {
                                    const FBPIRPin* Index = FindPin(
                                        *ActiveLoops[Level], TEXT("Index"), EGPD_Output);
                                    if (Index != nullptr &&
                                        InputLink->SourceNode == ActiveLoops[Level]->Id &&
                                        InputLink->SourcePin == Index->Id)
                                    {
                                        Result.Kind = EBPSemanticExpressionKind::VariableReference;
                                        Result.Type.Kind = EBPSemanticTypeKind::Int32;
                                        Result.VariableName = Level == 0 ? TEXT("LoopIndex") :
                                            Level == 1 ? TEXT("InnerIndex") :
                                            FName(*FString::Printf(TEXT("LoopIndex%d"), Level));
                                        Result.SourceNodeId = ActiveLoops[Level]->Id;
                                        // ローカルIndexにはメンバーGetter識別用のSourcePinIdを設定しない。
                                        return true;
                                    }
                                }
                                const FBPIRNode* Arithmetic = EventGraph->Nodes.FindByPredicate(
                                    [InputLink](const FBPIRNode& Candidate)
                                    {
                                        return Candidate.Id == InputLink->SourceNode;
                                    });
                                if (Arithmetic != nullptr &&
                                    Arithmetic->ClassPath ==
                                        TEXT("/Script/BlueprintGraph.K2Node_VariableGet"))
                                {
                                    // 単独Conv_IntToStringでも反射済みint32メンバーを参照する。
                                    const FBPSemanticVariable* Variable =
                                        FindSemanticVariable(OutIR.Variables,
                                            Arithmetic->VariableName);
                                    const FBPIRPin* ValuePin = FindPin(
                                        *Arithmetic, Arithmetic->VariableName, EGPD_Output);
                                    if (Variable == nullptr ||
                                        Variable->Type.Kind != EBPSemanticTypeKind::Int32 ||
                                        Variable->ReflectedCppType != TEXT("int32") ||
                                        ValuePin == nullptr ||
                                        ValuePin->Id != InputLink->SourcePin ||
                                        UsedConverters.Contains(Arithmetic->Id))
                                    {
                                        return false;
                                    }
                                    Result.Kind =
                                        EBPSemanticExpressionKind::VariableReference;
                                    Result.Type = Variable->Type;
                                    Result.VariableName = Variable->Name;
                                    Result.SourceNodeId = Arithmetic->Id;
                                    Result.SourcePinId = ValuePin->Id;
                                    UsedConverters.Add(Arithmetic->Id);
                                    return true;
                                }
                                if (Arithmetic == nullptr ||
                                    Arithmetic->FunctionOwnerPath !=
                                        TEXT("/Script/Engine.KismetMathLibrary") ||
                                    (Arithmetic->FunctionName != TEXT("Add_IntInt") &&
                                        Arithmetic->FunctionName != TEXT("Subtract_IntInt") &&
                                        Arithmetic->FunctionName != TEXT("Multiply_IntInt")) ||
                                    ArithmeticStack.Contains(Arithmetic->Id))
                                {
                                    return false;
                                }
                                const FBPIRPin* Output = FindPin(
                                    *Arithmetic, TEXT("ReturnValue"), EGPD_Output);
                                if (Output == nullptr || Output->Id != InputLink->SourcePin)
                                {
                                    return false;
                                }
                                ArithmeticStack.Add(Arithmetic->Id);
                                Result.Kind = EBPSemanticExpressionKind::FunctionCall;
                                Result.Type.Kind = EBPSemanticTypeKind::Int32;
                                Result.FunctionName = Arithmetic->FunctionName;
                                Result.FunctionOwnerPath = Arithmetic->FunctionOwnerPath;
                                Result.SourceNodeId = Arithmetic->Id;
                                const FName Names[] = { TEXT("A"), TEXT("B") };
                                for (const FName& Name : Names)
                                {
                                    const FBPIRPin* Operand = FindPin(
                                        *Arithmetic, Name, EGPD_Input);
                                    FBPSemanticExpression Argument;
                                    if (Operand == nullptr ||
                                        LowerInteger(*Arithmetic, *Operand, Depth + 1,
                                            Argument) == false)
                                    {
                                        ArithmeticStack.Remove(Arithmetic->Id);
                                        return false;
                                    }
                                    Result.Arguments.Add(MoveTemp(Argument));
                                }
                                ArithmeticStack.Remove(Arithmetic->Id);
                                UsedConverters.Add(Arithmetic->Id);
                                return true;
                            };
                            FBPSemanticExpression Reference;
                            if (LowerInteger(*Converter, *Input, 0, Reference) == false)
                            {
                                return false;
                            }
                            FBPSemanticExpression Conversion;
                            Conversion.Kind = EBPSemanticExpressionKind::FunctionCall;
                            Conversion.Type.Kind = EBPSemanticTypeKind::String;
                            Conversion.FunctionName = TEXT("Conv_IntToString");
                            Conversion.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
                            Conversion.SourceNodeId = Converter->Id;
                            Conversion.SourcePinId = Output->Id;
                            Conversion.Arguments.Add(MoveTemp(Reference));
                            Print.Expression.Arguments[1] = MoveTemp(Conversion);
                            UsedConverters.Add(Converter->Id);
                        }
                        Destination.Add(MoveTemp(Print));
                        const FBPIRNode* Next = nullptr;
                        if (NextExec(*Current, UEdGraphSchema_K2::PN_Then, Next) == false)
                        {
                            return false;
                        }
                        Current = Next;
                    }
                    else if (Current->MacroGraphName == TEXT("ForLoop") &&
                        Current->ClassPath == TEXT("/Script/BlueprintGraph.K2Node_MacroInstance") &&
                        Depth < 16)
                    {
                        const FBPIRPin* First = FindPin(*Current, TEXT("FirstIndex"), EGPD_Input);
                        const FBPIRPin* Last = FindPin(*Current, TEXT("LastIndex"), EGPD_Input);
                        int32 FirstValue = 0;
                        int32 LastValue = 0;
                        if (First == nullptr || Last == nullptr ||
                            LexTryParseString(FirstValue, *First->DefaultValue) == false ||
                            LexTryParseString(LastValue, *Last->DefaultValue) == false)
                        {
                            return false;
                        }
                        FBPSemanticStatement Loop;
                        Loop.Kind = EBPSemanticStatementKind::ForLoop;
                        Loop.SourceNodeId = Current->Id;
                        Loop.LoopIndexVariable = Depth == 0 ? TEXT("LoopIndex") :
                            Depth == 1 ? TEXT("InnerIndex") :
                            FName(*FString::Printf(TEXT("LoopIndex%d"), Depth));
                        Loop.LoopFirstIndex = Literal(EBPSemanticTypeKind::Int32,
                            LexToString(FirstValue), *Current, TEXT("FirstIndex"));
                        Loop.LoopLastIndex = Literal(EBPSemanticTypeKind::Int32,
                            LexToString(LastValue), *Current, TEXT("LastIndex"));
                        const FBPIRNode* Body = nullptr;
                        const FBPIRNode* Completed = nullptr;
                        if (NextExec(*Current, TEXT("LoopBody"), Body) == false ||
                            NextExec(*Current, TEXT("Completed"), Completed) == false ||
                            Body == nullptr)
                        {
                            return false;
                        }
                        ActiveLoops.Add(Current);
                        const bool bBodyLowered = LowerChain(
                            Body, Loop.LoopBodyStatements, Depth + 1);
                        ActiveLoops.Pop();
                        if (bBodyLowered == false)
                        {
                            return false;
                        }
                        Destination.Add(MoveTemp(Loop));
                        // 内側Completedは外側LoopBodyの続きとして生成する。
                        Current = Completed;
                    }
                    else
                    {
                        return false;
                    }
                }
                return true;
            };
            const FBPIRNode* First = nullptr;
            FBPSemanticFunction Function;
            Function.Name = TEXT("BeginPlay");
            Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
            FBPSemanticBasicBlock Block;
            Block.SourceEntryNodeId = Entry->Id;
            if (NextExec(*Entry, UEdGraphSchema_K2::PN_Then, First) &&
                First != nullptr && LowerChain(First, Block.Statements, 0) &&
                Visited.Num() + UsedConverters.Num() == EventGraph->Nodes.Num() - 1 &&
                EventGraph->ExecEdges.Num() == Visited.Num() &&
                UsedConverters.Num() == ConverterCount + ArithmeticCount + AppendCount + GetterCount &&
                EventGraph->DataEdges.Num() >= ConverterCount * 2 + AppendCount &&
                Block.Statements.Num() == 1 &&
                Block.Statements[0].Kind == EBPSemanticStatementKind::ForLoop)
            {
                Function.Blocks.Add(MoveTemp(Block));
                OutIR.Functions.Add(MoveTemp(Function));
                return true;
            }
            // 専用Loweringが扱う既存の二重ループ形状はそちらへ委譲する。
            // それ以外の未対応形状は後続の検証で診断する。
        }
    }
    if (NestedLoops.Num() == 2)
    {
        const FBPIRNode* Entry = nullptr;
        TArray<const FBPIRNode*> PrintsInNestedGraph;
        TArray<const FBPIRNode*> IndexConverters;
        TArray<const FBPIRNode*> IndexAdditions;
        for (const FBPIRNode& Node : EventGraph->Nodes)
        {
            if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") &&
                Node.FunctionName == TEXT("ReceiveBeginPlay"))
            {
                Entry = &Node;
            }
            else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
                Node.FunctionName == TEXT("PrintString"))
            {
                PrintsInNestedGraph.Add(&Node);
            }
            else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
                Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
                Node.FunctionName == TEXT("Conv_IntToString"))
            {
                IndexConverters.Add(&Node);
            }
            else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
                Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetMathLibrary") &&
                (Node.FunctionName == TEXT("Add_IntInt") ||
                    Node.FunctionName == TEXT("Subtract_IntInt") ||
                    Node.FunctionName == TEXT("Multiply_IntInt")))
            {
                IndexAdditions.Add(&Node);
            }
        }
        if (Entry == nullptr || PrintsInNestedGraph.IsEmpty() ||
            EventGraph->Nodes.Num() != 3 + PrintsInNestedGraph.Num() +
                IndexConverters.Num() + IndexAdditions.Num() ||
            EventGraph->ExecEdges.Num() != 2 + PrintsInNestedGraph.Num() ||
            EventGraph->DataEdges.Num() != IndexConverters.Num() * 2 +
                IndexAdditions.Num() * 2)
        {
            AddDiagnostic(OutIR, EventGraph->Name, FGuid(),
                TEXT("Nested ForLoop requires BeginPlay, two loops and one PrintString"));
            return false;
        }
        const FBPIRNode* Outer = nullptr;
        const FBPIRNode* Inner = nullptr;
        const FBPIRPin* EntryThen = FindPin(*Entry, UEdGraphSchema_K2::PN_Then, EGPD_Output);
        for (const FBPIRNode* Candidate : NestedLoops)
        {
            const FBPIRPin* Execute = FindPin(*Candidate, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
            if (EntryThen != nullptr && Execute != nullptr &&
                EventGraph->ExecEdges.ContainsByPredicate(
                    [Entry, EntryThen, Candidate, Execute](const FBPIRLink& Edge)
                    {
                        return Edge.SourceNode == Entry->Id && Edge.SourcePin == EntryThen->Id &&
                            Edge.TargetNode == Candidate->Id && Edge.TargetPin == Execute->Id;
                    }))
            {
                Outer = Candidate;
            }
        }
        if (Outer != nullptr)
        {
            Inner = NestedLoops[0] == Outer ? NestedLoops[1] : NestedLoops[0];
        }
        const auto HasExec = [EventGraph](const FBPIRNode& Source, const FName OutputName,
            const FBPIRNode& Target) -> bool
        {
            const FBPIRPin* Output = FindPin(Source, OutputName, EGPD_Output);
            const FBPIRPin* Execute = FindPin(Target, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
            return Output != nullptr && Execute != nullptr &&
                EventGraph->ExecEdges.ContainsByPredicate(
                    [&Source, Output, &Target, Execute](const FBPIRLink& Edge)
                    {
                        return Edge.SourceNode == Source.Id && Edge.SourcePin == Output->Id &&
                            Edge.TargetNode == Target.Id && Edge.TargetPin == Execute->Id;
                    });
        };
        const FBPIRNode* BodyPrint = nullptr;
        const FBPIRNode* InnerCompletedPrint = nullptr;
        const FBPIRNode* OuterCompletedPrint = nullptr;
        if (Outer != nullptr && Inner != nullptr)
        {
            for (const FBPIRNode* Candidate : PrintsInNestedGraph)
            {
                if (HasExec(*Inner, TEXT("LoopBody"), *Candidate))
                {
                    BodyPrint = Candidate;
                }
                if (HasExec(*Inner, TEXT("Completed"), *Candidate))
                {
                    InnerCompletedPrint = Candidate;
                }
                if (HasExec(*Outer, TEXT("Completed"), *Candidate))
                {
                    OuterCompletedPrint = Candidate;
                }
            }
        }
        TSet<FGuid> UsedPrintIds;
        if (BodyPrint != nullptr)
        {
            UsedPrintIds.Add(BodyPrint->Id);
        }
        if (InnerCompletedPrint != nullptr)
        {
            UsedPrintIds.Add(InnerCompletedPrint->Id);
        }
        if (OuterCompletedPrint != nullptr)
        {
            UsedPrintIds.Add(OuterCompletedPrint->Id);
        }
        if (Outer == nullptr || Inner == nullptr || BodyPrint == nullptr ||
            HasExec(*Outer, TEXT("LoopBody"), *Inner) == false ||
            UsedPrintIds.Num() != PrintsInNestedGraph.Num() ||
            (InnerCompletedPrint != nullptr && InnerCompletedPrint == BodyPrint) ||
            (OuterCompletedPrint != nullptr &&
                (OuterCompletedPrint == BodyPrint ||
                    OuterCompletedPrint == InnerCompletedPrint)))
        {
            AddDiagnostic(OutIR, EventGraph->Name, FGuid(),
                TEXT("Nested ForLoop Exec flow is unsupported"));
            return false;
        }
        const auto MakeLoop = [&](const FBPIRNode& Node, const FName IndexName,
            FBPSemanticStatement& Result) -> bool
        {
            const FBPIRPin* First = FindPin(Node, TEXT("FirstIndex"), EGPD_Input);
            const FBPIRPin* Last = FindPin(Node, TEXT("LastIndex"), EGPD_Input);
            const FBPIRPin* Index = FindPin(Node, TEXT("Index"), EGPD_Output);
            const FBPIRPin* Completed = FindPin(Node, TEXT("Completed"), EGPD_Output);
            int32 FirstValue = 0;
            int32 LastValue = 0;
            if (First == nullptr || Last == nullptr || Index == nullptr || Completed == nullptr ||
                LexTryParseString(FirstValue, *First->DefaultValue) == false ||
                LexTryParseString(LastValue, *Last->DefaultValue) == false)
            {
                return false;
            }
            Result.Kind = EBPSemanticStatementKind::ForLoop;
            Result.SourceNodeId = Node.Id;
            Result.LoopIndexVariable = IndexName;
            Result.LoopFirstIndex = Literal(
                EBPSemanticTypeKind::Int32, LexToString(FirstValue), Node, TEXT("FirstIndex"));
            Result.LoopLastIndex = Literal(
                EBPSemanticTypeKind::Int32, LexToString(LastValue), Node, TEXT("LastIndex"));
            return true;
        };
        FBPSemanticStatement OuterStatement;
        FBPSemanticStatement InnerStatement;
        if (MakeLoop(*Outer, TEXT("LoopIndex"), OuterStatement) == false ||
            MakeLoop(*Inner, TEXT("InnerIndex"), InnerStatement) == false)
        {
            AddDiagnostic(OutIR, EventGraph->Name, FGuid(),
                TEXT("Nested ForLoop bounds must be int32 literals"));
            return false;
        }
        // Completedは内側ループ終了時と外側ループ終了時でスコープが異なる。
        // 各出力先を明示的に分離し、PrintStringの重複実行を防ぐ。
        const auto LowerPrint = [&](const FBPIRNode& Node,
            FBPSemanticStatement& Result) -> bool
        {
            FBPPrintStringCall Values;
            if (AnalyzeBranchPrint(
                    GraphIR, *EventGraph, *Entry, Node, Values, OutIR) == false)
            {
                return false;
            }
            Result.Kind = EBPSemanticStatementKind::Expression;
            Result.SourceNodeId = Node.Id;
            Result.Expression = MakePrintStringExpression(
                Values, Node, GraphIR.ParentClassPath);
            // 各PrintStringに接続されたConverterを独立に解決する。
            // 内側Completedでは外側Indexのみ有効。外側CompletedでのIndex参照は拒否する。
            const FBPIRPin* TextPin = FindPin(Node, TEXT("InString"), EGPD_Input);
            for (const FBPIRNode* Converter : IndexConverters)
            {
                const FBPIRPin* Input = FindPin(*Converter, TEXT("InInt"), EGPD_Input);
                const FBPIRPin* Output = FindPin(*Converter, TEXT("ReturnValue"), EGPD_Output);
                if (Input == nullptr || Output == nullptr || TextPin == nullptr)
                {
                    return false;
                }
                const bool bFeedsPrint = EventGraph->DataEdges.ContainsByPredicate(
                    [Converter, Output, &Node, TextPin](const FBPIRLink& Edge)
                    {
                        return Edge.SourceNode == Converter->Id &&
                            Edge.SourcePin == Output->Id &&
                            Edge.TargetNode == Node.Id && Edge.TargetPin == TextPin->Id;
                    });
                if (bFeedsPrint == false)
                {
                    continue;
                }
                if (OuterCompletedPrint != nullptr && Node.Id == OuterCompletedPrint->Id)
                {
                    return false;
                }
                const FBPIRNode* IndexOwner = nullptr;
                FName IndexName;
                const FBPIRNode* Candidates[] = { Outer, Inner };
                const FName Names[] = { TEXT("LoopIndex"), TEXT("InnerIndex") };
                for (int32 I = 0; I < 2; ++I)
                {
                    const FBPIRPin* Index = FindPin(*Candidates[I], TEXT("Index"), EGPD_Output);
                    if (Index != nullptr && EventGraph->DataEdges.ContainsByPredicate(
                            [Candidates, I, Index, Converter, Input](const FBPIRLink& Edge)
                            {
                                return Edge.SourceNode == Candidates[I]->Id &&
                                    Edge.SourcePin == Index->Id &&
                                    Edge.TargetNode == Converter->Id &&
                                    Edge.TargetPin == Input->Id;
                            }))
                    {
                        if (IndexOwner != nullptr)
                        {
                            return false;
                        }
                        IndexOwner = Candidates[I];
                        IndexName = Names[I];
                    }
                }
                if (IndexOwner == nullptr && IndexAdditions.IsEmpty())
                {
                    return false;
                }
                if (InnerCompletedPrint != nullptr &&
                    Node.Id == InnerCompletedPrint->Id && IndexOwner == Inner)
                {
                    return false;
                }
                FBPSemanticExpression Conversion;
                Conversion.Kind = EBPSemanticExpressionKind::FunctionCall;
                Conversion.Type.Kind = EBPSemanticTypeKind::String;
                Conversion.FunctionName = TEXT("Conv_IntToString");
                Conversion.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
                Conversion.SourceNodeId = Converter->Id;
                Conversion.SourcePinId = Output->Id;
                FBPSemanticExpression Reference;
                Reference.Kind = EBPSemanticExpressionKind::VariableReference;
                Reference.Type.Kind = EBPSemanticTypeKind::Int32;
                if (IndexOwner != nullptr)
                {
                    Reference.VariableName = IndexName;
                    Reference.SourceNodeId = IndexOwner->Id;
                    Reference.SourcePinId = FGuid();
                    Conversion.Arguments.Add(MoveTemp(Reference));
                }
                else
                {
                    // Index参照と二項演算を再帰的に解析する。
                    // 共有ノード・循環・未接続入力は推測せずに失敗させる。
                    if (Node.Id != BodyPrint->Id)
                    {
                        return false;
                    }
                    TSet<FGuid> Visiting;
                    TFunction<bool(const FBPIRNode&, const FBPIRPin&, int32,
                        FBPSemanticExpression&)> LowerInteger;
                    LowerInteger = [&](const FBPIRNode& Target, const FBPIRPin& TargetPin,
                        int32 Depth, FBPSemanticExpression& Expression) -> bool
                    {
                        if (Depth > 16)
                        {
                            return false;
                        }
                        const FBPIRLink* Link = EventGraph->DataEdges.FindByPredicate(
                            [&Target, &TargetPin](const FBPIRLink& Edge)
                            {
                                return Edge.TargetNode == Target.Id &&
                                    Edge.TargetPin == TargetPin.Id;
                            });
                        if (Link == nullptr)
                        {
                            return false;
                        }
                        const FBPIRNode* Candidates[] = { Outer, Inner };
                        const FName Names[] = { TEXT("LoopIndex"), TEXT("InnerIndex") };
                        for (int32 I = 0; I < 2; ++I)
                        {
                            const FBPIRPin* IndexPin = FindPin(
                                *Candidates[I], TEXT("Index"), EGPD_Output);
                            if (IndexPin != nullptr &&
                                Link->SourceNode == Candidates[I]->Id &&
                                Link->SourcePin == IndexPin->Id)
                            {
                                Expression.Kind = EBPSemanticExpressionKind::VariableReference;
                                Expression.Type.Kind = EBPSemanticTypeKind::Int32;
                                Expression.VariableName = Names[I];
                                Expression.SourceNodeId = Candidates[I]->Id;
                                Expression.SourcePinId = FGuid();
                                return true;
                            }
                        }
                        for (const FBPIRNode* Arithmetic : IndexAdditions)
                        {
                            const FBPIRPin* Output = FindPin(
                                *Arithmetic, TEXT("ReturnValue"), EGPD_Output);
                            if (Output == nullptr ||
                                Link->SourceNode != Arithmetic->Id ||
                                Link->SourcePin != Output->Id)
                            {
                                continue;
                            }
                            if (Visiting.Contains(Arithmetic->Id))
                            {
                                return false;
                            }
                            Visiting.Add(Arithmetic->Id);
                            Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
                            Expression.Type.Kind = EBPSemanticTypeKind::Int32;
                            Expression.FunctionName = Arithmetic->FunctionName;
                            Expression.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
                            Expression.SourceNodeId = Arithmetic->Id;
                            const FName Inputs[] = { TEXT("A"), TEXT("B") };
                            bool bValid = true;
                            for (const FName& Name : Inputs)
                            {
                                const FBPIRPin* Operand = FindPin(
                                    *Arithmetic, Name, EGPD_Input);
                                FBPSemanticExpression Argument;
                                if (Operand == nullptr ||
                                    LowerInteger(*Arithmetic, *Operand, Depth + 1,
                                        Argument) == false)
                                {
                                    bValid = false;
                                    break;
                                }
                                Expression.Arguments.Add(MoveTemp(Argument));
                            }
                            Visiting.Remove(Arithmetic->Id);
                            return bValid;
                        }
                        return false;
                    };
                    FBPSemanticExpression ArithmeticExpression;
                    if (LowerInteger(*Converter, *Input, 0, ArithmeticExpression) == false ||
                        ArithmeticExpression.Kind != EBPSemanticExpressionKind::FunctionCall)
                    {
                        return false;
                    }
                    Conversion.Arguments.Add(MoveTemp(ArithmeticExpression));
                }
                Result.Expression.Arguments[1] = MoveTemp(Conversion);
            }
            return true;
        };
        // すべての変換ノードに一意な接続先を要求する。
        // 未使用・重複接続を黙って捨てると生成コードの意味が変わるため拒否する。
        TSet<FGuid> ConverterConsumers;
        for (const FBPIRNode* Converter : IndexConverters)
        {
            const FBPIRPin* Output = FindPin(*Converter, TEXT("ReturnValue"), EGPD_Output);
            const FBPIRPin* Input = FindPin(*Converter, TEXT("InInt"), EGPD_Input);
            int32 InputEdges = 0;
            int32 OutputEdges = 0;
            for (const FBPIRLink& Edge : EventGraph->DataEdges)
            {
                if (Input != nullptr && Edge.TargetNode == Converter->Id &&
                    Edge.TargetPin == Input->Id)
                {
                    ++InputEdges;
                }
                if (Output != nullptr && Edge.SourceNode == Converter->Id &&
                    Edge.SourcePin == Output->Id)
                {
                    ++OutputEdges;
                    if (ConverterConsumers.Contains(Edge.TargetNode))
                    {
                        return false;
                    }
                    ConverterConsumers.Add(Edge.TargetNode);
                }
            }
            if (InputEdges != 1 || OutputEdges != 1)
            {
                AddDiagnostic(OutIR, EventGraph->Name, Converter->Id,
                    TEXT("Nested ForLoop Index converter requires one input and one output"));
                return false;
            }
        }
        FBPSemanticStatement PrintStatement;
        if (LowerPrint(*BodyPrint, PrintStatement) == false)
        {
            return false;
        }
        InnerStatement.LoopBodyStatements.Add(MoveTemp(PrintStatement));
        if (InnerCompletedPrint != nullptr)
        {
            FBPSemanticStatement CompletedStatement;
            if (LowerPrint(*InnerCompletedPrint, CompletedStatement) == false)
            {
                return false;
            }
            InnerStatement.CompletedStatements.Add(MoveTemp(CompletedStatement));
        }
        OuterStatement.LoopBodyStatements.Add(MoveTemp(InnerStatement));
        if (OuterCompletedPrint != nullptr)
        {
            FBPSemanticStatement CompletedStatement;
            if (LowerPrint(*OuterCompletedPrint, CompletedStatement) == false)
            {
                return false;
            }
            OuterStatement.CompletedStatements.Add(MoveTemp(CompletedStatement));
        }
        FBPSemanticFunction Function;
        Function.Name = TEXT("BeginPlay");
        Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
        FBPSemanticBasicBlock Block;
        Block.SourceEntryNodeId = Entry->Id;
        Block.Statements.Add(MoveTemp(OuterStatement));
        Function.Blocks.Add(MoveTemp(Block));
        OutIR.Functions.Add(MoveTemp(Function));
        return true;
    }
    const FBPIRNode* BeginPlay = nullptr;
    const FBPIRNode* Loop = nullptr;
    const FBPIRNode* IndexToString = nullptr;
    const FBPIRNode* BodyBranch = nullptr;
    TArray<const FBPIRNode*> BranchNodes;
    const FBPIRNode* BodyComparison = nullptr;
    TArray<const FBPIRNode*> ComparisonNodes;
    TArray<const FBPIRNode*> BodyArithmeticNodes;
    TArray<const FBPIRNode*> Prints;
    for (const FBPIRNode& Node : EventGraph->Nodes)
    {
        if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") &&
            Node.FunctionName == TEXT("ReceiveBeginPlay"))
        {
            BeginPlay = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_MacroInstance") &&
            Node.MacroGraphName == TEXT("ForLoop"))
        {
            Loop = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("PrintString"))
        {
            Prints.Add(&Node);
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionName == TEXT("Conv_IntToString") &&
            Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary"))
        {
            IndexToString = &Node;
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
        {
            BodyBranch = &Node;
            BranchNodes.Add(&Node);
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetMathLibrary"))
        {
            FBPNumericComparison Comparison;
            TArray<FString> Diagnostics;
            if (FBPNumericComparisonResolver::Resolve(
                Node.FunctionOwnerPath, Node.FunctionName, Comparison, Diagnostics))
            {
                BodyComparison = &Node;
                ComparisonNodes.Add(&Node);
            }
            else
            {
                BodyArithmeticNodes.Add(&Node);
            }
        }
        else
        {
            AddDiagnostic(OutIR, EventGraph->Name, Node.Id,
                TEXT("ForLoop conversion contains an unsupported node"));
            return false;
        }
    }
    if (BeginPlay == nullptr || Loop == nullptr || Prints.IsEmpty() ||
        EventGraph->Nodes.Num() != 2 + Prints.Num() + (IndexToString != nullptr ? 1 : 0) +
            BranchNodes.Num() + ComparisonNodes.Num() +
            BodyArithmeticNodes.Num() ||
        EventGraph->ExecEdges.Num() != 1 + Prints.Num() + BranchNodes.Num() ||
        (BodyBranch != nullptr && IndexToString != nullptr) ||
        (ComparisonNodes.IsEmpty()
            ? (IndexToString == nullptr ? EventGraph->DataEdges.IsEmpty() == false : EventGraph->DataEdges.Num() != 2)
            : (EventGraph->DataEdges.Num() < ComparisonNodes.Num() ||
                EventGraph->DataEdges.Num() > ComparisonNodes.Num() * 3 +
                    BodyArithmeticNodes.Num() * 3)))
    {
        AddDiagnostic(OutIR, EventGraph->Name, FGuid(),
            TEXT("ForLoop conversion requires BeginPlay, ForLoop, one body PrintString, and optional Completed PrintString"));
        return false;
    }

    const FBPIRPin* BeginThen = FindPin(*BeginPlay, UEdGraphSchema_K2::PN_Then, EGPD_Output);
    const FBPIRPin* Execute = FindPin(*Loop, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    const FBPIRPin* FirstIndex = FindPin(*Loop, TEXT("FirstIndex"), EGPD_Input);
    const FBPIRPin* LastIndex = FindPin(*Loop, TEXT("LastIndex"), EGPD_Input);
    const FBPIRPin* LoopBody = FindPin(*Loop, TEXT("LoopBody"), EGPD_Output);
    const FBPIRPin* LoopIndex = FindPin(*Loop, TEXT("Index"), EGPD_Output);
    const FBPIRPin* Completed = FindPin(*Loop, TEXT("Completed"), EGPD_Output);
    if (BeginThen == nullptr || Execute == nullptr || FirstIndex == nullptr || LastIndex == nullptr ||
        LoopBody == nullptr || LoopIndex == nullptr || Completed == nullptr)
    {
        AddDiagnostic(OutIR, EventGraph->Name, Loop->Id, TEXT("ForLoop pins are incomplete"));
        return false;
    }
    int32 FirstValue = 0;
    int32 LastValue = 0;
    if (LexTryParseString(FirstValue, *FirstIndex->DefaultValue) == false ||
        LexTryParseString(LastValue, *LastIndex->DefaultValue) == false)
    {
        AddDiagnostic(OutIR, EventGraph->Name, Loop->Id,
            TEXT("ForLoop bounds must be unconnected int32 literals"));
        return false;
    }

    bool bBeginConnected = false;
    const FBPIRNode* BodyPrint = nullptr;
    const FBPIRNode* CompletedPrint = nullptr;
    const auto FindPrintById = [&Prints](const FGuid& Id)
    {
        const FBPIRNode* const* Found = Prints.FindByPredicate(
            [&Id](const FBPIRNode* Candidate) { return Candidate != nullptr && Candidate->Id == Id; });
        return Found != nullptr ? *Found : nullptr;
    };
    for (const FBPIRLink& Edge : EventGraph->ExecEdges)
    {
        bBeginConnected |= Edge.SourceNode == BeginPlay->Id && Edge.SourcePin == BeginThen->Id &&
            Edge.TargetNode == Loop->Id && Edge.TargetPin == Execute->Id;
        if (Edge.SourceNode == Loop->Id && Edge.SourcePin == LoopBody->Id)
        {
            // ネスト時は最後に列挙されたBranchではなく、LoopBody直結を根にする。
            for (const FBPIRNode* Candidate : BranchNodes)
            {
                if (Candidate->Id == Edge.TargetNode)
                {
                    BodyBranch = Candidate;
                    break;
                }
            }
            BodyPrint = FindPrintById(Edge.TargetNode);
            if (BodyPrint == nullptr && BodyBranch != nullptr && Edge.TargetNode != BodyBranch->Id)
            {
                BodyBranch = nullptr;
            }
        }
        else if (Edge.SourceNode == Loop->Id && Edge.SourcePin == Completed->Id)
        {
            CompletedPrint = FindPrintById(Edge.TargetNode);
        }
    }
    const FBPIRPin* BodyExecute = BodyPrint != nullptr
        ? FindPin(*BodyPrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input)
        : (BodyBranch != nullptr
            ? FindPin(*BodyBranch, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr);
    const FBPIRPin* CompletedExecute = CompletedPrint != nullptr
        ? FindPin(*CompletedPrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
    const bool bBodyConnected = EventGraph->ExecEdges.ContainsByPredicate(
        [Loop, LoopBody, BodyPrint, BodyBranch, BodyExecute](const FBPIRLink& Edge)
        {
            const FGuid TargetId = BodyPrint != nullptr ? BodyPrint->Id :
                (BodyBranch != nullptr ? BodyBranch->Id : FGuid());
            return TargetId.IsValid() && BodyExecute != nullptr && Edge.SourceNode == Loop->Id &&
                Edge.SourcePin == LoopBody->Id && Edge.TargetNode == TargetId &&
                Edge.TargetPin == BodyExecute->Id;
        });
    const bool bCompletedConnected = CompletedPrint == nullptr ||
        EventGraph->ExecEdges.ContainsByPredicate(
            [Loop, Completed, CompletedPrint, CompletedExecute](const FBPIRLink& Edge)
            {
                return CompletedExecute != nullptr && Edge.SourceNode == Loop->Id &&
                    Edge.SourcePin == Completed->Id && Edge.TargetNode == CompletedPrint->Id &&
                    Edge.TargetPin == CompletedExecute->Id;
            });
    if (bBeginConnected == false || bBodyConnected == false || bCompletedConnected == false ||
        (BodyPrint != nullptr && BodyPrint == CompletedPrint))
    {
        AddDiagnostic(OutIR, EventGraph->Name, Loop->Id,
            TEXT("ForLoop Exec flow does not match the supported shape"));
        return false;
    }

    TArray<const FBPIRNode*> OrderedBodyPrints;
    TSet<FGuid> VisitedBodyNodes;
    const FBPIRNode* CurrentBodyPrint = BodyPrint;
    while (CurrentBodyPrint != nullptr)
    {
        if (VisitedBodyNodes.Contains(CurrentBodyPrint->Id))
        {
            AddDiagnostic(OutIR, EventGraph->Name, CurrentBodyPrint->Id,
                TEXT("ForLoop body contains an Exec cycle"));
            return false;
        }
        VisitedBodyNodes.Add(CurrentBodyPrint->Id);
        OrderedBodyPrints.Add(CurrentBodyPrint);
        const FBPIRPin* ThenPin = FindPin(
            *CurrentBodyPrint, UEdGraphSchema_K2::PN_Then, EGPD_Output);
        if (ThenPin == nullptr)
        {
            break;
        }
        const FBPIRLink* NextEdge = EventGraph->ExecEdges.FindByPredicate(
            [CurrentBodyPrint, ThenPin](const FBPIRLink& Edge)
            {
                return Edge.SourceNode == CurrentBodyPrint->Id && Edge.SourcePin == ThenPin->Id;
            });
        if (NextEdge == nullptr)
        {
            break;
        }
        CurrentBodyPrint = FindPrintById(NextEdge->TargetNode);
        const FBPIRPin* NextExecute = CurrentBodyPrint != nullptr
            ? FindPin(*CurrentBodyPrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
        if (CurrentBodyPrint == nullptr || CurrentBodyPrint == CompletedPrint ||
            NextExecute == nullptr || NextEdge->TargetPin != NextExecute->Id)
        {
            AddDiagnostic(OutIR, EventGraph->Name, NextEdge->SourceNode,
                TEXT("ForLoop body Exec chain contains an unsupported target"));
            return false;
        }
    }
    // Completed の直列PrintStringをExec接続順に収集し、循環と分岐を拒否する。
    TArray<const FBPIRNode*> OrderedCompletedPrints;
    TSet<FGuid> VisitedCompleted;
    const FBPIRNode* CurrentCompleted = CompletedPrint;
    while (CurrentCompleted != nullptr)
    {
        if (VisitedCompleted.Contains(CurrentCompleted->Id) ||
            VisitedBodyNodes.Contains(CurrentCompleted->Id))
        {
            AddDiagnostic(OutIR, EventGraph->Name, CurrentCompleted->Id,
                TEXT("ForLoop Completed contains an Exec cycle or shared body node"));
            return false;
        }
        VisitedCompleted.Add(CurrentCompleted->Id);
        OrderedCompletedPrints.Add(CurrentCompleted);
        const FBPIRPin* ThenPin = FindPin(*CurrentCompleted, UEdGraphSchema_K2::PN_Then, EGPD_Output);
        const FBPIRLink* NextEdge = nullptr;
        for (const FBPIRLink& Edge : EventGraph->ExecEdges)
        {
            if (ThenPin != nullptr && Edge.SourceNode == CurrentCompleted->Id &&
                Edge.SourcePin == ThenPin->Id)
            {
                if (NextEdge != nullptr)
                {
                    AddDiagnostic(OutIR, EventGraph->Name, CurrentCompleted->Id,
                        TEXT("ForLoop Completed has multiple Exec successors"));
                    return false;
                }
                NextEdge = &Edge;
            }
        }
        if (NextEdge == nullptr)
        {
            break;
        }
        const FBPIRNode* NextPrint = FindPrintById(NextEdge->TargetNode);
        const FBPIRPin* NextExecute = NextPrint != nullptr
            ? FindPin(*NextPrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
        if (NextExecute == nullptr || NextEdge->TargetPin != NextExecute->Id)
        {
            AddDiagnostic(OutIR, EventGraph->Name, CurrentCompleted->Id,
                TEXT("ForLoop Completed contains an unsupported Exec target"));
            return false;
        }
        CurrentCompleted = NextPrint;
    }
    if (BodyBranch == nullptr &&
        OrderedBodyPrints.Num() + OrderedCompletedPrints.Num() != Prints.Num())
    {
        AddDiagnostic(OutIR, EventGraph->Name, Loop->Id,
            TEXT("ForLoop contains a disconnected or ambiguous PrintString"));
        return false;
    }

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    Block.SourceEntryNodeId = BeginPlay->Id;
    FBPSemanticStatement LoopStatement;
    LoopStatement.Kind = EBPSemanticStatementKind::ForLoop;
    LoopStatement.SourceNodeId = Loop->Id;
    LoopStatement.LoopIndexVariable = TEXT("LoopIndex");
    LoopStatement.LoopFirstIndex = Literal(
        EBPSemanticTypeKind::Int32, LexToString(FirstValue), *Loop, TEXT("FirstIndex"));
    LoopStatement.LoopLastIndex = Literal(
        EBPSemanticTypeKind::Int32, LexToString(LastValue), *Loop, TEXT("LastIndex"));
    // 複数Branchの場合は、Execの木構造を先に検証してからSemantic IRへ変換する。
    // 比較式・データ接続を含む複雑な経路は既存の単一Branch経路と分離する。
    if (BranchNodes.Num() > 1)
    {
        if (IndexToString != nullptr || BodyPrint != nullptr || BranchNodes.Num() > 16 ||
            (ComparisonNodes.IsEmpty() && EventGraph->DataEdges.IsEmpty() == false))
        {
            AddDiagnostic(OutIR, EventGraph->Name, Loop->Id,
                TEXT("Nested ForLoop Branch supports bool literals and int32 comparisons"));
            return false;
        }
        const FBPIRNode* RootBranch = nullptr;
        const FBPIRLink* RootEdge = EventGraph->ExecEdges.FindByPredicate(
            [Loop, LoopBody](const FBPIRLink& Edge)
            {
                return Edge.SourceNode == Loop->Id && Edge.SourcePin == LoopBody->Id;
            });
        for (const FBPIRNode* Candidate : BranchNodes)
        {
            if (RootEdge != nullptr && Candidate->Id == RootEdge->TargetNode)
            {
                RootBranch = Candidate;
            }
        }
        TSet<FGuid> VisitedBranches;
        TSet<FGuid> VisitedPrints;
        TSet<FGuid> UsedComparisons;
        TSet<FGuid> UsedArithmeticNodes;
        TSet<FGuid> ActiveArithmeticNodes;
        int32 ConsumedConditionEdges = 0;
        // 各Branchの比較オペランドを再帰的に解析する。
        // ノード共有・循環・未接続の算術ノードを拒否して意味の曖昧な変換を防ぐ。
        TFunction<bool(const FBPIRNode&, const FBPIRPin&, int32,
            FBPSemanticExpression&)> BuildNumericOperand;
        BuildNumericOperand = [&](const FBPIRNode& Owner, const FBPIRPin& Input,
            int32 Depth, FBPSemanticExpression& Result) -> bool
        {
            if (Depth > 16 || FBPNumericComparisonResolver::MatchesPinType(
                    Input.Category, Input.SubCategory, EBPSemanticTypeKind::Int32) == false)
            {
                return false;
            }
            const FBPIRLink* Source = nullptr;
            for (const FBPIRLink& Edge : EventGraph->DataEdges)
            {
                if (Edge.TargetNode == Owner.Id && Edge.TargetPin == Input.Id)
                {
                    if (Source != nullptr)
                    {
                        return false;
                    }
                    Source = &Edge;
                }
            }
            if (Source == nullptr)
            {
                FString Value;
                if (FBPNumericComparisonResolver::CanonicalizeLiteral(
                        EBPSemanticTypeKind::Int32, Input.DefaultValue, Value) == false)
                {
                    return false;
                }
                Result = Literal(EBPSemanticTypeKind::Int32, Value, Owner, Input.Name);
                return true;
            }
            ++ConsumedConditionEdges;
            if (Source->SourceNode == Loop->Id && Source->SourcePin == LoopIndex->Id)
            {
                Result.Kind = EBPSemanticExpressionKind::VariableReference;
                Result.Type.Kind = EBPSemanticTypeKind::Int32;
                Result.VariableName = LoopStatement.LoopIndexVariable;
                Result.SourceNodeId = Loop->Id;
                Result.SourcePinId = FGuid();
                return true;
            }
            const FBPIRNode* Arithmetic = nullptr;
            for (const FBPIRNode* Candidate : BodyArithmeticNodes)
            {
                if (Candidate->Id == Source->SourceNode)
                {
                    Arithmetic = Candidate;
                    break;
                }
            }
            if (Arithmetic == nullptr || UsedArithmeticNodes.Contains(Arithmetic->Id) ||
                ActiveArithmeticNodes.Contains(Arithmetic->Id))
            {
                return false;
            }
            FBPNumericArithmeticOperation Operation;
            TArray<FString> Diagnostics;
            if (FBPNumericArithmeticResolver::Resolve(Arithmetic->FunctionOwnerPath,
                    Arithmetic->FunctionName, Operation, Diagnostics) == false ||
                Operation.OperandType != EBPSemanticTypeKind::Int32)
            {
                return false;
            }
            const FBPIRPin* Output = FindPin(*Arithmetic, TEXT("ReturnValue"), EGPD_Output);
            const FBPIRPin* A = FindPin(*Arithmetic, TEXT("A"), EGPD_Input);
            const FBPIRPin* B = FindPin(*Arithmetic, TEXT("B"), EGPD_Input);
            if (Output == nullptr || A == nullptr || B == nullptr ||
                Source->SourcePin != Output->Id)
            {
                return false;
            }
            ActiveArithmeticNodes.Add(Arithmetic->Id);
            Result.Kind = EBPSemanticExpressionKind::FunctionCall;
            Result.Type.Kind = EBPSemanticTypeKind::Int32;
            Result.FunctionName = Arithmetic->FunctionName;
            Result.FunctionOwnerPath = Arithmetic->FunctionOwnerPath;
            Result.SourceNodeId = Arithmetic->Id;
            Result.SourcePinId = Output->Id;
            FBPSemanticExpression Left;
            FBPSemanticExpression Right;
            if (BuildNumericOperand(*Arithmetic, *A, Depth + 1, Left) == false ||
                BuildNumericOperand(*Arithmetic, *B, Depth + 1, Right) == false)
            {
                return false;
            }
            Result.Arguments.Add(MoveTemp(Left));
            Result.Arguments.Add(MoveTemp(Right));
            ActiveArithmeticNodes.Remove(Arithmetic->Id);
            UsedArithmeticNodes.Add(Arithmetic->Id);
            return true;
        };
        TFunction<bool(const FBPIRNode&, int32, FBPSemanticStatement&)> BuildBranch;
        BuildBranch = [&](const FBPIRNode& Current, int32 Depth,
            FBPSemanticStatement& OutStatement) -> bool
        {
            if (Depth > 16 || VisitedBranches.Contains(Current.Id))
            {
                return false;
            }
            const FBPIRPin* Condition = FindPin(Current, TEXT("Condition"), EGPD_Input);
            if (Condition == nullptr)
            {
                return false;
            }
            VisitedBranches.Add(Current.Id);
            OutStatement.Kind = EBPSemanticStatementKind::Branch;
            OutStatement.SourceNodeId = Current.Id;
            const FBPIRLink* ConditionEdge = nullptr;
            for (const FBPIRLink& Edge : EventGraph->DataEdges)
            {
                if (Edge.TargetNode == Current.Id && Edge.TargetPin == Condition->Id)
                {
                    if (ConditionEdge != nullptr)
                    {
                        return false;
                    }
                    ConditionEdge = &Edge;
                }
            }
            if (ConditionEdge == nullptr)
            {
                if (Condition->DefaultValue != TEXT("true") &&
                    Condition->DefaultValue != TEXT("false"))
                {
                    return false;
                }
                OutStatement.Expression = Literal(
                    EBPSemanticTypeKind::Bool, Condition->DefaultValue, Current, TEXT("Condition"));
            }
            else
            {
                // 比較結果は1つのBranchだけに接続できる。Index参照とint32定数を検証する。
                const FBPIRNode* CurrentComparison = nullptr;
                for (const FBPIRNode* Candidate : ComparisonNodes)
                {
                    if (Candidate->Id == ConditionEdge->SourceNode)
                    {
                        CurrentComparison = Candidate;
                        break;
                    }
                }
                if (CurrentComparison == nullptr ||
                    UsedComparisons.Contains(CurrentComparison->Id))
                {
                    return false;
                }
                FBPNumericComparison Comparison;
                TArray<FString> Diagnostics;
                if (FBPNumericComparisonResolver::Resolve(CurrentComparison->FunctionOwnerPath,
                        CurrentComparison->FunctionName, Comparison, Diagnostics) == false ||
                    Comparison.OperandType != EBPSemanticTypeKind::Int32)
                {
                    return false;
                }
                const FBPIRPin* ReturnPin = FindPin(*CurrentComparison, TEXT("ReturnValue"), EGPD_Output);
                const FBPIRPin* A = FindPin(*CurrentComparison, TEXT("A"), EGPD_Input);
                const FBPIRPin* B = FindPin(*CurrentComparison, TEXT("B"), EGPD_Input);
                if (ReturnPin == nullptr || A == nullptr || B == nullptr ||
                    ConditionEdge->SourceNode != CurrentComparison->Id ||
                    ConditionEdge->SourcePin != ReturnPin->Id)
                {
                    return false;
                }
                OutStatement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
                OutStatement.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
                OutStatement.Expression.FunctionName = CurrentComparison->FunctionName;
                OutStatement.Expression.FunctionOwnerPath = CurrentComparison->FunctionOwnerPath;
                OutStatement.Expression.SourceNodeId = CurrentComparison->Id;
                OutStatement.Expression.SourcePinId = ReturnPin->Id;
                const FBPIRPin* Inputs[] = { A, B };
                ++ConsumedConditionEdges;
                for (const FBPIRPin* Input : Inputs)
                {
                    FBPSemanticExpression Argument;
                    if (BuildNumericOperand(*CurrentComparison, *Input, 0, Argument) == false)
                    {
                        return false;
                    }
                    OutStatement.Expression.Arguments.Add(MoveTemp(Argument));
                }
                UsedComparisons.Add(CurrentComparison->Id);
            }
            const FName Outputs[] = { UEdGraphSchema_K2::PN_Then, TEXT("else") };
            for (int32 OutputIndex = 0; OutputIndex < 2; ++OutputIndex)
            {
                const FBPIRPin* Output = FindPin(Current, Outputs[OutputIndex], EGPD_Output);
                const FBPIRLink* Connection = nullptr;
                for (const FBPIRLink& Edge : EventGraph->ExecEdges)
                {
                    if (Output != nullptr && Edge.SourceNode == Current.Id &&
                        Edge.SourcePin == Output->Id)
                    {
                        if (Connection != nullptr)
                        {
                            return false;
                        }
                        Connection = &Edge;
                    }
                }
                if (Connection == nullptr)
                {
                    return false;
                }
                FBPSemanticStatement Child;
                const FBPIRNode* ChildBranch = nullptr;
                for (const FBPIRNode* Candidate : BranchNodes)
                {
                    if (Candidate->Id == Connection->TargetNode)
                    {
                        ChildBranch = Candidate;
                    }
                }
                if (ChildBranch != nullptr)
                {
                    const FBPIRPin* Execute = FindPin(
                        *ChildBranch, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
                    if (Execute == nullptr || Execute->Id != Connection->TargetPin ||
                        BuildBranch(*ChildBranch, Depth + 1, Child) == false)
                    {
                        return false;
                    }
                }
                else
                {
                    const FBPIRNode* Print = FindPrintById(Connection->TargetNode);
                    const FBPIRPin* Execute = Print != nullptr
                        ? FindPin(*Print, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
                    if (Print == nullptr || Execute == nullptr ||
                        Execute->Id != Connection->TargetPin ||
                        VisitedPrints.Contains(Print->Id))
                    {
                        return false;
                    }
                    FBPPrintStringCall Values;
                    if (AnalyzeBranchPrint(
                            GraphIR, *EventGraph, *BeginPlay, *Print, Values, OutIR) == false)
                    {
                        return false;
                    }
                    VisitedPrints.Add(Print->Id);
                    Child.Kind = EBPSemanticStatementKind::Expression;
                    Child.SourceNodeId = Print->Id;
                    Child.Expression = MakePrintStringExpression(
                        Values, *Print, GraphIR.ParentClassPath);
                }
                if (OutputIndex == 0)
                {
                    OutStatement.ThenStatements.Add(MoveTemp(Child));
                }
                else
                {
                    OutStatement.ElseStatements.Add(MoveTemp(Child));
                }
            }
            return true;
        };
        FBPSemanticStatement RootStatement;
        if (RootBranch == nullptr ||
            BuildBranch(*RootBranch, 0, RootStatement) == false ||
            VisitedBranches.Num() != BranchNodes.Num() ||
            VisitedPrints.Num() + OrderedCompletedPrints.Num() != Prints.Num() ||
            UsedComparisons.Num() != ComparisonNodes.Num() ||
            UsedArithmeticNodes.Num() != BodyArithmeticNodes.Num() ||
            ConsumedConditionEdges != EventGraph->DataEdges.Num())
        {
            AddDiagnostic(OutIR, EventGraph->Name, Loop->Id,
                TEXT("Nested ForLoop Branch has a cycle, shared target or unsupported Exec flow"));
            return false;
        }
        LoopStatement.LoopBodyStatements.Add(MoveTemp(RootStatement));
    }
    else if (BodyBranch != nullptr)
    {
        const FBPIRPin* Condition = FindPin(*BodyBranch, TEXT("Condition"), EGPD_Input);
        const FBPIRPin* Then = FindPin(*BodyBranch, UEdGraphSchema_K2::PN_Then, EGPD_Output);
        const FBPIRPin* Else = FindPin(*BodyBranch, TEXT("else"), EGPD_Output);
        const auto FindBranchTarget = [EventGraph, BodyBranch, &FindPrintById](const FBPIRPin* Pin)
        {
            const FBPIRLink* Edge = Pin != nullptr ? EventGraph->ExecEdges.FindByPredicate(
                [BodyBranch, Pin](const FBPIRLink& Candidate)
                {
                    return Candidate.SourceNode == BodyBranch->Id && Candidate.SourcePin == Pin->Id;
                }) : nullptr;
            return Edge != nullptr ? FindPrintById(Edge->TargetNode) : nullptr;
        };
        const FBPIRNode* ThenPrint = FindBranchTarget(Then);
        const FBPIRNode* ElsePrint = FindBranchTarget(Else);
        const FBPIRPin* ThenExecute = ThenPrint != nullptr
            ? FindPin(*ThenPrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
        const FBPIRPin* ElseExecute = ElsePrint != nullptr
            ? FindPin(*ElsePrint, UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
        const auto HasBranchEdge = [EventGraph, BodyBranch](const FBPIRPin* Source,
            const FBPIRNode* Target, const FBPIRPin* TargetExecute)
        {
            return Source != nullptr && Target != nullptr && TargetExecute != nullptr &&
                EventGraph->ExecEdges.ContainsByPredicate(
                    [BodyBranch, Source, Target, TargetExecute](const FBPIRLink& Edge)
                    {
                        return Edge.SourceNode == BodyBranch->Id && Edge.SourcePin == Source->Id &&
                            Edge.TargetNode == Target->Id && Edge.TargetPin == TargetExecute->Id;
                    });
        };
        if (Condition == nullptr || ThenPrint == nullptr || ElsePrint == nullptr ||
            ThenPrint == ElsePrint || ThenPrint == CompletedPrint || ElsePrint == CompletedPrint ||
            HasBranchEdge(Then, ThenPrint, ThenExecute) == false ||
            HasBranchEdge(Else, ElsePrint, ElseExecute) == false ||
            (BodyComparison == nullptr &&
                Condition->DefaultValue != TEXT("true") && Condition->DefaultValue != TEXT("false")) ||
            Prints.Num() != 2 + OrderedCompletedPrints.Num())
        {
            AddDiagnostic(OutIR, EventGraph->Name, BodyBranch->Id,
                TEXT("ForLoop Branch requires a bool literal and distinct True/False PrintString targets"));
            return false;
        }
        FBPPrintStringCall ThenValues;
        FBPPrintStringCall ElseValues;
        if (AnalyzeBranchPrint(GraphIR, *EventGraph, *BeginPlay, *ThenPrint, ThenValues, OutIR) == false ||
            AnalyzeBranchPrint(GraphIR, *EventGraph, *BeginPlay, *ElsePrint, ElseValues, OutIR) == false)
        {
            return false;
        }
        FBPSemanticStatement BranchStatement;
        BranchStatement.Kind = EBPSemanticStatementKind::Branch;
        BranchStatement.SourceNodeId = BodyBranch->Id;
        if (BodyComparison == nullptr)
        {
            BranchStatement.Expression = Literal(
                EBPSemanticTypeKind::Bool, Condition->DefaultValue, *BodyBranch, TEXT("Condition"));
        }
        else
        {
            // 比較関数の型・所有クラスを既存Resolverで検証し、未知の式は変換しない。
            FBPNumericComparison Comparison;
            TArray<FString> Diagnostics;
            if (FBPNumericComparisonResolver::Resolve(BodyComparison->FunctionOwnerPath,
                    BodyComparison->FunctionName, Comparison, Diagnostics) == false)
            {
                for (const FString& Message : Diagnostics)
                {
                    AddDiagnostic(OutIR, EventGraph->Name, BodyComparison->Id, Message);
                }
                return false;
            }
            const FBPIRPin* ReturnPin = FindPin(*BodyComparison, TEXT("ReturnValue"), EGPD_Output);
            const FBPIRLink* ConditionEdge = EventGraph->DataEdges.FindByPredicate(
                [BodyComparison, ReturnPin, BodyBranch, Condition](const FBPIRLink& Edge)
                {
                    return ReturnPin != nullptr && Edge.SourceNode == BodyComparison->Id &&
                        Edge.SourcePin == ReturnPin->Id && Edge.TargetNode == BodyBranch->Id &&
                        Edge.TargetPin == Condition->Id;
                });
            const FBPIRPin* A = FindPin(*BodyComparison, TEXT("A"), EGPD_Input);
            const FBPIRPin* B = FindPin(*BodyComparison, TEXT("B"), EGPD_Input);
            if (ConditionEdge == nullptr || A == nullptr || B == nullptr ||
                Comparison.OperandType != EBPSemanticTypeKind::Int32 ||
                FBPNumericComparisonResolver::MatchesPinType(A->Category, A->SubCategory,
                    Comparison.OperandType) == false ||
                FBPNumericComparisonResolver::MatchesPinType(B->Category, B->SubCategory,
                    Comparison.OperandType) == false)
            {
                AddDiagnostic(OutIR, EventGraph->Name, BodyComparison->Id,
                    TEXT("ForLoop comparison requires int32 operands and a connected condition"));
                return false;
            }
            BranchStatement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
            BranchStatement.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
            BranchStatement.Expression.FunctionName = BodyComparison->FunctionName;
            BranchStatement.Expression.FunctionOwnerPath = BodyComparison->FunctionOwnerPath;
            BranchStatement.Expression.SourceNodeId = BodyComparison->Id;
            BranchStatement.Expression.SourcePinId = ReturnPin->Id;
            // 比較式から到達可能な算術ノードを再帰的に構造化する。
            // 深さ制限・循環検出・使用済みノード集合により、不正グラフを拒否する。
            TSet<FGuid> ActiveArithmeticNodes;
            TSet<FGuid> UsedArithmeticNodes;
            int32 ConsumedArithmeticEdges = 0;
            TFunction<bool(const FBPIRNode&, const FBPIRPin&, int32,
                FBPSemanticExpression&)> BuildOperand;
            BuildOperand = [&](const FBPIRNode& Owner, const FBPIRPin& Input,
                int32 Depth, FBPSemanticExpression& OutExpression) -> bool
            {
                if (Depth > 16 ||
                    FBPNumericComparisonResolver::MatchesPinType(
                        Input.Category, Input.SubCategory, EBPSemanticTypeKind::Int32) == false)
                {
                    return false;
                }
                const FBPIRLink* Source = nullptr;
                for (const FBPIRLink& Edge : EventGraph->DataEdges)
                {
                    if (Edge.TargetNode == Owner.Id && Edge.TargetPin == Input.Id)
                    {
                        if (Source != nullptr)
                        {
                            return false;
                        }
                        Source = &Edge;
                    }
                }
                if (Source == nullptr)
                {
                    FString Value;
                    if (FBPNumericComparisonResolver::CanonicalizeLiteral(
                        EBPSemanticTypeKind::Int32, Input.DefaultValue, Value) == false)
                    {
                        return false;
                    }
                    OutExpression = Literal(EBPSemanticTypeKind::Int32, Value, Owner, Input.Name);
                    return true;
                }
                ++ConsumedArithmeticEdges;
                if (Source->SourceNode == Loop->Id && Source->SourcePin == LoopIndex->Id)
                {
                    OutExpression.Kind = EBPSemanticExpressionKind::VariableReference;
                    OutExpression.Type.Kind = EBPSemanticTypeKind::Int32;
                    OutExpression.SourceNodeId = Loop->Id;
                    OutExpression.SourcePinId = FGuid();
                    OutExpression.VariableName = LoopStatement.LoopIndexVariable;
                    return true;
                }
                const FBPIRNode* const* Found = BodyArithmeticNodes.FindByPredicate(
                    [Source](const FBPIRNode* Node)
                    {
                        return Node != nullptr && Node->Id == Source->SourceNode;
                    });
                const FBPIRNode* Arithmetic = Found != nullptr ? *Found : nullptr;
                if (Arithmetic == nullptr ||
                    ActiveArithmeticNodes.Contains(Arithmetic->Id) ||
                    UsedArithmeticNodes.Contains(Arithmetic->Id))
                {
                    return false;
                }
                FBPNumericArithmeticOperation Operation;
                TArray<FString> Diagnostics;
                if (FBPNumericArithmeticResolver::Resolve(Arithmetic->FunctionOwnerPath,
                        Arithmetic->FunctionName, Operation, Diagnostics) == false ||
                    Operation.OperandType != EBPSemanticTypeKind::Int32)
                {
                    return false;
                }
                const FBPIRPin* Result = FindPin(*Arithmetic, TEXT("ReturnValue"), EGPD_Output);
                const FBPIRPin* InputA = FindPin(*Arithmetic, TEXT("A"), EGPD_Input);
                const FBPIRPin* InputB = FindPin(*Arithmetic, TEXT("B"), EGPD_Input);
                if (Result == nullptr || InputA == nullptr || InputB == nullptr ||
                    Source->SourcePin != Result->Id)
                {
                    return false;
                }
                ActiveArithmeticNodes.Add(Arithmetic->Id);
                OutExpression.Kind = EBPSemanticExpressionKind::FunctionCall;
                OutExpression.Type.Kind = EBPSemanticTypeKind::Int32;
                OutExpression.FunctionName = Arithmetic->FunctionName;
                OutExpression.FunctionOwnerPath = Arithmetic->FunctionOwnerPath;
                OutExpression.SourceNodeId = Arithmetic->Id;
                OutExpression.SourcePinId = Result->Id;
                FBPSemanticExpression Left;
                FBPSemanticExpression Right;
                if (BuildOperand(*Arithmetic, *InputA, Depth + 1, Left) == false ||
                    BuildOperand(*Arithmetic, *InputB, Depth + 1, Right) == false)
                {
                    return false;
                }
                OutExpression.Arguments.Add(MoveTemp(Left));
                OutExpression.Arguments.Add(MoveTemp(Right));
                ActiveArithmeticNodes.Remove(Arithmetic->Id);
                UsedArithmeticNodes.Add(Arithmetic->Id);
                return true;
            };
            const FBPIRPin* Operands[] = { A, B };
            for (const FBPIRPin* Operand : Operands)
            {
                FBPSemanticExpression Argument;
                if (BuildOperand(*BodyComparison, *Operand, 0, Argument) == false)
                {
                    AddDiagnostic(OutIR, EventGraph->Name, BodyComparison->Id,
                        TEXT("ForLoop comparison contains an unsupported arithmetic expression"));
                    return false;
                }
                BranchStatement.Expression.Arguments.Add(MoveTemp(Argument));
            }
            if (ConsumedArithmeticEdges + 1 != EventGraph->DataEdges.Num() ||
                UsedArithmeticNodes.Num() != BodyArithmeticNodes.Num())
            {
                AddDiagnostic(OutIR, EventGraph->Name, BodyComparison->Id,
                    TEXT("ForLoop comparison contains unused or ambiguous data nodes"));
                return false;
            }
        }
        FBPSemanticStatement ThenStatement;
        ThenStatement.Kind = EBPSemanticStatementKind::Expression;
        ThenStatement.SourceNodeId = ThenPrint->Id;
        ThenStatement.Expression = MakePrintStringExpression(
            ThenValues, *ThenPrint, GraphIR.ParentClassPath);
        BranchStatement.ThenStatements.Add(MoveTemp(ThenStatement));
        FBPSemanticStatement ElseStatement;
        ElseStatement.Kind = EBPSemanticStatementKind::Expression;
        ElseStatement.SourceNodeId = ElsePrint->Id;
        ElseStatement.Expression = MakePrintStringExpression(
            ElseValues, *ElsePrint, GraphIR.ParentClassPath);
        BranchStatement.ElseStatements.Add(MoveTemp(ElseStatement));
        LoopStatement.LoopBodyStatements.Add(MoveTemp(BranchStatement));
    }
    const FBPIRNode* IndexConsumerPrint = nullptr;
    FBPSemanticExpression IndexConversion;
    if (IndexToString != nullptr)
    {
        const FBPIRPin* ConverterInput = FindPin(*IndexToString, TEXT("InInt"), EGPD_Input);
        const FBPIRPin* ConverterOutput = FindPin(*IndexToString, TEXT("ReturnValue"), EGPD_Output);
        const FBPIRLink* ConverterConsumerEdge = EventGraph->DataEdges.FindByPredicate(
            [IndexToString, ConverterOutput](const FBPIRLink& Edge)
            {
                return ConverterOutput != nullptr && Edge.SourceNode == IndexToString->Id &&
                    Edge.SourcePin == ConverterOutput->Id;
            });
        IndexConsumerPrint = ConverterConsumerEdge != nullptr
            ? FindPrintById(ConverterConsumerEdge->TargetNode) : nullptr;
        const FBPIRPin* PrintText = IndexConsumerPrint != nullptr
            ? FindPin(*IndexConsumerPrint, TEXT("InString"), EGPD_Input) : nullptr;
        const bool bIndexToConverter = EventGraph->DataEdges.ContainsByPredicate(
            [Loop, LoopIndex, IndexToString, ConverterInput](const FBPIRLink& Edge)
            {
                return ConverterInput != nullptr && Edge.SourceNode == Loop->Id &&
                    Edge.SourcePin == LoopIndex->Id && Edge.TargetNode == IndexToString->Id &&
                    Edge.TargetPin == ConverterInput->Id;
            });
        const bool bConverterToPrint = EventGraph->DataEdges.ContainsByPredicate(
            [IndexToString, ConverterOutput, IndexConsumerPrint, PrintText](const FBPIRLink& Edge)
            {
                return ConverterOutput != nullptr && PrintText != nullptr &&
                    Edge.SourceNode == IndexToString->Id && Edge.SourcePin == ConverterOutput->Id &&
                    Edge.TargetNode == IndexConsumerPrint->Id && Edge.TargetPin == PrintText->Id;
            });
        if (bIndexToConverter == false || bConverterToPrint == false ||
            OrderedBodyPrints.Contains(IndexConsumerPrint) == false)
        {
            AddDiagnostic(OutIR, EventGraph->Name, IndexToString->Id,
                TEXT("ForLoop Index conversion must connect Index -> Conv_IntToString -> PrintString.InString"));
            return false;
        }
        IndexConversion.Kind = EBPSemanticExpressionKind::FunctionCall;
        IndexConversion.Type.Kind = EBPSemanticTypeKind::String;
        IndexConversion.SourceNodeId = IndexToString->Id;
        IndexConversion.SourcePinId = ConverterOutput->Id;
        IndexConversion.FunctionName = TEXT("Conv_IntToString");
        IndexConversion.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
        FBPSemanticExpression IndexReference;
        IndexReference.Kind = EBPSemanticExpressionKind::VariableReference;
        IndexReference.Type.Kind = EBPSemanticTypeKind::Int32;
        IndexReference.SourceNodeId = Loop->Id;
        IndexReference.SourcePinId = FGuid();
        IndexReference.VariableName = LoopStatement.LoopIndexVariable;
        IndexConversion.Arguments.Add(MoveTemp(IndexReference));
        // 対象Body文を構築するときに式へ差し込む。
    }
    for (const FBPIRNode* OrderedPrint : OrderedBodyPrints)
    {
        FBPPrintStringCall BodyValues;
        if (OrderedPrint == nullptr || AnalyzeBranchPrint(
            GraphIR, *EventGraph, *BeginPlay, *OrderedPrint, BodyValues, OutIR) == false)
        {
            return false;
        }
        FBPSemanticStatement BodyStatement;
        BodyStatement.Kind = EBPSemanticStatementKind::Expression;
        BodyStatement.SourceNodeId = OrderedPrint->Id;
        BodyStatement.Expression = MakePrintStringExpression(
            BodyValues, *OrderedPrint, GraphIR.ParentClassPath);
        if (OrderedPrint == IndexConsumerPrint)
        {
            BodyStatement.Expression.Arguments[1] = MoveTemp(IndexConversion);
        }
        LoopStatement.LoopBodyStatements.Add(MoveTemp(BodyStatement));
    }
    for (const FBPIRNode* OrderedCompleted : OrderedCompletedPrints)
    {
        FBPPrintStringCall CompletedValues;
        if (AnalyzeBranchPrint(
            GraphIR, *EventGraph, *BeginPlay, *OrderedCompleted, CompletedValues, OutIR) == false)
        {
            return false;
        }
        FBPSemanticStatement CompletedStatement;
        CompletedStatement.Kind = EBPSemanticStatementKind::Expression;
        CompletedStatement.SourceNodeId = OrderedCompleted->Id;
        CompletedStatement.Expression = MakePrintStringExpression(
            CompletedValues, *OrderedCompleted, GraphIR.ParentClassPath);
        LoopStatement.CompletedStatements.Add(MoveTemp(CompletedStatement));
    }
    Block.Statements.Add(MoveTemp(LoopStatement));
    Function.Blocks.Add(MoveTemp(Block));
    OutIR.Functions.Add(MoveTemp(Function));
    return true;
}
}

bool FBPSemanticLowering::Lower(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    bool bHasPrintString = false;
    bool bHasGetActorLocation = false;
    bool bHasSetActorLocation = false;
    bool bHasBranch = false;
    bool bHasForLoop = false;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            bHasPrintString |= Node.FunctionName == TEXT("PrintString");
            bHasGetActorLocation |= Node.FunctionName == TEXT("K2_GetActorLocation");
            bHasSetActorLocation |= Node.FunctionName == TEXT("K2_SetActorLocation");
            bHasBranch |= Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
            bHasForLoop |= Node.MacroGraphName == TEXT("ForLoop");
        }
    }
    // ForEachLoopは配列要素型・配列の供給元・Exec経路の検証が必要。
    // 未対応の段階で通常のPrintString経路へ誤振り分けしない。
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.MacroGraphName == TEXT("ForEachLoop") ||
                Node.MacroGraphName == TEXT("ForEachLoopWithBreak"))
            {
                OutIR = FBPSemanticIR();
                OutIR.AssetPath = GraphIR.AssetPath;
                OutIR.ParentClassPath = GraphIR.ParentClassPath;
                if (Node.MacroGraphName == TEXT("ForEachLoop"))
                {
                    if (Blueprint == nullptr)
                    {
                        AddDiagnostic(OutIR, Graph.Name, Node.Id,
                            TEXT("ForEachLoop requires a compiled Blueprint for array type resolution"));
                        return false;
                    }
                    return LowerForEachLoopImpl(Blueprint, GraphIR, OutIR);
                }
                if (Blueprint == nullptr)
                {
                    AddDiagnostic(OutIR, Graph.Name, Node.Id,
                        TEXT("ForEachLoopWithBreak requires a compiled Blueprint for array type resolution"));
                    return false;
                }
                return LowerForEachLoopImpl(Blueprint, GraphIR, OutIR);
            }
        }
    }
    if (bHasForLoop && bHasPrintString && bHasBranch == false &&
        bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        return LowerForLoop(Blueprint, GraphIR, OutIR);
    }
    if (bHasBranch && bHasForLoop == false && bHasPrintString &&
        bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        return LowerBranch(Blueprint, GraphIR, OutIR);
    }
    if (bHasBranch == false && bHasForLoop == false && bHasPrintString &&
        bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        FBPSemanticIR BeginPlayIR;
        if (LowerPrintString(Blueprint, GraphIR, BeginPlayIR))
        {
            OutIR = MoveTemp(BeginPlayIR);
            return true;
        }
        // BeginPlayの限定形状に一致しない場合だけ、Reflectionで署名を確定できる
        // Function Entry / Custom Event経路を試す。
        return LowerSingleEntryPrintStringImpl(Blueprint, GraphIR, OutIR);
    }
    if (bHasBranch == false && bHasPrintString == false &&
        bHasGetActorLocation && bHasSetActorLocation)
    {
        return LowerActorLocation(Blueprint, GraphIR, OutIR);
    }
    OutIR = FBPSemanticIR();
    OutIR.AssetPath = GraphIR.AssetPath;
    OutIR.ParentClassPath = GraphIR.ParentClassPath;
    AddDiagnostic(OutIR, FString(), FGuid(),
        TEXT("No unambiguous supported Semantic conversion pattern was found"));
    return false;
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

bool FBPSemanticLowering::LowerActorLocation(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    return LowerActorLocationImpl(Blueprint, GraphIR, OutIR);
}

bool FBPSemanticLowering::LowerBranch(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    return LowerBranchImpl(Blueprint, GraphIR, OutIR);
}

bool FBPSemanticLowering::LowerForLoop(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    return LowerForLoopImpl(nullptr, GraphIR, OutIR);
}

bool FBPSemanticLowering::LowerForLoop(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    return LowerForLoopImpl(Blueprint, GraphIR, OutIR);
}
