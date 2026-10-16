#include "BPSemanticCodeGenerator.h"
#include "BPNumericArithmeticResolver.h"
#include "BPNumericComparisonResolver.h"
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

bool EmitNumericLiteral(const EBPSemanticTypeKind Type, const FString& Value,
    FString& OutLiteral)
{
    FString CanonicalValue;
    if (FBPNumericComparisonResolver::CanonicalizeLiteral(
        Type, Value, CanonicalValue) == false)
    {
        return false;
    }
    if (Type == EBPSemanticTypeKind::Int32)
    {
        OutLiteral = MoveTemp(CanonicalValue);
        return true;
    }
    if (Type == EBPSemanticTypeKind::Float)
    {
        float Parsed = 0.0f;
        if (LexTryParseString(Parsed, *CanonicalValue) == false)
        {
            return false;
        }
        OutLiteral = FloatLiteral(Parsed);
        return true;
    }
    if (Type == EBPSemanticTypeKind::Double)
    {
        double Parsed = 0.0;
        if (LexTryParseString(Parsed, *CanonicalValue) == false)
        {
            return false;
        }
        OutLiteral = DoubleLiteral(Parsed);
        return true;
    }
    return false;
}

bool ReadArgument(const FBPSemanticExpression& Arg, EBPSemanticTypeKind Expected,
    FString& Out, TArray<FString>& Diagnostics)
{
    if (Arg.Kind != EBPSemanticExpressionKind::Literal || Arg.Type.Kind != Expected)
    {
        Diagnostics.Add(TEXT("Unsupported literal argument expression or type"));
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

bool CanEmitImportText(const FBPSemanticVariable& Variable, FString& OutReason)
{
    if (CanEmitDeclarationOnly(Variable, OutReason) == false)
    {
        return false;
    }
    if (Variable.bDefaultValueAvailable == false ||
        Variable.DefaultValueSource != TEXT("cdo_export_text") ||
        Variable.bExportedDefaultValueAvailable == false ||
        Variable.bExportTextRoundTripVerified == false ||
        Variable.DefaultValue != Variable.ExportedDefaultValue)
    {
        OutReason = TEXT("ExportText value was not verified against the CDO");
        return false;
    }
    if (Variable.ReflectedPropertyClass.IsEmpty())
    {
        OutReason = TEXT("reflected property class is unavailable");
        return false;
    }
    return true;
}

bool EmitVariables(const TArray<FBPSemanticVariable>& Variables, const FString& ClassName,
    FString& Out, FString& OutConstructorBody, TArray<FString>& Diagnostics,
    const EBPVariableFallbackMode FallbackMode)
{
    Out.Reset();
    OutConstructorBody.Reset();
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

        if (Variable.InitializationStrategy == EBPVariableInitializationStrategy::ImportText)
        {
            FString Reason;
            if (CanEmitImportText(Variable, Reason) == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Blueprint variable cannot use verified ImportText initialization: %s (%s)"),
                    *Name, *Reason));
                return false;
            }
            FString EscapedExportText;
            if (EscapeString(Variable.ExportedDefaultValue, EscapedExportText, Diagnostics) == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Invalid ExportText default for Blueprint variable: %s"), *Name));
                return false;
            }
            Out += FString::Printf(
                TEXT("    UPROPERTY()\n    %s %s{};\n\n"),
                *Variable.ReflectedCppType, *Name);
            OutConstructorBody += FString::Printf(
                TEXT("    // ExportText/ImportTextの往復検証済み値からBlueprintのデフォルト値を復元します。\n")
                TEXT("    if (FProperty* Property = FindFProperty<FProperty>(StaticClass(), GET_MEMBER_NAME_CHECKED(%s, %s)))\n")
                TEXT("    {\n")
                TEXT("        Property->ImportText_Direct(TEXT(\"%s\"), Property->ContainerPtrToValuePtr<void>(this), this, PPF_None);\n")
                TEXT("    }\n"),
                *ClassName, *Name, *EscapedExportText);
            continue;
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

bool ReadBoolLiteral(const FBPSemanticExpression& Expression, FString& OutValue)
{
    if (Expression.Kind != EBPSemanticExpressionKind::Literal ||
        Expression.Type.Kind != EBPSemanticTypeKind::Bool ||
        (Expression.LiteralValue != TEXT("true") && Expression.LiteralValue != TEXT("false")))
    {
        return false;
    }
    OutValue = Expression.LiteralValue;
    return true;
}

bool EmitNumericExpression(const FBPSemanticExpression& Expression,
    const EBPSemanticTypeKind OperandType,
    const TArray<FBPSemanticVariable>& Variables, FString& OutOperand,
    TArray<FString>& Diagnostics, const FName LocalIndex = NAME_None)
{
    if (Expression.Type.Kind != OperandType ||
        Expression.Type.bIsReference || Expression.Type.bIsConst)
    {
        Diagnostics.Add(TEXT("Numeric comparison operand type is invalid"));
        return false;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::Literal)
    {
        if (EmitNumericLiteral(OperandType,
            Expression.LiteralValue, OutOperand) == false)
        {
            Diagnostics.Add(TEXT("Invalid numeric comparison literal"));
            return false;
        }
        return true;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::FunctionCall)
    {
        FBPNumericArithmeticOperation Operation;
        if (Expression.Arguments.Num() != 2 ||
            FBPNumericArithmeticResolver::Resolve(Expression.FunctionOwnerPath,
                Expression.FunctionName, Operation, Diagnostics) == false ||
            Operation.OperandType != OperandType)
        {
            Diagnostics.Add(TEXT("Unsupported numeric arithmetic expression"));
            return false;
        }
        FString A;
        FString B;
        if (EmitNumericExpression(Expression.Arguments[0], OperandType,
                Variables, A, Diagnostics, LocalIndex) == false ||
            EmitNumericExpression(Expression.Arguments[1], OperandType,
                Variables, B, Diagnostics, LocalIndex) == false)
        {
            return false;
        }
        // 除算を含め、Blueprint側のゼロ除算処理と同じ実装を呼び出す。
        OutOperand = FString::Printf(TEXT("UKismetMathLibrary::%s(%s, %s)"),
            *Operation.FunctionName.ToString(), *A, *B);
        return true;
    }
    if (Expression.Kind != EBPSemanticExpressionKind::VariableReference)
    {
        Diagnostics.Add(TEXT("Unsupported numeric comparison operand expression"));
        return false;
    }

    const FString Name = Expression.VariableName.ToString();
    if (IsIdentifier(Name) == false || Name == TEXT("this"))
    {
        Diagnostics.Add(TEXT("Numeric comparison variable name is invalid"));
        return false;
    }
    // ForLoopのIndexは反射メンバーではなくfor文のローカル変数。
    if (LocalIndex.IsNone() == false && Expression.VariableName == LocalIndex &&
        OperandType == EBPSemanticTypeKind::Int32)
    {
        OutOperand = Name;
        return true;
    }
    const FBPSemanticVariable* Match = nullptr;
    for (const FBPSemanticVariable& Variable : Variables)
    {
        if (Variable.Name != Expression.VariableName)
        {
            continue;
        }
        if (Match != nullptr)
        {
            Diagnostics.Add(TEXT("Numeric comparison variable is ambiguous"));
            return false;
        }
        Match = &Variable;
    }
    const FString ExpectedCppType = OperandType == EBPSemanticTypeKind::Int32
        ? TEXT("int32")
        : OperandType == EBPSemanticTypeKind::Float
            ? TEXT("float")
            : OperandType == EBPSemanticTypeKind::Double
                ? TEXT("double") : FString();
    if (Match == nullptr || Match->Type.Kind != OperandType ||
        Match->Type.bIsReference || Match->Type.bIsConst ||
        Match->ReflectedCppType != ExpectedCppType)
    {
        Diagnostics.Add(TEXT("Numeric comparison variable does not match one reflected member"));
        return false;
    }
    OutOperand = Name;
    return true;
}

bool EmitForLoopCondition(const FBPSemanticExpression& Expression,
    const TArray<FBPSemanticVariable>& Variables, const FName LocalIndex,
    FString& OutCondition, TArray<FString>& Diagnostics)
{
    if (ReadBoolLiteral(Expression, OutCondition))
    {
        return true;
    }
    FBPNumericComparison Comparison;
    if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Expression.Type.Kind != EBPSemanticTypeKind::Bool ||
        Expression.Arguments.Num() != 2 ||
        FBPNumericComparisonResolver::Resolve(Expression.FunctionOwnerPath,
            Expression.FunctionName, Comparison, Diagnostics) == false)
    {
        Diagnostics.Add(TEXT("Unsupported ForLoop Branch comparison"));
        return false;
    }
    FString A;
    FString B;
    if (EmitNumericExpression(Expression.Arguments[0], Comparison.OperandType,
            Variables, A, Diagnostics, LocalIndex) == false ||
        EmitNumericExpression(Expression.Arguments[1], Comparison.OperandType,
            Variables, B, Diagnostics, LocalIndex) == false)
    {
        return false;
    }
    OutCondition = FString::Printf(TEXT("%s %s %s"), *A, *Comparison.OperatorToken, *B);
    return true;
}

bool EmitBranchCondition(const FBPSemanticExpression& Expression,
    const TArray<FBPSemanticVariable>& Variables,
    FString& OutCondition, TArray<FString>& Diagnostics)
{
    if (ReadBoolLiteral(Expression, OutCondition))
    {
        return true;
    }
    if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Expression.Type.Kind != EBPSemanticTypeKind::Bool ||
        Expression.Type.bIsReference || Expression.Type.bIsConst ||
        Expression.Arguments.Num() != 2)
    {
        Diagnostics.Add(TEXT("Unsupported Branch condition expression"));
        return false;
    }

    FBPNumericComparison Comparison;
    if (FBPNumericComparisonResolver::Resolve(Expression.FunctionOwnerPath,
        Expression.FunctionName, Comparison, Diagnostics) == false)
    {
        return false;
    }
    FString A;
    FString B;
    if (EmitNumericExpression(Expression.Arguments[0],
            Comparison.OperandType, Variables, A, Diagnostics) == false ||
        EmitNumericExpression(Expression.Arguments[1],
            Comparison.OperandType, Variables, B, Diagnostics) == false)
    {
        return false;
    }
    OutCondition = FString::Printf(TEXT("%s %s %s"),
        *A, *Comparison.OperatorToken, *B);
    return true;
}

