// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_CreatureInstanceData.h"
#include "GF_VaultSystem.h"
#include "GF_Creature.h"
#include "GF_ItemInventorySystem.h"  // ADD THIS LINE
#include "GF_GridMovementComponent.h"
// FGF_CatchResult / FGF_CatchContext are used in the catching function signatures below.
// This was previously only reaching us by luck through a unity build.
#include "GF_CatchingLibrary.h"
#include "GF_ResettableState.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureManagerSubsystem.generated.h"

// Forward declaration — full include is in the .cpp
class UGF_RouteData;
class UPaperSprite;
class UPaperFlipbook;
struct FStreamableHandle;   // PreviewHandle below; full include lives in the .cpp

/** Fires with the loaded front-idle flipbook, or null if it could not be loaded. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FGF_GESpeciesPreviewLoaded, UPaperFlipbook*, Flipbook);

/**
 * Everything a Compendium list or grid needs about a species, WITHOUT loading it.
 *
 * Assembled from asset registry tags, so building all 118 of these costs a registry
 * read and no asset loads. A UGF_CreatureSpeciesData averages ~35 MB once its sprites
 * and cry come with it; this struct is a few dozen bytes.
 *
 * DisplayIcon is deliberately soft. Resolve it for visible rows only -- a
 * UPaperSprite is a few KB, versus ~35 MB for the species that owns it.
 */
USTRUCT(BlueprintType)
struct FGF_SpeciesSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Species")
	FName SpeciesName;

	UPROPERTY(BlueprintReadOnly, Category = "Species")
	int32 CompendiumNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Species")
	EGF_Element PrimaryElement = EGF_Element::None;

	UPROPERTY(BlueprintReadOnly, Category = "Species")
	EGF_Element SecondaryElement = EGF_Element::None;

	UPROPERTY(BlueprintReadOnly, Category = "Species")
	TSoftObjectPtr<UPaperSprite> DisplayIcon;

	/**
	 * The species' side-on Idle flipbook, soft.
	 *
	 * Load THIS for a Compendium preview, not the species -- it is one flipbook against
	 * the whole asset, which drags every variant set and the cry you are not showing.
	 * Use Async Load Asset so browsing never blocks the game thread.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Species")
	TSoftObjectPtr<UPaperFlipbook> BattleIdleAnimation;

	/** False for a lookup that found nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "Species")
	bool bIsValid = false;
};

/**
 * Game Instance Subsystem that manages Creature party and box system
 * This is the bridge between your blueprint battle system and the save/load system
 *
 * Usage in Blueprints:
 * - Get Subsystem → Creature Manager
 * - Call functions to manage party/boxes
 * - Spawn Creature actors from data
 */
class AGF_SimpleFollower;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnCreatureEvolved, const FGF_CreatureInstanceData&, EvolvedCreature);

// Fires when a Creature wants to learn a move but already knows 4.
// Bind to this to show the "forget a move?" UI.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnSkillLearnRequired, int32, PartyIndex, TSoftClassPtr<AGF_SkillDefinition>, NewSkill);

// Fires when a Creature successfully learns a move (both auto-learn and after forget-a-move).
// Use this to show "[Creature] learned [Skill]!" text.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnSkillLearned, int32, PartyIndex, TSoftClassPtr<AGF_SkillDefinition>, LearnedSkill);

// Fires when the entire move-learn queue is empty and all Creature have been resolved.
// Use this as the signal to continue your battle-end sequence (ReturnToPlayerAfterBattle etc).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnSkillLearnQueueEmpty);

// Fires after battle ends and the overworld transition completes, if a post-battle
// cutscene was queued. CutsceneName is the dialogue asset to play. TargetActorTag
// is the tag of the NPC that should handle it — other NPCs should ignore the event.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnPostBattleCutsceneReady, FName, CutsceneName, FName, TargetActorTag);


USTRUCT(BlueprintType)
struct FGF_NPCTradeOffer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName RequestedSpecies;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName OfferedSpecies;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 OfferedLevel = 5;

    // If true, the received Creature keeps the LEVEL and GENDER of the Creature the
    // player traded away (OfferedLevel is ignored). If false, it uses OfferedLevel
    // and rolls a random gender from the new species' ratio.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRetainPlayerLevelAndGender = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName OfferedNickname;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName HeldItem;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 OfferedTamerID = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName OfferedTamerName;
};

UCLASS(BlueprintType)
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureManagerSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//--------------------
	// BOX SYSTEM REFERENCE
	//--------------------

	// The box system (party + boxes)
	UPROPERTY(BlueprintReadOnly, Category = "Creature")
	UGF_VaultSystem* VaultSystem;

	// ADD THIS PROPERTY - The inventory system
	UPROPERTY(BlueprintReadOnly, Category = "Creature")
	UGF_ItemInventorySystem* Inventory;

	// Class to use when spawning Creature actors
	UPROPERTY(BlueprintReadWrite, Category = "Creature")
	TSubclassOf<AGF_Creature> CreatureActorClass;

	// Evolution Event
    UPROPERTY(BlueprintAssignable, Category = "Creature|Evolution")
    FGF_OnCreatureEvolved OnCreatureEvolved;

    // Fires when a Creature has 4 moves and the player must choose what to forget.
    // Bind to this in your battle/level-up UI. Call ResolveSkillLearn() when done.
    UPROPERTY(BlueprintAssignable, Category = "Creature|Skills")
    FGF_OnSkillLearnRequired OnSkillLearnRequired;

    // Fires whenever a move is successfully learned — both auto-learn (<4 moves)
    // and after the player resolves a forget-a-move choice.
    // Bind to this to show "[Creature] learned [Skill]!" text.
    UPROPERTY(BlueprintAssignable, Category = "Creature|Skills")
    FGF_OnSkillLearned OnSkillLearned;

    // Fires when all queued forget-a-move resolutions are done.
    // Bind to this to know when it's safe to continue the battle-end sequence.
    UPROPERTY(BlueprintAssignable, Category = "Creature|Skills")
    FGF_OnSkillLearnQueueEmpty OnSkillLearnQueueEmpty;

    /**
     * Call this after every level-up. Automatically learns moves if slots are free.
     * If the Creature already knows 4 moves, broadcasts OnSkillLearnRequired and pauses
     * — call ResolveSkillLearn() from your UI when the player decides.
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
    void TryLearnLevelUpSkills(int32 PartyIndex, int32 NewLevel);

    /**
     * Called by Blueprint after the player picks a move to forget.
     * ForgetIndex 0-3 = forget that move and learn the new one.
     * ForgetIndex  -1 = don't learn the move (skip).
     * Automatically continues processing any remaining queued moves.
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
    void ResolveSkillLearn(int32 PartyIndex, int32 ForgetIndex);

    /** Returns true if any Creature still needs to resolve a forget-a-move choice.
     *  Check this after your TryLearnLevelUpSkills loop — if true, wait for
     *  OnSkillLearnQueueEmpty before continuing the battle-end sequence. */
    UFUNCTION(BlueprintPure, Category = "Creature|Skills")
    bool HasPendingSkillLearn() const { return !PendingSkillToLearn.IsNull() || PendingSkillLearnQueue.Num() > 0; }

    /**
     * Did WE just broadcast OnSkillLearned? Consumes the answer.
     *
     * Call this at the top of your OnSkillLearned handler and gate the "X learned Y!" dialogue on
     * it. Returns true exactly once per real broadcast from this subsystem, then false.
     *
     * This exists because that handler demonstrably fires when nothing was learned -- backing out
     * of the forget-a-move UI triggers it with no broadcast from here at all, and the resulting
     * "Sprigling learned First Strike!" is a lie that also derails the battle-end sequence. Guarding
     * on the Creature's own moveset was tried and misfires the other way: it reported "not known"
     * one second after the move was written, silently swallowing the legitimate message.
     *
     * A token from the broadcaster cannot be wrong about who broadcast. Whatever is firing that
     * event, it is not this, and this is the only thing that can say so.
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
    bool ConsumeSkillLearnedNotification();

    /** Returns the PartyIndex of the Creature currently waiting for a forget-a-move decision.
     *  Use this instead of storing the PartyIndex from OnSkillLearnRequired — always fresh. */
    UFUNCTION(BlueprintPure, Category = "Creature|Skills")
    int32 GetPendingSkillLearnPartyIndex() const { return PendingSkillLearnPartyIndex; }

    /**
     * Call this from the HUD when it is ready for the next move-learn step.
     * Call it after:
     *   - The "X learned Y!" dialogue finishes (forget-a-move learned path)
     *   - The player clicks Don't Learn (ResolveSkillLearn -1 path)
     * Pops the next entry from the queue and fires OnSkillLearnRequired,
     * or fires OnSkillLearnQueueEmpty if the queue is empty.
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
    void ProcessNextSkillLearn();

    /**
     * Evolve a Creature to a new species
     * @param Creature - The Creature instance to evolve (modified in place)
     * @param NewSpecies - Name of the species to evolve into
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
    void EvolveCreature(UPARAM(ref) FGF_CreatureInstanceData& Creature, FName NewSpecies);

    /**
     * Check if a Creature meets evolution conditions
     * @param Creature - The Creature to check
     * @param Trigger - The evolution trigger to check for
     * @return True if Creature can evolve
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
    bool CheckForEvolution(const FGF_CreatureInstanceData& Creature, EGF_EvolutionTrigger Trigger);

    /**
     * Execute an NPC trade
     * @param PartyIndex - Index of Creature in party to trade
     * @param TradeOffer - Details of the NPC's trade offer
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Trading")
    void ExecuteTrade(int32 PartyIndex, const FGF_NPCTradeOffer& TradeOffer);

    /**
     * Check for moves learned on evolution
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
    void CheckEvolutionSkills(UPARAM(ref) FGF_CreatureInstanceData& Creature);

    /**
     * Get species data by name
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Data")
    UGF_CreatureSpeciesData* GetCreatureSpeciesData(FName SpeciesName) const;

    /**
     * Recalculate a Creature's stats based on current level, APs, EPs
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
    void RecalculateStats(UPARAM(ref) FGF_CreatureInstanceData& Creature);

    /**
     * Move EPs into (positive Delta) or back out of (negative Delta) one stat of a
     * party Creature, then recalculate and save. Allowed at any time outside battle.
     * Clamped to the Creature's unspent points and to what the stat holds.
     * @return the points actually moved, signed like Delta (0 if nothing changed).
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
    int32 AllocatePartyEP(int32 PartyIndex, EGF_CreatureStat Stat, int32 Delta);

    /** Refund every EP of a party Creature back to unspent, then recalculate and save. */
    UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
    bool ResetPartyEPs(int32 PartyIndex);

    /**
     * Give a party Creature affinity outside of battle (events, items), raising its
     * APs to match. Battles already award affinity through AwardEXPFromBattle.
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
    bool AddPartyAffinity(int32 PartyIndex, int32 Delta);


	//--------------------
// BOX CREATURE MOVE ACCESS
//--------------------

/**
 * Get moves for a Creature in a box (with Uses information)
 * @param VaultPageIndex - Which box
 * @param SlotIndex - Which slot in the box
 * @param OutSkills - Array of move classes
 * @param OutCurrentUses - Array of current Uses for each move
 * @param OutMaxUses - Array of max Uses for each move
 * @return True if Creature exists and has moves
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
bool GetBoxCreatureMovesWithPP(int32 VaultPageIndex, int32 SlotIndex,
    TArray<TSubclassOf<AGF_SkillDefinition>>& OutSkills,
    TArray<int32>& OutCurrentUses,
    TArray<int32>& OutMaxUses);



/**
 * Get just the move classes for a box Creature (no Uses)
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
TArray<TSubclassOf<AGF_SkillDefinition>> GetVaultCreatureSkills(int32 VaultPageIndex, int32 SlotIndex);


//--------------------
// TRANSFER BALL MECHANICS
//--------------------

UPROPERTY(BlueprintReadWrite, Category = "Transfer Core System")
UTexture2D* OldCore;

UPROPERTY(BlueprintReadWrite, Category = "Transfer Core System")
UPaperSprite* NewCore;

UFUNCTION(BlueprintCallable, Category = "Transfer Core System")
bool TransferCreatureToNewCore(int32 PartyIndex, FName NewCoreName, FGF_CreatureInstanceData& OutUpdatedCreature);

UFUNCTION(BlueprintCallable, Category = "Transfer Core System")
bool TransferVaultCreatureToNewCore(int32 VaultPageIndex, int32 SlotIndex, FName NewCoreName, FGF_CreatureInstanceData& OutUpdatedCreature);

UFUNCTION(BlueprintCallable, Category = "Transfer Core System")
void RemoveCore(FName Core);


//--------------------
// ENHANCED BOX MANAGEMENT (Blueprint Callable)
//--------------------

/**
 * Get all Creature in a specific box (does NOT include party Creature)
 * Use this for displaying storage box contents in UI
 *
 * @param VaultPageIndex - Which box to get Creature from (0-13)
 * @param OutCreature - Array of Creature data in this box
 * @return True if successful
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
bool GetAllCreaturesInVaultPage(int32 VaultPageIndex, TArray<FGF_CreatureInstanceData>& OutCreature) const;

/**
 * Get Creature at a specific slot in a box
 *
 * @param VaultPageIndex - Which box (0-13)
 * @param SlotIndex - Which slot in the box (0-29)
 * @param OutData - Creature data at this slot
 * @return True if Creature exists at this slot
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
bool GetCreatureAtSlot(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const;

/**
 * Check if a specific slot is empty
 *
 * @param VaultPageIndex - Which box (0-13)
 * @param SlotIndex - Which slot to check (0-29)
 * @return True if slot is empty (no Creature stored)
 */
