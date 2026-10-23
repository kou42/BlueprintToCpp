#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticLowering.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
template <typename NodeType>
NodeType* AddActorLocationNode(UEdGraph* Graph)
{
    NodeType* Node = NewObject<NodeType>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    return Node;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPRealActorLocationConversionTest,
    "BlueprintAIConverter.Conversion.RealBlueprint.ActorLocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPRealActorLocationConversionTest::RunTest(const FString& Parameters)
{
    const FName BlueprintName = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_RealActorLocation"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(), GetTransientPackage(), BlueprintName,
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Actor派生Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("EventGraphを取得"), EventGraph) == false)
    {
        return false;
    }
    EventGraph->Nodes.Reset();

    UK2Node_Event* BeginPlay = AddActorLocationNode<UK2Node_Event>(EventGraph);
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->AllocateDefaultPins();

    UFunction* GetFunction = AActor::StaticClass()->FindFunctionByName(
        TEXT("K2_GetActorLocation"));
    UFunction* SetFunction = AActor::StaticClass()->FindFunctionByName(
        TEXT("K2_SetActorLocation"));
    if (TestNotNull(TEXT("GetActorLocation UFunction"), GetFunction) == false ||
        TestNotNull(TEXT("SetActorLocation UFunction"), SetFunction) == false)
    {
        return false;
    }
    UK2Node_CallFunction* GetLocation =
        AddActorLocationNode<UK2Node_CallFunction>(EventGraph);
    GetLocation->SetFromFunction(GetFunction);
    GetLocation->AllocateDefaultPins();
    UK2Node_CallFunction* SetLocation =
        AddActorLocationNode<UK2Node_CallFunction>(EventGraph);
    SetLocation->SetFromFunction(SetFunction);
    SetLocation->AllocateDefaultPins();

    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
    UEdGraphPin* ThenPin = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* ExecutePin = SetLocation->FindPin(UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* ReturnPin = GetLocation->FindPin(TEXT("ReturnValue"));
    UEdGraphPin* NewLocationPin = SetLocation->FindPin(TEXT("NewLocation"));
    if (TestNotNull(TEXT("K2 schema"), Schema) == false ||
        TestNotNull(TEXT("BeginPlay Then"), ThenPin) == false ||
        TestNotNull(TEXT("SetActorLocation Execute"), ExecutePin) == false ||
        TestNotNull(TEXT("GetActorLocation ReturnValue"), ReturnPin) == false ||
        TestNotNull(TEXT("SetActorLocation NewLocation"), NewLocationPin) == false)
    {
        return false;
    }
    TestTrue(TEXT("BeginPlayからSetActorLocationを接続"),
        Schema->TryCreateConnection(ThenPin, ExecutePin));
    TestTrue(TEXT("GetActorLocationをNewLocationへ接続"),
        Schema->TryCreateConnection(ReturnPin, NewLocationPin));
    SetLocation->FindPinChecked(TEXT("bSweep"))->DefaultValue = TEXT("false");
    SetLocation->FindPinChecked(TEXT("bTeleport"))->DefaultValue = TEXT("false");

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestNotNull(TEXT("Blueprintをコンパイル"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("実BlueprintからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    FBPSemanticIR SemanticIR;
    if (TestTrue(TEXT("通常経路でActorLocationをLowering"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
        {
            AddError(Diagnostic.Message);
        }
        return false;
    }

    const auto HasDiagnostic = [](const FBPSemanticIR& IR, const TCHAR* Text)
    {
        return IR.Diagnostics.ContainsByPredicate(
            [Text](const FBPSemanticDiagnostic& Diagnostic)
            {
                return Diagnostic.Message.Contains(Text);
            });
    };
    UBlueprint* ObjectBlueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(
            GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_ObjectLocationPolicy")),
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("UObject Policy検証用Blueprintを生成"), ObjectBlueprint) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(ObjectBlueprint);
    if (TestNotNull(TEXT("UObject Policy検証用Blueprint"),
            ObjectBlueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    FBPGraphIR ObjectLocationIR = GraphIR;
    ObjectLocationIR.ParentClassPath = UObject::StaticClass()->GetPathName();
    FBPSemanticIR RejectedPolicyIR;
    TestFalse(TEXT("UObjectへActor位置Policyを誤適用しない"),
        FBPSemanticLowering::Lower(ObjectBlueprint, ObjectLocationIR, RejectedPolicyIR));
    TestTrue(TEXT("Actor専用Policyの拒否理由を保持"),
        HasDiagnostic(RejectedPolicyIR, TEXT("not derived from AActor")));

    RejectedPolicyIR = FBPSemanticIR();
    TestFalse(TEXT("Graph IRとBlueprintの親クラス不一致を拒否"),
        FBPSemanticLowering::Lower(ObjectBlueprint, GraphIR, RejectedPolicyIR));
    TestTrue(TEXT("親クラス不一致の診断を保持"),
        HasDiagnostic(RejectedPolicyIR, TEXT("does not match the source Blueprint")));

    UBlueprint* ComponentBlueprint = FKismetEditorUtilities::CreateBlueprint(
        UActorComponent::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(
            GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_ComponentLocationPolicy")),
        BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("ActorComponent Policy検証用Blueprintを生成"),
            ComponentBlueprint) == false)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(ComponentBlueprint);
    if (TestNotNull(TEXT("ActorComponent Policy検証用Blueprint"),
            ComponentBlueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }
    FBPGraphIR ComponentLocationIR = GraphIR;
    ComponentLocationIR.ParentClassPath = UActorComponent::StaticClass()->GetPathName();
    RejectedPolicyIR = FBPSemanticIR();
    TestFalse(TEXT("BeginPlay対応ComponentにActor位置APIを許可しない"),
        FBPSemanticLowering::Lower(
            ComponentBlueprint, ComponentLocationIR, RejectedPolicyIR));
    TestTrue(TEXT("ActorComponentへの位置Policy拒否を保持"),
        HasDiagnostic(RejectedPolicyIR, TEXT("not derived from AActor")));

    if (SemanticIR.Functions.Num() != 1 ||
        SemanticIR.Functions[0].Blocks.Num() != 1 ||
        SemanticIR.Functions[0].Blocks[0].Statements.Num() != 1)
    {
        AddError(TEXT("ActorLocation Semantic IRの関数構造が不正です"));
        return false;
    }
    const FBPSemanticExpression& Call =
        SemanticIR.Functions[0].Blocks[0].Statements[0].Expression;
    TestEqual(TEXT("SetActorLocation呼び出しを保持"),
        Call.FunctionName, FName(TEXT("K2_SetActorLocation")));
    TestEqual(TEXT("selfを含む引数数"), Call.Arguments.Num(), 5);
    if (Call.Arguments.Num() != 5)
    {
        return false;
    }
    TestEqual(TEXT("NewLocationはGetActorLocation式"),
        Call.Arguments[1].FunctionName, FName(TEXT("K2_GetActorLocation")));
    TestEqual(TEXT("SweepHitResult型"), Call.Arguments[3].Type.TypePath,
        FString(TEXT("/Script/Engine.HitResult")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ActorLocation C++を生成"),
        FBPSemanticCodeGenerator::Generate(SemanticIR,
            TEXT("ARealActorLocation"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("FHitResultヘッダー"),
        Files.SourceContent.Contains(TEXT("#include \"Engine/HitResult.h\"")));
    TestTrue(TEXT("GetActorLocation式を一度だけ生成"),
        Files.SourceContent.Contains(TEXT("K2_SetActorLocation(GetActorLocation(), false,")));
    TestTrue(TEXT("未接続ReturnValueを代入しない"),
        Files.SourceContent.Contains(TEXT("= K2_SetActorLocation")) == false);
    const FString HitName = Call.Arguments[3].VariableName.ToString();
    TestTrue(TEXT("出力引数のローカル領域を宣言"),
        Files.SourceContent.Contains(FString::Printf(TEXT("FHitResult %s{};"), *HitName)));

    FBPSemanticIR Tampered = SemanticIR;
    Tampered.Functions[0].Blocks[0].Statements[0].Expression.Arguments[3].Type.TypePath =
        TEXT("/Script/CoreUObject.Vector");
    FBPGeneratedActorFiles RejectedFiles;
    Diagnostics.Reset();
    TestFalse(TEXT("改ざんされた出力引数型をGeneratorで拒否"),
        FBPSemanticCodeGenerator::Generate(Tampered,
            TEXT("ARealActorLocation"), FString(), RejectedFiles, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), RejectedFiles.HeaderContent.IsEmpty());

    const FString OutputDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("BlueprintToCppGenerated"), TEXT("RealActorLocation"));
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    const FString HeaderPath = FPaths::Combine(OutputDirectory, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(OutputDirectory, Files.SourceFileName);
    TestTrue(TEXT("ActorLocationヘッダーを保存"),
        FFileHelper::SaveStringToFile(Files.HeaderContent, *HeaderPath));
    TestTrue(TEXT("ActorLocationソースを保存"),
        FFileHelper::SaveStringToFile(Files.SourceContent, *SourcePath));
    AddInfo(FString::Printf(TEXT("REAL_ACTOR_LOCATION_HEADER=%s"), *HeaderPath));
    AddInfo(FString::Printf(TEXT("REAL_ACTOR_LOCATION_SOURCE=%s"), *SourcePath));
    return true;
}

#endif
