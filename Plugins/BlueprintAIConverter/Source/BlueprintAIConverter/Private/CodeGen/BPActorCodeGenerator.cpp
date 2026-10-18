#include "BPActorCodeGenerator.h"
#include "BPParentClassResolver.h"
#include "BPPrintStringAnalyzer.h"

namespace
{
bool IsIdentifier(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Character = Name[Index];
        const bool bAlpha = (Character >= TEXT('A') && Character <= TEXT('Z')) ||
            (Character >= TEXT('a') && Character <= TEXT('z'));
        const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
        if (bAlpha == false && Character != TEXT('_') && (Index == 0 || bDigit == false))
        {
            return false;
        }
    }
    return true;
}

bool EscapeCppString(const FString& Value, FString& Out, TArray<FString>& Diagnostics)
{
    Out.Reset();
    for (int32 Index = 0; Index < Value.Len(); ++Index)
    {
        const TCHAR Character = Value[Index];
        switch (Character)
        {
        case TEXT('\\'): Out += TEXT("\\\\"); break;
        case TEXT('"'): Out += TEXT("\\\""); break;
        case TEXT('\n'): Out += TEXT("\\n"); break;
        case TEXT('\r'): Out += TEXT("\\r"); break;
        case TEXT('\t'): Out += TEXT("\\t"); break;
        case TEXT('\0'):
            Diagnostics.Add(TEXT("NUL in PrintString literal is unsupported"));
            return false;
        default:
            // C++の文字列リテラルに制御文字を直接埋め込まない。
            if (Character < 0x20)
            {
                Diagnostics.Add(TEXT("Unsupported control character in PrintString literal"));
                return false;
            }
            Out.AppendChar(Character);
            break;
        }
    }
    return true;
}
}

