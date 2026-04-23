using UnrealBuildTool;

public class CryImporter : ModuleRules
{
    public CryImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "UnrealEd",
                "LevelEditor",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "Projects",
                "XmlParser",
                "AssetRegistry",
                "AssetTools",
                "Landscape",
                "Foliage",
                "ContentBrowser",
                "DesktopPlatform",
                "Json",
                "JsonUtilities"
            }
        );
    }
}