bool EmitPrintStringStatement(const FBPSemanticExpression& Call,
    const FString& ParentClassPath, const FString& Indent, FString& OutStatement,
    TArray<FString>& Diagnostics)
{
    OutStatement.Reset();
    if (Call.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Call.Type.Kind != EBPSemanticTypeKind::Void ||
        Call.FunctionName != TEXT("PrintString") ||
        Call.FunctionOwnerPath != TEXT("/Script/Engine.KismetSystemLibrary") ||
        Call.Arguments.Num() != 7)
    {
        Diagnostics.Add(TEXT("Unsupported PrintString semantic signature"));
        return false;
    }
    const FBPSemanticExpression& Context = Call.Arguments[0];
    if (Context.Kind != EBPSemanticExpressionKind::VariableReference ||
        Context.VariableName != TEXT("this") ||
        Context.Type.Kind != EBPSemanticTypeKind::Object ||
        Context.Type.TypePath != ParentClassPath ||
        Context.Type.bIsReference || Context.Type.bIsConst)
    {
        Diagnostics.Add(TEXT("Unsupported PrintString world context"));
        return false;
    }

    FString Text;
    FString TextExpression;
    FString Screen;
    FString Log;
    FString Color;
    FString DurationText;
    FString Key;
    const FBPSemanticExpression& TextArgument = Call.Arguments[1];
    if (TextArgument.Kind == EBPSemanticExpressionKind::Literal)
    {
        if (ReadArgument(TextArgument, EBPSemanticTypeKind::String, Text, Diagnostics) == false)
        {
            return false;
        }
        FString EscapedText;
        if (EscapeString(Text, EscapedText, Diagnostics) == false)
        {
            return false;
        }
        TextExpression = FString::Printf(TEXT("TEXT(\"%s\")"), *EscapedText);
    }
    else if (TextArgument.Kind == EBPSemanticExpressionKind::FunctionCall &&
        TextArgument.Type.Kind == EBPSemanticTypeKind::String &&
        TextArgument.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
        TextArgument.FunctionName == TEXT("Conv_IntToString") &&
        TextArgument.Arguments.Num() == 1 &&
        TextArgument.Arguments[0].Kind == EBPSemanticExpressionKind::VariableReference &&
        TextArgument.Arguments[0].Type.Kind == EBPSemanticTypeKind::Int32 &&
        IsIdentifier(TextArgument.Arguments[0].VariableName.ToString()))
    {
        TextExpression = FString::Printf(TEXT("UKismetStringLibrary::Conv_IntToString(%s)"),
            *TextArgument.Arguments[0].VariableName.ToString());
    }
    else
    {
        Diagnostics.Add(TEXT("Unsupported PrintString text expression"));
        return false;
    }
    if (ReadArgument(Call.Arguments[2], EBPSemanticTypeKind::Bool, Screen, Diagnostics) == false ||
        ReadArgument(Call.Arguments[3], EBPSemanticTypeKind::Bool, Log, Diagnostics) == false ||
        ReadArgument(Call.Arguments[4], EBPSemanticTypeKind::Struct, Color, Diagnostics) == false ||
        ReadArgument(Call.Arguments[5], EBPSemanticTypeKind::Float, DurationText, Diagnostics) == false ||
        ReadArgument(Call.Arguments[6], EBPSemanticTypeKind::String, Key, Diagnostics) == false)
    {
        return false;
    }
    if ((Screen != TEXT("true") && Screen != TEXT("false")) ||
        (Log != TEXT("true") && Log != TEXT("false")) ||
        Call.Arguments[4].Type.TypePath != TEXT("/Script/CoreUObject.LinearColor"))
    {
        Diagnostics.Add(TEXT("Invalid PrintString boolean or color type"));
        return false;
    }
    float Duration = 0.0f;
    FLinearColor LinearColor;
    if (LexTryParseString(Duration, *DurationText) == false ||
        FMath::IsFinite(Duration) == false ||
        LinearColor.InitFromString(Color) == false ||
        FMath::IsFinite(LinearColor.R) == false || FMath::IsFinite(LinearColor.G) == false ||
        FMath::IsFinite(LinearColor.B) == false || FMath::IsFinite(LinearColor.A) == false)
    {
        Diagnostics.Add(TEXT("Invalid PrintString duration or color"));
        return false;
    }
    FString EscapedKey;
    if (EscapeString(Key, EscapedKey, Diagnostics) == false)
    {
        return false;
    }
    OutStatement = FString::Printf(
        TEXT("%sUKismetSystemLibrary::PrintString(this, %s, %s, %s, FLinearColor(%s, %s, %s, %s), %s, TEXT(\"%s\"));\n"),
        *Indent, *TextExpression, *Screen, *Log,
        *FloatLiteral(LinearColor.R), *FloatLiteral(LinearColor.G),
        *FloatLiteral(LinearColor.B), *FloatLiteral(LinearColor.A),
        *FloatLiteral(Duration), *EscapedKey);
    return true;
}

