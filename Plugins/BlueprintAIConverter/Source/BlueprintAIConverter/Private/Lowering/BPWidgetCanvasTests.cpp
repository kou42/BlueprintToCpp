#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPSemanticLowering.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticJsonExporter.h"
#include "BPSemanticClassExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include <limits>

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBPWidgetCanvasOverlayTest,
    "BlueprintAIConverter.SemanticIR.Lowering.CanvasOverlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBPWidgetCanvasOverlayTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const TCHAR* Name : { TEXT("CanvasFixed"), TEXT("CanvasStretch"), TEXT("Overlay"), TEXT("MixedPanels"),
        TEXT("MixedPanelsFunctions"), TEXT("EmptyCanvas"), TEXT("EmptyOverlay") })
    {
        Names.Add(Name);
        Commands.Add(Name);
    }
}

bool FBPWidgetCanvasOverlayTest::RunTest(const FString& Parameters)
{
    const bool bMixed = Parameters.StartsWith(TEXT("Mixed"));
    const bool bCanvas = Parameters.Contains(TEXT("Canvas")) || bMixed;
    const bool bEmpty = Parameters.StartsWith(TEXT("Empty"));
    const bool bFunctions = Parameters.EndsWith(TEXT("Functions"));
    const bool bStretch = Parameters == TEXT("CanvasStretch") || bMixed;
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(),
            UWidgetBlueprint::StaticClass(), TEXT("WBP_CanvasOverlay")), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), NAME_None));
    if (TestNotNull(TEXT("Real layout Blueprint"), Blueprint) == false) { return false; }
    if (Blueprint->WidgetTree == nullptr)
    {
        Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
    }
    UPanelWidget* Root = bCanvas
        ? static_cast<UPanelWidget*>(Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Layout")))
        : static_cast<UPanelWidget*>(Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Layout")));
    Root->bIsVariable = false;
    Blueprint->WidgetTree->RootWidget = Root;
    if (bEmpty == false)
    {
        UTextBlock* Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Message"));
        Text->bIsVariable = false;
        Text->SetText(FText::AsCultureInvariant(TEXT("Canvas and Overlay")));
        UPanelSlot* Slot = Root->AddChild(Text);
        if (bCanvas)
        {
            UCanvasPanelSlot* Canvas = CastChecked<UCanvasPanelSlot>(Slot);
            FAnchors Anchors;
            Anchors.Minimum = bStretch ? FVector2D(0.1, 0.2) : FVector2D(0.5, 0.5);
            Anchors.Maximum = bStretch ? FVector2D(0.9, 0.8) : Anchors.Minimum;
            Canvas->SetAnchors(Anchors);
            Canvas->SetOffsets(FMargin(11, -12, 33, 44));
            Canvas->SetAlignment(FVector2D(0.1234567890123456, 0.5));
            Canvas->SetAutoSize(bStretch == false);
            Canvas->SetZOrder(-7);
        }
        else
        {
            UOverlaySlot* Overlay = CastChecked<UOverlaySlot>(Slot);
            Overlay->SetPadding(FMargin(1, 2, 3, 4));
            Overlay->SetHorizontalAlignment(HAlign_Right);
            Overlay->SetVerticalAlignment(VAlign_Bottom);
        }
        if (bMixed)
        {
            UOverlay* Stack = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Stack"));
            Stack->bIsVariable = false;
            UCanvasPanelSlot* StackSlot = CastChecked<UCanvasPanelSlot>(Root->AddChild(Stack));
            StackSlot->SetZOrder(5);
            UVerticalBox* Column = Blueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
            Column->bIsVariable = false;
            Stack->AddChildToOverlay(Column)->SetPadding(FMargin(8));
            UHorizontalBox* Row = Blueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
            Row->bIsVariable = false;
            Column->AddChild(Row);
            UTextBlock* Nested = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Nested"));
            Nested->bIsVariable = false;
            Nested->SetText(NSLOCTEXT("WidgetConversion", "Nested", "Nested label"));
            Row->AddChild(Nested);
            UTextBlock* Top = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OverlayText"));
            Top->bIsVariable = false;
            Top->SetText(FText::AsCultureInvariant(TEXT("Top layer")));
            Stack->AddChild(Top);
        }
    }
    if (bFunctions)
    {
        for (const FName Name : { FName(TEXT("RunFirst")), FName(TEXT("RunSecond")) })
        {
            UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name,
                UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
            UK2Node_FunctionEntry* Entry = nullptr;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node)) { Entry = Candidate; }
            }
            if (TestNotNull(TEXT("Layout function entry"), Entry) == false) { return false; }
            UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
            Graph->AddNode(Call, false, false);
            Call->CreateNewGuid(); Call->PostPlacedNewNode();
            Call->SetFromFunction(UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetSubtitlesEnabled")));
            Call->AllocateDefaultPins();
            if (TestTrue(TEXT("Layout function connected"), GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
                Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then), Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false) { return false; }
        }
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("Layout Blueprint compiled"), Blueprint->Status != BS_Error) == false) { return false; }
    FBPGraphIR Graph;
    if (TestTrue(TEXT("Layout graphs built"), FBPGraphIRBuilder::Build(Blueprint, Graph)) == false) { return false; }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Canvas and Overlay lowered"), FBPSemanticLowering::Lower(Blueprint, Graph, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics) { AddError(Diagnostic.Message); }
        return false;
    }
    TestEqual(TEXT("Layout node count"), Semantic.WidgetTree.Nodes.Num(), bEmpty ? 1 : bMixed ? 7 : 2);
    TestEqual(TEXT("Layout function count"), Semantic.Functions.Num(), bFunctions ? 2 : 0);
    if (bEmpty == false && bCanvas)
    {
        if (TestTrue(TEXT("Canvas payload present"), Semantic.WidgetTree.Nodes[1].CanvasSlot.IsSet()) == false) { return false; }
        const FBPSemanticCanvasSlot& Canvas = Semantic.WidgetTree.Nodes[1].CanvasSlot.GetValue();
        TestEqual(TEXT("Canvas offset"), Canvas.OffsetTop, -12.0f);
        TestEqual(TEXT("Canvas precision"), Canvas.AlignmentX, 0.1234567890123456);
        TestEqual(TEXT("Canvas ZOrder"), Canvas.ZOrder, -7);
        TestEqual(TEXT("Canvas AutoSize"), Canvas.bAutoSize, bStretch == false);
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Layout C++ generated"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UConvertedLayout"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { AddError(Diagnostic); }
        return false;
    }
    TestTrue(TEXT("Correct root generated"), Files.SourceContent.Contains(bCanvas ? TEXT("ConstructWidget<UCanvasPanel>") : TEXT("ConstructWidget<UOverlay>")));
    if (bEmpty == false)
    {
        if (bCanvas)
        {
            TestTrue(TEXT("Canvas slot generated"), Files.SourceContent.Contains(TEXT("->AddChildToCanvas(GeneratedWidget1)")));
            TestTrue(TEXT("Canvas anchors generated"), Files.SourceContent.Contains(TEXT("->SetAnchors(GeneratedAnchors1)")));
            TestTrue(TEXT("Canvas ZOrder generated"), Files.SourceContent.Contains(TEXT("->SetZOrder(-7)")));
        }
        else
        {
            TestTrue(TEXT("Overlay attachment generated"), Files.SourceContent.Contains(TEXT("->AddChildToOverlay(GeneratedWidget1)")));
            TestFalse(TEXT("Overlay never emits box sizing"), Files.SourceContent.Contains(TEXT("->SetSize(")));
        }
        if (bMixed)
        {
            TestTrue(TEXT("Nested Overlay generated"), Files.SourceContent.Contains(TEXT("->AddChildToOverlay(GeneratedWidget3)")));
            TestTrue(TEXT("Nested boxes generated"), Files.SourceContent.Contains(TEXT("->AddChildToHorizontalBox(GeneratedWidget5)")));
            TestTrue(TEXT("Overlay sibling order retained"), Files.SourceContent.Find(TEXT("->AddChildToOverlay(GeneratedWidget3)")) <
                Files.SourceContent.Find(TEXT("->AddChildToOverlay(GeneratedWidget6)")));
        }
    }
    if (bFunctions)
    {
        TestTrue(TEXT("First normal function retained"), Files.SourceContent.Contains(TEXT("UConvertedLayout::RunFirst()")));
        TestTrue(TEXT("Second normal function retained"), Files.SourceContent.Contains(TEXT("UConvertedLayout::RunSecond()")));
    }
    FBPSemanticClassExtractor::Append(Blueprint, Semantic);
    FBPSemanticGraphTraceExtractor::Append(Graph, Semantic);
    FString Json;
    TestTrue(TEXT("Layout JSON serialized"), FBPSemanticJsonSerializer::Serialize(Semantic, FEngineVersion::Current().ToString(), Json));
    TSharedPtr<FJsonObject> RootJson;
    if (TestTrue(TEXT("Layout JSON parsed"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), RootJson)) == false ||
        RootJson.IsValid() == false) { return false; }
    const TArray<TSharedPtr<FJsonValue>>& Nodes = RootJson->GetObjectField(TEXT("widget_tree"))->GetArrayField(TEXT("nodes"));
    TestEqual(TEXT("JSON retains all nodes"), Nodes.Num(), Semantic.WidgetTree.Nodes.Num());
    if (bCanvas && bEmpty == false)
    {
        const TSharedPtr<FJsonObject> Canvas = Nodes[1]->AsObject()->GetObjectField(TEXT("canvas_slot"));
        TestEqual(TEXT("JSON retains ZOrder"), Canvas->GetIntegerField(TEXT("z_order")), -7);
        TestEqual(TEXT("JSON retains alignment precision"), Canvas->GetArrayField(TEXT("alignment"))[0]->AsNumber(), 0.1234567890123456);
    }
    FString ExportPath, Error;
    if (TestTrue(TEXT("Real layout JSON exported"), FBPSemanticJsonExporter::Export(
        Semantic, FEngineVersion::Current().ToString(), ExportPath, Error)) == false) { AddError(Error); return false; }
    AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));
    FBPSemanticIR Invalid = Semantic;
    Invalid.WidgetTree.Nodes[0].CanvasSlot = FBPSemanticCanvasSlot();
    TestFalse(TEXT("Root cannot have Canvas slot"), FBPSemanticCodeGenerator::Generate(Invalid, TEXT("UConvertedLayout"), FString(), Files, Diagnostics));
    if (bEmpty == false)
    {
        Invalid = Semantic;
        Invalid.WidgetTree.Nodes[1].bSizeFill = true;
        TestFalse(TEXT("Canvas and Overlay cannot contain box sizes"), FBPSemanticCodeGenerator::Generate(Invalid, TEXT("UConvertedLayout"), FString(), Files, Diagnostics));
        Invalid = Semantic;
        if (bCanvas) { Invalid.WidgetTree.Nodes[1].CanvasSlot.Reset(); }
        else { Invalid.WidgetTree.Nodes[1].CanvasSlot = FBPSemanticCanvasSlot(); }
        TestFalse(TEXT("Canvas payload must match parent kind"), FBPSemanticCodeGenerator::Generate(Invalid, TEXT("UConvertedLayout"), FString(), Files, Diagnostics));
        if (bCanvas)
        {
            Invalid = Semantic;
            Invalid.WidgetTree.Nodes[1].CanvasSlot.GetValue().AlignmentX = std::numeric_limits<double>::infinity();
            TestFalse(TEXT("Nonfinite Canvas layout rejected"), FBPSemanticCodeGenerator::Generate(Invalid, TEXT("UConvertedLayout"), FString(), Files, Diagnostics));
        }
    }
    TestTrue(TEXT("Rejected layout publishes no output"), Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    return true;
}

#endif
