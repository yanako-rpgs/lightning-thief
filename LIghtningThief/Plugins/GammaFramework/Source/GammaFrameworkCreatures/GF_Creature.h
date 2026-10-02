// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PaperFlipbook.h"
#include <string>
#include <map>
#include "GF_SkillDefinition.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_ElementTypes.h"
#include "GF_Creature.generated.h"


// Forward declaration (no include needed in header)
struct FGF_CreatureInstanceData;



	// This is where the Creature Typing will live.
//Creature Weak/Resistant/Immune
//Creature Temperaments
//Creature Temperaments
UENUM(BlueprintType)
enum class EGF_CreatureCurrentGender : uint8
{
	Male		UMETA(DisplayName = "Male"),
	Female		UMETA(DisplayName = "Female"),
	Genderless		UMETA(DisplayName = "Genderless"),

};


//Creature STATUS
UENUM(BlueprintType)
enum class EGF_STATUS : uint8
{
	None	UMETA(DisplayName = "None"),
	Burned		UMETA(DisplayName = "Burned"),
	Paralyzed		UMETA(DisplayName = "Paralyzed"),
	Poisoned		UMETA(DisplayName = "Poisoned"),
	Sleeping		UMETA(DisplayName = "Sleeping"),
	Frozen		UMETA(DisplayName = "Frozen"),
	Invigorated		UMETA(DisplayName = "Invigorated"),
	Confused		UMETA(DisplayName = "Confused"),
};



// Creature Stats will go here.
USTRUCT(BlueprintType)
struct FGF_CreatureCurrentStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float MaxHP = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float CurrentHP = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float Attack = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float Defense = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float Magic = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float Poise = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float Speed = 5;


};


// Creature APs
USTRUCT(BlueprintType)
struct FGF_CreatureAPs
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 HP_AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Attack_AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Defense_AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Magic_AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Poise_AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Speed_AP = 0;

};

// Creature EPs
USTRUCT(BlueprintType)
struct FGF_CreatureEPs
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 HP_EP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Attack_EP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Defense_EP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Magic_EP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Poise_EP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 Speed_EP = 0;

};

// Creature Temperament Mods
USTRUCT(BlueprintType)
struct FGF_CreatureTemperamentMods
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float HPTemperamentMod = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float ATKTemperamentMod = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float DEFTemperamentMod = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float MAGICTemperamentMod = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float POISETemperamentMod = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	float SPDTemperamentMod = 0;

};

// Creature Temperament Mods
USTRUCT(BlueprintType)
struct FGF_SkillUses
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 CurrentUses = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature Core")
	int32 MaxUses = 0;

};




UCLASS()
class GAMMAFRAMEWORKCREATURES_API AGF_Creature : public AActor
{
	GENERATED_BODY()





public:
	AGF_Creature();



#pragma region Creature CORE

