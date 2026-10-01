// FlinchHelperLibrary.h
// Blueprint Function Library for Flinch mechanics

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_SkillDefinition.h"
#include "GF_Creature.h"
#include "GF_FlinchHelperLibrary.generated.h"

/**
 * Result struct for flinch application
 */
USTRUCT(BlueprintType)
struct FGF_FlinchResult
{
    GENERATED_BODY()

    /** Was flinch successfully applied? */
    UPROPERTY(BlueprintReadOnly, Category = "Flinch")
    bool bFlinchApplied = false;

    /** Reason flinch failed (empty if successful) */
    UPROPERTY(BlueprintReadOnly, Category = "Flinch")
    FString FailureReason;

    /** Message to display in battle (e.g., "Voltkit flinched!") */
    UPROPERTY(BlueprintReadOnly, Category = "Flinch")
    FString BattleMessage;

    /** Did the roll succeed but target was immune? */
    UPROPERTY(BlueprintReadOnly, Category = "Flinch")
    bool bWasImmuneToFlinch = false;

    /** Was the target immune to the move's TYPE (e.g. Astonish on a Normal type)? */
    UPROPERTY(BlueprintReadOnly, Category = "Flinch")
    bool bWasTypeImmune = false;

    /** Did the roll succeed but target already moved? */
    UPROPERTY(BlueprintReadOnly, Category = "Flinch")
    bool bTargetAlreadyMoved = false;
};

/**
 * Blueprint Function Library for handling Flinch mechanics in battle
 */
UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_FlinchHelperLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Process flinch from a move - the main function to call from battle flow
     * 
     * @param Skill - The move that was used (spawned actor)
     * @param TargetCreature - The Creature being attacked
     * @return FGF_FlinchResult with success status and battle message
     * 
     * Usage in Battle Flow:
     * 1. Calculate damage
     * 2. Apply damage
     * 3. Call ProcessSkillFlinch() AFTER damage is applied
     * 4. If result.bFlinchApplied is true, mark target as flinched
     * 5. Display result.BattleMessage if not empty
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle | Flinch")
    static FGF_FlinchResult ProcessSkillFlinch(AGF_SkillDefinition* Skill, AGF_Creature* TargetCreature);

    /**
     * Does the target take zero damage from this move's type?
     * A move the target is immune to never lands, so it can't flinch either -
     * Astonish (Ghost) on Scampling (Normal) does nothing at all.
     * Covers the type chart plus traits that grant their own immunity (Hover,
     * Aegis) and any extra immunities listed on the Creature's species data.
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle | Flinch")
    static bool IsTargetImmuneToSkillElement(AGF_SkillDefinition* Skill, AGF_Creature* TargetCreature);

    /**
     * Check if a Creature should skip their turn due to flinch
     * Call this BEFORE the Creature takes their action
     * 
     * @param Creature - The Creature about to act
     * @param OutFlinchMessage - Message to display if flinched
     * @return True if Creature is flinched and should skip their turn
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle | Flinch")
    static bool ShouldSkipTurnDueToFlinch(AGF_Creature* Creature, FString& OutFlinchMessage);

    /**
     * Reset turn flags for both Creature at the start of a new turn
     * Call this at the START of each battle turn, BEFORE any actions
     * 
     * @param PlayerCreature - The player's active Creature
     * @param EnemyCreature - The enemy's active Creature
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle | Flinch")
    static void ResetTurnFlagsForBothCreature(AGF_Creature* PlayerCreature, AGF_Creature* EnemyCreature);

    /**
     * Mark a Creature as having taken their action this turn
     * Call this AFTER a Creature completes their action
     * 
     * @param Creature - The Creature that just acted
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle | Flinch")
    static void MarkCreatureAsActed(AGF_Creature* Creature);

    /**
     * Get flinch chance from a move class without spawning
     * Useful for AI or UI display
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle | Flinch")
    static int32 GetFlinchChanceFromSkillClass(TSubclassOf<AGF_SkillDefinition> SkillClass);

    /**
     * Check if a move can cause flinch
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle | Flinch")
    static bool CanSkillClassCauseFlinch(TSubclassOf<AGF_SkillDefinition> SkillClass);
};
