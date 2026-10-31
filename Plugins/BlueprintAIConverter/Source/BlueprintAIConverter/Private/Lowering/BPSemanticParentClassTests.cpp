#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPGraphIR.h"
#include "BPParentClassResolver.h"
#include "BPSemanticLowering.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticClassExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "GameFramework/SaveGame.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBPSemanticNativeParentRoundtripTest,
    "BlueprintAIConverter.SemanticIR.Lowering.NativeParentClasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBPSemanticNativeParentRoundtripTest::GetTests(
    TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const TCHAR* Name : { TEXT("ActorComponent"), TEXT("SceneComponent"),
        TEXT("DataAsset"), TEXT("SaveGame") })
    {
        Names.Add(Name);
        Commands.Add(Name);
    }
}

bool FBPSemanticNativeParentRoundtripTest::RunTest(const FString& Parameters)
{
    UClass* Parent = nullptr;
    if (Parameters == TEXT("ActorComponent")) { Parent = UActorComponent::StaticClass(); }
    else if (Parameters == TEXT("SceneComponent")) { Parent = USceneComponent::StaticClass(); }
    else if (Parameters == TEXT("DataAsset")) { Parent = UDataAsset::StaticClass(); }
    else if (Parameters == TEXT("SaveGame")) { Parent = USaveGame::StaticClass(); }
    if (TestNotNull(TEXT("Native test parent"), Parent) == false) { return false; }
    const bool bComponent = Parent->IsChildOf(UActorComponent::StaticClass());
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent,
        GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(),
            UBlueprint::StaticClass(), TEXT("BP_NativeParent")), BPTYPE_Normal, NAME_None);
    if (TestNotNull(TEXT("Real native-parent Blueprint"), Blueprint) == false) { return false; }
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    // Non-PrintString calls exercise the common reflected static/self path.
    const auto AddCall = [Schema, bComponent](UEdGraph* Graph,
        UEdGraphPin* From) -> bool
    {
        UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
        Graph->AddNode(Call, false, false);
        Call->CreateNewGuid();
        Call->PostPlacedNewNode();
        Call->SetFromFunction(bComponent
            ? UActorComponent::StaticClass()->FindFunctionByName(TEXT("SetComponentTickEnabled"))
            : UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetSubtitlesEnabled")));
        Call->AllocateDefaultPins();
        return Schema->TryCreateConnection(From,
            Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
    };
    for (const FName Name : { FName(TEXT("RunFirst")), FName(TEXT("RunSecond")) })
    {
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name,
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
        UK2Node_FunctionEntry* Entry = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node))
            {
                Entry = Candidate;
            }
        }
        if (TestNotNull(TEXT("Function entry"), Entry) == false ||
            TestTrue(TEXT("Native call connected"), AddCall(Graph,
                Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then))) == false) { return false; }
    }
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (TestNotNull(TEXT("Event graph"), EventGraph) == false) { return false; }
    EventGraph->Nodes.Reset();
    if (bComponent)
    {
        UK2Node_Event* Event = NewObject<UK2Node_Event>(EventGraph);
        EventGraph->AddNode(Event, false, false);
        Event->CreateNewGuid();
        Event->PostPlacedNewNode();
        Event->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"),
            UActorComponent::StaticClass());
        Event->bOverrideFunction = true;
        Event->AllocateDefaultPins();
        if (TestTrue(TEXT("Component lifecycle call connected"), AddCall(EventGraph,
            Event->FindPinChecked(UEdGraphSchema_K2::PN_Then))) == false) { return false; }
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("Blueprint compiled"), Blueprint->Status != BS_Error) == false) { return false; }
    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("Graph IR built"), FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false) { return false; }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("All graphs lowered atomically"),
        FBPSemanticLowering::Lower(Blueprint, GraphIR, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics) { AddError(Diagnostic.Message); }
        return false;
    }
    TestEqual(TEXT("All functions retained"), Semantic.Functions.Num(), bComponent ? 3 : 2);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Native subclass generated"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UNativeParentGenerated"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { AddError(Diagnostic); }
        return false;
    }
    FBPResolvedParentClass Resolved;
    TestTrue(TEXT("Parent resolved"), FBPParentClassResolver::ResolveSemanticParent(
        Parent->GetPathName(), Resolved, Diagnostics));
    TestTrue(TEXT("Exact native inheritance"), Files.HeaderContent.Contains(
        FString::Printf(TEXT("class UNativeParentGenerated : public %s"), *Resolved.CppClassName)));
    TestTrue(TEXT("Public native header"), Files.HeaderContent.Contains(
        FString::Printf(TEXT("#include \"%s\""), *Resolved.HeaderPath)));
    for (const TCHAR* Name : { TEXT("RunFirst"), TEXT("RunSecond") })
    {
        TestTrue(TEXT("Normal function definition"), Files.SourceContent.Contains(
            FString::Printf(TEXT("void UNativeParentGenerated::%s()"), Name)));
    }
    TestEqual(TEXT("Only components emit lifecycle Super call"),
        Files.SourceContent.Contains(TEXT("Super::BeginPlay();")), bComponent);
    TestEqual(TEXT("Only components declare lifecycle override"),
        Files.HeaderContent.Contains(TEXT("virtual void BeginPlay() override;")), bComponent);
    TestFalse(TEXT("Actor lifecycle signature never leaks"),
        Files.SourceContent.Contains(TEXT("Super::Tick(")));

    FBPSemanticGraphTraceExtractor::Append(GraphIR, Semantic);
    TestTrue(TEXT("Class metadata extracted"), FBPSemanticClassExtractor::Append(Blueprint, Semantic));
    FString ExportPath;
    FString ExportError;
    if (TestTrue(TEXT("Real editor JSON exported"), FBPSemanticJsonExporter::Export(
        Semantic, FEngineVersion::Current().ToString(), ExportPath, ExportError)) == false)
    {
        AddError(ExportError);
        return false;
    }
    AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));

    if (bComponent == false)
    {
        FBPSemanticIR InvalidLifecycle = Semantic;
        FBPSemanticFunction& Function = InvalidLifecycle.Functions[0];
        Function.Name = TEXT("BeginPlay");
        Function.bIsOverride = true;
        Function.bIsEventGraph = true;
        Diagnostics.Reset();
        TestFalse(TEXT("UObject native parents cannot acquire Actor lifecycle"),
            FBPSemanticCodeGenerator::Generate(InvalidLifecycle, TEXT("UNativeParentGenerated"),
                FString(), Files, Diagnostics));
        TestTrue(TEXT("Lifecycle refusal clears source"), Files.SourceContent.IsEmpty());
    }

    // Parent substitution must not let an Actor API or lifecycle leak into a
    // UObject class, and failed generation must clear previously generated files.
    Semantic.ParentClassPath = TEXT("/Script/Engine.AnimInstance");
    Diagnostics.Reset();
    TestFalse(TEXT("Specialized generated data cannot be silently discarded"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("UNativeParentGenerated"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("Rejected generation publishes no source"), Files.SourceContent.IsEmpty());
    TestTrue(TEXT("Rejected generation publishes no header"), Files.HeaderContent.IsEmpty());
    FBPGraphIR RejectedGraph = GraphIR;
    RejectedGraph.ParentClassPath = TEXT("/Script/Engine.AnimInstance");
    FBPSemanticIR Rejected;
    TestFalse(TEXT("Specialized lowering rejected"),
        FBPSemanticLowering::Lower(Blueprint, RejectedGraph, Rejected));
    TestTrue(TEXT("Rejected lowering publishes no functions"), Rejected.Functions.IsEmpty());
    return true;
}

#endif
