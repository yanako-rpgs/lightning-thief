// ItemInventorySystem.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GF_ItemEnums.h"
#include "GF_ItemData.h"
#include "GF_ItemInventorySystem.generated.h"

// Forward declaration
class UGF_ItemDataManager;

/**
 * Wrapper struct for TArray to work in TMap with UPROPERTY
 * Unreal's reflection system requires TMap values to be structs, not raw TArrays
 */
USTRUCT(BlueprintType)
struct FGF_ItemCategoryInventory
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TArray<FGF_ItemInstance> Items;

	FGF_ItemCategoryInventory() {}
};

/**
 * Runtime inventory system
 * NOW SUPPORTS NAME-BASED ITEM LOOKUPS!
 * Use names in code (e.g., "Core"), see display names in UI (e.g., "Pok� Core")
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKWORLD_API UGF_ItemInventorySystem : public UObject
{
	GENERATED_BODY()

public:
	UGF_ItemInventorySystem();

	//--------------------
	// INVENTORY DATA
	//--------------------

	// Items organized by category
	UPROPERTY(SaveGame)
	TMap<EGF_ItemCategory, FGF_ItemCategoryInventory> ItemsByCategory;

	/**
	 * What a new game starts with.
	 *
	 * Lives here because this is the money the game actually reads --
	 * UGF_CreatureManagerSubsystem::GetPlayerMoney() returns Inventory->Money.
	 * UGF_VaultSystem::PlayerMoney is only the SAVED copy, synced in on load and out on save,
	 * and its "= 3000" default never reaches a new game because the reset path overwrites the
	 * live value. A tester started their run with 0 because of exactly that.
	 */
	static constexpr int32 StartingMoney = 3000;

	// Player's money (max 999,999)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	int32 Money = StartingMoney;

	//--------------------
	// ITEM MANAGEMENT - BY NAME (Recommended!)
	//--------------------

	/** Add item by name (e.g., "Core", "MajorSalve") */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItemByName(EGF_ItemCategory Category, FName ItemName, int32 Quantity = 1);

	/** Remove item by name */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveItemByName(EGF_ItemCategory Category, FName ItemName, int32 Quantity = 1);

	/**
	 * Remove ALL copies of an item by name, however many the player holds.
	 * Use for key items in cutscenes where the player might have picked up more
	 * than one and you need to guarantee they end up with zero.
	 * Returns true if any were removed (false only if they had none).
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveAllByName(EGF_ItemCategory Category, FName ItemName);

	/** Check if player has item by name */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItemByName(EGF_ItemCategory Category, FName ItemName, int32 MinQuantity = 1) const;

	/** Get quantity of item by name */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetItemQuantityByName(EGF_ItemCategory Category, FName ItemName) const;

	//--------------------
	// ITEM MANAGEMENT - BY ID (For backwards compatibility)
	//--------------------

	/** Add item to inventory (auto-stacks) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItem(EGF_ItemCategory Category, int32 ItemID, int32 Quantity = 1);

	/** Remove item from inventory */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveItem(EGF_ItemCategory Category, int32 ItemID, int32 Quantity = 1);

	/** Check if player has specific item */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItem(EGF_ItemCategory Category, int32 ItemID, int32 MinQuantity = 1) const;

	/** Get quantity of specific item */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetItemQuantity(EGF_ItemCategory Category, int32 ItemID) const;

	//--------------------
	// QUERY FUNCTIONS
	//--------------------

	/** Get all items in a category */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	TArray<FGF_ItemInstance> GetItemsInCategory(EGF_ItemCategory Category) const;

	/** Get all item names in a category (for populating UI lists) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	TArray<FName> GetItemNamesInCategory(EGF_ItemCategory Category) const;

	/** Get total number of unique items across all categories */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetTotalUniqueItems() const;

	/** Clear entire inventory */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ClearInventory();

	//--------------------
	// MONEY MANAGEMENT
	//--------------------

	/** Add money (max 999,999) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddMoney(int32 Amount);

	/** Remove money */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveMoney(int32 Amount);

	/** Check if player can afford something */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool CanAfford(int32 Cost) const;

	//--------------------
	// SAVE/LOAD CONVERSION
	//--------------------

	/** Convert to save format for GF_VaultSystem. Keyed by ItemName. */
	TMap<uint8, TMap<FName, int32>> ToSaveFormat() const;

	/** Load from save format from GF_VaultSystem (without ItemDataManager) */
	void FromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData);

	/** Load from save format WITH ItemDataManager (re-populates ItemID from names) */
	void FromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData, UGF_ItemDataManager* DataManager);

	//--------------------
	// UTILITY
	//--------------------

	/** Sort items in a category by name */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SortCategory(EGF_ItemCategory Category);

	/** Get item instance by name (for detailed info) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool GetItemInstanceByName(EGF_ItemCategory Category, FName ItemName, FGF_ItemInstance& OutItem) const;

	/** Get item instance by ID (for detailed info) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool GetItemInstance(EGF_ItemCategory Category, int32 ItemID, FGF_ItemInstance& OutItem) const;

	/** Set the ItemDataManager (called by CreatureManagerSubsystem on initialization) */
	void SetItemDataManager(UGF_ItemDataManager* DataManager) { ItemDataManager = DataManager; }

private:
	/** Reference to ItemDataManager for name->ID lookups */
	UPROPERTY()
	UGF_ItemDataManager* ItemDataManager = nullptr;

	/** Ensure category exists in map */
	void EnsureCategoryExists(EGF_ItemCategory Category);

	/** Find item by name in category */
	int32 FindItemIndexByName(EGF_ItemCategory Category, FName ItemName) const;

	/** Find item by ID in category */
	int32 FindItemIndex(EGF_ItemCategory Category, int32 ItemID) const;
};