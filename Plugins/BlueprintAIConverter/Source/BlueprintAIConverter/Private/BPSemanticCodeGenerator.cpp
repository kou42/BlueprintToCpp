#include "BPSemanticCodeGenerator.h"
#include "BPParentClassResolver.h"

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
                Diagnostics.Add(TEXT("Unsupported control character in string literal"));
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

FString DoubleLiteral(double Value)
{
    FString Result = LexToString(Value);
    if (Result.Contains(TEXT(".")) == false && Result.Contains(TEXT("e"), ESearchCase::IgnoreCase) == false)
    {
        Result += TEXT(".0");
    }
    return Result;
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

bool IsSafeReflectedCppType(const FString& Type)
{
    if (Type.IsEmpty() || Type.Len() > 512)
    {
        return false;
    }
    int32 TemplateDepth = 0;
    bool bSawIdentifier = false;
    for (const TCHAR Character : Type)
    {
        const bool bAlphaNumeric =
            (Character >= TEXT('A') && Character <= TEXT('Z')) ||
            (Character >= TEXT('a') && Character <= TEXT('z')) ||
            (Character >= TEXT('0') && Character <= TEXT('9'));
        if (bAlphaNumeric || Character == TEXT('_'))
        {
            bSawIdentifier = true;
            continue;
        }
        if (Character == TEXT('<'))
        {
            ++TemplateDepth;
            continue;
        }
        if (Character == TEXT('>'))
        {
            --TemplateDepth;
            if (TemplateDepth < 0)
            {
                return false;
            }
            continue;
        }
        if (Character == TEXT(':') || Character == TEXT(',') ||
            Character == TEXT('*') || Character == TEXT(' ') || Character == TEXT('\t'))
        {
            continue;
        }
        return false;
    }
    return bSawIdentifier && TemplateDepth == 0;
}

bool CanEmitDeclarationOnly(const FBPSemanticVariable& Variable, FString& OutReason)
{
    if (IsSafeReflectedCppType(Variable.ReflectedCppType) == false)
    {
        OutReason = TEXT("reflected C++ type is unavailable or unsafe");
        return false;
    }
    if (Variable.TypePath.StartsWith(TEXT("/Game/")))
    {
        OutReason = TEXT("Blueprint-defined type has no verified native C++ declaration");
        return false;
    }
    uint64 Flags = 0;
    if (Variable.PropertyFlags.IsEmpty() == false &&
        LexTryParseString(Flags, *Variable.PropertyFlags) == false)
    {
        OutReason = TEXT("property flags are invalid");
        return false;
    }
    if ((Flags & static_cast<uint64>(CPF_InstancedReference | CPF_ContainsInstancedReference)) != 0)
    {
        OutReason = TEXT("instanced property cannot be replaced by default construction");
        return false;
    }
    return true;
}

bool EmitDeclarationOnly(const FBPSemanticVariable& Variable, const FString& Name,
    FString& Out, TArray<FString>& Diagnostics)
{
    FString Reason;
    if (CanEmitDeclarationOnly(Variable, Reason) == false)
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Blueprint variable cannot use declaration-only fallback: %s (%s)"),
            *Name, *Reason));
        return false;
    }
    const FString Guid = Variable.Guid.IsValid()
        ? Variable.Guid.ToString(EGuidFormats::DigitsWithHyphens)
        : TEXT("取得不可");
    Out += FString::Printf(
        TEXT("    // 部分変換: Blueprintのデフォルト値とプロパティメタデータは保持されていません。\n")
        TEXT("    // 元の変数: %s (GUID: %s)。必要なヘッダーとモジュール依存関係を確認してください。\n")
        TEXT("    UPROPERTY()\n    %s %s{};\n\n"),
        *Name, *Guid, *Variable.ReflectedCppType, *Name);
    Diagnostics.Add(FString::Printf(
        TEXT("Partial variable conversion: %s was emitted as %s %s{}; without its Blueprint default value"),
        *Name, *Variable.ReflectedCppType, *Name));
    return true;
}

