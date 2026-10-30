#include "BPSemanticCodeGenerator.h"
#include "BPGenericFunctionExpressionEmitter.h"
#include "BPGeneratedFunctionShape.h"
#include "BPFunctionSignatureResolver.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPNumericArithmeticResolver.h"
#include "BPNumericComparisonResolver.h"
#include "BPParentClassResolver.h"
#include "BPReflectedCallResolver.h"
#include "BPTypedLiteralEmitter.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

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

bool EmitReflectedLiteralValue(const FString& RawValue, const FBPSemanticType& Type,
    const bool bMetadataDefault, FString& OutValue, TArray<FString>& Diagnostics)
{
    if (Type.Kind == EBPSemanticTypeKind::Bool)
    {
        if (RawValue != TEXT("true") && RawValue != TEXT("false"))
        {
            Diagnostics.Add(TEXT("Invalid reflected bool literal"));
            return false;
        }
        OutValue = RawValue;
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Int32)
    {
        int32 Parsed = 0;
        if (LexTryParseString(Parsed, *RawValue) == false)
        {
            Diagnostics.Add(TEXT("Invalid reflected int32 literal"));
            return false;
        }
        OutValue = LexToString(Parsed);
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Int64)
    {
        int64 Parsed = 0;
        if (LexTryParseString(Parsed, *RawValue) == false)
        {
            Diagnostics.Add(TEXT("Invalid reflected int64 literal"));
            return false;
        }
        OutValue = Parsed == MIN_int64
            ? TEXT("(-9223372036854775807LL - 1)")
            : LexToString(Parsed) + TEXT("LL");
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Float ||
        Type.Kind == EBPSemanticTypeKind::Double)
    {
        double Parsed = 0.0;
        if (LexTryParseString(Parsed, *RawValue) == false || FMath::IsFinite(Parsed) == false)
        {
            Diagnostics.Add(TEXT("Invalid reflected floating-point literal"));
            return false;
        }
        if (Type.Kind == EBPSemanticTypeKind::Float)
        {
            const float FloatValue = static_cast<float>(Parsed);
            if (FMath::IsFinite(FloatValue) == false)
            {
                Diagnostics.Add(TEXT("Reflected float argument exceeds finite float range"));
                return false;
            }
            OutValue = FloatLiteral(FloatValue);
        }
        else
        {
            OutValue = DoubleLiteral(Parsed);
        }
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::Struct &&
        Type.TypePath == TEXT("/Script/CoreUObject.LinearColor"))
    {
        FLinearColor Color;
        if (Color.InitFromString(RawValue) == false ||
            FMath::IsFinite(Color.R) == false || FMath::IsFinite(Color.G) == false ||
            FMath::IsFinite(Color.B) == false || FMath::IsFinite(Color.A) == false)
        {
            Diagnostics.Add(TEXT("Invalid reflected LinearColor literal"));
            return false;
        }
        OutValue = FString::Printf(TEXT("FLinearColor(%s, %s, %s, %s)"),
            *FloatLiteral(Color.R), *FloatLiteral(Color.G),
            *FloatLiteral(Color.B), *FloatLiteral(Color.A));
        return true;
    }
    if (Type.Kind == EBPSemanticTypeKind::String || Type.Kind == EBPSemanticTypeKind::Name)
    {
        if (bMetadataDefault && Type.Kind == EBPSemanticTypeKind::Name &&
            (RawValue.IsEmpty() || RawValue.Equals(TEXT("None"), ESearchCase::IgnoreCase)))
        {
            OutValue = TEXT("NAME_None");
            return true;
        }
        FString Escaped;
        if (EscapeString(RawValue, Escaped, Diagnostics) == false)
        {
            return false;
        }
        OutValue = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
        return true;
    }
    if (bMetadataDefault && Type.Kind == EBPSemanticTypeKind::Object &&
        (RawValue.Equals(TEXT("None"), ESearchCase::IgnoreCase) ||
         RawValue.Equals(TEXT("nullptr"), ESearchCase::IgnoreCase)))
    {
        OutValue = TEXT("nullptr");
        return true;
    }
    Diagnostics.Add(bMetadataDefault
        ? TEXT("Reflected parameter default type is unsupported")
        : TEXT("Reflected call argument type is unsupported"));
    return false;
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

bool SemanticTypesMatch(const FBPSemanticType& Left,
    const FBPSemanticType& Right, int32 Depth);

bool BuildFunctionShape(const FBPSemanticFunction& Function,
    const FBPResolvedParentClass& ParentClass, const FString& ClassName,
    FBPGeneratedFunctionShape& OutShape, TArray<FString>& Diagnostics)
{
    OutShape = FBPGeneratedFunctionShape();
    const FString FunctionName = Function.Name.ToString();
    EBPSemanticFunctionVisibility EffectiveVisibility = Function.Visibility;
    if (EffectiveVisibility != EBPSemanticFunctionVisibility::Unspecified &&
        EffectiveVisibility != EBPSemanticFunctionVisibility::Public &&
        EffectiveVisibility != EBPSemanticFunctionVisibility::Protected &&
        EffectiveVisibility != EBPSemanticFunctionVisibility::Private)
    {
        Diagnostics.Add(TEXT("Semantic function has an invalid visibility"));
        return false;
    }
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
    if ((Function.bIsOverride == false && Function.OverriddenFunctionPath.IsEmpty() == false) ||
        (Function.bIsInterfaceImplementation == false &&
            (Function.InterfaceClassPath.IsEmpty() == false ||
                Function.InterfaceFunctionPath.IsEmpty() == false)) ||
        (Function.bIsInterfaceImplementation &&
            (Function.InterfaceClassPath.IsEmpty() ||
                Function.InterfaceFunctionPath.IsEmpty())))
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Semantic function %s has inconsistent override or interface metadata"),
            *FunctionName));
        return false;
    }

    // Reference returns require a lifetime policy that is not yet implemented.
    if (Function.ReturnType.bIsReference || Function.ReturnType.bIsConst ||
        (Function.ReturnType.Kind == EBPSemanticTypeKind::Void &&
            Function.ReturnType.ContainerKind != TEXT("none")))
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Semantic function %s has unsupported return type qualifiers or void container"),
            *FunctionName));
        return false;
    }
    FString ReturnType;
    if (EmitFunctionType(Function.ReturnType, ReturnType, Diagnostics) == false)
    {
        return false;
    }
    TArray<const FBPSemanticParameter*> OrderedParameters;
    TSet<FName> ParameterNames;
    TSet<int32> DeclarationIndices;
    int32 ReturnParameterCount = 0;
    for (const FBPSemanticParameter& Parameter : Function.Parameters)
    {
        const FString ParameterName = Parameter.Name.ToString();
        if (IsIdentifier(ParameterName) == false ||
            ParameterNames.Contains(Parameter.Name))
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s has an invalid or duplicate parameter name: %s"),
                *FunctionName, *ParameterName));
            return false;
        }
        ParameterNames.Add(Parameter.Name);
        if (Parameter.bIsReturn)
        {
            ++ReturnParameterCount;
            FString ReflectedReturnType;
            if (ReturnParameterCount > 1 ||
                EmitFunctionType(Parameter.Type, ReflectedReturnType, Diagnostics) == false ||
                SemanticTypesMatch(Parameter.Type, Function.ReturnType, 0) == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s has inconsistent return parameter metadata"),
                    *FunctionName));
                return false;
            }
            continue;
        }
        // Output assignment/dataflow is not implemented. Const reference inputs
        // remain supported even when Reflection also marks them CPF_OutParm.
        if ((Parameter.bIsOut &&
                (Parameter.Type.bIsReference == false || Parameter.Type.bIsConst == false)) ||
            (Parameter.Type.bIsReference && Parameter.Type.bIsConst == false))
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s parameter %s has unsupported output or mutable reference direction"),
                *FunctionName, *ParameterName));
            return false;
        }
        if (Parameter.DeclarationIndex < 0 ||
            DeclarationIndices.Contains(Parameter.DeclarationIndex))
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s has an invalid or duplicate parameter declaration index"),
                *FunctionName));
            return false;
        }
        DeclarationIndices.Add(Parameter.DeclarationIndex);
        OrderedParameters.Add(&Parameter);
    }
    OrderedParameters.Sort([](const FBPSemanticParameter& A, const FBPSemanticParameter& B)
    {
        return A.DeclarationIndex < B.DeclarationIndex;
    });
    for (int32 ParameterIndex = 0; ParameterIndex < OrderedParameters.Num(); ++ParameterIndex)
    {
        if (OrderedParameters[ParameterIndex] == nullptr ||
            OrderedParameters[ParameterIndex]->DeclarationIndex != ParameterIndex)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s parameter declaration indices must be contiguous from zero"),
                *FunctionName));
            return false;
        }
    }
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
            Function.bIsConst || ReturnType != TEXT("void") ||
            Function.bIsInterfaceImplementation ||
            (EffectiveVisibility != EBPSemanticFunctionVisibility::Unspecified &&
                EffectiveVisibility != EBPSemanticFunctionVisibility::Protected))
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
        EffectiveVisibility = EBPSemanticFunctionVisibility::Protected;
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

    if (bOverride && FunctionName != TEXT("BeginPlay"))
    {
        const UClass* Parent = LoadObject<UClass>(nullptr, *ParentClass.ClassPath);
        const auto ValidateTarget = [&](const UFunction* Target, const FString& TargetPath)
        {
            // UFunction does not describe arbitrary C++ virtual methods. Only a
            // native Blueprint event proves the _Implementation override contract.
            if (Target == nullptr || Target->GetPathName() != TargetPath ||
                Target->GetFName() != Function.Name ||
                Target->HasAnyFunctionFlags(FUNC_Native) == false ||
                Target->HasAnyFunctionFlags(FUNC_BlueprintEvent) == false ||
                Target->HasAnyFunctionFlags(FUNC_Final | FUNC_Static | FUNC_Private) ||
                Function.bIsBlueprintEvent == false || bEventImplementation == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Override target for %s is not an accessible native Blueprint event: %s"),
                    *FunctionName, *TargetPath));
                return false;
            }
            FBPResolvedFunctionSignature Signature;
            const EBPSemanticFunctionVisibility TargetVisibility =
                FBPSemanticFunctionExtractor::ResolveVisibility(Target);
            if (TargetVisibility == EBPSemanticFunctionVisibility::Unspecified ||
                (EffectiveVisibility != EBPSemanticFunctionVisibility::Unspecified &&
                    EffectiveVisibility != TargetVisibility))
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Override visibility mismatch for %s"), *FunctionName));
                return false;
            }
            EffectiveVisibility = TargetVisibility;
            if (FBPFunctionSignatureResolver::Resolve(Target, Signature, Diagnostics) == false)
            {
                return false;
            }
            if (Signature.bStatic != Function.bIsStatic ||
                Signature.bConst != Function.bIsConst ||
                SemanticTypesMatch(Signature.ReturnType, Function.ReturnType, 0) == false ||
                Signature.ParameterNames.Num() != OrderedParameters.Num())
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Override signature mismatch for %s: return type, qualifiers or parameter count"),
                    *FunctionName));
                return false;
            }
            for (int32 Index = 0; Index < OrderedParameters.Num(); ++Index)
            {
                const FBPSemanticParameter& Parameter = *OrderedParameters[Index];
                const bool bOutput = Parameter.bIsOut && Parameter.Type.bIsConst == false;
                if (Parameter.Name != Signature.ParameterNames[Index] ||
                    SemanticTypesMatch(Parameter.Type, Signature.ParameterTypes[Index], 0) == false ||
                    bOutput != Signature.OutputParameters[Index])
                {
                    Diagnostics.Add(FString::Printf(
                        TEXT("Override signature mismatch for %s parameter %s at index %d"),
                        *FunctionName, *Parameter.Name.ToString(), Index));
                    return false;
                }
            }
            return true;
        };
        if (Function.bIsOverride &&
            ValidateTarget(Parent != nullptr ? Parent->FindFunctionByName(Function.Name) : nullptr,
                Function.OverriddenFunctionPath) == false)
        {
            return false;
        }
        if (Function.bIsInterfaceImplementation)
        {
            const UClass* Interface = LoadObject<UClass>(nullptr, *Function.InterfaceClassPath);
            // The generated class currently inherits only ParentClass. A newly
            // introduced interface needs an explicit inheritance emitter first.
            if (Parent == nullptr || Interface == nullptr ||
                Interface->HasAnyClassFlags(CLASS_Interface) == false ||
                Parent->ImplementsInterface(Interface) == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Interface override for %s requires an interface implemented by the native parent"),
                    *FunctionName));
                return false;
            }
            if (ValidateTarget(Interface->FindFunctionByName(Function.Name),
                    Function.InterfaceFunctionPath) == false)
            {
                return false;
            }
        }
    }

    const FString OverrideSuffix = bOverride ? TEXT(" override") : TEXT("");
    if (EffectiveVisibility == EBPSemanticFunctionVisibility::Private && bEventImplementation)
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Private Blueprint native events are unsupported: %s"), *FunctionName));
        return false;
    }
    const TCHAR* Visibility = EffectiveVisibility == EBPSemanticFunctionVisibility::Private
        ? TEXT("private") : EffectiveVisibility == EBPSemanticFunctionVisibility::Protected
        ? TEXT("protected") : TEXT("public");
    const FString ProtectedMetadata = EffectiveVisibility == EBPSemanticFunctionVisibility::Protected
        ? TEXT(", meta=(BlueprintProtected)") : FString();
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
                TEXT("%s:\n    UFUNCTION(BlueprintNativeEvent, BlueprintCallable%s)\n")
                TEXT("    %s %s(%s)%s;\n")
                TEXT("    virtual %s %s(%s)%s;\n"),
                Visibility, *ProtectedMetadata, *ReturnType, *FunctionName, *Parameters, *ConstSuffix,
                *ReturnType, *DefinitionFunctionName, *Parameters, *ConstSuffix);
        }
    }
    else
    {
        const FString ReflectionDeclaration = bOverride
            ? FString()
            : FString::Printf(TEXT("    UFUNCTION(BlueprintCallable%s)\n"), *ProtectedMetadata);
        OutShape.Declaration = FString::Printf(
            TEXT("%s:\n%s    %s%s %s(%s)%s%s;\n"),
            Visibility, *ReflectionDeclaration, bOverride ? TEXT("virtual ") : TEXT(""),
            *ReturnType, *FunctionName, *Parameters, *ConstSuffix, *OverrideSuffix);
    }
    OutShape.Definition = FString::Printf(TEXT("%s %s::%s(%s)%s"),
        *ReturnType, *ClassName, *DefinitionFunctionName, *Parameters, *ConstSuffix);
    if (bCallSuper)
    {
        if (ReturnType != TEXT("void"))
        {
            Diagnostics.Add(TEXT("Non-void overrides require an explicit verified Super return policy"));
            return false;
        }
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

bool SemanticTypesMatch(const FBPSemanticType& Left, const FBPSemanticType& Right,
    const int32 Depth = 0)
{
    if (Depth > 16)
    {
        return false;
    }
    const auto NormalizeContainer = [](const FString& Container)
    {
        return Container.IsEmpty() ? FString(TEXT("none")) : Container;
    };
    if (Left.Kind != Right.Kind || Left.TypePath != Right.TypePath ||
        NormalizeContainer(Left.ContainerKind) != NormalizeContainer(Right.ContainerKind) ||
        Left.InnerType != Right.InnerType || Left.KeyType != Right.KeyType ||
        Left.ValueType != Right.ValueType ||
        Left.bIsReference != Right.bIsReference || Left.bIsConst != Right.bIsConst)
    {
        return false;
    }
    if (Left.ElementType.IsValid() != Right.ElementType.IsValid() ||
        Left.KeyTypeInfo.IsValid() != Right.KeyTypeInfo.IsValid() ||
        Left.ValueTypeInfo.IsValid() != Right.ValueTypeInfo.IsValid())
    {
        return false;
    }
    return (Left.ElementType.IsValid() == false ||
            SemanticTypesMatch(*Left.ElementType, *Right.ElementType, Depth + 1)) &&
        (Left.KeyTypeInfo.IsValid() == false ||
            SemanticTypesMatch(*Left.KeyTypeInfo, *Right.KeyTypeInfo, Depth + 1)) &&
        (Left.ValueTypeInfo.IsValid() == false ||
            SemanticTypesMatch(*Left.ValueTypeInfo, *Right.ValueTypeInfo, Depth + 1));
}

bool BuildLinearFunctionStatements(const FBPSemanticFunction& Function,
    TArray<FBPSemanticStatement>& OutStatements, TArray<FString>& Diagnostics)
{
    OutStatements.Reset();
    if (Function.Blocks.IsEmpty())
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Semantic function %s requires at least one non-empty BasicBlock"),
            *Function.Name.ToString()));
        return false;
    }

    const int32 LoopBlockIndex = Function.Blocks.IndexOfByPredicate(
        [](const FBPSemanticBasicBlock& Block)
        {
            return Block.Terminator == EBPSemanticBlockTerminatorKind::Loop;
        });
    if (LoopBlockIndex != INDEX_NONE)
    {
        // Loop CFGの正規形は、任意のPreheaderからヘッダーへ進み、
        // ヘッダーからBody／Completedへ分岐し、Bodyがヘッダーへ戻る。
        // 配列内の構造化Bodyを併用せず、接続関係を唯一の制御フロー情報とする。
        const bool bHasPreheader = LoopBlockIndex == 1;
        if (Function.ReturnType.Kind != EBPSemanticTypeKind::Void ||
            (LoopBlockIndex != 0 && LoopBlockIndex != 1) ||
            Function.Blocks.Num() < (bHasPreheader ? 4 : 3) ||
            Function.Blocks.Num() > (bHasPreheader ? 6 : 5) ||
            Function.Blocks.FilterByPredicate(
                [](const FBPSemanticBasicBlock& Block)
                {
                    return Block.Terminator == EBPSemanticBlockTerminatorKind::Loop;
                }).Num() != 1)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s has an unsupported Loop CFG shape"),
                *Function.Name.ToString()));
            return false;
        }

        TMap<FGuid, const FBPSemanticBasicBlock*> BlocksById;
        for (const FBPSemanticBasicBlock& Block : Function.Blocks)
        {
            if (Block.BlockId.IsValid() == false || BlocksById.Contains(Block.BlockId))
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s requires unique valid BlockIds"),
                    *Function.Name.ToString()));
                return false;
            }
            BlocksById.Add(Block.BlockId, &Block);
        }

        const FBPSemanticBasicBlock& HeaderBlock = Function.Blocks[LoopBlockIndex];
        if (bHasPreheader)
        {
            const FBPSemanticBasicBlock& PreheaderBlock = Function.Blocks[0];
            if (PreheaderBlock.Statements.IsEmpty() ||
                PreheaderBlock.Terminator != EBPSemanticBlockTerminatorKind::Fallthrough ||
                PreheaderBlock.SuccessorBlockIds.Num() != 1 ||
                PreheaderBlock.SuccessorBlockIds[0] != HeaderBlock.BlockId)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s Loop CFG has an invalid Preheader block"),
                    *Function.Name.ToString()));
                return false;
            }
        }
        if (HeaderBlock.Statements.Num() != 1 ||
            (HeaderBlock.Statements[0].Kind != EBPSemanticStatementKind::ForLoop &&
             HeaderBlock.Statements[0].Kind != EBPSemanticStatementKind::ForEachLoop) ||
            HeaderBlock.Statements[0].LoopBodyStatements.IsEmpty() == false ||
            HeaderBlock.Statements[0].CompletedStatements.IsEmpty() == false ||
            HeaderBlock.SuccessorBlockIds.Num() != 2 ||
            HeaderBlock.SuccessorBlockIds[0] == HeaderBlock.SuccessorBlockIds[1])
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s requires one unstructured Loop statement and Body/Completed successors"),
                *Function.Name.ToString()));
            return false;
        }
        const FBPSemanticBasicBlock* const* BodyBlockValue =
            BlocksById.Find(HeaderBlock.SuccessorBlockIds[0]);
        const FBPSemanticBasicBlock* const* CompletedBlockValue =
            BlocksById.Find(HeaderBlock.SuccessorBlockIds[1]);
        const FBPSemanticBasicBlock* BodyBlock =
            BodyBlockValue != nullptr ? *BodyBlockValue : nullptr;
        const FBPSemanticBasicBlock* CompletedBlock =
            CompletedBlockValue != nullptr ? *CompletedBlockValue : nullptr;
        if (BodyBlock == nullptr || CompletedBlock == nullptr ||
            BodyBlock->Statements.IsEmpty() ||
            CompletedBlock->Terminator != EBPSemanticBlockTerminatorKind::End ||
            CompletedBlock->SuccessorBlockIds.IsEmpty() == false)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s Loop CFG requires a Body back-edge and terminal Completed block"),
                *Function.Name.ToString()));
            return false;
        }

        FBPSemanticStatement StructuredLoop = HeaderBlock.Statements[0];
        if (BodyBlock->Terminator == EBPSemanticBlockTerminatorKind::Branch)
        {
            const int32 ExpectedBranchBlockCount = bHasPreheader ? 6 : 5;
            if (Function.Blocks.Num() != ExpectedBranchBlockCount ||
                BodyBlock->Statements.Num() != 1 ||
                BodyBlock->Statements[0].Kind != EBPSemanticStatementKind::Branch ||
                BodyBlock->Statements[0].ThenStatements.IsEmpty() == false ||
                BodyBlock->Statements[0].ElseStatements.IsEmpty() == false ||
                BodyBlock->SuccessorBlockIds.Num() != 2 ||
                BodyBlock->SuccessorBlockIds[0] == BodyBlock->SuccessorBlockIds[1])
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s Loop Body Branch CFG is invalid"),
                    *Function.Name.ToString()));
                return false;
            }
            const FBPSemanticBasicBlock* const* TrueBlockValue =
                BlocksById.Find(BodyBlock->SuccessorBlockIds[0]);
            const FBPSemanticBasicBlock* const* FalseBlockValue =
                BlocksById.Find(BodyBlock->SuccessorBlockIds[1]);
            const FBPSemanticBasicBlock* TrueBlock =
                TrueBlockValue != nullptr ? *TrueBlockValue : nullptr;
            const FBPSemanticBasicBlock* FalseBlock =
                FalseBlockValue != nullptr ? *FalseBlockValue : nullptr;
            const auto IsLoopLatch = [&HeaderBlock](const FBPSemanticBasicBlock* Block)
            {
                return Block != nullptr && Block->Statements.IsEmpty() == false &&
                    Block->Terminator == EBPSemanticBlockTerminatorKind::Fallthrough &&
                    Block->SuccessorBlockIds.Num() == 1 &&
                    Block->SuccessorBlockIds[0] == HeaderBlock.BlockId;
            };
            if (TrueBlock == FalseBlock || IsLoopLatch(TrueBlock) == false ||
                IsLoopLatch(FalseBlock) == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s Loop Body Branch requires True/False back-edges"),
                    *Function.Name.ToString()));
                return false;
            }
            FBPSemanticStatement StructuredBranch = BodyBlock->Statements[0];
            StructuredBranch.ThenStatements = TrueBlock->Statements;
            StructuredBranch.ElseStatements = FalseBlock->Statements;
            StructuredLoop.LoopBodyStatements.Add(MoveTemp(StructuredBranch));
        }
        else
        {
            const int32 ExpectedSimpleBlockCount = bHasPreheader ? 4 : 3;
            if (Function.Blocks.Num() != ExpectedSimpleBlockCount ||
                BodyBlock->Terminator != EBPSemanticBlockTerminatorKind::Fallthrough ||
                BodyBlock->SuccessorBlockIds.Num() != 1 ||
                BodyBlock->SuccessorBlockIds[0] != HeaderBlock.BlockId)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s Loop CFG requires a Body back-edge"),
                    *Function.Name.ToString()));
                return false;
            }
            StructuredLoop.LoopBodyStatements = BodyBlock->Statements;
        }
        StructuredLoop.CompletedStatements = CompletedBlock->Statements;
        if (bHasPreheader)
        {
            OutStatements.Append(Function.Blocks[0].Statements);
        }
        OutStatements.Add(MoveTemp(StructuredLoop));
        return true;
    }

    const int32 BranchBlockIndex = Function.Blocks.IndexOfByPredicate(
        [](const FBPSemanticBasicBlock& Block)
        {
            return Block.Terminator == EBPSemanticBlockTerminatorKind::Branch;
        });
    if (BranchBlockIndex != INDEX_NONE)
    {
        // 最初のCFG対応は、入口BranchからTrue/Falseへ分かれ、両枝が終了するか
        // 同じJoinへFallthroughする正規形に限定する。既存の構造化Emitterへ渡す
        // 前に接続を厳密に検証し、BasicBlock順から制御フローを推測しない。
        if (Function.ReturnType.Kind != EBPSemanticTypeKind::Void ||
            BranchBlockIndex != 0 ||
            Function.Blocks.FilterByPredicate(
                [](const FBPSemanticBasicBlock& Block)
                {
                    return Block.Terminator == EBPSemanticBlockTerminatorKind::Branch;
                }).Num() != 1)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s has an unsupported Branch CFG shape"),
                *Function.Name.ToString()));
            return false;
        }

        TMap<FGuid, const FBPSemanticBasicBlock*> BlocksById;
        for (const FBPSemanticBasicBlock& Block : Function.Blocks)
        {
            if (Block.BlockId.IsValid() == false || BlocksById.Contains(Block.BlockId))
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s requires unique valid BlockIds"),
                    *Function.Name.ToString()));
                return false;
            }
            BlocksById.Add(Block.BlockId, &Block);
        }

        const FBPSemanticBasicBlock& BranchBlock = Function.Blocks[0];
        if (BranchBlock.Statements.Num() != 1 ||
            BranchBlock.Statements[0].Kind != EBPSemanticStatementKind::Branch ||
            BranchBlock.Statements[0].ThenStatements.IsEmpty() == false ||
            BranchBlock.Statements[0].ElseStatements.IsEmpty() == false ||
            BranchBlock.SuccessorBlockIds.Num() != 2 ||
            BranchBlock.SuccessorBlockIds[0] == BranchBlock.SuccessorBlockIds[1])
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s requires one unstructured Branch statement and True/False successors"),
                *Function.Name.ToString()));
            return false;
        }
        const FBPSemanticBasicBlock* const* TrueBlockValue =
            BlocksById.Find(BranchBlock.SuccessorBlockIds[0]);
        const FBPSemanticBasicBlock* const* FalseBlockValue =
            BlocksById.Find(BranchBlock.SuccessorBlockIds[1]);
        const FBPSemanticBasicBlock* TrueBlock =
            TrueBlockValue != nullptr ? *TrueBlockValue : nullptr;
        const FBPSemanticBasicBlock* FalseBlock =
            FalseBlockValue != nullptr ? *FalseBlockValue : nullptr;
        if (TrueBlock == nullptr || FalseBlock == nullptr ||
            TrueBlock->Statements.IsEmpty() || FalseBlock->Statements.IsEmpty())
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s Branch CFG references an unknown or empty successor"),
                *Function.Name.ToString()));
            return false;
        }

        const bool bTerminalBranches =
            TrueBlock->Terminator == EBPSemanticBlockTerminatorKind::End &&
            FalseBlock->Terminator == EBPSemanticBlockTerminatorKind::End &&
            TrueBlock->SuccessorBlockIds.IsEmpty() &&
            FalseBlock->SuccessorBlockIds.IsEmpty();
        const bool bJoinedBranches =
            TrueBlock->Terminator == EBPSemanticBlockTerminatorKind::Fallthrough &&
            FalseBlock->Terminator == EBPSemanticBlockTerminatorKind::Fallthrough &&
            TrueBlock->SuccessorBlockIds.Num() == 1 &&
            FalseBlock->SuccessorBlockIds.Num() == 1 &&
            TrueBlock->SuccessorBlockIds[0] == FalseBlock->SuccessorBlockIds[0];
        const FBPSemanticBasicBlock* JoinBlock = nullptr;
        if (bJoinedBranches)
        {
            const FBPSemanticBasicBlock* const* JoinBlockValue =
                BlocksById.Find(TrueBlock->SuccessorBlockIds[0]);
            JoinBlock = JoinBlockValue != nullptr ? *JoinBlockValue : nullptr;
            if (JoinBlock == nullptr || JoinBlock == TrueBlock || JoinBlock == FalseBlock ||
                JoinBlock->Terminator != EBPSemanticBlockTerminatorKind::End ||
                JoinBlock->SuccessorBlockIds.IsEmpty() == false ||
                JoinBlock->Statements.IsEmpty())
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s has an invalid Branch join block"),
                    *Function.Name.ToString()));
                return false;
            }
        }
        else if (bTerminalBranches == false)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s Branch successors must terminate or share one join block"),
                *Function.Name.ToString()));
            return false;
        }

        const int32 ExpectedBlockCount = JoinBlock != nullptr ? 4 : 3;
        if (Function.Blocks.Num() != ExpectedBlockCount)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s Branch CFG contains unreachable or unsupported blocks"),
                *Function.Name.ToString()));
            return false;
        }
        FBPSemanticStatement StructuredBranch = BranchBlock.Statements[0];
        StructuredBranch.ThenStatements = TrueBlock->Statements;
        StructuredBranch.ElseStatements = FalseBlock->Statements;
        OutStatements.Add(MoveTemp(StructuredBranch));
        if (JoinBlock != nullptr)
        {
            OutStatements.Append(JoinBlock->Statements);
        }
        return true;
    }

    TSet<FGuid> BlockIds;
    for (int32 BlockIndex = 0; BlockIndex < Function.Blocks.Num(); ++BlockIndex)
    {
        const FBPSemanticBasicBlock& Block = Function.Blocks[BlockIndex];
        if (Block.Statements.IsEmpty())
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s contains an empty BasicBlock"),
                *Function.Name.ToString()));
            return false;
        }
        if (Function.Blocks.Num() > 1)
        {
            if (Block.BlockId.IsValid() == false || BlockIds.Contains(Block.BlockId))
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s requires unique valid BlockIds"),
                    *Function.Name.ToString()));
                return false;
            }
            BlockIds.Add(Block.BlockId);
            const bool bLastBlock = BlockIndex + 1 == Function.Blocks.Num();
            if (bLastBlock == false)
            {
                const FGuid ExpectedSuccessor = Function.Blocks[BlockIndex + 1].BlockId;
                if (Block.Terminator != EBPSemanticBlockTerminatorKind::Fallthrough ||
                    Block.SuccessorBlockIds.Num() != 1 ||
                    Block.SuccessorBlockIds[0] != ExpectedSuccessor)
                {
                    Diagnostics.Add(FString::Printf(
                        TEXT("Semantic function %s has a non-linear or invalid BasicBlock successor"),
                        *Function.Name.ToString()));
                    return false;
                }
            }
            else if (Block.SuccessorBlockIds.IsEmpty() == false ||
                (Block.Terminator != EBPSemanticBlockTerminatorKind::End &&
                 Block.Terminator != EBPSemanticBlockTerminatorKind::Return))
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Semantic function %s requires an End or Return terminal BasicBlock"),
                    *Function.Name.ToString()));
                return false;
            }
        }
        else if (Block.SuccessorBlockIds.IsEmpty() == false ||
            Block.Terminator == EBPSemanticBlockTerminatorKind::Fallthrough ||
            Block.Terminator == EBPSemanticBlockTerminatorKind::Branch ||
            Block.Terminator == EBPSemanticBlockTerminatorKind::Loop)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s has an invalid single BasicBlock terminator"),
                *Function.Name.ToString()));
            return false;
        }
        OutStatements.Append(Block.Statements);
    }

    for (int32 Index = 0; Index + 1 < OutStatements.Num(); ++Index)
    {
        if (OutStatements[Index].Kind == EBPSemanticStatementKind::Return)
        {
            Diagnostics.Add(FString::Printf(
                TEXT("Semantic function %s has a non-terminal Return statement"),
                *Function.Name.ToString()));
            return false;
        }
    }
    if (Function.ReturnType.Kind != EBPSemanticTypeKind::Void &&
        OutStatements.Last().Kind != EBPSemanticStatementKind::Return)
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Non-void semantic function %s requires a terminal Return statement"),
            *Function.Name.ToString()));
        return false;
    }
    const FBPSemanticBasicBlock& LastBlock = Function.Blocks.Last();
    if (Function.Blocks.Num() > 1 &&
        ((LastBlock.Terminator == EBPSemanticBlockTerminatorKind::Return) !=
            (OutStatements.Last().Kind == EBPSemanticStatementKind::Return)))
    {
        Diagnostics.Add(FString::Printf(
            TEXT("Semantic function %s terminal kind does not match its final statement"),
            *Function.Name.ToString()));
        return false;
    }
    return true;
}

