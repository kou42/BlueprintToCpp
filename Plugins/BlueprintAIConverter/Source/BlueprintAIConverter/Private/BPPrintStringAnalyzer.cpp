#include "BPPrintStringAnalyzer.h"

namespace
{
const FBPIRPin* FindPin(const FBPIRNode& Node, const FName Name, const EEdGraphPinDirection Direction)
{
    for (const FBPIRPin& Pin : Node.Pins)
    {
        if (Pin.Name == Name && Pin.Direction == Direction)
        {
            return &Pin;
        }
    }
    return nullptr;
}

bool ReadBool(const FBPIRPin* Pin, const bool DefaultValue, bool& OutValue, TArray<FString>& Diagnostics)
{
    OutValue = DefaultValue;
    if (Pin == nullptr || Pin->DefaultValue.IsEmpty())
    {
        return true;
    }
    if (Pin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
    {
        OutValue = true;
        return true;
    }
    if (Pin->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
    {
        OutValue = false;
        return true;
    }
    Diagnostics.Add(FString::Printf(TEXT("Invalid boolean value: %s"), *Pin->DefaultValue));
    return false;
}
}

bool FBPPrintStringAnalyzer::Analyze(const FBPGraphIR& IR, FBPPrintStringCall& OutCall, TArray<FString>& OutDiagnostics)
{
    OutCall = FBPPrintStringCall();
    OutDiagnostics.Reset();
    OutDiagnostics.Append(IR.Diagnostics);

    const FBPIRGraph* MatchedGraph = nullptr;
    const FBPIRNode* BeginPlay = nullptr;
    const FBPIRNode* PrintString = nullptr;
    int32 EventCount = 0;
    int32 PrintCount = 0;

    for (const FBPIRGraph& Graph : IR.Graphs)
    {
        if (Graph.Kind != TEXT("event"))
        {
            continue;
        }
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_Event") &&
                Node.FunctionName == TEXT("ReceiveBeginPlay"))
            {
                ++EventCount;
                MatchedGraph = &Graph;
                BeginPlay = &Node;
            }
            if (Node.ClassPath == TEXT("/Script/BlueprintGraph.K2Node_CallFunction") &&
                Node.FunctionName == TEXT("PrintString"))
            {
                ++PrintCount;
                if (MatchedGraph == &Graph || BeginPlay == nullptr)
                {
                    PrintString = &Node;
                }
            }
        }
    }

    if (EventCount != 1 || PrintCount != 1 || MatchedGraph == nullptr || BeginPlay == nullptr || PrintString == nullptr)
    {
        OutDiagnostics.Add(TEXT("Exactly one BeginPlay event and one PrintString call are required"));
        return false;
    }

    // イベントと呼び出しが別グラフの場合は変換対象にしない。
    bool bPrintInGraph = false;
    for (const FBPIRNode& Node : MatchedGraph->Nodes)
    {
        if (&Node == PrintString)
        {
            bPrintInGraph = true;
        }
    }
    if (bPrintInGraph == false)
    {
        OutDiagnostics.Add(TEXT("BeginPlay and PrintString must be in the same event graph"));
        return false;
    }

    // 単一実行エッジだけを許可し、余分なノードの処理順序を推測しない。
    if (MatchedGraph->ExecEdges.Num() != 1 ||
        MatchedGraph->ExecEdges[0].SourceNode != BeginPlay->Id ||
        MatchedGraph->ExecEdges[0].TargetNode != PrintString->Id)
    {
        OutDiagnostics.Add(TEXT("Unsupported execution flow: expected BeginPlay -> PrintString"));
        return false;
    }

    const FBPIRPin* InString = FindPin(*PrintString, TEXT("InString"), EGPD_Input);
    if (InString == nullptr || InString->Category != TEXT("string"))
    {
        OutDiagnostics.Add(TEXT("PrintString.InString string input is missing"));
        return false;
    }

    if (MatchedGraph->DataEdges.IsEmpty() == false)
    {
        // データ依存の評価は未実装なので、定数へのフォールバックを禁止する。
        OutDiagnostics.Add(TEXT("Connected data inputs are not supported in PrintString MVP"));
        return false;
    }

    for (const FBPIRNode& Node : MatchedGraph->Nodes)
    {
        if (Node.Id != BeginPlay->Id && Node.Id != PrintString->Id)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Unsupported node in event graph: %s"), *Node.ClassPath));
            return false;
        }
    }

    OutCall.InString = InString->DefaultValue;

    const FBPIRPin* PrintToScreen = FindPin(*PrintString, TEXT("bPrintToScreen"), EGPD_Input);
    const FBPIRPin* PrintToLog = FindPin(*PrintString, TEXT("bPrintToLog"), EGPD_Input);
    if (ReadBool(PrintToScreen, true, OutCall.bPrintToScreen, OutDiagnostics) == false ||
        ReadBool(PrintToLog, true, OutCall.bPrintToLog, OutDiagnostics) == false)
    {
        return false;
    }

    const FBPIRPin* Duration = FindPin(*PrintString, TEXT("Duration"), EGPD_Input);
    if (Duration != nullptr && Duration->DefaultValue.IsEmpty() == false)
    {
        if (LexTryParseString(OutCall.Duration, *Duration->DefaultValue) == false)
        {
            OutDiagnostics.Add(TEXT("PrintString.Duration is not a valid float"));
            return false;
        }
    }

    const FBPIRPin* Key = FindPin(*PrintString, TEXT("Key"), EGPD_Input);
    if (Key != nullptr)
    {
        OutCall.Key = Key->DefaultValue;
    }

    // WorldContextObjectは通常自動解決されるが、明示値の意味はまだ保証できない。
    const FBPIRPin* WorldContext = FindPin(*PrintString, TEXT("WorldContextObject"), EGPD_Input);
    if (WorldContext != nullptr && WorldContext->DefaultObjectPath.IsEmpty() == false &&
        WorldContext->DefaultObjectPath != TEXT("None"))
    {
        OutDiagnostics.Add(TEXT("Explicit WorldContextObject is not supported"));
        return false;
    }

    const FBPIRPin* TextColor = FindPin(*PrintString, TEXT("TextColor"), EGPD_Input);
    if (TextColor != nullptr && TextColor->DefaultValue.IsEmpty() == false)
    {
        OutDiagnostics.Add(TEXT("Explicit TextColor requires color parsing before code generation"));
        return false;
    }

    return OutDiagnostics.Num() == 0;
}