	// Creature Name

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Core")
	UGF_CreatureSpeciesData* SpeciesData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Creature Display Name.", Category = "Creature | Core"))
	FName Name = "Default Creature";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "First frame of Creature Icon used in the Vault or Summary.", Category = "Creature | Core"))
	UPaperSprite* DisplayIcon1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Second frame of Creature Icon used in the Vault or Summary.", Category = "Creature | Core"))
	UPaperSprite* DisplayIcon2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "First frame of Unique Creature Icon used in the Vault or Summary.", Category = "Creature | Core"))
	UPaperSprite* UniqueDisplayIcon1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Second frame of Unique Creature Icon used in the Vault or Summary.", Category = "Creature | Core"))
	UPaperSprite* UniqueDisplayIcon2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The icon of the ball the Creature was caught or obtained with.", Category = "Creature | Core"))
	UTexture2D* CaughtCore;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Core")
	FName CaughtCoreName = "Simple Core";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The animation of the ball from the caught ball.", Category = "Creature | Core"))
	UPaperFlipbook* CaughtCoreAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The animation of the ball used to call back a creature (Should be the same as the caught ball).", Category = "Creature | Core"))
	UPaperFlipbook* ReturnCoreAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The cry that plays when the Creature is sent into battle.", Category = "Creature | Core"))
	USoundWave* Call;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Gender of the Creature.", Category = "Creature | Core"))
	EGF_CreatureGender Gender = EGF_CreatureGender::Genderless;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "If the Creature is Genderless.", Category = "Creature | Core"))
	bool isGenderless;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Original Tamer Name.", Category = "Creature | Core"))
	FName OriginalTamerName = "Original Tamer Name";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Current Tamer Name.", Category = "Creature | Core"))
	FName TamerName = "Tamer Name";

		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Core")
	FGuid UniqueID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Core")
	int32 OriginalTamerID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Core")
	int32 CurrentTamerID = 0;

	/**
	 * Which seat on its side this creature is standing in, 0..3, or -1 when it
	 * is not on the field.
	 *
	 * Set by the board the moment a creature takes a seat, which is the moment
	 * anything hanging off it -- a health bar, a ring, a camera -- wants to know.
	 * Asking the board instead means asking before the board is populated, and
	 * every route to it is a lookup that can be timed wrong.
	 *
	 * A plain index rather than a battle slot: FGF_BattleSlot lives in the Battle
	 * module, and Creatures must never depend on it. isPlayerCreature already
	 * says which side.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Creature | Battle")
	int32 BattleSeatIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The moves the Creature currently knows.", Category = "Creature | Core"))
	TArray<TSubclassOf<AGF_SkillDefinition>> Skills;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The move that is being readied for the next turn.", Category = "Creature | Core"))
	TSubclassOf<AGF_SkillDefinition> SkillInQueue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Power Points of the moves.", Category = "Creature | Core"))
	TArray<FGF_SkillUses> SkillUses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Core")
	FName HeldItem = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The last move selected from battle.", Category = "Creature | Statistics | Core"))
	int32 LastSkillSelected = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Compendium Entry number of the Creature.", Category = "Creature | Statistics | Core"))
	int32 CompendiumNumber = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Unique number given to the creature (Randomly Generated upon creation).", Category = "Creature | Statistics | Core"))
	int32 CreatureID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Crit Stage of the next attack.", Category = "Creature | Statistics | Core"))
	int32 CritStage = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is the Creature Downed?.", Category = "Creature | Statistics | Core"))
	bool isDowned;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this the player's Creature?", Category = "Creature | Statistics | Core"))
	bool isPlayerCreature;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this a wild Creature?", Category = "Creature | Statistics | Core"))
	bool isWildCreature;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this Creature currently on Stage? (Is out outside its core during battle?)", Category = "Creature | Statistics | Core"))
	bool isOnStage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Is this Creature unique?", Category = "Creature | Statistics | Core"))
	bool isUnique;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Are we debugging this creature?", Category = "Creature | Statistics | Core"))
	bool isDebuggingMode;





#pragma endregion

#pragma region Pokemmon STATISTICS
	//Creature Typings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "First and main Typing of the Creature.", Category = "Creature | Statistics | Typings"))
	EGF_Element PrimaryElement = EGF_Element::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Secondary (double) typing of the Creature.", Category = "Creature | Statistics | Typings"))
	EGF_Element SecondaryElement = EGF_Element::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Type the Creature are WEAK too.", Category = "Creature | Statistics | Typings"))
	TArray<EGF_Element> Weakness;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Type the Creature are RESISTANT too.", Category = "Creature | Statistics | Typings"))
	TArray<EGF_Element> Resistant;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Type the Creature are IMMUNE too.", Category = "Creature | Statistics | Typings"))
	TArray<EGF_Element> Immune;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The Temperament of the Creature.", Category = "Creature | Statistics | Temperament"))
	EGF_Temperament CreatureTemperament = EGF_Temperament::Ferocious;

	//Creature Base Stats
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "This is where the Creature BASE STATS go!", Category = "Creature | Statistics | System"))
	FGF_CreatureBaseStats InstanceBaseStats;  // Renamed to avoid conflict

	//Creature Current Stats
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "This is where the Creature CURRENT STATS go!", Category = "Creature | Statistics | System"))
	FGF_CreatureCurrentStats CurrentStats;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "This is where the Creature APs go!", Category = "Creature | Statistics | System"))
	FGF_CreatureAPs APs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "This is where the Creature EPs go!", Category = "Creature | Statistics | System"))
	FGF_CreatureEPs EPs;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The current STATUS of the Creature.", Category = "Creature | Statistics | System"))
	EGF_STATUS Status = EGF_STATUS::None;

	UPROPERTY(BlueprintReadWrite, meta = (ToolTip = "The number of the sleep counter generated.", Category = "Creature | Statistics | System"))
	int32 SleepCounter = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "EXP Curve for the Creature to Level Up.", Category = "Creature | Statistics | EXP System"))
	EGF_EXPCurves EXPCurves = EGF_EXPCurves::Measured;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The base EXP used to calculate the EXP amount mod for the victor.", Category = "Creature | Statistics | EXP System"))
	float BaseEXP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The base EXP used to calculate the EXP amount mod for the victor.", Category = "Creature | Statistics | EXP System"))
	float CurrentEXP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The base EXP used to calculate the EXP amount mod for the victor.", Category = "Creature | Statistics | EXP System"))
	float TotalEXPGained = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The base EXP used to calculate the EXP amount mod for the victor.", Category = "Creature | Statistics | EXP System"))
	float EXPNeededToNextLevel = 100;

#pragma endregion

#pragma region Creature ANIMATIONS

	/**
	 * The side-on flipbook sets, copied off the species at initialisation.
	 *
	 * One set of art per creature, not a front set and a back set: the stage is
	 * viewed in profile, so there is no angle that shows a face on one line and a
	 * back on the other.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Side-on battle animations."), Category = "Creature | Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Side-on battle animations for the unique form."), Category = "Creature | Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimationsUnique;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Side-on battle animations for visibly different females."), Category = "Creature | Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimationsFemale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Side-on battle animations for unique females."), Category = "Creature | Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimationsFemaleUnique;

	/**
	 * Fraction of MaxHP at or below which Idle becomes IdleLowHP.
	 *
	 * A third, matching where the genre puts the low-health warning, so the art
	 * change and the beeping health bar happen at the same moment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Animations", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHPAnimationThreshold = 0.33f;

	/**
	 * The flipbook for a state, resolved against THIS creature's unique flag and
	 * gender, with the same fallbacks the species asset applies.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature | Animations")
	UPaperFlipbook* GetAnimationForState(EGF_CreatureAnimState State) const;

	/**
	 * Idle or IdleLowHP, whichever this creature's current HP calls for.
	 * The resting-state lookup, so nothing has to re-check the threshold.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature | Animations")
	UPaperFlipbook* GetIdleAnimation() const;

	UFUNCTION(BlueprintPure, Category = "Creature | Animations")
	bool IsAtLowHP() const;

	/**
	 * True when the last requested state was a one-shot -- Call, Attack, Hurt
	 * or Down -- rather than a resting loop.
	 *
	 * Based on what was last REQUESTED, not on what a component is playing:
	 * the framework owns no components and cannot know whether this creature is
	 * a flipbook, a mesh or something else.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature | Animations")
	bool IsPlayingOneShotAnimation() const;

#pragma endregion

#pragma region Creature FUNCTIONS


	//Creature Functions

	UFUNCTION(BlueprintCallable, Category = "Creature | Data")
	void InitializeFromInstanceData(const FGF_CreatureInstanceData& InstanceData);

	/**
	 * Raised at the very end of InitializeFromInstanceData, once species data,
	 * stats, elements and the flipbook maps are all populated.
	 *
	 * This is where a Blueprint subclass sets up its presentation. It cannot be
	 * done in BeginPlay: the spawn path calls SpawnActor first and initialises
	 * afterwards, so BeginPlay runs against a creature that has no species yet
	 * and no animations to read.
	 *
	 * The framework deliberately owns no visual components -- a project decides
	 * whether a creature is a flipbook, a mesh or nothing at all -- so this is
	 * the only ordering guarantee it can offer.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Data")
	void OnCreatureInitialized();

	/**
	 * HP moved, for any reason -- a hit, a heal, a status tick, a revive.
	 *
	 * The only signal a subclass needs to keep the resting animation honest,
	 * since IdleLowHP has to swap the moment HP crosses LowHPAnimationThreshold
	 * and there is no other way to notice that without polling on Tick.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Presentation")
	void OnHealthChanged(float OldHP, float NewHP);

	/**
	 * Damage specifically, separated from OnHealthChanged because a hurt
	 * reaction wants to know how hard it landed and a health bar does not.
	 * Raised before OnHealthChanged so a flinch can start on the same frame the
	 * number moves.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Presentation")
	void OnDamageTaken(float Amount, bool bWasCritical);

	/** Went down. Raised once, after HP has already reached zero. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Presentation")
	void OnCreatureDowned();

	/**
	 * A stat stage moved on this creature. Positive is a rise, negative a fall.
	 *
	 * ActualChange is what happened after the -6..+6 clamp and any trait block,
	 * not what was asked for -- so this only fires when something really
	 * changed, and can drive the rise/fall effects without further checking.
	 *
	 * Raised for every source, not only skills: items, traits and end-of-turn
	 * effects all route through the same place in the resolver.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Presentation")
	void OnStatStageChanged(EGF_StatStages Stat, int32 ActualChange, bool bAtLimit);

	/**
	 * A status condition took hold, or was cured.
	 *
	 * Condition names which one it was in both cases -- on a cure that is
	 * the condition being removed, so the right effect can play for shaking off
	 * a burn as opposed to a freeze.
	 *
	 * Raised from ApplyStatusCondition and ClearStatusCondition, so it covers
	 * skills, contact traits, held items and end-of-turn effects alike. A
	 * condition that was refused -- because one is already in place -- does not
	 * raise it.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Presentation")
	void OnStatusConditionChanged(EGF_STATUSEffect Condition, bool bCleared);

	/**
	 * Play a one-shot animation state and return to rest afterwards.
	 *
	 * The framework cannot do this itself -- it owns no components and does not
	 * know whether this creature is a flipbook, a mesh or a particle system --
	 * so it says WHAT should play and the subclass decides how.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Creature | Presentation")
	void OnPlayAnimationState(EGF_CreatureAnimState State);

	/**
	 * Raise the presentation events above. Call these rather than writing to
	 * CurrentHP directly, or a subclass will never hear about the change.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature | Presentation")
	void NotifyHealthChanged(float OldHP, float NewHP);

	UFUNCTION(BlueprintCallable, Category = "Creature | Presentation")
	void NotifyDamageTaken(float Amount, bool bWasCritical);

	UFUNCTION(BlueprintCallable, Category = "Creature | Presentation")
	void NotifyStatStageChanged(EGF_StatStages Stat, int32 ActualChange, bool bAtLimit);

	UFUNCTION(BlueprintCallable, Category = "Creature | Presentation")
	void PlayAnimationState(EGF_CreatureAnimState State);

	/**
	 * The last state PlayAnimationState was asked for.
	 *
	 * Debug only. A Blueprint that resolves the wrong flipbook plays something
	 * other than what was requested, and without both halves visible that is
	 * near impossible to spot -- it just looks like the wrong animation.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Creature | Presentation")
	EGF_CreatureAnimState LastAnimStateRequested = EGF_CreatureAnimState::Idle;

	UFUNCTION(BlueprintCallable, Category = "Creature | Data")
	FGF_CreatureInstanceData ExportToInstanceData() const;

	UFUNCTION(BlueprintCallable, Category = "Creature | Data")
	void RecalculateStatsFromInstanceData(const FGF_CreatureInstanceData& InstanceData);

	UFUNCTION(BlueprintCallable)
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, class AActor* DamageCauser) override;

	UFUNCTION(BlueprintCallable)
	bool IsDowned() const;

	UFUNCTION(BlueprintCallable)
	void Heal(float HealAmount);

	//Calculate the Base Stats of the Creature
	UFUNCTION(BlueprintCallable, Category = "Creature | Stats")
	float CalculateBaseStats(float BaseStat, int32 AP, int32 EP, int32 Level, float temperamentMod);

	UFUNCTION(BlueprintCallable, Category = "Creature | Stats")
	float CalculateBaseHP(float BaseStat, int32 AP, int32 EP, int32 Level);

	//Roll APs
	UFUNCTION(BlueprintCallable, Category = "Creature | Stats")
	void CreateAPs();

	//SetWildInfo
	UFUNCTION(BlueprintCallable, Category = "Creature | Stats")
	void GenerateStats();

	UFUNCTION(BlueprintCallable, Category = "Creature | Stats")
	void RefreshStats();

	UFUNCTION(BlueprintCallable, Category = "Creature | Stats")
	void CreateRandomTemperament();

	UFUNCTION(BlueprintCallable, Category = "Creature | Core")
	EGF_Temperament SetRandomTemperament();

	UFUNCTION(BlueprintCallable, Category = "Creature | Core")
	void SetRandomGender();

	UFUNCTION(BlueprintCallable, Category = "Creature | Core")
	const TArray<TSubclassOf<AGF_SkillDefinition>>& GetSkills() const;

	// ============================================
// VOLATILE STATUS TRACKING (Battle-only, reset when switching or turn ends)
// ============================================

/**
 * Current semi-invulnerable state (Fly, Dig, Dive, etc.)
 * Set at the start of the charge turn, cleared after the attack fires on turn 2.
 * While set, most moves automatically miss this Creature.
 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
EGF_SemiInvulnerableState SemiInvulnerableState = EGF_SemiInvulnerableState::None;

/**
 * True when this Creature has used a two-turn move (Fly/Dig/Bounce/Dive/etc.)
 * and is waiting to execute the attack on turn 2.
 * Set on the charge turn, cleared after the attack fires.
 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsChargingTwoTurnSkill = false;

/**
 * The move to execute on turn 2 of a two-turn move.
 * Set when bIsChargingTwoTurnSkill becomes true, cleared after the attack fires.
 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
TSubclassOf<AGF_SkillDefinition> PendingTwoTurnSkill;

/**
 * Is this Creature flinching this turn?
 * Flinch prevents the Creature from acting if they haven't moved yet.
 * Cleared at end of turn.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsFlinched = false;

/**
 * Has this Creature already acted this turn?
 * Used for flinch logic - flinch only works on Creature that haven't moved yet.
 * Cleared at start of new turn.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bHasMovedThisTurn = false;

/**
 * Is this Creature immune to flinching?
 * True if they have Composed trait or similar.
 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsImmuneToFlinch = false;

/**
 * Is this Creature confused (volatile confusion, separate from status)?
 * Confusion can be both a status effect and a volatile condition.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsConfused = false;

/**
 * Number of turns remaining for confusion.
 * Typically 1-4 turns in the genre.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
int32 ConfusionTurnsRemaining = 0;

/**
 * Is this Creature protected this turn? (Protect, Detect, etc.)
 * Cleared at the start of the next turn.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsProtected = false;

/**
 * Is this Creature Bracing this round? Halves incoming damage and raises Poise
 * one stage. Set when a Brace order resolves, cleared at the start of the next
 * round.
 *
 * NOT the same thing as the Brace SKILL, which is the store-and-release move
 * (bIsBiding below). Two different mechanics wearing the same word: this one is
 * the action-menu stance every creature can take, that one is a skill a species
 * has to learn. Worth renaming one of them.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsBracing = false;

/**
 * How many consecutive turns Protect has been used.
 * Each consecutive use halves the success chance (classic mechanic).
 * Reset to 0 when any other move is used.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
int32 ConsecutiveProtectCount = 0;

/**
 * How many consecutive times Rising Slash (or a Fury-Cutter-style move) has hit.
 * Power = 40 * 2^Count, capped at 160. Reset to 0 on a miss, on switch out,
 * or when a different move is used.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
int32 FuryCutterCount = 0;

/**
 * Is this Creature caught in a partial trap? (Ember Vortex, Ensnare, Bind, Clamp,
 * Whirlpool, Sand Tomb.) While trapped it cannot switch out and takes chip
 * damage at the end of each turn. Cleared when the timer runs out or it downs.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsPartiallyTrapped = false;

/**
 * Turns remaining on the partial trap (2-5). Decrement at end of turn; 0 = freed.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
int32 PartialTrapTurnsRemaining = 0;

/**
 * Display name of the move that caused the trap, for the end-of-turn messages
 * ("X was hurt by Ember Vortex!" / "X was freed from Ember Vortex!").
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
FName PartialTrapMoveName = NAME_None;

/**
 * Is this Creature currently using Brace? While biding it is locked into the move
 * (can't choose another or switch) and stores all damage it takes. Released as
 * 2x the stored damage when the counter runs out.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bIsBiding = false;

/**
 * Storing turns left before Brace unleashes. Decrement on each of the user's turns;
 * when it hits 0 it's the release turn.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
int32 BideTurnsRemaining = 0;

/**
 * Total damage taken while biding. Released as 2x this value, then reset.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
float BideDamageStored = 0.0f;

/**
 * Crit stage boost from moves like Focus Energy.
 * 0 = normal, 1 = Focus Energy active (shifts crit chance one stage up).
 * Persists until the Creature switches out or the battle ends.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
int32 CritStageBoost = 0;

/**
 * Did this Creature take damage from an attack THIS turn? Powers Revenge (doubles
 * to 120 if true). Clear at the start of each round before anyone acts, and set
 * true whenever the Creature takes attack damage.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Volatile Status")
bool bTookDamageThisTurn = false;

// ============================================
// TRAIT
// ============================================

/**
 * The trait this Creature is battling with RIGHT NOW. Always read this in battle
 * rather than the instance data, because Trace overwrites it mid-fight.
 * Seeded by UGF_CreatureTraitLibrary::InitializeTraitOnActor when the Creature
 * is sent out.
 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Battle | Trait")
EGF_CreatureTrait CurrentTrait = EGF_CreatureTrait::None;

/**
 * The trait the Creature actually owns. CurrentTrait is restored to this on
 * switch-out so a Traced trait doesn't stick around.
 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature | Battle | Trait")
EGF_CreatureTrait OriginalTrait = EGF_CreatureTrait::None;

/**
 * Sluggard bookkeeping: true means this Creature loafs on its NEXT action.
 * Driven entirely by UGF_CreatureTraitLibrary::TruantCanActThisTurn — don't set
 * it by hand. Reset on switch-in.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle | Trait")
bool bTruantLoafingThisTurn = false;

/**
 * True once this Creature has taken any action since it entered the field.
 * First-turn-only skills (Surprise Attack, Early Bird) fail once it is set.
 * Set by UGF_BattleResolver::ApplyResolution; cleared on send-out and switch-in.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle")
bool bHasActedSinceEntering = false;

/**
 * True when this Creature's next turn is spent recharging, after a recharge
 * skill (Charge Cannon, Dark Bond) connected. The resolver turns its next action
 * into a "must recharge" turn and clears it. Cleared on switch-out.
 */
