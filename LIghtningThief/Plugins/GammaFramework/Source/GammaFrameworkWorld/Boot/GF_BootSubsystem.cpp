#include "GF_BootSubsystem.h"

#include "GF_BootPlan.h"
#include "GF_DiagLog.h"

#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ShaderPipelineCache.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"
#include "PipelineStateCache.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogGFBoot);

namespace GEBoot
{
	/** How long to wait for the pipeline cache to start reporting work before
	 *  concluding this build shipped without one. The cache file is opened on a
	 *  background task shortly after RHI init, so it is never instant. */
	static constexpr double PSOStartGraceSeconds = 3.0;

	/** Default Settle length when a Settle stage leaves MaxSeconds at 0. */
	static constexpr float DefaultSettleSeconds = 0.35f;
}

// Clear the Gameplay Debugger's activation key off its config CDO so the overlay cannot be
// opened by keypress. No-op in the editor -- PIE keeps the debugger for development;
// standalone deliberately behaves like the shipped game so this is testable without a cook.
static void DisableGameplayDebuggerActivation(const TCHAR* Context)
{
	if (GIsEditor)
	{
		return;
	}

	UClass* ConfigClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayDebugger.GameplayDebuggerConfig"));
	if (!ConfigClass)
	{
		// Once only. This fires on every world init otherwise, and would bury the
		// diagnostics file in a line that says nothing new.
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
#if UE_BUILD_SHIPPING
			// Shipping compiles the whole GameplayDebugger module out, so this is the
			// expected, correct outcome -- worded as such because a tester reading the
			// diagnostics file will otherwise report it as an error.
			GF_DIAG("Boot(%s): gameplay debugger is compiled out of this build. Nothing to disable.", Context);
#else
			// Outside Shipping the module SHOULD be present, so not finding it means either
			// it has not registered yet or something changed. The world-init pass is the
			// backstop for the first case.
			GF_DIAG("Boot(%s): GameplayDebuggerConfig not found, but this build should have it - not registered yet, or the module is missing.", Context);
#endif
		}
		return;
	}

	UObject* ConfigCDO = ConfigClass->GetDefaultObject();
	FStructProperty* KeyProp = ConfigCDO ? FindFProperty<FStructProperty>(ConfigClass, TEXT("ActivationKey")) : nullptr;
	if (!KeyProp)
	{
		GF_DIAG("Boot(%s): could not reach GameplayDebuggerConfig::ActivationKey - THE DEBUGGER IS STILL REACHABLE.", Context);
		return;
	}

	FKey* ActivationKey = KeyProp->ContainerPtrToValuePtr<FKey>(ConfigCDO);
	if (!ActivationKey->IsValid())
	{
		return;   // already cleared by an earlier pass
	}

	GF_DIAG("Boot(%s): gameplay debugger key was '%s' - clearing it.", Context, *ActivationKey->ToString());
	*ActivationKey = FKey();
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void UGF_BootSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// No other subsystem is fetched here on purpose. Anything fetched during
	// Initialize has to be declared with InitializeDependency or it comes back
	// null, and this subsystem has no reason to need one.

	// Silence the on-screen messages -- outside the editor only.
	//
	// GAreScreenMessagesEnabled is ONE switch covering BOTH on-screen message systems, not
	// just the engine's own warnings. UKismetSystemLibrary::PrintString tests this global
	// itself before it ever reaches AddOnScreenDebugMessage, so clearing it silences every
	// Blueprint Print String as well. An earlier version of this comment had it as two
	// independent switches -- bEnableOnScreenDebugMessages (DefaultEngine.ini) for Print
	// String, this global for engine warnings -- which is wrong, and it read as an engine
	// bug when the prints went missing in PIE. Print String's Print to Log branch is genuinely
	// separate and keeps writing to LogBlueprintUserMessages either way, so nothing is lost
	// from the Output Log when this is off.
	//
	// What this is here for: "TEXTURE STREAMING POOL OVER n MiB BUDGET", "LIGHTING NEEDS TO BE
	// REBUILT" and friends draw straight over the game. It is a plain global with no config
	// binding -- the only stock way to clear it is the DisableAllScreenMessages console
	// command, which is no use when the console is unbound in shipped builds. Setting the ini
	// alone left the streaming warning on screen for a tester.
	//
	// Shipping would strip these callers at compile time, but this game ships as Development
	// (Shipping does not currently build), so it has to be done at runtime.
	//
	// #if !UE_BUILD_SHIPPING is deliberately NOT used here: it is TRUE in Development, so it would
	// gate nothing in the build that actually reaches players, while breaking a future Shipping
	// build where GAreScreenMessagesEnabled is already irrelevant.
	//
	// !GIsEditor matches the console-key and gameplay-debugger passes below: PIE keeps its
	// debug output, standalone behaves like the shipped game so this stays testable without a
	// cook. `EnableAllScreenMessages` from the console still brings them back in a standalone
	// run where the console is reachable.
	if (!GIsEditor)
	{
		GAreScreenMessagesEnabled = false;
	}

	// Wipe the console key bindings at runtime, every boot.
	//
	// DefaultInput.ini already removes Tilde and Caret, but that only ships a DEFAULT: the
	// engine merges the player's own Saved/Config/Windows/Input.ini over it, so anyone who
	// adds ConsoleKeys back there has the console again -- and players have been finding
	// exactly these leftovers. Clearing the array here runs after that merge, so a local
	// ini edit does not survive.
	//
	// Compiling the console out with ALLOW_CONSOLE=0 would be stronger, but that is an
	// engine-wide definition and this project builds against an installed engine, which
	// rejects it: "Remove the modified setting, or set BuildEnvironment = Unique". Same wall
	// that stops Shipping building here.
	// No-op in the editor -- PIE keeps the console for development; standalone deliberately
	// behaves like the shipped game so this is testable without a cook. Same gate as the
	// gameplay debugger above.
	if (!GIsEditor)
	{
		if (UInputSettings* InputSettings = UInputSettings::GetInputSettings())
		{
			if (InputSettings->ConsoleKeys.Num() > 0)
			{
				GF_DIAG("Boot: cleared %d console key binding(s).", InputSettings->ConsoleKeys.Num());
				InputSettings->ConsoleKeys.Empty();
			}
		}
	}

	// Unbind the Gameplay Debugger's activation key (apostrophe by default).
	//
	// bUseGameplayDebugger = false in GammaFramework.Target.cs DOES NOT WORK and does not
	// error -- see the comment there. Verified by diffing the shipped exe against an older
	// one: "EnableGDT", "gdt.Enable", "gdt.Toggle" and 82 "GameplayDebugger" strings were
	// present in identical counts before and after the flag. 1.11.1 shipped with the overlay
	// live because a clean compile was mistaken for a working fix.
	//
	// UGameplayDebuggerLocalController::BindInput reads the key straight off the config CDO
	// (GameplayDebuggerLocalController.cpp:516-522) and is called from
	// GameplayDebuggerPlayerManager.cpp:225 when a player controller registers. Clearing the
	// CDO first makes that bind land on an invalid key. Key press is the ONLY way in besides
	// the gdt.* console commands, and the ConsoleKeys wipe above already closes those.
	//
	// Reached by reflection: UGameplayDebuggerConfig is MinimalAPI, so linking it would add a
	// module dependency for one field.
	DisableGameplayDebuggerActivation(TEXT("boot"));

	// Again on every world init. The boot pass alone assumes the class is registered this
	// early, which holds in a monolithic build but is not worth betting the fix on -- and a
	// player controller for a later map binds input again. OnPostWorldInitialization always
	// runs before those controllers exist. Never unbound: !GIsEditor means this only ever
	// happens in a standalone or packaged process that exits wholesale.
	if (!GIsEditor)
	{
		static bool bHookedWorldInit = false;
		if (!bHookedWorldInit)
		{
			bHookedWorldInit = true;
			FWorldDelegates::OnPostWorldInitialization.AddStatic(
				[](UWorld*, const UWorld::InitializationValues)
				{
					DisableGameplayDebuggerActivation(TEXT("world init"));
				});
		}
	}
}

