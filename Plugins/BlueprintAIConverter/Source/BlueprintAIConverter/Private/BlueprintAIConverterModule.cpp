#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Blueprint.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticVariableExtractor.h"
#include "BPSemanticClassExtractor.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "BPCopilotGraphJsonExporter.h"
#include "Misc/EngineVersion.h"
#include "BPGraphIR.h"
#include "BPSemanticLowering.h"
#include "BPSemanticCodeGenerator.h"
#include "BPReflectedClassPreview.h"
#include "BPNativeSubclassGenerator.h"
#include "BPNativeClassDependencyReport.h"
#include "BPGeneratedCodePreview.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/MessageDialog.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintAIConverter, Log, All);

namespace
{
// Content BrowserのアセットではないネイティブUCLASSをパスから指定してJSONに保存する。
// BlueprintグラフのLoweringとは独立した宣言情報のエクスポート経路とする。
void ExportNativeClassSemanticJson(const TArray<FString>& Args)
{
    if (Args.Num() != 1 || Args[0].StartsWith(TEXT("/Script/")) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error,
            TEXT("Usage: BlueprintAIConverter.ExportNativeClass /Script/Module.ClassName"));
        return;
    }

    const FString ClassPath = Args[0];
    UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
    if (Class == nullptr)
    {
        Class = LoadObject<UClass>(nullptr, *ClassPath);
    }
    if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Native) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Native UCLASS not found: %s"), *ClassPath);
        return;
    }

    FBPSemanticIR IR;
    IR.AssetPath = ClassPath;
    IR.ParentClassPath = GetPathNameSafe(Class->GetSuperClass());
    if (FBPSemanticClassExtractor::Extract(Class, IR.ClassInfo) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Class reflection failed: %s"), *ClassPath);
        return;
    }
    FString OutputPath;
    FString Error;
    if (FBPSemanticJsonExporter::Export(
        IR, FEngineVersion::Current().ToString(), OutputPath, Error) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Native UCLASS export failed: %s"), *Error);
        return;
    }
    // このJSONは宣言メンバーのみであり、ネイティブ関数の実装本体は含まない。
    UE_LOG(LogBlueprintAIConverter, Display, TEXT("Native UCLASS semantic JSON: %s"), *OutputPath);
}

// ネイティブクラスのリフレクション宣言をクリップボードへコピーする。
// 既存UCLASSの再定義につながるソースファイルは生成しない。
void CopyNativeClassCppPreview(const TArray<FString>& Args)
{
    if (Args.Num() != 1 || Args[0].StartsWith(TEXT("/Script/")) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error,
            TEXT("Usage: BlueprintAIConverter.CopyNativeClassCppPreview /Script/Module.ClassName"));
        return;
    }
    UClass* Class = FindObject<UClass>(nullptr, *Args[0]);
    if (Class == nullptr)
    {
        Class = LoadObject<UClass>(nullptr, *Args[0]);
    }
    if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Native) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Native UCLASS not found: %s"), *Args[0]);
        return;
    }
    FBPSemanticClassInfo Info;
    if (FBPSemanticClassExtractor::Extract(Class, Info) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Class reflection failed: %s"), *Args[0]);
        return;
    }
    FString Preview;
    FString Error;
    if (FBPReflectedClassPreview::Generate(Info, Preview, Error) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("C++ preview failed: %s"), *Error);
        return;
    }
    FPlatformApplicationMisc::ClipboardCopy(*Preview);
    UE_LOG(LogBlueprintAIConverter, Display,
        TEXT("Native UCLASS reference preview copied (not compilable): %s"), *Args[0]);
}

FAutoConsoleCommandWithArgs CopyNativeClassCppPreviewCommand(
    TEXT("BlueprintAIConverter.CopyNativeClassCppPreview"),
    TEXT("Copy native UCLASS reflection reference to clipboard (not compilable)"),
    FConsoleCommandWithArgsDelegate::CreateStatic(&CopyNativeClassCppPreview));

