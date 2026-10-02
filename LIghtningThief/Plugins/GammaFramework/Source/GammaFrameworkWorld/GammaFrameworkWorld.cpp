#include "GammaFrameworkWorld.h"
#include "Modules/ModuleManager.h"
#include "Tween/GF_Tween.h"

// The tween pools are static and live for the whole module lifetime, the same
// way the FCTween plugin's own module owns them. UGF_TweenSubsystem only ticks
// them; it must not create or free them.
class FGammaFrameworkWorldModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGF_Tween::Initialize();
	}

	virtual void ShutdownModule() override
	{
		FGF_Tween::Deinitialize();
	}
};

IMPLEMENT_MODULE(FGammaFrameworkWorldModule, GammaFrameworkWorld);