void UGF_BootSubsystem::Deinitialize()
{
	FGF_DiagLog::Shutdown();

	if (CurrentHandle.IsValid())
	{
		CurrentHandle->CancelHandle();
		CurrentHandle.Reset();
	}
	KeepAliveHandles.Reset();
	ActivePlan = nullptr;
	bRunning = false;

	Super::Deinitialize();
}

void UGF_BootSubsystem::ResetToBootState()
{
	// Abandon anything in flight. A New Game / Quit to Title during the boot screen
	// is unlikely but not impossible, and a subsystem that still thinks it is booting
	// would keep ticking and fire OnBootFinished into the new run.
	if (bRunning)
	{
		UE_LOG(LogGFBoot, Warning, TEXT("GameReset while a boot was still running -- abandoning it."));
	}

	DestroyWarmingComponents();
	PendingNiagara.Reset();

	if (CurrentHandle.IsValid())
	{
		CurrentHandle->CancelHandle();
		CurrentHandle.Reset();
	}

	bRunning = false;
	bFinished = false;
	bStagesComplete = false;
	bNiagaraSpawnComplete = false;
	CurrentStage = INDEX_NONE;
	RawProgress = 0.0f;
	DisplayProgress = 0.0f;
	CompletedWeight = 0.0f;
	StatusText = FText::GetEmpty();
	StageTimings.Reset();
	ActivePlan = nullptr;

	// KeepAliveHandles are intentionally left alone -- see the header. Those are
	// warmed content, not player state; ReleasePreloadedAssets() exists if you ever
	// genuinely want the memory back.

	UE_LOG(LogGFBoot, Display, TEXT("GameReset: boot subsystem reset to boot state."));
}

