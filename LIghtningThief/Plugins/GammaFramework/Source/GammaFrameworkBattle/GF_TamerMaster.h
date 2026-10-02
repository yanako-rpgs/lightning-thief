// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GF_CreatureInstanceData.h"
#include "GF_Creature.h"
#include "GF_SkillDefinition.h"
#include "GF_CreatureManagerSubsystem.h"
#include "PaperFlipbook.h"
#include "GF_BattleParticipantInterface.h"
#include "GF_ElementTypes.h"
#include "GF_TamerMaster.generated.h"

UENUM(BlueprintType)
enum class EGF_Decisions : uint8
{
	Deciding UMETA(DisplayName = "Deciding"),
	Attacking UMETA(DisplayName = "Attacking"),
	Healing UMETA(DisplayName = "Healing"),
	LastCreature UMETA(DisplayName = "LastCreature"),
	Lost UMETA(DisplayName = "Lost"),
	Victory UMETA(DisplayName = "Victory")
};

UENUM(BlueprintType)
enum class EGF_TamerAnimations : uint8
{
	Intro UMETA(DisplayName = "Intro"),
	Idle UMETA(DisplayName = "Idle"),
	Throw UMETA(DisplayName = "Throw"),
	LastCreatureReaction UMETA(DisplayName = "Last Creature Reaction")
};

USTRUCT(BlueprintType)
struct FGF_TamerCreatureSetup
{
	GENERATED_BODY()

