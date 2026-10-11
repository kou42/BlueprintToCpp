#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Blueprint.h"
#include "BPSnapshotExporter.h"
#include "BPGraphIR.h"
#include "BPActorCodeGenerator.h"
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
                            FPaths::ProjectSourceDir(), ModuleName, ModuleName + TEXT(".Build.cs"));
                        const FString ApiMacro = IFileManager::Get().FileExists(*BuildRules)
                            ? ModuleName.ToUpper() + TEXT("_API") : FString();
                        if (FBPActorCodeGenerator::Generate(IR, ClassName, ApiMacro, Files, Diagnostics) == false)
                        {
                            const FString Message = FString::Join(Diagnostics, TEXT("\\n"));
                            UE_LOG(LogBlueprintAIConverter, Warning, TEXT("C++ preview failed: %s"), *Message);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
                            return;
                        }

                        // 生成内容は読み取り専用の別ウィンドウで表示する。
                        // コピー操作はウィンドウ内の個別ボタンから行える。
                        FBPGeneratedCodePreview::Show(Files);
                        UE_LOG(LogBlueprintAIConverter, Display,
                            TEXT("C++ preview opened for %s: %s, %s"),
                            *Blueprint->GetName(), *Files.HeaderFileName, *Files.SourceFileName);
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