TStatId UGF_BootSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGF_BootSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------
// control
// ---------------------------------------------------------------------------

void UGF_BootSubsystem::StartBoot(UGF_BootPlan* Plan)
{
	if (bRunning)
	{
		UE_LOG(LogGFBoot, Warning, TEXT("StartBoot ignored -- a boot sequence is already running."));
		return;
	}

	ActivePlan = Plan;

	if (!ActivePlan)
	{
		if (const UGF_BootSettings* Settings = UGF_BootSettings::Get())
		{
			ActivePlan = Settings->DefaultBootPlan.LoadSynchronous();
		}
	}

#if WITH_EDITOR
	if (const UGF_BootSettings* Settings = UGF_BootSettings::Get())
	{
		if (Settings->bSkipBootInEditor)
		{
			UE_LOG(LogGFBoot, Display, TEXT("Boot skipped (bSkipBootInEditor)."));
			bRunning = true;   // so FinishBoot's broadcast is symmetric with the real path
			BootStartTime = FPlatformTime::Seconds();
			StageTimings.Reset();
			FinishBoot(TEXT("skipped in editor"));
			return;
		}
	}
#endif

	if (!ActivePlan || ActivePlan->Stages.Num() == 0)
	{
		// An unconfigured plan must not leave the screen up forever. Finish, loudly.
		UE_LOG(LogGFBoot, Error,
			TEXT("No boot plan (or an empty one) -- finishing immediately. Set Project Settings > Game > Gamma Framework Boot > Default Boot Plan."));
		bRunning = true;
		BootStartTime = FPlatformTime::Seconds();
		StageTimings.Reset();
		FinishBoot(TEXT("no plan"));
		return;
	}

	TotalWeight = 0.0f;
	for (const FGF_GEBootStage& Stage : ActivePlan->Stages)
	{
		TotalWeight += FMath::Max(Stage.Weight, 0.01f);
	}

	bRunning = true;
	bFinished = false;
	bStagesComplete = false;
	CompletedWeight = 0.0f;
	RawProgress = 0.0f;
	DisplayProgress = 0.0f;
	LastReport.Reset();
	StageTimings.Reset();
	BootStartTime = FPlatformTime::Seconds();

	UE_LOG(LogGFBoot, Display, TEXT("Boot started: %d stage(s), plan '%s'."),
		ActivePlan->Stages.Num(), *GetNameSafe(ActivePlan));

	BeginStage(0);
}

void UGF_BootSubsystem::AbortBoot(const FString& Reason)
{
	if (!bRunning)
	{
		return;
	}
	UE_LOG(LogGFBoot, Warning, TEXT("Boot aborted: %s"), *Reason);
	FinishBoot(FString::Printf(TEXT("aborted (%s)"), *Reason));
}