bool EmitReturnStatement(const FBPSemanticFunction& Function,
    const FBPSemanticStatement& Statement, const FString& ParentClassPath,
    const FString& Indent, FString& OutCode, TArray<FString>& Diagnostics)
{
    OutCode.Reset();
    if (Statement.Kind != EBPSemanticStatementKind::Return)
    {
        Diagnostics.Add(TEXT("Return emitter received a non-Return statement"));
        return false;
    }
    if (Function.ReturnType.Kind == EBPSemanticTypeKind::Void)
    {
        const bool bEmptyExpression =
            Statement.Expression.Kind == EBPSemanticExpressionKind::Literal &&
            (Statement.Expression.Type.Kind == EBPSemanticTypeKind::Unresolved ||
                Statement.Expression.Type.Kind == EBPSemanticTypeKind::Void) &&
            Statement.Expression.SourceNodeId.IsValid() == false &&
            Statement.Expression.SourcePinId.IsValid() == false &&
            Statement.Expression.LiteralValue.IsEmpty() &&
            Statement.Expression.VariableName.IsNone() &&
            Statement.Expression.FunctionName.IsNone() &&
            Statement.Expression.FunctionOwnerPath.IsEmpty() &&
            Statement.Expression.Arguments.IsEmpty();
        if (bEmptyExpression == false)
        {
            Diagnostics.Add(TEXT("Void semantic function cannot return a value"));
            return false;
        }
        OutCode = Indent + TEXT("return;\n");
        return true;
    }
    if (Function.ReturnType.bIsReference ||
        SemanticTypesMatch(Function.ReturnType, Statement.Expression.Type) == false)
    {
        Diagnostics.Add(TEXT("Return expression type does not match the semantic function return type"));
        return false;
    }

    FString ExpressionCode;
    if (Statement.Expression.Kind == EBPSemanticExpressionKind::Literal)
    {
        if (FBPTypedLiteralEmitter::Emit(Statement.Expression.Type,
                Statement.Expression.LiteralValue, ExpressionCode) == false)
        {
            Diagnostics.Add(TEXT("Return literal type is unsupported"));
            return false;
        }
    }
    else if (Statement.Expression.Kind == EBPSemanticExpressionKind::VariableReference)
    {
        const FBPSemanticParameter* Parameter = Function.Parameters.FindByPredicate(
            [&Statement](const FBPSemanticParameter& Candidate)
            {
                return Candidate.bIsReturn == false &&
                    Candidate.Name == Statement.Expression.VariableName;
            });
        if (Parameter == nullptr || Parameter->Name.IsNone() ||
            IsIdentifier(Parameter->Name.ToString()) == false ||
            SemanticTypesMatch(Parameter->Type, Statement.Expression.Type) == false)
        {
            Diagnostics.Add(TEXT("Return variable must reference a matching function parameter"));
            return false;
        }
        ExpressionCode = Parameter->Name.ToString();
    }
    else if (Statement.Expression.Kind == EBPSemanticExpressionKind::FunctionCall)
    {
        const UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
        if (FBPGenericFunctionExpressionEmitter::Emit(
                Statement.Expression, ParentClass, ExpressionCode, Diagnostics) == false)
        {
            Diagnostics.Add(TEXT("Return function call is not a verified pure expression"));
            return false;
        }
    }
    else
    {
        Diagnostics.Add(TEXT("Unsupported Return expression kind"));
        return false;
    }
    OutCode = FString::Printf(TEXT("%sreturn %s;\n"), *Indent, *ExpressionCode);
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

// 括弧で囲まれた二項算術式の最上位演算子を調べる。
// 数値リテラルの符号やネスト内の演算子は対象外にする。
int32 GetWrappedArithmeticPrecedence(const FString& Expression)
{
    if (Expression.Len() < 5 || Expression[0] != TEXT('(') ||
        Expression[Expression.Len() - 1] != TEXT(')'))
    {
        return 0;
    }
    int32 Depth = 0;
    int32 Precedence = 0;
    for (int32 Index = 0; Index < Expression.Len(); ++Index)
    {
        const TCHAR Character = Expression[Index];
        if (Character == TEXT('('))
        {
            ++Depth;
        }
        else if (Character == TEXT(')'))
        {
            --Depth;
            if (Depth == 0 && Index != Expression.Len() - 1)
            {
                return 0;
            }
        }
        else if (Depth == 1 && Index > 0 && Index + 1 < Expression.Len() &&
            Expression[Index - 1] == TEXT(' ') &&
            Expression[Index + 1] == TEXT(' '))
        {
            if (Character == TEXT('+') || Character == TEXT('-'))
            {
                // 同じ階層に乗除があっても、加減が式全体の優先順位を決める。
                Precedence = 1;
            }
            else if ((Character == TEXT('*') || Character == TEXT('/')) &&
                Precedence == 0)
            {
                Precedence = 2;
            }
        }
        if (Depth < 0)
        {
            return 0;
        }
    }
    return Depth == 0 ? Precedence : 0;
}

// 子式の優先順位が高ければ括弧を省略できる。
// 同順位の場合は左辺だけを省略する。右辺の同順位は減算や
// 浮動小数点の丸め順序を変え得るため括弧を維持する。
FString FormatArithmeticChild(const FString& Expression,
    const int32 ParentPrecedence, const bool bLeft)
{
    const int32 ChildPrecedence = GetWrappedArithmeticPrecedence(Expression);
    if (ChildPrecedence > ParentPrecedence ||
        (bLeft && ChildPrecedence == ParentPrecedence))
    {
        if (ChildPrecedence != 0)
        {
            return Expression.Mid(1, Expression.Len() - 2);
        }
    }
    return Expression;
}

// 四則演算の表示形式を共通化する。未知の関数は変換しない。
// 最外周括弧は親の式生成時に安全性を判定して省略する。
bool FormatArithmeticOperator(const FName FunctionName,
    const FString& A, const FString& B, FString& Out)
{
    const FString Name = FunctionName.ToString();
    const TCHAR* Symbol = Name.StartsWith(TEXT("Add_")) ? TEXT("+")
        : Name.StartsWith(TEXT("Subtract_")) ? TEXT("-")
        : Name.StartsWith(TEXT("Multiply_")) ? TEXT("*")
        : Name.StartsWith(TEXT("Divide_")) ? TEXT("/") : nullptr;
    if (Symbol == nullptr)
    {
        return false;
    }
    const int32 Precedence =
        (Name.StartsWith(TEXT("Multiply_")) || Name.StartsWith(TEXT("Divide_"))) ? 2 : 1;
    const FString Left = FormatArithmeticChild(A, Precedence, true);
    const FString Right = FormatArithmeticChild(B, Precedence, false);
    Out = FString::Printf(TEXT("(%s %s %s)"), *Left, Symbol, *Right);
    return true;
}

// 比較演算子は算術演算子より優先順位が低いため、比較式の直下にある
// 算術式の最外周括弧のみ省略できる。内側の括弧は演算順序保持のため残す。
FString UnwrapComparisonOperand(const FString& Expression)
{
    if (Expression.Len() < 2 || Expression[0] != TEXT('(') ||
        Expression[Expression.Len() - 1] != TEXT(')'))
    {
        return Expression;
    }
    int32 Depth = 0;
    for (int32 Index = 0; Index < Expression.Len(); ++Index)
    {
        if (Expression[Index] == TEXT('('))
        {
            ++Depth;
        }
        else if (Expression[Index] == TEXT(')'))
        {
            --Depth;
            if (Depth == 0 && Index != Expression.Len() - 1)
            {
                return Expression;
            }
        }
        if (Depth < 0)
        {
            return Expression;
        }
    }
    return Depth == 0 ? Expression.Mid(1, Expression.Len() - 2) : Expression;
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
        // 0と-1以外の整数リテラル除数なら、ゼロ除算とINT_MIN/-1の
        // オーバーフローを避けられるためC++の除算演算子に変換する。
        // それ以外はBlueprint固有の挙動を維持する。
        const bool bDivision = Operation.FunctionName.ToString().StartsWith(TEXT("Divide_"));
        int32 Divisor = 0;
        const bool bSafeIntegerDivision = bDivision &&
            OperandType == EBPSemanticTypeKind::Int32 &&
            Expression.Arguments[1].Kind == EBPSemanticExpressionKind::Literal &&
            LexTryParseString(Divisor, *Expression.Arguments[1].LiteralValue) &&
            Divisor != 0 && Divisor != -1;
        if (bDivision && bSafeIntegerDivision == false)
        {
            OutOperand = FString::Printf(TEXT("UKismetMathLibrary::%s(%s, %s)"),
                *Operation.FunctionName.ToString(), *A, *B);
        }
        else if (FormatArithmeticOperator(Operation.FunctionName, A, B, OutOperand) == false)
        {
            Diagnostics.Add(TEXT("Unsupported numeric arithmetic operator"));
            return false;
        }
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
    OutCondition = FString::Printf(TEXT("%s %s %s"),
        *UnwrapComparisonOperand(A), *Comparison.OperatorToken, *UnwrapComparisonOperand(B));
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
        *UnwrapComparisonOperand(A), *Comparison.OperatorToken, *UnwrapComparisonOperand(B));
    return true;
}