// 既存ネイティブクラスを継承する新規UCLASSの最小ソースをクリップボードへ出力する。
// コードをディスクへ書き込まず、開発者が生成先モジュールで確認してから使用する。
void CopyNativeSubclassCpp(const TArray<FString>& Args)
{
    if (Args.Num() != 2 || Args[0].StartsWith(TEXT("/Script/")) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error,
            TEXT("Usage: BlueprintAIConverter.CopyNativeSubclassCpp /Script/Module.ClassName ANewClass"));
        return;
    }
    UClass* Class = FindObject<UClass>(nullptr, *Args[0]);
    if (Class == nullptr)
    {
        Class = LoadObject<UClass>(nullptr, *Args[0]);
    }
    if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Native) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Native UCLASS not found: %s"), *Args[0]);
        return;
    }
    FBPSemanticClassInfo Info;
    if (FBPSemanticClassExtractor::Extract(Class, Info) == false)
    {
        return;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (FBPNativeSubclassGenerator::Generate(Info, Args[1], FString(), Files, Diagnostics) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Subclass generation failed: %s"),
            *FString::Join(Diagnostics, TEXT("; ")));
        return;
    }
    const FString Preview = FString::Printf(TEXT("// %s\n%s\n// %s\n%s"),
        *Files.HeaderFileName, *Files.HeaderContent,
        *Files.SourceFileName, *Files.SourceContent);
    FPlatformApplicationMisc::ClipboardCopy(*Preview);
    // コマンドに生成先モジュールがないため依存を確定できない。
    // 専用コマンドでBuild.csの確認事項を明示的に取得する。
    UE_LOG(LogBlueprintAIConverter, Warning,
        TEXT("Check target Build.cs dependencies with BlueprintAIConverter.NativeClassDependencies"));
    UE_LOG(LogBlueprintAIConverter, Display, TEXT("Native subclass C++ copied: %s"), *Args[1]);
}

// 生成先モジュールを明示して、Build.csに必要な確認事項を表示する。
void ShowNativeClassDependencies(const TArray<FString>& Args)
{
    if (Args.Num() != 2 || Args[0].StartsWith(TEXT("/Script/")) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error,
            TEXT("Usage: BlueprintAIConverter.NativeClassDependencies /Script/Module.ClassName TargetModule"));
        return;
    }
    UClass* Class = FindObject<UClass>(nullptr, *Args[0]);
    if (Class == nullptr)
    {
        Class = LoadObject<UClass>(nullptr, *Args[0]);
    }
    if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Native) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Native UCLASS not found: %s"), *Args[0]);
        return;
    }
    FBPSemanticClassInfo Info;
    if (FBPSemanticClassExtractor::Extract(Class, Info) == false)
    {
        return;
    }
    FString Report;
    FString Error;
    if (FBPNativeClassDependencyReport::Build(Info, Args[1], Report, Error) == false)
    {
        UE_LOG(LogBlueprintAIConverter, Error, TEXT("Dependency report failed: %s"), *Error);
        return;
    }
    UE_LOG(LogBlueprintAIConverter, Display, TEXT("%s"), *Report);
}

FAutoConsoleCommandWithArgs NativeClassDependenciesCommand(
    TEXT("BlueprintAIConverter.NativeClassDependencies"),
    TEXT("Show target Build.cs dependency checklist: /Script/Module.ClassName TargetModule"),
    FConsoleCommandWithArgsDelegate::CreateStatic(&ShowNativeClassDependencies));

FAutoConsoleCommandWithArgs CopyNativeSubclassCppCommand(
    TEXT("BlueprintAIConverter.CopyNativeSubclassCpp"),
    TEXT("Copy minimal native UCLASS subclass header/source: /Script/Module.ClassName ANewClass"),
    FConsoleCommandWithArgsDelegate::CreateStatic(&CopyNativeSubclassCpp));

FAutoConsoleCommandWithArgs ExportNativeClassCommand(
    TEXT("BlueprintAIConverter.ExportNativeClass"),
    TEXT("Export native UCLASS declarations: /Script/Module.ClassName"),
    FConsoleCommandWithArgsDelegate::CreateStatic(&ExportNativeClassSemanticJson));

