#include "BPGenericFunctionCallEmitter.h"

#include "BPCallTargetResolver.h"
#include "BPFunctionCallValidator.h"
#include "BPBooleanPinDefaultEmitter.h"
#include "BPConnectedActorLocationLowering.h"
#include "BPGenericFunctionExpressionLowering.h"
#include "BPGenericFunctionExpressionEmitter.h"
#include "BPDataLinkResolver.h"
#include "Misc/LexFromString.h"
#include "Misc/LexToString.h"
#include "BPFunctionSignatureResolver.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

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

// Blueprintの数値既定値は一度数値型に解析し、正規化して出力する。
// 入力文字列を直接C++式として連結するとコード注入や型不一致につながる。
bool EmitNumericDefault(const FBPIRPin& Pin, const FBPSemanticType& Type, FString& Out)
{
    if (Type.bIsReference || Type.ContainerKind != TEXT("none"))
    {
        return false;
    }
    if (Type.Kind == EBPSemanticTypeKind::Int32)
    {
        int32 Value = 0;
        if (LexTryParseString(Value, *Pin.DefaultValue) == false)
        {
            return false;
        }
        Out = LexToString(Value);
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Int64)
    {
        int64 Value = 0;
        if (LexTryParseString(Value, *Pin.DefaultValue) == false)
        {
            return false;
        }
        Out = FString::Printf(TEXT("static_cast<int64>(%s)"), *LexToString(Value));
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Float)
    {
        float Value = 0.0f;
        if (LexTryParseString(Value, *Pin.DefaultValue) == false ||
            FMath::IsFinite(Value) == false)
        {
            return false;
        }
        Out = FString::Printf(TEXT("static_cast<float>(%s)"), *LexToString(Value));
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Double)
    {
        double Value = 0.0;
        if (LexTryParseString(Value, *Pin.DefaultValue) == false ||
            FMath::IsFinite(Value) == false)
        {
            return false;
        }
        Out = LexToString(Value);
        return true;
    }
    return false;
}

