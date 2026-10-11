#include "BPConnectedFunctionCallEmitter.h"

#include "BPConnectedFunctionArguments.h"
#include "BPFunctionSignatureResolver.h"
#include "BPFunctionCallValidator.h"
#include "BPFunctionPinTypeMatcher.h"
#include "BPOutputParameterStorage.h"
#include "BPOutputPinBindingPlan.h"
#include "BPSetActorLocationCallArguments.h"
#include "BPConnectedActorLocationLowering.h"
#include "BPBooleanPinDefaultEmitter.h"
#include "GameFramework/Actor.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"

bool FBPConnectedFunctionCallEmitter::Emit(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, const UFunction* Function, FString& OutStatement,
    TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutStatement.Reset();

    // 任意の反射名をそのままC++へ出力せず、変換規約が確認済みのAPIに限定する。
    if (Function == nullptr ||
        Function->GetOwnerClass() != AActor::StaticClass() ||
        Function->GetFName() != TEXT("K2_SetActorLocation"))
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Unsupported function for C++ statement emission");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    FBPResolvedFunctionSignature Signature;
    TArray<FString> Messages;
    if (FBPFunctionSignatureResolver::Resolve(Function, Signature, Messages) == false)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = FString::Join(Messages, TEXT("; "));
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // 呼び出しノードが存在しない場合は署名の詳細より先に構造エラーを報告する。
    int32 CallMatches = 0;
    const FBPIRNode* CallNode = nullptr;
    FString CallGraphName;
    const FBPIRGraph* CallGraph = nullptr;
    for (const FBPIRGraph& Graph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == CallNodeId)
            {
                ++CallMatches;
                CallNode = &Node;
                CallGraphName = Graph.Name;
                CallGraph = &Graph;
            }
        }
    }
    if (CallMatches != 1)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Call node missing or ambiguous");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // 引数の接続を調べる前に、ノードのピン方向と型を署名と照合する。
    FBPResolvedFunctionSignature ValidatedSignature;
    TArray<FString> ValidationMessages;
    if (FBPFunctionCallValidator::Validate(*CallNode, Function,
        ValidatedSignature, ValidationMessages) == false)
    {
        for (const FString& Message : ValidationMessages)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = CallGraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = Message;
            OutDiagnostics.Add(MoveTemp(Diagnostic));
        }
        return false;
    }

    // 出力パラメータに対応するピンの接続先を検証するまでは生成しない。
    // 引数数と型を事前に確認し、エラーメッセージを明確にする。
    if (Signature.ParameterNames.Num() != Signature.ParameterTypes.Num() ||
        Signature.ParameterNames.Num() != Signature.OutputParameters.Num())
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Inconsistent function signature");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }

    // 出力引数はC++側で書き込み可能な変数を必要とする。
    // 接続先がある場合は後続のデータフローと変数寿命の解決が必要なため、
    // 未接続の場合と区別して診断する。どちらも現段階では安全に拒否する。
    for (int32 Index = 0; Index < Signature.OutputParameters.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index] == false)
        {
            continue;
        }

        const FName ParameterName = Signature.ParameterNames[Index];
        const FBPIRPin* OutputPin = nullptr;
        for (const FBPIRPin& Pin : CallNode->Pins)
        {
            if (Pin.Name == ParameterName && Pin.Direction == EGPD_Output)
            {
                OutputPin = &Pin;
                break;
            }
        }

        // Validatorが成功した場合、出力ピンは必ず一つ存在する。
        // IRが不整合でも誤ったC++を出さないよう再確認する。
        if (OutputPin == nullptr || CallGraph == nullptr)
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.GraphName = CallGraphName;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = FString::Printf(TEXT("Output parameter pin missing: %s"),
                *ParameterName.ToString());
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }

        int32 ConnectedEdges = 0;
        for (const FBPIRLink& Edge : CallGraph->DataEdges)
        {
            if (Edge.SourceNode == CallNodeId && Edge.SourcePin == OutputPin->Id)
            {
                // 接続先が同じグラフに存在し、入力データピンであることを確認する。
                // 存在しないノードや型の異なるピンへの接続はデータフローと認めない。
                const FBPIRPin* TargetPin = nullptr;
                int32 TargetMatches = 0;
                for (const FBPIRNode& TargetNode : CallGraph->Nodes)
                {
                    if (TargetNode.Id == Edge.TargetNode)
                    {
                        for (const FBPIRPin& Pin : TargetNode.Pins)
                        {
                            if (Pin.Id == Edge.TargetPin)
                            {
                                ++TargetMatches;
                                TargetPin = &Pin;
                            }
                        }
                    }
                }
                if (TargetMatches != 1 || TargetPin == nullptr ||
                    TargetPin->Direction != EGPD_Input ||
                    TargetPin->Category == UEdGraphSchema_K2::PC_Exec ||
                    FBPFunctionPinTypeMatcher::Matches(*TargetPin, Signature.ParameterTypes[Index]) == false)
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.GraphName = CallGraphName;
                    Diagnostic.NodeId = CallNodeId;
                    Diagnostic.PinId = OutputPin->Id;
                    Diagnostic.Message = FString::Printf(
                        TEXT("Invalid output parameter data edge target or type: %s"),
                        *ParameterName.ToString());
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
                ++ConnectedEdges;
            }
            // 逆方向のリンクも不正なIRとして拒否する。
            if (Edge.TargetNode == CallNodeId && Edge.TargetPin == OutputPin->Id)
            {
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.GraphName = CallGraphName;
                Diagnostic.NodeId = CallNodeId;
                Diagnostic.PinId = OutputPin->Id;
                Diagnostic.Message = FString::Printf(TEXT("Invalid incoming data edge on output parameter: %s"),
                    *ParameterName.ToString());
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
        }

        // 未接続の出力でもC++側には書き込み可能な領域が必要となる。
        // 宣言生成を実際に試し、対応可能な型と未対応型を区別する。
        // ただし呼び出し引数への組み込み前に文を出すと意味が壊れるため、ここでは診断のみ行う。
        FString StorageName;
        FString StorageDeclaration;
        const bool bStorageSupported = FBPOutputParameterStorage::Emit(
            Signature.ParameterTypes[Index], CallNodeId, StorageName, StorageDeclaration);
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.GraphName = CallGraphName;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.PinId = OutputPin->Id;
        if (bStorageSupported == false)
        {
            Diagnostic.Message = FString::Printf(
                TEXT("Unsupported output parameter storage type: %s"), *ParameterName.ToString());
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }
        // 出力ピンと後続入力ピンの対応をBindingPlanに集約する。
        // 変数の寿命とExec順序が保証されるまでは接続済み出力を生成しない。
        FBPOutputPinBinding Binding;
        FString BindingError;
        if (FBPOutputPinBindingPlan::Build(*CallGraph, CallNodeId,
            OutputPin->Id, StorageName, Signature.ParameterTypes[Index],
            Binding, BindingError) == false)
        {
            Diagnostic.Message = FString::Printf(
                TEXT("Invalid output binding plan: %s (%s)"),
                *ParameterName.ToString(), *BindingError);
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }
        if (ConnectedEdges != Binding.ConsumerPinIds.Num())
        {
            Diagnostic.Message = TEXT("Output binding edge count mismatch");
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }
        if (Binding.ConsumerPinIds.IsEmpty() == false)
        {
            // Exec順序が証明できない出力を、後続の変数参照に渡してはいけない。
            // 順序が安全でもConsumerの文生成と同一スコープ化は未実装なので拒否する。
            if (FBPOutputPinBindingPlan::ValidateExecOrder(
                *CallGraph, Binding, BindingError) == false)
            {
                Diagnostic.Message = FString::Printf(
                    TEXT("Invalid output Exec order: %s (%s)"),
                    *ParameterName.ToString(), *BindingError);
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
            // Exec由来の文配置順を確定し、出力変数の宣言順を検証する。
            TArray<FGuid> StatementOrder;
            if (FBPOutputPinBindingPlan::BuildStatementOrder(
                *CallGraph, Binding, StatementOrder, BindingError) == false)
            {
                Diagnostic.Message = FString::Printf(
                    TEXT("Invalid output statement order: %s"),
                    *BindingError);
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
            Diagnostic.Message = FString::Printf(
                TEXT("Connected output parameter requires data-flow lowering: %s (%d edges)"),
                *ParameterName.ToString(), ConnectedEdges);
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }
        // 未接続の出力のみ、後段で宣言と呼び出しを同時に生成する。

    }

    // 戻り値が使われている場合、呼び出し結果を捨てると意味が変わる。
    // また外部self参照の変換は未対応なので明示的に拒否する。
    if (GraphIR.Diagnostics.IsEmpty() == false ||
        GraphIR.ParentClassPath != TEXT("/Script/Engine.Actor"))
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Unsupported graph diagnostics or Actor parent");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }
    for (const FBPIRLink& Edge : CallGraph->DataEdges)
    {
        if (Edge.SourceNode == CallNodeId)
        {
            for (const FBPIRPin& Pin : CallNode->Pins)
            {
                if (Pin.Id == Edge.SourcePin && Pin.Name == TEXT("ReturnValue"))
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.NodeId = CallNodeId;
                    Diagnostic.Message = TEXT("Connected return value requires data-flow lowering");
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
            }
        }
    }
    for (const FBPIRPin& Pin : CallNode->Pins)
    {
        if (Pin.Name == TEXT("self"))
        {
            for (const FBPIRLink& Edge : CallGraph->DataEdges)
            {
                if (Edge.TargetNode == CallNodeId && Edge.TargetPin == Pin.Id)
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.NodeId = CallNodeId;
                    Diagnostic.Message = TEXT("Connected self target is unsupported");
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
            }
        }
    }

    FString Location;
    FString Sweep;
    FString Teleport;
    FString StorageName;
    FString StorageDeclaration;
    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        const FName Name = Signature.ParameterNames[Index];
        const FBPIRPin* Pin = CallNode->Pins.FindByPredicate(
            [Name](const FBPIRPin& Candidate) { return Candidate.Name == Name; });
        if (Pin == nullptr)
        {
            return false;
        }
        if (Name == TEXT("NewLocation") && Signature.OutputParameters[Index] == false)
        {
            if (FBPConnectedActorLocationLowering::EmitInputCpp(
                GraphIR, CallNodeId, Pin->Id, Location, OutDiagnostics) == false)
            {
                return false;
            }
        }
        else if (Name == TEXT("bSweep") && Signature.OutputParameters[Index] == false)
        {
            if (FBPBooleanPinDefaultEmitter::Emit(*Pin, Sweep) == false)
            {
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.NodeId = CallNodeId;
                Diagnostic.PinId = Pin->Id;
                Diagnostic.Message = TEXT("Unsupported bSweep default");
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
        }
        else if (Name == TEXT("bTeleport") && Signature.OutputParameters[Index] == false)
        {
            if (FBPBooleanPinDefaultEmitter::Emit(*Pin, Teleport) == false)
            {
                FBPSemanticDiagnostic Diagnostic;
                Diagnostic.NodeId = CallNodeId;
                Diagnostic.PinId = Pin->Id;
                Diagnostic.Message = TEXT("Unsupported bTeleport default");
                OutDiagnostics.Add(MoveTemp(Diagnostic));
                return false;
            }
        }
        else if (Name == TEXT("SweepHitResult") && Signature.OutputParameters[Index])
        {
            if (FBPOutputParameterStorage::Emit(Signature.ParameterTypes[Index],
                CallNodeId, StorageName, StorageDeclaration) == false)
            {
                return false;
            }
        }
        else
        {
            FBPSemanticDiagnostic Diagnostic;
            Diagnostic.NodeId = CallNodeId;
            Diagnostic.Message = TEXT("Unsupported SetActorLocation parameter layout");
            OutDiagnostics.Add(MoveTemp(Diagnostic));
            return false;
        }
    }
    // bool入力の接続を既定値に置き換えてはいけない。
    for (const FBPIRLink& Edge : CallGraph->DataEdges)
    {
        if (Edge.TargetNode == CallNodeId)
        {
            for (const FBPIRPin& Pin : CallNode->Pins)
            {
                if (Pin.Id == Edge.TargetPin &&
                    (Pin.Name == TEXT("bSweep") || Pin.Name == TEXT("bTeleport")))
                {
                    FBPSemanticDiagnostic Diagnostic;
                    Diagnostic.NodeId = CallNodeId;
                    Diagnostic.PinId = Pin.Id;
                    Diagnostic.Message = TEXT("Connected bool input is unsupported");
                    OutDiagnostics.Add(MoveTemp(Diagnostic));
                    return false;
                }
            }
        }
    }
    FString Arguments;
    if (FBPSetActorLocationCallArguments::Emit(
        Location, Sweep, StorageName, Teleport, Arguments) == false)
    {
        FBPSemanticDiagnostic Diagnostic;
        Diagnostic.NodeId = CallNodeId;
        Diagnostic.Message = TEXT("Cannot assemble K2_SetActorLocation arguments");
        OutDiagnostics.Add(MoveTemp(Diagnostic));
        return false;
    }
    // 変数宣言と呼び出しを同じ文スコープにまとめる。
    OutStatement = FString::Printf(TEXT("%s\nK2_SetActorLocation(%s);"),
        *StorageDeclaration, *Arguments);
    return true;
}
