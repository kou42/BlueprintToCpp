#include "BPFunctionCallValidator.h"
#include "BPFunctionPinTypeMatcher.h"

#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"

bool FBPFunctionCallValidator::Validate(const FBPIRNode& Node, const UFunction* Function,
    FBPResolvedFunctionSignature& OutSignature, TArray<FString>& OutDiagnostics)
{
    OutSignature = FBPResolvedFunctionSignature();
    if (Node.ClassPath != TEXT("/Script/BlueprintGraph.K2Node_CallFunction"))
    {
        OutDiagnostics.Add(TEXT("Node is not a K2Node_CallFunction"));
        return false;
    }
    if (Function == nullptr || Node.FunctionName.IsNone() || Node.FunctionName != Function->GetFName())
    {
        OutDiagnostics.Add(TEXT("Function reference does not match the call node"));
        return false;
    }

    FBPResolvedFunctionSignature Candidate;
    if (FBPFunctionSignatureResolver::Resolve(Function, Candidate, OutDiagnostics) == false)
    {
        return false;
    }

    // ピンの名前だけでなく入出力方向も検証する。
    // ReturnValueは戻り値、OutParmは出力ピンとして扱う。
    for (int32 Index = 0; Index < Candidate.ParameterNames.Num(); ++Index)
    {
        const FName Name = Candidate.ParameterNames[Index];
        const bool bOutput = Candidate.OutputParameters[Index];
        const EEdGraphPinDirection ExpectedDirection = bOutput ? EGPD_Output : EGPD_Input;
        int32 Matches = 0;
        for (const FBPIRPin& Pin : Node.Pins)
        {
            if (Pin.Name == Name)
            {
                ++Matches;
                if (Pin.Direction != ExpectedDirection)
                {
                    OutDiagnostics.Add(FString::Printf(TEXT("Pin direction mismatch: %s"), *Name.ToString()));
                    return false;
                }
                // 実行ピンをデータ引数として受理しない。
                if (FBPFunctionPinTypeMatcher::Matches(Pin, Candidate.ParameterTypes[Index]) == false)
                {
                    OutDiagnostics.Add(FString::Printf(TEXT("Parameter pin type mismatch: %s"), *Name.ToString()));
                    return false;
                }
            }
        }
        if (Matches != 1)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Expected one parameter pin: %s (found %d)"),
                *Name.ToString(), Matches));
            return false;
        }
    }

    if (Candidate.ReturnType.Kind != EBPSemanticTypeKind::Void)
    {
        int32 ReturnPins = 0;
        for (const FBPIRPin& Pin : Node.Pins)
        {
            if (Pin.Name == TEXT("ReturnValue"))
            {
                ++ReturnPins;
                if (Pin.Direction != EGPD_Output ||
                    FBPFunctionPinTypeMatcher::Matches(Pin, Candidate.ReturnType) == false)
                {
                    OutDiagnostics.Add(TEXT("Invalid ReturnValue pin"));
                    return false;
                }
            }
        }
        if (ReturnPins != 1)
        {
            OutDiagnostics.Add(TEXT("ReturnValue pin missing or duplicated"));
            return false;
        }
    }

    // void関数にReturnValueが存在する場合は、未対応の出力を黙って破棄しない。
    if (Candidate.ReturnType.Kind == EBPSemanticTypeKind::Void)
    {
        for (const FBPIRPin& Pin : Node.Pins)
        {
            if (Pin.Name == TEXT("ReturnValue"))
            {
                OutDiagnostics.Add(TEXT("Void function must not have ReturnValue pin"));
                return false;
            }
        }
    }

    // selfピンはインスタンス関数の入力に限定する。
    int32 SelfPins = 0;
    for (const FBPIRPin& Pin : Node.Pins)
    {
        if (Pin.Name == TEXT("self"))
        {
            ++SelfPins;
            if (Candidate.bStatic || Pin.Direction != EGPD_Input ||
                Pin.Category == UEdGraphSchema_K2::PC_Exec)
            {
                OutDiagnostics.Add(TEXT("Invalid self pin"));
                return false;
            }
        }
    }
    if (SelfPins > 1)
    {
        OutDiagnostics.Add(TEXT("Duplicate self pins"));
        return false;
    }

    // 未知のデータピンを黙って無視すると引数や出力が欠落する。
    for (const FBPIRPin& Pin : Node.Pins)
    {
        if (Pin.Category == UEdGraphSchema_K2::PC_Exec ||
            Pin.Name == TEXT("self") || Pin.Name == TEXT("ReturnValue"))
        {
            continue;
        }
        if (Candidate.ParameterNames.Contains(Pin.Name) == false)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Unknown call pin: %s"), *Pin.Name.ToString()));
            return false;
        }
    }

    OutSignature = MoveTemp(Candidate);
    return true;
}