void UGF_BootSubsystem::ReleasePreloadedAssets()
{
	const int32 Released = KeepAliveHandles.Num();
	KeepAliveHandles.Reset();
	UE_LOG(LogGFBoot, Display, TEXT("Released %d preload handle(s); their assets are now collectable."), Released);
}

// ---------------------------------------------------------------------------
// readout
// ---------------------------------------------------------------------------

void UGF_BootSubsystem::GetStageCounter(int32& OutStage, int32& OutTotal) const
{
	OutTotal = ActivePlan ? ActivePlan->Stages.Num() : 0;
	OutStage = FMath::Clamp(CurrentStage + 1, 0, OutTotal);
}

int32 UGF_BootSubsystem::GetShaderPrecompilesRemaining() const
{
	return static_cast<int32>(FShaderPipelineCache::NumPrecompilesRemaining());
}

// ---------------------------------------------------------------------------
// stage machinery
// ---------------------------------------------------------------------------

void UGF_BootSubsystem::BeginStage(int32 StageIndex)
{
	CurrentStage = StageIndex;
	StageStartTime = FPlatformTime::Seconds();

	const FGF_GEBootStage& Stage = ActivePlan->Stages[StageIndex];
	StatusText = Stage.DisplayText;

	switch (Stage.Kind)
	{
	case EGF_GEBootStageKind::ShaderPipelineCache:
	{
		PSOPeakRemaining = 0;
		bPSOSeenWork = false;

		// Precompile mode is the aggressive batch size -- correct here because a
		// boot screen is exactly the moment we are allowed to monopolise the GPU.
		FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Precompile);
		if (FShaderPipelineCache::IsBatchingPaused())
		{
			FShaderPipelineCache::ResumeBatching();
		}
		UE_LOG(LogGFBoot, Display, TEXT("Stage %d (shader pipeline cache) begun."), StageIndex + 1);
		break;
	}

	case EGF_GEBootStageKind::AssetPreload:
	{
		TArray<FSoftObjectPath> Paths;
		GatherStageAssets(Stage, Paths);

		if (Paths.Num() == 0)
		{
			UE_LOG(LogGFBoot, Warning, TEXT("Stage %d (asset preload) resolved to 0 assets."), StageIndex + 1);
			CurrentHandle.Reset();
			break;
		}

		UE_LOG(LogGFBoot, Display, TEXT("Stage %d (asset preload) requesting %d asset(s)."), StageIndex + 1, Paths.Num());

		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
		CurrentHandle = Streamable.RequestAsyncLoad(
			Paths,
			FStreamableDelegate(),
			FStreamableManager::AsyncLoadHighPriority,
			/*bManageActiveHandle*/ false,
			/*bStartStalled*/ false,
			FString::Printf(TEXT("GEBoot stage %d"), StageIndex + 1));

		if (CurrentHandle.IsValid())
		{
			// This is what makes the preload stick. Without holding the handle the
			// loaded objects are unreferenced and the next GC undoes the stage.
			KeepAliveHandles.Add(CurrentHandle);
		}
		break;
	}

	case EGF_GEBootStageKind::NiagaraPrecache:
	{
		PendingNiagara.Reset();
		WarmingComponents.Reset();
		bNiagaraSpawnComplete = false;

		TArray<FSoftObjectPath> Paths;
		GatherNiagaraSystems(Stage, Paths);
		NiagaraTotal = Paths.Num();

		if (NiagaraTotal == 0)
		{
			UE_LOG(LogGFBoot, Warning, TEXT("Stage %d (niagara precache) found no systems."), StageIndex + 1);
			bNiagaraSpawnComplete = true;
			break;
		}

		for (const FSoftObjectPath& Path : Paths)
		{
			PendingNiagara.Add(TSoftObjectPtr<UNiagaraSystem>(Path));
		}

		// Load them all up front at high priority; spawning is what costs frame time,
		// and that is paced separately by SystemsPerFrame.
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
		CurrentHandle = Streamable.RequestAsyncLoad(
			Paths,
			FStreamableDelegate(),
			FStreamableManager::AsyncLoadHighPriority,
			/*bManageActiveHandle*/ false,
			/*bStartStalled*/ false,
			FString::Printf(TEXT("GEBoot niagara %d"), StageIndex + 1));

		// NOT added to KeepAliveHandles, unlike the preload stage. Once a system's PSOs
		// are precached they live in the RHI's cache, so the system itself is dead weight
		// -- and 274 of them plus their materials, textures and meshes is a lot of dead
		// weight on a 4-core APU with ~9.8 GB free. CurrentHandle keeps them alive for the
		// duration of the stage; FinishStage drops it, and they become collectable.

		UE_LOG(LogGFBoot, Display, TEXT("Stage %d (niagara precache): %d system(s), %d per frame."),
			StageIndex + 1, NiagaraTotal, FMath::Max(1, Stage.SystemsPerFrame));
		break;
	}

	case EGF_GEBootStageKind::Settle:
	default:
		break;
	}
}

