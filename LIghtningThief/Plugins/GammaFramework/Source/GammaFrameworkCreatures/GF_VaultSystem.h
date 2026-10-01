// GF_VaultSystem.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GF_CreatureInstanceData.h"
#include "GF_ItemData.h"
#include "GF_ItemEnums.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_CreatureBridge.h"   // EGF_PlayerGender
#include "Sanctuary/GF_SanctuaryTypes.h"

#include "GF_VaultSystem.generated.h"

// Forward declaration
class UGF_ItemDataManager;
class AGF_SkillDefinition;

// EGF_PlayerGender now lives in GF_CreatureBridge.h (World): the dialogue
// system substitutes gendered text and cannot depend on Creatures.

// EGF_PickedStarter used to hardcode one game's three starters. A framework
// cannot know those, so the save records the chosen species by name instead
// -- see PickedStarter below.

// Wrapper struct for box storage with FIXED 30 slots
USTRUCT(BlueprintType)
struct FGF_VaultPage
{
	GENERATED_BODY()

	// Fixed 30-slot array (empty slots have invalid SpeciesData)
	UPROPERTY(SaveGame)
	FGF_CreatureInstanceData Creature[30];

	FGF_VaultPage()
	{
		// Initialize all slots as empty (default constructor creates invalid Creature)
		for (int32 i = 0; i < 30; i++)
		{
			Creature[i] = FGF_CreatureInstanceData();
		}
	}
};

USTRUCT(BlueprintType)
struct FGF_OverworldCreatureSaveData
{
    GENERATED_BODY()

    // Unique ID to match this save entry to a specific overworld actor
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    int32 UniqueID = -1;

    // Species name
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    FName SpeciesName;

    // Creature level
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    int32 Level = 5;

    // Is this Creature unique
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    bool bIsUnique = false;

    // World location
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    FVector Location = FVector::ZeroVector;

    // Facing direction stored as uint8 (cast from EGF_PlayerDirection)
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    uint8 FacingDirection = 1; // Default South

    // Map this Creature belongs to
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    FString MapName;

    // Has this Creature been caught or defeated?
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Overworld")
    bool bHasBeenRemoved = false;
};

