// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GF_ElementTypes.h"
#include "GF_SkillDefinition.generated.h"

class AGF_Creature;

// This is where the Creature Typing will live.
//Creature STATUS (Persistent - these last until cured)
// EGF_STATUSEffect now lives in GF_StatusTypes.h (World), reached through
// GF_ElementTypes.h. Items cure status and items live in the World module,
// so the enum cannot live in Creatures.

// Volatile Status Effects (reset at end of turn or when switching out)
UENUM(BlueprintType)
enum class EGF_VolatileStatus : uint8
{
	None		UMETA(DisplayName = "None"),
	Flinch		UMETA(DisplayName = "Flinch"),
	Confused	UMETA(DisplayName = "Confused"),    // Confusion is both volatile AND tracked separately
	Infatuation	UMETA(DisplayName = "Infatuation"), // For future use (Attract)
	Trapped		UMETA(DisplayName = "Trapped"),     // For future use (Mean Look, etc.)
	LeechSeed	UMETA(DisplayName = "Leech Seed"),  // For future use
	Cursed		UMETA(DisplayName = "Cursed"),      // For future use (Ghost Curse)
	Nightmare	UMETA(DisplayName = "Nightmare"),   // For future use
	Torment		UMETA(DisplayName = "Torment"),     // For future use
	Taunt		UMETA(DisplayName = "Taunt"),       // For future use
	Encore		UMETA(DisplayName = "Encore"),      // For future use
};

UENUM(BlueprintType)
enum class EGF_StatStages : uint8
{
	None			UMETA(DisplayName = "None"),
	AttackUp		UMETA(DisplayName = "Attack Up!"),
	AttackDown		UMETA(DisplayName = "Attack Down!"),
	DefenseUp		UMETA(DisplayName = "Defense Up!"),
	DefenseDown		UMETA(DisplayName = "Defense Down!"),
	MagicUp			UMETA(DisplayName = "Sp. Attack Up!"),
	MagicDown		UMETA(DisplayName = "Sp. Attack Down!"),
	PoiseUp			UMETA(DisplayName = "Sp. Defense Up!"),
	PoiseDown		UMETA(DisplayName = "Sp. Defense Down!"),
	SpeedUp			UMETA(DisplayName = "Speed Up!"),
	SpeedDown		UMETA(DisplayName = "Speed Down!"),
	AccuracyUp		UMETA(DisplayName = "Accuracy Up!"),
	AccuracyDown	UMETA(DisplayName = "Accuracy Down!"),
	EvasionUp		UMETA(DisplayName = "Evasion Up!"),
	EvasionDown		UMETA(DisplayName = "Evasion Down!")
};

// The semi-invulnerable state a two-turn move puts the user into
UENUM(BlueprintType)
enum class EGF_SemiInvulnerableState : uint8
{
    None            UMETA(DisplayName = "None"),
    InAir           UMETA(DisplayName = "In Air (Fly / Bounce)"),
    Underground     UMETA(DisplayName = "Underground (Dig)"),
    Underwater      UMETA(DisplayName = "Underwater (Dive)"),
    PhaseShifted     UMETA(DisplayName = "Phase Shifted"),
};

/** A battle stat a skill can push up or down, without a direction baked in. */
UENUM(BlueprintType)
enum class EGF_BattleStat : uint8
{
	Attack		UMETA(DisplayName = "Attack"),
	Defense		UMETA(DisplayName = "Defense"),
	Magic		UMETA(DisplayName = "Magic"),
	Poise		UMETA(DisplayName = "Poise"),
	Speed		UMETA(DisplayName = "Speed"),
	Accuracy	UMETA(DisplayName = "Accuracy"),
	Evasion		UMETA(DisplayName = "Evasion")
};