bool EmitVariables(const TArray<FBPSemanticVariable>& Variables, FString& Out,
    TArray<FString>& Diagnostics, const EBPVariableFallbackMode FallbackMode)
{
    Out.Reset();
    TSet<FName> Names;
    for (const FBPSemanticVariable& Variable : Variables)
    {
        const FString Name = Variable.Name.ToString();
        if (IsIdentifier(Name) == false)
        {
            Diagnostics.Add(FString::Printf(TEXT("Blueprint variable is not a valid C++ identifier: %s"), *Name));
            return false;
        }
        if (Names.Contains(Variable.Name))
        {
            Diagnostics.Add(FString::Printf(TEXT("Duplicate Blueprint variable: %s"), *Name));
            return false;
        }
        Names.Add(Variable.Name);
        if (Variable.Type.bIsReference || Variable.Type.bIsConst)
        {
            Diagnostics.Add(FString::Printf(TEXT("Unsupported Blueprint variable qualifier: %s"), *Name));
            return false;
        }
        const bool bExactInitializationAvailable =
            Variable.bDefaultValueAvailable && Variable.DefaultValueSource == TEXT("cdo") &&
            Variable.InitializationStrategy != EBPVariableInitializationStrategy::Unsupported;
        if (bExactInitializationAvailable == false)
        {
            if (FallbackMode == EBPVariableFallbackMode::DeclarationOnly)
            {
                if (EmitDeclarationOnly(Variable, Name, Out, Diagnostics))
                {
                    continue;
                }
                return false;
            }
            Diagnostics.Add(FString::Printf(TEXT("Blueprint variable has no verified CDO default: %s"), *Name));
            return false;
        }

        FString CppType;
        FString Initializer;
        EBPVariableInitializationStrategy ExpectedStrategy =
            EBPVariableInitializationStrategy::Unsupported;
        switch (Variable.Type.Kind)
        {
        case EBPSemanticTypeKind::Bool:
            if (Variable.DefaultValue != TEXT("true") && Variable.DefaultValue != TEXT("false"))
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid bool default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("bool");
            ExpectedStrategy = EBPVariableInitializationStrategy::DirectLiteral;
            Initializer = Variable.DefaultValue;
            break;
        case EBPSemanticTypeKind::Int32:
        {
            int32 Value = 0;
            if (LexTryParseString(Value, *Variable.DefaultValue) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid int32 default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("int32");
            ExpectedStrategy = EBPVariableInitializationStrategy::DirectLiteral;
            Initializer = LexToString(Value);
            break;
        }
        case EBPSemanticTypeKind::Int64:
        {
            int64 Value = 0;
            if (LexTryParseString(Value, *Variable.DefaultValue) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid int64 default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("int64");
            ExpectedStrategy = EBPVariableInitializationStrategy::DirectLiteral;
            Initializer = Value == MIN_int64
                ? TEXT("(-9223372036854775807LL - 1LL)")
                : LexToString(Value) + TEXT("LL");
            break;
        }
        case EBPSemanticTypeKind::Float:
        {
            float Value = 0.0f;
            if (LexTryParseString(Value, *Variable.DefaultValue) == false || FMath::IsFinite(Value) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid float default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("float");
            ExpectedStrategy = EBPVariableInitializationStrategy::DirectLiteral;
            Initializer = FloatLiteral(Value);
            break;
        }
        case EBPSemanticTypeKind::Double:
        {
            double Value = 0.0;
            if (LexTryParseString(Value, *Variable.DefaultValue) == false || FMath::IsFinite(Value) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid double default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("double");
            ExpectedStrategy = EBPVariableInitializationStrategy::DirectLiteral;
            Initializer = DoubleLiteral(Value);
            break;
        }
        case EBPSemanticTypeKind::String:
        {
            FString Escaped;
            if (EscapeString(Variable.DefaultValue, Escaped, Diagnostics) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid FString default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("FString");
            ExpectedStrategy = EBPVariableInitializationStrategy::StringLiteral;
            Initializer = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
            break;
        }
        case EBPSemanticTypeKind::Name:
        {
            FString Escaped;
            if (EscapeString(Variable.DefaultValue, Escaped, Diagnostics) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid FName default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("FName");
            ExpectedStrategy = EBPVariableInitializationStrategy::NameLiteral;
            Initializer = Variable.DefaultValue.IsEmpty()
                ? TEXT("NAME_None")
                : FString::Printf(TEXT("FName(TEXT(\"%s\"))"), *Escaped);
            break;
        }
        case EBPSemanticTypeKind::Text:
        {
            FString EscapedValue;
            if (EscapeString(Variable.DefaultValue, EscapedValue, Diagnostics) == false)
            {
                Diagnostics.Add(FString::Printf(TEXT("Invalid FText default for Blueprint variable: %s"), *Name));
                return false;
            }
            CppType = TEXT("FText");
            ExpectedStrategy = EBPVariableInitializationStrategy::TextLiteral;
            if (Variable.DefaultValue.IsEmpty())
            {
                Initializer = TEXT("FText::GetEmpty()");
            }
            else if (Variable.bTextCultureInvariant)
            {
                Initializer = FString::Printf(
                    TEXT("FText::AsCultureInvariant(TEXT(\"%s\"))"), *EscapedValue);
            }
            else if (Variable.bTextLocalizationIdentityAvailable)
            {
                FString EscapedNamespace;
                FString EscapedKey;
                FString EscapedSource;
                if (EscapeString(Variable.TextNamespace, EscapedNamespace, Diagnostics) == false ||
                    EscapeString(Variable.TextKey, EscapedKey, Diagnostics) == false ||
                    EscapeString(Variable.TextSource, EscapedSource, Diagnostics) == false)
                {
                    Diagnostics.Add(FString::Printf(
                        TEXT("Invalid localized FText identity for Blueprint variable: %s"), *Name));
                    return false;
                }
                Initializer = FString::Printf(
                    TEXT("NSLOCTEXT(\"%s\", \"%s\", \"%s\")"),
                    *EscapedNamespace, *EscapedKey, *EscapedSource);
            }
            else
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("FText localization identity is unavailable for Blueprint variable: %s"), *Name));
                return false;
            }
            break;
        }
        default:
            Diagnostics.Add(FString::Printf(TEXT("Unsupported Blueprint variable type: %s"), *Name));
            return false;
        }

        if (Variable.InitializationStrategy != ExpectedStrategy)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Blueprint variable initialization strategy mismatch: %s"), *Name));
            return false;
        }
        if (Variable.ReflectedCppType != CppType)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Blueprint variable reflected C++ type mismatch: %s (%s != %s)"),
                *Name, *Variable.ReflectedCppType, *CppType));
            return false;
        }
        // 宣言にはPinCategoryから組み立てた名前ではなくFPropertyの型名を使用する。
        CppType = Variable.ReflectedCppType;

        Out += FString::Printf(
            TEXT("    UPROPERTY()\n    %s %s = %s;\n\n"),
            *CppType, *Name, *Initializer);
    }
    return true;
}
}

