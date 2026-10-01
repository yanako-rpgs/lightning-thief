

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GF_ItemData.h"
#include "GF_Creature.h"
#include "GF_CreatureInstanceData.h"
#include "GF_ElementTypes.h"
#include "GF_HeldItemBattleHelper.generated.h"

/**
 * Helper class for held item battle integration
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKBATTLE_API UGF_HeldItemBattleHelper : public UObject
{
    GENERATED_BODY()

public:
    //=============================================================================
    // HELPER FUNCTION: Get Held Item Data
    //=============================================================================

    /**
     * Get the ItemData for a Creature's held item
     * Returns nullptr if Creature has no held item or item doesn't exist
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static UGF_ItemData* GetCreatureHeldItemData(UObject* WorldContext, const FGF_CreatureInstanceData& CreatureData);

    //=============================================================================
    // 1. STAT CALCULATION
    //=============================================================================

    /**
     * Apply held item stat modifiers to a Creature
     * Call this when spawning Creature or recalculating stats
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static void ApplyHeldItemStatModifiers(UObject* WorldContext, AGF_Creature* Creature);

    //=============================================================================
    // 2. DAMAGE CALCULATION
    //=============================================================================

    /**
     * Calculate damage multiplier from held items
     * Returns the final multiplier to apply to damage
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static float GetHeldItemDamageMultiplier(
        UObject* WorldContext,
        AGF_Creature* Attacker,
        AGF_Creature* Defender,
        EGF_Element SkillElement,
        bool bIsSuperEffective
    );

    //=============================================================================
    // 3. AFTER DAMAGE
    //=============================================================================

    /**
     * Handle held item effects after damage is dealt
     * This includes recoil, HP restore berries, etc.
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static void HandleAfterDamageEffects(
        UObject* WorldContext,
        AGF_Creature* Attacker,
        AGF_Creature* Defender,
        float DamageDealt
    );

    //=============================================================================
    // 4. PREVENT FAINTING
    //=============================================================================

    /**
     * Check if held item prevents downing (Last Stand Sash, Grit Band)
     * Returns true if Creature survived due to item
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static bool CheckPreventDowning(
        UObject* WorldContext,
        AGF_Creature* Creature,
        float DamageTaken
    );

    //=============================================================================
    // 5. END OF TURN
    //=============================================================================

    /**
     * Handle end-of-turn held item effects
     * This includes Sustain Charm, Foul Sludge, status cure berries
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static void HandleEndOfTurnEffects(UObject* WorldContext, AGF_Creature* Creature);

    //=============================================================================
    // 6. EXP BOOST
    //=============================================================================

    /**
     * Apply EXP multiplier from held items
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static int32 ApplyEXPBoost(UObject* WorldContext, AGF_Creature* Creature, int32 BaseEXP);

    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
	static int32 ApplyEXPBoostFromData(UObject* WorldContext, const FGF_CreatureInstanceData& CreatureData, int32 BaseEXP);

    //=============================================================================
    // 7. MONEY BOOST
    //=============================================================================

    /**
     * Calculate money multiplier from party's held items
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static int32 ApplyMoneyBoost(UObject* WorldContext, TArray<AGF_Creature*> PartyCreature, int32 BaseMoney);

    //=============================================================================
    // 8. ACCURACY BOOST
    //=============================================================================

    /**
     * Get accuracy modifier from held items
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static float GetAccuracyMultiplier(UObject* WorldContext, AGF_Creature* Attacker, bool bMovedSecond);

    //=============================================================================
    // 9. CRITICAL HIT BOOST
    //=============================================================================

    /**
     * Get critical hit stage boost from held items
     */
    UFUNCTION(BlueprintCallable, Category = "Battle|Held Items")
    static int32 GetCriticalHitBoost(UObject* WorldContext, AGF_Creature* Attacker);

private:
    /**
     * Helper to consume a held item
     */
    static void ConsumeHeldItem(AGF_Creature* Creature);
};