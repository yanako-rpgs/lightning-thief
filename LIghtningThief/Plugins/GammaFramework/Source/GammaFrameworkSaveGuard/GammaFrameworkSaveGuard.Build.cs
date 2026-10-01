using UnrealBuildTool;

// Custom PlatformFeatures module. It exists only so that
// IPlatformFeaturesModule::Get() can hand back our save game system instead of
// the engine's FGenericSaveGameSystem -- see GF_SaveGuardModule.cpp for why this
// has to be its own module and cannot live inside GammaFrameworkWorld.
public class GammaFrameworkSaveGuard : ModuleRules
{
	public GammaFrameworkSaveGuard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});
	}
}