bool FBPSemanticCodeGenerator::Generate(const FBPSemanticIR& IR, const FString& ClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics,
    const EBPVariableFallbackMode VariableFallbackMode)
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
    FBPResolvedParentClass ParentClass;
    if (FBPParentClassResolver::ResolveActorParent(
        IR.ParentClassPath, ParentClass, OutDiagnostics) == false)
    {
        return false;
    }
    if (IR.Functions.Num() != 1)
    {
        OutDiagnostics.Add(TEXT("Semantic MVP requires one function on an Actor subclass"));
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
        Context.Type.TypePath != IR.ParentClassPath)
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

    FString VariableDeclarations;
    if (EmitVariables(IR.Variables, VariableDeclarations, OutDiagnostics,
        VariableFallbackMode) == false)
    {
        return false;
    }

    const FString BaseName = ClassName.Mid(1);
    const FString ApiPrefix = ModuleApiMacro.IsEmpty() ? FString() : ModuleApiMacro + TEXT(" ");
    FBPGeneratedActorFiles Files;
    Files.HeaderFileName = BaseName + TEXT(".h");
    Files.SourceFileName = BaseName + TEXT(".cpp");
    Files.HeaderContent = FString::Printf(
        TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%sprotected:\n    virtual void BeginPlay() override;\n};\n"),
        *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName, *ParentClass.CppClassName,
        *VariableDeclarations);
    Files.SourceContent = FString::Printf(
        TEXT("#include \"%s.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    UKismetSystemLibrary::PrintString(this, TEXT(\"%s\"), %s, %s, FLinearColor(%s, %s, %s, %s), %s, TEXT(\"%s\"));\n}\n"),
        *BaseName, *ClassName, *EscapedText, *Screen, *Log,
        *FloatLiteral(LinearColor.R), *FloatLiteral(LinearColor.G),
        *FloatLiteral(LinearColor.B), *FloatLiteral(LinearColor.A),
        *FloatLiteral(Duration), *EscapedKey);
    OutFiles = MoveTemp(Files);
    return true;
}