// PrintString専用経路ではBlueprint変数の宣言一覧を受け取らないため、
// 少なくとも既存ネイティブプロパティと同名の場合はReflection型を照合する。
// 見つからない名前は既存のBlueprint変数参照との互換性を維持する。
bool IsCompatibleStringMember(const FName Name, const FString& ParentClassPath,
    const TArray<FBPSemanticVariable>* Variables)
{
    // Semantic変数一覧が渡された経路では、名前だけで任意の
    // BlueprintメンバーをC++に出力せず、宣言された型を照合する。
    if (Variables != nullptr)
    {
        const FBPSemanticVariable* Variable = Variables->FindByPredicate(
            [Name](const FBPSemanticVariable& Candidate)
            {
                return Candidate.Name == Name;
            });
        if (Variable != nullptr)
        {
            const bool bScalar = Variable->Type.ContainerKind.IsEmpty() ||
                Variable->Type.ContainerKind == TEXT("none");
            return Variable->Type.Kind == EBPSemanticTypeKind::String &&
                bScalar &&
                Variable->Type.bIsReference == false &&
                Variable->Type.bIsConst == false &&
                Variable->ReflectedCppType == TEXT("FString");
        }
    }
    const UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
    if (ParentClass == nullptr)
    {
        return false;
    }
    const FProperty* Member = ParentClass->FindPropertyByName(Name);
    if (Member == nullptr)
    {
        // 変数宣言一覧を持つ経路では未宣言名を許可しない。
        // 一覧を受け取れない旧経路のみ従来動作を維持する。
        return Variables == nullptr;
    }
    return CastField<FStrProperty>(Member) != nullptr &&
        Member->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic);
}