	/**
	 * Which species this team slot is.
	 *
	 * SOFT on purpose. As a hard UGF_CreatureSpeciesData* every tamer placed in a
	 * loaded sublevel pulled in their whole TeamSetup AND RematchTeamSetup, and each
	 * species hard-references its full sprite set (~35 MB). Measured 90 species
	 * resident from walking the overworld -- the starter evolution lines in that
	 * list were rival battles.
	 *
	 * IMPORTANT for Blueprints: connecting this pin straight into something that
	 * wants a UGF_CreatureSpeciesData* compiles WITHOUT error -- UE inserts a hidden
	 * "Resolve Soft Reference" autocast, which returns None when the asset is not
	 * already loaded. It will silently hand you a null species. Use
	 * AGF_TamerMaster::ResolveSetupSpecies, which actually loads it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UGF_CreatureSpeciesData> Species;


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Level = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSubclassOf<AGF_SkillDefinition>> ForcedSkills;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName HeldItem;

	// --- Optional preset overrides ---
	// If bPresetGender is true, Gender is used instead of a random roll
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bPresetGender = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetGender"))
	EGF_CreatureGender Gender = EGF_CreatureGender::Male;

	// If bPresetTemperament is true, Temperament is used instead of a random roll
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bPresetTemperament = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetTemperament"))
	EGF_Temperament Temperament = EGF_Temperament::Robust;

	// Forces this Creature to be unique. Leaving it false does NOT force it non-unique —
	// the normal 1/4096 roll from Initialize() still stands, so a tamer can luck into one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bForceUnique = false;

	// If bPresetAPs is true, all six values below replace the random rolls
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bPresetAPs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetAPs", ClampMin = "0", ClampMax = "50"))
	int32 HP_AP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetAPs", ClampMin = "0", ClampMax = "50"))
	int32 Attack_AP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetAPs", ClampMin = "0", ClampMax = "50"))
	int32 Defense_AP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetAPs", ClampMin = "0", ClampMax = "50"))
	int32 Magic_AP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetAPs", ClampMin = "0", ClampMax = "50"))
	int32 Poise_AP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (EditCondition = "bPresetAPs", ClampMin = "0", ClampMax = "50"))
	int32 Speed_AP = 50;
};

// AI Difficulty levels
UENUM(BlueprintType)
enum class EGF_TamerAIDifficulty : uint8
{
    Random      UMETA(DisplayName = "Random"),      // Just picks random moves
    Basic       UMETA(DisplayName = "Basic"),       // Picks highest power move
    Smart       UMETA(DisplayName = "Smart"),       // Considers type effectiveness
    Expert      UMETA(DisplayName = "Expert")       // Full strategy with switching
};

// What the AI decided to do
UENUM(BlueprintType)
enum class EGF_TamerDecision : uint8
{
    Attack          UMETA(DisplayName = "Attack"),
    SwitchCreature   UMETA(DisplayName = "Switch Creature"),
    UseItem         UMETA(DisplayName = "Use Item"),
    Undecided       UMETA(DisplayName = "Undecided")
};

// Which healing item the AI chose to throw
UENUM(BlueprintType)
enum class EGF_TamerHealItem : uint8
{
    None        UMETA(DisplayName = "None"),
    MinorHeal      UMETA(DisplayName = "Minor Heal"),
    MajorHeal UMETA(DisplayName = "Major Heal"),
    FullHeal UMETA(DisplayName = "Full Heal")
};

// Result of AI decision
USTRUCT(BlueprintType)
struct FGF_TamerAIDecision
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    EGF_TamerDecision Decision = EGF_TamerDecision::Undecided;

    // If attacking, which move to use
    UPROPERTY(BlueprintReadOnly)
    int32 SkillIndex = 0;

    UPROPERTY(BlueprintReadOnly)
    TSubclassOf<AGF_SkillDefinition> SelectedSkill;

    // If switching, which Creature to switch to
    UPROPERTY(BlueprintReadOnly)
    int32 SwitchToIndex = -1;

    // If using an item, which healing item to throw
    UPROPERTY(BlueprintReadOnly)
    EGF_TamerHealItem ItemToUse = EGF_TamerHealItem::None;

    // How much HP the chosen item will restore (for UI/messages)
    UPROPERTY(BlueprintReadOnly)
    int32 ItemHealAmount = 0;

    // For debugging/dialogue
    UPROPERTY(BlueprintReadOnly)
    FString ReasonForDecision;
};

// Skill evaluation for AI
USTRUCT(BlueprintType)
struct FGF_SkillEvaluation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 SkillIndex = 0;

    UPROPERTY(BlueprintReadOnly)
    TSubclassOf<AGF_SkillDefinition> SkillClass;

    UPROPERTY(BlueprintReadOnly)
    float Score = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float TypeEffectiveness = 1.0f;

    UPROPERTY(BlueprintReadOnly)
    bool bHasSTAB = false;

    UPROPERTY(BlueprintReadOnly)
    bool bHasUses = true;
};


// Result of throwing a healing item — gives the UI everything it needs to tween the HP bar
USTRUCT(BlueprintType)
struct FGF_TamerHealResult
{
    GENERATED_BODY()

    // True if an item was actually consumed and applied
    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;

    // HP before healing — tween FROM this value
    UPROPERTY(BlueprintReadOnly)
    int32 OldHP = 0;

    // HP after healing (already clamped to MaxHP) — tween TO this value
    UPROPERTY(BlueprintReadOnly)
    int32 NewHP = 0;

    // Actual HP restored (NewHP - OldHP). For 5/50 + Full Salve this is 45, not 50.
    UPROPERTY(BlueprintReadOnly)
    int32 AmountHealed = 0;

    // True if a Full Salve cleared a major status condition this call
    UPROPERTY(BlueprintReadOnly)
    bool bStatusCured = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTamerCreatureDowned, int32, CreatureIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnTamerCreatureStatusChanged, int32, CreatureIndex, EGF_STATUSEffect, NewStatus);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnTamerDefeated);

UCLASS()
class GAMMAFRAMEWORKBATTLE_API AGF_TamerMaster : public AActor, public IGF_BattleParticipantInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AGF_TamerMaster();

	// Fired when a tamer Creature downs — widget binds to this for drop animation
	UPROPERTY(BlueprintAssignable, Category = "Tamer Events")
	FGF_OnTamerCreatureDowned OnTamerCreatureDowned;

	// Fired when a tamer Creature gains/loses a status condition
	UPROPERTY(BlueprintAssignable, Category = "Tamer Events")
	FGF_OnTamerCreatureStatusChanged OnTamerCreatureStatusChanged;

	// Fired when all tamer Creature have downed — use this to trigger defeat dialogue and reward
	UPROPERTY(BlueprintAssignable, Category = "Tamer Events")
	FGF_OnTamerDefeated OnTamerDefeated;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Tamer Team")
	TArray<FGF_TamerCreatureSetup> TeamSetup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Team")
	TArray<AGF_Creature*> Team;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Team")
	TArray<FGF_CreatureInstanceData> TeamInstanceData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Team")
	TSubclassOf<AGF_Creature> CreatureClassToSpawn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Team")
	int32 TamerID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Team")
	FName TamerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Team")
	int32 CurrentCreatureIndex;

	// Index into TeamSetup/TeamInstanceData of this tamer's ACE. -1 = no ace (ordinary tamer).
	// The ace is held in reserve: it will never lead, and the AI will never send it out or
	// voluntarily switch to it while any other Creature on the team is still able to fight.
	// It comes out last, and only last. Set this to the last slot for a classic Trial Leader ace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Team", meta = (ClampMin = "-1"))
	int32 AceIndex = -1;

	// True while Index is the ace AND at least one non-ace Creature can still fight — i.e. the
	// ace is not allowed on the field yet. Every send-out and switch path consults this.
	UFUNCTION(BlueprintPure, Category="Tamer Team")
	bool IsAceLocked(int32 Index) const;

	// ============================================
	// REMATCH TEAM
	// ============================================
	// A rematch loadout is authored IN FULL and swapped in wholesale by InitializeTeam.
	// Nothing carries over from the main-team settings below — ace slot, AI tier and item
	// stock are all read from the Rematch* values when a rematch is running, so a tamer
	// that should still throw potions on a rematch needs them entered here too.

	// The team this tamer fields when the battle is a rematch. Leave it EMPTY and the
	// tamer simply has no rematch team: InitializeTeam falls back to TeamSetup and logs
	// a warning. That fallback is deliberate — a tamer with an empty team reaches the
	// player as a battle that cannot be won or escaped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch")
	TArray<FGF_TamerCreatureSetup> RematchTeamSetup;

	// Ace slot for the REMATCH team (-1 = no ace). Indexes RematchTeamSetup, NOT TeamSetup —
	// the two teams can be different sizes, so reusing AceIndex would point at the wrong slot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch", meta = (ClampMin = "-1"))
	int32 RematchAceIndex = -1;

	// AI tier for the rematch. Still shifted by the player's difficulty option at runtime,
	// exactly like AIDifficulty — read GetEffectiveAIDifficulty() in AI code either way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch")
	EGF_TamerAIDifficulty RematchAIDifficulty = EGF_TamerAIDifficulty::Expert;

	// Healing item stock for the rematch. Defaults to zero: set these or the rematch
	// tamer throws nothing, however many potions the main-team loadout carried.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch", meta = (ClampMin = "0"))
	int32 RematchPotionAmount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch", meta = (ClampMin = "0"))
	int32 RematchMajorSalveAmount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch", meta = (ClampMin = "0"))
	int32 RematchFullStoreAmount = 0;

	// Editor/debug override: fields the rematch team even outside a rematch battle, so a
	// tamer dropped straight into a test map can be checked without the facility menu.
	// Leave this false on anything that ships.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Rematch")
	bool bForceRematchTeam = false;

	// True when THIS battle should field RematchTeamSetup. Reads the authoritative flag off
	// UGF_CreatureManagerSubsystem and ORs in bForceRematchTeam, then requires a non-empty
	// rematch team — so the empty-team fallback is decided in exactly one place.
	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	bool ShouldUseRematchTeam() const;

	// Highest level on the rematch team — the "recommended level" for the facility list.
	// Falls back to the main team's highest when no rematch team is authored.
	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	int32 GetRematchRecommendedLevel() const;

	// The setup array actually being fielded THIS battle: RematchTeamSetup during a rematch,
	// TeamSetup otherwise. Uses the same ShouldUseRematchTeam() decision InitializeTeam made,
	// so the two can never disagree.
	//
	// Any Blueprint that reads TeamSetup to DESCRIBE the tamer must read this instead. Those
	// reads were correct only because there used to be a single array: with a rematch running
	// they narrate the main team while the tamer fields the other one — the send-out line
	// saying "Sprigling" while Thornwood walks out. Known readers of raw TeamSetup:
	// BP_BattlePlayerGAMMA (send-out notification), W_Tamer_Transition and
	// W_Tamer_Transition_GREENSCREEN (VS screen).
	//
	// Returns by value; a team is at most six small structs.
	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	TArray<FGF_TamerCreatureSetup> GetActiveTeamSetup() const;

	/**
	 * Load the species a team slot points at, and return it.
	 *
	 * Use this anywhere you used to read the Species pin directly. Dragging the soft
	 * pin into a hard-object input instead compiles clean and yields None at runtime
	 * -- see the note on FGF_TamerCreatureSetup::Species.
	 */
	// Pure so it drops straight into a data chain -- an exec pin here forces callers to
	// stage the result in a variable just to read a species name.
	UFUNCTION(BlueprintPure, Category = "Tamer|Team")
	static UGF_CreatureSpeciesData* ResolveSetupSpecies(const FGF_TamerCreatureSetup& Setup);

