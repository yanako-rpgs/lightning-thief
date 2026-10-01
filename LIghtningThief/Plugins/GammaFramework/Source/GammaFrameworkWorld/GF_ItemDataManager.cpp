// ItemDataManager.cpp
#include "GF_ItemDataManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

void UGF_ItemDataManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("ItemDataManager: Initializing..."));

    LoadAllItems();

    UE_LOG(LogTemp, Log, TEXT("ItemDataManager: Loaded %d categories with %d total items"),
        ItemDatabase.Num(), GlobalNameLookup.Num());
}

void UGF_ItemDataManager::LoadAllItems()
{
    // Clear existing data
    ItemDatabase.Empty();
    GlobalNameLookup.Empty();
    GlobalIDLookup.Empty();

    // Load from each configured path
    for (const FString& Path : ItemDataPaths)
    {
        LoadItemsFromPath(Path);
    }
}

void UGF_ItemDataManager::LoadItemsFromPath(const FString& Path)
{
    UE_LOG(LogTemp, Log, TEXT("ItemDataManager: Loading items from %s"), *Path);

    // Get the asset registry
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // The asset registry stores package paths WITHOUT a trailing slash. A trailing
    // slash breaks recursive subpath expansion, so items in subfolders get missed.
    FString NormalizedPath = Path;
    NormalizedPath.RemoveFromEnd(TEXT("/"));

    // Make sure the registry has scanned this path (and its subfolders) before querying.
    AssetRegistry.ScanPathsSynchronous({ NormalizedPath }, /*bForceRescan*/ false);

    // Find all assets in the path, recursing into subfolders.
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*NormalizedPath));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssets(Filter, AssetDataList);

    int32 LoadedCount = 0;

    for (const FAssetData& AssetData : AssetDataList)
    {
        // Try to load the asset
        UObject* LoadedAsset = AssetData.GetAsset();
        if (!LoadedAsset)
            continue;

        // Check if it's an item data asset
        UGF_ItemData* ItemData = Cast<UGF_ItemData>(LoadedAsset);
        if (!ItemData)
            continue;

        // Validate item data
        if (ItemData->ItemName == NAME_None)
        {
            UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Item '%s' has no ItemName, skipping"),
                *ItemData->GetName());
            continue;
        }

        // Add to category database
        if (!ItemDatabase.Contains(ItemData->Category))
        {
            ItemDatabase.Add(ItemData->Category, FGF_ItemDataMap());
        }

        FGF_ItemDataMap& CategoryMap = ItemDatabase[ItemData->Category];

        // Check for duplicate names
        if (CategoryMap.ItemsByName.Contains(ItemData->ItemName))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("ItemDataManager: Duplicate item name '%s' in category %d. Overwriting."),
                *ItemData->ItemName.ToString(), (int32)ItemData->Category);
        }

        // Add by name
        CategoryMap.ItemsByName.Add(ItemData->ItemName, ItemData);
        GlobalNameLookup.Add(ItemData->ItemName, ItemData);

        // Add by ID if ItemID is set
        if (ItemData->ItemID > 0)
        {
            CategoryMap.ItemsByID.Add(ItemData->ItemID, ItemData);
            GlobalIDLookup.Add(ItemData->ItemID, ItemData);
        }

        LoadedCount++;

        UE_LOG(LogTemp, Log, TEXT("ItemDataManager: Loaded '%s' (Name: %s, ID: %d, Category: %d)"),
            *ItemData->DisplayName.ToString(), *ItemData->ItemName.ToString(),
            ItemData->ItemID, (int32)ItemData->Category);
    }

    UE_LOG(LogTemp, Log, TEXT("ItemDataManager: Loaded %d items from %s"), LoadedCount, *Path);
}

//--------------------
// GET BY NAME
//--------------------

UGF_ItemData* UGF_ItemDataManager::GetItemByName(FName ItemName) const
{
    UGF_ItemData* const* ItemData = GlobalNameLookup.Find(ItemName);
    if (!ItemData)
    {
        UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Item '%s' not found"), *ItemName.ToString());
        return nullptr;
    }

    return *ItemData;
}

