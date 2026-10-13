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

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestNotNull(TEXT("Blueprintをコンパイル"), Blueprint->GeneratedClass.Get());

    FBPGraphIR GraphIR;
    TestTrue(TEXT("実BlueprintからGraph IRを生成"), FBPGraphIRBuilder::Build(Blueprint, GraphIR));
    TestEqual(TEXT("Pawn親クラスパスを保持"),
        GraphIR.ParentClassPath, APawn::StaticClass()->GetPathName());

    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("実GraphをSemantic IRへLowering"),
        FBPSemanticLowering::LowerPrintString(GraphIR, SemanticIR)) == false)
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