	// ---- Class-level queries for the facility menu ----
	// These read a Blueprint's CLASS DEFAULTS, so the menu can describe a tamer without
	// spawning one. They do need the class loaded, and a tamer class hard-references its
	// team's species — which in turn hard-reference sprites and cries. Keep the roster on
	// TSoftClassPtr and only call these for the row the player actually selected.

	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	static bool HasRematchTeam(TSubclassOf<AGF_TamerMaster> TamerClass);

	// Highest level on that class's rematch team (0 if the class is null or has none).
	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	static int32 GetRecommendedLevelForClass(TSubclassOf<AGF_TamerMaster> TamerClass);

	// One line per Creature — "Lv.42 RIVERGAUNT" — for the preview panel.
	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	static TArray<FText> GetRematchTeamPreview(TSubclassOf<AGF_TamerMaster> TamerClass);

	// The TamerName authored on the class, for the list label and for gating the row on
	// UGF_QuestSubsystem::IsTamerDefeated().
	UFUNCTION(BlueprintPure, Category = "Tamer Rematch")
	static FName GetTamerNameForClass(TSubclassOf<AGF_TamerMaster> TamerClass);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer AI")
	EGF_Decisions Decisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI")
	bool isDefeated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "1", ClampMax = "20"))
	int32 LevelDisadvantageThreshold = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerHPThresholdToSwitch = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Animations")
	TMap<EGF_TamerAnimations, UPaperFlipbook*> TamerAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Items")
	int32 FullStoreAmount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Items")
	int32 MajorSalveAmount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Items")
	int32 PotionAmount = 0;

    // This tamer's authored AI tier. The player's global Difficulty option shifts
	// it at runtime — read GetEffectiveAIDifficulty() instead of this in AI code.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI")
	EGF_TamerAIDifficulty AIDifficulty = EGF_TamerAIDifficulty::Smart;

	// AIDifficulty shifted by the player's difficulty setting (Easy makes tamers
	// dumber, Hard smarter), clamped to the ends of the enum. Falls back to the
	// authored value when the settings subsystem is unavailable.
	UFUNCTION(BlueprintPure, Category = "Tamer AI")
	EGF_TamerAIDifficulty GetEffectiveAIDifficulty() const;

	// HP percentage threshold to consider switching (0.0 - 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwitchHPThreshold = 0.25f;

	// Chance to use a status move when available (0.0 - 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StatusSkillChance = 0.3f;

	// AI throws a healing item when the current Creature's HP drops below this fraction (0.0 - 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealHPThreshold = 0.33f;

	// How strongly this tamer hoards healing items for its later Creature (its ace).
	// 0 = spends items freely the instant HP is low (easy to bait into wasting them on a weak lead).
	// 0.5 = keeps roughly half its items in reserve until it's down to its last Creature.
	// 1 = won't spend ANY item until the ace (last Creature) is out. Crank this up for Trial Leaders.
	// The current Creature is always allowed to heal once no Creature remain behind it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ItemConservation = 0.5f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI")
	bool bCanSwitch = true;

	// Minimum turns between switches — prevents the AI from switching every turn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer AI", meta = (ClampMin = "0"))
	int32 SwitchCooldownTurns = 3;

	// Tracks how many turns until the AI can switch again (decremented each turn via NotifyTurnPassed)
	UPROPERTY(BlueprintReadOnly, Category = "Tamer AI")
	int32 TurnsUntilCanSwitch = 0;

	// IGF_BattleParticipantInterface
	virtual void OnBattleTurnPassed_Implementation() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Settings")
	FGuid UniqueID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Settings")
	USoundBase* IntroTheme;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tamer Settings")
	USoundBase* BattleThemeLoop;

	// Base payout multiplier for this tamer class.
	// See tamer_payouts.md for the full table (Tuber=1, Youngster=4, unlisted=5, Trial Leader=25, Champion=50, etc.)
	// Final reward = BasePayout * HighestCreatureLevel * 4  (classic formula)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tamer Settings", meta = (ClampMin = "1"))
	int32 BasePayout = 5;

	// Returns the prize money the player receives for winning.
	// Formula: BasePayout * HighestCreatureLevel * 4  (matches the classic formula)
	UFUNCTION(BlueprintPure, Category = "Tamer Settings")
	int32 GetRewardMoney() const;

	UPROPERTY(BlueprintReadOnly, Category = "Tamer Team")
	AActor* LastSpawnedCreatureActor;






	UFUNCTION(BlueprintCallable, Category="Team Settings")
	void InitializeTeam();

	UFUNCTION(BlueprintCallable, Category="Team Settings")
	AGF_Creature* GetCurrentCreature() const;

	/**
	 * Returns the instance data of the CURRENT Creature (at CurrentCreatureIndex).
	 * Use this at battle start to get the name for "Tamer is about to send out X!"
	 * before the first Creature has been spawned.
	 * Returns false if CurrentCreatureIndex is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category="Team Settings")
	bool GetCurrentCreatureData(FGF_CreatureInstanceData& OutData) const;

	/**
	 * Returns the instance data of the next Creature that will be sent out,
	 * without actually advancing the index. Use this AFTER a down to show
	 * "Tamer is about to send out {name}!" before calling SendOutNextCreature.
	 * Returns false if no valid next Creature exists (tamer is out of Creature).
	 */
	UFUNCTION(BlueprintCallable, Category="Team Settings")
	bool GetNextCreatureData(FGF_CreatureInstanceData& OutData) const;

	UFUNCTION(BlueprintCallable, Category="Team Settings")
	bool SwitchToNextCreature();

	UFUNCTION(BlueprintCallable, Category = "Team Settings")
	bool SendOutNextCreature(FVector SpawnLocation, FRotator SpawnRotation);

	UFUNCTION(BlueprintCallable, Category="Team Settings")
	AGF_Creature* SpawnCreatureAtIndex(int32 Index, FVector SpawnLocation, FRotator SpawnRotation);

	UFUNCTION(BlueprintCallable, Category="Team Settings")
	AGF_Creature* SpawnCurrentCreature(FVector SpawnLocation, FRotator SpawnRotation);

	UFUNCTION(BlueprintCallable, Category = "Team Settings")
	AGF_Creature* SwitchToSpecificCreature(int32 NewIndex, FVector SpawnLocation, FRotator SpawnRotation);

	UFUNCTION(BlueprintPure, Category = "Team Settings")
    int32 GetRemainingCreatureCount() const;

	UFUNCTION(BlueprintPure, Category = "Team Settings")
	bool HasUsableCreature() const;

	UFUNCTION(BlueprintCallable, Category = "Team Settings")
	void SyncCurrentCreatureStatus();

	// Call this from Blueprint the moment a tamer Creature is confirmed downed
	// (before the actor is destroyed). Marks it downed in TeamInstanceData and
	// fires OnTamerCreatureDowned so the UI can play the drop animation.
	UFUNCTION(BlueprintCallable, Category = "Team Settings")
	void NotifyCreatureDowned(int32 Index);




	  // ============================================
    // AI DECISION MAKING
    // ============================================

    // Main AI decision function - call this each turn
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    FGF_TamerAIDecision DecideAction(const FGF_CreatureInstanceData& PlayerCreature);

    // Get the best move against a target
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    FGF_SkillEvaluation GetBestSkill(const FGF_CreatureInstanceData& AttackerData, const FGF_CreatureInstanceData& DefenderData);

    // Evaluate all moves and return sorted by score
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    TArray<FGF_SkillEvaluation> EvaluateAllSkills(const FGF_CreatureInstanceData& AttackerData, const FGF_CreatureInstanceData& DefenderData);

    // Check if switching would be beneficial
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    int32 FindBestSwitchTarget(const FGF_CreatureInstanceData& PlayerCreature);

    // ============================================
    // HEALING ITEM USAGE
    // ============================================

    // Picks the best in-stock potion for the given Creature. Returns None if it shouldn't heal
    // (too healthy, out of stock, OR reserving items for a later Creature — see ItemConservation).
    // OutHealAmount is set to the HP the chosen item restores.
    EGF_TamerHealItem ChooseHealingItem(const FGF_CreatureInstanceData& Current, int32& OutHealAmount) const;

    // Number of undowned Creature still waiting behind the current one (the ace is the last of these).
    // Returns 0 when the current Creature is the tamer's last — i.e. its ace is on the field.
    UFUNCTION(BlueprintPure, Category = "Tamer AI")
    int32 GetFutureCreatureCount() const;

    // Convenience all-in-one: consume + apply HP instantly + cure status, in one call.
    // Best for the non-animated path. For a tweened HP bar, DON'T use this — it sets HP
    // instantly and your bound bar will snap. Use the granular functions below instead.
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    FGF_TamerHealResult ApplyHealingItem(EGF_TamerHealItem Item);

    // --- Granular control (use these when animating the HP bar with a tween) ---

    // Calculates OldHP / NewHP / AmountHealed for the item WITHOUT consuming it or touching HP.
    // Feed OldHP -> NewHP into your tween. bSuccess is false if out of stock or no on-stage Creature.
    // bStatusCured reports whether a Full Salve *would* cure a status (so you can gate the cure FX).
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    FGF_TamerHealResult PreviewHealingItem(EGF_TamerHealItem Item) const;

    // Decrements the item's stock only — no HP, no status. Call once you've committed to the heal.
    // Returns false if that item is out of stock.
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    bool ConsumeHealingItem(EGF_TamerHealItem Item);

    // Clears major status (+ sleep counter + volatiles) on the Creature at Index, mirrors it to the
    // persistent instance data, and fires OnTamerCreatureStatusChanged. Safe whether or not that
    // Creature is currently on stage. Call this yourself to time the cure with your animation.
    UFUNCTION(BlueprintCallable, Category = "Tamer AI")
    void CureStatus(int32 Index);

    // ============================================
    // TYPE EFFECTIVENESS HELPERS
    // ============================================

    UFUNCTION(BlueprintPure, Category = "Tamer AI")
    float GetTypeEffectiveness(EGF_Element SkillElement, EGF_Element DefenderType1, EGF_Element DefenderType2);

    UFUNCTION(BlueprintPure, Category = "Tamer AI")
    bool HasTypeAdvantage(const FGF_CreatureInstanceData& Attacker, const FGF_CreatureInstanceData& Defender);


private:
    // Was a rematch ASKED for this battle, regardless of whether one can be fielded?
    // Kept separate from ShouldUseRematchTeam() so InitializeTeam can tell "no rematch"
    // apart from "rematch wanted but nothing authored" — only the second is a warning.
    bool IsRematchRequested() const;

    // Internal helpers
    float CalculateSkillScore(const FGF_CreatureInstanceData& Attacker, const FGF_CreatureInstanceData& Defender,
                             TSubclassOf<AGF_SkillDefinition> SkillClass, int32 SkillIndex);
    bool ShouldConsiderSwitching(const FGF_CreatureInstanceData& CurrentCreature, const FGF_CreatureInstanceData& PlayerCreature);
    float GetSingleTypeEffectiveness(EGF_Element AttackType, EGF_Element DefenseType);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
