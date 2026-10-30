#include "BPSemanticJsonSerializer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString TypeKindName(EBPSemanticTypeKind Kind)
{
    switch (Kind)
    {
    case EBPSemanticTypeKind::Unresolved: return TEXT("unresolved");
    case EBPSemanticTypeKind::Void: return TEXT("void");
    case EBPSemanticTypeKind::Bool: return TEXT("bool");
    case EBPSemanticTypeKind::Int32: return TEXT("int32");
    case EBPSemanticTypeKind::Int64: return TEXT("int64");
    case EBPSemanticTypeKind::Float: return TEXT("float");
    case EBPSemanticTypeKind::Double: return TEXT("double");
    case EBPSemanticTypeKind::String: return TEXT("string");
    case EBPSemanticTypeKind::Name: return TEXT("name");
    case EBPSemanticTypeKind::Text: return TEXT("text");
    case EBPSemanticTypeKind::Object: return TEXT("object");
    case EBPSemanticTypeKind::Struct: return TEXT("struct");
    case EBPSemanticTypeKind::Enum: return TEXT("enum");
    case EBPSemanticTypeKind::Byte: return TEXT("byte");
    case EBPSemanticTypeKind::Delegate: return TEXT("delegate");
    case EBPSemanticTypeKind::MulticastDelegate: return TEXT("multicast_delegate");
    }
    return TEXT("unresolved");
}

FString ExpressionKindName(EBPSemanticExpressionKind Kind)
{
    switch (Kind)
    {
    case EBPSemanticExpressionKind::Literal: return TEXT("literal");
    case EBPSemanticExpressionKind::VariableReference: return TEXT("variable_reference");
    case EBPSemanticExpressionKind::FunctionCall: return TEXT("function_call");
    }
    return TEXT("literal");
}

FString StatementKindName(EBPSemanticStatementKind Kind)
{
    switch (Kind)
    {
    case EBPSemanticStatementKind::Expression: return TEXT("expression");
    case EBPSemanticStatementKind::Assignment: return TEXT("assignment");
    case EBPSemanticStatementKind::Return: return TEXT("return");
    case EBPSemanticStatementKind::Branch: return TEXT("branch");
    case EBPSemanticStatementKind::ForLoop: return TEXT("for_loop");
    case EBPSemanticStatementKind::ForEachLoop: return TEXT("for_each_loop");
    case EBPSemanticStatementKind::Break: return TEXT("break");
    }
    return TEXT("expression");
}

FString BlockTerminatorKindName(EBPSemanticBlockTerminatorKind Kind)
{
    switch (Kind)
    {
    case EBPSemanticBlockTerminatorKind::Unspecified: return TEXT("unspecified");
    case EBPSemanticBlockTerminatorKind::End: return TEXT("end");
    case EBPSemanticBlockTerminatorKind::Fallthrough: return TEXT("fallthrough");
    case EBPSemanticBlockTerminatorKind::Return: return TEXT("return");
    case EBPSemanticBlockTerminatorKind::Branch: return TEXT("branch");
    case EBPSemanticBlockTerminatorKind::Loop: return TEXT("loop");
    }
    return TEXT("unspecified");
}

// 無効GUIDをゼロGUIDとして偽装せず、出典が存在しないことをnullで示す。
TSharedPtr<FJsonValue> GuidValue(const FGuid& Id)
{
    if (Id.IsValid() == false)
    {
        return MakeShared<FJsonValueNull>();
    }
    return MakeShared<FJsonValueString>(Id.ToString(EGuidFormats::DigitsWithHyphens));
}

TSharedPtr<FJsonValue> OptionalName(FName Name)
{
    if (Name.IsNone())
    {
        return MakeShared<FJsonValueNull>();
    }
    return MakeShared<FJsonValueString>(Name.ToString());
}

TSharedPtr<FJsonValue> OptionalString(const FString& Value)
{
    if (Value.IsEmpty())
    {
        return MakeShared<FJsonValueNull>();
    }
    return MakeShared<FJsonValueString>(Value);
}

