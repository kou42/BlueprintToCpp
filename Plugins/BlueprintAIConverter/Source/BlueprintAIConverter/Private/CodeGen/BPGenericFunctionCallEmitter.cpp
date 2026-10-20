#include "BPGenericFunctionCallEmitter.h"

#include "BPCallTargetResolver.h"
#include "BPFunctionCallValidator.h"
#include "BPBooleanPinDefaultEmitter.h"
#include "BPFunctionSignatureResolver.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

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

bool IsIdentifier(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Character = Name[Index];
        const bool bAlpha = (Character >= TEXT('a') && Character <= TEXT('z')) ||
            (Character >= TEXT('A') && Character <= TEXT('Z'));
        const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
        if (bAlpha == false && Character != TEXT('_') && (Index == 0 || bDigit == false))
        {
            return false;
        }
    }
    return true;
}
}

bool FBPGenericFunctionCallEmitter::Emit(const FBPGraphIR& GraphIR,
    const FGuid& CallNodeId, const UFunction* Function, const UClass* OwnerClass,
    FString& OutStatement, TArray<FBPSemanticDiagnostic>& OutDiagnostics)
{
    OutStatement.Reset();
    if (Function == nullptr || OwnerClass == nullptr ||
        Function->HasAnyFunctionFlags(FUNC_Native) == false ||
        Function->HasAnyFunctionFlags(FUNC_Static | FUNC_BlueprintEvent) ||
        Function->GetOwnerClass() == nullptr)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Generic emitter requires a native instance function"));
        return false;
    }

    const FBPIRNode* Call = nullptr;
    const FBPIRGraph* Graph = nullptr;
    for (const FBPIRGraph& CandidateGraph : GraphIR.Graphs)
    {
        for (const FBPIRNode& Node : CandidateGraph.Nodes)
        {
            if (Node.Id == CallNodeId)
            {
                if (Call != nullptr)
                {
                    Report(OutDiagnostics, CallNodeId, TEXT("Ambiguous call GUID"));
                    return false;
                }
                Call = &Node;
                Graph = &CandidateGraph;
            }
        }
    }
    if (Call == nullptr || Graph == nullptr)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Call node not found"));
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
    // 戻り値を無視したり、出力引数の格納先を推測したりしない。
    if (Signature.bStatic || Signature.ReturnType.Kind != EBPSemanticTypeKind::Void ||
        Signature.ParameterNames.Num() != Signature.ParameterTypes.Num() ||
        Signature.ParameterNames.Num() != Signature.OutputParameters.Num())
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Generic emitter supports void instance calls only"));
        return false;
    }

    FBPSemanticExpression Target;
    if (FBPCallTargetResolver::Resolve(GraphIR, CallNodeId, Target, OutDiagnostics) == false)
    {
        return false;
    }
    const UClass* TargetClass = FindObject<UClass>(nullptr, *Target.Type.TypePath);
    if (TargetClass == nullptr ||
        TargetClass->IsChildOf(Function->GetOwnerClass()) == false)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Target class cannot call the reflected function"));
        return false;
    }

    FString Prefix;
    if (Target.VariableName == TEXT("this"))
    {
        if (OwnerClass->IsChildOf(Function->GetOwnerClass()) == false)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Generated class cannot call this function"));
            return false;
        }
    }
    else
    {
        const FProperty* Property = OwnerClass->FindPropertyByName(Target.VariableName);
        const FObjectProperty* Object = CastField<FObjectProperty>(Property);
        if (Object == nullptr || Object->PropertyClass == nullptr ||
            Object->PropertyClass->GetPathName() != Target.Type.TypePath ||
            IsIdentifier(Target.VariableName.ToString()) == false)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Target is not a matching object property"));
            return false;
        }
        Prefix = Target.VariableName.ToString() + TEXT("->");
    }

    TArray<FString> Args;
    for (int32 Index = 0; Index < Signature.ParameterNames.Num(); ++Index)
    {
        if (Signature.OutputParameters[Index])
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Output parameters require dedicated lowering"));
            return false;
        }
        const FBPIRPin* Pin = Call->Pins.FindByPredicate(
            [Name = Signature.ParameterNames[Index]](const FBPIRPin& Candidate)
            {
                return Candidate.Name == Name && Candidate.Direction == EGPD_Input;
            });
        if (Pin == nullptr)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Missing input parameter"));
            return false;
        }
        for (const FBPIRLink& Edge : Graph->DataEdges)
        {
            if (Edge.TargetNode == CallNodeId && Edge.TargetPin == Pin->Id)
            {
                Report(OutDiagnostics, CallNodeId, TEXT("Connected generic parameters are not yet supported"));
                return false;
            }
        }
        // 最初の汎用対応は正規化済みbool既定値に限定する。
        // 他型の既定値を未検証のC++リテラルとして埋め込まない。
        FString Value;
        if (Signature.ParameterTypes[Index].Kind != EBPSemanticTypeKind::Bool ||
            FBPBooleanPinDefaultEmitter::Emit(*Pin, Value) == false)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unsupported generic parameter default"));
            return false;
        }
        Args.Add(MoveTemp(Value));
    }

    // 未知の入力リンクや戻り値の接続を黙って捨てない。
    for (const FBPIRLink& Edge : Graph->DataEdges)
    {
        if (Edge.SourceNode == CallNodeId)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Generic call output data edge is unsupported"));
            return false;
        }
        if (Edge.TargetNode == CallNodeId && Edge.TargetPin != Target.SourcePinId)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unsupported generic call input edge"));
            return false;
        }
    }

    const FString FunctionName = Function->GetName();
    if (IsIdentifier(FunctionName) == false)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Invalid reflected function identifier"));
        return false;
    }
    const FString Invocation = FString::Printf(TEXT("%s%s(%s);"),
        *Prefix, *FunctionName, *FString::Join(Args, TEXT(", ")));
    if (Prefix.IsEmpty())
    {
        OutStatement = Invocation;
    }
    else
    {
        OutStatement = FString::Printf(TEXT("if (%s != nullptr)\n{\n%s\n}"),
            *Target.VariableName.ToString(), *Invocation);
    }
    return true;
}
