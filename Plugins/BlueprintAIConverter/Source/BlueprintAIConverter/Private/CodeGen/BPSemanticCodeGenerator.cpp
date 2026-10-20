#include "BPSemanticCodeGenerator.h"
#include "BPGeneratedFunctionShape.h"
#include "BPNumericArithmeticResolver.h"
#include "BPNumericComparisonResolver.h"
#include "BPParentClassResolver.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

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

bool EmitFunctionType(const FBPSemanticType& Type, FString& OutType,
    TArray<FString>& Diagnostics, int32 Depth = 0)
{
    if (Depth > 16)
    {
        Diagnostics.Add(TEXT("Function type nesting exceeds the supported depth"));
        return false;
    }
    FString ValueType;
    switch (Type.Kind)
    {
    case EBPSemanticTypeKind::Void: ValueType = TEXT("void"); break;
    case EBPSemanticTypeKind::Bool: ValueType = TEXT("bool"); break;
    case EBPSemanticTypeKind::Int32: ValueType = TEXT("int32"); break;
    case EBPSemanticTypeKind::Int64: ValueType = TEXT("int64"); break;
    case EBPSemanticTypeKind::Float: ValueType = TEXT("float"); break;
    case EBPSemanticTypeKind::Double: ValueType = TEXT("double"); break;
    case EBPSemanticTypeKind::String: ValueType = TEXT("FString"); break;
    case EBPSemanticTypeKind::Name: ValueType = TEXT("FName"); break;
    case EBPSemanticTypeKind::Text: ValueType = TEXT("FText"); break;
    case EBPSemanticTypeKind::Byte: ValueType = TEXT("uint8"); break;
    default:
        Diagnostics.Add(FString::Printf(
            TEXT("Function signature contains an unsupported type path: %s"), *Type.TypePath));
        return false;
    }

    if (Type.ContainerKind != TEXT("none"))
    {
        if (Type.ContainerKind != TEXT("array") || Type.ElementType.IsValid() == false)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Unsupported function parameter container: %s"), *Type.ContainerKind));
            return false;
        }
        FString ElementType;
        if (EmitFunctionType(*Type.ElementType, ElementType, Diagnostics, Depth + 1) == false ||
            ElementType == TEXT("void"))
        {
            return false;
        }
        ValueType = FString::Printf(TEXT("TArray<%s>"), *ElementType);
    }

    if (Type.bIsReference)
    {
        OutType = Type.bIsConst
            ? FString::Printf(TEXT("const %s&"), *ValueType)
            : FString::Printf(TEXT("%s&"), *ValueType);
    }
    else
    {
        OutType = MoveTemp(ValueType);
    }
    return true;
}

