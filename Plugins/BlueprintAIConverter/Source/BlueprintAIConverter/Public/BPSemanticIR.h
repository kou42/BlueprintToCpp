#pragma once

#include "CoreMinimal.h"
#include "BPGraphIR.h"

// Graph IRの接続情報を失わず、型解決後の意味情報を保持するための中間表現。
// 未解決の型やノードを推測でC++へ出力しないよう、診断を明示的に持つ。
enum class EBPSemanticTypeKind : uint8
{
    Unresolved,
    Void,
    Bool,
    Int32,
    Int64,
    Float,
    Double,
    String,
    Name,
    Text,
    Object,
    Struct
};

struct FBPSemanticType
{
    EBPSemanticTypeKind Kind = EBPSemanticTypeKind::Unresolved;
    // Object/Structなどの具体的な型をリフレクションで解決した際に設定する。
    FString TypePath;
    bool bIsReference = false;
    bool bIsConst = false;
};

enum class EBPSemanticExpressionKind : uint8
{
    Literal,
    VariableReference,
    FunctionCall
};

struct FBPSemanticExpression
{
    EBPSemanticExpressionKind Kind = EBPSemanticExpressionKind::Literal;
    FBPSemanticType Type;
    FGuid SourceNodeId;
    FGuid SourcePinId;
    FString LiteralValue;
    FName VariableName;
    FName FunctionName;
    FString FunctionOwnerPath;
    // 式ツリーの所有関係を値で保持し、UnrealのGC対象外で安全に利用する。
    TArray<FBPSemanticExpression> Arguments;
};

enum class EBPSemanticStatementKind : uint8
{
    Expression,
    Assignment,
    Return
};

struct FBPSemanticStatement
{
    EBPSemanticStatementKind Kind = EBPSemanticStatementKind::Expression;
    FGuid SourceNodeId;
    FBPSemanticExpression Expression;
    FName AssignmentTarget;
};

struct FBPSemanticBasicBlock
{
    FGuid SourceEntryNodeId;
    TArray<FBPSemanticStatement> Statements;
};

// UFunctionの引数と戻り値の型・方向・所有クラスを明示する。
struct FBPSemanticParameter
{
    FName Name;
    FBPSemanticType Type;
    bool bIsOut = false;
    bool bIsReturn = false;
};

struct FBPSemanticFunction
{
    FName Name;
    FBPSemanticType ReturnType;
    FString OwnerClassPath;
    FString SignatureSource;
    TArray<FBPSemanticParameter> Parameters;
    TArray<FBPSemanticBasicBlock> Blocks;
};

struct FBPSemanticDiagnostic
{
    FString GraphName;
    FGuid NodeId;
    FGuid PinId;
    FString Message;
};

// Blueprint宣言の情報を保持する。CDOの値は未取得でも空文字と混同しない。
struct FBPSemanticVariable
{
    FName Name;
    FGuid Guid;
    FBPSemanticType Type;
    FString PinCategory;
    FString TypePath;
    FString PropertyFlags;
    FString Source = TEXT("blueprint_new_variables");
    FString DefaultValueSource = TEXT("unavailable");
    bool bDefaultValueAvailable = false;
    FString DefaultValue;
};

struct FBPSemanticIR
{
    FString AssetPath;
    FString ParentClassPath;
    TArray<FBPSemanticFunction> Functions;
    TArray<FBPSemanticVariable> Variables;
    TArray<FBPSemanticDiagnostic> Diagnostics;

    bool HasErrors() const
    {
        return Diagnostics.IsEmpty() == false;
    }
};