bool EmitVerifiedIntegerExpression(const FBPSemanticExpression& Expression,
    const FString& ParentClassPath, const TArray<FBPSemanticVariable>* Variables,
    const FName LocalIndex, const FName LocalElement, const int32 Depth,
    FString& OutExpression, const TArray<FName>* LocalIntegerVariables = nullptr)
{
    OutExpression.Reset();
    const bool bScalar = Expression.Type.ContainerKind.IsEmpty() ||
        Expression.Type.ContainerKind == TEXT("none");
    if (Depth > 16 || Expression.Type.Kind != EBPSemanticTypeKind::Int32 ||
        bScalar == false ||
        Expression.Type.bIsReference || Expression.Type.bIsConst)
    {
        return false;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::Literal)
    {
        int32 Value = 0;
        if (LexTryParseString(Value, *Expression.LiteralValue) == false)
        {
            return false;
        }
        OutExpression = LexToString(Value);
        return true;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::VariableReference)
    {
        if (Expression.VariableName.IsNone() ||
            Expression.VariableName == TEXT("this") ||
            IsIdentifier(Expression.VariableName.ToString()) == false)
        {
            return false;
        }

        // SourcePinIdを持たない参照はループローカルに限定する。
        // 任意の名前をローカル変数として出力すると、未宣言識別子や
        // スコープ外のIndexを含むC++を生成できてしまう。
        if (Expression.SourcePinId.IsValid() == false)
        {
            const bool bActiveLoopLocal = LocalIntegerVariables != nullptr &&
                LocalIntegerVariables->Contains(Expression.VariableName);
            if (bActiveLoopLocal == false && Expression.VariableName != LocalIndex &&
                Expression.VariableName != LocalElement)
            {
                return false;
            }
            OutExpression = Expression.VariableName.ToString();
            return true;
        }

        // Getter由来の参照はSemantic変数宣言または親クラスReflectionの
        // どちらかとint32単一値として一致した場合だけメンバー式にする。
        if (Variables != nullptr)
        {
            const FBPSemanticVariable* Variable = Variables->FindByPredicate(
                [&Expression](const FBPSemanticVariable& Candidate)
                {
                    return Candidate.Name == Expression.VariableName;
                });
            if (Variable != nullptr)
            {
                const bool bVariableScalar = Variable->Type.ContainerKind.IsEmpty() ||
                    Variable->Type.ContainerKind == TEXT("none");
                if (Variable->Type.Kind != EBPSemanticTypeKind::Int32 ||
                    bVariableScalar == false ||
                    Variable->Type.bIsReference || Variable->Type.bIsConst ||
                    Variable->ReflectedCppType != TEXT("int32"))
                {
                    return false;
                }
                OutExpression = FString::Printf(TEXT("this->%s"),
                    *Expression.VariableName.ToString());
                return true;
            }
        }
        const UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
        const FProperty* Member = ParentClass != nullptr
            ? ParentClass->FindPropertyByName(Expression.VariableName) : nullptr;
        if (Member == nullptr)
        {
            // 変数一覧を取得できない旧経路だけは従来互換を維持する。
            if (Variables != nullptr)
            {
                return false;
            }
        }
        else if (CastField<FIntProperty>(Member) == nullptr ||
            Member->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic) == false)
        {
            return false;
        }
        OutExpression = FString::Printf(TEXT("this->%s"),
            *Expression.VariableName.ToString());
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
    if (EmitVerifiedIntegerExpression(Expression.Arguments[0], ParentClassPath,
            Variables, LocalIndex, LocalElement, Depth + 1, Left,
            LocalIntegerVariables) == false ||
        EmitVerifiedIntegerExpression(Expression.Arguments[1], ParentClassPath,
            Variables, LocalIndex, LocalElement, Depth + 1, Right,
            LocalIntegerVariables) == false)
    {
        return false;
    }
    return FormatArithmeticOperator(Expression.FunctionName, Left, Right, OutExpression);
}