UPROPERTY(BlueprintReadWrite, Category = "Creature | Battle")
bool bMustRecharge = false;


// ============================================
// ADD THESE FUNCTIONS in #pragma region Creature FUNCTIONS
// ============================================

/**
 * Apply flinch to this Creature.
 *
 * Also runs Resolute, which raises Speed one stage every time its holder flinches.
 * That happens here rather than through an trait hook because this is the single
 * place flinch is ever applied, so Blueprint needs no extra wiring.
 *
 * @param OutTraitMessage  Resolute's message, ready to print. Empty when nothing fired.
 * @return True if flinch was applied, false if immune or already flinched
 */
UFUNCTION(BlueprintCallable, Category = "Creature | Battle | Volatile Status")
bool ApplyFlinch(FString& OutTraitMessage);

/**
 * Check if this Creature is currently flinched
 */
UFUNCTION(BlueprintPure, Category = "Creature | Battle | Volatile Status")
bool IsFlinched() const { return bIsFlinched; }

/**
 * Check if this Creature can be flinched
 * @return False if already moved, immune to flinch, or already flinched
 */
UFUNCTION(BlueprintPure, Category = "Creature | Battle | Volatile Status")
bool CanBeFlinched() const;

/**
 * Called when this Creature takes their turn action
 * Marks them as having moved this turn (prevents flinch from working)
 */