bool BuildFunctionShape(const FBPSemanticFunction& Function,
    const FBPResolvedParentClass& ParentClass, const FString& ClassName,
    FBPGeneratedFunctionShape& OutShape, TArray<FString>& Diagnostics)
{
    OutShape = FBPGeneratedFunctionShape();
    const FString FunctionName = Function.Name.ToString();
    if (IsIdentifier(FunctionName) == false)
    {
        Diagnostics.Add(TEXT("Semantic function name must be a valid C++ identifier"));
        return false;
    }
    if (Function.bIsStatic)
    {
        Diagnostics.Add(TEXT("Static semantic functions are not yet supported by instance-body emitters"));
        return false;
    }

    FString ReturnType;
    if (EmitFunctionType(Function.ReturnType, ReturnType, Diagnostics) == false)
    {
        return false;
    }
    // 現在対応する文IRには戻り値式がないため、非void関数を成功扱いしない。
    if (ReturnType != TEXT("void"))
    {
        Diagnostics.Add(TEXT("Non-void semantic functions require a verified Return statement"));
        return false;
    }

    TArray<const FBPSemanticParameter*> OrderedParameters;
    for (const FBPSemanticParameter& Parameter : Function.Parameters)
    {
        if (Parameter.bIsReturn == false)
        {
            OrderedParameters.Add(&Parameter);
        }
    }
    OrderedParameters.Sort([](const FBPSemanticParameter& A, const FBPSemanticParameter& B)
    {
        return A.DeclarationIndex < B.DeclarationIndex;
    });
    FString Parameters;
    for (const FBPSemanticParameter* Parameter : OrderedParameters)
    {
        if (Parameter == nullptr || IsIdentifier(Parameter->Name.ToString()) == false)
        {
            Diagnostics.Add(TEXT("Semantic function parameter name must be a valid C++ identifier"));
            return false;
        }
        FString ParameterType;
        if (EmitFunctionType(Parameter->Type, ParameterType, Diagnostics) == false ||
            ParameterType == TEXT("void"))
        {
            return false;
        }
        if (Parameters.IsEmpty() == false)
        {
            Parameters += TEXT(", ");
        }
        Parameters += ParameterType + TEXT(" ") + Parameter->Name.ToString();
    }

    const FString ConstSuffix = Function.bIsConst ? TEXT(" const") : TEXT("");
    bool bOverride = Function.bIsOverride || Function.bIsInterfaceImplementation;
    bool bCallSuper = Function.bIsOverride;
    bool bEventImplementation = Function.bIsEventGraph;
    if (FunctionName == TEXT("BeginPlay"))
    {
        if (ParentClass.bSupportsBeginPlay == false || Parameters.IsEmpty() == false ||
            Function.bIsConst)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Parent class does not provide the required BeginPlay lifecycle signature: %s"),
                *ParentClass.ClassPath));
            return false;
        }
        bOverride = true;
        bCallSuper = true;
        // ReceiveBeginPlayではなく、ネイティブのBeginPlay lifecycleを生成する。
        bEventImplementation = false;
    }
    else if (bOverride &&
        (Function.SignatureSource != TEXT("generated_class_reflection") ||
            (Function.bIsOverride && Function.OverriddenFunctionPath.IsEmpty()) ||
            (Function.bIsInterfaceImplementation && Function.InterfaceFunctionPath.IsEmpty())))
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Override signature for %s was not verified by generated-class reflection"),
            *FunctionName));
        return false;
    }
    if (bEventImplementation && bOverride && Function.bIsNative == false)
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Blueprint event override %s has no verified native _Implementation target"),
            *FunctionName));
        return false;
    }

    const FString OverrideSuffix = bOverride ? TEXT(" override") : TEXT("");
    const TCHAR* Visibility = FunctionName == TEXT("BeginPlay")
        ? TEXT("protected") : TEXT("public");
    FString DefinitionFunctionName = FunctionName;
    if (bEventImplementation)
    {
        DefinitionFunctionName += TEXT("_Implementation");
        if (bOverride)
        {
            OutShape.Declaration = FString::Printf(
                TEXT("%s:\n    virtual %s %s(%s)%s override;\n"),
                Visibility, *ReturnType, *DefinitionFunctionName, *Parameters, *ConstSuffix);
        }
        else
        {
            OutShape.Declaration = FString::Printf(
                TEXT("%s:\n    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)\n")
                TEXT("    %s %s(%s)%s;\n")
                TEXT("    virtual %s %s(%s)%s;\n"),
                Visibility, *ReturnType, *FunctionName, *Parameters, *ConstSuffix,
                *ReturnType, *DefinitionFunctionName, *Parameters, *ConstSuffix);
        }
    }
    else
    {
        const FString ReflectionDeclaration = bOverride
            ? FString()
            : TEXT("    UFUNCTION(BlueprintCallable)\n");
        OutShape.Declaration = FString::Printf(
            TEXT("%s:\n%s    %s%s %s(%s)%s%s;\n"),
            Visibility, *ReflectionDeclaration, bOverride ? TEXT("virtual ") : TEXT(""),
            *ReturnType, *FunctionName, *Parameters, *ConstSuffix, *OverrideSuffix);
    }
    OutShape.Definition = FString::Printf(TEXT("%s %s::%s(%s)%s"),
        *ReturnType, *ClassName, *DefinitionFunctionName, *Parameters, *ConstSuffix);
    if (bCallSuper)
    {
        FString Arguments;
        for (const FBPSemanticParameter* Parameter : OrderedParameters)
        {
            if (Arguments.IsEmpty() == false)
            {
                Arguments += TEXT(", ");
            }
            Arguments += Parameter->Name.ToString();
        }
        OutShape.Prologue = FString::Printf(TEXT("    Super::%s(%s);\n"),
            *DefinitionFunctionName, *Arguments);
    }
    return true;
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
    else if (TextArgument.Kind == EBPSemanticExpressionKind::VariableReference &&
        TextArgument.Type.Kind == EBPSemanticTypeKind::String)
    {
        // メンバー変数を明示的に参照し、ローカル変数との名前衝突を避ける。
        if (IsIdentifier(TextArgument.VariableName.ToString()) == false ||
            TextArgument.VariableName == TEXT("this"))
        {
            Diagnostics.Add(TEXT("Invalid PrintString member variable name"));
            return false;
        }
        TextExpression = FString::Printf(TEXT("this->%s"),
            *TextArgument.VariableName.ToString());
    }
    else if (TextArgument.Kind == EBPSemanticExpressionKind::FunctionCall &&
        TextArgument.Type.Kind == EBPSemanticTypeKind::String &&
        TextArgument.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
        TextArgument.FunctionName == TEXT("Concat_StrStr") &&
        TextArgument.Arguments.Num() == 2)
    {
        // 結合式を再帰的に出力する。深さ制限と関数の許可リストで
        // 不正なIRから任意のC++式が生成されることを防ぐ。
        TFunction<bool(const FBPSemanticExpression&, int32, FString&)> EmitString;
        EmitString = [&](const FBPSemanticExpression& Expression,
            int32 Depth, FString& Result) -> bool
        {
            if (Depth > 16 || Expression.Type.Kind != EBPSemanticTypeKind::String)
            {
                return false;
            }
            if (Expression.Kind == EBPSemanticExpressionKind::Literal)
            {
                FString Value;
                FString Escaped;
                if (ReadArgument(Expression, EBPSemanticTypeKind::String,
                        Value, Diagnostics) == false ||
                    EscapeString(Value, Escaped, Diagnostics) == false)
                {
                    return false;
                }
                Result = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
                return true;
            }
            if (Expression.Kind == EBPSemanticExpressionKind::VariableReference)
            {
                if (IsIdentifier(Expression.VariableName.ToString()) == false ||
                    Expression.VariableName == TEXT("this"))
                {
                    return false;
                }
                Result = FString::Printf(TEXT("this->%s"),
                    *Expression.VariableName.ToString());
                return true;
            }
            if (Expression.Kind == EBPSemanticExpressionKind::FunctionCall &&
                Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
                Expression.FunctionName == TEXT("Conv_IntToString") &&
                Expression.Arguments.Num() == 1)
            {
                // Concatの内側でもIndexと整数演算を同じ規則で再帰生成する。
                TFunction<bool(const FBPSemanticExpression&, int32, FString&)> EmitInteger;
                EmitInteger = [&](const FBPSemanticExpression& Integer,
                    int32 IntegerDepth, FString& Out) -> bool
                {
                    if (IntegerDepth > 16 ||
                        Integer.Type.Kind != EBPSemanticTypeKind::Int32)
                    {
                        return false;
                    }
                    if (Integer.Kind == EBPSemanticExpressionKind::Literal)
                    {
                        int32 Value = 0;
                        if (LexTryParseString(Value, *Integer.LiteralValue) == false)
                        {
                            return false;
                        }
                        Out = LexToString(Value);
                        return true;
                    }
                    if (Integer.Kind == EBPSemanticExpressionKind::VariableReference)
                    {
                        if (IsIdentifier(Integer.VariableName.ToString()) == false)
                        {
                            return false;
                        }
                        // Getterの出力ピンIDは有効、ForLoop Indexは未設定。
                        // 名前の接頭辞では判定せず、IR上の参照元で区別する。
                        const FString Name = Integer.VariableName.ToString();
                        Out = Integer.SourcePinId.IsValid()
                            ? FString::Printf(TEXT("this->%s"), *Name) : Name;
                        return true;
                    }
                    if (Integer.Kind != EBPSemanticExpressionKind::FunctionCall ||
                        Integer.FunctionOwnerPath != TEXT("/Script/Engine.KismetMathLibrary") ||
                        (Integer.FunctionName != TEXT("Add_IntInt") &&
                            Integer.FunctionName != TEXT("Subtract_IntInt") &&
                            Integer.FunctionName != TEXT("Multiply_IntInt")) ||
                        Integer.Arguments.Num() != 2)
                    {
                        return false;
                    }
                    FString Left;
                    FString Right;
                    if (EmitInteger(Integer.Arguments[0], IntegerDepth + 1, Left) == false ||
                        EmitInteger(Integer.Arguments[1], IntegerDepth + 1, Right) == false)
                    {
                        return false;
                    }
                    Out = FString::Printf(TEXT("UKismetMathLibrary::%s(%s, %s)"),
                        *Integer.FunctionName.ToString(), *Left, *Right);
                    return true;
                };
                FString IndexExpression;
                if (EmitInteger(Expression.Arguments[0], Depth + 1,
                        IndexExpression) == false)
                {
                    return false;
                }
                Result = FString::Printf(
                    TEXT("UKismetStringLibrary::Conv_IntToString(%s)"),
                    *IndexExpression);
                return true;
            }
            if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
                Expression.FunctionOwnerPath != TEXT("/Script/Engine.KismetStringLibrary") ||
                Expression.FunctionName != TEXT("Concat_StrStr") ||
                Expression.Arguments.Num() != 2)
            {
                return false;
            }
            FString Left;
            FString Right;
            if (EmitString(Expression.Arguments[0], Depth + 1, Left) == false ||
                EmitString(Expression.Arguments[1], Depth + 1, Right) == false)
            {
                return false;
            }
            Result = FString::Printf(TEXT("UKismetStringLibrary::Concat_StrStr(%s, %s)"),
                *Left, *Right);
            return true;
        };
        if (EmitString(TextArgument, 0, TextExpression) == false)
        {
            Diagnostics.Add(TEXT("Unsupported nested string concatenation"));
            return false;
        }
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
        // Getterは出力ピンIDを持つ。LoopIndexのローカル参照と区別する。
        const FBPSemanticExpression& Input = TextArgument.Arguments[0];
        const FString Name = Input.VariableName.ToString();
        const FString Reference = Input.SourcePinId.IsValid()
            ? FString::Printf(TEXT("this->%s"), *Name) : Name;
        TextExpression = FString::Printf(TEXT("UKismetStringLibrary::Conv_IntToString(%s)"),
            *Reference);
    }
    else if (TextArgument.Kind == EBPSemanticExpressionKind::FunctionCall &&
        TextArgument.Type.Kind == EBPSemanticTypeKind::String &&
        TextArgument.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
        TextArgument.FunctionName == TEXT("Conv_IntToString") &&
        TextArgument.Arguments.Num() == 1)
    {
        // 二項演算を再帰的に展開する。深さ制限で循環的・異常なIRを拒否する。
        // 変数参照は識別子検証し、関数名は許可リストで固定する。
        TFunction<bool(const FBPSemanticExpression&, int32, FString&)> EmitInteger;
        EmitInteger = [&](const FBPSemanticExpression& Expression,
            int32 Depth, FString& Result) -> bool
        {
            if (Depth > 16 || Expression.Type.Kind != EBPSemanticTypeKind::Int32)
            {
                return false;
            }
            if (Expression.Kind == EBPSemanticExpressionKind::Literal)
            {
                int32 Number = 0;
                if (LexTryParseString(Number, *Expression.LiteralValue) == false)
                {
                    return false;
                }
                Result = LexToString(Number);
                return true;
            }
            if (Expression.Kind == EBPSemanticExpressionKind::VariableReference)
            {
                if (IsIdentifier(Expression.VariableName.ToString()) == false)
                {
                    return false;
                }
                Result = Expression.VariableName.ToString();
                return true;
            }
            if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall ||
                Expression.FunctionOwnerPath != TEXT("/Script/Engine.KismetMathLibrary") ||
                (Expression.FunctionName != TEXT("Add_IntInt") &&
                    Expression.FunctionName != TEXT("Subtract_IntInt") &&
                    Expression.FunctionName != TEXT("Multiply_IntInt")) ||
                Expression.Arguments.Num() != 2)
            {
                return false;
            }
            FString Left;
            FString Right;
            if (EmitInteger(Expression.Arguments[0], Depth + 1, Left) == false ||
                EmitInteger(Expression.Arguments[1], Depth + 1, Right) == false)
            {
                return false;
            }
            Result = FString::Printf(TEXT("UKismetMathLibrary::%s(%s, %s)"),
                *Expression.FunctionName.ToString(), *Left, *Right);
            return true;
        };
        FString IntegerExpression;
        if (EmitInteger(TextArgument.Arguments[0], 0, IntegerExpression) == false)
        {
            Diagnostics.Add(TEXT("Unsupported nested Index arithmetic expression"));
            return false;
        }
        TextExpression = FString::Printf(
            TEXT("UKismetStringLibrary::Conv_IntToString(%s)"), *IntegerExpression);
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
    const FString& ParentClassPath, const FString& Indent, FString& OutStatement,
    TArray<FString>& Diagnostics);

