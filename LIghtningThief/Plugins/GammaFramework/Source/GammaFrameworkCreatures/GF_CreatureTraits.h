// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureTraitTypes.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_SkillDefinition.h"
// UGF_BattleComponent is referenced in comments only; no include needed.   // EGF_WeatherType
#include "GF_ElementTypes.h"
#include "GF_CreatureTraits.generated.h"

class AGF_Creature;
class UGF_CreatureSpeciesData;

/**
 * Result of an on-contact trait firing (Static, Venomspur, Sporeburst,
 * Beguile, Scorchhide, Bramblehide).
 *
 * The defender's trait fires when the ATTACKER lands a contact move, so
 * everything in here is applied TO THE ATTACKER.
 */
USTRUCT(BlueprintType)
struct FGF_TraitContactResult
{
	GENERATED_BODY()

	/** False when nothing happened — ignore every other field. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	bool bTriggered = false;

	/** Status to inflict on the attacker. None when the trait deals damage instead. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	EGF_STATUSEffect StatusToApply = EGF_STATUSEffect::None;

	/** Beguile — infatuate the attacker instead of applying a status condition. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	bool bInfatuate = false;

	/** Bramblehide — flat HP to subtract from the attacker (already 1/16 of its MaxHP). */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	float DamageToAttacker = 0.0f;

	/** Ready-to-print battle message, e.g. "Voltkit's Static paralyzed Brawnling!" */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	FString Message;
};

/**
 * Result of the end-of-turn trait pass (Moult, Rainfed).
 */
USTRUCT(BlueprintType)
struct FGF_TraitEndOfTurnResult
{
	GENERATED_BODY()

	/** False when nothing happened — ignore every other field. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	bool bTriggered = false;

	/** Rainfed — HP to restore. Already clamped so it can't overheal. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	float HealAmount = 0.0f;

	/** Moult — true when the Creature's status condition was cleared this tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	bool bStatusCured = false;

	/** Ready-to-print battle message. */
	UPROPERTY(BlueprintReadOnly, Category = "Traits")
	FString Message;
};

