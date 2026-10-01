// ItemDataManager.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_ItemData.h"
#include "GF_ResettableState.h"
#include "GF_ItemDataManager.generated.h"

/**
 * Wrapper struct for the inner TMap (Unreal doesn't support nested TMaps in UPROPERTY)
 */
USTRUCT()
struct FGF_ItemDataMap
{
    GENERATED_BODY()

    UPROPERTY()
    TMap<int32, UGF_ItemData*> ItemsByID;

    UPROPERTY()
    TMap<FName, UGF_ItemData*> ItemsByName;
};

/**
 * Manages all item data assets in the game
 * Loads them on startup and provides easy access by NAME or ID
 */
UCLASS(config=Game)
class GAMMAFRAMEWORKWORLD_API UGF_ItemDataManager : public UGameInstanceSubsystem, public IGF_ResettableState
{
    GENERATED_BODY()

public:
    /** Initialize and load all item data */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    //--------------------
    // GET BY NAME (Recommended for code)
    //--------------------

    /** Get item by name (e.g., "Core", "MajorSalve") */
    UFUNCTION(BlueprintCallable, Category = "Items")
    UGF_ItemData* GetItemByName(FName ItemName) const;

    /** Get item by name in specific category */
    UFUNCTION(BlueprintCallable, Category = "Items")
    UGF_ItemData* GetItemByNameInCategory(EGF_ItemCategory Category, FName ItemName) const;

    /** Check if item exists by name */
    UFUNCTION(BlueprintCallable, Category = "Items")
    bool ItemExistsByName(FName ItemName) const;

    //--------------------
    // GET BY ID (For save/load compatibility)
    //--------------------

    /** Get item by ID */
    UFUNCTION(BlueprintCallable, Category = "Items")
    UGF_ItemData* GetItemByID(int32 ItemID) const;

    /** Get item by ID in specific category */
    UFUNCTION(BlueprintCallable, Category = "Items")
    UGF_ItemData* GetItemByIDInCategory(EGF_ItemCategory Category, int32 ItemID) const;

    //--------------------
    // CONVERSION
    //--------------------

    /** Convert item name to ID */
    UFUNCTION(BlueprintCallable, Category = "Items")
    int32 GetItemIDFromName(FName ItemName) const;

    /** Convert item ID to name */
    UFUNCTION(BlueprintCallable, Category = "Items")
    FName GetItemNameFromID(int32 ItemID) const;

    //--------------------
    // CATEGORY QUERIES
    //--------------------

    /** Get all items in a category */
    UFUNCTION(BlueprintCallable, Category = "Items")
    TArray<UGF_ItemData*> GetItemsInCategory(EGF_ItemCategory Category) const;

    /** Get all item names in a category (useful for UI dropdowns) */
    UFUNCTION(BlueprintCallable, Category = "Items")
    TArray<FName> GetItemNamesInCategory(EGF_ItemCategory Category) const;

    //--------------------
    // DEBUG
    //--------------------

    /** Print all loaded items to log */
    UFUNCTION(BlueprintCallable, Category = "Items")
    void DebugPrintAllItems() const;

protected:
    /** All loaded item data, organized by category */
    UPROPERTY()
    TMap<EGF_ItemCategory, FGF_ItemDataMap> ItemDatabase;

    /** Global name lookup (searches all categories) */
    UPROPERTY()
    TMap<FName, UGF_ItemData*> GlobalNameLookup;

    /** Global ID lookup (searches all categories) */
    UPROPERTY()
    TMap<int32, UGF_ItemData*> GlobalIDLookup;

    /** Path to scan for items (editable in Project Settings) */
    UPROPERTY(Config, EditDefaultsOnly, Category = "Item Data")
    TArray<FString> ItemDataPaths = { "/Game/Items/" };

    /** Load items from specified paths */
    void LoadAllItems();

public:
    // IGF_ResettableState - DELIBERATE NO-OP.
    // This subsystem holds item DEFINITIONS read from /Game/Items, not player state.
    // The player's bag lives on UGF_ItemInventorySystem (reset by UGF_CreatureManagerSubsystem).
    // Clearing the database here would empty every shop and break every give-by-name
    // lookup for the rest of the session, and re-scanning costs a full registry pass.
    virtual void ResetToBootState() override {}

private:

    /** Load items from a single path */
    void LoadItemsFromPath(const FString& Path);
};