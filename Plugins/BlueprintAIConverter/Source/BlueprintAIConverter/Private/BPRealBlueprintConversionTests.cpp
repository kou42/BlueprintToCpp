#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Pawn.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
template <typename NodeType>
NodeType* AddNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}

bool AddScalarVariable(UBlueprint* Blueprint, const FName Name, const FName Category,
    const FString& DefaultValue, const FName SubCategory = NAME_None)
{
    FEdGraphPinType Type;
    Type.PinCategory = Category;
    Type.PinSubCategory = SubCategory;
    return FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue);
}

bool AddStructVariable(UBlueprint* Blueprint, const FName Name, UScriptStruct* Struct,
    const FString& DefaultValue)
{
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
    Type.PinSubCategoryObject = Struct;
    return FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue);
}

bool AddIntArrayVariable(UBlueprint* Blueprint, const FName Name, const FString& DefaultValue)
{
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Int;
    Type.ContainerType = EPinContainerType::Array;
    return FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealBlueprintPrintStringConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.PawnPrintString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealBlueprintPrintStringConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_RealPawnPrintString"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        APawn::StaticClass(), GetTransientPackage(), BlueprintName, BPTYPE_Normal, NAME_None);
    TestNotNull(TEXT("Pawn派生Blueprintを生成"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }

    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    TestNotNull(TEXT("EventGraphを取得"), EventGraph);
    if (EventGraph == nullptr)
    {
        return false;
    }
    // テンプレート由来ノードの有無にテスト結果を依存させない。
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(
        TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    TestNotNull(TEXT("PrintString UFunctionを取得"), PrintFunction);
    if (PrintFunction == nullptr)
    {
        return false;
    }
    UK2Node_CallFunction* Print = AddNode<UK2Node_CallFunction>(EventGraph);
    Print->SetFromFunction(PrintFunction);
    Print->AllocateDefaultPins();

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    TestNotNull(TEXT("K2 schemaを取得"), Schema);
    UEdGraphPin* ThenPin = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ExecutePin = Print->FindPin(UEdGraphSchema_K2::PN_Execute);
    TestNotNull(TEXT("BeginPlay then pin"), ThenPin);
    TestNotNull(TEXT("PrintString execute pin"), ExecutePin);
    if (Schema == nullptr || ThenPin == nullptr || ExecutePin == nullptr)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからPrintStringを接続"),
        Schema->TryCreateConnection(ThenPin, ExecutePin));

    Print->FindPinChecked(TEXT("InString"))->DefaultValue = TEXT("Real Blueprint Parent");
    Print->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("true");
    Print->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("false");
    Print->FindPinChecked(TEXT("TextColor"))->DefaultValue = TEXT("(R=0.25,G=0.5,B=0.75,A=1.0)");
    Print->FindPinChecked(TEXT("Duration"))->DefaultValue = TEXT("4.25");
    Print->FindPinChecked(TEXT("Key"))->DefaultValue = TEXT("parent-key");

    TestTrue(TEXT("boolメンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Enabled"), UEdGraphSchema_K2::PC_Boolean, TEXT("true")));
    TestTrue(TEXT("int32メンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Health"), UEdGraphSchema_K2::PC_Int, TEXT("42")));
    TestTrue(TEXT("FStringメンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Title"), UEdGraphSchema_K2::PC_String, TEXT("Pawn \"Ready\"")));
    TestTrue(TEXT("int64メンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("LargeCount"), UEdGraphSchema_K2::PC_Int64, TEXT("9000000000")));
    TestTrue(TEXT("floatメンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Speed"), UEdGraphSchema_K2::PC_Real, TEXT("1.25"),
        UEdGraphSchema_K2::PC_Float));
    TestTrue(TEXT("doubleメンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Precision"), UEdGraphSchema_K2::PC_Real, TEXT("2.5"),
        UEdGraphSchema_K2::PC_Double));
    TestTrue(TEXT("FNameメンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Tag"), UEdGraphSchema_K2::PC_Name, TEXT("Runner")));
    TestTrue(TEXT("FTextメンバー変数を追加"), AddScalarVariable(
        Blueprint, TEXT("Label"), UEdGraphSchema_K2::PC_Text, TEXT("INVTEXT(\"Ready Text\")")));
    TestTrue(TEXT("FVectorメンバー変数を追加"), AddStructVariable(
        Blueprint, TEXT("SpawnOffset"), TBaseStructure<FVector>::Get(),
        TEXT("(X=125.0,Y=-50.0,Z=300.0)")));
    TestTrue(TEXT("FRotatorメンバー変数を追加"), AddStructVariable(
        Blueprint, TEXT("SpawnRotation"), TBaseStructure<FRotator>::Get(),
        TEXT("(Pitch=10.0,Yaw=20.0,Roll=30.0)")));
    TestTrue(TEXT("FTransformメンバー変数を追加"), AddStructVariable(
        Blueprint, TEXT("SpawnTransform"), TBaseStructure<FTransform>::Get(),
        FString()));
    TestTrue(TEXT("FLinearColorメンバー変数を追加"), AddStructVariable(
        Blueprint, TEXT("Tint"), TBaseStructure<FLinearColor>::Get(),
        TEXT("(R=0.1,G=0.2,B=0.3,A=1.0)")));
    TestTrue(TEXT("int32配列メンバー変数を追加"), AddIntArrayVariable(
        Blueprint, TEXT("Checkpoints"), TEXT("(10,20,30)")));

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestNotNull(TEXT("Blueprintをコンパイル"), Blueprint->GeneratedClass.Get());

    FBPGraphIR GraphIR;
    TestTrue(TEXT("実BlueprintからGraph IRを生成"), FBPGraphIRBuilder::Build(Blueprint, GraphIR));
    TestEqual(TEXT("Pawn親クラスパスを保持"),
        GraphIR.ParentClassPath, APawn::StaticClass()->GetPathName());
    TestEqual(TEXT("Blueprintメンバー変数数を保持"), GraphIR.BlueprintVariableCount, 13);

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("実GraphをSemantic IRへLowering"),
        FBPSemanticLowering::LowerPrintString(Blueprint, GraphIR, SemanticIR)) == false)
    {
        return false;
    }

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Pawn派生C++を生成"), FBPSemanticCodeGenerator::Generate(
        SemanticIR, TEXT("ARealPawnPrintString"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("親ヘッダー"), Files.HeaderContent.Contains(TEXT("GameFramework/Pawn.h")));
    TestTrue(TEXT("親クラス"), Files.HeaderContent.Contains(TEXT("public APawn")));
    TestTrue(TEXT("文字列"), Files.SourceContent.Contains(TEXT("Real Blueprint Parent")));
    TestTrue(TEXT("画面出力"), Files.SourceContent.Contains(TEXT(", true, false,")));
    TestTrue(TEXT("Duration"), Files.SourceContent.Contains(TEXT("4.25f")));
    TestTrue(TEXT("Key"), Files.SourceContent.Contains(TEXT("parent-key")));
    TestTrue(TEXT("bool宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("bool Enabled = true;")));
    TestTrue(TEXT("int32宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("int32 Health = 42;")));
    TestTrue(TEXT("FString宣言とエスケープ済みCDO初期値"),
        Files.HeaderContent.Contains(TEXT("FString Title = TEXT(\"Pawn \\\"Ready\\\"\");")));
    TestTrue(TEXT("int64宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("int64 LargeCount = 9000000000LL;")));
    TestTrue(TEXT("float宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("float Speed = 1.25f;")));
    TestTrue(TEXT("double宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("double Precision = 2.5")));
    TestTrue(TEXT("FName宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("FName Tag = FName(TEXT(\"Runner\"));")));
    TestTrue(TEXT("FText宣言とCDO初期値"),
        Files.HeaderContent.Contains(TEXT("FText Label = ")));
    TestTrue(TEXT("FVector宣言"),
        Files.HeaderContent.Contains(TEXT("FVector SpawnOffset{};")));
    TestTrue(TEXT("FVector既定値を汎用ImportTextで復元"),
        Files.SourceContent.Contains(TEXT("GET_MEMBER_NAME_CHECKED(ARealPawnPrintString, SpawnOffset)")) &&
        Files.SourceContent.Contains(TEXT("X=125.000000")) &&
        Files.SourceContent.Contains(TEXT("Y=-50.000000")) &&
        Files.SourceContent.Contains(TEXT("Z=300.000000")));
    TestTrue(TEXT("int32配列宣言"),
        Files.HeaderContent.Contains(TEXT("TArray<int32> Checkpoints{};")));
    TestTrue(TEXT("int32配列既定値を汎用ImportTextで復元"),
        Files.SourceContent.Contains(
            TEXT("GET_MEMBER_NAME_CHECKED(ARealPawnPrintString, Checkpoints)")) &&
        Files.SourceContent.Contains(TEXT("(10,20,30)")));
    TestTrue(TEXT("FRotator既定値を汎用ImportTextで復元"),
        Files.HeaderContent.Contains(TEXT("FRotator SpawnRotation{};")) &&
        Files.SourceContent.Contains(
            TEXT("GET_MEMBER_NAME_CHECKED(ARealPawnPrintString, SpawnRotation)")));
    TestTrue(TEXT("FTransform既定値を汎用ImportTextで復元"),
        Files.HeaderContent.Contains(TEXT("FTransform SpawnTransform{};")) &&
        Files.SourceContent.Contains(
            TEXT("GET_MEMBER_NAME_CHECKED(ARealPawnPrintString, SpawnTransform)")));
    TestTrue(TEXT("FLinearColor既定値を汎用ImportTextで復元"),
        Files.HeaderContent.Contains(TEXT("FLinearColor Tint{};")) &&
        Files.SourceContent.Contains(
            TEXT("GET_MEMBER_NAME_CHECKED(ARealPawnPrintString, Tint)")));
    TestEqual(TEXT("Variable IRを13件保持"), SemanticIR.Variables.Num(), 13);
    for (const FBPSemanticVariable& Variable : SemanticIR.Variables)
    {
        TestTrue(*FString::Printf(TEXT("%sの反射C++型を保持"), *Variable.Name.ToString()),
            Variable.ReflectedCppType.IsEmpty() == false);
        TestTrue(*FString::Printf(TEXT("%sのExportText値を保持"), *Variable.Name.ToString()),
            Variable.bExportedDefaultValueAvailable);
        TestTrue(*FString::Printf(TEXT("%sのExportText往復を検証"), *Variable.Name.ToString()),
            Variable.bExportTextRoundTripVerified);
        TestTrue(*FString::Printf(TEXT("%sの初期化戦略を解決"), *Variable.Name.ToString()),
            Variable.InitializationStrategy != EBPVariableInitializationStrategy::Unsupported);
    }
    const FBPSemanticVariable* SpawnOffset = SemanticIR.Variables.FindByPredicate(
        [](const FBPSemanticVariable& Variable)
        {
            return Variable.Name == TEXT("SpawnOffset");
        });
    TestNotNull(TEXT("FVector Variable IR"), SpawnOffset);
    if (SpawnOffset != nullptr)
    {
        TestTrue(TEXT("FVectorはImportText初期化"),
            SpawnOffset->InitializationStrategy == EBPVariableInitializationStrategy::ImportText);
        TestEqual(TEXT("FVector既定値ソース"), SpawnOffset->DefaultValueSource,
            FString(TEXT("cdo_export_text")));
    }
    const FBPSemanticVariable* Checkpoints = SemanticIR.Variables.FindByPredicate(
        [](const FBPSemanticVariable& Variable)
        {
            return Variable.Name == TEXT("Checkpoints");
        });
    TestNotNull(TEXT("TArray<int32> Variable IR"), Checkpoints);
    if (Checkpoints != nullptr)
    {
        TestEqual(TEXT("配列の反射C++型"), Checkpoints->ReflectedCppType,
            FString(TEXT("TArray<int32>")));
        TestTrue(TEXT("配列はImportText初期化"),
            Checkpoints->InitializationStrategy == EBPVariableInitializationStrategy::ImportText);
        TestEqual(TEXT("配列既定値ソース"), Checkpoints->DefaultValueSource,
            FString(TEXT("cdo_export_text")));
    }

    const FString OutputDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("BlueprintToCppGenerated"), TEXT("RealPawnPrintString"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("実Blueprint由来ヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("実Blueprint由来ソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_BP_GENERATED_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_BP_GENERATED_SOURCE=%s"), *SourcePath));
    return true;
}

#endif
