#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Blueprint.h"
#include "BPSnapshotExporter.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticVariableExtractor.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "BPCopilotGraphJsonExporter.h"
#include "Misc/EngineVersion.h"
#include "BPGraphIR.h"
#include "BPActorCodeGenerator.h"
#include "BPSemanticParityChecker.h"
#include "BPSemanticLowering.h"
#include "BPSemanticCodeGenerator.h"
#include "BPGeneratedCodePreview.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/MessageDialog.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintAIConverter, Log, All);

class FBlueprintAIConverterModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintAIConverterModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (UToolMenus::IsToolMenuUIEnabled())
        {
            UToolMenus::UnRegisterStartupCallback(this);
            UToolMenus::UnregisterOwner(this);
        }
    }

private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu"));
        if (Menu == nullptr)
        {
            return;
        }
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("BlueprintAIConverter"));
        Section.AddDynamicEntry(TEXT("ExportBlueprintSnapshot"), FNewToolMenuSectionDelegate::CreateLambda(
            [](FToolMenuSection& InSection)
            {
                const auto* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
                if (Context == nullptr)
                {
                    return;
                }
                bool bHasBlueprint = false;
                for (const FAssetData& Asset : Context->SelectedAssets)
                {
                    if (Cast<UBlueprint>(Asset.GetAsset()) != nullptr)
                    {
                        bHasBlueprint = true;
                        break;
                    }
                }
                if (bHasBlueprint == false)
                {
                    return;
                }
                // 既存のJSON Exportとは独立した、C++直接解析の最小Editor導線。
                // ファイルへの書き込みは行わず、生成結果をSlate画面で確認する。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.CopyGeneratedCpp"),
                    FText::FromString(TEXT("Convert to C++ (Preview)")),
                    FText::FromString(TEXT("Preview generated Actor C++ files in a read-only window (no files written)")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (Ctx == nullptr)
                        {
                            return;
                        }
                        // 複数選択時に別のBlueprintを誤って変換しない。
                        if (Ctx->SelectedAssets.Num() != 1)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Select exactly one Blueprint for C++ preview.")));
                            return;
                        }
                        UBlueprint* Blueprint = Cast<UBlueprint>(Ctx->SelectedAssets[0].GetAsset());
                        if (Blueprint == nullptr)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("The selected asset is not a Blueprint.")));
                            return;
                        }

                        FBPGraphIR IR;
                        if (FBPGraphIRBuilder::Build(Blueprint, IR) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(FString::Join(IR.Diagnostics, TEXT("\\n"))));
                            return;
                        }

                        // クラス名はBlueprintのアセット名から作り、識別子として不正な名前は
                        // Generator側で拒否する。勝手に置換するとクラス名の衝突が起こり得る。
                        const FString ClassName = TEXT("A") + Blueprint->GetName() + TEXT("Generated");
                        FBPGeneratedActorFiles Files;
                        TArray<FString> Diagnostics;
                        // 同名ゲームモジュールのBuild.csが存在する場合だけAPIマクロを付ける。
                        // 推測した名前で別モジュールのAPIを参照しない。
                        const FString ModuleName = FApp::GetProjectName();
                        const FString BuildRules = FPaths::Combine(
                            FPaths::ProjectDir(), TEXT("Source"), ModuleName, ModuleName + TEXT(".Build.cs"));
                        const FString ApiMacro = IFileManager::Get().FileExists(*BuildRules)
                            ? ModuleName.ToUpper() + TEXT("_API") : FString();
                        if (FBPActorCodeGenerator::Generate(IR, ClassName, ApiMacro, Files, Diagnostics) == false)
                        {
                            const FString Message = FString::Join(Diagnostics, TEXT("\\n"));
                            UE_LOG(LogBlueprintAIConverter, Warning, TEXT("C++ preview failed: %s"), *Message);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
                            return;
                        }

                        // Shadow Mode: 旧Generatorのプレビュー結果はそのまま使用し、
                        // Semantic経路との差異のみOutput Logへ記録する。差異があっても保存先は変えない。
                        TArray<FString> ParityDiagnostics;
                        if (FBPSemanticParityChecker::Compare(IR, ClassName, ApiMacro, Files, ParityDiagnostics))
                        {
                            UE_LOG(LogBlueprintAIConverter, Display,
                                TEXT("Semantic IR parity passed for %s"), *Blueprint->GetName());
                        }
                        else
                        {
                            UE_LOG(LogBlueprintAIConverter, Warning,
                                TEXT("Semantic IR parity failed for %s: %s"),
                                *Blueprint->GetName(), *FString::Join(ParityDiagnostics, TEXT("; ")));
                        }

                        // 生成内容は読み取り専用の別ウィンドウで表示する。
                        // コピー操作はウィンドウ内の個別ボタンから行える。
                        FBPGeneratedCodePreview::Show(Files, IR, ClassName);
                        UE_LOG(LogBlueprintAIConverter, Display,
                            TEXT("C++ preview opened for %s: %s, %s"),
                            *Blueprint->GetName(), *Files.HeaderFileName, *Files.SourceFileName);
                    }));

                // Semantic IRを明示的に試す独立プレビュー。
                // 旧プレビューを既定経路として維持し、段階的に新経路を検証する。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.SemanticPreview"),
                    FText::FromString(TEXT("Convert to C++ (Semantic Preview)")),
                    FText::FromString(TEXT("Preview C++ generated via Semantic IR (no files written)")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (Ctx == nullptr)
                        {
                            return;
                        }
                        if (Ctx->SelectedAssets.Num() != 1)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Select exactly one Blueprint for C++ preview.")));
                            return;
                        }
                        UBlueprint* Blueprint = Cast<UBlueprint>(Ctx->SelectedAssets[0].GetAsset());
                        if (Blueprint == nullptr)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("The selected asset is not a Blueprint.")));
                            return;
                        }

                        FBPGraphIR GraphIR;
                        if (FBPGraphIRBuilder::Build(Blueprint, GraphIR) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(FString::Join(GraphIR.Diagnostics, TEXT("\\n"))));
                            return;
                        }

                        FBPSemanticIR SemanticIR;
                        if (FBPSemanticLowering::LowerPrintString(Blueprint, GraphIR, SemanticIR) == false)
                        {
                            TArray<FString> Messages;
                            for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
                            {
                                Messages.Add(Diagnostic.Message);
                            }
                            const FString Message = FString::Join(Messages, TEXT("\\n"));
                            UE_LOG(LogBlueprintAIConverter, Warning, TEXT("Semantic preview Lowering failed: %s"), *Message);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
                            return;
                        }

                        const FString ClassName = TEXT("A") + Blueprint->GetName() + TEXT("Generated");
                        const FString ModuleName = FApp::GetProjectName();
                        const FString BuildRules = FPaths::Combine(
                            FPaths::ProjectDir(), TEXT("Source"), ModuleName, ModuleName + TEXT(".Build.cs"));
                        const FString ApiMacro = IFileManager::Get().FileExists(*BuildRules)
                            ? ModuleName.ToUpper() + TEXT("_API") : FString();

                        FBPGeneratedActorFiles SemanticFiles;
                        TArray<FString> Diagnostics;
                        if (FBPSemanticCodeGenerator::Generate(
                            SemanticIR, ClassName, ApiMacro, SemanticFiles, Diagnostics,
                            EBPVariableFallbackMode::DeclarationOnly) == false)
                        {
                            const FString Message = FString::Join(Diagnostics, TEXT("\\n"));
                            UE_LOG(LogBlueprintAIConverter, Warning, TEXT("Semantic preview generation failed: %s"), *Message);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
                            return;
                        }

                        // Preview画面は既存のものを再利用し、保存時の安全性チェックも共有する。
                        FBPGeneratedCodePreview::Show(SemanticFiles, GraphIR, ClassName);
                        UE_LOG(LogBlueprintAIConverter, Display,
                            TEXT("Semantic C++ preview opened for %s"), *Blueprint->GetName());
                    }));

                // 保存前に内容を確認できるよう、JSON文字列のコピー機能を別途提供する。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.CopySemanticJson"),
                    FText::FromString(TEXT("Copy Semantic JSON v2 (Preview)")),
                    FText::FromString(TEXT("Copy Semantic JSON v2 to clipboard without writing files")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (Ctx == nullptr)
                        {
                            return;
                        }
                        if (Ctx->SelectedAssets.Num() != 1)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Select exactly one Blueprint.")));
                            return;
                        }
                        UBlueprint* Blueprint = Cast<UBlueprint>(Ctx->SelectedAssets[0].GetAsset());
                        if (Blueprint == nullptr)
                        {
                            return;
                        }
                        FBPGraphIR GraphIR;
                        FBPSemanticIR SemanticIR;
                        if (FBPGraphIRBuilder::Build(Blueprint, GraphIR))
                        {
                            FBPSemanticLowering::LowerPrintString(Blueprint, GraphIR, SemanticIR);
                        }
                        else
                        {
                            SemanticIR.AssetPath = GraphIR.AssetPath;
                            SemanticIR.ParentClassPath = GraphIR.ParentClassPath;
                            for (const FString& Message : GraphIR.Diagnostics)
                            {
                                FBPSemanticDiagnostic Diagnostic;
                                Diagnostic.Message = Message;
                                SemanticIR.Diagnostics.Add(MoveTemp(Diagnostic));
                            }
                            if (SemanticIR.Diagnostics.IsEmpty())
                            {
                                FBPSemanticDiagnostic Diagnostic;
                                Diagnostic.Message = TEXT("Graph IR build failed");
                                SemanticIR.Diagnostics.Add(MoveTemp(Diagnostic));
                            }
                        }
                        FBPSemanticVariableExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticGraphTraceExtractor::Append(GraphIR, SemanticIR);
                        FString Json;
                        if (FBPSemanticJsonSerializer::Serialize(
                            SemanticIR, FEngineVersion::Current().ToString(), Json) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Semantic JSON serialization failed.")));
                            return;
                        }
                        FPlatformApplicationMisc::ClipboardCopy(*Json);
                        FMessageDialog::Open(EAppMsgType::Ok,
                            FText::FromString(TEXT("Semantic JSON v2 copied to clipboard (no files written).")));
                    }));

                // Snapshot v1とは独立したSemantic JSON v2の明示的な保存導線。
                // Loweringに失敗しても診断をJSONへ含め、失敗を成功扱いしない。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.ExportSemanticJson"),
                    FText::FromString(TEXT("Export Semantic JSON v2")),
                    FText::FromString(TEXT("Export Semantic IR and diagnostics to Saved/BlueprintSemanticJson")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (Ctx == nullptr)
                        {
                            return;
                        }
                        if (Ctx->SelectedAssets.Num() != 1)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Select exactly one Blueprint for Semantic JSON export.")));
                            return;
                        }
                        UBlueprint* Blueprint = Cast<UBlueprint>(Ctx->SelectedAssets[0].GetAsset());
                        if (Blueprint == nullptr)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("The selected asset is not a Blueprint.")));
                            return;
                        }

                        FBPGraphIR GraphIR;
                        FBPSemanticIR SemanticIR;
                        if (FBPGraphIRBuilder::Build(Blueprint, GraphIR))
                        {
                            // 現段階のLoweringはPrintString MVPに限定される。
                            FBPSemanticLowering::LowerPrintString(Blueprint, GraphIR, SemanticIR);
                        }
                        else
                        {
                            SemanticIR.AssetPath = GraphIR.AssetPath;
                            SemanticIR.ParentClassPath = GraphIR.ParentClassPath;
                            for (const FString& Message : GraphIR.Diagnostics)
                            {
                                FBPSemanticDiagnostic Diagnostic;
                                Diagnostic.Message = Message;
                                SemanticIR.Diagnostics.Add(MoveTemp(Diagnostic));
                            }
                            if (SemanticIR.Diagnostics.IsEmpty())
                            {
                                FBPSemanticDiagnostic Diagnostic;
                                Diagnostic.Message = TEXT("Graph IR build failed");
                                SemanticIR.Diagnostics.Add(MoveTemp(Diagnostic));
                            }
                        }

                        FBPSemanticVariableExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticGraphTraceExtractor::Append(GraphIR, SemanticIR);
                        FString Path;
                        FString Error;
                        if (FBPSemanticJsonExporter::Export(
                            SemanticIR, FEngineVersion::Current().ToString(), Path, Error) == false)
                        {
                            UE_LOG(LogBlueprintAIConverter, Error, TEXT("Semantic JSON export failed: %s"), *Error);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
                            return;
                        }
                        const FString Status = SemanticIR.HasErrors() ? TEXT("unsupported") : TEXT("partial");
                        UE_LOG(LogBlueprintAIConverter, Display,
                            TEXT("Semantic JSON exported (%s): %s"), *Status, *Path);
                        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                            FString::Printf(TEXT("Semantic JSON exported (%s):\n%s"), *Status, *Path)));
                    }));

                // Semantic JSON v2とは独立して、未対応ノードも含むGraph IRをCopilot用に保存する。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.ExportCopilotGraphJson"),
                    FText::FromString(TEXT("Export Copilot Graph JSON")),
                    FText::FromString(TEXT("Export node, pin, function reference and link data to Saved/BlueprintCopilotJson")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (Ctx == nullptr)
                        {
                            return;
                        }
                        if (Ctx->SelectedAssets.Num() != 1)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Select exactly one Blueprint.")));
                            return;
                        }
                        UBlueprint* Blueprint = Cast<UBlueprint>(Ctx->SelectedAssets[0].GetAsset());
                        if (Blueprint == nullptr)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("The selected asset is not a Blueprint.")));
                            return;
                        }
                        FBPGraphIR GraphIR;
                        if (FBPGraphIRBuilder::Build(Blueprint, GraphIR) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(TEXT("Graph IR build failed. Copilot JSON was not saved.")));
                            return;
                        }
                        FString Path;
                        FString Error;
                        if (FBPCopilotGraphJsonExporter::Export(
                            GraphIR, FEngineVersion::Current().ToString(), Path, Error) == false)
                        {
                            UE_LOG(LogBlueprintAIConverter, Error, TEXT("Copilot JSON export failed: %s"), *Error);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
                            return;
                        }
                        UE_LOG(LogBlueprintAIConverter, Display, TEXT("Copilot graph JSON exported: %s"), *Path);
                        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                            FString::Printf(TEXT("Copilot graph JSON exported (partial):\\n%s"), *Path)));
                    }));

                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.Export"),
                    FText::FromString(TEXT("Export Blueprint Snapshot (JSON)")),
                    FText::FromString(TEXT("Export selected Blueprints to Saved/BlueprintSnapshots")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (Ctx == nullptr)
                        {
                            return;
                        }
                        int32 Success = 0, Failure = 0;
                        for (const FAssetData& Asset : Ctx->SelectedAssets)
                        {
                            UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
                            if (BP == nullptr)
                            {
                                continue;
                            }
                            FString Path, Error;
                            if (FBPSnapshotExporter::Export(BP, Path, Error))
                            {
                                ++Success;
                                UE_LOG(LogBlueprintAIConverter, Display, TEXT("Exported: %s"), *Path);
                            }
                            else
                            {
                                ++Failure;
                                UE_LOG(LogBlueprintAIConverter, Error, TEXT("Export failed: %s"), *Error);
                            }
                        }
                        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
                            FText::FromString(TEXT("Blueprint snapshots: {0} exported, {1} failed. See Output Log.")),
                            FText::AsNumber(Success), FText::AsNumber(Failure)));
                    }));
            }));
    }
};

IMPLEMENT_MODULE(FBlueprintAIConverterModule, BlueprintAIConverter)
