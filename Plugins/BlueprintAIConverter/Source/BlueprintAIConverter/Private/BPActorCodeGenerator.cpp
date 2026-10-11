#include "BPActorCodeGenerator.h"
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

    if (ClassName.StartsWith(TEXT("A")) == false || IsIdentifier(ClassName) == false || ClassName.Len() < 2)
    {
        OutDiagnostics.Add(TEXT("Actor class name must be a valid A-prefixed C++ identifier"));
        return false;
    }
    if (ModuleApiMacro.IsEmpty() == false &&
        (IsIdentifier(ModuleApiMacro) == false || ModuleApiMacro.EndsWith(TEXT("_API")) == false))
    {
        OutDiagnostics.Add(TEXT("Module API macro must be empty or a valid MODULE_API identifier"));
        return false;
    }
    if (IR.ParentClassPath != TEXT("/Script/Engine.Actor"))
    {
        OutDiagnostics.Add(TEXT("MVP supports only Blueprint classes directly derived from Actor"));
        return false;
    }

    if (IR.BlueprintVariableCount != 0)
    {
        OutDiagnostics.Add(TEXT("MVP cannot generate Blueprint member variables"));
        return false;
    }
    if (IR.Graphs.Num() != 1 || IR.Graphs[0].Kind != TEXT("event"))
    {
        OutDiagnostics.Add(TEXT("MVP requires exactly one event graph and no function or macro graphs"));
        return false;
    }

    FBPPrintStringCall Call;
    if (FBPPrintStringAnalyzer::Analyze(IR, Call, OutDiagnostics) == false)
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
    const FString BaseName = ClassName.Mid(1);
    OutFiles.HeaderFileName = BaseName + TEXT(".h");
    OutFiles.SourceFileName = BaseName + TEXT(".cpp");

    const FString ApiPrefix = ModuleApiMacro.IsEmpty() ? FString() : ModuleApiMacro + TEXT(" ");
    OutFiles.HeaderContent = FString::Printf(
        TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"GameFramework/Actor.h\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public AActor\n{\n    GENERATED_BODY()\n\nprotected:\n    virtual void BeginPlay() override;\n};\n"),
        *BaseName, *ApiPrefix, *ClassName);

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
    OutFiles.SourceContent = FString::Printf(
        TEXT("#include \"%s.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    UKismetSystemLibrary::PrintString(this, TEXT(\"%s\"), %s, %s, FLinearColor(0.0f, 0.66f, 1.0f), %s, TEXT(\"%s\"));\n}\n"),
        *BaseName, *ClassName, *EscapedText, Screen, Log, *Duration, *EscapedKey);
    return true;
}
