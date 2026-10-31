#include "BPWidgetTreeLowering.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Internationalization/Text.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
bool ExtractText(const FText& Text, FBPSemanticWidgetText& Tree, TArray<FString>& Diagnostics)
{
    Tree.Text = Text.ToString();
    Tree.bTextCultureInvariant = Text.IsCultureInvariant();
    const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
    const TOptional<FString> Key = FTextInspector::GetKey(Text);
    const FString* Source = FTextInspector::GetSourceString(Text);
    if (Tree.bTextCultureInvariant == false)
    {
        if (Text.IsFromStringTable())
        {
            Diagnostics.Add(TEXT("TextBlock string-table text is not yet supported"));
            return false;
        }
        if (Namespace.IsSet() && Key.IsSet() && Key.GetValue().IsEmpty() == false && Source != nullptr)
        {
            Tree.bTextLocalizationIdentityAvailable = true;
            Tree.TextNamespace = Namespace.GetValue();
            Tree.TextKey = Key.GetValue();
            Tree.TextSource = *Source;
        }
        else if (Text.IsEmpty() == false)
        {
            Diagnostics.Add(TEXT("TextBlock text requires culture-invariant text or a stable localization identity"));
            return false;
        }
    }
    return true;
}

bool AuditProperties(const UObject* Object, const TSet<FName>& Supported,
    const FString& Label, TArray<FString>& Diagnostics)
{
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_EditorOnly) ||
            Supported.Contains(It->GetFName())) { continue; }
        if (It->Identical_InContainer(Object, Object->GetClass()->GetDefaultObject()) == false)
        {
            Diagnostics.Add(FString::Printf(TEXT("%s property is not yet supported: %s"), *Label, *It->GetName()));
            return false;
        }
    }
    return true;
}

FString HorizontalName(EHorizontalAlignment Value)
{
    switch (Value)
    {
    case HAlign_Fill: return TEXT("fill");
    case HAlign_Left: return TEXT("left");
    case HAlign_Center: return TEXT("center");
    case HAlign_Right: return TEXT("right");
    default: return TEXT("invalid");
    }
}

FString VerticalName(EVerticalAlignment Value)
{
    switch (Value)
    {
    case VAlign_Fill: return TEXT("fill");
    case VAlign_Top: return TEXT("top");
    case VAlign_Center: return TEXT("center");
    case VAlign_Bottom: return TEXT("bottom");
    default: return TEXT("invalid");
    }
}