// 検証済みSemantic式文の共通ディスパッチ。
// ループやBranchは特定の関数名を直接判定せず、この関数を通して対応範囲を共有する。
bool EmitExpressionStatement(const FBPSemanticExpression& Expression,
    const FString& ParentClassPath, const FString& Indent, FString& OutStatement,
    TArray<FString>& Diagnostics)
{
    if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall)
    {
        Diagnostics.Add(TEXT("Expression statement requires a verified function call"));
        return false;
    }
    if (Expression.FunctionName == TEXT("PrintString") &&
        Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetSystemLibrary"))
    {
        return EmitPrintStringStatement(Expression, ParentClassPath,
            Indent, OutStatement, Diagnostics);
    }
    if (Expression.FunctionName == TEXT("K2_SetActorLocation") &&
        Expression.FunctionOwnerPath == TEXT("/Script/Engine.Actor"))
    {
        return EmitActorLocationStatement(Expression, ParentClassPath,
            Indent, OutStatement, Diagnostics);
    }
    // Actorのネイティブ関数呼び出しは、Loweringで検証済みのリテラルのみを
    // C++引数へ変換する。任意の式や参照引数を文字列として出力しない。
    const UClass* OwnerClass = FindObject<UClass>(
        nullptr, *Expression.FunctionOwnerPath);
    const UClass* ParentClass = FindObject<UClass>(
        nullptr, *ParentClassPath);
    const UFunction* ReflectedFunction = OwnerClass != nullptr
        ? OwnerClass->FindFunctionByName(Expression.FunctionName) : nullptr;
    if (OwnerClass != nullptr && ParentClass != nullptr &&
        (ReflectedFunction != nullptr && ReflectedFunction->HasAnyFunctionFlags(FUNC_Static) ||
         ParentClass->IsChildOf(OwnerClass)) &&
        ReflectedFunction != nullptr &&
        ReflectedFunction->HasAnyFunctionFlags(FUNC_Native) &&

        ReflectedFunction->GetReturnProperty() == nullptr &&
        Expression.Type.Kind == EBPSemanticTypeKind::Void &&
        (ReflectedFunction->HasAnyFunctionFlags(FUNC_Static)
            ? Expression.Arguments.IsEmpty() == false ||
              OwnerClass->GetMetaData(TEXT("IncludePath")).IsEmpty() == false
            : Expression.Arguments.Num() >= 1 &&
              Expression.Arguments[0].Kind == EBPSemanticExpressionKind::VariableReference &&
              Expression.Arguments[0].VariableName == TEXT("this")) &&
        IsIdentifier(Expression.FunctionName.ToString()))
    {
        FString Arguments;
        const bool bStatic = ReflectedFunction->HasAnyFunctionFlags(FUNC_Static);
        const int32 FirstArgument = bStatic ? 0 : 1;
        for (int32 Index = FirstArgument; Index < Expression.Arguments.Num(); ++Index)
        {
            const FBPSemanticExpression& Argument = Expression.Arguments[Index];
            if (Argument.Kind != EBPSemanticExpressionKind::Literal ||
                Argument.Type.bIsReference || Argument.Type.bIsConst)
            {
                Diagnostics.Add(TEXT("Actor call contains an unsupported argument expression"));
                return false;
            }
            FString Value;
            if (Argument.Type.Kind == EBPSemanticTypeKind::Bool)
            {
                if (ReadBoolLiteral(Argument, Value) == false)
                {
                    return false;
                }
            }
            else if (Argument.Type.Kind == EBPSemanticTypeKind::Int32)
            {
                int32 Parsed = 0;
                if (LexTryParseString(Parsed, *Argument.LiteralValue) == false)
                {
                    return false;
                }
                Value = LexToString(Parsed);
            }
            else if (Argument.Type.Kind == EBPSemanticTypeKind::Float ||
                Argument.Type.Kind == EBPSemanticTypeKind::Double)
            {
                double Parsed = 0.0;
                if (LexTryParseString(Parsed, *Argument.LiteralValue) == false ||
                    FMath::IsFinite(Parsed) == false)
                {
                    return false;
                }
                Value = Argument.LiteralValue;
                if (Argument.Type.Kind == EBPSemanticTypeKind::Float)
                {
                    Value += TEXT("f");
                }
            }
            else
            {
                Diagnostics.Add(TEXT("Actor call argument type is unsupported"));
                return false;
            }
            if (Index > FirstArgument)
            {
                Arguments += TEXT(", ");
            }
            Arguments += Value;
        }
        const FString Target = bStatic
            ? OwnerClass->GetPrefixCPP() + OwnerClass->GetName() + TEXT("::")
            : TEXT("this->");
        OutStatement = FString::Printf(TEXT("%s%s%s(%s);\n"),
            *Indent, *Target, *Expression.FunctionName.ToString(), *Arguments);
        return true;
    }
    Diagnostics.Add(FString::Printf(TEXT("Unsupported semantic expression statement: %s::%s"),
        *Expression.FunctionOwnerPath, *Expression.FunctionName.ToString()));
    return false;
}

