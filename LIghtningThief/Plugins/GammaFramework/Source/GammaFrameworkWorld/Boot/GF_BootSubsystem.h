#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Engine/StreamableManager.h"
#include "GF_ResettableState.h"
#include "GF_BootSubsystem.generated.h"

class UGF_BootPlan;
class UNiagaraSystem;
class UNiagaraComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogGFBoot, Log, All);

/** Progress is 0..1 across the WHOLE plan, already smoothed. StatusText is the
 *  current stage's DisplayText. Fires every frame the boot is running. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_GEBootProgressSignature, float, Progress, const FText&, StatusText);

/** Fires exactly once per StartBoot, including on timeout or abort. Bind this
 *  instead of polling -- it is the only safe place to open the next level. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_GEBootFinishedSignature);

/**
 * Runs the game's boot sequence and reports honest progress.
 *
 * Replaces the ShaderCompilationScreen plugin's BP_ShaderCompiler. That actor
 * measured progress as "materials and Niagara systems loaded so far", which meant
 * the bar was tracking asset loads that had nothing to do with the shaders the game
 * needs, while the widget separately called PreloadAllSpeciesData and dragged the
 * entire ~4.18 GB sprite set in before the title screen. Here:
 *
 *   - the shader stage drains the shipped .upipelinecache and reports the RHI's own
 *     outstanding-PSO count, so the bar is the actual remaining work and no assets
 *     are loaded at all;
 *   - the preload stage loads only what a UGF_BootPlan lists, asynchronously, and
 *     keeps the handles so the work is not thrown away on the next GC;
 *   - every stage has a timeout, so this screen cannot become a permanent hang.
 *
 * Lives on the GameInstance, so it survives the OpenLevel out of the boot map --
 * the preloaded assets stay resident into the main menu and the first map.
 */
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_BootSubsystem : public UGameInstanceSubsystem, public FTickableGameObject, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------- lifecycle

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * IGF_ResettableState. Every GameInstance subsystem must implement this -- they
	 * survive OpenLevel, so anything not reset here leaks from one playthrough into
	 * the next, and GF_GameResetLibrary ensures on any that do not.
	 *
	 * The preloaded assets are deliberately KEPT. They are boot-time content warmed
	 * for the session, not player state, and dropping them would make the next run
	 * re-load everything for no benefit. What resets is the run-state: a boot left
	 * half-finished must not report itself as still running to the next one.
	 */
	virtual void ResetToBootState() override;

	// ---------------------------------------------------------------- control

	/**
	 * Begin the sequence. Pass null to use Project Settings > Game > Gamma Framework Boot.
	 * Safe to call again after it finished; calling it while running is ignored.
	 * If no plan can be resolved the boot completes immediately (and says so in the
	 * log) rather than leaving the screen up forever.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boot")
	void StartBoot(UGF_BootPlan* Plan = nullptr);

	/** Force-finish early. OnBootFinished still fires -- whatever opens the next
	 *  level stays wired to one event. */
	UFUNCTION(BlueprintCallable, Category = "Boot")
	void AbortBoot(const FString& Reason);

	/** Drop the preloaded assets so they can be garbage collected. Only worth
	 *  calling if you deliberately want the memory back; the whole point of the
	 *  preload is that these stay resident. */
	UFUNCTION(BlueprintCallable, Category = "Boot")
	void ReleasePreloadedAssets();

	// ---------------------------------------------------------------- readout

	/** Smoothed, monotonic 0..1 for the progress bar. */
	UFUNCTION(BlueprintPure, Category = "Boot")
	float GetProgress() const { return DisplayProgress; }

	/** Unsmoothed 0..1. Use this if you are driving something that should not lag. */
	UFUNCTION(BlueprintPure, Category = "Boot")
	float GetRawProgress() const { return RawProgress; }

	UFUNCTION(BlueprintPure, Category = "Boot")
	FText GetStatusText() const { return StatusText; }

	/** e.g. "3 / 5" -- for a stage counter next to the bar. */
	UFUNCTION(BlueprintPure, Category = "Boot")
	void GetStageCounter(int32& OutStage, int32& OutTotal) const;

	/** Outstanding PSO compiles right now, or 0 outside the shader stage. Handy for
	 *  a "1,204 shaders remaining" line that is genuinely true. */
	UFUNCTION(BlueprintPure, Category = "Boot")
	int32 GetShaderPrecompilesRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Boot")
	bool IsBootRunning() const { return bRunning; }

	UFUNCTION(BlueprintPure, Category = "Boot")
	bool IsBootFinished() const { return bFinished; }

	/** Multi-line per-stage timing summary from the last run. Empty until it ends. */
	UFUNCTION(BlueprintPure, Category = "Boot")
	FString GetBootReport() const { return LastReport; }

	// ---------------------------------------------------------------- events

	UPROPERTY(BlueprintAssignable, Category = "Boot")
	FGF_GEBootProgressSignature OnBootProgress;

	UPROPERTY(BlueprintAssignable, Category = "Boot")
	FGF_GEBootFinishedSignature OnBootFinished;

	// ---------------------------------------------------------------- tickable

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bRunning && !HasAnyFlags(RF_ClassDefaultObject); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual TStatId GetStatId() const override;

