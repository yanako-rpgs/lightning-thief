using UnrealBuildTool;

// Genre-agnostic half of the Gamma Framework: grid overworld movement and
// pathfinding, NPC AI, the dialogue system, quests/flags, settings, boot
// sequencing, the town map, items, routes and the runtime debugger.
//
// Nothing in here knows what a Creature is. A project that only wants the
// overworld tech can enable this module and leave GammaFrameworkCreatures off.
public class GammaFrameworkWorld : ModuleRules
{
	public GammaFrameworkWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The module is laid out flat (Dialogue/, Settings/, Boot/ ... directly
		// under the module root) rather than Public/Private, so the root has to
		// be an include path explicitly or "Dialogue/GF_DialogueTypes.h" will
		// not resolve from a sibling folder.
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
			"RHI",              // PipelineStateCache::NumActivePrecacheRequests, read by the
			                    // boot subsystem's Niagara precache progress counter. The game
			                    // target links monolithically and resolves it anyway; only the
			                    // editor's per-module DLL build fails without this.
			"UINavigation",     // dialogue / settings / town map widgets derive from UUINavWidget
			"DeveloperSettings",// UGF_GameSettingsConfig project settings page
			"Json",
			"JsonUtilities",
		});

		// NOTE: do not add an UncookedOnly module (anything depending on UnrealEd)
		// to the lists above. It breaks the non-editor game target with "Unable to
		// instantiate UnrealEd module for non-editor targets" -- which never shows
		// up in editor builds, only when packaging.
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
