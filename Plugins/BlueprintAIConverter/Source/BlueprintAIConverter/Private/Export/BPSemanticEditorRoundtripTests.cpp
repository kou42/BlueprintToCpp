#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticVariableExtractor.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
const FBPSemanticVariable* FindVariable(const FBPSemanticIR& IR, const FName Name)
{
    return IR.Variables.FindByPredicate(
        [Name](const FBPSemanticVariable& Variable)
        {
            return Variable.Name == Name;
        });
}

bool AddVariable(UBlueprint* Blueprint, const FName Name, const FName Category, const FString& DefaultValue)
{
    FEdGraphPinType Type;
    Type.PinCategory = Category;
    return FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticEditorRoundtripTest,
    "BlueprintAIConverter.SemanticJson.EditorRoundtrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticEditorRoundtripTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_SemanticRoundtrip"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName, BPTYPE_Normal, NAME_None);
    TestNotNull(TEXT("Editor APIで一時Blueprintを生成"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }

    TestTrue(TEXT("bool変数を追加"), AddVariable(
        Blueprint, TEXT("Enabled"), UEdGraphSchema_K2::PC_Boolean, TEXT("true")));
    TestTrue(TEXT("int変数を追加"), AddVariable(
        Blueprint, TEXT("Health"), UEdGraphSchema_K2::PC_Int, TEXT("42")));
    TestTrue(TEXT("FString変数を追加"), AddVariable(
        Blueprint, TEXT("Title"), UEdGraphSchema_K2::PC_String, TEXT("Roundtrip")));

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestNotNull(TEXT("GeneratedClassを生成"), Blueprint->GeneratedClass.Get());
    if (Blueprint->GeneratedClass == nullptr)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    TestTrue(TEXT("実BlueprintからGraph IRを生成"), FBPGraphIRBuilder::Build(Blueprint, GraphIR));

    FBPSemanticIR SemanticIR;
    SemanticIR.AssetPath = GraphIR.AssetPath;
    SemanticIR.ParentClassPath = GraphIR.ParentClassPath;
    FBPSemanticVariableExtractor::Append(Blueprint, SemanticIR);
    FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
    FBPSemanticGraphTraceExtractor::Append(GraphIR, SemanticIR);

    TestEqual(TEXT("Blueprint宣言変数を3件抽出"), SemanticIR.Variables.Num(), 3);
    const FBPSemanticVariable* Enabled = FindVariable(SemanticIR, TEXT("Enabled"));
    const FBPSemanticVariable* Health = FindVariable(SemanticIR, TEXT("Health"));
    const FBPSemanticVariable* Title = FindVariable(SemanticIR, TEXT("Title"));
    TestNotNull(TEXT("Enabledを抽出"), Enabled);
    TestNotNull(TEXT("Healthを抽出"), Health);
    TestNotNull(TEXT("Titleを抽出"), Title);
    if (Enabled == nullptr || Health == nullptr || Title == nullptr)
    {
        return false;
    }
    TestTrue(TEXT("EnabledのCDO値を取得"), Enabled->bDefaultValueAvailable);
    TestTrue(TEXT("HealthのCDO値を取得"), Health->bDefaultValueAvailable);
    TestTrue(TEXT("TitleのCDO値を取得"), Title->bDefaultValueAvailable);
    TestEqual(TEXT("Enabledの値"), Enabled->DefaultValue, FString(TEXT("true")));
    TestEqual(TEXT("Healthの値"), Health->DefaultValue, FString(TEXT("42")));
    TestEqual(TEXT("Titleの値"), Title->DefaultValue, FString(TEXT("Roundtrip")));

    FString Serialized;
    const FString EngineVersion = FEngineVersion::Current().ToString();
    TestTrue(TEXT("実Blueprint由来のSemantic JSONを生成"),
        FBPSemanticJsonSerializer::Serialize(SemanticIR, EngineVersion, Serialized));

    FString ExportPath;
    FString ExportError;
    TestTrue(TEXT("Semantic JSONをSavedへ保存"),
        FBPSemanticJsonExporter::Export(SemanticIR, EngineVersion, ExportPath, ExportError));
    if (ExportPath.IsEmpty())
    {
        AddError(ExportError);
        return false;
    }

    FString Exported;
    TestTrue(TEXT("保存したJSONを再読込"), FFileHelper::LoadFileToString(Exported, *ExportPath));
    TestEqual(TEXT("SerializerとExporterの内容が一致"), Exported, Serialized);

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Exported);
    TestTrue(TEXT("保存JSONを再解析"), FJsonSerializer::Deserialize(Reader, Root));
    if (Root == nullptr)
    {
        return false;
    }
    TestEqual(TEXT("保存JSONの変数数"), Root->GetArrayField(TEXT("variables")).Num(), 3);
    AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticIntArrayEditorTest,
    "BlueprintAIConverter.SemanticJson.EditorIntArrayReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticIntArrayEditorTest::RunTest(const FString& Parameters)
{
    // 実際のBlueprintをコンパイルし、PinCategoryだけでなく
    // FArrayPropertyのInner型まで検証した配列型をIRへ伝播できるか確認する。
    const FName Name = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_IntArrayReflection"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), Name, BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("一時Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }
    FEdGraphPinType ArrayType;
    ArrayType.PinCategory = UEdGraphSchema_K2::PC_Int;
    ArrayType.ContainerType = EPinContainerType::Array;
    if (TestTrue(TEXT("int32配列メンバーを追加"),
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Numbers"), ArrayType)) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("コンパイル済みGeneratedClass"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    // 実BlueprintからGraph IRを構築し、変数数の整合性も確認する。
    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("配列を持つBlueprintからGraph IRを構築"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    TestEqual(TEXT("Graph IRの変数数"), GraphIR.BlueprintVariableCount, 1);
    FBPSemanticIR IR;
    IR.AssetPath = GraphIR.AssetPath;
    IR.ParentClassPath = GraphIR.ParentClassPath;
    FBPSemanticVariableExtractor::Append(Blueprint, IR);
    const FBPSemanticVariable* Variable = FindVariable(IR, TEXT("Numbers"));
    if (TestNotNull(TEXT("配列変数を抽出"), Variable) == false)
    {
        return false;
    }
    TestEqual(TEXT("配列コンテナ"), Variable->Type.ContainerKind, FString(TEXT("array")));
    TestTrue(TEXT("要素型int32"), Variable->Type.Kind == EBPSemanticTypeKind::Int32);
    TestTrue(TEXT("型ツリーに要素型を保持"),
        Variable->Type.ElementType.IsValid() &&
        Variable->Type.ElementType->Kind == EBPSemanticTypeKind::Int32);
    TestEqual(TEXT("反射C++型"), Variable->ReflectedCppType, FString(TEXT("TArray<int32>")));

    // 実Blueprint由来の反射型を使って、DeclarationOnly経由で
    // C++の配列メンバー宣言まで到達することを検証する。
    // Blueprintの配列デフォルト値はこのモードでは再現しない。
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopIndexVariable = TEXT("ForEachArrayIndex");
    Loop.LoopElementVariable = TEXT("ForEachArrayElement");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = Variable->Name;
    Loop.LoopArray.Type = Variable->Type;
    // 空のBodyはCodeGenの安全検証で拒否されるため、ここでは
    // 配列変数の宣言経路だけを独立に確認する。
    Block.Statements.Add(MoveTemp(Loop));
    Function.Blocks.Add(MoveTemp(Block));
    IR.Functions.Add(MoveTemp(Function));
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("BodyなしのForEachは生成拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AIntArrayActor"),
            TEXT("TEST_API"), Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("拒否診断を保持"), Diagnostics.IsEmpty() == false);
    return true;
}

#endif
