// ItemInventorySystem.cpp
#include "GF_ItemInventorySystem.h"
#include "GF_ItemDataManager.h"

UGF_ItemInventorySystem::UGF_ItemInventorySystem()
{
	Money = 0;

	// Initialize all categories as empty
	for (int32 i = 0; i < (int32)EGF_ItemCategory::MAX; i++)
	{
		EGF_ItemCategory Category = (EGF_ItemCategory)i;
		ItemsByCategory.Add(Category, FGF_ItemCategoryInventory());
	}
}

//--------------------
// ITEM MANAGEMENT - BY NAME
//--------------------

bool UGF_ItemInventorySystem::AddItemByName(EGF_ItemCategory Category, FName ItemName, int32 Quantity)
{
	if (Quantity <= 0 || Category == EGF_ItemCategory::MAX || ItemName == NAME_None)
	{
		return false;
	}

	EnsureCategoryExists(Category);

	// Find existing item
	int32 Index = FindItemIndexByName(Category, ItemName);

	if (Index != -1)
	{
		// Stack with existing item (max 999 per stack)
		FGF_ItemInstance& Item = ItemsByCategory[Category].Items[Index];
		int32 NewQuantity = FMath::Min(Item.Quantity + Quantity, 999);
		int32 Added = NewQuantity - Item.Quantity;
		Item.Quantity = NewQuantity;

		UE_LOG(LogTemp, Log, TEXT("Inventory: Stacked '%s' in category %d - Now have %d (added %d)"),
			*ItemName.ToString(), (int32)Category, Item.Quantity, Added);

		return Added > 0;
	}
	else
	{
		// Add new item
		FGF_ItemInstance NewItem;
		NewItem.ItemName = ItemName;
		NewItem.Quantity = FMath::Min(Quantity, 999);

		// CRITICAL FIX: Look up ItemID from ItemDataManager for save compatibility
		if (ItemDataManager)
		{
			NewItem.ItemID = ItemDataManager->GetItemIDFromName(ItemName);

			if (NewItem.ItemID == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("Inventory: Item '%s' has no ItemID! Item may not save correctly."),
					*ItemName.ToString());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Inventory: ItemDataManager not set! Item '%s' may not save correctly."),
				*ItemName.ToString());
		}

		ItemsByCategory[Category].Items.Add(NewItem);

		UE_LOG(LogTemp, Log, TEXT("Inventory: Added new item '%s' (ID: %d) to category %d - Quantity: %d"),
			*ItemName.ToString(), NewItem.ItemID, (int32)Category, NewItem.Quantity);

		return true;
	}
}

bool UGF_ItemInventorySystem::RemoveItemByName(EGF_ItemCategory Category, FName ItemName, int32 Quantity)
{
	if (Quantity <= 0 || Category == EGF_ItemCategory::MAX || ItemName == NAME_None)
	{
		return false;
	}

	int32 Index = FindItemIndexByName(Category, ItemName);
	if (Index == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("Inventory: Cannot remove '%s' - Not found in category %d"),
			*ItemName.ToString(), (int32)Category);
		return false;
	}

	FGF_ItemInstance& Item = ItemsByCategory[Category].Items[Index];

	if (Item.Quantity < Quantity)
	{
		UE_LOG(LogTemp, Warning, TEXT("Inventory: Cannot remove %d of '%s' - Only have %d"),
			Quantity, *ItemName.ToString(), Item.Quantity);
		return false;
	}

	Item.Quantity -= Quantity;

	if (Item.Quantity <= 0)
	{
		// Remove item entirely
		ItemsByCategory[Category].Items.RemoveAt(Index);
		UE_LOG(LogTemp, Log, TEXT("Inventory: Removed '%s' from category %d completely"),
			*ItemName.ToString(), (int32)Category);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Inventory: Removed %d of '%s' - %d remaining"),
			Quantity, *ItemName.ToString(), Item.Quantity);
	}

	return true;
}

bool UGF_ItemInventorySystem::RemoveAllByName(EGF_ItemCategory Category, FName ItemName)
{
	if (Category == EGF_ItemCategory::MAX || ItemName == NAME_None)
	{
		return false;
	}

	int32 Index = FindItemIndexByName(Category, ItemName);
	if (Index == -1)
	{
		// Player already has none — nothing to do.
		return false;
	}

	const int32 Removed = ItemsByCategory[Category].Items[Index].Quantity;
	ItemsByCategory[Category].Items.RemoveAt(Index);

	UE_LOG(LogTemp, Log, TEXT("Inventory: Removed all %d of '%s' from category %d"),
		Removed, *ItemName.ToString(), (int32)Category);

	return true;
}

