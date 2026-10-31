#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPWidgetTreeLowering.h"
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
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Animation/WidgetAnimation.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBPWidgetTreeConversionTest,
    "BlueprintAIConverter.SemanticIR.Lowering.WidgetTree",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBPWidgetTreeConversionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const TCHAR* Name : { TEXT("Empty"), TEXT("TextBlock"), TEXT("LocalizedTextBlock"), TEXT("TextBlockFunctions"),
        TEXT("VerticalBox"), TEXT("HorizontalBox"), TEXT("NestedBoxes"), TEXT("NestedBoxesFunctions"), TEXT("EmptyBox") })
    {
        Names.Add(Name);
        Commands.Add(Name);
    }
}

bool FBPWidgetTreeConversionTest::RunTest(const FString& Parameters)
{
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(),
            UWidgetBlueprint::StaticClass(), TEXT("WBP_Conversion")), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), NAME_None));
    if (TestNotNull(TEXT("Real Widget Blueprint"), Blueprint) == false) { return false; }
    if (Blueprint->WidgetTree == nullptr)
    {
        Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
    }
    UTextBlock* Text = nullptr;
    const bool bPanel = Parameters.Contains(TEXT("Box"));
    const bool bNested = Parameters.StartsWith(TEXT("Nested"));
    const bool bFunctions = Parameters.EndsWith(TEXT("Functions"));
    if (Parameters != TEXT("Empty") && Parameters != TEXT("EmptyBox"))
    {
        Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Message"));
        Text->bIsVariable = false;
        Text->SetText(Parameters == TEXT("LocalizedTextBlock")
            ? NSLOCTEXT("WidgetConversion", "Greeting", "Localized greeting")
            : FText::AsCultureInvariant(TEXT("Hello \"Widget\"\n日本語")));
        Blueprint->WidgetTree->RootWidget = Text;
    }
    if (bPanel)
    {
        UPanelWidget* Panel = Parameters == TEXT("HorizontalBox")
            ? static_cast<UPanelWidget*>(Blueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Layout")))
            : static_cast<UPanelWidget*>(Blueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Layout")));
        Panel->bIsVariable = false;
        Blueprint->WidgetTree->RootWidget = Panel;
        if (Text != nullptr)
        {
            UPanelSlot* Slot = Panel->AddChild(Text);
            FSlateChildSize Size(ESlateSizeRule::Fill);
            Size.Value = 2.5f;
            if (UVerticalBoxSlot* Box = Cast<UVerticalBoxSlot>(Slot))
            {
                Box->SetPadding(FMargin(1, 2, 3, 4)); Box->SetSize(Size);
                Box->SetHorizontalAlignment(HAlign_Center); Box->SetVerticalAlignment(VAlign_Bottom);
            }
            else
            {
                UHorizontalBoxSlot* Horizontal = CastChecked<UHorizontalBoxSlot>(Slot);
                Horizontal->SetPadding(FMargin(1, 2, 3, 4)); Horizontal->SetSize(Size);
                Horizontal->SetHorizontalAlignment(HAlign_Center); Horizontal->SetVerticalAlignment(VAlign_Bottom);
            }
            UPanelWidget* SecondParent = Panel;
            if (bNested)
            {
                UHorizontalBox* Row = Blueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
                Row->bIsVariable = false;
                Panel->AddChild(Row);
                SecondParent = Row;
            }
            UTextBlock* Second = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Secondary"));
            Second->bIsVariable = false;
            Second->SetText(NSLOCTEXT("WidgetConversion", "Secondary", "Second label"));
            SecondParent->AddChild(Second);
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
            if (TestNotNull(TEXT("Widget normal function entry"), Entry) == false) { return false; }
            UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
            Graph->AddNode(Call, false, false);
            Call->CreateNewGuid();
            Call->PostPlacedNewNode();
            Call->SetFromFunction(UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("SetSubtitlesEnabled")));
            Call->AllocateDefaultPins();
            if (TestTrue(TEXT("Widget normal function call"), GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
                Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
                Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) == false) { return false; }
        }
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("Widget Blueprint compiled"), Blueprint->Status != BS_Error) == false) { return false; }
    FBPGraphIR Graph;
    TestTrue(TEXT("Widget graphs captured"), FBPGraphIRBuilder::Build(Blueprint, Graph));
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Widget payload and functions lowered"), FBPSemanticLowering::Lower(Blueprint, Graph, Semantic)) == false)
    {
        for (const FBPSemanticDiagnostic& Diagnostic : Semantic.Diagnostics) { AddError(Diagnostic.Message); }
        return false;
    }
    TestEqual(TEXT("Functions retained beside Widget Tree"), Semantic.Functions.Num(),
        bFunctions ? 2 : 0);
    TestEqual(TEXT("Tree mode"), Semantic.WidgetTree.Kind, bPanel ? EBPSemanticWidgetTreeKind::Panel :
        Text == nullptr ? EBPSemanticWidgetTreeKind::Empty : EBPSemanticWidgetTreeKind::TextBlock);
    if (bPanel)
    {
        TestEqual(TEXT("Hierarchy node count"), Semantic.WidgetTree.Nodes.Num(), Text == nullptr ? 1 : bNested ? 4 : 3);
        if (Text != nullptr)
        {
            TestEqual(TEXT("Sibling order"), Semantic.WidgetTree.Nodes[1].Name, FName(TEXT("Message")));
            TestEqual(TEXT("Slot padding"), Semantic.WidgetTree.Nodes[1].PaddingBottom, 4.0f);
            TestTrue(TEXT("Slot fill size"), Semantic.WidgetTree.Nodes[1].bSizeFill);
            TestEqual(TEXT("Slot weight"), Semantic.WidgetTree.Nodes[1].SizeValue, 2.5f);
        }
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Widget C++ generated"), FBPSemanticCodeGenerator::Generate(Semantic,
        TEXT("UConvertedWidget"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics) { AddError(Diagnostic); }
        return false;
    }
    TestTrue(TEXT("Widget inheritance"), Files.HeaderContent.Contains(TEXT("public UUserWidget")));
    TestTrue(TEXT("Native rebuilding hook"), Files.HeaderContent.Contains(TEXT("RebuildWidget() override")));
    TestTrue(TEXT("Super Slate rebuilding"), Files.SourceContent.Contains(TEXT("return Super::RebuildWidget();")));
    TestFalse(TEXT("No Actor lifecycle"), Files.SourceContent.Contains(TEXT("BeginPlay")));
    TestEqual(TEXT("TextBlock construction only for populated tree"),
        Files.SourceContent.Contains(TEXT("ConstructWidget<UTextBlock>")), Text != nullptr);
    if (Parameters == TEXT("LocalizedTextBlock"))
    {
        TestTrue(TEXT("Localization identity emitted"), Files.SourceContent.Contains(
            TEXT("NSLOCTEXT(\"WidgetConversion\", \"Greeting\", \"Localized greeting\")")));
    }
    if (bPanel && Text != nullptr)
    {
        TestTrue(TEXT("Slot padding emitted"), Files.SourceContent.Contains(TEXT("->SetPadding(FMargin(")));
        TestTrue(TEXT("Slot alignment emitted"), Files.SourceContent.Contains(TEXT("->SetVerticalAlignment(VAlign_Bottom)")));
        TestTrue(TEXT("Fill slot emitted"), Files.SourceContent.Contains(TEXT("ESlateSizeRule::Fill")));
        TestTrue(TEXT("Child attached through parent-specific API"), Files.SourceContent.Contains(
            Parameters == TEXT("HorizontalBox") ? TEXT("->AddChildToHorizontalBox(") : TEXT("->AddChildToVerticalBox(")));
        if (bNested)
        {
            TestTrue(TEXT("Nested horizontal panel emitted"), Files.SourceContent.Contains(TEXT("->AddChildToHorizontalBox(GeneratedWidget3)")));
        }
    }
    if (bFunctions)
    {
        TestTrue(TEXT("First normal function"), Files.SourceContent.Contains(TEXT("UConvertedWidget::RunFirst()")));
        TestTrue(TEXT("Second normal function"), Files.SourceContent.Contains(TEXT("UConvertedWidget::RunSecond()")));
    }
    FBPSemanticClassExtractor::Append(Blueprint, Semantic);
    FBPSemanticGraphTraceExtractor::Append(Graph, Semantic);
    FString Json;
    TestTrue(TEXT("Widget JSON serialized"), FBPSemanticJsonSerializer::Serialize(
        Semantic, FEngineVersion::Current().ToString(), Json));
    TestTrue(TEXT("Tree payload exported"), Json.Contains(TEXT("\"widget_tree\"")));
    FString RepeatedJson;
    TestTrue(TEXT("Widget JSON can be serialized again"), FBPSemanticJsonSerializer::Serialize(
        Semantic, FEngineVersion::Current().ToString(), RepeatedJson));
    TestEqual(TEXT("Widget JSON is deterministic"), RepeatedJson, Json);
    if (bPanel)
    {
        TSharedPtr<FJsonObject> RootJson;
        if (TestTrue(TEXT("Panel JSON reparsed"), FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Json), RootJson)) == false || RootJson.IsValid() == false) { return false; }
        const TArray<TSharedPtr<FJsonValue>>& Nodes = RootJson->GetObjectField(TEXT("widget_tree"))->GetArrayField(TEXT("nodes"));
        TestEqual(TEXT("JSON retains all panel nodes"), Nodes.Num(), Semantic.WidgetTree.Nodes.Num());
        TestEqual(TEXT("JSON panel kind"), Nodes[0]->AsObject()->GetStringField(TEXT("kind")),
            Parameters == TEXT("HorizontalBox") ? FString(TEXT("horizontal_box")) : FString(TEXT("vertical_box")));
        if (Text != nullptr)
        {
            TestEqual(TEXT("JSON child order"), Nodes[1]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("Message")));
            TestEqual(TEXT("JSON fill size"), Nodes[1]->AsObject()->GetNumberField(TEXT("size_value")), 2.5);
            TestEqual(TEXT("JSON parent link"), Nodes.Last()->AsObject()->GetIntegerField(TEXT("parent_index")), bNested ? 2 : 0);
        }
    }
    FString ExportPath;
    FString Error;
    if (TestTrue(TEXT("Real Widget export artifact"), FBPSemanticJsonExporter::Export(
        Semantic, FEngineVersion::Current().ToString(), ExportPath, Error)) == false)
    {
        AddError(Error);
        return false;
    }
    AddInfo(FString::Printf(TEXT("SEMANTIC_ROUNDTRIP_ARTIFACT=%s"), *ExportPath));

    Blueprint->Bindings.AddDefaulted();
    FBPSemanticIR Rejected;
    TestFalse(TEXT("Bindings cannot be discarded"), FBPSemanticLowering::Lower(Blueprint, Graph, Rejected));
    TestTrue(TEXT("Rejected asset publishes no payload"), Rejected.WidgetTree.Kind == EBPSemanticWidgetTreeKind::None && Rejected.Functions.IsEmpty());
    Blueprint->Bindings.Reset();
    Blueprint->Animations.Add(NewObject<UWidgetAnimation>(Blueprint));
    TestFalse(TEXT("Animations cannot be discarded"), FBPSemanticLowering::Lower(Blueprint, Graph, Rejected));
    Blueprint->Animations.Reset();
    if (Text != nullptr)
    {
        Text->SetVisibility(ESlateVisibility::Hidden);
        TestFalse(TEXT("Unimplemented runtime properties cannot be discarded"),
            FBPSemanticLowering::Lower(Blueprint, Graph, Rejected));
        TestTrue(TEXT("Property rejection is specific"), Rejected.Diagnostics.ContainsByPredicate(
            [](const FBPSemanticDiagnostic& Diagnostic) { return Diagnostic.Message.Contains(TEXT("Visibility")); }));
        Text->SetVisibility(GetDefault<UTextBlock>()->GetVisibility());
        if (bPanel)
        {
            UPanelWidget* Panel = CastChecked<UPanelWidget>(Blueprint->WidgetTree->RootWidget);
            UButton* Unsupported = Blueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("UnsupportedButton"));
            Unsupported->bIsVariable = false;
            Panel->AddChild(Unsupported);
            TestFalse(TEXT("Unsupported child class cannot be silently discarded"),
                FBPSemanticLowering::Lower(Blueprint, Graph, Rejected));
            TestTrue(TEXT("Unsupported child refusal identifies class"), Rejected.Diagnostics.ContainsByPredicate(
                [](const FBPSemanticDiagnostic& Diagnostic) { return Diagnostic.Message.Contains(TEXT("Unsupported Widget Tree class")); }));
            Panel->RemoveChild(Unsupported);
        }
    }
    FBPSemanticIR Invalid = Semantic;
    Invalid.ParentClassPath = TEXT("/Script/CoreUObject.Object");
    TestFalse(TEXT("Widget payload requires Widget parent"), FBPSemanticCodeGenerator::Generate(
        Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
    TestTrue(TEXT("Failure clears generated output"), Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    Invalid = Semantic;
    FBPSemanticVariable Collision;
    Collision.Name = TEXT("RebuildWidget");
    Invalid.Variables.Add(Collision);
    TestFalse(TEXT("Generated rebuilding hook cannot collide with a member"), FBPSemanticCodeGenerator::Generate(
        Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
    if (Semantic.WidgetTree.Kind == EBPSemanticWidgetTreeKind::TextBlock)
    {
        Invalid = Semantic;
        Invalid.WidgetTree.RootName = TEXT("Bad\"Name");
        TestFalse(TEXT("Unsafe widget name rejected before publishing"), FBPSemanticCodeGenerator::Generate(
            Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
        Invalid = Semantic;
        Invalid.WidgetTree.bTextCultureInvariant = true;
        Invalid.WidgetTree.bTextLocalizationIdentityAvailable = true;
        TestFalse(TEXT("Conflicting text identity rejected"), FBPSemanticCodeGenerator::Generate(
            Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
    }
    if (bPanel)
    {
        Invalid = Semantic;
        Invalid.WidgetTree.Nodes[0].ParentIndex = 0;
        TestFalse(TEXT("Cyclic root rejected"), FBPSemanticCodeGenerator::Generate(
            Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
        if (Text != nullptr)
        {
            Invalid = Semantic;
            Invalid.WidgetTree.Nodes[1].Name = Invalid.WidgetTree.Nodes[0].Name;
            TestFalse(TEXT("Duplicate names rejected"), FBPSemanticCodeGenerator::Generate(
                Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
            Invalid = Semantic;
            Invalid.WidgetTree.Nodes[1].SizeValue = -1;
            TestFalse(TEXT("Invalid slot weight rejected"), FBPSemanticCodeGenerator::Generate(
                Invalid, TEXT("UConvertedWidget"), FString(), Files, Diagnostics));
        }
    }
    return true;
}

#endif