private:
	// Per-stage drivers. Each returns true when the stage is complete, and writes
	// 0..1 progress within the stage to OutProgress.
	void  BeginStage(int32 StageIndex);
	bool  TickStage(float DeltaTime, float& OutStageProgress);
	void  FinishStage(bool bTimedOut);
	void  FinishBoot(const FString& Reason);

	/** Expand a stage's explicit list + folder sweeps into concrete paths. */
	void  GatherStageAssets(const struct FGF_GEBootStage& Stage, TArray<FSoftObjectPath>& OutPaths) const;

	/** Collect every UNiagaraSystem under the stage's ContentFolders. */
	void  GatherNiagaraSystems(const struct FGF_GEBootStage& Stage, TArray<FSoftObjectPath>& OutPaths) const;

	/** Tear down the components spawned last frame. Their PrecachePSOs has already
	 *  been issued by then, and the requests outlive the component. */
	void  DestroyWarmingComponents();

	float ComputeRawProgress(float StageProgress) const;

	UPROPERTY()
	TObjectPtr<UGF_BootPlan> ActivePlan = nullptr;

	// Handles are what keep the preloaded assets alive. Dropping these undoes the
	// entire preload on the next GC pass.
	TArray<TSharedPtr<FStreamableHandle>> KeepAliveHandles;
	TSharedPtr<FStreamableHandle> CurrentHandle;

	bool  bRunning = false;
	bool  bFinished = false;

	int32 CurrentStage = INDEX_NONE;
	double BootStartTime = 0.0;
	double StageStartTime = 0.0;
	float  CompletedWeight = 0.0f;
	float  TotalWeight = 0.0f;

	float  RawProgress = 0.0f;
	float  DisplayProgress = 0.0f;
	FText  StatusText;

	// ---------------------------------------------------------- NiagaraPrecache
	//
	// Spawning a UNiagaraComponent runs InitializeSystem, which calls PrecachePSOs().
	// That is the whole trick: the pipeline states get built without the system ever
	// being rendered, so there is none of the framerate collapse the old material
	// loop caused by drawing everything to a camera.

	// Systems still to be spawned this stage. Drained SystemsPerFrame at a time.
	TArray<TSoftObjectPtr<UNiagaraSystem>> PendingNiagara;
	int32 NiagaraTotal = 0;

	// Components spawned this frame batch, destroyed on the next tick once their
	// InitializeSystem has run.
	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> WarmingComponents;

	// Set once every system has been spawned; the stage then waits for the RHI's
	// outstanding precache count to drain.
	bool bNiagaraSpawnComplete = false;

	// Shader stage bookkeeping. NumPrecompilesRemaining only ever tells us what is
	// left, so the peak we have observed is our denominator.
	uint32 PSOPeakRemaining = 0;
	bool   bPSOSeenWork = false;

	// Set once all stages are done; the sequence then coasts to MinimumDisplaySeconds.
	bool   bStagesComplete = false;
	double StagesCompleteTime = 0.0;

	TArray<FString> StageTimings;
	FString LastReport;
};