bool ExtractPanelTree(UWidget* Root, FBPSemanticWidgetTree& Tree, TArray<FString>& Diagnostics)
{
    Tree.Kind = EBPSemanticWidgetTreeKind::Panel;
    Tree.RootName = Root->GetFName();
    TSet<UWidget*> Visited;
    TSet<FName> Names;
    TArray<UWidget*> SourceNodes;
    TFunction<bool(UWidget*, int32, int32)> Visit;
    Visit = [&](UWidget* Widget, int32 ParentIndex, int32 Depth)
    {
        if (Widget == nullptr || Visited.Contains(Widget) || Names.Contains(Widget->GetFName()) ||
            Widget->bIsVariable || Depth > 64 || Tree.Nodes.Num() >= 256)
        {
            Diagnostics.Add(TEXT("Invalid, shared, variable or excessive Widget Tree node"));
            return false;
        }
        Visited.Add(Widget);
        Names.Add(Widget->GetFName());
        FBPSemanticWidgetNode Node;
        Node.Name = Widget->GetFName();
        Node.ParentIndex = ParentIndex;
        if (Widget->GetClass() == UTextBlock::StaticClass())
        {
            Node.Kind = EBPSemanticWidgetNodeKind::TextBlock;
            if (AuditProperties(Widget, {TEXT("Text"), TEXT("Slot")}, TEXT("TextBlock"), Diagnostics) == false ||
                ExtractText(CastChecked<UTextBlock>(Widget)->GetText(), Node, Diagnostics) == false) { return false; }
        }
        else if (Widget->GetClass() == UVerticalBox::StaticClass() || Widget->GetClass() == UHorizontalBox::StaticClass() ||
            Widget->GetClass() == UOverlay::StaticClass() || Widget->GetClass() == UCanvasPanel::StaticClass())
        {
            Node.Kind = Widget->GetClass() == UVerticalBox::StaticClass() ? EBPSemanticWidgetNodeKind::VerticalBox :
                Widget->GetClass() == UHorizontalBox::StaticClass() ? EBPSemanticWidgetNodeKind::HorizontalBox :
                Widget->GetClass() == UOverlay::StaticClass() ? EBPSemanticWidgetNodeKind::Overlay : EBPSemanticWidgetNodeKind::CanvasPanel;
            if (AuditProperties(Widget, {TEXT("Slots"), TEXT("Slot")}, TEXT("Panel"), Diagnostics) == false) { return false; }
        }
        else
        {
            Diagnostics.Add(FString::Printf(TEXT("Unsupported Widget Tree class: %s"), *Widget->GetClass()->GetPathName()));
            return false;
        }
        if (ParentIndex == INDEX_NONE)
        {
            if (Widget->Slot != nullptr) { Diagnostics.Add(TEXT("Root Widget cannot have a slot")); return false; }
        }
        else
        {
            UPanelSlot* Slot = Widget->Slot;
            const EBPSemanticWidgetNodeKind ParentKind = Tree.Nodes[ParentIndex].Kind;
            const bool bVertical = ParentKind == EBPSemanticWidgetNodeKind::VerticalBox;
            const bool bOverlay = ParentKind == EBPSemanticWidgetNodeKind::Overlay;
            const bool bCanvas = ParentKind == EBPSemanticWidgetNodeKind::CanvasPanel;
            const UClass* ExpectedSlot = bVertical ? UVerticalBoxSlot::StaticClass() :
                bOverlay ? UOverlaySlot::StaticClass() : bCanvas ? UCanvasPanelSlot::StaticClass() : UHorizontalBoxSlot::StaticClass();
            if (Slot == nullptr || Widget->GetParent() != SourceNodes[ParentIndex] || Slot->GetClass() != ExpectedSlot)
            {
                Diagnostics.Add(TEXT("Widget slot does not match its parent panel"));
                return false;
            }
            FMargin Padding(0);
            FSlateChildSize Size(ESlateSizeRule::Automatic);
            Size.Value = 1.0f;
            if (bCanvas)
            {
                if (AuditProperties(Slot, {TEXT("Parent"), TEXT("Content"), TEXT("LayoutData"), TEXT("bAutoSize"),
                    TEXT("ZOrder")}, TEXT("Canvas slot"), Diagnostics) == false) { return false; }
                const UCanvasPanelSlot* Canvas = CastChecked<UCanvasPanelSlot>(Slot);
                const FMargin Offsets = Canvas->GetOffsets();
                const FAnchors Anchors = Canvas->GetAnchors();
                const FVector2D Alignment = Canvas->GetAlignment();
                FBPSemanticCanvasSlot Layout;
                Layout.OffsetLeft = Offsets.Left; Layout.OffsetTop = Offsets.Top;
                Layout.OffsetRight = Offsets.Right; Layout.OffsetBottom = Offsets.Bottom;
                Layout.AnchorMinimumX = Anchors.Minimum.X; Layout.AnchorMinimumY = Anchors.Minimum.Y;
                Layout.AnchorMaximumX = Anchors.Maximum.X; Layout.AnchorMaximumY = Anchors.Maximum.Y;
                Layout.AlignmentX = Alignment.X; Layout.AlignmentY = Alignment.Y;
                Layout.bAutoSize = Canvas->GetAutoSize(); Layout.ZOrder = Canvas->GetZOrder();
                if (FMath::IsFinite(Layout.OffsetLeft) == false || FMath::IsFinite(Layout.OffsetTop) == false ||
                    FMath::IsFinite(Layout.OffsetRight) == false || FMath::IsFinite(Layout.OffsetBottom) == false ||
                    FMath::IsFinite(Layout.AnchorMinimumX) == false || FMath::IsFinite(Layout.AnchorMinimumY) == false ||
                    FMath::IsFinite(Layout.AnchorMaximumX) == false || FMath::IsFinite(Layout.AnchorMaximumY) == false ||
                    FMath::IsFinite(Layout.AlignmentX) == false || FMath::IsFinite(Layout.AlignmentY) == false)
                {
                    Diagnostics.Add(TEXT("Invalid Canvas slot layout values"));
                    return false;
                }
                Node.CanvasSlot = Layout;
            }
            else
            {
                TSet<FName> Supported = {TEXT("Parent"), TEXT("Content"), TEXT("Padding"),
                    TEXT("HorizontalAlignment"), TEXT("VerticalAlignment")};
                if (bOverlay == false) { Supported.Add(TEXT("Size")); }
                if (AuditProperties(Slot, Supported, TEXT("Panel slot"), Diagnostics) == false) { return false; }
                if (bOverlay)
                {
                    const UOverlaySlot* Overlay = CastChecked<UOverlaySlot>(Slot);
                    Padding = Overlay->GetPadding();
                    Node.HorizontalAlignment = HorizontalName(Overlay->GetHorizontalAlignment());
                    Node.VerticalAlignment = VerticalName(Overlay->GetVerticalAlignment());
                }
                else if (bVertical)
                {
                    const UVerticalBoxSlot* BoxSlot = CastChecked<UVerticalBoxSlot>(Slot);
                    Padding = BoxSlot->GetPadding(); Size = BoxSlot->GetSize();
                    Node.HorizontalAlignment = HorizontalName(BoxSlot->GetHorizontalAlignment());
                    Node.VerticalAlignment = VerticalName(BoxSlot->GetVerticalAlignment());
                }
                else
                {
                    const UHorizontalBoxSlot* BoxSlot = CastChecked<UHorizontalBoxSlot>(Slot);
                    Padding = BoxSlot->GetPadding(); Size = BoxSlot->GetSize();
                    Node.HorizontalAlignment = HorizontalName(BoxSlot->GetHorizontalAlignment());
                    Node.VerticalAlignment = VerticalName(BoxSlot->GetVerticalAlignment());
                }
            }
            Node.PaddingLeft = Padding.Left; Node.PaddingTop = Padding.Top;
            Node.PaddingRight = Padding.Right; Node.PaddingBottom = Padding.Bottom;
            Node.bSizeFill = Size.SizeRule == ESlateSizeRule::Fill;
            Node.SizeValue = Size.Value;
            if ((Size.SizeRule != ESlateSizeRule::Automatic && Size.SizeRule != ESlateSizeRule::Fill) ||
                Node.HorizontalAlignment == TEXT("invalid") || Node.VerticalAlignment == TEXT("invalid") ||
                FMath::IsFinite(Size.Value) == false || Size.Value < 0 ||
                FMath::IsFinite(Padding.Left) == false || FMath::IsFinite(Padding.Top) == false ||
                FMath::IsFinite(Padding.Right) == false || FMath::IsFinite(Padding.Bottom) == false)
            {
                Diagnostics.Add(TEXT("Invalid panel slot layout values"));
                return false;
            }
        }
        const int32 Index = Tree.Nodes.Add(MoveTemp(Node));
        SourceNodes.Add(Widget);
        if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
            {
                if (Visit(Panel->GetChildAt(ChildIndex), Index, Depth + 1) == false) { return false; }
            }
        }
        return true;
    };
    return Visit(Root, INDEX_NONE, 0);
}
}

