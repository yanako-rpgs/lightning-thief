// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureMemoLibrary.generated.h"

/**
 * Everything the summary screen's tamer-memo panel needs, resolved in one call.
 *
 * The individual lines are exposed as well as the joined MemoText so a widget can
 * either drop MemoText into a single text block or lay the lines out separately
 * (different fonts, the location on its own row, and so on).
 *
 * For an egg every line except EggProgressLine is still filled in — the egg's own
 * received date and place are real data — but MemoText is JUST the hatch progress,
 * because that is what the games show while it's an egg.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_CreatureMemoInfo
{
	GENERATED_BODY()

	// True while this entry is an unhatched egg. MemoText is the hatch hint in that case.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	bool bIsEgg = false;

	// False when the Creature predates the memo system (or was never stamped), so the
	// date / location / met lines are empty. Hide the panel rows rather than showing blanks.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	bool bHasOrigin = false;

	// "Ferocious temperament."
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText TemperamentLine;

	// "Raises Attack, lowers Magic." Empty for a neutral nature. Not part of MemoText —
	// it's extra flavour for the stat page if you want it.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText TemperamentEffectLine;

	// "Aug. 5, 2026"
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText DateLine;

	// "Route 101"
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText LocationLine;

	// "Met at Lv. 5." / "Egg hatched." / "Egg received."
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText MetLine;

	// Egg only: "It appears to move occasionally. It may be close to hatching."
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText EggProgressLine;

	// True when the Original Tamer is somebody other than the player -- a traded
	// Creature, or an event gift. The memo should then be told from the OT's point
	// of view, because "You met it in Tidecliff City" is a lie to a player who has
	// never been there.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	bool bIsOutsider = false;

	// The Original Tamer's name, for phrasing an outsider's memo.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText OriginalTamerName;

	// "Its magnets are damaged. It cannot evolve." Empty for almost everything.
	// From FGF_CreatureInstanceData::MemoNote, falling back to a generic line when the
	// Creature cannot evolve and no note was written.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText NoteLine;

	// The whole origin as ONE sentence, already phrased for the right person:
	// "You met Zappy on Aug. 19, 2026 in Tidecliff City at Lv. 5." or
	// "John met Zappy on Aug. 19, 2026 in Tidecliff City at Lv. 5."
	//
	// Bind a summary screen to this rather than reassembling the parts, so the
	// outsider phrasing cannot be forgotten in one screen and not another. The
	// individual lines above are still there for layouts that want them apart.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText MetSentence;

	// The whole memo, newline-joined and ready for one multi-line text block.
	UPROPERTY(BlueprintReadOnly, Category = "Memo")
	FText MemoText;
};

/**
 * Tamer memo: where and when the player met a Creature, what nature it has, and
 * how close an egg is to hatching.
 *
 * Origin data is written ONCE, by StampMetInfo, at the moment a Creature becomes the
 * player's. Every give path in the game funnels through the two subsystem functions
 * that call StampMetInfoIfUnset, so ordinary catches, gifts and eggs are covered with
 * no Blueprint work. Call StampMetInfo explicitly only when a scripted event wants
 * different wording or a location the player isn't standing in.
 *
 * Nothing here mutates a Creature that already has a memo. That is what lets a traded
 * Creature keep the original tamer's memo instead of being re-stamped with the
 * receiver's route the moment it lands in their party.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureMemoLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	//====================================================================================
	// READING - summary screen
	//====================================================================================

	/**
	 * One-stop node for the memo panel. Pulls the player name from the save itself,
	 * so the widget doesn't have to fetch it to resolve routes named "{PlayerName}'s House".
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Tamer Memo"))
	static FGF_CreatureMemoInfo GetTamerMemo(const UObject* WorldContextObject, const FGF_CreatureInstanceData& Creature);

	/**
	 * The classic hatch hint, chosen from EggCyclesRemaining. Empty text for a
	 * Creature that isn't an egg.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static FText GetEggProgressText(const FGF_CreatureInstanceData& Creature);

	/** "Ferocious", straight from the enum's display name. */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static FText GetTemperamentDisplayName(EGF_Temperament Temperament);

	/** "Ferocious temperament." */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static FText GetTemperamentLine(EGF_Temperament Temperament);

	/** "Raises Attack, lowers Magic." Empty for the five neutral natures. */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static FText GetTemperamentEffectLine(EGF_Temperament Temperament);

	/** "Aug. 5, 2026". Empty text for a zero (never stamped) date. */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static FText FormatMetDate(const FDateTime& MetDate);

	/**
	 * The place name for the memo: MetLocationOverride if one was set, otherwise the
	 * route's display name with {PlayerName} substituted. Empty if neither is known.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static FText GetMetLocationText(const FGF_CreatureInstanceData& Creature, const FString& PlayerName);

	/** False for a Creature that was never stamped — old saves, mostly. */
	UFUNCTION(BlueprintPure, Category = "Creature|Memo")
	static bool HasOriginInfo(const FGF_CreatureInstanceData& Creature);

	//====================================================================================
	// WRITING - obtain paths
	//====================================================================================

	/**
	 * Record where and when the player got this Creature. Overwrites whatever was
	 * there, so use it for scripted events that need specific wording.
	 *
	 * @param MetLevelOverride  0 uses the Creature's current level, which is what you
	 *                          want for anything handed over at the level it shows.
	 * @param LocationOverride  Non-empty to name a place instead of using the current
	 *                          route ("Sanctuary Couple", "Eastern Isles", a person's name).
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Memo", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "MetLevelOverride,LocationOverride"))
	static void StampMetInfo(const UObject* WorldContextObject, UPARAM(ref) FGF_CreatureInstanceData& Creature,
		EGF_CreatureMetType MetType, int32 MetLevelOverride = 0, const FString& LocationOverride = TEXT(""));

	/**
	 * Same, but leaves an already-stamped Creature alone. This is the one the give
	 * paths call — it's what stops a traded Creature from being re-stamped with the
	 * receiver's location the moment it enters their party.
	 *
	 * @return true if it actually wrote anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Memo", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "MetLevelOverride,LocationOverride"))
	static bool StampMetInfoIfUnset(const UObject* WorldContextObject, UPARAM(ref) FGF_CreatureInstanceData& Creature,
		EGF_CreatureMetType MetType, int32 MetLevelOverride = 0, const FString& LocationOverride = TEXT(""));
};
