#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GF_BootPlan.generated.h"

/**
 * What a single boot stage actually does.
 *
 * Deliberately small. The old MAP_PSO screen (BP_ShaderCompiler from the
 * ShaderCompilationScreen plugin) had exactly one strategy -- load EVERY UMaterial
 * and EVERY NiagaraSystem in the build and draw them in front of a camera -- which
 * on this project meant ~650 materials and ~300 Niagara systems, most of them from
 * marketplace packs the game never uses, plus a PreloadAllSpeciesData call that
 * pulled ~4.18 GB of Creature sprites in before the title screen. This enum is the
 * replacement: the boot screen no longer guesses, it runs a list you author.
 */
UENUM(BlueprintType)
enum class EGF_GEBootStageKind : uint8
{
	/**
	 * Drain the shipped .upipelinecache through the RHI.
	 * Loads no assets and allocates no textures -- it hands precompiled PSO
	 * descriptions straight to the driver. This is the stage that actually stops
	 * first-encounter hitching, and it is the one the material loop was a bad
	 * approximation of.
	 */
	ShaderPipelineCache,

	/**
	 * Async-load a curated set of assets and HOLD them resident for the session.
	 * The subsystem keeps the streamable handles alive, so these do not get
	 * garbage collected the moment the boot map unloads.
	 */
	AssetPreload,

	/**
	 * Spawn each Niagara system briefly so the RHI precaches its PSOs.
	 *
	 * This is what stops a move stuttering the first time it is used. Activating a
	 * UNiagaraComponent runs InitializeSystem, which calls PrecachePSOs() -- so the
	 * pipeline states get built WITHOUT the system ever being drawn.
	 *
	 * The marketplace ShaderCompilationScreen plugin achieved the same thing by
	 * loading every material and Niagara system in the project and rendering them to
	 * an ortho camera, which is why it ran at 3-5 FPS: ~650 materials and ~300 systems,
	 * most of them from packs this game never uses. Point this at your own effect
	 * folders instead and nothing is rendered at all.
	 */
	NiagaraPrecache,

	/**
	 * Do nothing for a moment. Lets the render thread and the streaming pool catch
	 * up so the first real frame after the boot screen is not a stutter. Also
	 * useful as a deliberate beat before a transition.
	 */
	Settle
};

/** One step of the boot sequence. */
USTRUCT(BlueprintType)
struct FGF_GEBootStage
{
	GENERATED_BODY()

	/** Shown on the boot screen while this stage runs. Localizable, unlike a UENUM
	 *  DisplayName -- those are stripped from cooked builds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot Stage")
	FText DisplayText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot Stage")
	EGF_GEBootStageKind Kind = EGF_GEBootStageKind::AssetPreload;

	/** Relative share of the overall progress bar. A stage you know takes 10x
	 *  longer should be weighted 10x, otherwise the bar lies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot Stage", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;

	/** Hard ceiling for this stage. On expiry the stage is forced complete and a
	 *  warning is logged (it will show up in a tester's packaged log). 0 = no limit,
	 *  but the plan's GlobalTimeoutSeconds still applies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot Stage", meta = (ClampMin = "0.0"))
	float MaxSeconds = 0.0f;

	// ---------------------------------------------------------------- AssetPreload

	/** Explicit assets to warm. Soft refs -- listing something here does NOT make
	 *  the boot plan hard-reference it, so this asset stays cheap to load. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset Preload",
		meta = (EditCondition = "Kind == EGF_GEBootStageKind::AssetPreload", EditConditionHides))
	TArray<TSoftObjectPtr<UObject>> Assets;

	/** Content folders to sweep via the asset registry, e.g. "/Game/SPRITES/UI".
	 *  Used by BOTH AssetPreload and NiagaraPrecache -- the latter collects only
	 *  UNiagaraSystem assets found under them. Saves hand-listing hundreds of files. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folders",
		meta = (EditCondition = "Kind == EGF_GEBootStageKind::AssetPreload || Kind == EGF_GEBootStageKind::NiagaraPrecache", EditConditionHides))
	TArray<FString> ContentFolders;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folders",
		meta = (EditCondition = "Kind == EGF_GEBootStageKind::AssetPreload || Kind == EGF_GEBootStageKind::NiagaraPrecache", EditConditionHides))
	bool bRecursiveFolders = true;

	// ------------------------------------------------------------ NiagaraPrecache
	//
	// Uses the shared Folders section below; the class filter is implicit there
	// (only UNiagaraSystem assets are collected).

	/** How many systems to spawn per frame. Each one instantiates briefly, so a big
	 *  number finishes sooner at the cost of a longer frame. 4 is unnoticeable on a
	 *  loading screen; 1 is the safest on a weak machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara Precache",
		meta = (EditCondition = "Kind == EGF_GEBootStageKind::NiagaraPrecache", EditConditionHides, ClampMin = "1", ClampMax = "32"))
	int32 SystemsPerFrame = 4;

	/** Optional class filter for the folder sweep. Empty = every asset in the folder.
	 *  Set this to Texture2D / PaperFlipbook / SoundWave to keep a sweep honest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset Preload",
		meta = (EditCondition = "Kind == EGF_GEBootStageKind::AssetPreload", EditConditionHides))
	TArray<TSubclassOf<UObject>> FolderClassFilter;

	/** Safety valve. A mis-pointed folder on this project can drag gigabytes; the
	 *  sweep stops here and logs exactly how many assets it dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folders",
		meta = (EditCondition = "Kind == EGF_GEBootStageKind::AssetPreload || Kind == EGF_GEBootStageKind::NiagaraPrecache", EditConditionHides, ClampMin = "1"))
	int32 MaxAssetsFromFolders = 2000;
};

/**
 * The boot sequence, as data.
 *
 * Create one of these (Miscellaneous > Data Asset > GE Boot Plan), point
 * Project Settings > Game > Gamma Framework Boot at it, and the boot screen runs it.
 * Changing what the game warms up is then an editor edit, not a code change.
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKWORLD_API UGF_BootPlan : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (TitleProperty = "DisplayText"))
	TArray<FGF_GEBootStage> Stages;

	/** Whole-sequence ceiling. If this expires the boot finishes anyway, with a
	 *  warning naming the stage that was stuck. A boot screen that can hang forever
	 *  is a shipped softlock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (ClampMin = "5.0"))
	float GlobalTimeoutSeconds = 180.0f;

	/** Keep the screen up at least this long even if everything finished instantly.
	 *  Stops the boot screen flashing past on a fast machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (ClampMin = "0.0"))
	float MinimumDisplaySeconds = 1.5f;

	/** Progress-bar fill rate cap, in bar-fractions per second. The bar is
	 *  monotonic and speed-limited, so a stage that completes in one frame slides
	 *  instead of snapping. 0 = no smoothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (ClampMin = "0.0"))
	float ProgressFillSpeed = 0.6f;

	/** Logged (UE_LOG, so it reaches packaged logs) when the sequence finishes.
	 *  Leave on -- this is how you read a tester's boot times with no repro. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot")
	bool bLogTimings = true;
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Gamma Framework Boot"))
class GAMMAFRAMEWORKWORLD_API UGF_BootSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Never null in practice -- falls back to the CDO. */
	static const UGF_BootSettings* Get();

	UPROPERTY(config, EditAnywhere, Category = "Boot")
	TSoftObjectPtr<UGF_BootPlan> DefaultBootPlan;

	UPROPERTY(config, EditAnywhere, Category = "Boot")
	bool bSkipBootInEditor = true;
};