bool UGF_ItemInventorySystem::HasItemByName(EGF_ItemCategory Category, FName ItemName, int32 MinQuantity) const
{
	return GetItemQuantityByName(Category, ItemName) >= MinQuantity;
}

int32 UGF_ItemInventorySystem::GetItemQuantityByName(EGF_ItemCategory Category, FName ItemName) const
{
	if (Category == EGF_ItemCategory::MAX || ItemName == NAME_None)
	{
		return 0;
	}

	const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (!CategoryData)
	{
		return 0;
	}

	for (const FGF_ItemInstance& Item : CategoryData->Items)
	{
		if (Item.ItemName == ItemName)
		{
			return Item.Quantity;
		}
	}

	return 0;
}

//--------------------
// ITEM MANAGEMENT - BY ID (Backwards Compatibility)
//--------------------


void UGF_ItemInventorySystem::FromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData, UGF_ItemDataManager* DataManager)
{
    // Clear existing inventory
    for (auto& Pair : ItemsByCategory)
    {
        Pair.Value.Items.Empty();
    }

    if (!DataManager)
    {
        UE_LOG(LogTemp, Error, TEXT("Inventory: Cannot load - ItemDataManager is null!"));
        return;
    }

    // Load from save data
    for (const auto& CategoryPair : SaveData)
    {
        EGF_ItemCategory Category = (EGF_ItemCategory)CategoryPair.Key;
        EnsureCategoryExists(Category);

        for (const auto& ItemPair : CategoryPair.Value)
        {
            FName ItemName = ItemPair.Key;
            int32 Quantity = ItemPair.Value;

            // Validate the name against the item database.
            UGF_ItemData* ItemData = DataManager->GetItemByName(ItemName);
            if (ItemData)
            {
                FGF_ItemInstance Item;
                Item.ItemName = ItemName;
                Item.ItemID = ItemData->ItemID;  // re-populate for runtime ID lookups
                Item.Quantity = Quantity;

                ItemsByCategory[Category].Items.Add(Item);

                UE_LOG(LogTemp, Log, TEXT("Inventory: Restored item - Name=%s, ID=%d, Qty=%d"),
                    *Item.ItemName.ToString(), Item.ItemID, Quantity);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Inventory: Could not find ItemData for '%s'! Item skipped."), *ItemName.ToString());
            }
        }
    }

    int32 TotalItems = GetTotalUniqueItems();
    UE_LOG(LogTemp, Log, TEXT("Inventory: Loaded %d items from save"), TotalItems);
}

bool UGF_ItemInventorySystem::AddItem(EGF_ItemCategory Category, int32 ItemID, int32 Quantity)
{
	if (Quantity <= 0 || Category == EGF_ItemCategory::MAX)
	{
		return false;
	}

	EnsureCategoryExists(Category);

	// Find existing item
	int32 Index = FindItemIndex(Category, ItemID);

	if (Index != -1)
	{
		// Stack with existing item (max 999 per stack)
		FGF_ItemInstance& Item = ItemsByCategory[Category].Items[Index];
		int32 NewQuantity = FMath::Min(Item.Quantity + Quantity, 999);
		int32 Added = NewQuantity - Item.Quantity;
		Item.Quantity = NewQuantity;

		UE_LOG(LogTemp, Log, TEXT("Inventory: Stacked item %d in category %d - Now have %d (added %d)"),
			ItemID, (int32)Category, Item.Quantity, Added);

		return Added > 0;
	}
	else
	{
		// Add new item
		FGF_ItemInstance NewItem;
		NewItem.ItemID = ItemID;
		NewItem.Quantity = FMath::Min(Quantity, 999);

		// Resolve the name so the item saves correctly (saves are name-keyed).
		if (ItemDataManager)
		{
			NewItem.ItemName = ItemDataManager->GetItemNameFromID(ItemID);
			if (NewItem.ItemName == NAME_None)
			{
				UE_LOG(LogTemp, Warning, TEXT("Inventory: ItemID %d has no matching name! Item may not save correctly."), ItemID);
			}
		}

		ItemsByCategory[Category].Items.Add(NewItem);

		UE_LOG(LogTemp, Log, TEXT("Inventory: Added new item %d to category %d - Quantity: %d"),
			ItemID, (int32)Category, NewItem.Quantity);

		return true;
	}
}