bool UGF_BootSubsystem::TickStage(float DeltaTime, float& OutStageProgress)
{
	const FGF_GEBootStage& Stage = ActivePlan->Stages[CurrentStage];
	const double Elapsed = FPlatformTime::Seconds() - StageStartTime;

	switch (Stage.Kind)
	{
	case EGF_GEBootStageKind::ShaderPipelineCache:
	{
		const uint32 Remaining = FShaderPipelineCache::NumPrecompilesRemaining();
		PSOPeakRemaining = FMath::Max(PSOPeakRemaining, Remaining);
		if (Remaining > 0)
		{
			bPSOSeenWork = true;
		}

		if (!bPSOSeenWork)
		{
			// Nothing to do yet. Either the cache file is still opening, or this
			// build shipped without one.
			OutStageProgress = FMath::Clamp(static_cast<float>(Elapsed / GEBoot::PSOStartGraceSeconds), 0.0f, 0.95f);

			if (Elapsed >= GEBoot::PSOStartGraceSeconds && !FShaderPipelineCache::IsPrecompiling())
			{
				UE_LOG(LogGFBoot, Warning,
					TEXT("No PSO precompiles reported after %.1fs -- this build has no usable .upipelinecache. ")
					TEXT("Shipping without one means the hitching happens in-game instead of here."),
					GEBoot::PSOStartGraceSeconds);
				OutStageProgress = 1.0f;
				return true;
			}
			return false;
		}

		OutStageProgress = (PSOPeakRemaining > 0)
			? 1.0f - (static_cast<float>(Remaining) / static_cast<float>(PSOPeakRemaining))
			: 1.0f;

		return Remaining == 0 && !FShaderPipelineCache::IsPrecompiling();
	}

	case EGF_GEBootStageKind::AssetPreload:
	{
		if (!CurrentHandle.IsValid())
		{
			OutStageProgress = 1.0f;
			return true;
		}
		OutStageProgress = CurrentHandle->GetProgress();
		return CurrentHandle->HasLoadCompleted();
	}

	case EGF_GEBootStageKind::NiagaraPrecache:
	{
		// Last frame's components have had their InitializeSystem run by now.
		DestroyWarmingComponents();

		if (NiagaraTotal == 0)
		{
			OutStageProgress = 1.0f;
			return true;
		}

		if (!bNiagaraSpawnComplete)
		{
			// Wait for the load before spawning anything.
			if (CurrentHandle.IsValid() && !CurrentHandle->HasLoadCompleted())
			{
				OutStageProgress = CurrentHandle->GetProgress() * 0.25f;
				return false;
			}

			UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			if (!World)
			{
				UE_LOG(LogGFBoot, Warning, TEXT("Niagara precache: no world to spawn into; skipping stage."));
				PendingNiagara.Reset();
				bNiagaraSpawnComplete = true;
				OutStageProgress = 1.0f;
				return true;
			}

			const int32 Batch = FMath::Max(1, Stage.SystemsPerFrame);
			for (int32 i = 0; i < Batch && PendingNiagara.Num() > 0; i++)
			{
				const TSoftObjectPtr<UNiagaraSystem> Soft = PendingNiagara.Pop(EAllowShrinking::No);
				UNiagaraSystem* System = Soft.Get();
				if (!System)
				{
					continue;
				}

				// Activating runs InitializeSystem, which calls PrecachePSOs(). The
				// component is never attached to anything visible and is destroyed on
				// the next tick -- nothing is drawn.
				UNiagaraComponent* Comp = NewObject<UNiagaraComponent>(World);
				if (!Comp)
				{
					continue;
				}
				Comp->SetAsset(System);
				Comp->SetAutoDestroy(false);
				Comp->SetVisibility(false);
				Comp->bAutoActivate = false;
				Comp->RegisterComponentWithWorld(World);
				Comp->Activate(true);

				WarmingComponents.Add(Comp);
			}

			const int32 Done = NiagaraTotal - PendingNiagara.Num();
			OutStageProgress = 0.25f + 0.65f * (static_cast<float>(Done) / static_cast<float>(NiagaraTotal));

			if (PendingNiagara.Num() == 0)
			{
				bNiagaraSpawnComplete = true;
				UE_LOG(LogGFBoot, Display, TEXT("Niagara precache: all %d system(s) spawned; draining PSO queue."), NiagaraTotal);
			}
			return false;
		}

		// Everything spawned -- wait for the RHI to work through the precache queue.
		const uint32 Outstanding = PipelineStateCache::NumActivePrecacheRequests();
		OutStageProgress = (Outstanding == 0) ? 1.0f : 0.9f;
		return Outstanding == 0 && WarmingComponents.Num() == 0;
	}

	case EGF_GEBootStageKind::Settle:
	default:
	{
		const float Duration = (Stage.MaxSeconds > 0.0f) ? Stage.MaxSeconds : GEBoot::DefaultSettleSeconds;
		OutStageProgress = FMath::Clamp(static_cast<float>(Elapsed) / Duration, 0.0f, 1.0f);
		return Elapsed >= Duration;
	}
	}
}

