// Copyright ChordPBRGenerator

using UnrealBuildTool;

public class ChordPBRGenerator : ModuleRules
{
	public ChordPBRGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorSubsystem",
				"UnrealEd",
				"Projects",
				"InputCore",
				"LevelEditor",
				"EditorStyle",
				"DeveloperSettings",
				"PropertyEditor",
				"HTTP",
				"Json",
				"JsonUtilities",
				"WebSockets",
				"ImageWrapper",
				"AssetTools",
				"ContentBrowser",
				"DesktopPlatform"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore"
			}
		);

		bLegacyPublicIncludePaths = false;
	}
}
