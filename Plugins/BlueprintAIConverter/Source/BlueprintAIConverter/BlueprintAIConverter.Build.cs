using UnrealBuildTool;

public class BlueprintAIConverter : ModuleRules
{
    public BlueprintAIConverter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "BlueprintGraph",
            "Json", "JsonUtilities", "ContentBrowser", "ToolMenus", "Slate", "SlateCore", "DesktopPlatform"
        });
    }
}
