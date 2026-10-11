#include "BPGeneratedCodePreview.h"
#include "BPGeneratedCodeSaver.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

void FBPGeneratedCodePreview::Show(const FBPGeneratedActorFiles& Files,
    const FBPGraphIR& IR, const FString& ClassName)
{
    // 生成結果は値として保持し、元Blueprintの変更・破棄の影響を受けない。
    const FString Header = Files.HeaderContent;
    const FString Source = Files.SourceContent;
    const FString Combined = FString::Printf(
        TEXT("// ===== %s =====\n%s\n// ===== %s =====\n%s"),
        *Files.HeaderFileName, *Header, *Files.SourceFileName, *Source);

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Blueprint to C++ - Generated Code Preview")))
        .ClientSize(FVector2D(1000.0f, 750.0f))
        .SupportsMaximize(true)
        .SupportsMinimize(true);

    Window->SetContent(
        SNew(SBorder)
        .Padding(12.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(
                    TEXT("Review generated C++ before use. Save New Files never overwrites existing files.")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Copy Header")))
                    .OnClicked_Lambda([Header]()
                    {
                        FPlatformApplicationMisc::ClipboardCopy(*Header);
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Copy Source")))
                    .OnClicked_Lambda([Source]()
                    {
                        FPlatformApplicationMisc::ClipboardCopy(*Source);
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Save New Files...")))
                    .ToolTipText(FText::FromString(TEXT("Choose an existing folder. Existing files are never overwritten.")))
                    .OnClicked_Lambda([Files, Window]()
                    {
                        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
                        if (DesktopPlatform == nullptr)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Folder picker is unavailable.")));
                            return FReply::Handled();
                        }

                        FString Directory;
                        const bool bSelected = DesktopPlatform->OpenDirectoryDialog(
                            Window->GetNativeWindow().IsValid()
                                ? Window->GetNativeWindow()->GetOSWindowHandle() : nullptr,
                            TEXT("Choose an existing folder for generated C++ files"),
                            FPaths::ProjectDir(), Directory);
                        if (bSelected == false)
                        {
                            return FReply::Handled();
                        }

                        const EAppReturnType::Type Confirmation = FMessageDialog::Open(
                            EAppMsgType::YesNo,
                            FText::FromString(FString::Printf(
                                TEXT("Create %s and %s in:\n%s\n\nExisting files will not be overwritten. Continue?"),
                                *Files.HeaderFileName, *Files.SourceFileName, *Directory)));
                        if (Confirmation != EAppReturnType::Yes)
                        {
                            return FReply::Handled();
                        }

                        FString Error;
                        if (FBPGeneratedCodeSaver::SaveNewFiles(Files, Directory, Error) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
                        }
                        else
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Both generated files were saved successfully.")));
                        }
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Save to Module...")))
                    .OnClicked_Lambda([IR, ClassName, Window]()
                    {
                        IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
                        if (Desktop == nullptr)
                        {
                            return FReply::Handled();
                        }
                        FString ModuleRoot;
                        if (Desktop->OpenDirectoryDialog(nullptr,
                                TEXT("Select a Source module folder containing ModuleName.Build.cs"),
                                FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")), ModuleRoot) == false)
                        {
                            return FReply::Handled();
                        }
                        FPaths::NormalizeDirectoryName(ModuleRoot);
                        const FString ModuleName = FPaths::GetCleanFilename(ModuleRoot);
                        const FString ProjectSource = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")));
                        FString Root = FPaths::ConvertRelativePathToFull(ModuleRoot);
                        FPaths::NormalizeDirectoryName(Root);
                        // 選択されたパスを必ずProject/Source配下に限定する。
                        FString Prefix = ProjectSource;
                        FPaths::NormalizeDirectoryName(Prefix);
                        Prefix += TEXT("/");
                        if (Root.StartsWith(Prefix) == false ||
                            IFileManager::Get().FileExists(*FPaths::Combine(Root, ModuleName + TEXT(".Build.cs"))) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Select an existing module under Project/Source.")));
                            return FReply::Handled();
                        }
                        // 公開ヘッダーの参照関係を保つため、両ディレクトリは既存のみ許可する。
                        const FString HeaderDir = FPaths::Combine(Root, TEXT("Public"));
                        const FString SourceDir = FPaths::Combine(Root, TEXT("Private"));
                        FBPGeneratedActorFiles ModuleFiles;
                        TArray<FString> Diagnostics;
                        if (FBPActorCodeGenerator::Generate(IR, ClassName,
                                ModuleName.ToUpper() + TEXT("_API"), ModuleFiles, Diagnostics) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(FString::Join(Diagnostics, TEXT("\\n"))));
                            return FReply::Handled();
                        }
                        if (FMessageDialog::Open(EAppMsgType::YesNo,
                                FText::FromString(FString::Printf(
                                    TEXT("Save %s to Public and %s to Private of module %s? Existing files will not be overwritten."),
                                    *ModuleFiles.HeaderFileName, *ModuleFiles.SourceFileName, *ModuleName))) != EAppReturnType::Yes)
                        {
                            return FReply::Handled();
                        }
                        FString Error;
                        if (FBPGeneratedCodeSaver::SaveNewFilesSplit(
                                ModuleFiles, HeaderDir, SourceDir, Error) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
                        }
                        else
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("C++ files saved to module Public and Private.")));
                        }
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Copy Both")))
                    .OnClicked_Lambda([Combined]()
                    {
                        FPlatformApplicationMisc::ClipboardCopy(*Combined);
                        return FReply::Handled();
                    })
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Files.HeaderFileName))
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0.0f, 4.0f, 0.0f, 12.0f)
            [
                SNew(SMultiLineEditableTextBox)
                .Text(FText::FromString(Header))
                .IsReadOnly(true)
                .AlwaysShowScrollbars(true)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Files.SourceFileName))
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0.0f, 4.0f, 0.0f, 0.0f)
            [
                SNew(SMultiLineEditableTextBox)
                .Text(FText::FromString(Source))
                .IsReadOnly(true)
                .AlwaysShowScrollbars(true)
            ]
        ]);

    FSlateApplication::Get().AddWindow(Window);
}