/**
 * Central home for every trait effect in the game.
 *
 * Design: traits are a plain enum (EGF_CreatureTrait) stored on the species asset
 * (Trait1 / Trait2) and baked onto each Creature at creation
 * (FGF_CreatureInstanceData::Trait). Every effect lives in a switch inside this library,
 * so there is exactly one place to look for "what does X do".
 *
 * Tier 1 traits are already folded into the existing formulas inside
 * UGF_BattleComponent — you do not need to call anything for those.
 * Tier 2 traits need their hook called at the right moment in the battle
 * Blueprint; see Docs/TraitIntegration.md for the call sites.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureTraitLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	//====================================================================================
	// LOOKUP / DISPLAY
	//====================================================================================

	/** Display name for the summary screen and battle messages ("Currentborne"). */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static FText GetTraitDisplayName(EGF_CreatureTrait Trait);

	/** One-line effect description for the summary screen. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static FText GetTraitDescription(EGF_CreatureTrait Trait);

	/** Authoring tier — informational only, useful for debug menus. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static EGF_TraitTier GetTraitTier(EGF_CreatureTrait Trait);

	/** Trait in the given slot (0 = Trait1, 1 = Trait2). None if the slot is empty. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static EGF_CreatureTrait GetSpeciesTraitBySlot(const UGF_CreatureSpeciesData* Species, int32 Slot);

	/**
	 * Pick a random trait slot for a freshly created Creature, exactly like the games:
	 * a 50/50 roll when the species has two, always slot 0 when it has one.
	 * @param OutSlot  Which slot was chosen — store it on the instance so evolution keeps it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits")
	static EGF_CreatureTrait RollSpeciesTrait(const UGF_CreatureSpeciesData* Species, int32& OutSlot);

	/**
	 * The trait this Creature actually has.
	 * Falls back to the species' slot (or Trait1) when the saved value is None, so
	 * Creature created before traits existed still resolve correctly instead of
	 * coming back blank.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static EGF_CreatureTrait GetInstanceTrait(const FGF_CreatureInstanceData& Creature);

	/**
	 * The trait a battling Creature actor currently has — this is the one to read in
	 * battle, because Trace overwrites it mid-fight.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static EGF_CreatureTrait GetActorTrait(const AGF_Creature* Creature);

	/** Convenience check against GetActorTrait. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits")
	static bool DoesCreatureHaveTrait(const AGF_Creature* Creature, EGF_CreatureTrait Trait);

	/**
	 * Call once when a Creature actor is spawned/sent out, BEFORE the switch-in hook.
	 * Seeds CurrentTrait / OriginalTrait from the instance data and applies the
	 * passive flags that live on the actor (Composed -> bIsImmuneToFlinch).
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits")
	static void InitializeTraitOnActor(AGF_Creature* Creature, const FGF_CreatureInstanceData& Data);

	//====================================================================================
	// TIER 1 - DAMAGE / STAT MODIFIERS
	// These are already applied inside UGF_BattleComponent's damage path.
	// They are exposed so Blueprint can preview or reuse them.
	//====================================================================================

	/**
	 * Attack-stat multiplier from the attacker's trait.
	 * Titanstrength / Innerforce double physical Attack; Guts adds 50% while statused.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetAttackStatMultiplier(EGF_CreatureTrait Trait, bool bIsPhysical, EGF_STATUSEffect Status);

	/**
	 * Guts ignores the burn Attack drop in classic — a burned Guts user hits harder,
	 * not softer.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static bool DoesTraitIgnoreBurnAttackDrop(EGF_CreatureTrait Trait);

	/**
	 * Rootsurge / Cinderrage / Tidesurge / Hivecall: x1.5 to the matching type at 1/3 HP or less.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetLowHPTypeBoost(EGF_CreatureTrait Trait, EGF_Element SkillElement, float CurrentHP, float MaxHP);

	/** Insulated: the defender halves incoming Fire and Ice damage. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetDefenderDamageMultiplier(EGF_CreatureTrait DefenderTrait, EGF_Element SkillElement);

	/**
	 * Hover: full immunity to Ground-type moves, on top of the type chart.
	 * Checked by the damage path and by CanStatusSkillAffect (so Grit Fling misses too).
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static bool DoesTraitGrantTypeImmunity(EGF_CreatureTrait DefenderTrait, EGF_Element SkillElement);

	/**
	 * Aegis: only super-effective moves land. Pass the type effectiveness the
	 * damage calc already worked out; anything at 1x or below is blocked.
	 * Damaging moves only — status moves get through, as do weather and poison damage.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static bool DoesWonderGuardBlock(EGF_CreatureTrait DefenderTrait, float TypeEffectiveness);

	/** Duststep: x0.8 to the attacker's accuracy while a sandstorm is up. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetEvasionAccuracyMultiplier(EGF_CreatureTrait DefenderTrait, EGF_WeatherType Weather);

	/**
	 * Stillair suppresses ALL weather effects while its holder is on the field —
	 * damage modifiers, Currentborne, Sunfed, Rainfed, Duststep, the lot.
	 *
	 * Call this once at the start of a turn with both active Creature and feed the
	 * result everywhere you'd otherwise pass the raw weather. Returns None when
	 * either side has Stillair, and the weather unchanged otherwise.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static EGF_WeatherType GetEffectiveWeather(EGF_WeatherType Weather, const AGF_Creature* CreatureA, const AGF_Creature* CreatureB);

	/** Manyeyes: x1.3 accuracy. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetAccuracyMultiplier(EGF_CreatureTrait Trait);

	/** Currentborne doubles Speed in rain, Sunfed doubles it in harsh sun. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetSpeedMultiplier(EGF_CreatureTrait Trait, EGF_WeatherType Weather);

	/**
	 * Recoil the attacker takes from a recoil move — 0 with Ironskull.
	 * Reads bHasRecoil / RecoilPercentage off the move.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Damage")
	static float GetRecoilDamage(EGF_CreatureTrait AttackerTrait, TSubclassOf<AGF_SkillDefinition> Skill, float DamageDealt, float AttackerMaxHP);

	//====================================================================================
	// TIER 1 - IMMUNITIES AND BLOCKS
	//====================================================================================

	/**
	 * Dewshield blocks burn, Emberhide blocks freeze.
	 * @return True when the status is blocked; OutMessage is the "It doesn't affect..." line.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Immunity")
	static bool DoesTraitBlockStatus(EGF_CreatureTrait Trait, EGF_STATUSEffect Status, const FString& CreatureName, FString& OutMessage);

	/** Unfazed blocks infatuation (Attract, Beguile). */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Immunity")
	static bool DoesTraitBlockInfatuation(EGF_CreatureTrait Trait);

	/** Composed blocks flinching. Also mirrored onto AGF_Creature::bIsImmuneToFlinch. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Immunity")
	static bool DoesTraitBlockFlinch(EGF_CreatureTrait Trait);

	/**
	 * Muffled makes the defender immune to sound-based moves.
	 * Reads bIsSoundSkill off the move.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Immunity")
	static bool DoesTraitBlockMove(EGF_CreatureTrait DefenderTrait, TSubclassOf<AGF_SkillDefinition> Skill, const FString& CreatureName, FString& OutMessage);

	/**
	 * Unyielding / Hazeform block every opponent-inflicted stat drop;
	 * Sharpsight blocks accuracy drops only. Self-inflicted drops (Meltdown, Onslaught)
	 * always go through.
	 * @return True when the drop is allowed. False means show OutMessage instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Immunity")
	static bool CanStatStageBeLowered(EGF_CreatureTrait Trait, EGF_StatStages StatType, bool bSelfInflicted, const FString& CreatureName, FString& OutMessage);

	/**
	 * Damp: Selfdestruct / Explosion fail outright when EITHER side has it — and because
	 * the move never resolves, the user does NOT down. Check this BEFORE spending the
	 * user's HP; Uses is still spent on the failure.
	 * @return True when the move is blocked. Show OutMessage instead of resolving the turn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Immunity")
	static bool DoesDampBlockSkill(EGF_CreatureTrait UserTrait, EGF_CreatureTrait TargetTrait, TSubclassOf<AGF_SkillDefinition> Skill, const FString& UserName, const FString& TargetName, FString& OutMessage);

	/** Bulwark: one-hit-KO moves (bKO) always fail. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Immunity")
	static bool DoesOHKOSkillFail(EGF_CreatureTrait DefenderTrait, TSubclassOf<AGF_SkillDefinition> Skill, const FString& CreatureName, FString& OutMessage);

	/**
	 * Dustward: the defender ignores a move's ADDED effects (status chance,
	 * flinch chance, opponent-targeted stat drops). The move's damage still lands,
	 * and a status MOVE (Static Pulse, Envenom) is unaffected — only secondaries.
	 * @return True when the secondary effect should be rolled as normal.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Immunity")
	static bool ShouldSecondaryEffectApply(EGF_CreatureTrait DefenderTrait);

	/** Tightgrip: the held item can't be stolen or knocked off (Thief, Covet, Trick). */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Immunity")
	static bool CanHeldItemBeTaken(EGF_CreatureTrait TargetTrait);

	/** Fleetfoot: escaping a wild battle always succeeds. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Immunity")
	static bool CanAlwaysFleeFromWild(EGF_CreatureTrait Trait);

	/**
	 * Lodestone: a Steel-type facing a Lodestone holder can neither switch nor flee.
	 * @param Switcher  The Creature trying to leave.
	 * @param Opponent  The Creature on the other side of the field.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Immunity")
	static bool IsSwitchBlockedByTrait(const AGF_Creature* Switcher, const AGF_Creature* Opponent, FString& OutMessage);

	/** Foulsap: a draining move damages the drainer instead of healing it. */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Immunity")
	static bool DoesDrainHurtInstead(EGF_CreatureTrait DefenderTrait);

	//====================================================================================
	// TIER 2 - EVENT HOOKS
	//====================================================================================

	/**
	 * Call after a CONTACT move (bMakesContact) successfully damages the defender.
	 * Runs the defender's contact trait against the attacker.
	 * Nothing fires if the attacker already downed — check that first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static FGF_TraitContactResult OnContactMade(AGF_Creature* Attacker, AGF_Creature* Defender, TSubclassOf<AGF_SkillDefinition> Skill);

	/**
	 * Call when a Creature is sent out, after InitializeTraitOnActor.
	 * Trace copies the opponent's trait onto the entering Creature.
	 * @return True when an trait announced itself; print OutMessage if so.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static bool OnSwitchIn(AGF_Creature* Entering, AGF_Creature* Opponent, FString& OutMessage);

	/**
	 * Call when a Creature is recalled (and at the end of a battle it survived).
	 * Selfmend clears its status condition; a Traced trait is reverted.
	 * Writes the cure through to Data as well so it survives back into the party.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static bool OnSwitchOut(AGF_Creature* Leaving, UPARAM(ref) FGF_CreatureInstanceData& Data, FString& OutMessage);

	/**
	 * Call once per Creature during the end-of-turn phase, after held items.
	 * Moult rolls a 1/3 status cure; Rainfed restores 1/16 max HP in rain.
	 * The heal is returned, not applied — apply it yourself so the HP bar animates.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static FGF_TraitEndOfTurnResult OnEndOfTurn(AGF_Creature* Creature, EGF_WeatherType Weather);

	/**
	 * Call right after a Creature is given a status condition BY another Creature.
	 * Backlash passes burn / poison / paralysis straight back to the inflictor.
	 * @param OutStatusToReflect  The status to apply to the inflictor (None if nothing).
	 * @return True when Backlash fired.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static bool OnStatusInflicted(AGF_Creature* Afflicted, AGF_Creature* Inflictor, EGF_STATUSEffect Status, EGF_STATUSEffect& OutStatusToReflect, FString& OutMessage);

	/**
	 * Sluggard. Call at the START of the Creature's action, before the move resolves.
	 * Flips the loafing flag every turn: acts, loafs, acts, loafs...
	 * @return True when the Creature may act this turn. False = print OutMessage and skip.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static bool TruantCanActThisTurn(AGF_Creature* Creature, FString& OutMessage);

	/** Reset Sluggard's loafing state — call on switch-in and at battle start. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Hooks")
	static void ResetTruantState(AGF_Creature* Creature);

	/**
	 * Lightsleeper: sleep burns two counters per turn instead of one.
	 * Use this instead of a hardcoded -1 when ticking SleepCounter.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Hooks")
	static int32 GetSleepCounterDecrement(EGF_CreatureTrait Trait);

	//====================================================================================
	// TIER 3 - OVERWORLD
	//====================================================================================

	/**
	 * Pickup. Call once per party Creature after a battle is won.
	 * 10% chance per eligible Creature; the item tier scales with its level.
	 * @param OutItem  The item name found, NAME_None on a failed roll.
	 * @return True when something was picked up.
	 *
	 * The names it returns are the classic pickup table entries — make sure matching
	 * item assets exist, or remap the names in your inventory call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Traits|Overworld")
	static bool RollPickupItem(EGF_CreatureTrait Trait, int32 Level, FName& OutItem);

	/**
	 * Beacon on the LEAD party Creature doubles the wild encounter rate.
	 * Multiply your encounter roll by this.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Traits|Overworld")
	static float GetEncounterRateMultiplier(EGF_CreatureTrait LeadTrait);
};