// 文字列はC++リテラルに安全にエスケープしてから型付きで生成する。
bool EmitTextDefault(const FBPIRPin& Pin, const FBPSemanticType& Type, FString& Out)
{
    if (Type.bIsReference || Type.ContainerKind != TEXT("none") ||
        (Type.Kind != EBPSemanticTypeKind::String &&
         Type.Kind != EBPSemanticTypeKind::Name &&
         Type.Kind != EBPSemanticTypeKind::Text))
    {
        return false;
    }
    // FTextはDefaultTextのローカライズ識別子を復元できないため、未対応とする。
    if (Type.Kind == EBPSemanticTypeKind::Text)
    {
        return false;
    }
    FString Escaped;
    for (const TCHAR Character : Pin.DefaultValue)
    {
        switch (Character)
        {
        case TEXT('\\'): Escaped += TEXT("\\\\"); break;
        case TEXT('"'): Escaped += TEXT("\\\""); break;
        case TEXT('\n'): Escaped += TEXT("\\n"); break;
        case TEXT('\r'): Escaped += TEXT("\\r"); break;
        case TEXT('\t'): Escaped += TEXT("\\t"); break;
        default:
            if (Character < 0x20)
            {
                return false;
            }
            Escaped.AppendChar(Character);
            break;
        }
    }
    const FString Literal = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
    Out = Type.Kind == EBPSemanticTypeKind::String
        ? FString::Printf(TEXT("FString(%s)"), *Literal)
        : FString::Printf(TEXT("FName(%s)"), *Literal);
    return true;
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
        GraphIR.Diagnostics.IsEmpty() == false ||
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
    bool bHasConnectedArgument = false;
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
        bool bConnected = false;
        for (const FBPIRLink& Edge : Graph->DataEdges)
        {
            if (Edge.TargetNode == CallNodeId && Edge.TargetPin == Pin->Id)
            {
                bConnected = true;
            }
        }
        FString Value;
        if (bConnected)
        {
            bHasConnectedArgument = true;
            // 接続済みVector式は既存のGraph IR -> Semantic -> C++経路を再利用する。
            // 接続元ノードを先に解決し、Actor専用式と汎用式を排他的に選ぶ。
            const FBPIRNode* SourceNode = nullptr;
            const FBPIRPin* SourcePin = nullptr;
            TArray<FString> LinkMessages;
            if (FBPDataLinkResolver::ResolveInput(*Graph, CallNodeId, Pin->Id,
                SourceNode, SourcePin, LinkMessages) == false ||
                SourceNode == nullptr || SourcePin == nullptr ||
                SourcePin->Name != TEXT("ReturnValue"))
            {
                Report(OutDiagnostics, CallNodeId, TEXT("Unsupported generic input link"));
                return false;
            }
            if (SourceNode->FunctionName == TEXT("K2_GetActorLocation") &&
                Signature.ParameterTypes[Index].Kind == EBPSemanticTypeKind::Struct &&
                Signature.ParameterTypes[Index].TypePath == TEXT("/Script/CoreUObject.Vector"))
            {
                if (FBPConnectedActorLocationLowering::EmitInputCpp(
                    GraphIR, CallNodeId, Pin->Id, Value, OutDiagnostics) == false)
                {
                    return false;
                }
            }
            else
            {
                if (SourceNode->FunctionOwnerPath.IsEmpty())
                {
                    Report(OutDiagnostics, CallNodeId, TEXT("Unresolved generic expression owner"));
                    return false;
                }
                const UClass* SourceOwner = FindObject<UClass>(
                    nullptr, *SourceNode->FunctionOwnerPath);
                const UFunction* SourceFunction = SourceOwner != nullptr
                    ? SourceOwner->FindFunctionByName(SourceNode->FunctionName) : nullptr;
                FBPSemanticExpression SourceExpression;
                if (FBPGenericFunctionExpressionLowering::Lower(
                    GraphIR, SourceNode->Id, SourceFunction,
                    SourceExpression, OutDiagnostics) == false ||
                    SourceExpression.SourcePinId != SourcePin->Id ||
                    SourceExpression.Type.Kind != Signature.ParameterTypes[Index].Kind ||
                    SourceExpression.Type.TypePath != Signature.ParameterTypes[Index].TypePath ||
                    SourceExpression.Type.ContainerKind != Signature.ParameterTypes[Index].ContainerKind ||
                    SourceExpression.Type.bIsReference != Signature.ParameterTypes[Index].bIsReference ||
                    SourceExpression.Type.bIsConst != Signature.ParameterTypes[Index].bIsConst)
                {
                    Report(OutDiagnostics, CallNodeId, TEXT("Generic expression type mismatch"));
                    return false;
                }
                TArray<FString> EmitMessages;
                if (FBPGenericFunctionExpressionEmitter::Emit(
                    SourceExpression, OwnerClass, Value, EmitMessages) == false)
                {
                    for (const FString& Message : EmitMessages)
                    {
                        Report(OutDiagnostics, CallNodeId, Message);
                    }
                    return false;
                }
            }
        }
        else if (Signature.ParameterTypes[Index].Kind == EBPSemanticTypeKind::Bool)
        {
            if (FBPBooleanPinDefaultEmitter::Emit(*Pin, Value) == false)
            {
                Report(OutDiagnostics, CallNodeId, TEXT("Invalid bool default"));
                return false;
            }
        }
        else if (EmitNumericDefault(*Pin, Signature.ParameterTypes[Index], Value) == false &&
            EmitTextDefault(*Pin, Signature.ParameterTypes[Index], Value) == false)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unsupported generic argument default"));
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
        if (Edge.TargetNode == CallNodeId &&
            Call->Pins.ContainsByPredicate([&Edge](const FBPIRPin& Pin)
            {
                return Pin.Id == Edge.TargetPin &&
                    ((Pin.Name == TEXT("self") || Pin.Name == TEXT("Target")) ||
                        Signature.ParameterNames.Contains(Pin.Name));
            }) == false)
        {
            Report(OutDiagnostics, CallNodeId, TEXT("Unsupported generic call input edge"));
            return false;
        }
    }

    // 関数名を反射で確認できても、特殊なC++呼び出し規約の関数は対象外。
    const FString FunctionName = Function->GetName();
    if (IsIdentifier(FunctionName) == false)
    {
        Report(OutDiagnostics, CallNodeId, TEXT("Invalid reflected function identifier"));
        return false;
    }
    // C++の関数引数評価順序に依存しないよう、接続式を含む場合は
    // 引数をピンの宣言順で一時変数へ評価してから呼び出す。
    FString Preparation;
    FString ArgumentList = FString::Join(Args, TEXT(", "));
    if (bHasConnectedArgument)
    {
        TArray<FString> Locals;
        for (int32 Index = 0; Index < Args.Num(); ++Index)
        {
            const FString LocalName = FString::Printf(TEXT("BPArg%d"), Index);
            Preparation += FString::Printf(TEXT("const auto %s = %s;\\n"),
                *LocalName, *Args[Index]);
            Locals.Add(LocalName);
        }
        ArgumentList = FString::Join(Locals, TEXT(", "));
    }
    if (Prefix.IsEmpty())
    {
        OutStatement = Preparation + FString::Printf(TEXT("%s(%s);"),
            *FunctionName, *ArgumentList);
    }
    else
    {
        // Targetを一度だけ読み取り、null時には引数式を評価しない。
        OutStatement = FString::Printf(
            TEXT("{\\nauto* BPTarget = %s;\\nif (BPTarget != nullptr)\\n{\\n%sBPTarget->%s(%s);\\n}\\n}"),
            *Target.VariableName.ToString(), *Preparation, *FunctionName, *ArgumentList);
    }
    return true;
}