bool UGF_ItemInventorySystem::RemoveItem(EGF_ItemCategory Category, int32 ItemID, int32 Quantity)
{
	if (Quantity <= 0 || Category == EGF_ItemCategory::MAX)
	{
		return false;
	}

	int32 Index = FindItemIndex(Category, ItemID);
	if (Index == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("Inventory: Cannot remove item %d - Not found in category %d"),
			ItemID, (int32)Category);
		return false;
	}

	FGF_ItemInstance& Item = ItemsByCategory[Category].Items[Index];

	if (Item.Quantity < Quantity)
	{
		UE_LOG(LogTemp, Warning, TEXT("Inventory: Cannot remove %d of item %d - Only have %d"),
			Quantity, ItemID, Item.Quantity);
		return false;
	}

	Item.Quantity -= Quantity;

	if (Item.Quantity <= 0)
	{
		// Remove item entirely
		ItemsByCategory[Category].Items.RemoveAt(Index);
		UE_LOG(LogTemp, Log, TEXT("Inventory: Removed item %d from category %d completely"),
			ItemID, (int32)Category);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Inventory: Removed %d of item %d - %d remaining"),
			Quantity, ItemID, Item.Quantity);
	}

	return true;
}

bool UGF_ItemInventorySystem::HasItem(EGF_ItemCategory Category, int32 ItemID, int32 MinQuantity) const
{
	return GetItemQuantity(Category, ItemID) >= MinQuantity;
}

int32 UGF_ItemInventorySystem::GetItemQuantity(EGF_ItemCategory Category, int32 ItemID) const
{
	if (Category == EGF_ItemCategory::MAX)
	{
		return 0;
	}

	const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (!CategoryData)
	{
		return 0;
	}

	for (const FGF_ItemInstance& Item : CategoryData->Items)
	{
		if (Item.ItemID == ItemID)
		{
			return Item.Quantity;
		}
	}

	return 0;
}

//--------------------
// QUERY FUNCTIONS
//--------------------

TArray<FGF_ItemInstance> UGF_ItemInventorySystem::GetItemsInCategory(EGF_ItemCategory Category) const
{
	if (Category == EGF_ItemCategory::MAX)
	{
		return TArray<FGF_ItemInstance>();
	}

	const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (CategoryData)
	{
		return CategoryData->Items;
	}

	return TArray<FGF_ItemInstance>();
}

TArray<FName> UGF_ItemInventorySystem::GetItemNamesInCategory(EGF_ItemCategory Category) const
{
	TArray<FName> Names;

	if (Category == EGF_ItemCategory::MAX)
	{
		return Names;
	}

	const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (CategoryData)
	{
		for (const FGF_ItemInstance& Item : CategoryData->Items)
		{
			if (Item.ItemName != NAME_None)
			{
				Names.Add(Item.ItemName);
			}
		}
	}

	return Names;
}

int32 UGF_ItemInventorySystem::GetTotalUniqueItems() const
{
	int32 Total = 0;

	for (const auto& Pair : ItemsByCategory)
	{
		Total += Pair.Value.Items.Num();
	}

	return Total;
}

void UGF_ItemInventorySystem::ClearInventory()
{
	for (auto& Pair : ItemsByCategory)
	{
		Pair.Value.Items.Empty();
	}

	Money = 0;

	UE_LOG(LogTemp, Log, TEXT("Inventory: Cleared all items and money"));
}

//--------------------
// MONEY MANAGEMENT
//--------------------

bool UGF_ItemInventorySystem::AddMoney(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}

	int32 OldMoney = Money;
	Money = FMath::Min(Money + Amount, 999999);

	int32 ActualAdded = Money - OldMoney;

	return ActualAdded > 0;
}

bool UGF_ItemInventorySystem::RemoveMoney(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}

	if (Money < Amount)
	{
		return false;
	}

	Money -= Amount;

	return true;
}

bool UGF_ItemInventorySystem::CanAfford(int32 Cost) const
{
	return Money >= Cost;
}

//--------------------
// SAVE/LOAD CONVERSION
//--------------------

