// Copyright 2026, 123dx-svg. MIT License.

using UnrealBuildTool;

public class SmithUE : ModuleRules
{
	public SmithUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Json",
				"JsonUtilities"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"EditorScriptingUtilities",
				"EditorSubsystem",
				"Slate",
				"SlateCore",
				"Projects",
				"AssetRegistry",
				"LevelEditor",
				"Sockets",
				"Networking",
				"HTTP",
				"RenderCore"
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Kismet",
					"KismetCompiler",
					"BlueprintGraph",
					"GraphEditor",
					"MaterialEditor",
					"PropertyEditor",
					"ToolMenus",
					"ImageWrapper",
					"AssetTools"
				}
			);
		}
	}
}
