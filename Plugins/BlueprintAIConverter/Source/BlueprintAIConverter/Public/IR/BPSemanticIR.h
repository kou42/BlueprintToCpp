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
    Struct,
    Enum,
    Byte,
    Delegate,
    MulticastDelegate
};

struct FBPSemanticType
{
    EBPSemanticTypeKind Kind = EBPSemanticTypeKind::Unresolved;
    // Object/Structなどの具体的な型をリフレクションで解決した際に設定する。
    FString TypePath;
    // コンテナの要素型は未対応の場合でも型名を失わないよう記録する。
    FString ContainerKind = TEXT("none");
    FString InnerType;
    FString KeyType;
    FString ValueType;
    // 再帰的な型ツリー。子要素が存在しない場合はnullptrを使用する。
    TSharedPtr<FBPSemanticType> ElementType;
    TSharedPtr<FBPSemanticType> KeyTypeInfo;
    TSharedPtr<FBPSemanticType> ValueTypeInfo;
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
    Return,
    Branch,
    ForLoop,
    // CJ2: 配列反復を数値範囲ループと区別する。未解決の配列は生成しない。
    ForEachLoop,
    // Break付きループ内でのみ有効な明示的な制御フロー終端。
    Break
};

struct FBPSemanticStatement
{
    EBPSemanticStatementKind Kind = EBPSemanticStatementKind::Expression;
    FGuid SourceNodeId;
    FBPSemanticExpression Expression;
    FName AssignmentTarget;
    // BranchではExpressionをbool条件として使用し、各枝の文を順序付きで保持する。
    TArray<FBPSemanticStatement> ThenStatements;
    TArray<FBPSemanticStatement> ElseStatements;
    // ForLoopはFirstIndexからLastIndexまで両端を含めて反復する。
    FName LoopIndexVariable;
    FBPSemanticExpression LoopFirstIndex;
    FBPSemanticExpression LoopLastIndex;
    TArray<FBPSemanticStatement> LoopBodyStatements;
    TArray<FBPSemanticStatement> CompletedStatements;
    // ForEachLoop: 配列入力と現在要素の型が確定した場合のみ使用する。
    FBPSemanticExpression LoopArray;
    FName LoopElementVariable;
    FBPSemanticType LoopElementType;
};

// BasicBlockの末尾から次に進む制御フローを明示する。
// Unspecifiedは既存の単一Block IRとの互換用で、複数Blockでは受理しない。
enum class EBPSemanticBlockTerminatorKind : uint8
{
    Unspecified,
    End,
    Fallthrough,
    Return,
    Branch,
    Loop
};

struct FBPSemanticBasicBlock
{
    // SourceEntryNodeIdは由来追跡、BlockIdはSemantic CFG内の参照先として使う。
    // Branch終端はTrue、False、Loop終端はBody、Completedの順で後続を保持する。
    // Loop Body BlockはFallthroughでLoopヘッダーへ戻るback-edgeを持つ。
    FGuid BlockId;
    FGuid SourceEntryNodeId;
    EBPSemanticBlockTerminatorKind Terminator =
        EBPSemanticBlockTerminatorKind::Unspecified;
    TArray<FGuid> SuccessorBlockIds;
    TArray<FBPSemanticStatement> Statements;
};

// UFunctionの引数と戻り値の型・方向・所有クラスを明示する。
struct FBPSemanticParameter
{
    FName Name;
    FBPSemanticType Type;
    // UFunctionにおけるCPF_Parmの列挙順を明示する（戻り値は除外）。
    int32 DeclarationIndex = INDEX_NONE;
    bool bIsOut = false;
    bool bIsReturn = false;
    // メタデータが存在しない場合と空文字の既定値を区別する。
    bool bHasDefaultValue = false;
    // CJ1: 既定値が存在しない場合と、取得方法が未対応の場合を区別する。
    // specified / absent / unavailable のいずれかを保持する。
    FString DefaultValueStatus = TEXT("unavailable");
    FString DefaultValue;
    // 既定値の出典と元のGraph GUIDを記録する。
    FString DefaultValueSource = TEXT("unavailable");
    FGuid DefaultValueSourceNodeId;
    FGuid DefaultValueSourcePinId;
};

enum class EBPSemanticFunctionVisibility : uint8
{
    Unspecified,
    Public,
    Protected,
    Private
};

struct FBPSemanticFunction
{
    FName Name;
    FBPSemanticType ReturnType;
    FString OwnerClassPath;
    FString SignatureSource;
    // Unspecified preserves legacy IR; reflected functions explicitly record access.
    EBPSemanticFunctionVisibility Visibility = EBPSemanticFunctionVisibility::Unspecified;
    // リフレクションから得た属性。NativeであってもBP Override可能なため単一の実装区分に集約しない。
    bool bIsNative = false;
    bool bIsStatic = false;
    bool bIsConst = false;
    bool bIsBlueprintEvent = false;
    // Reflection上のBlueprintEvent属性とは別に、実行本体がEvent Graph由来かを保持する。
    bool bIsEventGraph = false;
    bool bIsOverride = false;
    FString OverriddenFunctionPath;
    bool bIsInterfaceImplementation = false;
    FString InterfaceClassPath;
    FString InterfaceFunctionPath;
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

// Blueprint生成クラスとネイティブUCLASSを共通形式で識別する。
// 未解析の情報は空文字のまま保持し、推測でC++宣言を生成しない。
enum class EBPSemanticClassOrigin : uint8
{
    Unknown,
    Blueprint,
    Native
};

// UClass自身が宣言したメンバーだけを記録する。関数本体や既定値は含めない。
struct FBPSemanticClassMember
{
    FString Name;
    FString CppType;
    FString ReflectionPath;
    FString Flags;
};

// UFunctionの宣言署名。C++の関数本体を復元できるという意味ではない。
struct FBPSemanticReflectedParameter
{
    FString Name;
    FString CppType;
    bool bIsOut = false;
    bool bIsReference = false;
    bool bIsConst = false;
};

struct FBPSemanticReflectedFunction
{
    FString Name;
    FString ReflectionPath;
    FString Flags;
    FString ReturnCppType = TEXT("void");
    // UFunction flags are not proof that the original C++ method is virtual.
    bool bIsNative = false;
    bool bIsStatic = false;
    bool bIsConst = false;
    bool bIsBlueprintEvent = false;
    bool bIsNet = false;
    bool bIsFinal = false;
    TArray<FBPSemanticReflectedParameter> Parameters;
};

struct FBPSemanticClassInfo
{
    EBPSemanticClassOrigin Origin = EBPSemanticClassOrigin::Unknown;
    FString ClassPath;
    FString ClassName;
    FString NativeParentClassPath;
    TArray<FString> InterfacePaths;
    TArray<FBPSemanticClassMember> DeclaredProperties;
    TArray<FBPSemanticClassMember> DeclaredFunctions;
    TArray<FBPSemanticReflectedFunction> DeclaredFunctionSignatures;
};

struct FBPSemanticIR
{
    FString AssetPath;
    FString ParentClassPath;
    FBPSemanticClassInfo ClassInfo;
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
