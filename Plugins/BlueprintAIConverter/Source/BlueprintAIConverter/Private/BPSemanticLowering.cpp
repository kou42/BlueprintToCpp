#include "BPSemanticLowering.h"
#include "BPParentClassResolver.h"
#include "BPPrintStringAnalyzer.h"
#include "BPBooleanPinDefaultEmitter.h"
#include "BPConnectedActorLocationLowering.h"
#include "BPConnectedFunctionCallEmitter.h"
#include "BPFunctionSignatureResolver.h"
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
}

bool FBPSemanticLowering::Lower(const UBlueprint* Blueprint,
    const FBPGraphIR& GraphIR, FBPSemanticIR& OutIR)
{
    bool bHasPrintString = false;
    bool bHasGetActorLocation = false;
    bool bHasSetActorLocation = false;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            bHasPrintString |= Node.FunctionName == TEXT("PrintString");
            bHasGetActorLocation |= Node.FunctionName == TEXT("K2_GetActorLocation");
            bHasSetActorLocation |= Node.FunctionName == TEXT("K2_SetActorLocation");
        }
    }
    if (bHasPrintString && bHasGetActorLocation == false && bHasSetActorLocation == false)
    {
        return LowerPrintString(Blueprint, GraphIR, OutIR);
    }
    if (bHasPrintString == false && bHasGetActorLocation && bHasSetActorLocation)
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
