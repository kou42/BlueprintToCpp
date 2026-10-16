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
    if (FBPParentClassResolver::ResolveActorParent(
        GraphIR.ParentClassPath, ParentClass, ParentDiagnostics) == false)
    {
        for (const FString& Message : ParentDiagnostics)
        {
            AddDiagnostic(OutIR, FString(), FGuid(), Message);
        }
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
            if (Branch != nullptr)
            {
                AddDiagnostic(OutIR, Graph.Name, Node.Id, TEXT("Duplicate Branch node"));
                return false;
            }
            Branch = &Node;
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

bool LowerForLoopImpl(const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
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
    if (GraphIR.BlueprintVariableCount != 0)
    {
        AddDiagnostic(OutIR, FString(), FGuid(),
            TEXT("ForLoop conversion does not yet support Blueprint member variables"));
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

    const FBPIRNode* BeginPlay = nullptr;
    const FBPIRNode* Loop = nullptr;
    const FBPIRNode* IndexToString = nullptr;
    const FBPIRNode* BodyBranch = nullptr;
    const FBPIRNode* BodyComparison = nullptr;
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
        }
        else if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
            Node.FunctionOwnerPath == TEXT("/Script/Engine.KismetMathLibrary"))
        {
            BodyComparison = &Node;
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
            (BodyBranch != nullptr ? 1 : 0) + (BodyComparison != nullptr ? 1 : 0) ||
        EventGraph->ExecEdges.Num() != 1 + Prints.Num() + (BodyBranch != nullptr ? 1 : 0) ||
        (BodyBranch != nullptr && IndexToString != nullptr) ||
        (BodyComparison == nullptr
            ? (IndexToString == nullptr ? EventGraph->DataEdges.IsEmpty() == false : EventGraph->DataEdges.Num() != 2)
            : EventGraph->DataEdges.Num() != 1))
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
    if (BodyBranch != nullptr)
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
            FString AValue;
            FString BValue;
            if (ConditionEdge == nullptr || A == nullptr || B == nullptr ||
                FBPNumericComparisonResolver::MatchesPinType(A->Category, A->SubCategory,
                    Comparison.OperandType) == false ||
                FBPNumericComparisonResolver::MatchesPinType(B->Category, B->SubCategory,
                    Comparison.OperandType) == false ||
                FBPNumericComparisonResolver::CanonicalizeLiteral(
                    Comparison.OperandType, A->DefaultValue, AValue) == false ||
                FBPNumericComparisonResolver::CanonicalizeLiteral(
                    Comparison.OperandType, B->DefaultValue, BValue) == false)
            {
                AddDiagnostic(OutIR, EventGraph->Name, BodyComparison->Id,
                    TEXT("ForLoop comparison requires two unconnected numeric literals"));
                return false;
            }
            BranchStatement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
            BranchStatement.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
            BranchStatement.Expression.FunctionName = BodyComparison->FunctionName;
            BranchStatement.Expression.FunctionOwnerPath = BodyComparison->FunctionOwnerPath;
            BranchStatement.Expression.SourceNodeId = BodyComparison->Id;
            BranchStatement.Expression.SourcePinId = ReturnPin->Id;
            BranchStatement.Expression.Arguments.Add(
                Literal(Comparison.OperandType, AValue, *BodyComparison, TEXT("A")));
            BranchStatement.Expression.Arguments.Add(
                Literal(Comparison.OperandType, BValue, *BodyComparison, TEXT("B")));
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
        IndexReference.SourcePinId = LoopIndex->Id;
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
    if (bHasForLoop && bHasPrintString && bHasBranch == false &&
        bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        return LowerForLoop(GraphIR, OutIR);
    }
    if (bHasBranch && bHasForLoop == false && bHasPrintString &&
        bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        return LowerBranch(Blueprint, GraphIR, OutIR);
    }
    if (bHasBranch == false && bHasForLoop == false && bHasPrintString &&
        bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        return LowerPrintString(Blueprint, GraphIR, OutIR);
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
    return LowerForLoopImpl(GraphIR, OutIR);
}