// 式文が必要とするヘッダーを、通常文・Branch・Loopの配置から独立して収集する。
// 新しい式Emitterを追加するときは、共通ディスパッチと依存ヘッダーだけを登録する。
void AppendExpressionIncludes(const FBPSemanticExpression& Expression, FString& OutIncludes)
{
    // 静的関数のC++所有クラスはIncludePathを反射情報から取得する。
    const UClass* Owner = FindObject<UClass>(nullptr, *Expression.FunctionOwnerPath);
    const UFunction* Function = Owner != nullptr
        ? Owner->FindFunctionByName(Expression.FunctionName) : nullptr;
    if (Function != nullptr && Function->HasAnyFunctionFlags(FUNC_Static))
    {
        const FString IncludePath = Owner->GetMetaData(TEXT("IncludePath"));
        if (IncludePath.IsEmpty() == false)
        {
            const FString IncludeLine = FString::Printf(TEXT("#include \"%s\"\n"), *IncludePath);
            if (OutIncludes.Contains(IncludeLine) == false)
            {
                OutIncludes += IncludeLine;
            }
        }
    }
    if (Expression.FunctionOwnerPath == TEXT("/Script/Engine.Actor") &&
        Expression.FunctionName == TEXT("K2_SetActorLocation") &&
        OutIncludes.Contains(TEXT("#include \"Engine/HitResult.h\"")) == false)
    {
        OutIncludes += TEXT("#include \"Engine/HitResult.h\"\n");
    }
    if (Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetMathLibrary") &&
        OutIncludes.Contains(TEXT("#include \"Kismet/KismetMathLibrary.h\"")) == false)
    {
        OutIncludes += TEXT("#include \"Kismet/KismetMathLibrary.h\"\n");
    }
    if (Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
        OutIncludes.Contains(TEXT("#include \"Kismet/KismetStringLibrary.h\"")) == false)
    {
        OutIncludes += TEXT("#include \"Kismet/KismetStringLibrary.h\"\n");
    }
    if (Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetSystemLibrary") &&
        OutIncludes.Contains(TEXT("#include \"Kismet/KismetSystemLibrary.h\"")) == false)
    {
        OutIncludes += TEXT("#include \"Kismet/KismetSystemLibrary.h\"\n");
    }
    for (const FBPSemanticExpression& Argument : Expression.Arguments)
    {
        AppendExpressionIncludes(Argument, OutIncludes);
    }
}

