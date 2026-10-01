using UnrealBuildTool;

// Editor-only tooling: the Create Creature window, the dialogue asset factory
// and its preview widget, and the species-data thumbnail renderer.
public class GammaFrameworkEditor : ModuleRules
{
	public GammaFrameworkEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add(ModuleDirectory); // lets Private/*.cpp reach Public/ headers

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetTools",
			"AssetDefinition",
			"Paper2DEditor",

			"GammaFrameworkWorld",
			"GammaFrameworkCreatures",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"Paper2D",
			"ToolMenus",        // UToolMenus -- top-bar menu registration
			"LevelEditor",      // level editor menu extension point
			"AssetRegistry",    // populating the asset picker list
			"ContentBrowser",   // syncing to a newly created asset
			"InputCore",        // FKey used by Slate
			"DirectoryWatcher", // IDirectoryWatcher for dialogue source auto-reimport
			"DesktopPlatform",  // IDesktopPlatform file open dialog

			// Blueprint -> JSON export
			"Json",             // FJsonObject / FJsonSerializer
			"JsonUtilities",    // FJsonObjectConverter, for property values
			"UMG",              // UWidgetTree, UWidget, UWidgetAnimation
			"UMGEditor",        // UWidgetBlueprint and its editor-only bindings
		});
	}
}