TSharedRef<FJsonObject> TypeToJson(const FBPSemanticType& Type)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("kind"), TypeKindName(Type.Kind));
    Json->SetStringField(TEXT("type_path"), Type.TypePath);
    Json->SetStringField(TEXT("container_kind"), Type.ContainerKind);
    Json->SetStringField(TEXT("inner_type"), Type.InnerType);
    Json->SetStringField(TEXT("key_type"), Type.KeyType);
    Json->SetStringField(TEXT("value_type"), Type.ValueType);
    // 従来の文字列フィールドに加えて、ネストした型の意味情報を保持する。
    if (Type.ElementType.IsValid())
    {
        Json->SetObjectField(TEXT("element_type_info"), TypeToJson(*Type.ElementType));
    }
    else
    {
        Json->SetField(TEXT("element_type_info"), MakeShared<FJsonValueNull>());
    }
    if (Type.KeyTypeInfo.IsValid())
    {
        Json->SetObjectField(TEXT("key_type_info"), TypeToJson(*Type.KeyTypeInfo));
    }
    else
    {
        Json->SetField(TEXT("key_type_info"), MakeShared<FJsonValueNull>());
    }
    if (Type.ValueTypeInfo.IsValid())
    {
        Json->SetObjectField(TEXT("value_type_info"), TypeToJson(*Type.ValueTypeInfo));
    }
    else
    {
        Json->SetField(TEXT("value_type_info"), MakeShared<FJsonValueNull>());
    }
    Json->SetBoolField(TEXT("is_reference"), Type.bIsReference);
    Json->SetBoolField(TEXT("is_const"), Type.bIsConst);
    return Json;
}

TSharedRef<FJsonObject> ExpressionToJson(const FBPSemanticExpression& Expression)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("kind"), ExpressionKindName(Expression.Kind));
    Json->SetObjectField(TEXT("type"), TypeToJson(Expression.Type));
    Json->SetField(TEXT("source_node_id"), GuidValue(Expression.SourceNodeId));
    Json->SetField(TEXT("source_pin_id"), GuidValue(Expression.SourcePinId));
    // 空文字列リテラルは有効な値なので省略しない。
    Json->SetStringField(TEXT("literal_value"), Expression.LiteralValue);
    Json->SetField(TEXT("variable_name"), OptionalName(Expression.VariableName));
    Json->SetField(TEXT("function_name"), OptionalName(Expression.FunctionName));
    Json->SetField(TEXT("function_owner_path"), OptionalString(Expression.FunctionOwnerPath));
    TArray<TSharedPtr<FJsonValue>> Arguments;
    for (const FBPSemanticExpression& Argument : Expression.Arguments)
    {
        Arguments.Add(MakeShared<FJsonValueObject>(ExpressionToJson(Argument)));
    }
    Json->SetArrayField(TEXT("arguments"), Arguments);
    return Json;
}

TSharedRef<FJsonObject> StatementToJson(const FBPSemanticStatement& Statement)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("kind"), StatementKindName(Statement.Kind));
    Json->SetField(TEXT("source_node_id"), GuidValue(Statement.SourceNodeId));
    Json->SetField(TEXT("assignment_target"), OptionalName(Statement.AssignmentTarget));
    Json->SetObjectField(TEXT("expression"), ExpressionToJson(Statement.Expression));
    TArray<TSharedPtr<FJsonValue>> ThenStatements;
    for (const FBPSemanticStatement& Child : Statement.ThenStatements)
    {
        ThenStatements.Add(MakeShared<FJsonValueObject>(StatementToJson(Child)));
    }
    Json->SetArrayField(TEXT("then_statements"), ThenStatements);
    TArray<TSharedPtr<FJsonValue>> ElseStatements;
    for (const FBPSemanticStatement& Child : Statement.ElseStatements)
    {
        ElseStatements.Add(MakeShared<FJsonValueObject>(StatementToJson(Child)));
    }
    Json->SetArrayField(TEXT("else_statements"), ElseStatements);
    Json->SetField(TEXT("loop_index_variable"), OptionalName(Statement.LoopIndexVariable));
    Json->SetObjectField(TEXT("loop_first_index"), ExpressionToJson(Statement.LoopFirstIndex));
    Json->SetObjectField(TEXT("loop_last_index"), ExpressionToJson(Statement.LoopLastIndex));
    TArray<TSharedPtr<FJsonValue>> LoopBodyStatements;
    for (const FBPSemanticStatement& Child : Statement.LoopBodyStatements)
    {
        LoopBodyStatements.Add(MakeShared<FJsonValueObject>(StatementToJson(Child)));
    }
    Json->SetArrayField(TEXT("loop_body_statements"), LoopBodyStatements);
    TArray<TSharedPtr<FJsonValue>> CompletedStatements;
    for (const FBPSemanticStatement& Child : Statement.CompletedStatements)
    {
        CompletedStatements.Add(MakeShared<FJsonValueObject>(StatementToJson(Child)));
    }
    Json->SetArrayField(TEXT("completed_statements"), CompletedStatements);
    // 既存v2のForLoopフィールドは維持し、ForEach専用情報を追加する。
    if (Statement.Kind == EBPSemanticStatementKind::ForEachLoop)
    {
        Json->SetObjectField(TEXT("loop_array"), ExpressionToJson(Statement.LoopArray));
        Json->SetField(TEXT("loop_element_variable"), OptionalName(Statement.LoopElementVariable));
        Json->SetObjectField(TEXT("loop_element_type"), TypeToJson(Statement.LoopElementType));
    }
    return Json;
}
}