// ReflectionでFString引数と確認されたCallから再利用する文字列式Emitter。
// PrintStringという関数名には依存せず、許可したKismet文字列演算と
// 検証済みメンバー／ループローカルだけをC++式へ変換する。
bool EmitVerifiedStringExpression(const FBPSemanticExpression& Expression,
    const FString& ParentClassPath, const TArray<FBPSemanticVariable>* Variables,
    const FName LocalIndex, const FName LocalElement, const int32 Depth,
    FString& OutExpression, TArray<FString>& Diagnostics,
    const TArray<FName>* LocalIntegerVariables = nullptr)
{
    OutExpression.Reset();
    const bool bScalar = Expression.Type.ContainerKind.IsEmpty() ||
        Expression.Type.ContainerKind == TEXT("none");
    if (Depth > 16 || Expression.Type.Kind != EBPSemanticTypeKind::String ||
        bScalar == false || Expression.Type.bIsReference || Expression.Type.bIsConst)
    {
        return false;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::Literal)
    {
        FString Value;
        FString Escaped;
        if (ReadArgument(Expression, EBPSemanticTypeKind::String, Value, Diagnostics) == false ||
            EscapeString(Value, Escaped, Diagnostics) == false)
        {
            return false;
        }
        OutExpression = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
        return true;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::VariableReference)
    {
        if (IsIdentifier(Expression.VariableName.ToString()) == false ||
            Expression.VariableName == TEXT("this") ||
            IsCompatibleStringMember(Expression.VariableName,
                ParentClassPath, Variables) == false)
        {
            return false;
        }
        OutExpression = FString::Printf(TEXT("this->%s"),
            *Expression.VariableName.ToString());
        return true;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::FunctionCall &&
        Expression.FunctionOwnerPath == TEXT("/Script/Engine.KismetStringLibrary") &&
        Expression.FunctionName == TEXT("Conv_IntToString") &&
        Expression.Arguments.Num() == 1)
    {
        FString IntegerExpression;
        if (EmitVerifiedIntegerExpression(Expression.Arguments[0],
                ParentClassPath, Variables, LocalIndex, LocalElement,
                Depth + 1, IntegerExpression, LocalIntegerVariables) == false)
        {
            return false;
        }
        OutExpression = FString::Printf(
            TEXT("UKismetStringLibrary::Conv_IntToString(%s)"), *IntegerExpression);
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
    if (EmitVerifiedStringExpression(Expression.Arguments[0], ParentClassPath,
            Variables, LocalIndex, LocalElement, Depth + 1, Left, Diagnostics,
            LocalIntegerVariables) == false ||
        EmitVerifiedStringExpression(Expression.Arguments[1], ParentClassPath,
            Variables, LocalIndex, LocalElement, Depth + 1, Right, Diagnostics,
            LocalIntegerVariables) == false)
    {
        return false;
    }
    OutExpression = FString::Printf(TEXT("UKismetStringLibrary::Concat_StrStr(%s, %s)"),
        *Left, *Right);
    return true;
}

bool EmitActorLocationStatement(const FBPSemanticExpression& Call,
    const FString& ParentClassPath, const FString& Indent, FString& OutStatement,
    TArray<FString>& Diagnostics);

// 検証済みSemantic式文の共通ディスパッチ。
// ループやBranchは特定の関数名を直接判定せず、この関数を通して対応範囲を共有する。
bool EmitExpressionStatement(const FBPSemanticExpression& Expression,
    const FString& ParentClassPath, const FString& Indent, FString& OutStatement,
    TArray<FString>& Diagnostics, const FName LocalIndex = NAME_None,
    const FName LocalElement = NAME_None,
    const TArray<FBPSemanticVariable>* Variables = nullptr,
    const TArray<FName>* LocalIntegerVariables = nullptr)
{
    if (Expression.Kind != EBPSemanticExpressionKind::FunctionCall)
    {
        Diagnostics.Add(TEXT("Expression statement requires a verified function call"));
        return false;
    }
    if (Expression.FunctionName == TEXT("K2_SetActorLocation") &&
        Expression.FunctionOwnerPath == TEXT("/Script/Engine.Actor"))
    {
        return EmitActorLocationStatement(Expression, ParentClassPath,
            Indent, OutStatement, Diagnostics);
    }
    // Actorのネイティブ関数呼び出しは、Loweringで検証済みのリテラルのみを
    // C++引数へ変換する。任意の式や参照引数を文字列として出力しない。
    // Semantic IRの所有クラスを共通Resolverで検証する。
    // static/selfの区別は先頭引数のthis参照から判断し、反射結果と照合する。
    const bool bHasTarget = Expression.Arguments.IsEmpty() == false &&
        Expression.Arguments[0].Kind == EBPSemanticExpressionKind::VariableReference;
    const bool bSelfCall = bHasTarget &&
        Expression.Arguments[0].VariableName == TEXT("this");
    const UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
    const FObjectPropertyBase* TargetProperty = nullptr;
    const UClass* TargetClass = ParentClass;
    if (bHasTarget && bSelfCall == false && ParentClass != nullptr)
    {
        // self以外のTargetは、親クラスに実在する公開C++オブジェクトプロパティのみ。
        // 未検証のBlueprint変数や複雑な式をC++識別子として出力しない。
        const FProperty* Property = ParentClass->FindPropertyByName(
            Expression.Arguments[0].VariableName);
        TargetProperty = CastField<FObjectPropertyBase>(Property);
        if (TargetProperty != nullptr &&
            CastField<FObjectProperty>(Property) != nullptr &&
            TargetProperty->PropertyClass != nullptr &&
            Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic) &&
            IsIdentifier(Expression.Arguments[0].VariableName.ToString()))
        {
            TargetClass = TargetProperty->PropertyClass;
        }
        else
        {
            TargetClass = nullptr;
        }
    }
    // WorldContext付き静的UFunctionは、IRの先頭thisをTargetではなく
    // Reflectionで指定されたWorldContext引数として扱う。
    // メタデータの名前と先頭パラメータの一致を確認できない場合は許可しない。
    const UClass* CandidateOwner = FindObject<UClass>(nullptr, *Expression.FunctionOwnerPath);
    const UFunction* CandidateFunction = CandidateOwner != nullptr
        ? CandidateOwner->FindFunctionByName(Expression.FunctionName) : nullptr;
    const bool bWorldContextStatic = bSelfCall && CandidateFunction != nullptr &&
        CandidateFunction->HasAnyFunctionFlags(FUNC_Native) &&
        CandidateFunction->HasAnyFunctionFlags(FUNC_Static) &&
        CandidateFunction->HasMetaData(TEXT("WorldContext"));
    FBPReflectedCall ResolvedCall;
    TArray<FString> ResolveDiagnostics;
    const bool bResolved = TargetClass != nullptr &&
        FBPReflectedCallResolver::ResolveForTarget(
            Expression.FunctionOwnerPath, Expression.FunctionName,
            TargetClass->GetPathName(), bHasTarget == false || bWorldContextStatic,
            ResolvedCall, ResolveDiagnostics);
    const UClass* OwnerClass = ResolvedCall.Owner;
    const UFunction* ReflectedFunction = ResolvedCall.Function;
    if (bResolved && ReflectedFunction != nullptr &&
        ReflectedFunction->GetReturnProperty() == nullptr &&
        Expression.Type.Kind == EBPSemanticTypeKind::Void &&
        IsIdentifier(Expression.FunctionName.ToString()))
    {
        FString Arguments;
        const bool bStatic = ReflectedFunction->HasAnyFunctionFlags(FUNC_Static);
        // 静的関数は通常すべて引数として扱う。インスタンス関数のみ
        // 先頭のTarget参照をC++引数から除外する。
        const int32 FirstArgument = bStatic ? 0 : 1;
        if (bWorldContextStatic)
        {
            const FString ContextName = ReflectedFunction->GetMetaData(TEXT("WorldContext"));
            const UClass* ContextClass = ResolvedCall.Signature.ParameterTypes.IsEmpty() == false
                ? FindObject<UClass>(nullptr,
                    *ResolvedCall.Signature.ParameterTypes[0].TypePath) : nullptr;
            if (ResolvedCall.Signature.ParameterNames.IsEmpty() ||
                ResolvedCall.Signature.ParameterNames[0].ToString() != ContextName ||
                ResolvedCall.Signature.ParameterTypes[0].Kind != EBPSemanticTypeKind::Object ||
                ResolvedCall.Signature.ParameterTypes[0].bIsReference ||
                ResolvedCall.Signature.OutputParameters[0] ||
                ContextClass == nullptr || ParentClass == nullptr ||
                ParentClass->IsChildOf(ContextClass) == false ||
                Expression.Arguments[0].Type.Kind != EBPSemanticTypeKind::Object ||
                Expression.Arguments[0].Type.TypePath != ParentClassPath ||
                Expression.Arguments[0].Type.bIsReference ||
                Expression.Arguments[0].Type.bIsConst)
            {
                Diagnostics.Add(TEXT("Invalid reflected WorldContext parameter"));
                return false;
            }
        }
        const int32 ProvidedParameterCount = Expression.Arguments.Num() - FirstArgument;
        const int32 ExpectedParameterCount = ResolvedCall.Signature.ParameterNames.Num();
        if (ProvidedParameterCount < 0 || ProvidedParameterCount > ExpectedParameterCount ||
            ResolvedCall.Signature.ParameterTypes.Num() != ExpectedParameterCount ||
            ResolvedCall.Signature.OutputParameters.Num() != ExpectedParameterCount ||
            ResolvedCall.Signature.bHasParameterDefaultValues.Num() != ExpectedParameterCount ||
            ResolvedCall.Signature.ParameterDefaultValues.Num() != ExpectedParameterCount)
        {
            Diagnostics.Add(TEXT("Reflected function parameter count does not match semantic call"));
            return false;
        }
        for (int32 Index = FirstArgument; Index < Expression.Arguments.Num(); ++Index)
        {
            const FBPSemanticExpression& Argument = Expression.Arguments[Index];
            const int32 ParameterIndex = Index - FirstArgument;
            const FBPSemanticType& ExpectedType =
                ResolvedCall.Signature.ParameterTypes[ParameterIndex];
            const bool bSupportedConstStringReference =
                ExpectedType.Kind == EBPSemanticTypeKind::String &&
                ExpectedType.bIsReference && ExpectedType.bIsConst;
            if (ResolvedCall.Signature.OutputParameters[ParameterIndex] ||
                ExpectedType.Kind != Argument.Type.Kind ||
                (ExpectedType.bIsReference && bSupportedConstStringReference == false))
            {
                Diagnostics.Add(TEXT("Reflected parameter type or direction does not match semantic call"));
                return false;
            }
            if (bWorldContextStatic && ParameterIndex == 0)
            {
                // メタデータとIRの型が一致したselfのみをC++のthisとして出力する。
                Arguments += TEXT("this");
                continue;
            }
            if (Argument.Type.bIsReference || Argument.Type.bIsConst)
            {
                Diagnostics.Add(TEXT("Reflected call argument has unsupported reference qualifiers"));
                return false;
            }
            // ReflectionでFString入力と確認された引数は、関数名に依存しない
            // 型付き文字列式Emitterへ渡す。リテラル、検証済みメンバー、
            // Concat、Conv_IntToStringを同じ規則で扱う。
            if (ExpectedType.Kind == EBPSemanticTypeKind::String)
            {
                FString Value;
                if (EmitVerifiedStringExpression(Argument, ParentClassPath, Variables,
                        LocalIndex, LocalElement, 0, Value, Diagnostics,
                        LocalIntegerVariables) == false)
                {
                    Diagnostics.Add(TEXT("Reflected FString argument contains an unsupported expression"));
                    return false;
                }
                if (Index > FirstArgument)
                {
                    Arguments += TEXT(", ");
                }
                Arguments += Value;
                continue;
            }
            // ForEachのローカルIndex/Elementは、ループ構築側で型と名前の
            // 衝突を検証済みの場合だけ参照する。メンバーとは別のスコープで扱い、
            // 未検証の任意識別子をC++式として出力しない。
            if (Argument.Kind == EBPSemanticExpressionKind::VariableReference &&
                (Argument.VariableName == LocalIndex ||
                    Argument.VariableName == LocalElement) &&
                Argument.VariableName.IsNone() == false &&
                (LocalIndex.IsNone() == false || LocalElement.IsNone() == false))
            {
                // ループローカル名の型偽装を拒否し、同名のActorメンバーへ
                // フォールバックさせない。宣言時のint32型と一致させる。
                const bool bScalar = Argument.Type.ContainerKind.IsEmpty() ||
                    Argument.Type.ContainerKind == TEXT("none");
                if (Argument.SourcePinId.IsValid() ||
                    Argument.Type.Kind != EBPSemanticTypeKind::Int32 ||
                    bScalar == false ||
                    Argument.Type.bIsReference || Argument.Type.bIsConst ||
                    IsIdentifier(Argument.VariableName.ToString()) == false)
                {
                    Diagnostics.Add(TEXT("ForEach local argument requires a plain int32 variable"));
                    return false;
                }
                if (Index > FirstArgument)
                {
                    Arguments += TEXT(", ");
                }
                Arguments += Argument.VariableName.ToString();
                continue;
            }
            // 共通Callのプリミティブ引数は公開ネイティブプロパティから参照できる。
            // IRの型だけを信用せず、Reflectionの実際の型・アクセス指定を照合する。
            // floatとdoubleは暗黙変換で取り違えないよう個別に検証する。
            if (Argument.Kind == EBPSemanticExpressionKind::VariableReference &&
                (Argument.Type.Kind == EBPSemanticTypeKind::Bool ||
                 Argument.Type.Kind == EBPSemanticTypeKind::Int32 ||
                 Argument.Type.Kind == EBPSemanticTypeKind::Float ||
                 Argument.Type.Kind == EBPSemanticTypeKind::Double))
            {
                const FProperty* Member = ParentClass != nullptr
                    ? ParentClass->FindPropertyByName(Argument.VariableName) : nullptr;
                const bool bMatchesType =
                    (Argument.Type.Kind == EBPSemanticTypeKind::Bool &&
                        CastField<FBoolProperty>(Member) != nullptr) ||
                    (Argument.Type.Kind == EBPSemanticTypeKind::Int32 &&
                        CastField<FIntProperty>(Member) != nullptr) ||
                    (Argument.Type.Kind == EBPSemanticTypeKind::Float &&
                        CastField<FFloatProperty>(Member) != nullptr) ||
                    (Argument.Type.Kind == EBPSemanticTypeKind::Double &&
                        CastField<FDoubleProperty>(Member) != nullptr);
                if (Member == nullptr || bMatchesType == false ||
                    Member->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic) == false ||
                    IsIdentifier(Argument.VariableName.ToString()) == false ||
                    Argument.VariableName == TEXT("this"))
                {
                    Diagnostics.Add(TEXT("Reflected primitive member is not a matching public native property"));
                    return false;
                }
                if (Index > FirstArgument)
                {
                    Arguments += TEXT(", ");
                }
                Arguments += FString::Printf(TEXT("this->%s"),
                    *Argument.VariableName.ToString());
                continue;
            }
            if (Argument.Kind != EBPSemanticExpressionKind::Literal)
            {
                Diagnostics.Add(TEXT("Reflected call contains an unsupported argument expression"));
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
                    Diagnostics.Add(TEXT("Invalid reflected int32 literal"));
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
                    Diagnostics.Add(TEXT("Invalid reflected floating-point literal"));
                    return false;
                }
                // 検証済み数値を正規化して出力する。入力文字列に直接fを付けると、
                // 指数表記や既存の接尾辞により不正なC++リテラルになり得る。
                if (Argument.Type.Kind == EBPSemanticTypeKind::Float)
                {
                    const float FloatValue = static_cast<float>(Parsed);
                    if (FMath::IsFinite(FloatValue) == false)
                    {
                        Diagnostics.Add(TEXT("Reflected float argument exceeds finite float range"));
                        return false;
                    }
                    Value = FloatLiteral(FloatValue);
                }
                else
                {
                    Value = DoubleLiteral(Parsed);
                }
            }
            else if (Argument.Type.Kind == EBPSemanticTypeKind::Struct &&
                Argument.Type.TypePath == TEXT("/Script/CoreUObject.LinearColor") &&
                ResolvedCall.Signature.ParameterTypes[ParameterIndex].TypePath ==
                    TEXT("/Script/CoreUObject.LinearColor"))
            {
                // 構造体の任意テキストをC++へ直書きせず、LinearColorとして解析して
                // 有限なRGBA値だけを型付きコンストラクタに変換する。
                FLinearColor Color;
                if (Color.InitFromString(Argument.LiteralValue) == false ||
                    FMath::IsFinite(Color.R) == false || FMath::IsFinite(Color.G) == false ||
                    FMath::IsFinite(Color.B) == false || FMath::IsFinite(Color.A) == false)
                {
                    Diagnostics.Add(TEXT("Invalid reflected LinearColor literal"));
                    return false;
                }
                Value = FString::Printf(TEXT("FLinearColor(%s, %s, %s, %s)"),
                    *FloatLiteral(Color.R), *FloatLiteral(Color.G),
                    *FloatLiteral(Color.B), *FloatLiteral(Color.A));
            }
            else if (Argument.Type.Kind == EBPSemanticTypeKind::String)
            {
                // 文字列はReflectionでFString引数と確認した場合のみ生成する。
                // 任意のIR文字列をC++ソースへ直書きせず、制御文字と引用符を検証・エスケープする。
                FString Escaped;
                if (EscapeString(Argument.LiteralValue, Escaped, Diagnostics) == false)
                {
                    return false;
                }
                Value = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
            }
            else if (Argument.Type.Kind == EBPSemanticTypeKind::Name)
            {
                FString Escaped;
                if (EscapeString(Argument.LiteralValue, Escaped, Diagnostics) == false)
                {
                    return false;
                }
                // 既存生成物との互換性を維持しつつ、Reflection上はFNameと照合する。
                Value = FString::Printf(TEXT("TEXT(\"%s\")"), *Escaped);
            }
            else
            {
                Diagnostics.Add(TEXT("Reflected call argument type is unsupported"));
                return false;
            }
            if (Index > FirstArgument)
            {
                Arguments += TEXT(", ");
            }
            Arguments += Value;
        }
        // Semantic IRで省略できるのは末尾の引数だけ。UHTのCPP_Defaultメタデータを
        // Reflectionで確認し、安全に型付きC++へ変換できる場合に限って明示出力する。
        // 生成C++をヘッダー側の暗黙デフォルトや任意文字列へ依存させない。
        for (int32 ParameterIndex = ProvidedParameterCount;
            ParameterIndex < ExpectedParameterCount; ++ParameterIndex)
        {
            const FBPSemanticType& ExpectedType =
                ResolvedCall.Signature.ParameterTypes[ParameterIndex];
            const bool bSupportedConstStringReference =
                ExpectedType.Kind == EBPSemanticTypeKind::String &&
                ExpectedType.bIsReference && ExpectedType.bIsConst;
            if (ResolvedCall.Signature.OutputParameters[ParameterIndex] ||
                (ExpectedType.bIsReference && bSupportedConstStringReference == false) ||
                ResolvedCall.Signature.bHasParameterDefaultValues[ParameterIndex] == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Missing reflected argument has no supported default: %s"),
                    *ResolvedCall.Signature.ParameterNames[ParameterIndex].ToString()));
                return false;
            }
            FString Value;
            if (EmitReflectedLiteralValue(
                    ResolvedCall.Signature.ParameterDefaultValues[ParameterIndex],
                    ExpectedType, true, Value, Diagnostics) == false)
            {
                Diagnostics.Add(FString::Printf(
                    TEXT("Unsupported reflected default for parameter: %s"),
                    *ResolvedCall.Signature.ParameterNames[ParameterIndex].ToString()));
                return false;
            }
            if (ParameterIndex > 0)
            {
                Arguments += TEXT(", ");
            }
            Arguments += Value;
        }
        const FString Target = bStatic
            ? OwnerClass->GetPrefixCPP() + OwnerClass->GetName() + TEXT("::")
            : bSelfCall ? TEXT("this->")
            : TEXT("this->") + Expression.Arguments[0].VariableName.ToString() + TEXT("->");
        OutStatement = FString::Printf(TEXT("%s%s%s(%s);\n"),
            *Indent, *Target, *Expression.FunctionName.ToString(), *Arguments);
        return true;
    }
    // Reflectionの失敗理由を破棄すると、型不一致や未解決クラスが単なる
    // 「未対応の式」に見えてしまう。共通Callの診断を呼び出し元まで伝播する。
    // 既存の専用Emitterは段階的に統合するため、ここでは共通経路のみを対象とする。
    if (bResolved == false)
    {
        Diagnostics.Append(ResolveDiagnostics);
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

bool ExpressionReferencesVariable(const FBPSemanticExpression& Expression,
    const FName VariableName, const int32 Depth = 0)
{
    if (Depth > 32)
    {
        return false;
    }
    if (Expression.Kind == EBPSemanticExpressionKind::VariableReference &&
        Expression.VariableName == VariableName)
    {
        return true;
    }
    return Expression.Arguments.ContainsByPredicate(
        [VariableName, Depth](const FBPSemanticExpression& Argument)
        {
            return ExpressionReferencesVariable(Argument, VariableName, Depth + 1);
        });
}

// ForLoopのBranchを再帰的に生成する。対応する葉は共通の式文Emitterへ渡し、
// 深さ制限で異常なSemantic IRによる過度な再帰を防ぐ。
bool EmitForLoopBodyStatement(const FBPSemanticStatement& Statement,
    const FString& ParentClassPath, const TArray<FBPSemanticVariable>& Variables,
    const TArray<FName>& LocalIndices, const FString& Indent, int32 Depth,
    FString& OutCode, TArray<FString>& Diagnostics)
{
    if (Depth > 16)
    {
        Diagnostics.Add(TEXT("ForLoop Branch nesting exceeds the supported depth"));
        return false;
    }
    if (Statement.Kind == EBPSemanticStatementKind::Expression)
    {
        const FName LocalIndex = LocalIndices.IsEmpty()
            ? NAME_None : LocalIndices.Last();
        return EmitExpressionStatement(Statement.Expression, ParentClassPath,
            Indent, OutCode, Diagnostics, LocalIndex, NAME_None, &Variables,
            &LocalIndices);
    }
    if (Statement.Kind == EBPSemanticStatementKind::ForLoop)
    {
        // 内側ループは独立したIndex名を必須とし、外側Indexのシャドーイングを防ぐ。
        // 現段階ではループ境界をint32リテラルに限定する。
        int32 First = 0;
        int32 Last = 0;
        if (Statement.LoopIndexVariable.IsNone() ||
            IsIdentifier(Statement.LoopIndexVariable.ToString()) == false ||
            LocalIndices.Contains(Statement.LoopIndexVariable) ||
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
        TArray<FName> NestedLocalIndices = LocalIndices;
        NestedLocalIndices.Add(Statement.LoopIndexVariable);
        for (const FBPSemanticStatement& Body : Statement.LoopBodyStatements)
        {
            FString Part;
            if (EmitForLoopBodyStatement(Body, ParentClassPath, Variables,
                    NestedLocalIndices, InnerIndent, Depth + 1,
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
                    LocalIndices, Indent, Depth + 1, Part, Diagnostics) == false)
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
    const FName LocalIndex = LocalIndices.IsEmpty()
        ? NAME_None : LocalIndices.Last();
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
                LocalIndices, Indent + TEXT("    "), Depth + 1, Part, Diagnostics) == false)
        {
            return false;
        }
        ThenCode += Part;
    }
    for (const FBPSemanticStatement& Child : Statement.ElseStatements)
    {
        FString Part;
        if (EmitForLoopBodyStatement(Child, ParentClassPath, Variables,
                LocalIndices, Indent + TEXT("    "), Depth + 1, Part, Diagnostics) == false)
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

bool EmitForEachStatement(const FBPSemanticStatement& Statement,
    const FString& ParentClassPath, const TArray<FBPSemanticVariable>& Variables,
    const FString& Indent, FString& OutCode, TArray<FString>& Diagnostics)
{
    OutCode.Reset();
    const FBPSemanticExpression& Array = Statement.LoopArray;
    const FBPSemanticVariable* SourceVariable = Variables.FindByPredicate(
        [&Array](const FBPSemanticVariable& Variable)
        {
            return Variable.Name == Array.VariableName;
        });
    const UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
    if (Statement.Kind != EBPSemanticStatementKind::ForEachLoop ||
        Array.Kind != EBPSemanticExpressionKind::VariableReference ||
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
        Variables.ContainsByPredicate(
            [&Statement](const FBPSemanticVariable& Variable)
            {
                return Variable.Name == Statement.LoopIndexVariable ||
                    Variable.Name == Statement.LoopElementVariable;
            }) ||
        (ParentClass != nullptr &&
            (ParentClass->FindPropertyByName(Statement.LoopIndexVariable) != nullptr ||
             ParentClass->FindPropertyByName(Statement.LoopElementVariable) != nullptr)) ||
        Statement.LoopBodyStatements.IsEmpty())
    {
        Diagnostics.Add(TEXT("ForEachLoop requires a verified int32 array member and distinct element/index identifiers"));
        return false;
    }

    const FString BodyIndent = Indent + TEXT("    ");
    FString BodyCode;
    bool bUsesElement = false;
    for (const FBPSemanticStatement& Body : Statement.LoopBodyStatements)
    {
        FString Part;
        if (Body.Kind == EBPSemanticStatementKind::Break)
        {
            BodyCode += BodyIndent + TEXT("break;\n");
            continue;
        }
        if (Body.Kind != EBPSemanticStatementKind::Expression ||
            EmitExpressionStatement(Body.Expression, ParentClassPath,
                BodyIndent, Part, Diagnostics, Statement.LoopIndexVariable,
                Statement.LoopElementVariable, &Variables) == false)
        {
            Diagnostics.Add(TEXT("ForEachLoop body contains an unsupported semantic statement"));
            return false;
        }
        bUsesElement = bUsesElement || ExpressionReferencesVariable(
            Body.Expression, Statement.LoopElementVariable);
        BodyCode += Part;
    }

    FString CompletedCode;
    for (const FBPSemanticStatement& Completed : Statement.CompletedStatements)
    {
        FString Part;
        if (Completed.Kind != EBPSemanticStatementKind::Expression ||
            EmitExpressionStatement(Completed.Expression, ParentClassPath,
                Indent, Part, Diagnostics, NAME_None, NAME_None,
                &Variables) == false)
        {
            Diagnostics.Add(TEXT("ForEachLoop Completed contains an unsupported semantic statement"));
            return false;
        }
        CompletedCode += Part;
    }

    const FString ElementDeclaration = bUsesElement
        ? FString::Printf(TEXT("%sconst int32 %s = %s[%s];\n"),
            *BodyIndent, *Statement.LoopElementVariable.ToString(),
            *Array.VariableName.ToString(), *Statement.LoopIndexVariable.ToString())
        : FString();
    OutCode = FString::Printf(
        TEXT("%sfor (int32 %s = 0; %s < %s.Num(); ++%s)\n%s{\n%s%s%s}\n%s"),
        *Indent, *Statement.LoopIndexVariable.ToString(),
        *Statement.LoopIndexVariable.ToString(), *Array.VariableName.ToString(),
        *Statement.LoopIndexVariable.ToString(), *Indent,
        *ElementDeclaration, *BodyCode, *Indent, *CompletedCode);
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
    if (IR.Functions.IsEmpty())
    {
        OutDiagnostics.Add(TEXT("Semantic code generation requires at least one function"));
        return false;
    }
    if (IR.Functions.Num() > 1)
    {
        FString VariableDeclarations;
        FString ConstructorBody;
        if (EmitVariables(IR.Variables, ClassName, VariableDeclarations,
                ConstructorBody, OutDiagnostics, VariableFallbackMode) == false)
        {
            return false;
        }

        TSet<FName> FunctionNames;
        TSet<FString> GeneratedDefinitionNames;
        FString FunctionDeclarations;
        FString FunctionDefinitions;
        FString FunctionIncludes;
        TFunction<void(const FBPSemanticStatement&)> CollectStatementIncludes;
        CollectStatementIncludes = [&](const FBPSemanticStatement& Statement)
        {
            if (Statement.Kind == EBPSemanticStatementKind::Expression ||
                Statement.Kind == EBPSemanticStatementKind::Branch ||
                Statement.Kind == EBPSemanticStatementKind::Return)
            {
                AppendExpressionIncludes(Statement.Expression, FunctionIncludes);
            }
            for (const FBPSemanticStatement& Child : Statement.ThenStatements)
            {
                CollectStatementIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Statement.ElseStatements)
            {
                CollectStatementIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Statement.LoopBodyStatements)
            {
                CollectStatementIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Statement.CompletedStatements)
            {
                CollectStatementIncludes(Child);
            }
        };

        for (const FBPSemanticFunction& CurrentFunction : IR.Functions)
        {
            if (CurrentFunction.Name.IsNone() ||
                CurrentFunction.Name.ToString() == ClassName ||
                FunctionNames.Contains(CurrentFunction.Name))
            {
                OutDiagnostics.Add(FString::Printf(
                    TEXT("Duplicate, empty, or class-conflicting semantic function name: %s"),
                    *CurrentFunction.Name.ToString()));
                return false;
            }
            FunctionNames.Add(CurrentFunction.Name);
            TArray<FBPSemanticStatement> CurrentStatements;
            if (BuildLinearFunctionStatements(CurrentFunction,
                    CurrentStatements, OutDiagnostics) == false)
            {
                return false;
            }

            FBPGeneratedFunctionShape CurrentShape;
            if (BuildFunctionShape(CurrentFunction, ParentClass, ClassName,
                    CurrentShape, OutDiagnostics) == false)
            {
                return false;
            }
            const FString DefinitionPrefix = ClassName + TEXT("::");
            const int32 DefinitionPrefixIndex = CurrentShape.Definition.Find(
                DefinitionPrefix, ESearchCase::CaseSensitive);
            const int32 DefinitionNameStart = DefinitionPrefixIndex == INDEX_NONE
                ? INDEX_NONE : DefinitionPrefixIndex + DefinitionPrefix.Len();
            const int32 DefinitionNameEnd = DefinitionNameStart == INDEX_NONE
                ? INDEX_NONE : CurrentShape.Definition.Find(TEXT("("),
                    ESearchCase::CaseSensitive, ESearchDir::FromStart,
                    DefinitionNameStart);
            if (DefinitionNameStart == INDEX_NONE ||
                DefinitionNameEnd <= DefinitionNameStart)
            {
                OutDiagnostics.Add(FString::Printf(
                    TEXT("Generated C++ function definition could not be validated: %s"),
                    *CurrentFunction.Name.ToString()));
                return false;
            }
            const FString GeneratedDefinitionName = CurrentShape.Definition.Mid(
                DefinitionNameStart, DefinitionNameEnd - DefinitionNameStart);
            if (GeneratedDefinitionNames.Contains(GeneratedDefinitionName))
            {
                OutDiagnostics.Add(FString::Printf(
                    TEXT("Generated C++ function definition name collision: %s"),
                    *GeneratedDefinitionName));
                return false;
            }
            GeneratedDefinitionNames.Add(GeneratedDefinitionName);
            FString FunctionBody;
            for (const FBPSemanticStatement& CurrentStatement : CurrentStatements)
            {
                FString StatementCode;
                if (CurrentStatement.Kind == EBPSemanticStatementKind::Expression)
                {
                    if (EmitExpressionStatement(CurrentStatement.Expression,
                            IR.ParentClassPath, TEXT("    "), StatementCode,
                            OutDiagnostics, NAME_None, NAME_None,
                            &IR.Variables) == false)
                    {
                        return false;
                    }
                }
                else if (CurrentStatement.Kind == EBPSemanticStatementKind::ForEachLoop)
                {
                    if (EmitForEachStatement(CurrentStatement,
                            IR.ParentClassPath, IR.Variables, TEXT("    "),
                            StatementCode, OutDiagnostics) == false)
                    {
                        return false;
                    }
                }
                else if (CurrentStatement.Kind == EBPSemanticStatementKind::Branch ||
                    CurrentStatement.Kind == EBPSemanticStatementKind::ForLoop)
                {
                    if (EmitForLoopBodyStatement(CurrentStatement,
                            IR.ParentClassPath, IR.Variables, TArray<FName>(),
                            TEXT("    "), 0, StatementCode, OutDiagnostics) == false)
                    {
                        return false;
                    }
                }
                else if (CurrentStatement.Kind == EBPSemanticStatementKind::Return)
                {
                    if (EmitReturnStatement(CurrentFunction, CurrentStatement,
                            IR.ParentClassPath, TEXT("    "), StatementCode,
                            OutDiagnostics) == false)
                    {
                        return false;
                    }
                }
                else
                {
                    OutDiagnostics.Add(FString::Printf(
                        TEXT("Multiple-function generation does not yet support statement kind in %s"),
                        *CurrentFunction.Name.ToString()));
                    return false;
                }
                CollectStatementIncludes(CurrentStatement);
                FunctionBody += StatementCode;
            }
            FunctionDeclarations += CurrentShape.Declaration;
            FunctionDeclarations += TEXT("\n");
            FunctionDefinitions +=
                FBPFunctionDefinitionEmitter::RenderDefinition(
                    CurrentShape, FunctionBody);
        }

        const FString ApiPrefix = ModuleApiMacro.IsEmpty()
            ? FString() : ModuleApiMacro + TEXT(" ");
        const FString ConstructorDeclaration = ConstructorBody.IsEmpty()
            ? FString()
            : FString::Printf(TEXT("public:\n    %s();\n\n"), *ClassName);
        const FString UnrealTypeInclude = ConstructorBody.IsEmpty()
            ? FString() : TEXT("#include \"UObject/UnrealType.h\"\n");
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
            *ParentClass.CppClassName, *VariableDeclarations,
            *ConstructorDeclaration, *FunctionDeclarations);
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n%s%s%s%s"),
            *BaseName, *FunctionIncludes, *UnrealTypeInclude,
            *ConstructorDefinition, *FunctionDefinitions);
        OutFiles = MoveTemp(Files);
        return true;
    }
    const FBPSemanticFunction& Function = IR.Functions[0];
    TArray<FBPSemanticStatement> Statements;
    if (BuildLinearFunctionStatements(Function, Statements, OutDiagnostics) == false)
    {
        return false;
    }
    FBPGeneratedFunctionShape FunctionShape;
    if (BuildFunctionShape(Function, ParentClass, ClassName,
        FunctionShape, OutDiagnostics) == false)
    {
        return false;
    }
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
                    IR.Variables, TArray<FName>(), TEXT("    "), 0, Part,
                    OutDiagnostics) == false)
            {
                return false;
            }
        }
        else if (LeadingStatement.Kind != EBPSemanticStatementKind::Expression ||
            EmitExpressionStatement(LeadingStatement.Expression, IR.ParentClassPath,
                TEXT("    "), Part, OutDiagnostics, NAME_None, NAME_None,
                &IR.Variables) == false)
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
    if (Statement.Kind == EBPSemanticStatementKind::Return)
    {
        FString ReturnCode;
        if (EmitReturnStatement(Function, Statement, IR.ParentClassPath,
                TEXT("    "), ReturnCode, OutDiagnostics) == false)
        {
            return false;
        }
        FString ReturnIncludes;
        AppendExpressionIncludes(Statement.Expression, ReturnIncludes);
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
            ? FString() : TEXT("#include \"UObject/UnrealType.h\"\n");
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
            *ParentClass.CppClassName, *VariableDeclarations,
            *ConstructorDeclaration, *FunctionShape.Declaration);
        const FString Body = LeadingCode + ReturnCode;
        Files.SourceContent = FString::Printf(
            TEXT("#include \"%s.h\"\n%s%s%s%s"),
            *BaseName, *ReturnIncludes, *LeadingIncludes,
            *UnrealTypeInclude, *ConstructorDefinition);
        Files.SourceContent += FBPFunctionDefinitionEmitter::RenderDefinition(
            FunctionShape, Body);
        OutFiles = MoveTemp(Files);
        return true;
    }
    if (Statement.Kind == EBPSemanticStatementKind::ForEachLoop)
    {
        FString ForEachCode;
        if (EmitForEachStatement(Statement, IR.ParentClassPath, IR.Variables,
                TEXT("    "), ForEachCode, OutDiagnostics) == false)
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
        const FString Body = LeadingCode + ForEachCode;
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
                    IR.Variables, TArray<FName>{Statement.LoopIndexVariable}, TEXT("        "),
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
                    IR.Variables, TArray<FName>(), TEXT("    "),
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
        FString LoopIncludes =
            TEXT("#include \"Kismet/KismetMathLibrary.h\"\n")
            TEXT("#include \"Kismet/KismetStringLibrary.h\"\n")
            TEXT("#include \"Kismet/KismetSystemLibrary.h\"\n");
        TFunction<void(const FBPSemanticStatement&)> CollectLoopIncludes;
        CollectLoopIncludes = [&](const FBPSemanticStatement& Current)
        {
            if (Current.Kind == EBPSemanticStatementKind::Expression)
            {
                AppendExpressionIncludes(Current.Expression, LoopIncludes);
            }
            for (const FBPSemanticStatement& Child : Current.ThenStatements)
            {
                CollectLoopIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Current.ElseStatements)
            {
                CollectLoopIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Current.LoopBodyStatements)
            {
                CollectLoopIncludes(Child);
            }
            for (const FBPSemanticStatement& Child : Current.CompletedStatements)
            {
                CollectLoopIncludes(Child);
            }
        };
        CollectLoopIncludes(Statement);
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
            TEXT("#include \"%s.h\"\n%s%s%s%s"),
            *BaseName, *LoopIncludes, *LeadingIncludes,
            *UnrealTypeInclude, *ConstructorDefinition);
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
                TArray<FName>(), TEXT("    "), 0, BranchCode, OutDiagnostics) == false)
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
            ExpressionStatementCode, OutDiagnostics,
            NAME_None, NAME_None, &IR.Variables) == false)
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