UGF_ItemData* UGF_ItemDataManager::GetItemByNameInCategory(EGF_ItemCategory Category, FName ItemName) const
{
    const FGF_ItemDataMap* CategoryMap = ItemDatabase.Find(Category);
    if (!CategoryMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Category %d not found"), (int32)Category);
        return nullptr;
    }

    UGF_ItemData* const* ItemData = CategoryMap->ItemsByName.Find(ItemName);
    if (!ItemData)
    {
        UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Item '%s' not found in category %d"),
            *ItemName.ToString(), (int32)Category);
        return nullptr;
    }

    return *ItemData;
}

bool UGF_ItemDataManager::ItemExistsByName(FName ItemName) const
{
    return GlobalNameLookup.Contains(ItemName);
}

//--------------------
// GET BY ID
//--------------------

UGF_ItemData* UGF_ItemDataManager::GetItemByID(int32 ItemID) const
{
    UGF_ItemData* const* ItemData = GlobalIDLookup.Find(ItemID);
    if (!ItemData)
    {
        UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Item ID %d not found"), ItemID);
        return nullptr;
    }

    return *ItemData;
}

UGF_ItemData* UGF_ItemDataManager::GetItemByIDInCategory(EGF_ItemCategory Category, int32 ItemID) const
{
    const FGF_ItemDataMap* CategoryMap = ItemDatabase.Find(Category);
    if (!CategoryMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Category %d not found"), (int32)Category);
        return nullptr;
    }

    UGF_ItemData* const* ItemData = CategoryMap->ItemsByID.Find(ItemID);
    if (!ItemData)
    {
        UE_LOG(LogTemp, Warning, TEXT("ItemDataManager: Item ID %d not found in category %d"),
            ItemID, (int32)Category);
        return nullptr;
    }

    return *ItemData;
}

//--------------------
// CONVERSION
//--------------------

int32 UGF_ItemDataManager::GetItemIDFromName(FName ItemName) const
{
    UGF_ItemData* ItemData = GetItemByName(ItemName);
    return ItemData ? ItemData->ItemID : 0;
}

FName UGF_ItemDataManager::GetItemNameFromID(int32 ItemID) const
{
    UGF_ItemData* ItemData = GetItemByID(ItemID);
    return ItemData ? ItemData->ItemName : NAME_None;
}

//--------------------
// CATEGORY QUERIES
//--------------------

TArray<UGF_ItemData*> UGF_ItemDataManager::GetItemsInCategory(EGF_ItemCategory Category) const
{
    TArray<UGF_ItemData*> Items;

    const FGF_ItemDataMap* CategoryMap = ItemDatabase.Find(Category);
    if (CategoryMap)
    {
        CategoryMap->ItemsByName.GenerateValueArray(Items);
    }

    return Items;
}

TArray<FName> UGF_ItemDataManager::GetItemNamesInCategory(EGF_ItemCategory Category) const
{
    TArray<FName> Names;

    const FGF_ItemDataMap* CategoryMap = ItemDatabase.Find(Category);
    if (CategoryMap)
    {
        CategoryMap->ItemsByName.GenerateKeyArray(Names);
    }

    return Names;
}

//--------------------
// DEBUG
//--------------------

void UGF_ItemDataManager::DebugPrintAllItems() const
{
    UE_LOG(LogTemp, Log, TEXT("=== ALL LOADED ITEMS ==="));

    for (const auto& CategoryPair : ItemDatabase)
    {
        EGF_ItemCategory Category = CategoryPair.Key;
        const FGF_ItemDataMap& CategoryMap = CategoryPair.Value;

        UE_LOG(LogTemp, Log, TEXT("Category %d: %d items"), (int32)Category, CategoryMap.ItemsByName.Num());

        for (const auto& ItemPair : CategoryMap.ItemsByName)
        {
            UGF_ItemData* ItemData = ItemPair.Value;
            UE_LOG(LogTemp, Log, TEXT("  - %s (Name: %s, ID: %d)"),
                *ItemData->DisplayName.ToString(),
                *ItemData->ItemName.ToString(),
                ItemData->ItemID);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("========================"));
}