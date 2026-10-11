#include "BPSemanticCodeGenerator.h"

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

bool EscapeString(const FString& Value, FString& Out, TArray<FString>& Diagnostics)
{
    Out.Reset();
    for (const TCHAR Character : Value)
    {
        switch (Character)
        {
        case TEXT('\\'): Out += TEXT("\\\\"); break;
        case TEXT('"'): Out += TEXT("\\\""); break;
        case TEXT('\n'): Out += TEXT("\\n"); break;
        case TEXT('\r'): Out += TEXT("\\r"); break;
        case TEXT('\t'): Out += TEXT("\\t"); break;
        default:
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

FString FloatLiteral(float Value)
{
    FString Result = FString::SanitizeFloat(Value);
    if (Result.Contains(TEXT(".")) == false && Result.Contains(TEXT("e"), ESearchCase::IgnoreCase) == false)
    {
        Result += TEXT(".0");
    }
    return Result + TEXT("f");
}

bool ReadArgument(const FBPSemanticExpression& Arg, EBPSemanticTypeKind Expected,
    FString& Out, TArray<FString>& Diagnostics)
{
    if (Arg.Kind != EBPSemanticExpressionKind::Literal || Arg.Type.Kind != Expected)
    {
        Diagnostics.Add(TEXT("Unsupported PrintString argument expression or type"));
        return false;
    }
    Out = Arg.LiteralValue;
    return true;
}
}

bool FBPSemanticCodeGenerator::Generate(const FBPSemanticIR& IR, const FString& ClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics)
{
    OutFiles = FBPGeneratedActorFiles();
    OutDiagnostics.Reset();
    for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics)
    {
        OutDiagnostics.Add(Diagnostic.Message);
    }
    if (IR.HasErrors())
    {
        return false;
    }
    if (ClassName.Len() < 2 || ClassName.StartsWith(TEXT("A")) == false || IsIdentifier(ClassName) == false)
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
    if (IR.ParentClassPath != TEXT("/Script/Engine.Actor") || IR.Functions.Num() != 1)
    {
        OutDiagnostics.Add(TEXT("Semantic MVP requires one function on a direct Actor subclass"));
        return false;
    }
    const FBPSemanticFunction& Function = IR.Functions[0];
    if (Function.Name != TEXT("BeginPlay") || Function.ReturnType.Kind != EBPSemanticTypeKind::Void ||
        Function.Blocks.Num() != 1 || Function.Blocks[0].Statements.Num() != 1)
    {
        OutDiagnostics.Add(TEXT("Semantic MVP requires one BeginPlay block and statement"));
        return false;
    }
    const FBPSemanticStatement& Statement = Function.Blocks[0].Statements[0];
    const FBPSemanticExpression& Call = Statement.Expression;
    if (Statement.Kind != EBPSemanticStatementKind::Expression ||
        Call.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Call.Type.Kind != EBPSemanticTypeKind::Void ||
        Call.FunctionName != TEXT("PrintString") ||
        Call.FunctionOwnerPath != TEXT("/Script/Engine.KismetSystemLibrary") ||
        Call.Arguments.Num() != 7)
    {
        OutDiagnostics.Add(TEXT("Unsupported semantic statement or function signature"));
        return false;
    }
    const FBPSemanticExpression& Context = Call.Arguments[0];
    if (Context.Kind != EBPSemanticExpressionKind::VariableReference ||
        Context.VariableName != TEXT("this") ||
        Context.Type.Kind != EBPSemanticTypeKind::Object ||
        Context.Type.TypePath != TEXT("/Script/Engine.Actor"))
    {
        OutDiagnostics.Add(TEXT("Unsupported PrintString world context"));
        return false;
    }

    FString Text, Screen, Log, Color, DurationText, Key;
    if (ReadArgument(Call.Arguments[1], EBPSemanticTypeKind::String, Text, OutDiagnostics) == false ||
        ReadArgument(Call.Arguments[2], EBPSemanticTypeKind::Bool, Screen, OutDiagnostics) == false ||
        ReadArgument(Call.Arguments[3], EBPSemanticTypeKind::Bool, Log, OutDiagnostics) == false ||
        ReadArgument(Call.Arguments[4], EBPSemanticTypeKind::Struct, Color, OutDiagnostics) == false ||
        ReadArgument(Call.Arguments[5], EBPSemanticTypeKind::Float, DurationText, OutDiagnostics) == false ||
        ReadArgument(Call.Arguments[6], EBPSemanticTypeKind::String, Key, OutDiagnostics) == false)
    {
        return false;
    }
    if ((Screen != TEXT("true") && Screen != TEXT("false")) ||
        (Log != TEXT("true") && Log != TEXT("false")) ||
        Call.Arguments[4].Type.TypePath != TEXT("/Script/CoreUObject.LinearColor"))
    {
        OutDiagnostics.Add(TEXT("Invalid PrintString boolean or color type"));
        return false;
    }
    float Duration = 0.0f;
    FLinearColor LinearColor;
    if (LexTryParseString(Duration, *DurationText) == false || FMath::IsFinite(Duration) == false ||
        LinearColor.InitFromString(Color) == false ||
        FMath::IsFinite(LinearColor.R) == false || FMath::IsFinite(LinearColor.G) == false ||
        FMath::IsFinite(LinearColor.B) == false || FMath::IsFinite(LinearColor.A) == false)
    {
        OutDiagnostics.Add(TEXT("Invalid PrintString duration or color"));
        return false;
    }
    FString EscapedText, EscapedKey;
    if (EscapeString(Text, EscapedText, OutDiagnostics) == false ||
        EscapeString(Key, EscapedKey, OutDiagnostics) == false)
    {
        return false;
    }

    const FString BaseName = ClassName.Mid(1);
    const FString ApiPrefix = ModuleApiMacro.IsEmpty() ? FString() : ModuleApiMacro + TEXT(" ");
    FBPGeneratedActorFiles Files;
    Files.HeaderFileName = BaseName + TEXT(".h");
    Files.SourceFileName = BaseName + TEXT(".cpp");
    Files.HeaderContent = FString::Printf(
        TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"GameFramework/Actor.h\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public AActor\n{\n    GENERATED_BODY()\n\nprotected:\n    virtual void BeginPlay() override;\n};\n"),
        *BaseName, *ApiPrefix, *ClassName);
    Files.SourceContent = FString::Printf(
        TEXT("#include \"%s.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    UKismetSystemLibrary::PrintString(this, TEXT(\"%s\"), %s, %s, FLinearColor(%s, %s, %s, %s), %s, TEXT(\"%s\"));\n}\n"),
        *BaseName, *ClassName, *EscapedText, *Screen, *Log,
        *FloatLiteral(LinearColor.R), *FloatLiteral(LinearColor.G),
        *FloatLiteral(LinearColor.B), *FloatLiteral(LinearColor.A),
        *FloatLiteral(Duration), *EscapedKey);
    OutFiles = MoveTemp(Files);
    return true;
}
