#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_BattleManagementFunctions.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_SkillDefinition.h"
#include "GF_ElementTypes.h"
#include "GF_BattleComponent.generated.h"

class AGF_Creature;

// Weather types
/**
 * Why a move can't be selected this turn.
 *
 * classic forces LastResort when EVERY move is unusable — not just when Uses runs out —
 * so this is the single place new blockers get added. When Disable, Taunt, Torment,
 * Encore or Choice-locking land, they become entries here and every caller
 * (move menu, enemy AI, the LastResort check) picks them up for free.
 */
UENUM(BlueprintType)
enum class EGF_SkillUnusableReason : uint8
{
    Usable          UMETA(DisplayName = "Usable"),
    NoSkillInSlot    UMETA(DisplayName = "No Skill In Slot"),
    OutOfUses         UMETA(DisplayName = "Out of Uses"),
};

/**
 * What the battle should actually do with the move the player just clicked.
 *
 * Answer this ONCE, at the top of the click handler, BEFORE any Uses is spent —
 * that ordering is the whole point of the enum. Asking "does this Creature have
 * a usable move?" after UseSkill() has already decremented the last point turns
 * the move the player paid for into LastResort.
 */
UENUM(BlueprintType)
enum class EGF_SkillTurnAction : uint8
{
    /** Fire the clicked move. Spend its Uses, then run the normal turn. */
    UseSelectedSkill UMETA(DisplayName = "Use Selected Skill"),

    /** Every move is unusable — fire LastResort and spend NO Uses at all. */
    UseLastResort     UMETA(DisplayName = "Use LastResort"),

    /** This move is out of Uses but others are not. Play the error sound and
     *  stay in the move menu; do NOT consume the turn. */
    RejectSelection UMETA(DisplayName = "Reject Selection"),
};

/**
 * What the battle should do now that an action has finished resolving.
 *
 * Answer this by LOOKING AT THE BOARD, not by remembering who took damage.
 * One action can down zero, one, or BOTH actives — Selfdestruct and Explosion
 * always do, and recoil (Double-Edge), Bramblehide, and end-of-turn burn/poison/
 * sandstorm all can. Any flow that routes off "whose HP did I just change?"
 * handles one casualty and silently drops the other.
 *
 * The ordering below is the whole point of the enum: a side being wiped out
 * OUTRANKS anyone needing a replacement. If the last opposing Creature downs on
 * the same action that downed yours, you have WON — the game must never stop to
 * ask you for a replacement first. Asking is how the battle hangs forever.
 */
UENUM(BlueprintType)
enum class EGF_BattleBoardResult : uint8
{
    /** Nobody is down. Run the next queued action. */
    Continue            UMETA(DisplayName = "Continue"),

    /** The opposing side has no usable Creature left. Go straight to the win path. */
    PlayerWins          UMETA(DisplayName = "Player Wins"),

    /** The player has no usable Creature left. Go straight to Rout. */
    PlayerLoses         UMETA(DisplayName = "Player Loses"),

    /** Only the enemy active is down, and its side still has Creature. */
    EnemyMustReplace    UMETA(DisplayName = "Enemy Must Replace"),

    /** Only the player's active is down, and the party still has Creature. */
    PlayerMustReplace   UMETA(DisplayName = "Player Must Replace"),

    /** Both actives went down and BOTH sides can still field a Creature. */
    BothMustReplace     UMETA(DisplayName = "Both Must Replace"),
};

// Result struct containing all damage calculation outputs
USTRUCT(BlueprintType)
struct FGF_BattleDamageResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float Damage = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    bool bWasCritical = false;

    UPROPERTY(BlueprintReadOnly)
    bool bSuperEffective = false;

    UPROPERTY(BlueprintReadOnly)
    bool bNotEffective = false;

    UPROPERTY(BlueprintReadOnly)
    bool bImmune = false;

    UPROPERTY(BlueprintReadOnly)
    bool bMissed = false;

    UPROPERTY(BlueprintReadOnly)
    bool bWasProtected = false;

    UPROPERTY(BlueprintReadOnly)
    float TypeEffectiveness = 1.0f;

    UPROPERTY(BlueprintReadOnly)
    FString DamageMessage;
};

