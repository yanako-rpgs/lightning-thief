using UnrealBuildTool;

// The turn-based battle layer: battle component and manager, damage resolution,
// trait effects, held-item effects, flinch, tamer AI, and the runtime debugger.
//
// Deliberately separable. GammaFrameworkCreatures owns the DATA -- species,
// creature instances, party, vault, breeding, trading -- and knows nothing about
// how a turn resolves. Disable or delete this module and the data layer keeps
// working; only combat goes away.
//
// That seam exists so this half can be replaced wholesale by a new battle
// system without touching the creature data model.
public class GammaFrameworkBattle : ModuleRules
{
	public GammaFrameworkBattle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		// The 4v4 turn engine. Its own folder so the ported single-battle code
		// beside it stays visibly separate and can be deleted in one move.
		PublicIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Turn"));

		// The presentation layer on top of the turn engine: battle manager with
		// turn holds, skill base with NotifyFinished, launcher, arena, anchors, FX.
		PublicIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Stage"));

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
			"Json",
			"JsonUtilities",

			"GammaFrameworkWorld",
			"GammaFrameworkCreatures",
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
