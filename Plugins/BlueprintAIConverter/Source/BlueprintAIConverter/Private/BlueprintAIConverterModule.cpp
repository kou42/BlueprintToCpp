#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Blueprint.h"
#include "BPSnapshotExporter.h"
#include "Misc/MessageDialog.h"

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
        UToolMenu* Menu = UToolMenus::ExtendMenu(TEXT("ContentBrowser.AssetContextMenu"));
        if (!Menu) return;
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("BlueprintAIConverter"));
        Section.AddDynamicEntry(TEXT("ExportBlueprintSnapshot"), FNewToolMenuSectionDelegate::CreateLambda(
            [](FToolMenuSection& InSection)
            {
                const auto* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
                if (!Context) return;
                bool bHasBlueprint = false;
                for (const FAssetData& Asset : Context->SelectedAssets)
                {
                    if (Cast<UBlueprint>(Asset.GetAsset())) { bHasBlueprint = true; break; }
                }
                if (!bHasBlueprint) return;
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.Export"),
                    FText::FromString(TEXT("Export Blueprint Snapshot (JSON)")),
                    FText::FromString(TEXT("Export selected Blueprints to Saved/BlueprintSnapshots")),
                    FSlateIcon(),
                    FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
                    {
                        const auto* Ctx = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
                        if (!Ctx) return;
                        int32 Success = 0, Failure = 0;
                        for (const FAssetData& Asset : Ctx->SelectedAssets)
                        {
                            UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
                            if (!BP) continue;
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