TSharedRef<FJsonObject> FBPSemanticJsonSerializer::ToJson(const FBPSemanticIR& IR, const FString& EngineVersion)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 2);
    Root->SetStringField(TEXT("format"), TEXT("blueprint_semantic_ir"));
    TSharedRef<FJsonObject> Producer = MakeShared<FJsonObject>();
    Producer->SetStringField(TEXT("name"), TEXT("BlueprintAIConverter"));
    Producer->SetStringField(TEXT("engine_version"), EngineVersion);
    Root->SetObjectField(TEXT("producer"), Producer);
    TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
    Source->SetStringField(TEXT("asset_path"), IR.AssetPath);
    Source->SetStringField(TEXT("parent_class_path"), IR.ParentClassPath);
    Source->SetNumberField(TEXT("snapshot_schema_version"), 1);
    Root->SetObjectField(TEXT("source"), Source);

    // クラスの出自を明示し、BlueprintとネイティブUCLASSを混同しない。
    // 未解析のクラスはunknownとし、既存JSON利用者との互換性を維持する。
    TSharedRef<FJsonObject> ClassJson = MakeShared<FJsonObject>();
    FString ClassOrigin = TEXT("unknown");
    if (IR.ClassInfo.Origin == EBPSemanticClassOrigin::Blueprint)
    {
        ClassOrigin = TEXT("blueprint");
    }
    else if (IR.ClassInfo.Origin == EBPSemanticClassOrigin::Native)
    {
        ClassOrigin = TEXT("native");
    }
    ClassJson->SetStringField(TEXT("origin"), ClassOrigin);
    ClassJson->SetStringField(TEXT("class_path"), IR.ClassInfo.ClassPath);
    ClassJson->SetStringField(TEXT("class_name"), IR.ClassInfo.ClassName);
    ClassJson->SetStringField(TEXT("native_parent_class_path"), IR.ClassInfo.NativeParentClassPath);
    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FString& InterfacePath : IR.ClassInfo.InterfacePaths)
    {
        Interfaces.Add(MakeShared<FJsonValueString>(InterfacePath));
    }
    ClassJson->SetArrayField(TEXT("interface_paths"), Interfaces);
    // 宣言メンバーのリフレクション情報を、既存の意味解析済みvariables/functionsと分離する。
    auto MembersToJson = [](const TArray<FBPSemanticClassMember>& Members)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FBPSemanticClassMember& Member : Members)
        {
            TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Member.Name);
            Entry->SetStringField(TEXT("cpp_type"), Member.CppType);
            Entry->SetStringField(TEXT("reflection_path"), Member.ReflectionPath);
            Entry->SetStringField(TEXT("flags"), Member.Flags);
            Result.Add(MakeShared<FJsonValueObject>(Entry));
        }
        return Result;
    };
    ClassJson->SetArrayField(TEXT("declared_properties"), MembersToJson(IR.ClassInfo.DeclaredProperties));
    ClassJson->SetArrayField(TEXT("declared_functions"), MembersToJson(IR.ClassInfo.DeclaredFunctions));
    TArray<TSharedPtr<FJsonValue>> Signatures;
    for (const FBPSemanticReflectedFunction& Function : IR.ClassInfo.DeclaredFunctionSignatures)
    {
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Function.Name);
        Entry->SetStringField(TEXT("reflection_path"), Function.ReflectionPath);
        Entry->SetStringField(TEXT("flags"), Function.Flags);
        Entry->SetStringField(TEXT("return_cpp_type"), Function.ReturnCppType);
        Entry->SetBoolField(TEXT("is_native"), Function.bIsNative);
        Entry->SetBoolField(TEXT("is_static"), Function.bIsStatic);
        Entry->SetBoolField(TEXT("is_const"), Function.bIsConst);
        Entry->SetBoolField(TEXT("is_blueprint_event"), Function.bIsBlueprintEvent);
        Entry->SetBoolField(TEXT("is_net"), Function.bIsNet);
        Entry->SetBoolField(TEXT("is_final"), Function.bIsFinal);
        TArray<TSharedPtr<FJsonValue>> Parameters;
        for (const FBPSemanticReflectedParameter& Parameter : Function.Parameters)
        {
            TSharedRef<FJsonObject> ParamJson = MakeShared<FJsonObject>();
            ParamJson->SetStringField(TEXT("name"), Parameter.Name);
            ParamJson->SetStringField(TEXT("cpp_type"), Parameter.CppType);
            ParamJson->SetBoolField(TEXT("is_out"), Parameter.bIsOut);
            ParamJson->SetBoolField(TEXT("is_reference"), Parameter.bIsReference);
            ParamJson->SetBoolField(TEXT("is_const"), Parameter.bIsConst);
            Parameters.Add(MakeShared<FJsonValueObject>(ParamJson));
        }
        Entry->SetArrayField(TEXT("parameters"), Parameters);
        Signatures.Add(MakeShared<FJsonValueObject>(Entry));
    }
    ClassJson->SetArrayField(TEXT("declared_function_signatures"), Signatures);
    Root->SetObjectField(TEXT("class_info"), ClassJson);

    // 現行IRは解析対象全体の網羅性を証明できない。成功時もpartialとし、AIによる過信を防ぐ。
    Root->SetStringField(TEXT("semantic_status"), IR.HasErrors() ? TEXT("unsupported") : TEXT("partial"));

    TArray<TSharedPtr<FJsonValue>> Functions;
    for (const FBPSemanticFunction& Function : IR.Functions)
    {
        TSharedRef<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
        FunctionJson->SetStringField(TEXT("name"), Function.Name.ToString());
        const TCHAR* Visibility = TEXT("invalid");
        switch (Function.Visibility)
        {
        case EBPSemanticFunctionVisibility::Unspecified: Visibility = TEXT("unspecified"); break;
        case EBPSemanticFunctionVisibility::Public: Visibility = TEXT("public"); break;
        case EBPSemanticFunctionVisibility::Protected: Visibility = TEXT("protected"); break;
        case EBPSemanticFunctionVisibility::Private: Visibility = TEXT("private"); break;
        }
        FunctionJson->SetStringField(TEXT("visibility"), Visibility);
        FunctionJson->SetObjectField(TEXT("return_type"), TypeToJson(Function.ReturnType));
        if (Function.SignatureSource.IsEmpty() == false)
        {
            FunctionJson->SetStringField(TEXT("owner_class_path"), Function.OwnerClassPath);
            FunctionJson->SetStringField(TEXT("signature_source"), Function.SignatureSource);
            FunctionJson->SetBoolField(TEXT("is_native"), Function.bIsNative);
            FunctionJson->SetBoolField(TEXT("is_static"), Function.bIsStatic);
            FunctionJson->SetBoolField(TEXT("is_const"), Function.bIsConst);
            FunctionJson->SetBoolField(TEXT("is_blueprint_event"), Function.bIsBlueprintEvent);
            FunctionJson->SetBoolField(TEXT("is_event_graph"), Function.bIsEventGraph);
            FunctionJson->SetBoolField(TEXT("is_override"), Function.bIsOverride);
            FunctionJson->SetBoolField(TEXT("is_interface_implementation"), Function.bIsInterfaceImplementation);
            FunctionJson->SetField(TEXT("interface_class_path"), OptionalString(Function.InterfaceClassPath));
            FunctionJson->SetField(TEXT("interface_function_path"), OptionalString(Function.InterfaceFunctionPath));
            FunctionJson->SetField(TEXT("overridden_function_path"), OptionalString(Function.OverriddenFunctionPath));
            // 属性は個別のboolを維持しつつ、反射由来であることを明示する。
            FunctionJson->SetStringField(TEXT("function_attributes_source"), Function.SignatureSource);
            TArray<TSharedPtr<FJsonValue>> Parameters;
            for (const FBPSemanticParameter& Parameter : Function.Parameters)
            {
                TSharedRef<FJsonObject> ParamJson = MakeShared<FJsonObject>();
                ParamJson->SetStringField(TEXT("name"), Parameter.Name.ToString());
                ParamJson->SetObjectField(TEXT("type"), TypeToJson(Parameter.Type));
                ParamJson->SetNumberField(TEXT("declaration_index"), Parameter.DeclarationIndex);
                ParamJson->SetBoolField(TEXT("is_out"), Parameter.bIsOut);
                ParamJson->SetBoolField(TEXT("is_return"), Parameter.bIsReturn);
                ParamJson->SetBoolField(TEXT("has_default_value"), Parameter.bHasDefaultValue);
                ParamJson->SetStringField(TEXT("default_value_source"),
                    Parameter.DefaultValueSource);
                ParamJson->SetStringField(TEXT("default_value_status"),
                    Parameter.DefaultValueStatus);
                ParamJson->SetField(TEXT("default_value_source_node_id"),
                    GuidValue(Parameter.DefaultValueSourceNodeId));
                ParamJson->SetField(TEXT("default_value_source_pin_id"),
                    GuidValue(Parameter.DefaultValueSourcePinId));
                if (Parameter.bHasDefaultValue)
                {
                    ParamJson->SetStringField(TEXT("default_value"), Parameter.DefaultValue);
                }
                else
                {
                    ParamJson->SetField(TEXT("default_value"), MakeShared<FJsonValueNull>());
                }
                Parameters.Add(MakeShared<FJsonValueObject>(ParamJson));
            }
            FunctionJson->SetArrayField(TEXT("parameters"), Parameters);
        }
        TArray<TSharedPtr<FJsonValue>> Blocks;
        for (const FBPSemanticBasicBlock& Block : Function.Blocks)
        {
            TSharedRef<FJsonObject> BlockJson = MakeShared<FJsonObject>();
            BlockJson->SetField(TEXT("block_id"), GuidValue(Block.BlockId));
            BlockJson->SetField(TEXT("source_entry_node_id"), GuidValue(Block.SourceEntryNodeId));
            BlockJson->SetStringField(TEXT("terminator"),
                BlockTerminatorKindName(Block.Terminator));
            TArray<TSharedPtr<FJsonValue>> SuccessorBlockIds;
            for (const FGuid& SuccessorBlockId : Block.SuccessorBlockIds)
            {
                SuccessorBlockIds.Add(GuidValue(SuccessorBlockId));
            }
            BlockJson->SetArrayField(TEXT("successor_block_ids"), SuccessorBlockIds);
            TArray<TSharedPtr<FJsonValue>> Statements;
            for (const FBPSemanticStatement& Statement : Block.Statements)
            {
                Statements.Add(MakeShared<FJsonValueObject>(StatementToJson(Statement)));
            }
            BlockJson->SetArrayField(TEXT("statements"), Statements);
            Blocks.Add(MakeShared<FJsonValueObject>(BlockJson));
        }
        FunctionJson->SetArrayField(TEXT("blocks"), Blocks);
        Functions.Add(MakeShared<FJsonValueObject>(FunctionJson));
    }
    Root->SetArrayField(TEXT("functions"), Functions);

    // 変数名でソートし、Blueprint内部配列の順序に依存しないJSONを生成する。
    TArray<FBPSemanticVariable> SortedVariables = IR.Variables;
    SortedVariables.Sort([](const FBPSemanticVariable& A, const FBPSemanticVariable& B)
    {
        return A.Name.ToString() < B.Name.ToString();
    });
    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPSemanticVariable& Variable : SortedVariables)
    {
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("name"), Variable.Name.ToString());
        Json->SetField(TEXT("guid"), GuidValue(Variable.Guid));
        Json->SetObjectField(TEXT("type"), TypeToJson(Variable.Type));
        Json->SetStringField(TEXT("pin_category"), Variable.PinCategory);
        Json->SetStringField(TEXT("type_path"), Variable.TypePath);
        Json->SetStringField(TEXT("property_flags"), Variable.PropertyFlags);
        Json->SetStringField(TEXT("source"), Variable.Source);
        Json->SetStringField(TEXT("default_value_source"), Variable.DefaultValueSource);
        Json->SetBoolField(TEXT("default_value_available"), Variable.bDefaultValueAvailable);
        if (Variable.bDefaultValueAvailable)
        {
            Json->SetStringField(TEXT("default_value"), Variable.DefaultValue);
        }
        else
        {
            Json->SetField(TEXT("default_value"), MakeShared<FJsonValueNull>());
        }
        Variables.Add(MakeShared<FJsonValueObject>(Json));
    }
    Root->SetArrayField(TEXT("variables"), Variables);

    // Snapshot v1は変更せず、Graph IR由来の接続をv2の独立した追跡領域に記録する。
    TArray<TSharedPtr<FJsonValue>> GraphTraces;
    for (const FBPSemanticGraphTrace& Trace : IR.GraphTraces)
    {
        TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
        GraphJson->SetStringField(TEXT("name"), Trace.Name);
        GraphJson->SetStringField(TEXT("path"), Trace.Path);
        GraphJson->SetStringField(TEXT("kind"), Trace.Kind);
        TArray<TSharedPtr<FJsonValue>> NodeIds;
        for (const FGuid& Id : Trace.NodeIds)
        {
            NodeIds.Add(GuidValue(Id));
        }
        GraphJson->SetArrayField(TEXT("node_ids"), NodeIds);
        TArray<TSharedPtr<FJsonValue>> PinIds;
        for (const FGuid& Id : Trace.PinIds)
        {
            PinIds.Add(GuidValue(Id));
        }
        GraphJson->SetArrayField(TEXT("pin_ids"), PinIds);
        const auto LinksToJson = [](const TArray<FBPIRLink>& Links)
        {
            TArray<TSharedPtr<FJsonValue>> Result;
            for (const FBPIRLink& Link : Links)
            {
                TSharedRef<FJsonObject> LinkJson = MakeShared<FJsonObject>();
                LinkJson->SetField(TEXT("source_node_id"), GuidValue(Link.SourceNode));
                LinkJson->SetField(TEXT("source_pin_id"), GuidValue(Link.SourcePin));
                LinkJson->SetField(TEXT("target_node_id"), GuidValue(Link.TargetNode));
                LinkJson->SetField(TEXT("target_pin_id"), GuidValue(Link.TargetPin));
                Result.Add(MakeShared<FJsonValueObject>(LinkJson));
            }
            return Result;
        };
        GraphJson->SetArrayField(TEXT("exec_edges"), LinksToJson(Trace.ExecEdges));
        GraphJson->SetArrayField(TEXT("data_edges"), LinksToJson(Trace.DataEdges));
        GraphTraces.Add(MakeShared<FJsonValueObject>(GraphJson));
    }
    Root->SetArrayField(TEXT("graph_traces"), GraphTraces);

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics)
    {
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        const bool bIsError = Diagnostic.Severity == EBPSemanticDiagnosticSeverity::Error;
        Json->SetStringField(TEXT("severity"), bIsError ? TEXT("error") : TEXT("warning"));
        Json->SetStringField(TEXT("code"),
            bIsError ? TEXT("SEMANTIC_LOWERING_ERROR") : TEXT("SEMANTIC_LOWERING_WARNING"));
        Json->SetStringField(TEXT("graph_name"), Diagnostic.GraphName);
        Json->SetField(TEXT("node_id"), GuidValue(Diagnostic.NodeId));
        Json->SetField(TEXT("pin_id"), GuidValue(Diagnostic.PinId));
        Json->SetStringField(TEXT("message"), Diagnostic.Message);
        Diagnostics.Add(MakeShared<FJsonValueObject>(Json));
    }
    Root->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Root;
}

bool FBPSemanticJsonSerializer::Serialize(const FBPSemanticIR& IR, const FString& EngineVersion, FString& OutJson)
{
    OutJson.Reset();
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    return FJsonSerializer::Serialize(ToJson(IR, EngineVersion), Writer);
}