UFUNCTION(BlueprintCallable, Category = "Creature | Battle | Volatile Status")
void MarkAsMovedThisTurn();

/**
 * Reset all volatile statuses (call at start of new turn or when switching)
 */
UFUNCTION(BlueprintCallable, Category = "Creature | Battle | Volatile Status")
void ResetVolatileStatuses();

/**
 * Reset turn-based flags (call at start of each turn)
 * This clears bHasMovedThisTurn and bIsFlinched
 */
UFUNCTION(BlueprintCallable, Category = "Creature | Battle | Volatile Status")
void ResetTurnFlags();

/**
 * Get the flinch message for battle dialogue
 * @return Message like "Voltkit flinched!" or "The wild Voltkit flinched!"
 */
UFUNCTION(BlueprintPure, Category = "Creature | Battle | Volatile Status")
FString GetFlinchMessage() const;

UFUNCTION(BlueprintCallable, Category = "Creature | Skills")
void SetSkillSlot(int32 SlotIndex, TSubclassOf<AGF_SkillDefinition> SkillClass);

// ============================================
// STATUS
// ============================================

/**
 * Apply a non-volatile status condition to this Creature.
 *
 * Exists mainly so Blueprint has a way across the EGF_STATUSEffect -> EGF_STATUS gap:
 * moves, traits and save data all speak EGF_STATUSEffect, while the battle actor
 * stores EGF_STATUS. They're separate UENUMs, so BP can't wire one into the other.
 *
 * Feed it the StatusToApply pin from On Contact Made, or a move's ApplyStatus result.
 *
 * Does NOT check type immunity or protective traits — On Contact Made and
 * CanStatusSkillAffect already do that upstream. It only refuses to stack a second
 * status on top of an existing one.
 *
 * @param NewStatus         The status to apply. None is a no-op.
 * @param bOverrideExisting Replace a status this Creature already has (Rest, debug).
 * @param MinSleepTurns     Sleep counter lower bound, used only for Sleeping.
 * @param MaxSleepTurns     Sleep counter upper bound. Match these to whatever your
 *                          existing sleep moves (Spore) roll — 1-3 is a placeholder.
 * @return True if the status was actually applied.
 */
UFUNCTION(BlueprintCallable, Category = "Creature | Battle | Status")
bool ApplyStatusCondition(EGF_STATUSEffect NewStatus, bool bOverrideExisting = false,
	int32 MinSleepTurns = 1, int32 MaxSleepTurns = 3);

/** Clear any non-volatile status and reset the sleep counter. */
UFUNCTION(BlueprintCallable, Category = "Creature | Battle | Status")
void ClearStatusCondition();

/** This Creature's status as the EGF_STATUSEffect the rest of the systems speak. */
UFUNCTION(BlueprintPure, Category = "Creature | Battle | Status")
EGF_STATUSEffect GetStatusAsEffect() const;


























#pragma endregion



protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;


public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;



};