/**
 * Save game object that stores Creature boxes and party
 * This is what gets serialized to disk
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_VaultSystem : public USaveGame
{
	GENERATED_BODY()

public:
	UGF_VaultSystem();

	// Save slot name
	static const FString SaveSlotName;
	static const int32 UserIndex;

	// Constants
	static constexpr int32 MaxPartySize = 6;
	static constexpr int32 MaxVaultPageSize = 30;
	static constexpr int32 MaxVaultPages = 14;

	// Party Creature (active team)
	UPROPERTY(SaveGame)
	TArray<FGF_CreatureInstanceData> Party;

	// Storage VaultPages (now using fixed 30-slot wrapper struct)
	UPROPERTY(SaveGame)
	TArray<FGF_VaultPage> VaultPages;

	// Vault Names
	UPROPERTY(SaveGame)
	TArray<FString> VaultPageNames;

	// Player Info
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FString PlayerName;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	int32 PlayerID;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	EGF_PlayerGender PlayerGender = EGF_PlayerGender::Boy;

	/** Fix tamer info for all Creature in party and boxes (debug utility) */
	UFUNCTION(BlueprintCallable, Category = "Creature|Debug")
	void FixAllTamerInfo();

	/**
	 * Resyncs Trait against TraitSlot for every Creature in the party, the boxes
	 * and the sanctuary, and returns how many needed correcting.
	 *
	 * Repairs saves written before EvolveCreature() re-resolved traits, where an
	 * evolved Creature kept its pre-evolution trait (a Direfang stuck with Run
	 * Away instead of Menace). Runs automatically on load; safe to re-run, and
	 * a no-op once everything is consistent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Debug")
	int32 RepairAllTraits(bool bSaveIfChanged = false);

		/** When the game was saved (date and time) */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FDateTime LastSaveDateTime;

	/** Total time played in seconds */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	float TotalTimePlayed;

	/** When this save was created */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FDateTime SaveCreationDateTime;

	//--------------------
	// PLAYER LOCATION
	//--------------------

	/** Player's world location when saved */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FVector PlayerLocation;

	/** Player's rotation when saved */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FRotator PlayerRotation;

	/** Player's facing DirectionalVector when saved (mirrors the PaperZD DirectionalVector variable). */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FVector2D PlayerFacingVector = FVector2D(0.f, 1.f); // Default South

	/** Current map/level name */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FString CurrentMapName;

	/**
	 * Asset path of the UGF_RouteData the player was in when the game was saved.
	 * Stored as a soft path so the save file remains valid even if levels move.
	 * Example: "/Game/Data/Routes/DA_Route101.DA_Route101"
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FSoftObjectPath SavedRoutePath;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	TArray<int32> SavedPartyPreview;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FVector LastPokeCenterVisited;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FGF_GridCoordinate LastPokeCenterTile;

	// True once the player has healed at any Creature Center or healing site.
	// Determines whether LastPokeCenterTile is valid for whiteout respawn.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bHasVisitedPokeCenter = false;

	// Outdoor tile to drop the player on when Escape Rope / Dig is used.
	// Set when the player enters a cave/dungeon.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	FGF_GridCoordinate LastDungeonEntranceTile;

	// True while the player is inside a dungeon. Gates Escape Rope usability,
	// like the real game (rope is unusable out in the overworld).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bIsInDungeon = false;

	//--------------------
	// FOLLOWER CREATURE
	//--------------------

	/** Is a follower Creature currently out? */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bHasFollowerOut;

	/** Which party index is following (0-5, or -1 if none) */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	int32 FollowerPartyIndex;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bLastBattleLost = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bLastBattleWon = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player" )
	TArray<int32> PartyMembersThatRecentlyDowned;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bHasTownMap = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	int32 RematchWinStreak;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	int32 BestRematchWinStreak;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	int32 RematchComboStreak;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Player")
	bool bMysteriousFlowerIsActive = false;


	//--------------------
	// HM AND BADGES
	//--------------------

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasCut = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasFly = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasSurf = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasStrength = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasFlash = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasRockSmash = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasWaterfall = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "HM")
	bool bHasDive = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	int32 TotalEmblemAmount = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasStoneEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasKnuckleEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasDynamoEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasHeatEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasBalanceEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasFeatherEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasMindEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Emblems")
	bool bHasRainEmblem = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Caves")
	bool bIsInCave = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "World")
	int32 RepelStepsLeft = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "World")
	bool bWallyRaltsIsUnique = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "World")
	bool MewReadyInSlatehaven = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "World")
	bool EncounteredMewInSlatehaven = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "World")
	bool MewInSlatehavenWasUnique = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Active Battle Data")
	int32 LastSelectedSkill = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Time & Day")
	int32 CurrentHours = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Time & Day")
	int32 CurrentMinutes = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Time & Day")
	int32 CurrentSeconds = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Tamer Card")
	int32 CardID = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Oceanic Museum")
	bool bIsInOceanicMuseum = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Key Items")
	bool bIsUsingBike = false;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "KeyItems")
	bool bIsInside = false;

	// ---- Settings mirror ----
	// UGF_SettingsSubsystem owns these values and writes them here after every
	// change (and after a save is loaded). The subsystem's own "GEOptions" slot
	// is the source of truth -- editing these directly will be overwritten on the
	// next settings change, so go through the subsystem instead.

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	FString TextSpeed = "Fast";

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	FString Difficulty = "Normal";

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	int32 FrameID = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	FString WindowedModes = "Fullscreen";

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	FString Resolution = "1920x1080";

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	float MasterVolume = 1.0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	float SoundEffectsVolume = 0.75;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	float MusicVolume = 0.75;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	bool bEXPShareEnabled = true;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	bool bDOFEnabled = true;

	// Auto-spend another Repel from the bag when one runs out. Mirror only -
	// set it through UGF_SettingsSubsystem::SetAutoRepelEnabled.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Settings")
	bool bAutoRepelEnabled = true;




	//--------------------
	// SANCTUARY
	//--------------------

	// Two boarded Creature, their pending step-EXP, the egg step counter and
	// whether an egg is waiting outside. Driven by UGF_SanctuarySubsystem.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Sanctuary")
	FGF_SanctuaryData Sanctuary;

	//--------------------
	// SAVE REPAIRS
	//--------------------

	// False on every save written before the evolution trait fix, so LoadGame()
	// knows to run RepairAllTraits() once and then never again. The sweep has to
	// LoadSynchronous every stored species, which is too expensive to repeat on
	// every load — hence the flag rather than an unconditional pass.
	UPROPERTY(SaveGame)
	bool bTraitsRepaired = false;

	//--------------------
	// GAME CORNER
	//--------------------

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Game Corner")
	int32 GameCornerCoins = 100;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Game Corner")
	int32 MaxGameCornerCoins = 99999;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Game Corner")
	int32 GameCornerPayout = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Game Corner")
	int32 GameCornerBetAmount = 0;


	//--------------------
	// PARTY MANAGEMENT
	//--------------------

	// Add Creature to party (returns false if party full)
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool AddToParty(const FGF_CreatureInstanceData& CreatureData);

	// Remove Creature from party by index
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool RemoveFromParty(int32 Index);

	// Get party size
	UFUNCTION(BlueprintPure, Category = "Creature|Party")
	int32 GetPartySize() const { return Party.Num(); }

	// Check if party is full
	UFUNCTION(BlueprintPure, Category = "Creature|Party")
	bool IsPartyFull() const { return Party.Num() >= MaxPartySize; }

	// Get Creature from party
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool GetPartyCreature(int32 Index, FGF_CreatureInstanceData& OutData) const;

	// Update Creature in party (after battle damage, level up, etc)
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool UpdatePartyCreature(int32 Index, const FGF_CreatureInstanceData& UpdatedData);

	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool UpdatePartyCreatureBattleData(int32 Index, const FGF_CreatureInstanceData& BattleData);

	// Swap two Creature in party
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	bool SwapPartyCreature(int32 Index1, int32 Index2);

	// Get first non-downed Creature index
	UFUNCTION(BlueprintCallable, Category = "Creature|Party")
	int32 GetFirstHealthyCreatureIndex() const;

	// Set which starter was picked, so rival and other events are updated accordingly
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Creature|Starter Picked")
	FName PickedStarter = NAME_None;   // species the player chose; project-defined

	//--------------------
	// BOX MANAGEMENT (FIXED 30-SLOT SYSTEM)
	//--------------------

	// Initialize boxes (default = 14) - FIXED: Now initializes with 30 empty slots
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	void InitializeVaultPages(int32 NumVaultPages = 14);

	// Check if a specific slot is empty (invalid Creature)
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	bool IsSlotEmpty(int32 VaultPageIndex, int32 SlotIndex) const;

	// Get all Creature in box (returns all 30 slots, including empty ones)
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	TArray<FGF_CreatureInstanceData> GetAllCreaturesInVaultPage(int32 VaultPageIndex) const;

	// Get Creature at specific slot (returns false if slot is empty)
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool GetCreatureAtSlot(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const;

	// Swap two Creature within the same box (works even if one slot is empty)
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool SwapCreatureInVault(int32 VaultPageIndex, int32 SlotA, int32 SlotB);

	// Swap Creature between two different boxes
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool SwapCreatureBetweenVaultPages(int32 VaultPageA, int32 SlotA, int32 VaultPageB, int32 SlotB);

	// Skill Creature to specific slot (replaces whatever was there)
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool MoveCreatureToSlot(int32 SourceVaultPage, int32 SourceSlot, int32 DestVaultPage, int32 DestSlot);

	// Insert Creature into specific slot
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool InsertCreatureAtSlot(int32 VaultPageIndex, int32 SlotIndex, const FGF_CreatureInstanceData& Creature);

	// Swap a party Creature with a box Creature
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool SwapPartyWithVault(int32 PartyIndex, int32 VaultPageIndex, int32 VaultSlot);

	// OLD FUNCTIONS (Kept for backward compatibility)
	bool AddToVault(int32 VaultPageIndex, const FGF_CreatureInstanceData& CreatureData);
	bool RemoveFromVault(int32 VaultPageIndex, int32 SlotIndex);
	bool GetVaultCreature(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const;
	bool UpdateVaultCreature(int32 VaultPageIndex, int32 SlotIndex, const FGF_CreatureInstanceData& UpdatedData);

	// Skill Creature from party to box
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool MovePartyToVault(int32 PartyIndex, int32 VaultPageIndex);

	// Skill Creature from box to party
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool MoveVaultToParty(int32 VaultPageIndex, int32 SlotIndex);

	// Get box name
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	FString GetVaultPageName(int32 VaultPageIndex) const;

	// Set box name
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	void SetVaultPageName(int32 VaultPageIndex, const FString& NewName);

	// Get number of Creature in box (counts non-empty slots)
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	int32 GetVaultCreatureCount(int32 VaultPageIndex) const;

	// Get total number of Creature across all boxes
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	int32 GetTotalStoredCreature() const;

	// Find which box and slot a Creature is stored in
	UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
	bool FindCreatureInStorage(int32 CreatureID, int32& OutVaultPageIndex, int32& OutSlotIndex) const;

	// Check if we can store more Creature
	UFUNCTION(BlueprintPure, Category = "Creature|Vault")
	bool CanStoreMoreCreature() const;

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
	// SAVE/LOAD
	//--------------------

	// Save this box system to disk
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	bool SaveToDisk();

	// Load box system from disk (static)
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	static UGF_VaultSystem* LoadFromDisk(bool& bSuccess);

	// Check if save file exists
	UFUNCTION(BlueprintPure, Category = "Creature|Save")
	static bool DoesSaveExist();

	// Delete save file
	UFUNCTION(BlueprintCallable, Category = "Creature|Save")
	static bool DeleteSave();

	// Validate indices
	bool IsValidPartyIndex(int32 Index) const;
	bool IsValidVaultPageIndex(int32 VaultPageIndex) const;
	bool IsValidVaultSlot(int32 VaultPageIndex, int32 SlotIndex) const;

	//--------------------
	// COMPENDIUM
	//--------------------

	/** Mark a Creature species as caught (for Compendium tracking) */
	UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
	void MarkSpeciesAsCaught(int32 CompendiumNumber);

	/** Mark a Creature species as caught using species data */
	UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
	void MarkSpeciesAsCaughtFromData(UGF_CreatureSpeciesData* SpeciesData);

	/** Check if a Creature species has been caught */
	UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
	bool HasCaughtSpecies(int32 CompendiumNumber) const;

	/** Check if a Creature species has been caught using species data */
	UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
	bool HasCaughtSpeciesFromData(UGF_CreatureSpeciesData* SpeciesData) const;

	/** Get total number of unique species caught */
	UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
	int32 GetTotalSpeciesCaught() const { return CaughtCreature.Num(); }

	/** Get all caught Creature Compendium numbers */
	UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
	TArray<int32> GetAllCaughtSpecies() const { return CaughtCreature.Array(); }

	/** Mark a Creature species as seen (encountered in the wild or in battle) */
	UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
	void MarkSpeciesAsSeen(int32 CompendiumNumber);

	/** Check if a Creature species has been seen */
	UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
	bool HasSeenSpecies(int32 CompendiumNumber) const;

	/** Get total number of unique species seen (includes caught) */
	UFUNCTION(BlueprintPure, Category = "Creature|Compendium")
	int32 GetTotalSpeciesSeen() const { return SeenCreature.Num(); }

	/** Get all seen Creature Compendium numbers */
	UFUNCTION(BlueprintCallable, Category = "Creature|Compendium")
	TArray<int32> GetAllSeenSpecies() const { return SeenCreature.Array(); }

	// Track which Creature species have been caught (for Compendium/UI)
	UPROPERTY(SaveGame)
	TSet<int32> CaughtCreature;

	// Track which Creature species have been seen but not necessarily caught
	UPROPERTY(SaveGame)
	TSet<int32> SeenCreature;

	//--------------------
	// INVENTORY
	//--------------------

	/** Player's items organized by category */
	UPROPERTY(SaveGame)
	TArray<FGF_CategoryItemData> PlayerItems;

	/** Player's money */
	UPROPERTY(SaveGame)
	int32 PlayerMoney = 3000;

	/** Conversion functions for inventory save format. Keyed by ItemName. */
	TMap<uint8, TMap<FName, int32>> GetInventorySaveFormat() const;

	/** Restore inventory from save format (name-keyed) */
	void SetInventoryFromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData);

	/** Restore inventory from save format WITH ItemDataManager (re-populates ItemID from names) */
	void SetInventoryFromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData, UGF_ItemDataManager* DataManager);

	//--------------------
	// HELD ITEM MANAGEMENT
	//--------------------

	/** Give a held item to a Creature in party */
	UFUNCTION(BlueprintCallable, Category = "Creature|Held Item")
	bool GiveHeldItemToPartyCreature(int32 PartyIndex, FName ItemName);

	/** Take held item from a Creature in party */
	UFUNCTION(BlueprintCallable, Category = "Creature|Held Item")
	FName TakeHeldItemFromPartyCreature(int32 PartyIndex);

	/** Check if party Creature has held item */
	UFUNCTION(BlueprintPure, Category = "Creature|Held Item")
	bool PartyCreatureHasHeldItem(int32 PartyIndex) const;

	/** Get held item name from party Creature */
	UFUNCTION(BlueprintPure, Category = "Creature|Held Item")
	FName GetPartyCreatureHeldItem(int32 PartyIndex) const;

	// Save overworld creature
	UPROPERTY(SaveGame)
	TArray<FGF_OverworldCreatureSaveData> SavedOverworldCreature;

	UPROPERTY(SaveGame)
	int32 NextOverworldCreatureID = 0;

private:

};