USTRUCT(BlueprintType)
struct FGF_DrainingSkillHealResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float HealAmount = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float AttackerOldHP = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float AttackerNewHP = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    bool bDidHeal = false;

    /**
     * Foulsap: the drain backfired and the attacker LOST HealAmount instead of
     * gaining it. bDidHeal is false in that case — check this before printing
     * "had its energy drained".
     */
    UPROPERTY(BlueprintReadOnly)
    bool bWasHurtInstead = false;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKBATTLE_API UGF_BattleComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_BattleComponent();

    //====================================================================================
    // WORKFLOW 1: Calculate damage immediately (spawns and destroys move internally)
    //====================================================================================

    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    FGF_BattleDamageResult CalculateDamage(
        const FGF_CreatureInstanceData& Attacker,
        const FGF_CreatureInstanceData& Defender,
        TSubclassOf<AGF_SkillDefinition> SkillUsed,
        FName AttackerHeldItem = NAME_None,
        FName DefenderHeldItem = NAME_None,
        EGF_WeatherType WeatherType = EGF_WeatherType::None,
        bool bHasReflect = false,
        bool bHasLightScreen = false,
        float AttackerAttackMult = 1.0f,
    float AttackerDefenseMult = 1.0f,
    float AttackerMagicMult = 1.0f,
    float AttackerPoiseMult = 1.0f,
    float AttackerSpeedMult = 1.0f,
    float DefenderAttackMult = 1.0f,
    float DefenderDefenseMult = 1.0f,
    float DefenderMagicMult = 1.0f,
    float DefenderPoiseMult = 1.0f,
    float DefenderSpeedMult = 1.0f,
    int32 AttackerCritStage = 0,
    EGF_CreatureTrait AttackerTraitOverride = EGF_CreatureTrait::None,
    EGF_CreatureTrait DefenderTraitOverride = EGF_CreatureTrait::None
    );

    /**
     * Applies the HP drain from Siphon / Mega Drain / Giga Drain / Leech Life.
     * Pass the DEFENDER's trait so Foulsap can turn the drain into damage —
     * when it does, the result comes back with bWasHurtInstead set and CurrentHP
     * already reduced.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
	FGF_DrainingSkillHealResult ApplyDrainingSkillHealing(
        UPARAM(ref) FGF_CreatureInstanceData& Attacker,
        const FGF_BattleDamageResult& DamageResult,
        AGF_SkillDefinition* Skill,
        EGF_CreatureTrait DefenderTrait = EGF_CreatureTrait::None
    );

    /**
     * Apply self-healing from moves like Recover, Roost, Soft-Boiled.
     * Heals based on the user's MaxHP using the move's SelfHealPercentage.
     * Returns heal result with OldHP, NewHP and HealAmount for UI/dialogue.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    FGF_DrainingSkillHealResult ApplySelfHealing(
        UPARAM(ref) FGF_CreatureInstanceData& User,
        AGF_SkillDefinition* Skill
    );

    //====================================================================================
    // WORKFLOW 2: Calculate damage WITHOUT spawning (for dialogue/camera flow!)
    //====================================================================================

    /**
     * Calculate damage WITHOUT spawning the move
     * Perfect for your workflow: Calculate → Dialogue → Camera → THEN Spawn Skill
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    FGF_BattleDamageResult CalculateDamageWithoutSpawning(
        const FGF_CreatureInstanceData& Attacker,
        const FGF_CreatureInstanceData& Defender,
        TSubclassOf<AGF_SkillDefinition> SkillUsed,
        FName AttackerHeldItem = NAME_None,
        FName DefenderHeldItem = NAME_None,
        EGF_WeatherType WeatherType = EGF_WeatherType::None,
        bool bHasReflect = false,
        bool bHasLightScreen = false,
        float AttackerAttackMult = 1.0f,
        float AttackerDefenseMult = 1.0f,
        float AttackerMagicMult = 1.0f,
        float AttackerPoiseMult = 1.0f,
        float AttackerSpeedMult = 1.0f,
        float DefenderAttackMult = 1.0f,
        float DefenderDefenseMult = 1.0f,
        float DefenderMagicMult = 1.0f,
        float DefenderPoiseMult = 1.0f,
        float DefenderSpeedMult = 1.0f,
        int32 AttackerCritStage = 0,
        int32 BasePowerOverride = -1,  // -1 = use the move's own Power; >=0 overrides it (Rising Slash, etc.)
        // Leave both as None and the traits are read straight off the instance data.
        // Only set them when the battling Creature's trait differs from what it owns —
        // i.e. after Trace. Pass AGF_Creature::CurrentTrait in that case.
        EGF_CreatureTrait AttackerTraitOverride = EGF_CreatureTrait::None,
        EGF_CreatureTrait DefenderTraitOverride = EGF_CreatureTrait::None
    );

    /**
     * Spawn a move actor at a specific location
     * IMPORTANT: You must call CleanupSkill() when done!
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    AGF_SkillDefinition* SpawnSkillForBattle(
        TSubclassOf<AGF_SkillDefinition> SkillClass,
        FVector SpawnLocation = FVector::ZeroVector,
        FRotator SpawnRotation = FRotator::ZeroRotator
    );

    /**
     * Get the duration of a move WITHOUT spawning it
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static float GetSkillDurationFromClass(TSubclassOf<AGF_SkillDefinition> SkillClass);

    /**
     * Get the time until camera should move to enemy WITHOUT spawning
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static float GetTimeTillCameraMovesToEnemyFromClass(TSubclassOf<AGF_SkillDefinition> SkillClass);

    /**
     * Get the duration from an already-spawned move
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static float GetSkillDuration(AGF_SkillDefinition* Skill);

    /**
     * Get camera timing from an already-spawned move
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static float GetTimeTillCameraMovesToEnemy(AGF_SkillDefinition* Skill);

    /**
     * Clean up a spawned move actor
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    void CleanupSkill(AGF_SkillDefinition* Skill);

    //====================================================================================
    // HELPER FUNCTIONS (Called internally, but exposed for flexibility)
    //====================================================================================

    /** Check if move will hit based on accuracy and evasion */
	UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    bool CheckSkillHit(
        TSubclassOf<AGF_SkillDefinition> SkillClass,
        const FGF_CreatureInstanceData& Attacker,
        const FGF_CreatureInstanceData& Defender,
        float AttackerAccuracyMult = 1.0f,
        float DefenderEvasionMult = 1.0f,
        // Only used by Duststep, which needs a sandstorm to do anything.
        EGF_WeatherType Weather = EGF_WeatherType::None
    );

    /**
     * Check if a move hits a Creature actor — also checks semi-invulnerable state (Fly, Dig, etc.)
     * DefenderActor supplies the SemiInvulnerableState; DefenderData supplies evasion stats.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    bool CheckSkillHitActor(
        TSubclassOf<AGF_SkillDefinition> SkillClass,
        const FGF_CreatureInstanceData& Attacker,
        AGF_Creature* DefenderActor,
        const FGF_CreatureInstanceData& DefenderData,
        float AttackerAccuracyMult = 1.0f,
        float DefenderEvasionMult = 1.0f
    );

    /** Roll for critical hit. Pass AttackerCreature->CritStageBoost as ExtraCritStage if the attacker has Focus Energy active. */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    bool RollCriticalHit(
        const FGF_CreatureInstanceData& Attacker,
        AGF_SkillDefinition* SkillUsed,
        int32 ExtraCritStage = 0
    );

    /** Get weather damage modifier */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    float GetWeatherModifier(EGF_Element SkillElement, EGF_WeatherType Weather) const;

    /** Get held item damage modifier */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    float GetHeldItemModifier(
        FName ItemName,
        EGF_Element SkillElement,
        float TypeEffectiveness,
        bool bIsAttacker
    ) const;

    /** Get screen damage modifier (Reflect/Veil) */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    float GetScreenModifier(
        bool bIsPhysical,
        bool bHasReflect,
        bool bHasLightScreen
    ) const;

    /**
     * Determines if the Player's Creature moves before the Enemy's Creature this turn.
     * Blueprint should identify which Creature is the player's (via isPlayerCreature)
     * and pass them in the correct slots — this function doesn't care about turn queue order.
     * Reads SkillInQueue from each Creature directly.
     * Checks priority bracket first, then speed, then coin flip.
     * Paralysis halves speed automatically.
     * Returns true if the Player goes first, false if the Enemy goes first.
     */
    /**
     * Returns true if the Attacking Creature moves before the Target Creature.
     * Checks move priority first, then speed, then coin flip on tie.
     * Paralysis halves speed automatically.
     * Pass the current Weather so Currentborne and Sunfed can double Speed.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static bool DoesAttackingCreatureGoFirst(
        AGF_Creature* AttackingCreature,
        TSubclassOf<AGF_SkillDefinition> AttackingSkill,
        AGF_Creature* TargetCreature,
        TSubclassOf<AGF_SkillDefinition> TargetSkill,
        EGF_WeatherType Weather = EGF_WeatherType::None
    );

    // ============================================
    // MOVE USABILITY / STRUGGLE
    // ============================================

    /**
     * Can this Creature use the move in the given slot right now?
     * Use it to grey out move-menu entries and to pick enemy AI moves.
     *
     * @param SlotIndex  Index into the Creature's Skills array (0-3).
     * @param OutReason  Why not, for the menu tooltip / debug. Usable when true.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle | Skills")
    static bool IsSkillUsable(const AGF_Creature* Creature, int32 SlotIndex, EGF_SkillUnusableReason& OutReason);

    /**
     * Does this Creature have at least one move it can use?
     *
     * When this is FALSE the Creature must use LastResort — skip the move menu
     * entirely rather than letting a 0-Uses move be selected and correcting after.
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle | Skills")
    static bool HasAnyUsableSkill(const AGF_Creature* Creature);

    /**
     * Index of the first usable move, or -1 when there are none (= LastResort).
     * Handy as the enemy AI's fallback when its preferred move is out of Uses.
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle | Skills")
    static int32 GetFirstUsableSkillIndex(const AGF_Creature* Creature);

    /**
     * The single "what happens when the player clicks a move" decision.
     *
     * Call this FIRST in the click handler and switch on the result. Nothing
     * downstream should re-ask HasAnyUsableSkill: once Uses has been spent the
     * answer changes, and a move used at its last Uses would flip to LastResort.
     *
     * @param Creature            The Creature taking the turn (the ACTIVE one — not the
     *                           party lead, not the enemy).
     * @param SelectedSlotIndex  Slot the player clicked, i.e. AttackSelectedID.
     * @param OutSkillSlotIndex   Slot whose Uses to spend. -1 for LastResort and for a
     *                           rejected click — spend nothing in those cases.
     *
     * Logs one summary line every call so a battle log always shows which branch ran.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle | Skills")
    static EGF_SkillTurnAction DecideTurnAction(const AGF_Creature* Creature, int32 SelectedSlotIndex, int32& OutSkillSlotIndex);

    /**
     * Attempt to apply Protect to a Creature for this turn.
     * Handles consecutive-use failure: each successive use halves the success chance.
     * On success: sets bIsProtected=true and increments ConsecutiveProtectCount.
     * On any non-Protect move used: call ResetProtectStreak(Creature) to clear the count.
     * @return True if Protect succeeded, false if it failed (show "But it failed!")
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static bool ApplyProtect(AGF_Creature* User);

    /**
     * Decide what the battle does now that an action has finished resolving.
     *
     * Call this ONCE per action, straight after the downed actives have been
     * pruned from the turn queue and BEFORE anything routes to a swap screen.
     * It reads IsDowned off both actives, so it is safe to call again on the
     * same board — it has no internal state and changes nothing.
     *
     * @param PlayerActive           The player's Creature on the field. Null is treated as "not down".
     * @param EnemyActive            The opposing Creature on the field. Same null handling.
     * @param PlayerUsableRemaining  Creature on the PLAYER's side that are not downed,
     *                               counting the active only if it survived this action.
     * @param EnemyUsableRemaining   Same count for the opposing side. In a wild battle
     *                               this is simply 1 if the wild Creature is up, else 0.
     *                               TamerMaster::HasUsableCreature covers the tamer case.
     * @param OutDowned             Every active that is down (0, 1 or 2 entries) so the
     *                               caller can play each down animation and award exp.
     *                               Award exp BEFORE acting on the result.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static EGF_BattleBoardResult ResolveBoardAfterAction(
        AGF_Creature* PlayerActive,
        AGF_Creature* EnemyActive,
        int32 PlayerUsableRemaining,
        int32 EnemyUsableRemaining,
        TArray<AGF_Creature*>& OutDowned);

    /**
     * Guard for a queued post-down swap prompt: true when the prompt must be abandoned
     * because the battle is already won.
     *
     * Down schedules its follow-up by timer name — TrySwitchCreature at 1.0s when the
     * player's Creature goes down, WinBattle at 2.0s when the opponent's does. One action
     * that downs BOTH (Selfdestruct, Explosion, recoil, Bramblehide, end-of-turn chip)
     * schedules both, and the 1.0s swap prompt beats the 2.0s win by a full second — so
     * the battle parks in the swap UI and the win lands on a dead flow.
     *
     * Call this at the TOP of TrySwitchCreature / DoYouWantToSendOutNext and return early
     * when it is true; the WinBattle timer then lands on a battle that is still live.
     *
     * @param EnemyActive                 The opposing Creature on the field.
     * @param bEnemySideHasUsableCreature  Does the opposing SIDE still have a Creature that
     *                                    can fight — counting its active only if that
     *                                    active survived? TamerMaster::HasUsableCreature
     *                                    answers this for tamers; pass false in a wild
     *                                    battle once the wild Creature is down.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static bool ShouldAbortSwapPrompt(AGF_Creature* EnemyActive, bool bEnemySideHasUsableCreature);

    /**
     * Reset the consecutive Protect counter when any non-Protect move is used.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void ResetProtectStreak(AGF_Creature* User);

    /**
     * Returns the power Rising Slash should use THIS turn, based on the user's
     * current FuryCutterCount: 40, 80, 160, capped at 160. Does NOT change the count.
     * Plug this into the Base Power Override pin of CalculateDamageWithoutSpawning.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static int32 GetFuryCutterPower(AGF_Creature* User);

    /**
     * Returns the power Revenge should use THIS turn: 120 if the user took damage
     * this turn (bTookDamageThisTurn), else 60. Plug into the Base Power Override
     * pin of CalculateDamageWithoutSpawning. Give the Revenge move Priority -4.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static int32 GetRevengePower(AGF_Creature* User);

    /**
     * Call on a SUCCESSFUL hit to ramp the Rising Slash counter for next time
     * (stops incrementing once the power cap is reached).
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void IncrementFuryCutter(AGF_Creature* User);

    /**
     * Reset the Rising Slash counter. Call on a miss, on switch-out,
     * or when any other move is used.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void ResetFuryCutterStreak(AGF_Creature* User);

    /**
     * Apply a partial trap (Ember Vortex, Ensnare, Bind, Clamp, Whirlpool, Sand Tomb)
     * to the target. Rolls a random duration in [MinTurns, MaxTurns] and stores the
     * move's display name for the messages. No-op if the target is already trapped
     * (partial traps don't stack or refresh). Returns true if newly applied.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static bool ApplyPartialTrap(AGF_Creature* Target, FName SkillName, int32 MinTurns = 2, int32 MaxTurns = 5);

    /** Is this Creature currently held by a partial trap? (Use this to also block switching.) */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static bool IsPartiallyTrapped(AGF_Creature* Target);

    /** Turns remaining on the partial trap (0 if not trapped). */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static int32 GetPartialTrapTurnsRemaining(AGF_Creature* Target);

    /** End-of-turn chip damage for the partial trap = MaxHP / 8 (minimum 1). 0 if not trapped. */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static int32 GetPartialTrapDamage(AGF_Creature* Target);

    /**
     * Advance the partial trap by one turn (call at end of turn AFTER applying the chip damage).
     * Decrements the counter and clears the trap when it reaches 0.
     * @param Target      The trapped Creature.
     * @param bJustEnded  Set true if the trap ran out THIS turn (show the "freed from" message).
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void TickPartialTrap(AGF_Creature* Target, bool& bJustEnded);

    /** Clear a partial trap immediately (e.g. forced switch via Roar/Displace). */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void ClearPartialTrap(AGF_Creature* Target);

    /**
     * Roll how many times a multi-hit move strikes this use.
     * Fixed count (MinHits == MaxHits) -> that number (Double Kick = 2, Twineedle = 2).
     * 2-5 range -> standard weighted distribution (2 & 3 hits most common, like Bullet Seed).
     * Any other range -> uniform random. Returns 1 if the move isn't a looping/multi-hit move.
     * Each individual hit is still resolved by its own CalculateDamageWithoutSpawning call,
     * so every hit rolls its own accuracy, crit, and damage independently.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static int32 GetMultiHitCount(TSubclassOf<AGF_SkillDefinition> SkillClass);

    // ============================================
    // BIDE
    // ============================================

    /**
     * Begin Brace. Locks the user in and starts the storing counter.
     * Call this when Brace is first selected (not yet biding).
     * @param StoreTurns How many turns to store before releasing (2 = store this turn + next, release on the 3rd).
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void StartBide(AGF_Creature* User, int32 StoreTurns = 2);

    /** Is this Creature currently biding? (Use for the action-lock branch AND to block switching.) */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static bool IsBiding(AGF_Creature* User);

    /**
     * Add damage to the Brace store. Call from your damage macro whenever a biding
     * Creature takes damage (gate it on IsBiding first). Ignored if not biding.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void AddBideDamage(AGF_Creature* User, float Amount);

    /**
     * Advance Brace one turn (call on each of the user's turns while biding).
     * @param bIsReleaseTurn  True if the counter ran out and Brace should unleash THIS turn.
     *                        On a storing turn this is false ("storing energy!").
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void TickBide(AGF_Creature* User, bool& bIsReleaseTurn);

    /**
     * The damage Brace unleashes = stored damage x 2 (typeless, applied directly, ignores the
     * damage formula). Returns 0 if nothing was stored (Brace fails). Does NOT clear the state —
     * call ClearBide after you've applied the damage.
     */
    UFUNCTION(BlueprintPure, Category = "GammaEngine | Battle")
    static int32 GetBideReleaseDamage(AGF_Creature* User);

    /** Clear Brace state (call after releasing, or on a forced switch). Down clears it automatically. */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    static void ClearBide(AGF_Creature* User);

    /**
     * Returns true if a status move can legally affect the defender.
     * Checks, in order:
     *   1. Type chart immunity — e.g. Static Pulse (Electric) can't affect Ground types.
     *   2. Trait immunity — Muffled blocks the move, Hover blocks Ground,
     *      Dewshield / Emberhide / Wakeful block the specific status.
     *   3. Hardcoded status immunities — e.g. Fire types can't be Burned.
     *
     * Call this before applying any status move effect.
     *
     * @param OutFailMessage  Why it failed, ready to print. Empty when the move is
     *                        allowed. Print this rather than a generic "But it failed!"
     *                        or the player has no idea an trait stopped them.
     */
    UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
    bool CanStatusSkillAffect(
        TSubclassOf<AGF_SkillDefinition> SkillClass,
        const FGF_CreatureInstanceData& Defender,
        FString& OutFailMessage
    ) const;

    // ==================================================================================
    // DEBUG
    // ==================================================================================

    /** When true, every hit is a critical hit. For testing crit VFX only — disable before shipping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaEngine | Battle | Debug")
    bool bForceCriticalHit = false;

protected:
    virtual void BeginPlay() override;

private:
    /** Spawn move actor to get its data */
    AGF_SkillDefinition* SpawnSkillActor(TSubclassOf<AGF_SkillDefinition> SkillClass);

    /** Spawn move actor at specific location */
    AGF_SkillDefinition* SpawnSkillActorAt(TSubclassOf<AGF_SkillDefinition> SkillClass, FVector Location, FRotator Rotation);

    /** Clean up spawned move actor */
    void CleanupSkillActor(AGF_SkillDefinition* Skill);
};