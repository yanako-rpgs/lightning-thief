#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "GF_SettingsTypes.generated.h"

class UTexture2D;

/**
 * Shared types for the player-facing options screen.
 *
 * The authoritative store for these values is UGF_SettingsSubsystem, which keeps
 * its own save slot ("GEOptions") so options survive New Game / Delete Save and
 * are available on the title screen before any game save is loaded. The matching
 * fields on UGF_VaultSystem are kept in sync by the subsystem so existing
 * Blueprint reads of VaultSystem keep working.
 */

// ------------------------------------------------------------------
// ENUMS
// ------------------------------------------------------------------

// How fast the dialogue typewriter reveals characters.
// Instant skips the animation entirely (full line appears at once).
UENUM(BlueprintType)
enum class EGF_TextSpeed : uint8
{
	Slow    UMETA(DisplayName = "Slow"),
	Normal  UMETA(DisplayName = "Normal"),
	Fast    UMETA(DisplayName = "Fast"),
	Instant UMETA(DisplayName = "Instant")
};

UENUM(BlueprintType)
enum class EGF_Difficulty : uint8
{
	Easy   UMETA(DisplayName = "Easy"),
	Normal UMETA(DisplayName = "Normal"),
	Hard   UMETA(DisplayName = "Hard")
};

UENUM(BlueprintType)
enum class EGF_WindowMode : uint8
{
	Fullscreen         UMETA(DisplayName = "Fullscreen"),
	WindowedFullscreen UMETA(DisplayName = "Borderless"),
	Windowed           UMETA(DisplayName = "Windowed")
};

// ------------------------------------------------------------------
// DIALOGUE FRAME
// ------------------------------------------------------------------

/**
 * One selectable dialogue box skin. Fill the DialogueFrames array in
 * Project Settings > Game > Gamma Framework Settings, one entry per frame the
 * player can pick. The player's choice is stored as an index (FrameID), so
 * keep the array order stable once a build ships or saved FrameIDs will point
 * at a different frame.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKWORLD_API FGF_DialogueFrame
{
	GENERATED_BODY()

	// Shown in the options screen next to the preview.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	FText DisplayName;

	// The frame artwork. Soft ref so unpicked frames never cook into memory.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	TSoftObjectPtr<UTexture2D> FrameTexture;

	// When true the brush is built as a Vault (nine-slice) so the frame stretches
	// without distorting its corners. Turn off for a fixed-size image.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	bool bUseNineSlice = true;

	// Nine-slice margins as a fraction of the texture (0-1 per side).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame", meta = (EditCondition = "bUseNineSlice"))
	FMargin NineSliceMargin = FMargin(0.25f);

	// Multiplied over the frame texture. Leave white for unmodified art.
	// Frame art only — dialogue text styling is owned by the RichTextBlock's
	// style DataTable and is deliberately not a player setting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	FLinearColor FrameTint = FLinearColor::White;
};

// ------------------------------------------------------------------
// DIFFICULTY PROFILE
// ------------------------------------------------------------------

/**
 * Everything one difficulty setting changes. Tunable in Project Settings so
 * balance passes do not need a recompile.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKWORLD_API FGF_DifficultyProfile
{
	GENERATED_BODY()

	// Scales every EXP award (both battlers and EXP Share recipients).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0"))
	float EXPMultiplier = 1.0f;

	// Shifts every tamer's editor-configured AIDifficulty by this many steps.
	// -1 makes Expert tamers behave as Smart, +1 promotes Smart to Expert.
	// Clamped to the ends of EGF_TamerAIDifficulty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "-3", ClampMax = "3"))
	int32 AIDifficultyOffset = 0;

	// When true, party Creature stop gaining EXP once they hit the emblem level cap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
	bool bEnforceLevelCap = false;

	// Level cap by emblem count. Index 0 = no emblems, index 8 = all eight.
	// Only consulted when bEnforceLevelCap is true. A short array clamps to its
	// last entry, an empty array disables the cap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (EditCondition = "bEnforceLevelCap"))
	TArray<int32> LevelCapPerEmblemCount;

	// When false the EXP Share option is locked and forced to bForcedEXPShare.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
	bool bAllowEXPShareToggle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (EditCondition = "!bAllowEXPShareToggle"))
	bool bForcedEXPShare = false;

	// Fraction of the battler's EXP given to party members that did not fight,
	// while EXP Share is on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EXPShareRatio = 0.25f;
};