void UGF_BootSubsystem::FinishStage(bool bTimedOut)
{
	const FGF_GEBootStage& Stage = ActivePlan->Stages[CurrentStage];
	const double Seconds = FPlatformTime::Seconds() - StageStartTime;

	StageTimings.Add(FString::Printf(TEXT("  %d. %-28s %6.2fs%s"),
		CurrentStage + 1,
		*Stage.DisplayText.ToString(),
		Seconds,
		bTimedOut ? TEXT("  [TIMED OUT]") : TEXT("")));

	if (bTimedOut)
	{
		UE_LOG(LogGFBoot, Warning, TEXT("Stage %d ('%s') hit its %.1fs limit and was forced complete."),
			CurrentStage + 1, *Stage.DisplayText.ToString(), Stage.MaxSeconds);

		if (Stage.Kind == EGF_GEBootStageKind::AssetPreload && CurrentHandle.IsValid())
		{
			// Leave it loading in the background rather than cancelling -- the
			// handle is already in KeepAliveHandles, so it lands eventually.
			UE_LOG(LogGFBoot, Warning, TEXT("  preload was %.0f%% done; it continues in the background."),
				CurrentHandle->GetProgress() * 100.0f);
		}
	}

	CompletedWeight += FMath::Max(Stage.Weight, 0.01f);
	CurrentHandle.Reset();
}

void UGF_BootSubsystem::FinishBoot(const FString& Reason)
{
	if (ActivePlan && ActivePlan->bLogTimings)
	{
		FString Report = FString::Printf(TEXT("Boot finished in %.2fs (%s)\n"),
			FPlatformTime::Seconds() - BootStartTime, *Reason);
		for (const FString& Line : StageTimings)
		{
			Report += Line + TEXT("\n");
		}
		LastReport = Report;
		UE_LOG(LogGFBoot, Display, TEXT("%s"), *Report);
	}
	else
	{
		LastReport = FString::Printf(TEXT("Boot finished in %.2fs (%s)"),
			FPlatformTime::Seconds() - BootStartTime, *Reason);
	}

	// Hand the PSO cache back to trickle mode so anything still outstanding keeps
	// compiling during the menu without competing for the frame.
	FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);

	DestroyWarmingComponents();
	PendingNiagara.Reset();

	bRunning = false;
	bFinished = true;
	CurrentStage = INDEX_NONE;
	RawProgress = 1.0f;
	DisplayProgress = 1.0f;
	CurrentHandle.Reset();

	OnBootProgress.Broadcast(1.0f, StatusText);
	OnBootFinished.Broadcast();
}