UFUNCTION(BlueprintPure, Category = "Creature|Vault")
bool IsSlotEmpty(int32 VaultPageIndex, int32 SlotIndex) const;

/**
 * Swap two Creature within the same box
 * Example: User drags Sprigling from slot 0 to slot 5
 *
 * @param VaultPageIndex - Which box
 * @param SlotA - First slot
 * @param SlotB - Second slot
 * @return True if swap successful
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
bool SwapCreatureInVault(int32 VaultPageIndex, int32 SlotA, int32 SlotB);

/**
 * Swap Creature between two different boxes
 * Example: Swap Vault 0 Slot 5 with Vault 2 Slot 10
 *
 * @param VaultPageA - First box index
 * @param SlotA - Slot in first box
 * @param VaultPageB - Second box index
 * @param SlotB - Slot in second box
 * @return True if swap successful
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
bool SwapCreatureBetweenVaultPages(int32 VaultPageA, int32 SlotA, int32 VaultPageB, int32 SlotB);

/**
 * Swap a party Creature with a box Creature
 * Example: Swap active party member with stored Creature
 *
 * IMPORTANT: Cannot swap if it would leave party empty!
 *
 * @param PartyIndex - Which party slot (0-5)
 * @param VaultPageIndex - Which box (0-13)
 * @param VaultSlot - Which slot in box (0-29)
 * @return True if swap successful
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
bool SwapPartyWithVault(int32 PartyIndex, int32 VaultPageIndex, int32 VaultSlot);

/**
 * Get total number of Creature stored in boxes (not including party)
 * Useful for UI display
 *
 * @return Total Creature count across all boxes
 */
UFUNCTION(BlueprintPure, Category = "Creature|Vault")
int32 GetTotalStoredCreature() const;

/**
 * Check if there's room to store more Creature
 *
 * @return True if at least one box has space
 */
UFUNCTION(BlueprintPure, Category = "Creature|Vault")
bool CanStoreMoreCreature() const;

UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
void ReleaseCreature(int32 PartyIndex, int32 VaultPageIndex, int32 VaultSlot);

	//--------------------
	// PARTY MANAGEMENT (Blueprint Callable)
	//--------------------

	// Get party size
	UFUNCTION(BlueprintPure, Category = "Creature|Party")
	int32 GetPartySize() const;

	// Check if party is full
	UFUNCTION(BlueprintPure, Category = "Creature|Party")
	bool IsPartyFull() const;

	// Get Creature data from party by index
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool GetPartyCreatureData(int32 Index, FGF_CreatureInstanceData& OutData) const;

	// Update Creature data in party (call this after battle/healing)
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool UpdatePartyCreatureData(int32 Index, const FGF_CreatureInstanceData& UpdatedData);

	/** Set or clear a Creature's nickname. Pass NAME_None to remove the nickname. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool SetCreatureNickname(int32 PartyIndex, FName Nickname);

	/**
	 * Syncs only battle-relevant fields (HP, Uses, Status, EXP, Level) from the battle actor back to VaultSystem.
	 * Deliberately preserves the Skills array already in VaultSystem — use this instead of UpdatePartyCreatureData
	 * at battle end so that moves learned mid-battle (TryLearnLevelUpSkills) are not overwritten.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool SyncBattleDataToParty(int32 Index, const FGF_CreatureInstanceData& BattleData);

	// Add Creature to party
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool AddToParty(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * Give a pre-built Creature instance to the player.
	 * Tries party first; falls back to boxes if party is full.
	 * Marks save data dirty on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool GiveCreatureInstance(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * Same as GiveCreatureInstance, but tells you WHERE it ended up — so gift dialogue
	 * can say "added to your party" or "sent to Vault 2" without you having to sample
	 * IsPartyFull beforehand and infer it.
	 *
	 * @param bWentToParty  True when it landed in the party. False means a box.
	 * @param OutVaultPageIndex   Vault it went to, or -1 when it went to the party.
	 * @param OutSlotIndex  Slot within that box, or the party slot when bWentToParty.
	 * @return False only when the party AND every box are full — nothing was placed,
	 *         so don't consume the NPC's gift flag.
	 *
	 * Eggs deliberately do NOT register in the Compendium here; that happens at hatch.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool GiveCreatureInstanceTracked(const FGF_CreatureInstanceData& CreatureData,
		bool& bWentToParty, int32& OutVaultPageIndex, int32& OutSlotIndex);

	/**
	 * Helper function to give the player a Creature (properly initializes with tamer info)
	 * This is the RECOMMENDED way to give Creature to ensure tamer data is set
	 * @param SpeciesData - The species to give
	 * @param Level - Level of the Creature
	 * @param StartingSkills - Optional starting moves (empty = learn naturally)
	 * @return True if successfully added
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool GiveCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level,
	    const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills);

	/**
	 * Give the player a Creature with default level (5) and natural moves
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party", meta = (DisplayName = "Give Creature (Simple)"))
	bool GiveCreatureSimple(UGF_CreatureSpeciesData* SpeciesData);

	// Give a Creature at a specific level with natural moves — no moves array needed
	UFUNCTION(BlueprintCallable, Category = "Creature|Party", meta = (DisplayName = "Give Creature At Level"))
	bool GiveCreatureAtLevel(UGF_CreatureSpeciesData* SpeciesData, int32 Level);

	/**
	 * Give a Creature and report where it landed.
	 * @param OutVaultPageIndex - -1 if added to the party, otherwise the index of the box it was sent to
	 * @return True if added anywhere (party or box), false only if party AND all boxes are full
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party", meta = (DisplayName = "Give Creature At Level (Tracked)"))
	bool GiveCreatureAtLevelTracked(UGF_CreatureSpeciesData* SpeciesData, int32 Level, int32& OutVaultPageIndex);

	/**
	 * Give a Creature whose unique state is decided by the CALLER, not by the normal 1/4096 roll.
	 * Use this for scripted gifts that show the Creature before handing it over (starter pick,
	 * gift events) — otherwise Initialize() re-rolls and the sprite the player saw won't match
	 * what they receive, in either direction.
	 * @param bUnique - Authoritative. True = unique, false = guaranteed NOT unique (no second chance).
	 * @param OutVaultPageIndex - -1 if added to the party, otherwise the index of the box it was sent to
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Party", meta = (DisplayName = "Give Creature At Level (Unique Override)"))
	bool GiveCreatureAtLevelUnique(UGF_CreatureSpeciesData* SpeciesData, int32 Level, bool bUnique, int32& OutVaultPageIndex);

	// IGF_ResettableState - rebuilds VaultSystem empty and clears the inventory and all
	// in-flight battle/move-learn state. LoadGame() repopulates from disk afterwards.
	virtual void ResetToBootState() override;

	// Core give logic shared by all GiveCreature variants. OutVaultPageIndex: -1 = party, >=0 = box index.
	// bOverrideUnique: when true, bUniqueValue replaces the roll Initialize() made.
	bool GiveCreatureTracked(UGF_CreatureSpeciesData* SpeciesData, int32 Level,
	    const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills, int32& OutVaultPageIndex,
	    bool bOverrideUnique = false, bool bUniqueValue = false);

	// Remove Creature from party
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool RemoveFromParty(int32 Index);

	// Swap two Creature in party
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool SwapPartyCreature(int32 Index1, int32 Index2);

	// Get first healthy Creature index
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	int32 GetFirstHealthyCreatureIndex() const;

	/**
	 * Same Creature as GetFirstHealthyCreatureIndex(), but expressed as a position in a list
	 * that CONTAINS ONLY NON-EGG PARTY MEMBERS -- which is what every party UI actually
	 * builds, because an egg can never be sent out and so never gets a row.
	 *
	 * Party indices and list positions are different index spaces the moment an egg is in the
	 * party, and mixing them is a soft lock, not a cosmetic slip: feeding a party index to
	 * Get Child At walks off the end of a shorter child list, returns null, and Set Focus(null)
	 * leaves the screen with nothing focused and no input. Party (Creature, egg, Creature) has
	 * three slots but two rows, so the second Creature is party index 2 and row 1.
	 *
	 * Returns 0 rather than -1 when nothing qualifies: -1 is just as null to Get Child At, and
	 * focusing the first row is the recoverable outcome.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Party")
	int32 GetFirstHealthyCreatureListPosition() const;

		/**
	 * Create a new Creature with default moves (learns moves up to level)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Creation")
	FGF_CreatureInstanceData CreateCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level);

	/**
	 * Create a new Creature with specific starting moves (for starters, eggs, gifts)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Creation")
	FGF_CreatureInstanceData CreateCreatureWithSkills(UGF_CreatureSpeciesData* SpeciesData, int32 Level, const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills);

		/**
	 * Check if Creature should learn new moves after leveling up
	 * Call this after a Creature gains a level
	 * @return Array of moves that can be learned
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
	TArray<TSoftClassPtr<AGF_SkillDefinition>> CheckLevelUpSkills(int32 PartyIndex, int32 NewLevel);

		/**
	 * Get loaded move classes for a party Creature (converts soft refs to hard refs)
	 * Use this for UI that needs the actual move classes
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
	TArray<TSubclassOf<AGF_SkillDefinition>> GetPartyCreatureLoadedSkills(int32 PartyIndex);

		/**
	 * Get loaded move classes with Uses data for a party Creature
	 * Returns move class, current Uses, and max Uses for each move
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
	TArray<FGF_SkillEntry> GetPartyCreatureSkillsWithUses(int32 PartyIndex);

	//--------------------
	// TEACH MOVE SCREEN ("Change Skills")
	//--------------------

	/**
	 * "Available Skills" tab: every level-up move this Creature could know at its current level
	 * (LearnLevel <= Level), de-duplicated. Skills it already knows come back with
	 * bAlreadyKnown = true so the UI can grey them out and show "Known".
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills|Teach")
	TArray<FGF_SkillOption> GetAvailableLevelUpSkills(int32 PartyIndex) const;

	/**
	 * Like GetAvailableLevelUpSkills, but returns the ENTIRE level-up learnset regardless of
	 * level — including moves the Creature hasn't reached yet. Each entry carries its LearnLevel
	 * (compare to the Creature's level to grey out not-yet moves) and bAlreadyKnown. Sorted by
	 * LearnLevel ascending. Use for a full learnset / relearner-style view.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills|Teach")
	TArray<FGF_SkillOption> GetAllLevelUpSkills(int32 PartyIndex) const;

	/**
	 * "Available Tomes" tab: Tomes the player OWNS whose move this species is allowed to learn
	 * (move is in the species' LearnableTomeMoves, or the move has bUniversalTM = true).
	 * Already-known moves come back with bAlreadyKnown = true for greying out.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills|Teach")
	TArray<FGF_SkillOption> GetAvailableTomeMoves(int32 PartyIndex) const;

	/**
	 * Teach a move into a specific slot — the "chamber the move, replace the picked slot" action.
	 * Overwrites Skills[ReplaceSlotIndex] and restores that slot's Uses to the move's max.
	 * If ReplaceSlotIndex == current move count and the Creature has < 4 moves, the move is
	 * appended into the empty slot instead. Refuses (returns false) if the Creature already
	 * knows the move (no duplicates). Tomes are reusable, so nothing is removed from the bag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills|Teach")
	bool TeachSkillToPartyCreature(int32 PartyIndex, TSoftClassPtr<AGF_SkillDefinition> Skill, int32 ReplaceSlotIndex);

	/**
	 * Reorder a Creature's moves — the "Rearrange Skills" action. Swaps the two slots
	 * so players can put a favorite move in slot 1. Each move's current/max Uses travels
	 * with it. Returns false if either index is out of range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills|Teach")
	bool SwapPartyCreatureSkillSlots(int32 PartyIndex, int32 SlotA, int32 SlotB);

	/**
	 * Resolve a (soft) move class to its display stats in one call, so move entry widgets
	 * don't need to async-load the class themselves. Returns a struct with bValid = false
	 * if the class can't be resolved. Pure — safe to call per entry while building the list.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Skills|Teach")
	static FGF_SkillDisplayInfo GetSkillDisplayInfo(TSoftClassPtr<AGF_SkillDefinition> Skill);

	/**
	 * Decrease Uses for a specific move
	 * Call this when a Creature uses a move in battle
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
	bool UseSkill(int32 PartyIndex, int32 SkillIndex);


	/**
 * Give EXP to a single Creature
 * @param PartyIndex - Which Creature receives EXP
 * @param ExpAmount - Amount of EXP to give
 * @param OutLeveledUp - True if Creature leveled up
 * @param OutNewLevel - The new level if leveled up
 * @return True if successful
 */
UFUNCTION(BlueprintCallable, Category = "Creature|EXP")
bool GiveEXP(int32 PartyIndex, int32 ExpAmount, bool& OutLeveledUp, int32& OutNewLevel);

/**
 * Give EXP to entire party after defeating a Creature
 * Uses EXP Share mechanics (all Creature get EXP)
 *
 * @param DefeatedCreature - The Creature that was defeated
 * @param BattlerIndices - Array of Creature that participated in battle (get bonus EXP)
 * @param OutLevelUps - Map of PartyIndex -> NewLevel for Creature that leveled up
 * @param YieldMultiplier - Scales the yield before it is shared out. 1.0 is an
 *        ordinary KO. A claim passes UGF_CatchingLibrary::GetClaimEXPMultiplier
 *        so a hard-won Creature pays more than an easy one. Applied to the
 *        yield rather than to each share, so EXP Share splits stay in
 *        proportion and a claim cannot round away a non-battler's cut. Composes
 *        with the player's own EXP rate setting, which is applied per share.
 * @return Total EXP awarded
 */
UFUNCTION(BlueprintCallable, Category = "Creature|EXP") int32 AwardEXPFromBattle(const FGF_CreatureInstanceData& DefeatedCreature, const TArray<int32>& BattlerIndices, TMap<int32, int32>& OutLevelUps,TMap<int32, int32>& OutEXPGains, float YieldMultiplier = 1.0f);

/**
 * Calculate EXP yield from a defeated Creature
 * Based on base EXP, level, and if it was a tamer battle
 */
UFUNCTION(BlueprintPure, Category = "Creature|EXP")
int32 CalculateEXPYield(const FGF_CreatureInstanceData& DefeatedCreature, bool bIsTamerBattle = false) const;

/**
 * Get EXP needed to reach next level
 */
UFUNCTION(BlueprintPure, Category = "Creature|EXP")
int32 GetEXPToNextLevel(int32 PartyIndex) const;

/**
 * How much EXP the given level spans, from the moment it is reached to the
 * moment it rolls over.
 *
 * GetEXPToNextLevel answers "how much is still owed" for the level a party
 * member is standing on right now. A victory screen counting a bar up through
 * several level-ups needs the other question -- the full width of a level it
 * has not reached yet -- and there was no way to ask it.
 *
 * Uses the species curve belonging to PartyIndex, so Level is free to run
 * ahead of the creature's current one.
 */
UFUNCTION(BlueprintPure, Category = "Creature|EXP")
int32 GetEXPSpanForLevel(int32 PartyIndex, int32 Level) const;

/**
 * Get all moves that a Creature should learn after leveling up
 * Returns a map of PartyIndex -> Array of learnable moves
 */
UFUNCTION(BlueprintCallable, Category = "Creature|EXP")
TMap<int32, FGF_LevelUpSkills> CheckPartyLevelUpSkills(const TMap<int32, int32>& LevelUps);


/**
 * Calculate total EXP needed to reach a specific level
 * Useful for UI progress bars
 */
UFUNCTION(BlueprintPure, Category = "Creature|EXP")
int32 CalculateEXPForLevel(int32 Level, EGF_EXPCurves Curve) const;

/**
 * Get current EXP progress percent for UI (0.0 to 1.0)
 */
