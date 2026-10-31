using UnrealBuildTool;

public class BlueprintAIConverter : ModuleRules
{
    public BlueprintAIConverter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // 公開ヘッダーは機能単位で分割する。従来の短い#include名との互換性を
        // 維持するため、各カテゴリを明示的なinclude pathとして登録する。
        PublicIncludePaths.AddRange(new string[] {
            System.IO.Path.Combine(ModuleDirectory, "Public", "Analysis"),
            System.IO.Path.Combine(ModuleDirectory, "Public", "CodeGen"),
            System.IO.Path.Combine(ModuleDirectory, "Public", "Export"),
            System.IO.Path.Combine(ModuleDirectory, "Public", "IR"),
            System.IO.Path.Combine(ModuleDirectory, "Public", "Lowering"),
            System.IO.Path.Combine(ModuleDirectory, "Public", "Tests"),
            System.IO.Path.Combine(ModuleDirectory, "Public", "Validation")
        });
        PrivateIncludePaths.Add(
            System.IO.Path.Combine(ModuleDirectory, "Private", "Editor"));

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "BlueprintGraph", "UMG", "UMGEditor",
            "Json", "JsonUtilities", "ContentBrowser", "ToolMenus", "Slate", "SlateCore", "DesktopPlatform",
            // ClipboardCopy は ApplicationCore に実装されるため明示的にリンクする。
            "ApplicationCore"
        });
    }
}