// ForLoopのBranchを再帰的に生成する。対応する葉は共通の式文Emitterへ渡し、
// 深さ制限で異常なSemantic IRによる過度な再帰を防ぐ。
bool EmitForLoopBodyStatement(const FBPSemanticStatement& Statement,
    const FString& ParentClassPath, const TArray<FBPSemanticVariable>& Variables,
    const FName LocalIndex, const FString& Indent, int32 Depth,
    FString& OutCode, TArray<FString>& Diagnostics)
{
    if (Depth > 16)
    {
        Diagnostics.Add(TEXT("ForLoop Branch nesting exceeds the supported depth"));
        return false;
    }
    if (Statement.Kind == EBPSemanticStatementKind::Expression)
    {
        return EmitExpressionStatement(Statement.Expression, ParentClassPath,
            Indent, OutCode, Diagnostics);
    }
    if (Statement.Kind == EBPSemanticStatementKind::ForLoop)
    {
        // 内側ループは独立したIndex名を必須とし、外側Indexのシャドーイングを防ぐ。
        // 現段階ではループ境界をint32リテラルに限定する。
        int32 First = 0;
        int32 Last = 0;
        if (Statement.LoopIndexVariable.IsNone() ||
            IsIdentifier(Statement.LoopIndexVariable.ToString()) == false ||
            Statement.LoopIndexVariable == LocalIndex ||
            Statement.LoopFirstIndex.Kind != EBPSemanticExpressionKind::Literal ||
            Statement.LoopFirstIndex.Type.Kind != EBPSemanticTypeKind::Int32 ||
            Statement.LoopLastIndex.Kind != EBPSemanticExpressionKind::Literal ||
            Statement.LoopLastIndex.Type.Kind != EBPSemanticTypeKind::Int32 ||
            LexTryParseString(First, *Statement.LoopFirstIndex.LiteralValue) == false ||
            LexTryParseString(Last, *Statement.LoopLastIndex.LiteralValue) == false ||
            Statement.LoopBodyStatements.IsEmpty())
        {
            Diagnostics.Add(TEXT("Nested ForLoop requires distinct index names and literal int32 bounds"));
            return false;
        }
        const FString InnerIndent = Indent + TEXT("    ");
        FString InnerCode;
        for (const FBPSemanticStatement& Body : Statement.LoopBodyStatements)
        {
            FString Part;
            if (EmitForLoopBodyStatement(Body, ParentClassPath, Variables,
                    Statement.LoopIndexVariable, InnerIndent, Depth + 1,
                    Part, Diagnostics) == false)
            {
                return false;
            }
            InnerCode += Part;
        }
        OutCode = FString::Printf(
            TEXT("%sfor (int32 %s = %d; %s <= %d; ++%s)\n%s{\n%s%s}\n"),
            *Indent, *Statement.LoopIndexVariable.ToString(), First,
            *Statement.LoopIndexVariable.ToString(), Last,
            *Statement.LoopIndexVariable.ToString(), *Indent, *InnerCode, *Indent);
        // Completedは内側ループ終了後、外側ループの同じ反復内で実行する。
        for (const FBPSemanticStatement& Completed : Statement.CompletedStatements)
        {
            FString Part;
            if (EmitForLoopBodyStatement(Completed, ParentClassPath, Variables,
                    LocalIndex, Indent, Depth + 1, Part, Diagnostics) == false)
            {
                Diagnostics.Add(TEXT("Nested ForLoop Completed contains an unsupported semantic statement"));
                return false;
            }
            OutCode += Part;
        }
        return true;
    }
    if (Statement.Kind != EBPSemanticStatementKind::Branch)
    {
        Diagnostics.Add(TEXT("Unsupported ForLoop body statement"));
        return false;
    }
    FString Condition;
    FString ThenCode;
    FString ElseCode;
    // 通常Branchでは既存の条件式検証を維持し、ループ内ではIndex参照を許可する。
    const bool bConditionValid = LocalIndex.IsNone()
        ? EmitBranchCondition(Statement.Expression, Variables, Condition, Diagnostics)
        : EmitForLoopCondition(Statement.Expression, Variables, LocalIndex, Condition, Diagnostics);
    if (bConditionValid == false)
    {
        return false;
    }
    // 各枝の文を順に生成する。内側のif終了後の文は同じ外側スコープに配置する。
    for (const FBPSemanticStatement& Child : Statement.ThenStatements)
    {
        FString Part;
        if (EmitForLoopBodyStatement(Child, ParentClassPath, Variables,
                LocalIndex, Indent + TEXT("    "), Depth + 1, Part, Diagnostics) == false)
        {
            return false;
        }
        ThenCode += Part;
    }
    for (const FBPSemanticStatement& Child : Statement.ElseStatements)
    {
        FString Part;
        if (EmitForLoopBodyStatement(Child, ParentClassPath, Variables,
                LocalIndex, Indent + TEXT("    "), Depth + 1, Part, Diagnostics) == false)
        {
            return false;
        }
        ElseCode += Part;
    }
    OutCode = FString::Printf(
        TEXT("%sif (%s)\n%s{\n%s%s}\n"),
        *Indent, *Condition, *Indent, *ThenCode, *Indent);
    if (Statement.ElseStatements.IsEmpty() == false)
    {
        OutCode += FString::Printf(
            TEXT("%selse\n%s{\n%s%s}\n"),
            *Indent, *Indent, *ElseCode, *Indent);
    }
    return true;
}