UFUNCTION(BlueprintPure, Category = "Creature|EXP")
float GetEXPProgressPercent(int32 PartyIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Creature|EXP")
	int32 GetTotalEXP(int32 PartyIndex) const;



	/**
	 * Teach a move to a Creature (useful for Tomes)
	 */
UFUNCTION(BlueprintCallable, Category = "Creature|Skills")
bool TeachSkill(int32 PartyIndex, TSoftClassPtr<AGF_SkillDefinition> Skill, bool bReplaceSkill = false, int32 ReplaceIndex = 0);

	//--------------------
	// ACTOR SPAWNING (Blueprint Callable)
	//--------------------

	/**
	 * Spawn a Creature actor from party data
	 * This replaces your old class-based spawning
	 * Call this from your GF_BattleComponent instead of SpawnActor
	 *
	 * @param PartyIndex - Index of Creature in party
	 * @param SpawnLocation - Where to spawn
	 * @param SpawnRotation - Rotation to spawn
	 * @param bIsPlayerCreature - Set to true for player Creature, false for enemy
	 * @return Spawned and initialized Creature actor
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Spawning")
	AGF_Creature* SpawnPartyCreature(int32 PartyIndex, FVector SpawnLocation, FRotator SpawnRotation, bool bIsPlayerCreature = true);

	/**
	 * Spawn a wild Creature actor from species data
	 * Use this for wild encounters
	 *
	 * @param SpeciesData - Species to spawn
	 * @param Level - Level of wild Creature
	 * @param SpawnLocation - Where to spawn
	 * @param SpawnRotation - Rotation to spawn
	 * @return Spawned and initialized Creature actor
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Spawning")
	AGF_Creature* SpawnWildCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level, FVector SpawnLocation, FRotator SpawnRotation);

	/**
	 * Spawn a creature from instance data, with ownership stated up front.
	 *
	 * SpawnWildCreature wraps this and hardcodes bIsPlayer=false, which is right
	 * for a wild encounter and wrong for anything else -- a party creature spawned
	 * through it comes out flagged as wild, and anything keyed off
	 * isPlayerCreature then reads the same value for both lines of a battle.
	 *
	 * Public because that is a distinction callers legitimately need to make.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature | Spawning")
	AGF_Creature* SpawnCreatureFromData(const FGF_CreatureInstanceData& Data, FVector Location, FRotator Rotation, bool bIsPlayer, bool bIsWild);

	//--------------------
	// SAVE/LOAD (Blueprint Callable)
	//--------------------

	// Save current state to disk
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool SaveGame();

	// Load from disk (called automatically on Initialize, but can call manually)
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool LoadGame();

	// Check if save exists
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	bool DoesSaveExist() const;

	// Delete save file
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool DeleteSave();

	// Create new save (use when starting new game)
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	void CreateNewSave(const FString& PlayerName);



	//--------------------
	// BATTLE MANAGEMENT (Blueprint Callable)
	//--------------------

	/**
	 * Update the manager's Creature data from an active actor
	 * Call this when a Creature downs, gains EXP, or takes damage
	 * This ensures the save data stays in sync with the actor
	 *
	 * @param CreatureActor - The actor to sync from
	 * @param PartyIndex - Index in party to update
	 * @return True if update was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	bool UpdatePartyFromActor(AGF_Creature* CreatureActor, int32 PartyIndex);

	/**
	 * Switch Creature in battle
	 * This handles the full switch flow:
	 * 1. Saves current Creature's state to manager
	 * 2. Destroys current Creature actor
	 * 3. Spawns new Creature actor from party
	 *
	 * @param CurrentCreatureActor - The Creature currently in battle (will be destroyed)
	 * @param CurrentPartyIndex - Party index of current Creature
	 * @param NewPartyIndex - Party index of Creature to switch to
	 * @param SpawnLocation - Where to spawn the new Creature
	 * @param SpawnRotation - Rotation for the new Creature
	 * @return The newly spawned Creature actor, or nullptr if switch failed
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	AGF_Creature* SwitchBattleCreature(
		AGF_Creature* CurrentCreatureActor,
		int32 CurrentPartyIndex,
		int32 NewPartyIndex,
		FVector SpawnLocation,
		FRotator SpawnRotation
	);

	/**
	 * Get list of party indices that can be switched to
	 * Excludes: current Creature, downed Creature
	 *
	 * @param CurrentPartyIndex - Index of Creature currently in battle
	 * @return Array of valid party indices for switching
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	TArray<int32> GetAvailableSwitchOptions(int32 CurrentPartyIndex) const;

	/**
	 * Check if a specific Creature can be switched to
	 *
	 * @param PartyIndex - Index of Creature to check
	 * @param CurrentPartyIndex - Index of Creature currently in battle
	 * @return True if switch is allowed
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	bool CanSwitchToCreature(int32 PartyIndex, int32 CurrentPartyIndex) const;

	/**
	 * Check if player has any healthy Creature available (not current, not downed)
	 * Useful for determining if player can continue battling
	 *
	 * @param CurrentPartyIndex - Index of Creature currently in battle (-1 if none)
	 * @return True if there are healthy Creature available to switch to
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	bool HasHealthyCreatureAvailable(int32 CurrentPartyIndex = -1) const;

	/**
	 * Get count of healthy (non-downed) Creature in party
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	int32 GetHealthyCreatureCount() const;

	/**
	 * Check if a specific party Creature is currently in battle
	 * Compares by CreatureID
	 *
	 * @param PartyIndex - Index of party Creature to check
	 * @return True if this Creature is currently active in battle
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	bool IsPartyCreatureCurrentlyActive(int32 PartyIndex) const;

	/**
	 * Get the party index of the currently active battle Creature
	 *
	 * @return Party index of current battle Creature, or -1 if none active
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	int32 GetCurrentBattleCreaturePartyIndex() const;

	/** Get the currently active battle Creature actor, or nullptr if none. */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	AGF_Creature* GetCurrentBattleCreatureActor() const { return CurrentBattleCreature; }

	/** Name of a dialogue asset to start after the next battle ends, before
	 *  the player regains input. Set before entering battle, clear after firing. */
	UPROPERTY(BlueprintReadWrite, Category = "Battle|Cutscene")
	FName PendingPostBattleCutscene = NAME_None;

	/** Actor tag of the NPC that should handle the post-battle cutscene.
	 *  NPCs bind to OnPostBattleCutsceneReady and check this against their own tag. */
	UPROPERTY(BlueprintReadWrite, Category = "Battle|Cutscene")
	FName PendingPostBattleCutsceneActorTag = NAME_None;

	/** Broadcast when the post-battle cutscene should start.
	 *  NPCs bind to this in BeginPlay — no hard references needed. */
	UPROPERTY(BlueprintAssignable, Category = "Battle|Cutscene")
	FGF_OnPostBattleCutsceneReady OnPostBattleCutsceneReady;

	/** Call this after the battle transition completes to fire the pending cutscene.
	 *  Broadcasts OnPostBattleCutsceneReady and clears the pending fields.
	 *  Returns true if a cutscene was pending (don't unlock input yet).
	 *  Returns false if nothing was pending (safe to unlock input immediately). */
	UFUNCTION(BlueprintCallable, Category = "Battle|Cutscene")
	bool FirePostBattleCutsceneIfPending();

	/**
	 * Set the current battle Creature (call this when spawning/switching)
	 *
	 * @param CreatureActor - The Creature actor now in battle
	 * @param PartyIndex - Party index of this Creature
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	void SetCurrentBattleCreature(AGF_Creature* CreatureActor, int32 PartyIndex);

	/**
	 * Clear the current battle Creature (call when battle ends or Creature downs)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	void ClearCurrentBattleCreature();

	/**
	 * Start battle mode (disables auto-save on PIE stop)
	 * Call this when battle begins
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	void StartBattle();

	/**
	 * End battle mode (re-enables auto-save and saves game)
	 * Call this when battle concludes (win/lose/run)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	void EndBattle(bool bSaveImmediately = true);

	/**
	 * Check if currently in battle mode
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	bool IsInBattle() const { return bIsInBattle; }

	//--------------------
	// REMATCH BATTLES
	//--------------------

	/**
	 * Mark the battle that is ABOUT to start as a rematch. Call this from the rematch
	 * facility immediately before StartTamerBattle.
	 *
	 * It lives here, and not on the tamer actor, because AGF_TamerMaster::InitializeTeam
	 * runs from the tamer's own Event BeginPlay — the very first node — so a flag set on
	 * the spawned actor would always arrive after the team was already built. This
	 * subsystem exists before the battle map streams in, so the tamer can just ask.
	 *
	 * EndBattle() clears it. Never clear it by hand and never mirror it onto an actor: a
	 * rematch flag that outlives its battle sends the NEXT tamer out with their rematch
	 * team, which is how bPlayerIsSwappingAfterEnemyDowned used to corrupt later battles.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	void SetRematchBattle(bool bInIsRematch);

	/** Is the battle running RIGHT NOW a rematch? False again the moment EndBattle runs. */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	bool IsRematchBattle() const { return bIsRematchBattle; }

	/**
	 * Was the battle that just finished a rematch? Use THIS for post-battle branching.
	 *
	 * IsRematchBattle() is already false by the time the return flow runs, so a branch on
	 * it placed after the End Battle node reads false and the rematch path silently never
	 * fires. This one survives until the next StartBattle(), so it cannot depend on where
	 * in the graph you happened to put the branch.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Battle")
	bool WasLastBattleRematch() const { return bLastBattleWasRematch; }


	UFUNCTION(BlueprintCallable, Category = "Player")
	void SetLostOrWonBattle(bool LostBattle);

	UFUNCTION(BlueprintCallable, Category = "Player")
	void ResetWonLostBattleBools();

	UFUNCTION(BlueprintCallable, Category = "Player")
	void AddPartyMemberToRecentlyDowned(const int32& PartyIndex);


	//--------------------
	// BOX MANAGEMENT (Blueprint Callable)
	//--------------------

	// Get number of boxes
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	int32 GetVaultPageCount() const;

	// Get Creature count in specific box
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	int32 GetVaultCreatureCount(int32 VaultPageIndex) const;

	// Get Creature from box
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool GetVaultCreatureData(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const;

	// Skill Creature from party to box
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool MovePartyToVault(int32 PartyIndex, int32 VaultPageIndex);

	// Skill Creature from box to party
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool MoveVaultToParty(int32 VaultPageIndex, int32 SlotIndex);

	//--------------------
	// HEALING (Blueprint Callable)
	//--------------------

	// Heal entire party (Creature Center)
	UFUNCTION(BlueprintCallable, Category = "Creature|Healing")
	void HealParty();

	// Heal single Creature by index
	UFUNCTION(BlueprintCallable, Category = "Creature|Healing")
	bool HealCreature(int32 PartyIndex);

	//--------------------
	// GAME CORNER
	//--------------------

	UPROPERTY(BlueprintReadWrite, Category="Game Corner")
	int32 GameCornerCoins = 100;

	UPROPERTY(BlueprintReadWrite, Category="Game Corner")
	int32 MaxGameCornerCoins = 99999;

	UPROPERTY(BlueprintReadWrite, Category="Game Corner")
	int32 GameCornerPayout = 0;

	UPROPERTY(BlueprintReadWrite, Category="Game Corner")
	int32 GameCornerBetAmount = 0;


	//--------------------
	// DEBUGGING
	//--------------------

	// Print party status to log
	UFUNCTION(BlueprintCallable, Category = "Creature|Debug")
	void DebugPrintParty() const;

	// Get player name
	UFUNCTION(BlueprintPure, Category = "Creature|Info")
	FString GetPlayerName() const;

	// Set player name
	UFUNCTION(BlueprintCallable, Category = "Creature|Info")
	void SetPlayerName(const FString& NewName);

	//--------------------
	// SAVE WITH LOCATION
	//--------------------

	/**
	 * Save game with current player location and follower status
	 * @param PlayerActor - Reference to the player pawn/character
	 * @param bHasFollower - Is a follower Creature currently out?
	 * @param FollowerIndex - Which party Creature is following (-1 if none)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool SaveGameWithLocation(AActor* PlayerActor, AGF_SimpleFollower* FollowerActor = nullptr);

	/**
	 * Writes PlayerActor's position into the save, but ONLY if it really is the
	 * overworld player standing somewhere real. Returns true if the transform was
	 * accepted; false means the previously saved position was deliberately kept.
	 *
	 * Every save path must go through this. A save that runs while a cutscene, battle
	 * or the starter scene owns the pawn would otherwise store that pawn's position -
	 * which is how confirming the starter overwrote a save made in front of the bag
	 * with (0,0,0) and respawned unique hunters in the middle of Hearthvale.
	 *
	 * Not a UFUNCTION: it takes a raw TCHAR* call-site label, and Blueprint reaches it
	 * through SaveGameWithLocation / ManualSave, which both funnel through it anyway.
	 */
	bool CommitPlayerTransformToSave(AActor* PlayerActor, const TCHAR* CallSite = TEXT("CommitPlayerTransformToSave"));

	/**
	 * Get formatted save information for display in UI
	 * @param OutSaveDate - Date in format "Jan 03, 2026"
	 * @param OutSaveTime - Time in 24-hour format "14:35"
	 * @param OutPlaytime - Formatted playtime "15h 23m"
	 * @param OutLocation - Route display name (e.g. "Route 101") or map name
	 * @param OutGameStartDate - Date the player first started this save "01/03/2026"
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	void GetSaveInfo(FString& OutSaveDate, FString& OutSaveTime, FString& OutPlaytime, FString& OutLocation, FString& OutGameStartDate) const;

	/**
	 * Get the internal route name from the save file (e.g. "Route101").
	 * Returns NAME_None if no route was saved yet.
	 * Use this to re-enter the correct RouteVolume on load, or to display the area name.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	FName GetSavedRouteName() const;

	/**
	 * Get the saved UGF_RouteData asset directly.
	 * Returns nullptr if no route was saved or the asset can't be found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	UGF_RouteData* GetSavedRouteData() const;

	/**
	 * Get total playtime in hours
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	float GetTotalPlaytimeHours() const;

	// Fallback tile used when the player has never healed at a Creature Center.
	// Set this in the subsystem CDO (e.g. to the mother's house tile).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player")
	FGF_GridCoordinate DefaultRespawnTile;

	UFUNCTION(BlueprintCallable, Category = "Player")
	void GoToLastPokeCenter(UGF_GridMovementComponent* GridMovementComponent);

	// Call this from your Creature Center / healing site when the player heals.
	// Persists to save via VaultSystem.
	UFUNCTION(BlueprintCallable, Category = "Player")
	void RegisterPokeCenterHeal(const FGF_GridCoordinate& HealTile);

	// Call when the player enters a cave/dungeon door.
	// EntranceTile = the OUTDOOR tile the Escape Rope should return the player to.
	UFUNCTION(BlueprintCallable, Category = "Player")
	void RegisterDungeonEntrance(const FGF_GridCoordinate& EntranceTile);

	// Call this when the player leaves a dungeon on foot (exit door back to overworld)
	// so the Escape Rope greys out again.
	UFUNCTION(BlueprintCallable, Category = "Player")
	void ClearDungeonState();

	// True when the player is inside a dungeon and has a valid entrance to return to.
	// Use this to enable/grey-out the Escape Rope in the bag.
	UFUNCTION(BlueprintPure, Category = "Player")
	bool CanUseEscapeRope() const;

	// Raw "are we in a cave/dungeon" flag. Unlike CanUseEscapeRope() this ignores
	// battle/cutscene state — use it for gameplay checks like blocking Fly/Teleport.
	UFUNCTION(BlueprintPure, Category = "Player")
	bool IsInDungeon() const;

	// The outdoor tile the player would return to (last registered cave entrance).
	// Only meaningful while IsInDungeon() is true.
	UFUNCTION(BlueprintPure, Category = "Player")
	FGF_GridCoordinate GetLastDungeonEntrance() const;

	// Call from the bag when the Escape Rope is used. Teleports the player to the
	// stored dungeon entrance and waits for the floor to stream in.
	// Returns false (and does nothing) if the player isn't in a dungeon.
	UFUNCTION(BlueprintCallable, Category = "Player")
	bool UseEscapeRope(UGF_GridMovementComponent* GridMovementComponent);

	// True when the field move Teleport (and later Fly) may be used right now.
	// The mirror of CanUseEscapeRope(): outdoors-only instead of dungeon-only,
	// and likewise blocked during battle and dialogue / cutscenes.
	// Use this to grey out the Teleport entry in the party menu.
	UFUNCTION(BlueprintPure, Category = "Player")
	bool CanUseTeleport() const;

	// Call from the party menu when the field move Teleport is used. Warps the player
	// to the last Creature Center they healed at — same destination as a whiteout, including
	// the DefaultRespawnTile fallback — and waits for the floor to stream in.
	// Returns false (and does nothing) if Teleport isn't usable here.
	// Deliberately does NOT spend Uses: classic field moves are free.
	UFUNCTION(BlueprintCallable, Category = "Player")
	bool UseTeleport(UGF_GridMovementComponent* GridMovementComponent);

	//--------------------
	// FOLLOWER SYSTEM INTEGRATION
	//--------------------


	/**
	 * Check if a follower should be spawned on load
	 * @param OutFollowerIndex - Which party Creature should be spawned as follower
	 * @return True if a follower should be spawned
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Follower")
	bool ShouldSpawnFollower(int32& OutFollowerIndex) const;

	/**
	 * Returns true if a real save file was loaded (false on a brand new game).
	 * Use this to gate player location/facing restore so new games don't teleport to 0,0,0.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	bool HasSaveData() const { return bSaveDataLoaded; }

	/**
	 * Get saved player location
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	FVector GetSavedPlayerLocation() const;

	/**
	 * Get saved player rotation
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	FRotator GetSavedPlayerRotation() const;

	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	void GetSavedPartyPreview(TArray<UGF_CreatureSpeciesData*>& OutSpeciesDataArray) const;

	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	TArray<int32> GetSavedPartyCompendiumNumbers() const;

	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	UGF_CreatureSpeciesData* GetCreatureSpeciesDataByCompendiumNumber(int32 CompendiumNumber) const;

	/**
	 * Returns ALL species data assets sorted by CompendiumNumber.
	 *
	 * EXPENSIVE: ~4.18 GB resident, because it loads all 118 assets and each one
	 * hard-references its full sprite set. Use GetAllSpeciesSummaries() to build a
	 * list or grid, and GetCreatureSpeciesData(Name) to load the one the player
	 * actually selected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	TArray<UGF_CreatureSpeciesData*> GetAllCreatureSpeciesData() const;

	/**
	 * Every species' name, dex number, typing and grid icon -- read from asset
	 * registry tags, loading NO species assets. This is what a Compendium list or grid
	 * should be built from.
	 *
	 * The icon is a soft ref: resolve it per visible row (a UPaperSprite is a few KB)
	 * rather than loading the species it belongs to. Sorted by CompendiumNumber.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	TArray<FGF_SpeciesSummary> GetAllSpeciesSummaries() const;

	/** One species' summary, or an empty summary if the name is unknown. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	FGF_SpeciesSummary GetSpeciesSummary(FName SpeciesName) const;

	/**
	 * The species that evolves INTO SpeciesName, or NAME_None if it is already a base form.
	 *
	 * Answered from the EvolutionTargets registry tag, so walking a whole evolution chain
	 * loads nothing. Breeding used to load all 118 species to read their Evolutions arrays.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Data")
	FName GetPreEvolutionName(FName SpeciesName) const;

	/**
	 * Load one species' front-idle flipbook at HIGH priority and call back with it.
	 *
	 * Use this for a Compendium preview instead of Blueprint's "Async Load Asset". That node
	 * loads at default priority, which this project caps at s.AsyncLoadingTimeLimit=1.85 ms
	 * per frame -- so a preview trickles in over ~3 seconds regardless of its size. A high
	 * priority request also gets s.PriorityAsyncLoadingExtraTime (15 ms/frame on top).
	 *
	 * Each call CANCELS the previous outstanding request, so scrolling a list does not queue
	 * up dozens of loads and a stale callback cannot stomp the panel with the wrong Creature.
	 * Fires immediately if the flipbook is already resident.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	void RequestSpeciesPreviewAsync(FName SpeciesName, FGF_GESpeciesPreviewLoaded OnLoaded);

	/**
	 * The species name for a Creature whose species asset may not be loaded.
	 *
	 * Reading SpeciesData->SpeciesName cannot work here: that dereferences the soft pointer, which
	 * is null until something loads it, and loading it is the very thing the caller is trying to
	 * ask for. Chicken and egg -- and in Blueprint it does not even error, it silently hands back
	 * None and the caller carries on with nothing.
	 *
	 * This resolves the name from the ASSET REGISTRY index instead, by reverse-mapping the soft
	 * object path. No asset is loaded, so it is safe to call on a Creature that just arrived over
	 * the wire from a trade and has never existed on this machine.
	 *
	 * Feed the result into RequestSpeciesPreviewAsync(). Returns NAME_None if the Creature has no
	 * species set or its asset is not in the index.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Data")
	FName GetSpeciesNameForInstance(const FGF_CreatureInstanceData& CreatureData) const;

	/** Drop any outstanding preview request. Safe to call when nothing is pending. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	void CancelSpeciesPreviewRequest();

	/** Total number of species data assets that exist in the project. */
	UFUNCTION(BlueprintPure, Category = "Creature|Data")
	int32 GetTotalCreatureSpeciesCount() const;




	//--------------------
// COMPENDIUM TRACKING (Blueprint Callable)
//--------------------

/**
 * Mark a Creature species as caught (shows Core icon in UI)
 * Call this when player catches a wild Creature or receives a gift Creature
 *
 * @param CompendiumNumber - National Dex number of the species
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
void MarkSpeciesAsCaught(int32 CompendiumNumber);

/**
 * Mark a Creature species as caught using species data
 * Convenience function for when you have the UGF_CreatureSpeciesData
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
void MarkSpeciesAsCaughtFromData(UGF_CreatureSpeciesData* SpeciesData);

/**
 * Check if player has caught this species before
 * Use this to show/hide Core icon in wild encounters and UI
 *
 * @param CompendiumNumber - National Dex number to check
 * @return True if player has caught this species before
 */
UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
bool HasCaughtSpecies(int32 CompendiumNumber) const;

/**
 * Check if player has caught this species before (using species data)
 */
UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
bool HasCaughtSpeciesFromData(UGF_CreatureSpeciesData* SpeciesData) const;

/**
 * Get total number of unique Creature species caught
 * Useful for Compendium completion percentage
 */
UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
int32 GetTotalSpeciesCaught() const;

/**
 * Get list of all caught Creature Compendium numbers
 * Useful for Compendium UI
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
TArray<int32> GetAllCaughtSpecies() const;

/**
 * Automatically mark all Creature in party as caught
 * Useful for starter Creature or initial party setup
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
void MarkAllPartyCreatureAsCaught();

/**
 * Marks every species the player currently owns (party AND all boxes) as caught,
 * and returns how many entries were missing.
 *
 * Repairs saves written while EvolveCreature() did not register the evolution in the
 * dex — a player who levelled a Sprigling to Thornwood had two blank dex slots for
 * Creature sitting in their party. Initialize() (the main-menu Continue path) and
 * LoadGame() (soft reset / whiteout reload) both run this on every load rather than
 * behind a one-shot flag, because unlike RepairAllTraits it loads NOTHING: the
 * species name comes from the registry index (GetSpeciesNameForInstance) and the dex
 * number from the summary list, so it never touches a species asset or its ~35 MB of
 * sprites. Safe and cheap to repeat, and it self-heals any future path that forgets.
 *
 * Does NOT write the save; the corrected entries ride out on the player's next save.
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
int32 BackfillCompendiumFromOwnedCreature();

/**
 * Repairs stored Creature whose species has a fixed 1 HP but whose save says otherwise,
 * and returns how many were corrected.
 *
 * Saves written before the Husk HP fix have a real HP stat baked into the party/box
 * entry, because the battle actor recomputed MaxHP from the formula and wrote it back.
 * Sending one into battle now self-corrects it, but one sitting in a box or simply never
 * sent out would keep the wrong number forever — and the summary screen reads the stored
 * value, not the formula.
 *
 * Runs on every load rather than behind a one-shot flag: like the Compendium backfill it
 * loads NO species assets (the name comes from the registry index), so it is cheap enough
 * to repeat and self-heals anything a future path gets wrong.
 *
 * Covers party, all boxes and the sanctuary. Does NOT write the save; the corrections ride
 * out on the player's next save.
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Party")
int32 RepairFixedHPCreature();

/**
 * Mark a Creature species as seen (call this when the player encounters a wild Creature
 * or when an opponent sends one out in a tamer battle)
 */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
void MarkSpeciesAsSeen(int32 CompendiumNumber);

/** Mark a species as seen using a data asset reference */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
void MarkSpeciesAsSeenFromData(UGF_CreatureSpeciesData* SpeciesData);

/** Returns true if the player has seen this species (includes caught) */
UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
bool HasSeenSpecies(int32 CompendiumNumber) const;

/** Total unique species seen */
UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
int32 GetTotalSpeciesSeen() const;

/** All seen Compendium numbers */
UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
TArray<int32> GetAllSeenSpecies() const;

/**
 * Returns the full Compendium state for a given dex number.
 * Caught > Seen > Unknown — use this to drive UI display logic.
 */
UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
EGF_CompendiumEntryState GetCompendiumEntryState(int32 CompendiumNumber) const;

	//--------------------
	// INVENTORY HELPER (if you need to give starting items)
	//--------------------

	/**
	 * Give starting items to a new player
	 * Can be called from CreateNewSave or any time you need to give starter items
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	void GiveStartingItems();

	/** Returns the player's current money (₽). */
	UFUNCTION(BlueprintPure, Category = "Creature|Inventory")
	int32 GetPlayerMoney() const;

	/** Add money to the player's wallet. Returns false if Inventory is null. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	bool AddPlayerMoney(int32 Amount);

	/** Remove money from the player's wallet. Returns false if not enough money or Inventory is null. */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	bool RemovePlayerMoney(int32 Amount);

	/**
	 * Get full ItemData from an ItemInstance
	 * Use this to access item properties (core actor class, catch rate, etc.)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	UGF_ItemData* GetItemDataFromInstance(const FGF_ItemInstance& ItemInstance) const;

	/**
	 * Returns how much HP this item would actually restore to the given Creature —
	 * capped at the HP missing so you never overheal.
	 * Handles both flat (Potion) and percentage (Revive, Max Potion) items.
	 * Returns 0 if the item is not a healing type or the Creature is already full.
	 * Use this to preview the heal amount in the bag UI before consuming the item.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Inventory")
	int32 CalculateHealAmount(UGF_ItemData* Item, const FGF_CreatureInstanceData& Creature) const;

	/**
	 * Applies a healing item to the given Creature, clamping to MaxHP.
	 * Consumes one of the item from inventory if bConsumeItem is true.
	 * Returns the actual HP restored (0 if the item couldn't be applied).
	 *
	 * NOTE: For Revive items (EGF_ItemType::Revive) this also sets CurrentHP
	 * from 0 — call this even if CurrentHP == 0 for downed Creature.
	 * For all other heal types, downed Creature are skipped (returns 0).
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	int32 ApplyHealingItem(UGF_ItemData* Item, UPARAM(Ref) FGF_CreatureInstanceData& Creature, bool bConsumeItem = true);

	/**
	 * Returns how many EP this vitamin would actually add to the given Creature.
	 * Always 0 for now: EPs are earned one per level and their total is pinned to the
	 * level, so vitamins have no job yet. Use this to preview in the bag UI
	 * ("It won't have any effect.").
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Inventory")
	int32 CalculateVitaminGain(UGF_ItemData* Item, const FGF_CreatureInstanceData& Creature) const;

	/**
	 * Applies a vitamin (Protein, Iron, Carbos, Calcium, Zinc, HP Up) to the given
	 * Creature. Currently a no-op that returns 0 -- see CalculateVitaminGain.
	 * Consumes one of the item from inventory if bConsumeItem is true and it applied.
	 * Returns the EP actually added.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	int32 ApplyVitaminItem(UGF_ItemData* Item, UPARAM(Ref) FGF_CreatureInstanceData& Creature, bool bConsumeItem = true);

	/**
	 * Party-index versions — fetch the Creature from the party, apply the item, and
	 * WRITE THE RESULT BACK via UpdatePartyCreatureData.
	 *
	 * Use these from bag/party UI instead of the struct versions above: in Blueprint,
	 * GetPartyCreatureData returns a COPY, so passing that copy into ApplyVitaminItem /
	 * ApplyHealingItem modifies data that is thrown away unless you remember to call
	 * UpdatePartyCreatureData afterwards. These wrappers do the write-back for you.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	int32 ApplyHealingItemToParty(int32 PartyIndex, UGF_ItemData* Item, bool bConsumeItem = true);

	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	int32 ApplyVitaminToParty(int32 PartyIndex, UGF_ItemData* Item, bool bConsumeItem = true);

	/**
	 * Uses a Growth Candy on a party Creature: raises it exactly one level (any partial
	 * EXP progress toward the next level is kept as surplus consumed by the level-up,
	 * matching classic — the Creature lands on the new level with 0 EXP into it).
	 * Stats/HP are updated through the same GiveEXP path battle level-ups use.
	 *
	 * Has no effect (returns 0, item NOT consumed) on downed or level-100 Creature —
	 * show "It won't have any effect." in the bag UI.
	 *
	 * On success returns the NEW level. The caller (bag UI) should then:
	 *   1. Show the "grew to level X!" dialogue.
	 *   2. Call TryLearnLevelUpSkills(PartyIndex, NewLevel) and wait on the move-learn
	 *      queue if HasPendingSkillLearn() is true.
	 *   3. Check GetEvolutionForTrigger(Creature, Level) and run the evolution sequence.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	int32 ApplyGrowthCandyToParty(int32 PartyIndex, UGF_ItemData* Item, bool bConsumeItem = true);

	/**
	 * ONE node for every Uses-restoring item — Ether, Max Ether, Elixir, Max Elixir, Leppa Berry.
	 * Restores Uses to a move on a party Creature, clamped to that move's MaxUses (Uses Ups included),
	 * and writes the result back to the party.
	 *
	 * @param PartyIndex    Which party Creature.
	 * @param SkillIndex     Which move slot (0-3). Pass -1 to restore EVERY move (Elixir / Max Elixir).
	 * @param UsesToRestore   How much Uses to add. Pass 0 or a negative number for a FULL restore
	 *                      (Max Ether / Max Elixir). Ether = 10, Leppa Berry = 10.
	 * @param Item          Optional — the item being used. Only needed if you want this node to
	 *                      consume it; leave empty (None) and nothing is removed from the bag.
	 * @param bConsumeItem  Consume one of Item from inventory on success. Ignored if Item is None.
	 *
	 * @return Total Uses actually restored. 0 means "It won't have any effect." — the move (or every
	 *         move) was already full, so nothing is changed and the item is NOT consumed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Inventory")
	int32 RestoreSkillUses(int32 PartyIndex, int32 SkillIndex, int32 UsesToRestore = 10, UGF_ItemData* Item = nullptr, bool bConsumeItem = true);


	//--------------------
	// CATCHING SYSTEM
	//--------------------

	/**
	 * Master catch function - handles complete catch attempt!
	 *
	 * IMPORTANT: Pass ItemName from ItemData, NOT the enum!
	 * Example: ItemData->ItemName gives you "Core", "WardenCore", etc.
	 *
	 * @param WildCreature - The Creature being caught
	 * @param CoreName - Item name from ItemData->ItemName
	 * @param StatusModifier - Status condition multiplier (1.0=none, 1.5=para, 2.5=sleep)
	 * @param CatchContext - Situational info for conditional balls. Fill in TurnCount
	 *        (1-based) for Quick/Timer, bIsUnderwater for Dive and bIsNightOrCave for Dusk.
	 *        bSpeciesAlreadyCaught is overwritten here from the Compendium, so ignore it.
	 * @param OutCatchResult - Detailed catch result (shake count, probability, etc.)
	 * @param OutCaughtCreature - The caught Creature data (only valid if caught)
	 * @return True if caught, False if broke free
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Catching")
	bool AttemptCatch(
		const FGF_CreatureInstanceData& WildCreature,
		FName CoreName,
		float StatusModifier,
		const FGF_CatchContext& CatchContext,
		FGF_CatchResult& OutCatchResult,
		FGF_CreatureInstanceData& OutCaughtCreature
	);

	/** Check if player has a specific core */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	bool HasCore(FName CoreName) const;

	/** Get quantity of a specific core */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	int32 GetCoreCount(FName CoreName) const;

	/** Use (consume) a core from inventory */
	UFUNCTION(BlueprintCallable, Category = "Creature|Catching")
	bool UseCore(FName CoreName);

	/** Get all cores player currently has */
	UFUNCTION(BlueprintCallable, Category = "Creature|Catching")
	TArray<FGF_ItemInstance> GetAvailableCores() const;

	/** Add caught Creature to party or box */
	UFUNCTION(BlueprintCallable, Category = "Creature|Catching")
	bool AddCaughtCreature(const FGF_CreatureInstanceData& CaughtCreature, bool bSendToVault = false);

	/** Check if there's room for a caught Creature */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	bool HasStorageSpace() const;

	/** Returns the box index (0-based) the last caught Creature was sent to.
	 *  Returns -1 if it went to the party instead of a box. */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	int32 GetLastCaughtVaultPageIndex() const { return LastCaughtVaultPageIndex; }

	/** Returns the display name of the box the last caught Creature was sent to.
	 *  Returns an empty string if it went to the party. */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	FString GetLastCaughtVaultPageName() const;

	/** Get catch probability for UI display.
	 *  Pass the same CatchContext you will pass to AttemptCatch, otherwise the number
	 *  shown will not match the roll the conditional balls actually make. */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	float GetCatchProbability(
		const FGF_CreatureInstanceData& WildCreature,
		FName CoreName,
		const FGF_CatchContext& CatchContext,
		float StatusModifier = 1.0f
	) const;


	//--------------------
	// CORE VISUAL HELPERS
	//--------------------

	/**
	 * Get the core ItemData from a Creature's caught ball
	 * Used to retrieve ball visuals (icons, flipbooks)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Visuals")
	UGF_ItemData* GetCaughtCoreData(const FGF_CreatureInstanceData& Creature) const;

	/**
	 * Get the throw flipbook for a Creature's caught ball
	 * (The spinning ball animation when thrown)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Visuals")
	UPaperFlipbook* GetCaughtCoreThrowAnimation(const FGF_CreatureInstanceData& Creature) const;

	/**
	 * Get the open flipbook for a Creature's caught ball
	 * (The ball opening when Creature returns)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Visuals")
	UPaperFlipbook* GetCaughtCoreOpenAnimation(const FGF_CreatureInstanceData& Creature) const;

	/**
	 * Get the idle sprite for a Creature's caught ball
	 * (The still ball icon for UI)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Visuals")
	UPaperSprite* GetCaughtCoreIcon(const FGF_CreatureInstanceData& Creature) const;

	//--------------------
	// BATTLE HELPERS
	//--------------------

	/** Get indices of all non-downed Creature in party */
	UFUNCTION(BlueprintCallable, Category = "Creature|Battle")
	TArray<int32> GetHealthyCreatureIndices() const;


		/**
	 * Manually save the game (player-initiated)
	 * Call this from save menu, Creature Centers, or story checkpoints
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool ManualSave();

	/**
	 * Auto-save at story checkpoints (Creature Centers, Trial victories, etc.)
	 * Use this ONLY for specific save points
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool AutoSaveAtCheckpoint(FString CheckpointName);

	/**
	 * Check if there are unsaved changes
	 * Warn player before quitting if true
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	bool HasUnsavedChanges() const;

	UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
	bool EvolvePartyCreature(int32 PartyIndex, FName NewSpecies);

	UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
	bool EvolveVaultCreature(int32 VaultPageIndex, int32 SlotIndex, FName NewSpecies);

	UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
	FName GetEvolutionForTrigger(const FGF_CreatureInstanceData& Creature, EGF_EvolutionTrigger Trigger) const;

	/**
	 * Grubling's second evolution: spawn the Husk that is left behind when Grubling
	 * becomes Skitterling.
	 *
	 * Husk is NOT generated from scratch — in classic implementations it is a copy of the
	 * Creature that evolved, so it inherits the moves, Uses, APs, EPs, nature, EXP,
	 * original tamer, met memo, affinity and shininess and only the species (and
	 * therefore trait and stats) changes. Building it with GiveCreature* instead
	 * hands the player a fresh level-5 roll whose moves come from Husk's own
	 * learnset, which is how it ended up with an empty move list.
	 *
	 * Call this AFTER EvolvePartyCreature has turned the slot into Skitterling; pass that
	 * same party index. Requires a free party slot (never falls back to a box).
	 *
	 * THIS FUNCTION CONSUMES THE BALL. If CoreItemName matches the source Creature's held
	 * item, that held item is cleared. Do not also remove one from the bag - a caller that
	 * did both charged the player twice and still left the ball on the Skitterling.
	 *
	 * It also writes the finished Husk into the party itself. Do NOT follow it with an
	 * "Update Party Creature Data" built from a Break/Make of the struct: every pin left
	 * unwired on that node writes its literal default, which silently erases the trait,
	 * moves, met memo and affinity this function just set.
	 *
	 * @param SourcePartyIndex  Party slot holding the freshly evolved Skitterling.
	 * @param CoreItemName      Core spent to make it, stamped as Husk's caught
	 *                          ball and consumed off the Skitterling if held there. Leave
	 *                          None to inherit the Skitterling's ball and charge nothing.
	 * @param OutHuskPartyIndex  Slot Husk landed in, or -1 on failure. Use this
	 *                          for the reveal animation - do not recompute it as
	 *                          GetPartySize() - 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
	bool CreateHuskFromSkitterling(int32 SourcePartyIndex, FName CoreItemName, int32& OutHuskPartyIndex);

	/**
	 * Evaluates an evolution's AdditionalCondition.
	 * RequiredLocation is only read by LocationSpecific — it's compared against the
	 * RouteSubsystem's current RouteName, so it must match a UGF_RouteData's RouteName
	 * exactly (FName compares are case-insensitive, but spelling still has to match).
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Evolution")
	bool MeetsEvolutionCondition(const FGF_CreatureInstanceData& Creature, EGF_EvolutionCondition Condition, FName RequiredLocation) const;

	//-------------------------------
	//OVERWORLD PERSISTANT DATA FOR SAVE FILE
	//-------------------------------

	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	void RegisterOverworldCreature(const int32 UniqueID, FName SpeciesName, int32 Level,bool bIsUnique, FVector Location, uint8 FacingDirection, const FString& MapName);

	/**
	 * Update an existing overworld Creature's location/direction
	 * Call this before saving if the Creature has moved
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	void UpdateOverworldCreature(const int32 UniqueID, FVector NewLocation, uint8 NewFacingDirection);

	/**
	 * Mark an overworld Creature as caught/defeated (won't respawn on load)
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	void RemoveOverworldCreature(const int32 UniqueID);

	/**
	 * Get all saved overworld Creature for the current map
	 * Call this on level load to respawn saved Creature
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	TArray<FGF_OverworldCreatureSaveData> GetSavedOverworldCreatureForMap(const FString& MapName) const;

	/**
	 * Check if a specific overworld Creature exists in save data
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Overworld")
	bool HasSavedOverworldCreature(const int32 UniqueID) const;

	/**
	 * Get a specific overworld Creature's save data
	 * @return True if found
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	bool GetOverworldCreatureData(const int32 UniqueID, FGF_OverworldCreatureSaveData& OutData) const;

	/**
	 * Clear all overworld Creature for a specific map
	 * Useful for map transitions or resetting areas
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	void ClearOverworldCreatureForMap(const FString& MapName);

	UFUNCTION(BlueprintCallable, Category = "Creature|Overworld")
	int32 GenerateOverworldCreatureID();


	/**
	 * Loads EVERY species data asset. Measured cost: ~4.18 GB resident and ~48s on
	 * a cold disk, because each asset hard-references its full sprite set.
	 *
	 * This is no longer needed to make lookups work -- GetCreatureSpeciesData and
	 * GetCreatureSpeciesDataByCompendiumNumber now resolve a single species from the
	 * asset registry index and load only that one. Do not call this from a loading
	 * screen. It remains only for the Compendium path that genuinely wants all of them,
	 * and it logs a warning when it runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	void PreloadAllSpeciesData();

	/**
	 * Load a specific set of species and hold them resident -- e.g. the 6 party +
	 * up to 6 enemy Creature at a battle transition, so no switch mid-battle hits
	 * disk. Already-loaded species are skipped. Returns how many were newly loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Data")
	int32 WarmSpecies(const TArray<FName>& SpeciesNames);

	/** Diagnostic: how many species are currently resident. Should be a handful in
	 *  normal play, not 119. Pair with GetTotalCreatureSpeciesCount, which answers
	 *  from registry metadata and loads nothing. */
	UFUNCTION(BlueprintPure, Category = "Creature|Data")
	int32 GetLoadedSpeciesCount() const { return LoadedSpeciesByName.Num(); }

private:
	/**
	 * Builds SpeciesName/CompendiumNumber -> asset path maps from the asset registry.
	 * Loads NOTHING -- it reads the AssetRegistrySearchable tags on
	 * UGF_CreatureSpeciesData. Cheap enough to be the first thing every lookup does.
	 */
	void BuildSpeciesIndex() const;

	/**
	 * Fallback for assets that predate the AssetRegistrySearchable tags (i.e. have
	 * not been through the ResavePackages pass yet). Loads untagged species one at a
	 * time until the requested one is found, folding each into the index on the way,
	 * so the worst case degrades to the old behaviour instead of failing.
	 * Pass NAME_None or INDEX_NONE for whichever key you are not searching by.
	 */
	UGF_CreatureSpeciesData* ResolveUntaggedSpecies(FName SpeciesName, int32 CompendiumNumber) const;

	/** Registers a loaded asset into every index map. */
	void RegisterSpeciesInIndex(UGF_CreatureSpeciesData* Species, const FSoftObjectPath& Path) const;

	// Track active Creature actors (for cleanup)
	UPROPERTY()
	TArray<AGF_Creature*> ActiveCreatureActors;

	// Track the currently active battle Creature
	UPROPERTY()
	AGF_Creature* CurrentBattleCreature = nullptr;

	// Track if we're currently in a battle (disables auto-save)
	bool bIsInBattle = false;

	// Live for the length of one rematch battle. See SetRematchBattle().
	bool bIsRematchBattle = false;

	// Survives EndBattle so the post-battle flow can branch on it. See WasLastBattleRematch().
	bool bLastBattleWasRematch = false;

	// Track the party index of the current battle Creature (-1 if none)
	int32 CurrentBattleCreaturePartyIndex = -1;

	// Vault index of the last caught Creature (-1 = went to party, not a box)
	int32 LastCaughtVaultPageIndex = -1;

	// ---------------------------------------------------------------------------
	// SPECIES ASSET INDEX
	//
	// The index is registry METADATA only -- 119 name/number/path triples, a few KB.
	// Species assets themselves are loaded one at a time, on demand, and only the
	// ones actually asked for end up in LoadedSpeciesByName. That is the whole
	// difference between a ~35 MB lookup and a ~4.18 GB one.
	//
	// Mutable because every lookup is const and has to be able to lazily populate.
	// ---------------------------------------------------------------------------

	mutable TMap<FName, FSoftObjectPath> SpeciesPathsByName;
	mutable TMap<int32, FSoftObjectPath> SpeciesPathsByDex;

	// Assets whose registry tags are missing (not yet resaved). Drained by
	// ResolveUntaggedSpecies as lookups force them to load.
	mutable TArray<FSoftObjectPath> UntaggedSpeciesPaths;

	// Built alongside the path maps, sorted by CompendiumNumber. Pure registry data.
	mutable TArray<FGF_SpeciesSummary> SpeciesSummaries;

	// EvolvedSpecies -> the species that evolves into it. Inverted from the
	// EvolutionTargets tag while the index is built; no assets are loaded.
	mutable TMap<FName, FName> PreEvolutionByName;

	// The one in-flight preview load. Replaced (and cancelled) on every new request.
	TSharedPtr<FStreamableHandle> PreviewHandle;

	mutable bool bSpeciesIndexBuilt = false;

	// Logged once, not once per lookup.
	mutable bool bWarnedAboutUntaggedSpecies = false;

	// Species that have actually been loaded. UPROPERTY, so these are rooted and a
	// repeat lookup never touches disk. Not mutable -- UHT rejects that on a
	// UPROPERTY -- so the const lookups const_cast to write it, same as the rest of
	// this class already does.
	UPROPERTY()
	TMap<FName, TObjectPtr<UGF_CreatureSpeciesData>> LoadedSpeciesByName;

	// Only populated by the deliberate load-everything paths
	// (PreloadAllSpeciesData / GetAllCreatureSpeciesData). Empty in normal play.
	UPROPERTY()
	TArray<UGF_CreatureSpeciesData*> CachedAllSpecies;

	bool bSpeciesCacheBuilt = false;


	// Kept here for the private helpers below it; the spawn entry point itself is
	// public further up.

	// Calculate total EXP needed to reach a specific level with a growth curve
	int32 CalculateTotalEXPForLevel(int32 Level, EGF_EXPCurves Curve) const;

	/**
     * Generate a random tamer ID for NPC Creature
     */
    int32 GenerateNPCTamerID() const;

    /**
     * Save Creature data (calls existing save system)
     */
    void SaveCreatureData();

	bool bHasUnsavedChanges = false;
	bool bSaveDataLoaded = false;  // True only when LoadFromDisk succeeded (real save exists)

    struct FGF_SkillLearnQueueEntry
    {
        int32 PartyIndex = 0;
        TSoftClassPtr<AGF_SkillDefinition> Skill;
        // True  = Creature had a free slot, move already saved — just show "X learned Y!" dialogue
        // False = Creature had 4 moves, needs forget-a-move UI before the move is saved
        bool bAlreadyLearned = false;
    };

    // Unified queue for all move-learn events (both auto-learned and forget-a-move)
    TArray<FGF_SkillLearnQueueEntry> PendingSkillLearnQueue;

    /**
     * True once OnSkillLearnQueueEmpty has been announced for the current drain.
     *
     * ProcessNextSkillLearn() used to broadcast every single time it was called on an empty
     * queue, and Blueprint calls it from several places at once: one repro fired it six times
     * inside a single frame. Listeners treat the broadcast as "the move-learn phase is over,
     * continue the battle-end sequence", so the repeats re-ran that continuation against a
     * dialogue that had already started -- the tamer-defeat line opened, a stale repeat
     * closed it 10ms later, and the caller restarted it, stranding the player.
     *
     * Re-armed when a move is queued and on every battle send-out, so a battle that queues
     * nothing at all still gets its one announcement and the sequence still advances.
     */
    bool bSkillLearnQueueEmptyAnnounced = false;

    /**
     * Party index -> the level that Creature was BEFORE this battle's EXP was applied.
     *
     * CheckPartyLevelUpSkills() only receives the NEW level, so it used to ask the learnset
     * what is learned at that one level. Gaining two levels at once therefore skipped the
     * intermediate one entirely: a Sprigling going 5 -> 7 in a single battle was asked what it
     * learns at 7, and Siphon -- learned at 6 -- was silently lost. The Creature never offered
     * the move and the player was never told.
     *
     * GiveEXP() records the starting level here so the scan can cover every level crossed.
     */
    TMap<int32, int32> LevelBeforeEXPByPartyIndex;

    /** True when this drain auto-learned a move, so a "X learned Y!" line is on its way. */
    bool bSkillLearnedThisDrain = false;

    /** Set once the deferred announcement has seen the learn dialogue actually open. */
    bool bDeferSawLearnDialogue = false;

    /** Seconds spent waiting for that dialogue, so a missing one cannot stall the battle. */
    float SkillLearnDeferElapsed = 0.0f;

    FTimerHandle SkillLearnQueueEmptyDeferHandle;

    /**
     * Announces OnSkillLearnQueueEmpty once the "X learned Y!" dialogue has opened and closed.
     *
     * Blueprint takes ~600ms to open that dialogue after OnSkillLearned fires (level-up fanfare),
     * and the queue-empty listener starts the tamer-defeat line the instant it hears the
     * broadcast. The defeat line therefore won the race by ~70ms, the learn line queued behind
     * it, and the player got "Youngster was defeated" followed by "Sprigling learned Siphon!"
     * flashing over the battle-end transition. Waiting for the dialogue rather than guessing a
     * delay keeps the order right however long Blueprint takes.
     */
    void AnnounceSkillLearnQueueEmptyWhenIdle();

    // Set immediately before each real OnSkillLearned broadcast, consumed by
    // ConsumeSkillLearnedNotification(). Proof that the notification came from here.
    bool bSkillLearnedNotificationPending = false;
    TSoftClassPtr<AGF_SkillDefinition> PendingSkillToLearn; // the move currently shown in UI
    int32 PendingSkillLearnPartyIndex = -1;
    FDateTime LastSaveTime;

	void SyncActiveCreatureToSave();






public:
    /** Mark data as having unsaved changes without triggering a save. */
    UFUNCTION(BlueprintCallable, Category = "Save")
	 void MarkDirty()
    {
        bHasUnsavedChanges = true;
        UE_LOG(LogTemp, VeryVerbose, TEXT("Unsaved changes detected"));
    }
private:

	/** Time when game session started (for playtime tracking) */
	FDateTime SessionStartTime;

	/** Update playtime in save data */
	void UpdatePlaytime();




};