/**
 * One stat change a skill makes. A skill can carry any number of these, on the
 * user and on the target at once -- Pep Talk raises three stats and lowers two,
 * Dark Toll lowers the target's attacks while raising the user's.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_SkillStatChange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Change")
	EGF_BattleStat Stat = EGF_BattleStat::Attack;

	/** Stages to move: positive raises, negative lowers. Stages cap at -6 and +6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Change", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 Stages = 1;

	/** True changes the user's stat, false the target's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Change")
	bool bAffectsSelf = true;

	/** The direction-carrying stage this change applies as, e.g. Attack +1 -> AttackUp. */
	EGF_StatStages ToStatStage() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGF_OnStatStageApplied, EGF_StatStages, StatType, int32, StageChange, bool, bAffectedSelf);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnFlinchApplied);

UCLASS()
class GAMMAFRAMEWORKCREATURES_API AGF_SkillDefinition : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AGF_SkillDefinition();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Name of the move.", Category = "Skill Information"))
	FName Name = "Default Skill";

	/**
	 * Player-facing description, shown in the skill info box.
	 *
	 * FText rather than FName because this is prose, not an identifier. FName is
	 * interned and compared case-insensitively, and it cannot be localised --
	 * fine for Name above, which really is an identifier, wrong for a sentence.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Information of the move.", MultiLine = "true", Category = "Skill Information"))
	FText Description = NSLOCTEXT("GammaFramework", "SkillDescriptionDefault", "Put the move description here that will be displayed on the screen or info box.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Primary Typing of the move.", Category = "Skill Information"))
	EGF_Element Type = EGF_Element::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "If the attack is a Special or Physical Skill.", Category = "Skill Information"))
	EGF_SkillCategory Split = EGF_SkillCategory::Physical;

	/**
	 * How many seats this skill reaches on a four-a-side board.
	 *
	 * Defaults to Single, so every skill authored before the board widened keeps
	 * behaving exactly as it did.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "How many seats this Skill reaches. Single is the default and matches pre-4v4 behaviour.", Category = "Skill Information"))
	EGF_SkillTargetShape TargetShape = EGF_SkillTargetShape::Single;

	/**
	 * Damage multiplier per target when this skill hits more than one seat.
	 *
	 * 0.65 rather than the usual 0.75 because the board is twice as wide: at
	 * four-a-side a spread skill lands on four targets, and at 0.75 that is
	 * three full hits for one action, which makes spreading strictly better than
	 * concentrating. The whole tactical question of the format is whether to
	 * focus one target down or spread, so this number is what keeps that a real
	 * choice. Only applied when the expanded target list has more than one entry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Damage multiplier applied per target when this Skill hits more than one seat.", Category = "Skill Information", ClampMin = "0.1", ClampMax = "1.0"))
	float MultiTargetDamageMultiplier = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "What kind of STATUS effect to give the creature.", Category = "Skill Information"))
	EGF_STATUSEffect Status = EGF_STATUSEffect::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Chance to apply the STATUS on this move.", Category = "Skill Information"))
	int32 StatusChance = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "This sets the move to be a K.O. move.", Category = "Skill Information"))
	bool bKO = false;

	// ============================================
	// TRAIT-FACING FLAGS
	// These exist so traits can key off the move. Nothing else reads them.
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does this move physically touch the target? Drives the on-contact traits (Static, Venomspur, Sporeburst, Beguile, Scorchhide, Bramblehide). Ram/Bite/Vine Whip = true; Ember/Water Gun/Static Pulse = false.", Category = "Traits"))
	bool bMakesContact = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this a sound-based move? Muffled gives full immunity to it. Bluster, Shriek, Discord, Uproar, Hyper Voice, Sing, Roar, Snore, Perish Song.", Category = "Traits"))
	bool bIsSoundSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does the user take recoil damage from this move? Ironskull cancels it. Take Down, Double-Edge, Submission, LastResort.", Category = "Traits"))
	bool bHasRecoil = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Percentage of the USER'S MAX HP lost as recoil when the skill deals damage. Cannonball = 25, Teardown = 20.", Category = "Traits", EditCondition = "bHasRecoil", ClampMin = "0", ClampMax = "100"))
	float RecoilPercentage = 25.0f;



	// ============================================
	// FLINCH PROPERTIES
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Can this move cause the target to flinch?", Category = "Flinch"))
	bool bCanCauseFlinch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Chance to cause flinch (0-100%). Air Slash = 30%, Iron Head = 30%, Bite = 30%, etc.", Category = "Flinch", EditCondition = "bCanCauseFlinch", ClampMin = "0", ClampMax = "100"))
	int32 FlinchChance = 0;

	// ============================================
	// STAT STAGE PROPERTIES
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta= (ToolTip = "Does the Skill affect STAT STAGES?", Category = "Skill Information"))
	bool bAffectsStatStage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "What kind of STAT STAGE Will be affected?", Category = "Skill Information"))
	EGF_StatStages StatStages = EGF_StatStages::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Chance to apply the stat stage (0-100%)", Category = "Skill Information", EditCondition = "bAffectsStatStage", ClampMin = "0", ClampMax = "100"))
	int32 StatStageChance = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does this stat change affect the user (true) or the target (false)?", Category = "Skill Information", EditCondition = "bAffectsStatStage"))
	bool bStatChangeAffectsSelf = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Randomly Picks a STAT to change", Category = "Skill Information", EditCondition = "bStatChangeAffectsSelf"))
	bool bStatIsRandom = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Number of stages to change (positive increases, negative decreases). Range: -6 to +6", Category = "Skill Information", EditCondition = "bAffectsStatStage", ClampMin = "-6", ClampMax = "6"))
	int32 StatStageAmount = -1;

	/**
	 * Every stat change this skill makes, on the user and the target, all landing
	 * on one Stat Stage Chance roll. Works alongside the single-stat fields above;
	 * Dokimon moves use this list and leave those off.
	 *
	 * On a damaging skill the changes need the hit to connect. On a Status skill
	 * the user's own changes always land.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Stat changes on the user and/or target. All share the Stat Stage Chance roll.", Category = "Skill Information"))
	TArray<FGF_SkillStatChange> StatChanges;

	// ============================================
	// DOKIMON MOVE RULES
	// ============================================

	/**
	 * Flat HP the user recovers when it uses this skill. Not a percentage.
	 * On a damaging skill (Dark Bond, Soul Drain) the heal needs the hit to
	 * connect; on a Status skill (Healing Herbs) it always happens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Flat HP the user heals. Damaging skills only heal if they connect.", Category = "Dokimon", ClampMin = "0"))
	int32 HealAmount = 0;

	/** Only usable on the user's first turn after entering battle (Surprise Attack, Early Bird). Fails otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Fails unless it is the user's first turn on the field.", Category = "Dokimon"))
	bool bFirstTurnOnly = false;

	/** If this skill connects, the user loses its next turn recharging (Charge Cannon, Dark Bond). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The user must recharge next turn if this skill connects.", Category = "Dokimon"))
	bool bRequiresRecharge = false;

	/** Resets the user's stat stages to 0 (Purify, Glacial Embrace). Pair with Is Refresh to also cure status. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Clears all of the user's stat stage changes.", Category = "Dokimon"))
	bool bCleansesStatChanges = false;

	// ============================================
	// CORE MOVE PROPERTIES
	// ============================================

	// Skill priority bracket (-7 to +7). Higher priority always goes first regardless of Speed.
	// If two moves share the same priority, Speed determines order as normal.
	// Common values: First Strike = +1, Extreme Speed = +2, Protect = +4, Trick Room = -7
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Skill priority (-7 to +7). Higher = moves first. 0 = normal.", Category = "Skill Information", ClampMin = "-7", ClampMax = "7"))
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Power of the Skill.", Category = "Skill Information"))
	float Power = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Accuracy of the Skill.", Category = "Skill Information"))
	float Accuracy = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The Current Uses of the Skill, same as Max Uses.", Category = "Skill Information"))
	int32 CurrentUses = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The Max Uses of the Skill.", Category = "Skill Information"))
	int32 MaxUses = 10;

	// If true, ANY species that can use Tomes may learn this move via Tome, without needing it in
	// their per-species LearnableTomeMoves list. Use for near-universal Tomes/HMs (Return, Envenom,
	// Protect, Rest, Substitute, Hidden Power, Facade, etc.) to avoid listing them on every species.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Learnable by any species via Tome without a per-species entry (near-universal Tomes/HMs).", Category = "Skill Information"))
	bool bUniversalTM = false;

	// Hides this move from the "Change Skills" screen (both Available Skills and Available Tomes)
	// and refuses any attempt to teach it. Use for unfinished/unshippable moves without having
	// to strip them from every species' learnset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Hide from the teach-move screen and block teaching it (unfinished moves).", Category = "Skill Information"))
	bool bExcludeFromSkillPool = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The duration of time it takes before the camera moves to the Opponent (1 second to charge then move camera over).", Category = "Skill Information"))
	float TimeTillCameraMovesToEnemy = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Determins if this move will deliver an increase Critical chance.", Category = "Skill Information"))
	bool isHighCritRatio = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Once enabled, this move will check if it will repeat. Example: Bullet Seed", Category = "Skill Information"))
	bool isLoopingSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Minimum number of times this move hits. Double Kick / Twineedle = 2. Bullet Seed = 2.", Category = "Skill Information", EditCondition = "isLoopingSkill", ClampMin = "1"))
	int32 MinHits = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Maximum number of times this move hits. Double Kick / Twineedle = 2 (set equal to Min for a fixed count). Bullet Seed = 5.", Category = "Skill Information", EditCondition = "isLoopingSkill", ClampMin = "1"))
	int32 MaxHits = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Fury-Cutter-style ramping power: 40/80/160 on consecutive hits, reset on a miss. Power field is ignored when this is on; use GetFuryCutterPower for the override.", Category = "Skill Information"))
	bool bIsFuryCutterStyle = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Low Kick / Grass Knot: power is derived from the TARGET's weight. The Power field is ignored when this is on.", Category = "Skill Information"))
	bool bIsWeightBasedPower = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Seismic Toss (Fighting) / Night Shade (Ghost): deals fixed damage equal to the USER's level, ignoring stats/STAB/crit/weather. Type immunity still applies (Ghost is immune to Fighting; Normal is immune to Ghost). Power is ignored.", Category = "Skill Information"))
	bool bIsFixedLevelDamage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Revenge / Avalanche: power doubles (60 -> 120) if the user took damage this turn. Set Priority to -4. At the CalculateDamageWithoutSpawning call site, gate on this to feed GetRevengePower(User) into the Base Power Override pin.", Category = "Skill Information"))
	bool bIsRevengeStyle = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this Brace? Locks the user in, stores damage taken, then unleashes 2x. Power/Accuracy are ignored.", Category = "Skill Information"))
	bool bIsBideSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "How many turns Brace stores before releasing (2 = store this turn + next, unleash on the 3rd).", Category = "Skill Information", EditCondition = "bIsBideSkill", ClampMin = "1"))
	int32 BideStoreTurns = 2;

	// ============================================
	// PARTIAL TRAP PROPERTIES (Ember Vortex, Ensnare, Bind, Clamp, Whirlpool, Sand Tomb)
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does this move trap the target (no switching + 1/8 chip damage each end of turn)? Ember Vortex, Ensnare, etc.", Category = "Partial Trap"))
	bool bIsTrappingSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Minimum number of turns the trap lasts.", Category = "Partial Trap", EditCondition = "bIsTrappingSkill", ClampMin = "1"))
	int32 TrapMinTurns = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Maximum number of turns the trap lasts.", Category = "Partial Trap", EditCondition = "bIsTrappingSkill", ClampMin = "1"))
	int32 TrapMaxTurns = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The time duration length of the Attack.", Category = "Skill Information"))
	float DurationOfAttack = 1.0f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Checks if we need to move our camera to the enemy.", Category = "Skill Information"))
	bool MoveCameraToEnemy = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this a draining move (heals user based on damage dealt)?", Category = "Skill Information"))
	bool bIsDrainingSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Percentage of damage to heal (0.5 = 50%, 0.75 = 75%, etc.)", Category = "Skill Information"))
	float DrainPercentage = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "is this move healing the creature who casted it?", Category = "Skill Information"))
	bool bIsSelfHealingSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Percentage to heal (0.5 = 50%, 0.75 = 75%, etc.)", Category = "Skill Information"))
	float SelfHealPercentage = 0.5f;

	// ============================================
	// TWO-TURN MOVE PROPERTIES
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does this move take two turns? (Fly, Dig, Bounce, Dive, etc.)", Category = "Two-Turn Skill"))
	bool bIsTwoTurnSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "What semi-invulnerable state the user enters on the charge turn.", Category = "Two-Turn Skill", EditCondition = "bIsTwoTurnSkill"))
	EGF_SemiInvulnerableState TwoTurnState = EGF_SemiInvulnerableState::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Can this move hit a Creature that is In Air (using Fly or Bounce)? e.g. Thunder, Sky Uppercut, Gust.", Category = "Two-Turn Skill"))
	bool bCanHitInAir = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Can this move hit a Creature that is Underground (using Dig)? e.g. Earthquake, Magnitude, Fissure.", Category = "Two-Turn Skill"))
	bool bCanHitUnderground = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Can this move hit a Creature that is Underwater (using Dive)? e.g. Surf, Whirlpool.", Category = "Two-Turn Skill"))
	bool bCanHitUnderwater = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Sets move to be Refresh, cures poison, burn and paralysis", Category = "Skill Information"))
	bool bIsRefresh = false;

	// ============================================
	// CRIT STAGE PROPERTIES
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does this move raise the user's critical hit stage? (Focus Energy, etc.)", Category = "Crit Stage"))
	bool bRaisesCritStage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "How many crit stages to add to the user. 1 = doubles crit chance (classic).", Category = "Crit Stage", EditCondition = "bRaisesCritStage", ClampMin = "1", ClampMax = "4"))
	int32 CritStageBoostAmount = 1;

	// ============================================
	// PROTECT PROPERTIES
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this a Protect-family move? (Protect, Detect, etc.) Sets bIsProtected on the user for the turn.", Category = "Protect"))
	bool bIsProtectSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Does this move bypass Protect? (Feint, etc.)", Category = "Protect"))
	bool bIgnoresProtect = false;

	// ============================================
	// ESCAPE PROPERTIES (Teleport)
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Teleport: instead of attacking, the user flees the battle. Only works in a WILD battle with nothing blocking escape - it fails against Tamers, while partially trapped, and in scripted no-run battles. Power/Accuracy are ignored; Uses is still spent on a failure.", Category = "Escape"))
	bool bIsEscapeSkill = false;

	// ============================================
	// SELF-KO PROPERTIES (Selfdestruct / Explosion)
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Selfdestruct / Explosion: the user downs as part of using the move. The down happens even if the move misses, the target is immune, or the target is behind Protect - the ONLY thing that stops it is Damp on either side, which makes the move fail outright. Do NOT use bKO for this: Bulwark reads bKO and would no-sell the move.", Category = "Self KO"))
	bool bIsSelfKOSkill = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "classic halves the target's Defense against Selfdestruct / Explosion (0.5). Applied inside the damage calc for physical splits only - no Blueprint wiring needed.", Category = "Self KO", EditCondition = "bIsSelfKOSkill", ClampMin = "0.01", ClampMax = "1.0"))
	float SelfKODefenseMultiplier = 0.5f;

	// ============================================
	// FUNCTIONS
	// ============================================

	UFUNCTION(BlueprintCallable, Category = "Skill STATUS Effect")
	EGF_STATUSEffect ApplyStatus();

	// Delegate that fires when a stat stage is successfully applied
	UPROPERTY(BlueprintAssignable, Category = "Skill Stat Stage")
	FGF_OnStatStageApplied OnStatStageApplied;

	// Delegate that fires when flinch is successfully applied
	UPROPERTY(BlueprintAssignable, Category = "Skill Flinch")
	FGF_OnFlinchApplied OnFlinchApplied;

	// Attempts to apply stat stage based on chance. Returns true if successful.
	// Broadcasts OnStatStageApplied delegate when stat stage is applied.
	UFUNCTION(BlueprintCallable, Category = "Skill Stat Stage")
	bool TryApplyStatStage();

	/**
	 * Attempts to apply flinch based on the move's FlinchChance.
	 * Flinch only works if the target hasn't moved yet this turn.
	 *
	 * @param bTargetHasMovedThisTurn - If true, flinch cannot be applied (target already moved)
	 * @param bTargetIsImmuneToFlinch - If true, target has Composed or similar immunity
	 * @return True if flinch was successfully applied
	 */
	UFUNCTION(BlueprintCallable, Category = "Skill Flinch")
	bool TryApplyFlinch(bool bTargetHasMovedThisTurn = false, bool bTargetIsImmuneToFlinch = false);

	/**
	 * Get flinch chance from a move class without spawning
	 * @param SkillClass - The move class to check
	 * @return Flinch chance (0-100), or 0 if move can't cause flinch
	 */
	UFUNCTION(BlueprintCallable, Category = "Skill Flinch")
	static int32 GetFlinchChanceFromClass(TSubclassOf<AGF_SkillDefinition> SkillClass);

	/**
	 * Check if a move can cause flinch from class without spawning
	 */
	UFUNCTION(BlueprintCallable, Category = "Skill Flinch")
	static bool CanSkillCauseFlinch(TSubclassOf<AGF_SkillDefinition> SkillClass);

	/**
	 * Is this an escape move (Teleport)? Read from the class CDO, no spawning.
	 * Use it to branch the turn away from the normal damage path.
	 */
	UFUNCTION(BlueprintPure, Category = "Skill Escape")
	static bool IsEscapeSkill(TSubclassOf<AGF_SkillDefinition> SkillClass);

	/**
	 * Resolves whether a Teleport-style escape actually succeeds this turn.
	 * Call it once Uses has been spent: true = end the battle as a successful run,
	 * false = print "But it failed!" and continue the turn.
	 *
	 * @param SkillClass             The move being used. Returns false if it isn't an escape move.
	 * @param User                  The Creature using it — checked for partial traps (Ensnare, Ember Vortex).
	 * @param bIsTamerBattle      True in a Tamer battle, where escape always fails.
	 * @param bEscapeBlockedByScript Set from the battle BP for scripted no-run battles
	 *                              (rival intro, legendary encounters), and for future
	 *                              escape blockers: Mean Look, Ingrain, Arena Trap / Shadow Tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Skill Escape")
	static bool CanEscapeWithSkill(TSubclassOf<AGF_SkillDefinition> SkillClass, AGF_Creature* User, bool bIsTamerBattle, bool bEscapeBlockedByScript = false);

	/**
	 * Is this a Selfdestruct / Explosion style move? Read from the class CDO, no spawning.
	 * Gate the turn on this, then check Damp with UGF_CreatureTraitLibrary::DoesDampBlockSkill
	 * before spending the user's HP.
	 */
	UFUNCTION(BlueprintPure, Category = "Skill Self KO")
	static bool IsSelfKOSkill(TSubclassOf<AGF_SkillDefinition> SkillClass);
};