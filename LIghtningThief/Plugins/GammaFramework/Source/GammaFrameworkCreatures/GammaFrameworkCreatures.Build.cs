using UnrealBuildTool;

// The creature-collector half: species data, creature instances, the turn-based
// battle stack, skills, traits, temperaments, capture, breeding/sanctuary, the
// vault (off-party storage), tamers, and peer-to-peer trading.
//
// Depends on GammaFrameworkWorld (grid, items, dialogue, save) but never the
// other way round.
public class GammaFrameworkCreatures : ModuleRules
{
	public GammaFrameworkCreatures(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Paper2D",
			"AssetRegistry",
			"UMG",
			"Niagara",
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"UINavigation",
			"DeveloperSettings",
			"AudioMixer",       // ISubmixEnvelopeListener -- the creature Call visualizer
			"HTTP",             // UGF_TradeSubsystem talks to the relay over plain HTTP.
			                    // Deliberately NOT OnlineSubsystem/Sockets: a trade is one
			                    // atomic ~1KB exchange, so it needs a mailbox, not a NetDriver.
			"Json",
			"JsonUtilities",

			"GammaFrameworkWorld",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"AssetTools",
				"BlueprintGraph",
				"Kismet",
				"KismetCompiler",
				"DetailCustomizations",
				"GraphEditor",
				"Projects",
				"PropertyEditor",
				"EditorStyle",
				"ToolMenus",
				"DesktopPlatform",
				"ClassViewer",
			});
		}
	}
}
