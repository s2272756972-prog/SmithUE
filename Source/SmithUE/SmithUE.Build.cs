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
				"JsonUtilities",
				"DeveloperSettings"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"EditorFramework",
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
				"ApplicationCore",
			"RenderCore",
			"RHI",
			"Landscape",
				"Foliage"
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
					"AssetTools",
					"Niagara",
					"NiagaraEditor",
					"Sequencer",
					"MovieScene",
					"MovieSceneTracks",
					"LevelSequence",
					"EnhancedInput",
				"InputBlueprintNodes",
					"UMG",
					"UMGEditor"
			}
		);
		}
	}
}
