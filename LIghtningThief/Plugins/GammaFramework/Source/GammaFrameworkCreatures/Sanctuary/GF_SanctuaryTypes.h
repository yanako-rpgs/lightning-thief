// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GF_CreatureInstanceData.h"
#include "GF_SanctuaryTypes.generated.h"

/** The sanctuary holds exactly two Creature, like every classic game. */
#define SANCTUARY_SLOT_COUNT 2

/**
 * How well the two Creature in the sanctuary get along. The value of each entry is
 * the percent chance per 256 steps that an egg appears, straight out of classic's
 * GetSanctuaryCompatibilityScore.
 */
UENUM(BlueprintType)
enum class EGF_SanctuaryCompatibility : uint8
{
	// Can't breed at all: no breeding tags, two universal breeders, same gender,
	// genderless parents, or no shared breeding group.
	Incompatible	= 0		UMETA(DisplayName = "Incompatible (0%)"),

	// Different species + same OT, or Shifter + same OT.
	Low				= 20	UMETA(DisplayName = "Low (20%)"),

	// Same species + same OT, different species + different OT, or Shifter + different OT.
	Medium			= 50	UMETA(DisplayName = "Medium (50%)"),

	// Same species from two different tamers — the ideal pairing.
	High			= 70	UMETA(DisplayName = "High (70%)")
};

/**
 * One of the two sanctuary slots. The Creature is stored here verbatim; the EXP it
 * earns while boarded is tracked separately as StepsAccumulated and only applied
 * when the player takes it back, which is how classic does it (and it's why the
 * sanctuary man can quote a price before you commit).
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_SanctuarySlot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	bool bOccupied = false;

	// The boarded Creature, exactly as it was deposited. Level/EXP are NOT updated
	// while it sits here — call the subsystem's preview helpers to see the
	// level it would come out at.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	FGF_CreatureInstanceData Mon;

	// Level the Creature had when it was handed over. The withdrawal fee is
	// 100 + 100 * (levels gained since this).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	int32 LevelOnDeposit = 0;

	// One step walked = one EXP earned, so this doubles as the pending EXP total.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	int32 StepsAccumulated = 0;
};

/**
 * Everything the sanctuary persists. Lives on UGF_VaultSystem so it rides along
 * with the normal save file.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_SanctuaryData
{
	GENERATED_BODY()

	// Always SANCTUARY_SLOT_COUNT entries; the subsystem guarantees the size.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	TArray<FGF_SanctuarySlot> Slots;

	// Counts 0-255. Every time it wraps, the game rolls for an egg and burns one
	// egg cycle off every egg in the party.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	int32 StepCounter = 0;

	// True when the sanctuary man is standing outside holding an egg for the player.
	// The egg itself isn't built until the player accepts it.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	bool bEggWaiting = false;

	// Lifetime count of eggs the player has collected here. Purely for flavour text.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	int32 EggsCollected = 0;

	FGF_SanctuaryData()
	{
		Slots.SetNum(SANCTUARY_SLOT_COUNT);
	}
};

/**
 * Flattened, ready-to-display snapshot of one sanctuary slot, so the sanctuary UI can
 * fill a row with a single call instead of resolving species assets itself.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_SanctuarySlotPreview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	bool bOccupied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	FName DisplayName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	FName SpeciesName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	UGF_CreatureSpeciesData* Species = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	EGF_CreatureGender Gender = EGF_CreatureGender::Genderless;

	// Level it was deposited at.
	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	int32 LevelOnDeposit = 0;

	// Level it would be at right now if withdrawn — what the sanctuary lady quotes.
	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	int32 CurrentLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	int32 LevelsGained = 0;

	// Money owed to get it back: 100 + 100 * LevelsGained.
	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	int32 Cost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Sanctuary")
	bool bIsUnique = false;
};