FString JoinSemanticDiagnostics(const FBPSemanticIR& IR)
{
    TArray<FString> Messages;
    for (const FBPSemanticDiagnostic& Diagnostic : IR.Diagnostics)
    {
        Messages.Add(Diagnostic.Message);
    }
    return FString::Join(Messages, TEXT("\n"));
}
}

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
        Section.AddDynamicEntry(TEXT("BlueprintAIConverterActions"), FNewToolMenuSectionDelegate::CreateLambda(
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
                // C++変換のEditor導線はSemantic IR経路へ一本化する。
                // 旧Direct Generatorは移行期のParityテスト専用としてUIから分離する。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.CopyGeneratedCpp"),
                    FText::FromString(TEXT("Convert to C++ (Preview)")),
                    FText::FromString(TEXT("Preview C++ generated via Semantic IR (no files written)")),
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

                        FBPGraphIR GraphIR;
                        if (FBPGraphIRBuilder::Build(Blueprint, GraphIR) == false)
                        {
                            FMessageDialog::Open(EAppMsgType::Ok,
                                FText::FromString(FString::Printf(
                                    TEXT("C++ CONVERSION FAILED\n\n%s"),
                                    *FString::Join(GraphIR.Diagnostics, TEXT("\n")))));
                            return;
                        }

                        FBPSemanticIR SemanticIR;
                        if (FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR) == false)
                        {
                            TArray<FString> Messages;
                            for (const FBPSemanticDiagnostic& Diagnostic : SemanticIR.Diagnostics)
                            {
                                Messages.Add(Diagnostic.Message);
                            }
                            const FString Message = FString::Join(Messages, TEXT("\n"));
                            UE_LOG(LogBlueprintAIConverter, Warning,
                                TEXT("C++ preview Lowering failed: %s"), *Message);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                                FString::Printf(TEXT("C++ CONVERSION FAILED\n\n%s"), *Message)));
                            return;
                        }

                        const FString ClassPrefix = Blueprint->ParentClass != nullptr
                            ? Blueprint->ParentClass->GetPrefixCPP() : FString();
                        const FString ClassName =
                            ClassPrefix + Blueprint->GetName() + TEXT("Generated");
                        // 同名ゲームモジュールのBuild.csが存在する場合だけAPIマクロを付ける。
                        // 推測した名前で別モジュールのAPIを参照しない。
                        const FString ModuleName = FApp::GetProjectName();
                        const FString BuildRules = FPaths::Combine(
                            FPaths::ProjectDir(), TEXT("Source"), ModuleName, ModuleName + TEXT(".Build.cs"));
                        const FString ApiMacro = IFileManager::Get().FileExists(*BuildRules)
                            ? ModuleName.ToUpper() + TEXT("_API") : FString();

                        FBPGeneratedActorFiles Files;
                        TArray<FString> Diagnostics;
                        if (FBPSemanticCodeGenerator::Generate(
                            SemanticIR, ClassName, ApiMacro, Files, Diagnostics,
                            EBPVariableFallbackMode::DeclarationOnly) == false)
                        {
                            const FString Message = FString::Join(Diagnostics, TEXT("\n"));
                            UE_LOG(LogBlueprintAIConverter, Warning,
                                TEXT("C++ preview generation failed: %s"), *Message);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                                FString::Printf(TEXT("C++ CONVERSION FAILED\n\n%s"), *Message)));
                            return;
                        }

                        // 生成内容は読み取り専用の別ウィンドウで表示する。
                        // コピー操作はウィンドウ内の個別ボタンから行える。
                        FBPGeneratedCodePreview::Show(Files, ClassName, ApiMacro);
                        UE_LOG(LogBlueprintAIConverter, Display,
                            TEXT("C++ preview opened for %s: %s, %s"),
                            *Blueprint->GetName(), *Files.HeaderFileName, *Files.SourceFileName);
                    }));

                // ファイルを書かず、Semantic JSON文字列を直接クリップボードへコピーする。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.CopySemanticJson"),
                    FText::FromString(TEXT("Copy Semantic JSON")),
                    FText::FromString(TEXT("Copy Semantic JSON to clipboard without writing files")),
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
                            FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR);
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
                        FBPSemanticClassExtractor::Append(Blueprint, SemanticIR);
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
                        const FString Message = SemanticIR.HasErrors()
                            ? FString::Printf(
                                TEXT("COPY SUCCEEDED, BUT SEMANTIC CONVERSION FAILED\n\n")
                                TEXT("Diagnostic JSON was copied to the clipboard.\n\n%s"),
                                *JoinSemanticDiagnostics(SemanticIR))
                            : TEXT("COPY SUCCEEDED\n\nSemantic JSON was copied to the clipboard. No file was written.");
                        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
                    }));

                // Snapshot v1とは独立したSemantic JSON v2の明示的な保存導線。
                // Loweringに失敗しても診断をJSONへ含め、失敗を成功扱いしない。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.ExportSemanticJson"),
                    FText::FromString(TEXT("Export Semantic JSON")),
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
                            FBPSemanticLowering::Lower(Blueprint, GraphIR, SemanticIR);
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

                        FBPSemanticClassExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticVariableExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticGraphTraceExtractor::Append(GraphIR, SemanticIR);
                        FString Path;
                        FString Error;
                        if (FBPSemanticJsonExporter::Export(
                            SemanticIR, FEngineVersion::Current().ToString(), Path, Error) == false)
                        {
                            UE_LOG(LogBlueprintAIConverter, Error, TEXT("Semantic JSON export failed: %s"), *Error);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                                FString::Printf(
                                    TEXT("EXPORT FAILED\n\nNo Semantic JSON file was saved.\n\nReason: %s"),
                                    *Error)));
                            return;
                        }
                        const FString Status = SemanticIR.HasErrors() ? TEXT("unsupported") : TEXT("partial");
                        UE_LOG(LogBlueprintAIConverter, Display,
                            TEXT("Semantic JSON exported (%s): %s"), *Status, *Path);
                        const FString Message = SemanticIR.HasErrors()
                            ? FString::Printf(
                                TEXT("FILE EXPORT SUCCEEDED, BUT SEMANTIC CONVERSION FAILED\n\n")
                                TEXT("A diagnostic JSON file was saved. Generated C++ is not available for this result.\n\n")
                                TEXT("File:\n%s\n\nDiagnostics:\n%s"),
                                *Path, *JoinSemanticDiagnostics(SemanticIR))
                            : FString::Printf(
                                TEXT("EXPORT SUCCEEDED\n\n")
                                TEXT("Semantic conversion succeeded for the supported graph.\n\nFile:\n%s"),
                                *Path);
                        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
                    }));

                // Semantic JSON v2とは独立して、未対応ノードも含むGraph IRをCopilot用に保存する。
                InSection.AddMenuEntry(
                    TEXT("BlueprintAIConverter.ExportCopilotGraphJson"),
                    FText::FromString(TEXT("Export Raw Blueprint Graph JSON")),
                    FText::FromString(TEXT("Export raw node, pin, function reference and link data to Saved/BlueprintCopilotJson")),
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
                        // Graph IRとは別に、Blueprintの宣言情報をReflectionから取得する。
                        // PrintString限定のLoweringには依存させない。
                        FBPSemanticIR SemanticIR;
                        SemanticIR.AssetPath = GraphIR.AssetPath;
                        SemanticIR.ParentClassPath = GraphIR.ParentClassPath;
                        FBPSemanticClassExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticVariableExtractor::Append(Blueprint, SemanticIR);
                        FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
                        FString Path;
                        FString Error;
                        if (FBPCopilotGraphJsonExporter::Export(
                            GraphIR, FEngineVersion::Current().ToString(), Path, Error, &SemanticIR) == false)
                        {
                            UE_LOG(LogBlueprintAIConverter, Error, TEXT("Copilot JSON export failed: %s"), *Error);
                            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                                FString::Printf(
                                    TEXT("EXPORT FAILED\n\nNo Raw Blueprint Graph JSON file was saved.\n\nReason: %s"),
                                    *Error)));
                            return;
                        }
                        UE_LOG(LogBlueprintAIConverter, Display, TEXT("Raw Blueprint graph JSON exported: %s"), *Path);
                        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
                            FString::Printf(
                                TEXT("EXPORT SUCCEEDED\n\n")
                                TEXT("The raw Blueprint graph was saved successfully.\n")
                                TEXT("Semantic declarations are supplemental and may be partial.\n\nFile:\n%s"),
                                *Path)));
                    }));

            }));
    }
};

IMPLEMENT_MODULE(FBlueprintAIConverterModule, BlueprintAIConverter)