bool FBPWidgetTreeLowering::Extract(const UBlueprint* Blueprint,
    FBPSemanticWidgetTree& OutTree, TArray<FString>& OutDiagnostics)
{
    OutTree = FBPSemanticWidgetTree();
    if (Blueprint == nullptr || Blueprint->ParentClass == nullptr ||
        Blueprint->ParentClass->IsChildOf(UUserWidget::StaticClass()) == false)
    {
        return true;
    }
    const UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(Blueprint);
    if (Widget == nullptr || Blueprint->ParentClass != UUserWidget::StaticClass())
    {
        OutDiagnostics.Add(TEXT("Widget conversion currently requires a UWidgetBlueprint with direct UUserWidget parent"));
        return false;
    }
    if (Widget->Bindings.IsEmpty() == false || Widget->Animations.IsEmpty() == false)
    {
        OutDiagnostics.Add(TEXT("Widget bindings and animations are not yet supported by Widget Tree conversion"));
        return false;
    }
    for (TFieldIterator<FArrayProperty> It(Widget->GetClass()); It; ++It)
    {
        if (It->GetName().Contains(TEXT("Extension")))
        {
            FScriptArrayHelper Extensions(*It, It->ContainerPtrToValuePtr<void>(Widget));
            if (Extensions.Num() != 0)
            {
                OutDiagnostics.Add(TEXT("Widget Blueprint extensions require specialized conversion"));
                return false;
            }
        }
    }
    if (Widget->GeneratedClass == nullptr || Widget->Status == BS_Error)
    {
        OutDiagnostics.Add(TEXT("Widget Blueprint must be compiled before conversion"));
        return false;
    }
    const UObject* WidgetDefaults = Widget->GeneratedClass->GetDefaultObject();
    const UObject* ParentDefaults = UUserWidget::StaticClass()->GetDefaultObject();
    for (TFieldIterator<FProperty> It(UUserWidget::StaticClass()); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_EditorOnly) ||
            It->GetFName() == TEXT("WidgetTree")) { continue; }
        if (It->Identical_InContainer(WidgetDefaults, ParentDefaults) == false)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("UserWidget class default is not yet supported: %s"),
                *It->GetName()));
            return false;
        }
    }
    if (Widget->WidgetTree == nullptr)
    {
        OutDiagnostics.Add(TEXT("Widget Blueprint has no source Widget Tree to audit"));
        return false;
    }
    TArray<UWidget*> Widgets;
    UWidget* Root = Widget->WidgetTree->RootWidget;
    if (Root == nullptr)
    {
        Widget->WidgetTree->GetAllWidgets(Widgets);
        if (Widgets.IsEmpty() == false)
        {
            OutDiagnostics.Add(TEXT("A rootless Widget Tree contains widgets"));
            return false;
        }
        OutTree.Kind = EBPSemanticWidgetTreeKind::Empty;
        return true;
    }
    if (Root->GetClass() == UVerticalBox::StaticClass() || Root->GetClass() == UHorizontalBox::StaticClass() ||
        Root->GetClass() == UOverlay::StaticClass() || Root->GetClass() == UCanvasPanel::StaticClass())
    {
        FBPSemanticWidgetTree Tree;
        if (ExtractPanelTree(Root, Tree, OutDiagnostics) == false) { return false; }
        Widget->WidgetTree->GetAllWidgets(Widgets);
        if (Tree.Nodes.Num() != Widgets.Num())
        {
            OutDiagnostics.Add(TEXT("Widget Tree contains nodes outside the supported hierarchy"));
            return false;
        }
        OutTree = MoveTemp(Tree);
        return true;
    }
    Widget->WidgetTree->GetAllWidgets(Widgets);
    if (Root->GetClass() != UTextBlock::StaticClass() ||
        Widgets.Num() != 1 || Root->Slot != nullptr || Root->bIsVariable)
    {
        OutDiagnostics.Add(TEXT("Widget root must be a supported panel or a single non-variable TextBlock without slots"));
        return false;
    }
    // Only Text is emitted in this stage. Reject every edited runtime property
    // outside that contract rather than silently replacing it with a CDO value.
    const UObject* Defaults = Root->GetClass()->GetDefaultObject();
    for (TFieldIterator<FProperty> It(Root->GetClass()); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_EditorOnly) ||
            Property->GetFName() == TEXT("Text"))
        {
            continue;
        }
        if (Property->Identical_InContainer(Root, Defaults) == false)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("TextBlock property is not yet supported: %s"),
                *Property->GetName()));
            return false;
        }
    }
    FBPSemanticWidgetTree Tree;
    Tree.Kind = EBPSemanticWidgetTreeKind::TextBlock;
    Tree.RootName = Root->GetFName();
    if (ExtractText(CastChecked<UTextBlock>(Root)->GetText(), Tree, OutDiagnostics) == false) { return false; }
    OutTree = MoveTemp(Tree);
    return true;
}