bool FBPActorCodeGenerator::Generate(const FBPGraphIR& IR, const FString& ClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
    TArray<FString>& OutDiagnostics)
{
    OutFiles = FBPGeneratedActorFiles();
    OutDiagnostics.Reset();

    if (ModuleApiMacro.IsEmpty() == false &&
        (IsIdentifier(ModuleApiMacro) == false || ModuleApiMacro.EndsWith(TEXT("_API")) == false))
    {
        OutDiagnostics.Add(TEXT("Module API macro must be empty or a valid MODULE_API identifier"));
        return false;
    }
    FBPResolvedParentClass ParentClass;
    if (FBPParentClassResolver::ResolveParent(
        IR.ParentClassPath, ParentClass, OutDiagnostics) == false)
    {
        return false;
    }

    // BeginPlayを持つ親クラスに限り、このMVPのイベント変換を許可する。
    // UObjectなどにはBeginPlayが存在しないため、無効なoverrideを生成しない。
    if (ParentClass.bSupportsBeginPlay == false)
    {
        OutDiagnostics.Add(TEXT("Parent class does not support BeginPlay; this MVP requires ReceiveBeginPlay"));
        return false;
    }
    // UEのクラス接頭辞は親クラスの種類に合わせる（Actor=A、Component=Uなど）。
    const FString& Prefix = ParentClass.GeneratedClassPrefix;
    if (ClassName.StartsWith(Prefix) == false || ClassName.Len() <= Prefix.Len() ||
        IsIdentifier(ClassName) == false)
    {
        OutDiagnostics.Add(FString::Printf(TEXT("Class name must use the parent prefix %s and be a valid C++ identifier"), *Prefix));
        return false;
    }

    if (IR.BlueprintVariableCount != 0)
    {
        OutDiagnostics.Add(TEXT("MVP cannot generate Blueprint member variables"));
        return false;
    }
    // Actor Blueprintにはユーザーが関数を作成していなくても
    // Construction Scriptが標準グラフとして含まれる場合がある。
    // 実行接続や関数呼び出しがある場合は動作を失うため、必ず変換を拒否する。
    FBPGraphIR ConversionIR = IR;
    ConversionIR.Graphs.Reset();
    for (const FBPIRGraph& Graph : IR.Graphs)
    {
        if (Graph.Kind == TEXT("function") && Graph.Name == TEXT("UserConstructionScript"))
        {
            bool bHasExecutableNodes = false;
            for (const FBPIRNode& Node : Graph.Nodes)
            {
                if (Node.FunctionName.IsNone() == false || Node.VariableName.IsNone() == false)
                {
                    bHasExecutableNodes = true;
                    break;
                }
            }
            if (Graph.ExecEdges.IsEmpty() && Graph.DataEdges.IsEmpty() && bHasExecutableNodes == false)
            {
                // 未接続の標準Construction Scriptのみ変換対象から除外する。
                continue;
            }
        }
        ConversionIR.Graphs.Add(Graph);
    }
    if (ConversionIR.Graphs.Num() != 1 || ConversionIR.Graphs[0].Kind != TEXT("event"))
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("MVP requires exactly one event graph; found %d graph(s) after ignoring an empty Construction Script"),
            ConversionIR.Graphs.Num()));
        for (const FBPIRGraph& Graph : IR.Graphs)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Graph: %s (kind=%s, nodes=%d, exec edges=%d, data edges=%d)"),
                *Graph.Name, *Graph.Kind, Graph.Nodes.Num(), Graph.ExecEdges.Num(), Graph.DataEdges.Num()));
        }
        return false;
    }

    FBPPrintStringCall Call;
    if (FBPPrintStringAnalyzer::Analyze(ConversionIR, Call, OutDiagnostics) == false)
    {
        return false;
    }

    FString EscapedText;
    FString EscapedKey;
    if (EscapeCppString(Call.InString, EscapedText, OutDiagnostics) == false ||
        EscapeCppString(Call.Key, EscapedKey, OutDiagnostics) == false)
    {
        return false;
    }

    // ファイル名とUHT generated header名を一致させる。
    const FString BaseName = ClassName.Mid(ParentClass.GeneratedClassPrefix.Len());
    OutFiles.HeaderFileName = BaseName + TEXT(".h");
    OutFiles.SourceFileName = BaseName + TEXT(".cpp");

    const FString ApiPrefix = ModuleApiMacro.IsEmpty() ? FString() : ModuleApiMacro + TEXT(" ");
    OutFiles.HeaderContent = FString::Printf(
        TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\nprotected:\n    virtual void BeginPlay() override;\n};\n"),
        *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName, *ParentClass.CppClassName);

    // PrintStringの引数を省略せず渡し、画面/ログ出力・Duration・Keyを保存する。
    const TCHAR* Screen = Call.bPrintToScreen ? TEXT("true") : TEXT("false");
    const TCHAR* Log = Call.bPrintToLog ? TEXT("true") : TEXT("false");
    // 数値が整数の場合でもC++の浮動小数点リテラルになるよう小数点を補う。
    FString Duration = FString::SanitizeFloat(Call.Duration);
    if (Duration.Contains(TEXT(".")) == false && Duration.Contains(TEXT("e"), ESearchCase::IgnoreCase) == false)
    {
        Duration += TEXT(".0");
    }
    Duration += TEXT("f");
    // 色成分はSanitizeFloatでC++数値リテラルへ変換し、整数にも小数点を付ける。
    const auto ColorComponent = [](float Value) -> FString
    {
        FString Result = FString::SanitizeFloat(Value);
        if (Result.Contains(TEXT(".")) == false &&
            Result.Contains(TEXT("e"), ESearchCase::IgnoreCase) == false)
        {
            Result += TEXT(".0");
        }
        return Result + TEXT("f");
    };
    const FString Red = ColorComponent(Call.TextColor.R);
    const FString Green = ColorComponent(Call.TextColor.G);
    const FString Blue = ColorComponent(Call.TextColor.B);
    const FString Alpha = ColorComponent(Call.TextColor.A);
    OutFiles.SourceContent = FString::Printf(
        TEXT("#include \"%s.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    UKismetSystemLibrary::PrintString(this, TEXT(\"%s\"), %s, %s, FLinearColor(%s, %s, %s, %s), %s, TEXT(\"%s\"));\n}\n"),
        *BaseName, *ClassName, *EscapedText, Screen, Log, *Red, *Green, *Blue, *Alpha, *Duration, *EscapedKey);
    return true;
}