float UGF_BootSubsystem::ComputeRawProgress(float StageProgress) const
{
	if (TotalWeight <= 0.0f || CurrentStage == INDEX_NONE)
	{
		return 0.0f;
	}
	const float StageWeight = FMath::Max(ActivePlan->Stages[CurrentStage].Weight, 0.01f);
	return FMath::Clamp((CompletedWeight + StageProgress * StageWeight) / TotalWeight, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------

void UGF_BootSubsystem::Tick(float DeltaTime)
{
	if (!bRunning || !ActivePlan)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();

	// Whole-sequence ceiling. A boot screen that can hang forever is a softlock
	// that only ever reproduces on someone else's machine.
	if (ActivePlan->GlobalTimeoutSeconds > 0.0f && (Now - BootStartTime) > ActivePlan->GlobalTimeoutSeconds)
	{
		if (ActivePlan->Stages.IsValidIndex(CurrentStage))
		{
			UE_LOG(LogGFBoot, Error, TEXT("Global boot timeout (%.0fs) hit during stage %d ('%s')."),
				ActivePlan->GlobalTimeoutSeconds, CurrentStage + 1,
				*ActivePlan->Stages[CurrentStage].DisplayText.ToString());
			FinishStage(/*bTimedOut*/ true);
		}
		FinishBoot(TEXT("global timeout"));
		return;
	}

	float StageProgress = 1.0f;

	if (!bStagesComplete)
	{
		const FGF_GEBootStage& Stage = ActivePlan->Stages[CurrentStage];
		bool bStageDone = TickStage(DeltaTime, StageProgress);
		bool bStageTimedOut = false;

		if (!bStageDone && Stage.MaxSeconds > 0.0f && (Now - StageStartTime) > Stage.MaxSeconds)
		{
			bStageDone = true;
			bStageTimedOut = true;
		}

		if (bStageDone)
		{
			StageProgress = 1.0f;
			FinishStage(bStageTimedOut);

			const int32 Next = CurrentStage + 1;
			if (ActivePlan->Stages.IsValidIndex(Next))
			{
				BeginStage(Next);
			}
			else
			{
				bStagesComplete = true;
				StagesCompleteTime = Now;
			}
		}
	}

	RawProgress = bStagesComplete ? 1.0f : ComputeRawProgress(StageProgress);

	// Monotonic and speed-limited: the bar never jumps and never walks backwards,
	// which is most of what makes a loading screen feel unpolished.
	if (ActivePlan->ProgressFillSpeed > 0.0f)
	{
		DisplayProgress = FMath::FInterpConstantTo(DisplayProgress, RawProgress, DeltaTime, ActivePlan->ProgressFillSpeed);
	}
	else
	{
		DisplayProgress = RawProgress;
	}
	DisplayProgress = FMath::Max(DisplayProgress, 0.0f);

	OnBootProgress.Broadcast(DisplayProgress, StatusText);

	if (bStagesComplete)
	{
		const bool bMinTimeMet = (Now - BootStartTime) >= ActivePlan->MinimumDisplaySeconds;
		const bool bBarFull = DisplayProgress >= 0.999f;
		if (bMinTimeMet && bBarFull)
		{
			FinishBoot(TEXT("all stages complete"));
		}
	}
}

// ---------------------------------------------------------------------------
// asset gathering
// ---------------------------------------------------------------------------

void UGF_BootSubsystem::GatherStageAssets(const FGF_GEBootStage& Stage, TArray<FSoftObjectPath>& OutPaths) const
{
	TSet<FSoftObjectPath> Unique;

	for (const TSoftObjectPtr<UObject>& Soft : Stage.Assets)
	{
		if (!Soft.IsNull())
		{
			Unique.Add(Soft.ToSoftObjectPath());
		}
	}

	if (Stage.ContentFolders.Num() > 0)
	{
		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = Module.Get();

#if WITH_EDITOR
		// In the editor the registry may still be scanning; a partial scan would
		// silently preload a fraction of the folder.
		if (Registry.IsLoadingAssets())
		{
			Registry.WaitForCompletion();
		}
#endif

		FARFilter Filter;
		Filter.bRecursivePaths = Stage.bRecursiveFolders;
		for (const FString& Folder : Stage.ContentFolders)
		{
			if (!Folder.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*Folder));
			}
		}
		for (const TSubclassOf<UObject>& Class : Stage.FolderClassFilter)
		{
			if (Class)
			{
				Filter.ClassPaths.Add(Class->GetClassPathName());
			}
		}
		Filter.bRecursiveClasses = Filter.ClassPaths.Num() > 0;

		if (Filter.PackagePaths.Num() > 0)
		{
			TArray<FAssetData> Found;
			Registry.GetAssets(Filter, Found);

			int32 Added = 0;
			int32 Dropped = 0;
			for (const FAssetData& Data : Found)
			{
				if (Added >= Stage.MaxAssetsFromFolders)
				{
					++Dropped;
					continue;
				}
				bool bAlready = false;
				Unique.Add(Data.ToSoftObjectPath(), &bAlready);
				if (!bAlready)
				{
					++Added;
				}
			}

			if (Dropped > 0)
			{
				// Never truncate silently -- a capped sweep reads as full coverage.
				UE_LOG(LogGFBoot, Warning,
					TEXT("Folder sweep for '%s' hit MaxAssetsFromFolders (%d) and skipped %d asset(s). ")
					TEXT("Narrow the folders or the class filter rather than raising the cap."),
					*Stage.DisplayText.ToString(), Stage.MaxAssetsFromFolders, Dropped);
			}
		}
	}

	OutPaths = Unique.Array();
}

