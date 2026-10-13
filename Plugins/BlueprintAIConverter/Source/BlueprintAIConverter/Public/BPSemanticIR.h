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

enum class EBPSemanticDiagnosticSeverity : uint8
{
    Warning,
    Error
};

struct FBPSemanticDiagnostic
{
    EBPSemanticDiagnosticSeverity Severity = EBPSemanticDiagnosticSeverity::Error;
    FString GraphName;
    FGuid NodeId;
    FGuid PinId;
    FString Message;
};

// 変数の型そのものではなく、既定値をどの安全な方式で生成するかを表す。
// ImportTextは候補として保持するが、往復検証済みPropertyに限定して有効化する。
enum class EBPVariableInitializationStrategy : uint8
{
    Unsupported,
    DirectLiteral,
    StringLiteral,
    NameLiteral,
    TextLiteral,
    ImportText
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
    // GeneratedClassのFPropertyから得た宣言型と標準ExportText表現。
    // PinCategoryからC++型名を推測せず、今後の再帰Property IRの基礎にする。
    FString ReflectedCppType;
    FString ReflectedPropertyClass;
    bool bExportedDefaultValueAvailable = false;
    FString ExportedDefaultValue;
    bool bExportTextRoundTripVerified = false;
    EBPVariableInitializationStrategy InitializationStrategy =
        EBPVariableInitializationStrategy::Unsupported;
    // FTextの表示文字列だけでローカライズ識別情報を失わないために保持する。
    bool bTextCultureInvariant = false;
    bool bTextLocalizationIdentityAvailable = false;
    FString TextNamespace;
    FString TextKey;
    FString TextSource;
};

// Graph IRの元GUIDを保持する追跡情報。意味解析結果とは独立して記録する。
struct FBPSemanticGraphTrace
{
    FString Name;
    FString Path;
    FString Kind;
    TArray<FGuid> NodeIds;
    TArray<FGuid> PinIds;
    TArray<FBPIRLink> ExecEdges;
    TArray<FBPIRLink> DataEdges;
};

struct FBPSemanticIR
{
    FString AssetPath;
    FString ParentClassPath;
    TArray<FBPSemanticFunction> Functions;
    TArray<FBPSemanticVariable> Variables;
    TArray<FBPSemanticGraphTrace> GraphTraces;
    TArray<FBPSemanticDiagnostic> Diagnostics;

    bool HasErrors() const
    {
        return Diagnostics.ContainsByPredicate(
            [](const FBPSemanticDiagnostic& Diagnostic)
            {
                return Diagnostic.Severity == EBPSemanticDiagnosticSeverity::Error;
            });
    }
};