TMap<uint8, TMap<FName, int32>> UGF_ItemInventorySystem::ToSaveFormat() const
{
	TMap<uint8, TMap<FName, int32>> SaveData;

	for (const auto& CategoryPair : ItemsByCategory)
	{
		TMap<FName, int32> CategoryData;

		for (const FGF_ItemInstance& Item : CategoryPair.Value.Items)
		{
			// Save by name — unique and stable, no ID bookkeeping needed.
			FName KeyName = Item.ItemName;

			// Safety net for legacy items added by ID only: recover the name.
			if (KeyName == NAME_None && ItemDataManager && Item.ItemID > 0)
			{
				KeyName = ItemDataManager->GetItemNameFromID(Item.ItemID);
			}

			if (KeyName != NAME_None)
			{
				CategoryData.Add(KeyName, Item.Quantity);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Inventory: Item with no name (ID %d) skipped on save."), Item.ItemID);
			}
		}

		if (CategoryData.Num() > 0)
		{
			SaveData.Add((uint8)CategoryPair.Key, CategoryData);
		}
	}

	return SaveData;
}

void UGF_ItemInventorySystem::FromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData)
{
	// Clear existing inventory
	for (auto& Pair : ItemsByCategory)
	{
		Pair.Value.Items.Empty();
	}

	// Load from save data
	for (const auto& CategoryPair : SaveData)
	{
		EGF_ItemCategory Category = (EGF_ItemCategory)CategoryPair.Key;
		EnsureCategoryExists(Category);

		for (const auto& ItemPair : CategoryPair.Value)
		{
			FGF_ItemInstance Item;
			Item.ItemName = ItemPair.Key;
			// Re-populate ItemID from the name if the manager is available (runtime ID lookups).
			if (ItemDataManager)
			{
				Item.ItemID = ItemDataManager->GetItemIDFromName(Item.ItemName);
			}
			Item.Quantity = ItemPair.Value;
			ItemsByCategory[Category].Items.Add(Item);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Inventory: Loaded %d items from save"), GetTotalUniqueItems());
}

//--------------------
// UTILITY
//--------------------

void UGF_ItemInventorySystem::SortCategory(EGF_ItemCategory Category)
{
	if (Category == EGF_ItemCategory::MAX)
	{
		return;
	}

	FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (CategoryData)
	{
		CategoryData->Items.Sort([](const FGF_ItemInstance& A, const FGF_ItemInstance& B)
		{
			// Sort by name if both have names
			if (A.ItemName != NAME_None && B.ItemName != NAME_None)
			{
				return A.ItemName.LexicalLess(B.ItemName);
			}
			// Otherwise sort by ID
			return A.ItemID < B.ItemID;
		});
	}
}

bool UGF_ItemInventorySystem::GetItemInstanceByName(EGF_ItemCategory Category, FName ItemName, FGF_ItemInstance& OutItem) const
{
	int32 Index = FindItemIndexByName(Category, ItemName);
	if (Index != -1)
	{
		const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
		if (CategoryData)
		{
			OutItem = CategoryData->Items[Index];
			return true;
		}
	}

	return false;
}

bool UGF_ItemInventorySystem::GetItemInstance(EGF_ItemCategory Category, int32 ItemID, FGF_ItemInstance& OutItem) const
{
	int32 Index = FindItemIndex(Category, ItemID);
	if (Index != -1)
	{
		const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
		if (CategoryData)
		{
			OutItem = CategoryData->Items[Index];
			return true;
		}
	}

	return false;
}

//--------------------
// PRIVATE HELPERS
//--------------------

void UGF_ItemInventorySystem::EnsureCategoryExists(EGF_ItemCategory Category)
{
	if (!ItemsByCategory.Contains(Category))
	{
		ItemsByCategory.Add(Category, FGF_ItemCategoryInventory());
	}
}

int32 UGF_ItemInventorySystem::FindItemIndexByName(EGF_ItemCategory Category, FName ItemName) const
{
	const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (!CategoryData)
	{
		return -1;
	}

	for (int32 i = 0; i < CategoryData->Items.Num(); i++)
	{
		if (CategoryData->Items[i].ItemName == ItemName)
		{
			return i;
		}
	}

	return -1;
}

int32 UGF_ItemInventorySystem::FindItemIndex(EGF_ItemCategory Category, int32 ItemID) const
{
	const FGF_ItemCategoryInventory* CategoryData = ItemsByCategory.Find(Category);
	if (!CategoryData)
	{
		return -1;
	}

	for (int32 i = 0; i < CategoryData->Items.Num(); i++)
	{
		if (CategoryData->Items[i].ItemID == ItemID)
		{
			return i;
		}
	}

	return -1;
}