void UGF_BootSubsystem::GatherNiagaraSystems(const FGF_GEBootStage& Stage, TArray<FSoftObjectPath>& OutPaths) const
{
	if (Stage.ContentFolders.Num() == 0)
	{
		return;
	}

	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Module.Get();

#if WITH_EDITOR
	if (Registry.IsLoadingAssets())
	{
		Registry.WaitForCompletion();
	}
#endif

	FARFilter Filter;
	Filter.bRecursivePaths = Stage.bRecursiveFolders;
	Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	for (const FString& Folder : Stage.ContentFolders)
	{
		if (!Folder.IsEmpty())
		{
			Filter.PackagePaths.Add(FName(*Folder));
		}
	}

	TArray<FAssetData> Found;
	Registry.GetAssets(Filter, Found);

	int32 Dropped = 0;
	for (const FAssetData& Data : Found)
	{
		if (OutPaths.Num() >= Stage.MaxAssetsFromFolders)
		{
			++Dropped;
			continue;
		}
		OutPaths.AddUnique(Data.ToSoftObjectPath());
	}

	if (Dropped > 0)
	{
		UE_LOG(LogGFBoot, Warning,
			TEXT("Niagara sweep for '%s' hit MaxAssetsFromFolders (%d) and skipped %d system(s)."),
			*Stage.DisplayText.ToString(), Stage.MaxAssetsFromFolders, Dropped);
	}
}

void UGF_BootSubsystem::DestroyWarmingComponents()
{
	for (TObjectPtr<UNiagaraComponent>& Comp : WarmingComponents)
	{
		if (Comp)
		{
			Comp->Deactivate();
			Comp->DestroyComponent();
		}
	}
	WarmingComponents.Reset();
}

// ---------------------------------------------------------------------------
// console
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GEBootReportCmd(
	TEXT("gf.Boot.Report"),
	TEXT("Print the last boot sequence's per-stage timings."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || !World->GetGameInstance())
			{
				return;
			}
			if (const UGF_BootSubsystem* Boot = World->GetGameInstance()->GetSubsystem<UGF_BootSubsystem>())
			{
				const FString Report = Boot->GetBootReport();
				UE_LOG(LogGFBoot, Display, TEXT("%s"), Report.IsEmpty() ? TEXT("No boot has run this session.") : *Report);
			}
		}));

static FAutoConsoleCommandWithWorldAndArgs GEBootPSOStatusCmd(
	TEXT("gf.Boot.PSOStatus"),
	TEXT("Print outstanding PSO precompiles and whether the pipeline cache is active."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UE_LOG(LogGFBoot, Display, TEXT("PSO precompiles remaining: %u | precompiling: %s | batching paused: %s"),
				FShaderPipelineCache::NumPrecompilesRemaining(),
				FShaderPipelineCache::IsPrecompiling() ? TEXT("yes") : TEXT("no"),
				FShaderPipelineCache::IsBatchingPaused() ? TEXT("yes") : TEXT("no"));
		}));