bool EmitActorLocationStatement(const FBPSemanticExpression& Call,
    const FString& ParentClassPath, const FString& Indent, FString& OutStatement,
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
        TEXT("%sFHitResult %s{};\n")
        TEXT("%sK2_SetActorLocation(GetActorLocation(), %s, %s, %s);\n"),
        *Indent, *HitName, *Indent, *Sweep, *HitName, *Teleport);
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
    if (ClassName.Len() <= ParentClass.GeneratedClassPrefix.Len() ||
        ClassName.StartsWith(ParentClass.GeneratedClassPrefix) == false ||
        IsIdentifier(ClassName) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Generated class name must be a valid %s-prefixed C++ identifier for parent %s"),
            *ParentClass.GeneratedClassPrefix, *ParentClass.CppClassName));
        return false;
    }
    const FString BaseName = ClassName.Mid(ParentClass.GeneratedClassPrefix.Len());
    if (IR.Functions.Num() != 1)
    {
        OutDiagnostics.Add(TEXT("Semantic MVP requires exactly one function"));
        return false;
    }
    const FBPSemanticFunction& Function = IR.Functions[0];
    if (Function.Blocks.Num() != 1 || Function.Blocks[0].Statements.IsEmpty())
    {
        OutDiagnostics.Add(TEXT("Semantic MVP requires one non-empty function block"));
        return false;
    }
    FBPGeneratedFunctionShape FunctionShape;
    if (BuildFunctionShape(Function, ParentClass, ClassName,
        FunctionShape, OutDiagnostics) == false)
    {
        return false;
    }
    const TArray<FBPSemanticStatement>& Statements = Function.Blocks[0].Statements;
    FString LeadingCode;
    for (int32 Index = 0; Index + 1 < Statements.Num(); ++Index)
    {
        const FBPSemanticStatement& LeadingStatement = Statements[Index];
        FString Part;
        // 合流後の処理はBranchと同じBasicBlockの後続文として保持する。
        // Branchを先行文として生成し、後続の式文がifの外へ出るようにする。
        if (LeadingStatement.Kind == EBPSemanticStatementKind::Branch)
        {
            if (EmitForLoopBodyStatement(LeadingStatement, IR.ParentClassPath,
                    IR.Variables, NAME_None, TEXT("    "), 0, Part, OutDiagnostics) == false)
            {
                return false;
            }
        }
        else if (LeadingStatement.Kind != EBPSemanticStatementKind::Expression ||
            EmitExpressionStatement(LeadingStatement.Expression, IR.ParentClassPath,
                TEXT("    "), Part, OutDiagnostics) == false)
        {
            OutDiagnostics.Add(TEXT("Function entry sequence contains an unsupported semantic statement"));
            return false;
        }
        LeadingCode += Part;
    }
    // 既存の単一文出力は完全一致を維持する。先行文がある場合だけ、
    // 共通式Emitterが必要とし得るヘッダーを追加する。
    const FString LeadingIncludes = LeadingCode.IsEmpty()
        ? FString()
        : TEXT("#include \"Engine/HitResult.h\"\n")
          TEXT("#include \"Kismet/KismetMathLibrary.h\"\n")
          TEXT("#include \"Kismet/KismetStringLibrary.h\"\n")
          TEXT("#include \"Kismet/KismetSystemLibrary.h\"\n");
    const FBPSemanticStatement& Statement = Statements.Last();
    const FBPSemanticExpression& Call = Statement.Expression;
    if (Statement.Kind == EBPSemanticStatementKind::ForEachLoop)
    {
        // 配列の実体と要素型が確定したSemantic IRに限定する。
        // ループローカル変数とBlueprintメンバーの衝突はC++の名前隠蔽を招くため拒否する。
        // Breakと未知の式は未対応として成功扱いしない。
        const FBPSemanticExpression& Array = Statement.LoopArray;
        const FBPSemanticVariable* SourceVariable = IR.Variables.FindByPredicate(
            [&Array](const FBPSemanticVariable& Variable)
            {
                return Variable.Name == Array.VariableName;
            });
        if (Array.Kind != EBPSemanticExpressionKind::VariableReference ||
            Array.VariableName.IsNone() ||
            IsIdentifier(Array.VariableName.ToString()) == false ||
            Array.Type.ContainerKind != TEXT("array") ||
            SourceVariable == nullptr ||
            SourceVariable->Type.ContainerKind != TEXT("array") ||
            SourceVariable->Type.Kind != EBPSemanticTypeKind::Int32 ||
            Array.Type.Kind != EBPSemanticTypeKind::Int32 ||
            Array.Type.Kind != SourceVariable->Type.Kind ||
            Array.Type.ContainerKind != SourceVariable->Type.ContainerKind ||
            SourceVariable->ReflectedCppType != TEXT("TArray<int32>") ||
            Statement.LoopElementType.Kind != EBPSemanticTypeKind::Int32 ||
            Statement.LoopElementVariable.IsNone() ||
            IsIdentifier(Statement.LoopElementVariable.ToString()) == false ||
            Statement.LoopIndexVariable.IsNone() ||
            IsIdentifier(Statement.LoopIndexVariable.ToString()) == false ||
            Statement.LoopIndexVariable == Statement.LoopElementVariable ||
            Statement.LoopIndexVariable == Array.VariableName ||
            Statement.LoopElementVariable == Array.VariableName ||
            IR.Variables.ContainsByPredicate(
                [&Statement](const FBPSemanticVariable& Variable)
                {
                    return Variable.Name == Statement.LoopIndexVariable ||
                        Variable.Name == Statement.LoopElementVariable;
                }) ||
            Statement.LoopBodyStatements.IsEmpty())
        {
            OutDiagnostics.Add(TEXT("ForEachLoop requires a verified int32 array member and distinct element/index identifiers"));
            return false;
        }
        FString BodyCode;
        bool bUsesElement = false;
        for (const FBPSemanticStatement& Body : Statement.LoopBodyStatements)
        {
            FString Part;
            if (Body.Kind == EBPSemanticStatementKind::Break)
            {
                BodyCode += TEXT("        break;\n");
                continue;
            }
            if (Body.Kind != EBPSemanticStatementKind::Expression)
            {
                OutDiagnostics.Add(TEXT("ForEachLoop body contains an unsupported semantic statement"));
                return false;
            }
            const bool bPrintString =
                Body.Expression.FunctionName == TEXT("PrintString") &&
                Body.Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetSystemLibrary");
            // ForEach固有のIndex/Element参照は、型とローカル名を検証できる
            // PrintString変換だけに限定する。その他の式文は共通Emitterへ渡す。
            if (bPrintString &&
                (Body.Expression.Arguments.Num() != 7 ||
                    (Body.Expression.Arguments[1].Kind != EBPSemanticExpressionKind::Literal &&
                        (Body.Expression.Arguments[1].Kind != EBPSemanticExpressionKind::FunctionCall ||
                            Body.Expression.Arguments[1].FunctionName != TEXT("Conv_IntToString") ||
                            Body.Expression.Arguments[1].FunctionOwnerPath != TEXT("/Script/Engine.KismetStringLibrary") ||
                            Body.Expression.Arguments[1].Arguments.Num() != 1 ||
                            Body.Expression.Arguments[1].Arguments[0].Kind != EBPSemanticExpressionKind::VariableReference ||
                            Body.Expression.Arguments[1].Arguments[0].Type.Kind != EBPSemanticTypeKind::Int32 ||
                            (Body.Expression.Arguments[1].Arguments[0].VariableName != Statement.LoopIndexVariable &&
                                Body.Expression.Arguments[1].Arguments[0].VariableName != Statement.LoopElementVariable)))))
            {
                OutDiagnostics.Add(TEXT("ForEachLoop PrintString requires a literal or verified int32 Index/Element value"));
                return false;
            }
            if (EmitExpressionStatement(Body.Expression, IR.ParentClassPath,
                    TEXT("        "), Part, OutDiagnostics) == false)
            {
                return false;
            }
            if (bPrintString &&
                Body.Expression.Arguments[1].Kind == EBPSemanticExpressionKind::FunctionCall &&
                Body.Expression.Arguments[1].Arguments[0].VariableName == Statement.LoopElementVariable)
            {
                bUsesElement = true;
            }
            BodyCode += Part;
        }
        FString CompletedCode;
        for (const FBPSemanticStatement& Completed : Statement.CompletedStatements)
        {
            FString Part;
            if (Completed.Kind != EBPSemanticStatementKind::Expression ||
                ((Completed.Expression.FunctionName == TEXT("PrintString") &&
                    Completed.Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetSystemLibrary")) &&
                    (Completed.Expression.Arguments.Num() != 7 ||
                        Completed.Expression.Arguments[1].Kind != EBPSemanticExpressionKind::Literal)) ||
                EmitExpressionStatement(Completed.Expression, IR.ParentClassPath,
                    TEXT("    "), Part, OutDiagnostics) == false)
            {
                OutDiagnostics.Add(TEXT("ForEachLoop Completed contains an unsupported semantic statement"));
                return false;
            }
            CompletedCode += Part;
        }
        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
            ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }
        const FString ApiPrefix = ModuleApiMacro.IsEmpty()
            ? FString() : ModuleApiMacro + TEXT(" ");
        const FString ConstructorDeclaration = ConstructorBody.IsEmpty()
            ? FString() : FString::Printf(TEXT("public:\n    %s();\n\n"), *ClassName);
        const FString UnrealTypeInclude = ConstructorBody.IsEmpty()
            ? FString() : TEXT("#include \"UObject/UnrealType.h\"\n");
        const FString ConstructorDefinition = ConstructorBody.IsEmpty()
            ? FString() : FString::Printf(TEXT("\n%s::%s()\n{\n%s}\n"),
                *ClassName, *ClassName, *ConstructorBody);
        FBPGeneratedActorFiles Files;
        Files.HeaderFileName = BaseName + TEXT(".h");
        Files.SourceFileName = BaseName + TEXT(".cpp");
        Files.HeaderContent = FString::Printf(
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%s%s};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration,
            *FunctionShape.Declaration);
        // 配列要素の参照がある場合のみローカル変数を生成する。
        // 未使用の要素コピーを避け、要素参照は現在の反復位置に限定する。
        const FString ElementDeclaration = bUsesElement
            ? FString::Printf(TEXT("        const int32 %s = %s[%s];\n"),
                *Statement.LoopElementVariable.ToString(),
                *Array.VariableName.ToString(), *Statement.LoopIndexVariable.ToString())
            : FString();
        const FString Body = FString::Printf(
            TEXT("%s    for (int32 %s = 0; %s < %s.Num(); ++%s)\n    {\n%s%s    }\n%s"),
            *LeadingCode, *Statement.LoopIndexVariable.ToString(),
            *Statement.LoopIndexVariable.ToString(), *Array.VariableName.ToString(),
            *Statement.LoopIndexVariable.ToString(), *ElementDeclaration, *BodyCode, *CompletedCode);
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n#include \"Kismet/KismetStringLibrary.h\"\n%s%s%s"),
            *BaseName, *LeadingIncludes, *UnrealTypeInclude, *ConstructorDefinition);
        Files.SourceContent += FBPFunctionDefinitionEmitter::RenderDefinition(FunctionShape, Body);
    OutFiles = MoveTemp(Files);
        return true;
    }
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
            if (EmitForLoopBodyStatement(BodyStatement, IR.ParentClassPath,
                    IR.Variables, Statement.LoopIndexVariable, TEXT("        "),
                    0, StatementCode, OutDiagnostics) == false)
            {
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
            if (EmitForLoopBodyStatement(CompletedStatement, IR.ParentClassPath,
                    IR.Variables, Statement.LoopIndexVariable, TEXT("    "),
                    0, StatementCode, OutDiagnostics) == false)
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
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%s%s};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration,
            *FunctionShape.Declaration);
        const FString Body = FString::Printf(
            TEXT("%s    for (int32 %s = %d; %s <= %d; ++%s)\n    {\n%s    }\n%s"),
            *LeadingCode, *Statement.LoopIndexVariable.ToString(), FirstIndex,
            *Statement.LoopIndexVariable.ToString(), LastIndex,
            *Statement.LoopIndexVariable.ToString(), *BodyCode, *CompletedCode);
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n#include \"Kismet/KismetMathLibrary.h\"\n#include \"Kismet/KismetStringLibrary.h\"\n#include \"Kismet/KismetSystemLibrary.h\"\n%s%s%s"),
            *BaseName, *LeadingIncludes, *UnrealTypeInclude, *ConstructorDefinition);
        Files.SourceContent += FBPFunctionDefinitionEmitter::RenderDefinition(FunctionShape, Body);
        OutFiles = MoveTemp(Files);
        return true;
    }
    if (Statement.Kind == EBPSemanticStatementKind::Branch)
    {
        // 通常BranchもForLoopと同じ再帰Emitterを使い、複数文とネストを保持する。
        // LoopIndexを参照しない通常Branchでは名前を空にして評価する。
        FString BranchCode;
        if (EmitForLoopBodyStatement(Statement, IR.ParentClassPath, IR.Variables,
                NAME_None, TEXT("    "), 0, BranchCode, OutDiagnostics) == false)
        {
            return false;
        }
        FString BranchIncludes = TEXT("#include \"Kismet/KismetMathLibrary.h\"\n");
        TFunction<void(const FBPSemanticStatement&)> CollectIncludes;
        CollectIncludes = [&](const FBPSemanticStatement& Current)
        {
            if (Current.Kind == EBPSemanticStatementKind::Expression)
            {
                AppendExpressionIncludes(Current.Expression, BranchIncludes);
            }
            for (const FBPSemanticStatement& Child : Current.ThenStatements)
            {
                CollectIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Current.ElseStatements)
            {
                CollectIncludes(Child);
            }
        };
        CollectIncludes(Statement);

        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
            ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }
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
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%s%s};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration,
            *FunctionShape.Declaration);
        const FString Body = LeadingCode + BranchCode;
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n%s%s%s%s"),
            *BaseName, *BranchIncludes, *LeadingIncludes,
            *UnrealTypeInclude, *ConstructorDefinition);
        Files.SourceContent += FBPFunctionDefinitionEmitter::RenderDefinition(FunctionShape, Body);
        OutFiles = MoveTemp(Files);
        return true;
    }
    if (Statement.Kind == EBPSemanticStatementKind::Expression)
    {
        FString ExpressionStatementCode;
        if (EmitExpressionStatement(Call, IR.ParentClassPath, TEXT("    "),
            ExpressionStatementCode, OutDiagnostics) == false)
        {
            return false;
        }
        FString ExpressionIncludes;
        AppendExpressionIncludes(Call, ExpressionIncludes);
        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
            ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }
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
            TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\nUCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n\n%s%s%s};\n"),
            *ParentClass.HeaderPath, *BaseName, *ApiPrefix, *ClassName,
            *ParentClass.CppClassName, *VariableDeclarations, *ConstructorDeclaration,
            *FunctionShape.Declaration);
        const FString Body = LeadingCode + ExpressionStatementCode;
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n%s%s%s%s"),
            *BaseName, *ExpressionIncludes, *LeadingIncludes,
            *UnrealTypeInclude, *ConstructorDefinition);
        Files.SourceContent += FBPFunctionDefinitionEmitter::RenderDefinition(FunctionShape, Body);
        OutFiles = MoveTemp(Files);
        return true;
    }
    OutDiagnostics.Add(TEXT("Unsupported semantic statement kind"));
    return false;
}