bool EmitActorLocationStatement(const FBPSemanticExpression& Call,
    const FString& ParentClassPath, FString& OutStatement,
    TArray<FString>& Diagnostics)
{
    OutStatement.Reset();
    if (Call.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Call.Type.Kind != EBPSemanticTypeKind::Bool ||
        Call.FunctionName != TEXT("K2_SetActorLocation") ||
        Call.FunctionOwnerPath != TEXT("/Script/Engine.Actor") ||
        Call.Arguments.Num() != 5)
    {
        Diagnostics.Add(TEXT("Unsupported SetActorLocation semantic signature"));
        return false;
    }
    const FBPSemanticExpression& Target = Call.Arguments[0];
    if (Target.Kind != EBPSemanticExpressionKind::VariableReference ||
        Target.VariableName != TEXT("this") ||
        Target.Type.Kind != EBPSemanticTypeKind::Object ||
        Target.Type.TypePath != ParentClassPath ||
        Target.Type.bIsReference || Target.Type.bIsConst)
    {
        Diagnostics.Add(TEXT("Unsupported SetActorLocation target"));
        return false;
    }

    const FBPSemanticExpression& Location = Call.Arguments[1];
    if (Location.Kind != EBPSemanticExpressionKind::FunctionCall ||
        Location.FunctionName != TEXT("K2_GetActorLocation") ||
        Location.FunctionOwnerPath != TEXT("/Script/Engine.Actor") ||
        Location.Type.Kind != EBPSemanticTypeKind::Struct ||
        Location.Type.TypePath != TEXT("/Script/CoreUObject.Vector") ||
        Location.Type.bIsReference || Location.Type.bIsConst ||
        Location.Arguments.Num() != 1)
    {
        Diagnostics.Add(TEXT("Unsupported SetActorLocation location expression"));
        return false;
    }
    const FBPSemanticExpression& LocationTarget = Location.Arguments[0];
    if (LocationTarget.Kind != EBPSemanticExpressionKind::VariableReference ||
        LocationTarget.VariableName != TEXT("this") ||
        LocationTarget.Type.Kind != EBPSemanticTypeKind::Object ||
        LocationTarget.Type.TypePath != ParentClassPath ||
        LocationTarget.Type.bIsReference || LocationTarget.Type.bIsConst)
    {
        Diagnostics.Add(TEXT("Unsupported GetActorLocation target"));
        return false;
    }

    FString Sweep;
    FString Teleport;
    if (ReadBoolLiteral(Call.Arguments[2], Sweep) == false ||
        ReadBoolLiteral(Call.Arguments[4], Teleport) == false)
    {
        Diagnostics.Add(TEXT("Invalid SetActorLocation bool argument"));
        return false;
    }
    const FBPSemanticExpression& HitResult = Call.Arguments[3];
    const FString HitName = HitResult.VariableName.ToString();
    if (HitResult.Kind != EBPSemanticExpressionKind::VariableReference ||
        IsIdentifier(HitName) == false ||
        HitResult.Type.Kind != EBPSemanticTypeKind::Struct ||
        HitResult.Type.TypePath != TEXT("/Script/Engine.HitResult") ||
        HitResult.Type.bIsConst)
    {
        Diagnostics.Add(TEXT("Invalid SetActorLocation output storage"));
        return false;
    }
    OutStatement = FString::Printf(
        TEXT("    FHitResult %s{};\n")
        TEXT("    K2_SetActorLocation(GetActorLocation(), %s, %s, %s);\n"),
        *HitName, *Sweep, *HitName, *Teleport);
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
    if (Statement.Kind == EBPSemanticStatementKind::ForLoop)
    {
        int32 FirstIndex = 0;
        int32 LastIndex = 0;
        if (Statement.LoopIndexVariable.IsNone() ||
            IsIdentifier(Statement.LoopIndexVariable.ToString()) == false ||
            Statement.LoopFirstIndex.Kind != EBPSemanticExpressionKind::Literal ||
            Statement.LoopFirstIndex.Type.Kind != EBPSemanticTypeKind::Int32 ||
            Statement.LoopLastIndex.Kind != EBPSemanticExpressionKind::Literal ||
            Statement.LoopLastIndex.Type.Kind != EBPSemanticTypeKind::Int32 ||
            LexTryParseString(FirstIndex, *Statement.LoopFirstIndex.LiteralValue) == false ||
            LexTryParseString(LastIndex, *Statement.LoopLastIndex.LiteralValue) == false ||
            Statement.LoopBodyStatements.IsEmpty())
        {
            OutDiagnostics.Add(TEXT("ForLoop requires literal int32 bounds and one supported body statement"));
            return false;
        }
        FString BodyCode;
        for (const FBPSemanticStatement& BodyStatement : Statement.LoopBodyStatements)
        {
            FString StatementCode;
            if (BodyStatement.Kind == EBPSemanticStatementKind::Expression)
            {
                if (EmitPrintStringStatement(BodyStatement.Expression, IR.ParentClassPath,
                    TEXT("        "), StatementCode, OutDiagnostics) == false)
                {
                    return false;
                }
            }
            else if (BodyStatement.Kind == EBPSemanticStatementKind::Branch &&
                BodyStatement.ThenStatements.Num() == 1 && BodyStatement.ElseStatements.Num() == 1)
            {
                FString Condition;
                FString ThenCode;
                FString ElseCode;
                if (EmitForLoopCondition(BodyStatement.Expression, IR.Variables,
                        Statement.LoopIndexVariable, Condition, OutDiagnostics) == false ||
                    EmitPrintStringStatement(BodyStatement.ThenStatements[0].Expression,
                        IR.ParentClassPath, TEXT("            "), ThenCode, OutDiagnostics) == false ||
                    EmitPrintStringStatement(BodyStatement.ElseStatements[0].Expression,
                        IR.ParentClassPath, TEXT("            "), ElseCode, OutDiagnostics) == false)
                {
                    return false;
                }
                StatementCode = FString::Printf(
                    TEXT("        if (%s)\n        {\n%s        }\n        else\n        {\n%s        }\n"),
                    *Condition, *ThenCode, *ElseCode);
            }
            else
            {
                OutDiagnostics.Add(TEXT("ForLoop body contains an unsupported semantic statement"));
                return false;
            }
            BodyCode += StatementCode;
        }
        FString CompletedCode;
        // Completed はループ終了後の直列実行列として保持する。
        // 各文を接続順に生成し、未対応の文種別は推測せずエラーにする。
        for (const FBPSemanticStatement& CompletedStatement : Statement.CompletedStatements)
        {
            FString StatementCode;
            if (CompletedStatement.Kind != EBPSemanticStatementKind::Expression ||
                EmitPrintStringStatement(CompletedStatement.Expression, IR.ParentClassPath,
                    TEXT("    "), StatementCode, OutDiagnostics) == false)
            {
                OutDiagnostics.Add(TEXT("ForLoop Completed contains an unsupported semantic statement"));
                return false;
            }
            CompletedCode += StatementCode;
        }

        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
            ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }
        const FString BaseName = ClassName.Mid(1);
        const FString ApiPrefix = ModuleApiMacro.IsEmpty()
            ? FString() : ModuleApiMacro + TEXT(" ");
        const FString ConstructorDeclaration = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("public:\n    %s();\n\n"), *ClassName);
        const FString UnrealTypeInclude = ConstructorBody.IsEmpty()
            ? FString()
            : TEXT("#include \"UObject/UnrealType.h\"\n");
        const FString ConstructorDefinition = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("\n%s::%s()\n{\n%s}\n"),
                *ClassName, *ClassName, *ConstructorBody);
        FBPGeneratedActorFiles Files;
        Files.HeaderFileName = BaseName + TEXT(".h");
        Files.SourceFileName = BaseName + TEXT(".cpp");
        Files.HeaderContent = FString::Printf(
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%sprotected:\n    virtual void BeginPlay() override;\n};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration);
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n#include \"Kismet/KismetMathLibrary.h\"\n#include \"Kismet/KismetStringLibrary.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n%s%s\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    for (int32 %s = %d; %s <= %d; ++%s)\n    {\n%s    }\n%s}\n"),
            *BaseName, *UnrealTypeInclude, *ConstructorDefinition, *ClassName,
            *Statement.LoopIndexVariable.ToString(), FirstIndex,
            *Statement.LoopIndexVariable.ToString(), LastIndex,
            *Statement.LoopIndexVariable.ToString(), *BodyCode, *CompletedCode);
        OutFiles = MoveTemp(Files);
        return true;
    }
    if (Statement.Kind == EBPSemanticStatementKind::Branch)
    {
        FString Condition;
        if (EmitBranchCondition(Statement.Expression, IR.Variables,
            Condition, OutDiagnostics) == false ||
            Statement.ThenStatements.Num() != 1 || Statement.ElseStatements.Num() != 1)
        {
            OutDiagnostics.Add(TEXT("Branch requires one supported bool condition and one statement in each output"));
            return false;
        }
        const FBPSemanticStatement& ThenStatement = Statement.ThenStatements[0];
        const FBPSemanticStatement& ElseStatement = Statement.ElseStatements[0];
        if (ThenStatement.Kind != EBPSemanticStatementKind::Expression ||
            ElseStatement.Kind != EBPSemanticStatementKind::Expression ||
            ThenStatement.ThenStatements.IsEmpty() == false ||
            ThenStatement.ElseStatements.IsEmpty() == false ||
            ElseStatement.ThenStatements.IsEmpty() == false ||
            ElseStatement.ElseStatements.IsEmpty() == false)
        {
            OutDiagnostics.Add(TEXT("Branch outputs contain an unsupported semantic statement"));
            return false;
        }
        FString ThenCode;
        FString ElseCode;
        if (EmitPrintStringStatement(ThenStatement.Expression, IR.ParentClassPath,
            TEXT("        "), ThenCode, OutDiagnostics) == false ||
            EmitPrintStringStatement(ElseStatement.Expression, IR.ParentClassPath,
                TEXT("        "), ElseCode, OutDiagnostics) == false)
        {
            return false;
        }

        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
            ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }
        const FString BaseName = ClassName.Mid(1);
        const FString ApiPrefix = ModuleApiMacro.IsEmpty()
            ? FString() : ModuleApiMacro + TEXT(" ");
        const FString ConstructorDeclaration = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("public:\n    %s();\n\n"), *ClassName);
        const FString UnrealTypeInclude = ConstructorBody.IsEmpty()
            ? FString()
            : TEXT("#include \"UObject/UnrealType.h\"\n");
        const FString ConstructorDefinition = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("\n%s::%s()\n{\n%s}\n"),
                *ClassName, *ClassName, *ConstructorBody);
        FBPGeneratedActorFiles Files;
        Files.HeaderFileName = BaseName + TEXT(".h");
        Files.SourceFileName = BaseName + TEXT(".cpp");
        Files.HeaderContent = FString::Printf(
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%sprotected:\n    virtual void BeginPlay() override;\n};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration);
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n#include \"Kismet/KismetMathLibrary.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n%s%s\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    if (%s)\n    {\n%s    }\n    else\n    {\n%s    }\n}\n"),
            *BaseName, *UnrealTypeInclude, *ConstructorDefinition,
            *ClassName, *Condition, *ThenCode, *ElseCode);
        OutFiles = MoveTemp(Files);
        return true;
    }
    if (Statement.Kind == EBPSemanticStatementKind::Expression &&
        Call.FunctionName == TEXT("K2_SetActorLocation"))
    {
        FString ActorLocationStatement;
        if (EmitActorLocationStatement(Call, IR.ParentClassPath,
            ActorLocationStatement, OutDiagnostics) == false)
        {
            return false;
        }
        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
            ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }
        const FString BaseName = ClassName.Mid(1);
        const FString ApiPrefix = ModuleApiMacro.IsEmpty()
            ? FString() : ModuleApiMacro + TEXT(" ");
        const FString ConstructorDeclaration = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("public:\n    %s();\n\n"), *ClassName);
        const FString UnrealTypeInclude = ConstructorBody.IsEmpty()
            ? FString()
            : TEXT("#include \"UObject/UnrealType.h\"\n");
        const FString ConstructorDefinition = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("\n%s::%s()\n{\n%s}\n"),
                *ClassName, *ClassName, *ConstructorBody);
        FBPGeneratedActorFiles Files;
        Files.HeaderFileName = BaseName + TEXT(".h");
        Files.SourceFileName = BaseName + TEXT(".cpp");
        Files.HeaderContent = FString::Printf(
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%sprotected:\n    virtual void BeginPlay() override;\n};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration);
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n#include \"Engine/HitResult.h\"\n%s%s\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n%s}\n"),
            *BaseName, *UnrealTypeInclude, *ConstructorDefinition,
            *ClassName, *ActorLocationStatement);
        OutFiles = MoveTemp(Files);
        return true;
    }
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
    FString ConstructorBody;
    if (EmitVariables(IR.Variables, ClassName, VariableDeclarations, ConstructorBody, OutDiagnostics,
        VariableFallbackMode) == false)
    {
        return false;
    }

    const FString BaseName = ClassName.Mid(1);
    const FString ApiPrefix = ModuleApiMacro.IsEmpty() ? FString() : ModuleApiMacro + TEXT(" ");
    const FString ConstructorDeclaration = ConstructorBody.IsEmpty()
        ? FString()
        : FString::Printf(TEXT("public:\n    %s();\n\n"), *ClassName);
    const FString UnrealTypeInclude = ConstructorBody.IsEmpty()
        ? FString()
        : TEXT("#include \"UObject/UnrealType.h\"\n");
    const FString ConstructorDefinition = ConstructorBody.IsEmpty()
        ? FString()
        : FString::Printf(TEXT("\n%s::%s()\n{\n%s}\n"),
            *ClassName, *ClassName, *ConstructorBody);
    FBPGeneratedActorFiles Files;
    Files.HeaderFileName = BaseName + TEXT(".h");
    Files.SourceFileName = BaseName + TEXT(".cpp");
    Files.HeaderContent = FString::Printf(
        TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%sprotected:\n    virtual void BeginPlay() override;\n};\n"),
        *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName, *ParentClass.CppClassName,
        *VariableDeclarations, *ConstructorDeclaration);
    Files.SourceContent = FString::Printf(
        TEXT("#include \"%s.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n%s%s\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n    UKismetSystemLibrary::PrintString(this, TEXT(\"%s\"), %s, %s, FLinearColor(%s, %s, %s, %s), %s, TEXT(\"%s\"));\n}\n"),
        *BaseName, *UnrealTypeInclude, *ConstructorDefinition,
        *ClassName, *EscapedText, *Screen, *Log,
        *FloatLiteral(LinearColor.R), *FloatLiteral(LinearColor.G),
        *FloatLiteral(LinearColor.B), *FloatLiteral(LinearColor.A),
        *FloatLiteral(Duration), *EscapedKey);
    OutFiles = MoveTemp(Files);
    return true;
}
