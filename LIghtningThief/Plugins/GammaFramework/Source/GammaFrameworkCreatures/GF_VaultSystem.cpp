// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_VaultSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GF_ItemDataManager.h"
#include "GF_QuestSubsystem.h"
#include "GF_CreatureTraits.h"
#include "Berries/GF_BerryGrowthSubsystem.h"
#include "UObject/Stack.h"   // FFrame::GetScriptCallstack - names the Blueprint that made a bad party write
#include "Engine/Engine.h"   // GEngine + FWorldContext - the save has to find the GameInstance to write the quest half
#include "Engine/GameInstance.h"


const FString UGF_VaultSystem::SaveSlotName = TEXT("CreatureSaveSlot");
const int32 UGF_VaultSystem::UserIndex = 0;

namespace
{
	// Defined further down, next to RepairAllTraits. Declared here so the party write
	// guard in UpdatePartyCreature can reuse it instead of keeping a second copy of the
	// species/slot resolution rules.
	bool RepairCreatureTrait(FGF_CreatureInstanceData& Mon, EGF_CreatureTrait& OutOldTrait);
}

UGF_VaultSystem::UGF_VaultSystem()
{
	PlayerName = TEXT("Player");
	PlayerID = FMath::RandRange(0, 999999);

	// Initialize save metadata
	LastSaveDateTime = FDateTime::Now();
	SaveCreationDateTime = FDateTime::Now();
	TotalTimePlayed = 0.0f;

	// Initialize player location
	PlayerLocation = FVector::ZeroVector;
	PlayerRotation = FRotator::ZeroRotator;
	CurrentMapName = TEXT("");

	// Initialize follower
	bHasFollowerOut = false;
	FollowerPartyIndex = -1;
}

//--------------------
// PARTY MANAGEMENT
//--------------------

bool UGF_VaultSystem::AddToParty(const FGF_CreatureInstanceData& CreatureData)
{
	if (IsPartyFull())
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot add to party: Party is full!"));
		return false;
	}

	if (!CreatureData.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot add invalid Creature to party!"));
		return false;
	}
		FGF_CreatureInstanceData ModifiedData = CreatureData;


	//Party.Add(CreatureData);
	//UE_LOG(LogTemp, Log, TEXT("Added %s to party (Slot %d)"), *CreatureData.GetDisplayName().ToString(), Party.Num() - 1);
	//return true;

	// Set tamer names if they haven't been set yet
	if (ModifiedData.OriginalTamerName.IsNone())
	{
		ModifiedData.OriginalTamerName = FName(*PlayerName);
		ModifiedData.OriginalTamerID = PlayerID;
		UE_LOG(LogTemp, Log, TEXT("Set Original Tamer: %s"), *PlayerName);
	}

	if (ModifiedData.CurrentTamerName.IsNone())
	{
		ModifiedData.CurrentTamerName = FName(*PlayerName);
		ModifiedData.CurrentTamerID = PlayerID;
		UE_LOG(LogTemp, Log, TEXT("Set Current Tamer: %s"), *PlayerName);
	}

	Party.Add(ModifiedData);  // <- Changed to use ModifiedData
	UE_LOG(LogTemp, Log, TEXT("Added %s to party (Slot %d) - OT: %s"),
		*ModifiedData.GetDisplayName().ToString(),
		Party.Num() - 1,
		*ModifiedData.OriginalTamerName.ToString());
	return true;
}




bool UGF_VaultSystem::RemoveFromParty(int32 Index)
{
	if (!IsValidPartyIndex(Index))
	{
		return false;
	}

	Party.RemoveAt(Index);
	UE_LOG(LogTemp, Log, TEXT("Removed Creature from party slot %d"), Index);
	return true;
}

bool UGF_VaultSystem::GetPartyCreature(int32 Index, FGF_CreatureInstanceData& OutData) const
{
	if (!IsValidPartyIndex(Index))
	{
		return false;
	}

	OutData = Party[Index];
	OutData.NormalizeUses(); // align the parallel Uses arrays; does NOT restore spent Uses

	// Eggs always read back as downed so no battle check ever picks one. Doing it here
	// rather than only at creation also fixes eggs already sitting in existing saves —
	// Party is not Blueprint-readable, so every caller comes through this accessor.
	OutData.NormalizeEggState();

	return true;
}

bool UGF_VaultSystem::UpdatePartyCreature(int32 Index, const FGF_CreatureInstanceData& UpdatedData)
{
	if (!IsValidPartyIndex(Index))
	{
		return false;
	}

	// Identity guard. This entry point replaces the WHOLE slot, so a caller that passes the
	// wrong index does not just corrupt stats — it permanently deletes whoever was in that
	// slot. The classic case: a battle sends out the first HEALTHY Creature and then writes it
	// back using its position in the healthy list (0) instead of its real party index, wiping
	// the downed Creature in slot 0.
	//
	// Every legitimate caller here is a read-modify-write of one slot (heal, teach move, use
	// move, nickname, save actor state), so the incoming CreatureID always matches the slot it
	// is aimed at. Trades, deposits and swaps write Party[] directly and never reach this.
	// When the IDs disagree, redirect to the slot that actually holds this Creature rather
	// than destroying a party member.
	if (UpdatedData.CreatureID != 0
		&& Party[Index].CreatureID != 0
		&& Party[Index].CreatureID != UpdatedData.CreatureID)
	{
		const int32 RealIndex = Party.IndexOfByPredicate(
			[&UpdatedData](const FGF_CreatureInstanceData& Mon) { return Mon.CreatureID == UpdatedData.CreatureID; });

		UE_LOG(LogTemp, Error,
			TEXT("UpdatePartyCreature: slot %d holds %s (ID %d) but was handed %s (ID %d) - wrong index from the caller. %s"),
			Index,
			*Party[Index].GetDisplayName().ToString(),
			Party[Index].CreatureID,
			*UpdatedData.GetDisplayName().ToString(),
			UpdatedData.CreatureID,
			RealIndex != INDEX_NONE
				? *FString::Printf(TEXT("Redirecting to slot %d."), RealIndex)
				: TEXT("That Creature is not in the party - write REJECTED."));

#if DO_BLUEPRINT_GUARD
		// Names the Blueprint function and node that passed the bad index.
		UE_LOG(LogTemp, Error, TEXT("  Blueprint callstack:\n%s"), *FFrame::GetScriptCallstack());
#endif

		if (RealIndex == INDEX_NONE)
		{
			return false;
		}

		Index = RealIndex;
	}

	// Never let a write BLANK the trait.
	//
	// This entry point replaces the whole slot, and several of its callers are Blueprints that
	// rebuild the struct pin by pin. Any "Trait" pin left unwired on such a node sends
	// EGF_CreatureTrait::None, and that used to be stored verbatim — so a Creature that had
	// Menace a moment ago came back blank and the summary screen printed "None". It reads
	// as intermittent because it depends on which screen last wrote the slot: the battle actor
	// and GetInstanceTrait() both re-resolve from the species, so the same Creature looks
	// correct in one place and blank in another. (CheckForSkitterlingCore in
	// BP_GF_EvolveCreature is one confirmed node that does this.)
	//
	// Only pays the extra struct copy when the incoming trait is actually blank, so the
	// ordinary heal / use-move / nickname write is unaffected. RepairCreatureTrait re-derives
	// it from SpeciesData + TraitSlot, which is what GetInstanceTrait() does at read time —
	// this just makes the stored data agree so every reader sees it, not only the careful ones.
	if (UpdatedData.Trait == EGF_CreatureTrait::None)
	{
		FGF_CreatureInstanceData Repaired = UpdatedData;

		EGF_CreatureTrait OldTrait = EGF_CreatureTrait::None;
		RepairCreatureTrait(Repaired, OldTrait);

		if (Repaired.Trait != EGF_CreatureTrait::None)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("UpdatePartyCreature: slot %d was handed a BLANK trait for %s - resolved to %s from the species. "
				     "Some caller is writing this slot from a rebuilt struct with the Trait pin unwired."),
				Index,
				*Repaired.GetDisplayName().ToString(),
				*UGF_CreatureTraitLibrary::GetTraitDisplayName(Repaired.Trait).ToString());
		}

		Party[Index] = Repaired;
		return true;
	}

	Party[Index] = UpdatedData;
	return true;
}

bool UGF_VaultSystem::UpdatePartyCreatureBattleData(int32 Index, const FGF_CreatureInstanceData& BattleData)
{
	if (!IsValidPartyIndex(Index))
	{
		return false;
	}

	// Identity guard -- the same one UpdatePartyCreature() carries, and for the same reason.
	//
	// This path merges only the battle-volatile fields, so a wrong index does not visibly
	// replace anyone: the victim keeps its own name, level and moves and simply inherits the
	// on-stage Creature's HP and MaxHP. That is exactly how a level 100 Wisp came back reading
	// 311/314 -- Sprigling's HP -- while still looking like a level 100 Wisp, and why a party
	// member sitting on the sidelines dropped to the HP of whoever just drank a potion.
	//
	// The index is caller-supplied and the callers do get it wrong: SwitchCreature() takes
	// CurrentPartyIndex as a Blueprint parameter while CurrentCreatureActor is a member, so
	// the two can drift apart. CreatureID identifies the individual, so trust it over the
	// index and write where the data actually belongs.
	if (BattleData.CreatureID != 0
		&& Party[Index].CreatureID != 0
		&& Party[Index].CreatureID != BattleData.CreatureID)
	{
		const int32 RealIndex = Party.IndexOfByPredicate(
			[&BattleData](const FGF_CreatureInstanceData& Mon) { return Mon.CreatureID == BattleData.CreatureID; });

		UE_LOG(LogTemp, Error,
			TEXT("UpdatePartyCreatureBattleData: slot %d holds %s (ID %d) but was handed %s (ID %d) - wrong index from the caller. %s"),
			Index,
			*Party[Index].GetDisplayName().ToString(),
			Party[Index].CreatureID,
			*BattleData.GetDisplayName().ToString(),
			BattleData.CreatureID,
			RealIndex != INDEX_NONE
				? *FString::Printf(TEXT("Redirecting to slot %d."), RealIndex)
				: TEXT("That Creature is not in the party - write REJECTED."));

#if DO_BLUEPRINT_GUARD
		// Names the Blueprint function and node that passed the bad index.
		UE_LOG(LogTemp, Error, TEXT("  Blueprint callstack:\n%s"), *FFrame::GetScriptCallstack());
#endif

		if (RealIndex == INDEX_NONE)
		{
			return false;
		}

		Index = RealIndex;
	}

	// Get existing creature save data
	FGF_CreatureInstanceData& ExistingData = Party[Index];

	// Store the original data for logging
	int32 OriginalLevel = ExistingData.Level;
	float OriginalEXP = ExistingData.CurrentEXP;

	// ====================================
	// UPDATE ONLY BATTLE-AFFECTED FIELDS
	// ====================================

	// HP and Status
	ExistingData.CurrentHP = BattleData.CurrentHP;
	ExistingData.MaxHP = BattleData.MaxHP;
	ExistingData.StatusCondition = BattleData.StatusCondition;
	ExistingData.SleepCounter = BattleData.SleepCounter;
	ExistingData.bIsDowned = BattleData.bIsDowned;

	// Uses is deliberately NOT copied back from the battle actor, and neither are Skills.
	//
	// The party entry owns Uses: UseSkill() decrements it the instant a move is used and the
	// battle HUD reads it straight back out, so it is already correct by the time a battle
	// ends. The actor keeps its own SkillUses copy only for the "can this move still be used"
	// check, and copying that copy over the real Uses here meant ANY drift in it silently
	// became the saved value — spend 2 Uses on move 2, and move 1 came back 2 Uses short in the
	// next battle while move 2 was fully restored. UseSkill()/RestoreSkillUses() push their
	// result into the live actor instead, so the actor never becomes a second source of truth.
	for (int32 i = 0; i < BattleData.CurrentUses.Num() && i < ExistingData.CurrentUses.Num(); i++)
	{
		if (BattleData.CurrentUses[i] != ExistingData.CurrentUses[i])
		{
			UE_LOG(LogTemp, Warning,
				TEXT("UpdatePartyCreatureBattleData: %s (slot %d) move %d - battle actor says %d Uses, party says %d. "
					 "Keeping the party value; the actor's Uses tracking is out of step."),
				*ExistingData.GetDisplayName().ToString(), Index, i,
				BattleData.CurrentUses[i], ExistingData.CurrentUses[i]);
		}
	}

	// DON'T UPDATE LEVEL/EXP!
	// These are managed by GiveEXP() separately.
	// The battle actor has stale data (not updated during battle).
	// GiveEXP() has already updated VaultSystem with correct values.

	// ExistingData.Level = BattleData.Level;           ← REMOVED
	// ExistingData.CurrentEXP = BattleData.CurrentEXP; ← REMOVED

	// Training (updated if Creature defeated enemies)
	ExistingData.HP_Training = BattleData.HP_Training;
	ExistingData.Attack_Training = BattleData.Attack_Training;
	ExistingData.Defense_Training = BattleData.Defense_Training;
	ExistingData.Magic_Training = BattleData.Magic_Training;
	ExistingData.Poise_Training = BattleData.Poise_Training;
	ExistingData.Speed_Training = BattleData.Speed_Training;

	UE_LOG(LogTemp, Log, TEXT("Synced Battle Data for slot %d"), Index);
	UE_LOG(LogTemp, Log, TEXT("   - Level: %d (preserved from GiveEXP)"), ExistingData.Level);
	UE_LOG(LogTemp, Log, TEXT("   - EXP: %.0f (preserved from GiveEXP)"), ExistingData.CurrentEXP);
	UE_LOG(LogTemp, Log, TEXT("   - HP: %.0f / %.0f"), ExistingData.CurrentHP, ExistingData.MaxHP);
	UE_LOG(LogTemp, Log, TEXT("   - Status: %d"), (int32)ExistingData.StatusCondition);

	return true;
}


bool UGF_VaultSystem::SwapPartyCreature(int32 Index1, int32 Index2)
{
	if (!IsValidPartyIndex(Index1) || !IsValidPartyIndex(Index2))
	{
		return false;
	}

	Party.Swap(Index1, Index2);
	UE_LOG(LogTemp, Log, TEXT("Swapped party Creature: %d <-> %d"), Index1, Index2);

	return true;
}

int32 UGF_VaultSystem::GetFirstHealthyCreatureIndex() const
{
	for (int32 i = 0; i < Party.Num(); i++)
	{
		// An egg fills a party slot but can never be sent out.
		if (!Party[i].bIsEgg && !Party[i].bIsDowned && Party[i].CurrentHP > 0)
		{
			return i;
		}
	}
	return -1;
}

//--------------------
// BOX MANAGEMENT
//--------------------

void UGF_VaultSystem::InitializeVaultPages(int32 NumVaultPages)
{
	VaultPages.Empty();
	VaultPageNames.Empty();

	NumVaultPages = FMath::Clamp(NumVaultPages, 1, MaxVaultPages);

	for (int32 i = 0; i < NumVaultPages; i++)
	{
		FGF_VaultPage NewVaultPage;  // Constructor automatically initializes all 30 slots as empty
		VaultPages.Add(NewVaultPage);
		VaultPageNames.Add(FString::Printf(TEXT("Vault %d"), i + 1));
	}

	UE_LOG(LogTemp, Log, TEXT("Initialized %d Creature boxes with 30 slots each"), NumVaultPages);
}

bool UGF_VaultSystem::AddToVault(int32 VaultPageIndex, const FGF_CreatureInstanceData& CreatureData)
{
	if (!IsValidVaultPageIndex(VaultPageIndex))
	{
		return false;
	}

	if (!CreatureData.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot add invalid Creature to box!"));
		return false;
	}

	// Find first empty slot in the fixed 30-slot array
	for (int32 i = 0; i < MaxVaultPageSize; i++)
	{
		if (!VaultPages[VaultPageIndex].Creature[i].IsValid())
		{
			FGF_CreatureInstanceData ModifiedData = CreatureData;

			if (ModifiedData.OriginalTamerName.IsNone())
			{
				ModifiedData.OriginalTamerName = FName(*PlayerName);
				ModifiedData.OriginalTamerID = PlayerID;
			}

			if (ModifiedData.CurrentTamerName.IsNone())
			{
				ModifiedData.CurrentTamerName = FName(*PlayerName);
				ModifiedData.CurrentTamerID = PlayerID;
			}

			VaultPages[VaultPageIndex].Creature[i] = ModifiedData;
			UE_LOG(LogTemp, Log, TEXT("Added %s to box %d slot %d"),
				*ModifiedData.GetDisplayName().ToString(), VaultPageIndex, i);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Cannot add to box %d: No empty slots!"), VaultPageIndex);
	return false;
}

bool UGF_VaultSystem::RemoveFromVault(int32 VaultPageIndex, int32 SlotIndex)
{
	if (!IsValidVaultSlot(VaultPageIndex, SlotIndex))
	{
		return false;
	}

	// Clear the slot by setting it to an empty Creature
	VaultPages[VaultPageIndex].Creature[SlotIndex] = FGF_CreatureInstanceData();
	UE_LOG(LogTemp, Log, TEXT("Cleared box %d, slot %d"), VaultPageIndex, SlotIndex);
	return true;
}

bool UGF_VaultSystem::GetVaultCreature(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const
{
	if (!IsValidVaultSlot(VaultPageIndex, SlotIndex))
	{
		return false;
	}

	OutData = VaultPages[VaultPageIndex].Creature[SlotIndex];
	return true;
}

bool UGF_VaultSystem::UpdateVaultCreature(int32 VaultPageIndex, int32 SlotIndex, const FGF_CreatureInstanceData& UpdatedData)
{
	if (!IsValidVaultSlot(VaultPageIndex, SlotIndex))
	{
		return false;
	}

	VaultPages[VaultPageIndex].Creature[SlotIndex] = UpdatedData;
	UE_LOG(LogTemp, Log, TEXT("Updated box %d, slot %d"), VaultPageIndex, SlotIndex);
	return true;
}

bool UGF_VaultSystem::MovePartyToVault(int32 PartyIndex, int32 VaultPageIndex)
{
	if (!IsValidPartyIndex(PartyIndex) || !IsValidVaultPageIndex(VaultPageIndex))
	{
		return false;
	}

	// Find first empty slot in box
	for (int32 i = 0; i < MaxVaultPageSize; i++)
	{
		if (!VaultPages[VaultPageIndex].Creature[i].IsValid())
		{
			// Can't remove last healthy Creature from party
			if (Party.Num() == 1 && !Party[PartyIndex].bIsDowned)
			{
				UE_LOG(LogTemp, Warning, TEXT("Cannot move last healthy Creature from party!"));
				return false;
			}

			FGF_CreatureInstanceData CreatureData = Party[PartyIndex];
			VaultPages[VaultPageIndex].Creature[i] = CreatureData;
			Party.RemoveAt(PartyIndex);

			UE_LOG(LogTemp, Log, TEXT("Moved %s from party to box %d slot %d"),
				*CreatureData.GetDisplayName().ToString(), VaultPageIndex, i);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Cannot move to box: Vault is full!"));
	return false;
}

bool UGF_VaultSystem::MoveVaultToParty(int32 VaultPageIndex, int32 SlotIndex)
{
	if (!IsValidVaultSlot(VaultPageIndex, SlotIndex))
	{
		return false;
	}

	if (IsPartyFull())
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot move to party: Party is full!"));
		return false;
	}

	if (!VaultPages[VaultPageIndex].Creature[SlotIndex].IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot move from box: Slot is empty!"));
		return false;
	}

	FGF_CreatureInstanceData CreatureData = VaultPages[VaultPageIndex].Creature[SlotIndex];
	Party.Add(CreatureData);
	VaultPages[VaultPageIndex].Creature[SlotIndex] = FGF_CreatureInstanceData(); // Clear slot

	UE_LOG(LogTemp, Log, TEXT("Moved %s from box %d slot %d to party"),
		*CreatureData.GetDisplayName().ToString(), VaultPageIndex, SlotIndex);
	return true;
}

FString UGF_VaultSystem::GetVaultPageName(int32 VaultPageIndex) const
{
	if (!IsValidVaultPageIndex(VaultPageIndex))
	{
		return TEXT("Invalid Vault");
	}

	return VaultPageNames[VaultPageIndex];
}

void UGF_VaultSystem::SetVaultPageName(int32 VaultPageIndex, const FString& NewName)
{
	if (IsValidVaultPageIndex(VaultPageIndex))
	{
		VaultPageNames[VaultPageIndex] = NewName;
	}
}

int32 UGF_VaultSystem::GetVaultCreatureCount(int32 VaultPageIndex) const
{
	if (!IsValidVaultPageIndex(VaultPageIndex))
	{
		return 0;
	}

	// Count non-empty slots in the fixed 30-slot array
	int32 Count = 0;
	for (int32 i = 0; i < MaxVaultPageSize; i++)
	{
		if (VaultPages[VaultPageIndex].Creature[i].IsValid())
		{
			Count++;
		}
	}

	return Count;
}

//--------------------
// NEW BOX FUNCTIONS (FIXED 30-SLOT SYSTEM)
//--------------------

TArray<FGF_CreatureInstanceData> UGF_VaultSystem::GetAllCreaturesInVaultPage(int32 VaultPageIndex) const
{
	TArray<FGF_CreatureInstanceData> Result;

	if (!IsValidVaultPageIndex(VaultPageIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("GetAllCreaturesInVaultPage: Invalid box index %d"), VaultPageIndex);
		return Result;
	}

	// Return all 30 slots (including empty ones)
	for (int32 i = 0; i < MaxVaultPageSize; i++)
	{
		Result.Add(VaultPages[VaultPageIndex].Creature[i]);
	}

	return Result;
}

bool UGF_VaultSystem::GetCreatureAtSlot(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const
{
	if (!IsValidVaultPageIndex(VaultPageIndex) || SlotIndex < 0 || SlotIndex >= MaxVaultPageSize)
	{
		UE_LOG(LogTemp, Error, TEXT("GetCreatureAtSlot: Invalid indices (Vault: %d, Slot: %d)"), VaultPageIndex, SlotIndex);
		return false;
	}

	const FGF_CreatureInstanceData& Creature = VaultPages[VaultPageIndex].Creature[SlotIndex];

	// Check if slot actually has a valid Creature
	if (!Creature.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("GetCreatureAtSlot: Slot %d in Vault %d is empty"), SlotIndex, VaultPageIndex);
		return false;
	}

	OutData = Creature;

	// Same rule in the boxes: an egg is never battle-eligible, wherever it is stored.
	OutData.NormalizeEggState();

	return true;
}

bool UGF_VaultSystem::IsSlotEmpty(int32 VaultPageIndex, int32 SlotIndex) const
{
	if (!IsValidVaultPageIndex(VaultPageIndex) || SlotIndex < 0 || SlotIndex >= MaxVaultPageSize)
	{
		return true; // Invalid indices = empty
	}

	// A slot is empty if the Creature has no valid SpeciesData
	return !VaultPages[VaultPageIndex].Creature[SlotIndex].IsValid();
}

// REPLACE SwapCreatureInVault function (starting at line 432) with this:

bool UGF_VaultSystem::SwapCreatureInVault(int32 VaultPageIndex, int32 SlotA, int32 SlotB)
{
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("SWAP CREATURE IN BOX CALLED (Same Vault)"));
    UE_LOG(LogTemp, Warning, TEXT("   Vault: %d, Slot A: %d, Slot B: %d"), VaultPageIndex, SlotA, SlotB);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    if (!IsValidVaultPageIndex(VaultPageIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("SwapCreatureInVault: Invalid box index %d"), VaultPageIndex);
        return false;
    }

    if (SlotA < 0 || SlotA >= MaxVaultPageSize || SlotB < 0 || SlotB >= MaxVaultPageSize)
    {
        UE_LOG(LogTemp, Error, TEXT("SwapCreatureInVault: Invalid slot indices (A: %d, B: %d)"), SlotA, SlotB);
        return false;
    }

    // STEP 1: Before swap
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("BEFORE SWAP:"));
    UE_LOG(LogTemp, Warning, TEXT("  Slot[%d] IsValid: %s, SpeciesData: %s"),
        SlotA,
        VaultPages[VaultPageIndex].Creature[SlotA].IsValid() ? TEXT("TRUE") : TEXT("FALSE"),
        VaultPages[VaultPageIndex].Creature[SlotA].SpeciesData.IsNull() ? TEXT("NULL") : TEXT("Valid"));
    UE_LOG(LogTemp, Warning, TEXT("  Slot[%d] IsValid: %s, SpeciesData: %s"),
        SlotB,
        VaultPages[VaultPageIndex].Creature[SlotB].IsValid() ? TEXT("TRUE") : TEXT("FALSE"),
        VaultPages[VaultPageIndex].Creature[SlotB].SpeciesData.IsNull() ? TEXT("NULL") : TEXT("Valid"));

    // STEP 2: Create temp
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("CREATING TEMP"));
    FGF_CreatureInstanceData Temp = VaultPages[VaultPageIndex].Creature[SlotA];
    UE_LOG(LogTemp, Warning, TEXT("  Temp SpeciesData: %s"),
        Temp.SpeciesData.IsNull() ? TEXT("NULL ") : TEXT("Valid "));

    // STEP 3: Perform swap
    VaultPages[VaultPageIndex].Creature[SlotA] = VaultPages[VaultPageIndex].Creature[SlotB];
    VaultPages[VaultPageIndex].Creature[SlotB] = Temp;

    // STEP 4: Verify
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("AFTER SWAP:"));
    UE_LOG(LogTemp, Warning, TEXT("  Slot[%d] SpeciesData: %s"),
        SlotB,
        VaultPages[VaultPageIndex].Creature[SlotB].SpeciesData.IsNull() ? TEXT("NULL ") : TEXT("Valid "));

    if (VaultPages[VaultPageIndex].Creature[SlotB].IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("Creature valid at destination"));
    }

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT(""));

    return true;
}

// REPLACE SwapCreatureBetweenVaultPages function (starting at line 461) with this:

bool UGF_VaultSystem::SwapCreatureBetweenVaultPages(
    int32 VaultPageA, int32 SlotA,
    int32 VaultPageB, int32 SlotB)
{
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("SWAP CREATURE BETWEEN BOXES CALLED"));
    UE_LOG(LogTemp, Warning, TEXT("   Source: Vault %d Slot %d"), VaultPageA, SlotA);
    UE_LOG(LogTemp, Warning, TEXT("   Dest:   Vault %d Slot %d"), VaultPageB, SlotB);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    if (!IsValidVaultPageIndex(VaultPageA) || !IsValidVaultPageIndex(VaultPageB))
    {
        UE_LOG(LogTemp, Error, TEXT("SwapCreatureBetweenVaultPages: Invalid box indices"));
        return false;
    }

    if (SlotA < 0 || SlotA >= MaxVaultPageSize || SlotB < 0 || SlotB >= MaxVaultPageSize)
    {
        UE_LOG(LogTemp, Error, TEXT("SwapCreatureBetweenVaultPages: Invalid slot indices"));
        return false;
    }

    // STEP 1: Check BEFORE creating temp variables
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("STEP 1: BEFORE COPYING TO TEMP"));
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] IsValid: %s"),
        VaultPageA, SlotA, VaultPages[VaultPageA].Creature[SlotA].IsValid() ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] SpeciesData IsNull: %s"),
        VaultPageA, SlotA, VaultPages[VaultPageA].Creature[SlotA].SpeciesData.IsNull() ? TEXT("TRUE (BAD!)") : TEXT("FALSE (GOOD)"));

    if (!VaultPages[VaultPageA].Creature[SlotA].SpeciesData.IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("Vault[%d][%d] SpeciesData Path: %s"),
            VaultPageA, SlotA, *VaultPages[VaultPageA].Creature[SlotA].SpeciesData.ToSoftObjectPath().ToString());
    }

    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] CreatureID: %d"),
        VaultPageA, SlotA, VaultPages[VaultPageA].Creature[SlotA].CreatureID);
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] Level: %d"),
        VaultPageA, SlotA, VaultPages[VaultPageA].Creature[SlotA].Level);

    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] IsValid: %s"),
        VaultPageB, SlotB, VaultPages[VaultPageB].Creature[SlotB].IsValid() ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] SpeciesData IsNull: %s"),
        VaultPageB, SlotB, VaultPages[VaultPageB].Creature[SlotB].SpeciesData.IsNull() ? TEXT("TRUE (Empty Slot)") : TEXT("FALSE (Has Creature)"));

    // STEP 2: Create temp copies
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("STEP 2: CREATING TEMP COPIES"));
    FGF_CreatureInstanceData TempA = VaultPages[VaultPageA].Creature[SlotA];
    UE_LOG(LogTemp, Warning, TEXT("  TempA created - IsValid: %s"), TempA.IsValid() ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogTemp, Warning, TEXT("  TempA SpeciesData IsNull: %s"),
        TempA.SpeciesData.IsNull() ? TEXT("TRUE LOST DURING COPY!") : TEXT("FALSE "));

    if (!TempA.SpeciesData.IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("TempA SpeciesData Path: %s"),
            *TempA.SpeciesData.ToSoftObjectPath().ToString());
    }
    UE_LOG(LogTemp, Warning, TEXT("  TempA CreatureID: %d"), TempA.CreatureID);
    UE_LOG(LogTemp, Warning, TEXT("  TempA Level: %d"), TempA.Level);

    FGF_CreatureInstanceData TempB = VaultPages[VaultPageB].Creature[SlotB];
    UE_LOG(LogTemp, Warning, TEXT("  TempB created - IsValid: %s"), TempB.IsValid() ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogTemp, Warning, TEXT("  TempB SpeciesData IsNull: %s"),
        TempB.SpeciesData.IsNull() ? TEXT("TRUE (Empty)") : TEXT("FALSE"));

    // STEP 3: Do the swap
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("STEP 3: PERFORMING SWAP"));
    VaultPages[VaultPageA].Creature[SlotA] = TempB;
    UE_LOG(LogTemp, Warning, TEXT("Assigned TempB -> Vault[%d][%d]"), VaultPageA, SlotA);

    VaultPages[VaultPageB].Creature[SlotB] = TempA;
    UE_LOG(LogTemp, Warning, TEXT("Assigned TempA -> Vault[%d][%d]"), VaultPageB, SlotB);

    // STEP 4: Verify after swap
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("STEP 4: AFTER SWAP VERIFICATION"));
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] IsValid: %s"),
        VaultPageB, SlotB, VaultPages[VaultPageB].Creature[SlotB].IsValid() ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] SpeciesData IsNull: %s"),
        VaultPageB, SlotB, VaultPages[VaultPageB].Creature[SlotB].SpeciesData.IsNull() ? TEXT("TRUE LOST DURING ASSIGNMENT!") : TEXT("FALSE "));

    if (!VaultPages[VaultPageB].Creature[SlotB].SpeciesData.IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("Vault[%d][%d] SpeciesData Path: %s"),
            VaultPageB, SlotB, *VaultPages[VaultPageB].Creature[SlotB].SpeciesData.ToSoftObjectPath().ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("CRITICAL: SpeciesData WAS LOST!"));
        UE_LOG(LogTemp, Error, TEXT("  But CreatureID still exists: %d"), VaultPages[VaultPageB].Creature[SlotB].CreatureID);
        UE_LOG(LogTemp, Error, TEXT("  And Level still exists: %d"), VaultPages[VaultPageB].Creature[SlotB].Level);
    }

    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] CreatureID: %d"),
        VaultPageB, SlotB, VaultPages[VaultPageB].Creature[SlotB].CreatureID);
    UE_LOG(LogTemp, Warning, TEXT("  Vault[%d][%d] Level: %d"),
        VaultPageB, SlotB, VaultPages[VaultPageB].Creature[SlotB].Level);

    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("Swap completed"));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT(""));

    return true;
}

void UGF_VaultSystem::FixAllTamerInfo()
{
    int32 Fixed = 0;
    int32 Checked = 0;

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("FIXING TAMER INFO FOR ALL CREATURE"));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    // Fix party Creature
    for (int32 i = 0; i < Party.Num(); i++)
    {
        Checked++;

        bool bNeedsFixing = Party[i].OriginalTamerName.IsNone() ||
                           Party[i].OriginalTamerID == 0;

        if (bNeedsFixing)
        {
            UE_LOG(LogTemp, Warning, TEXT("Party Slot %d: %s - Missing tamer info"),
                i, *Party[i].GetDisplayName().ToString());

            Party[i].OriginalTamerName = FName(*PlayerName);
            Party[i].OriginalTamerID = PlayerID;
            Party[i].CurrentTamerName = FName(*PlayerName);
            Party[i].CurrentTamerID = PlayerID;

            UE_LOG(LogTemp, Log, TEXT("Fixed! Now: OT=%s, ID=%d"),
                *PlayerName, PlayerID);

            Fixed++;
        }
        else
        {
            UE_LOG(LogTemp, VeryVerbose, TEXT("Party Slot %d: %s - Already has tamer info"),
                i, *Party[i].GetDisplayName().ToString());
        }
    }

    // Fix box Creature
    for (int32 VaultPageIdx = 0; VaultPageIdx < VaultPages.Num(); VaultPageIdx++)
    {
        for (int32 SlotIdx = 0; SlotIdx < MaxVaultPageSize; SlotIdx++)
        {
            FGF_CreatureInstanceData& Creature = VaultPages[VaultPageIdx].Creature[SlotIdx];

            // Skip empty slots
            if (!Creature.IsValid())
            {
                continue;
            }

            Checked++;

            bool bNeedsFixing = Creature.OriginalTamerName.IsNone() ||
                               Creature.OriginalTamerID == 0;

            if (bNeedsFixing)
            {
                UE_LOG(LogTemp, Warning, TEXT("Vault %d Slot %d: %s - Missing tamer info"),
                    VaultPageIdx + 1, SlotIdx + 1, *Creature.GetDisplayName().ToString());

                Creature.OriginalTamerName = FName(*PlayerName);
                Creature.OriginalTamerID = PlayerID;
                Creature.CurrentTamerName = FName(*PlayerName);
                Creature.CurrentTamerID = PlayerID;

                UE_LOG(LogTemp, Log, TEXT("Fixed! Now: OT=%s, ID=%d"),
                    *PlayerName, PlayerID);

                Fixed++;
            }
            else
            {
                UE_LOG(LogTemp, VeryVerbose, TEXT("Vault %d Slot %d: %s - Already has tamer info"),
                    VaultPageIdx + 1, SlotIdx + 1, *Creature.GetDisplayName().ToString());
            }
        }
    }

    // Summary
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("SUMMARY:"));
    UE_LOG(LogTemp, Warning, TEXT("   Total Creature Checked: %d"), Checked);
    UE_LOG(LogTemp, Warning, TEXT("   Creature Fixed: %d"), Fixed);
    UE_LOG(LogTemp, Warning, TEXT("   Creature Already OK: %d"), Checked - Fixed);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    if (Fixed > 0)
    {
        // Save the changes
        bool bSaveSuccess = SaveToDisk();

        if (bSaveSuccess)
        {
            UE_LOG(LogTemp, Warning, TEXT("SAVED %d FIXED CREATURE TO DISK"), Fixed);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("FAILED TO SAVE! Changes will be lost!"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("All Creature already have correct tamer info - no changes needed"));
    }

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

namespace
{
	/**
	 * Brings one Creature's Trait back in line with its species.
	 *
	 * Until EvolveCreature() was fixed, evolving swapped SpeciesData but left the
	 * cached Trait alone, so a slot-0 Cubfang walked out of the evolution
	 * still holding Fleetfoot instead of picking up Direfang's Menace.
	 * TraitSlot survived intact, which is what makes the repair possible.
	 *
	 * The stored Trait is trusted whenever it's legal for the current species —
	 * only the slot is resynced in that case, so nothing the player has actually
	 * seen in the UI gets rewritten. The trait is only replaced when it belongs
	 * to no slot the species has, which is exactly the evolution bug's signature.
	 *
	 * Returns true if anything changed.
	 */
	bool RepairCreatureTrait(FGF_CreatureInstanceData& Mon, EGF_CreatureTrait& OutOldTrait)
	{
		OutOldTrait = Mon.Trait;

		if (!Mon.IsValid())
		{
			return false;
		}

		// IsValid() only says the soft pointer is set; the asset still has to load.
		const UGF_CreatureSpeciesData* Species = Mon.SpeciesData.LoadSynchronous();
		if (!Species)
		{
			return false;
		}

		const int32 OldSlot = Mon.TraitSlot;

		// Slot 1 is only meaningful on a species that has a second trait.
		if (Mon.TraitSlot != 0 && Species->Trait2 == EGF_CreatureTrait::None)
		{
			Mon.TraitSlot = 0;
		}

		if (Mon.Trait != EGF_CreatureTrait::None && Mon.Trait == Species->Trait1)
		{
			// Legal trait, slot disagreed — trust the trait.
			Mon.TraitSlot = 0;
		}
		else if (Mon.Trait != EGF_CreatureTrait::None
			&& Species->Trait2 != EGF_CreatureTrait::None
			&& Mon.Trait == Species->Trait2)
		{
			Mon.TraitSlot = 1;
		}
		else
		{
			// Either blank (a pre-trait save) or an trait this species can't
			// have (the evolution bug). Re-derive it from the surviving slot.
			const EGF_CreatureTrait Resolved =
				UGF_CreatureTraitLibrary::GetSpeciesTraitBySlot(Species, Mon.TraitSlot);

			// Never wipe a real trait just because the species asset has no
			// Trait1 configured — leaving it stale beats blanking it.
			if (Resolved != EGF_CreatureTrait::None)
			{
				Mon.Trait = Resolved;
			}
		}

		return Mon.Trait != OutOldTrait || Mon.TraitSlot != OldSlot;
	}
}

int32 UGF_VaultSystem::RepairAllTraits(bool bSaveIfChanged)
{
	int32 Checked = 0;
	int32 Fixed = 0;

	// One lambda over every storage location so party, boxes and sanctuary can't
	// drift apart — a Direfang sitting in a box is just as broken as one in the party.
	auto Repair = [&Checked, &Fixed](FGF_CreatureInstanceData& Mon, const FString& Where)
	{
		if (!Mon.IsValid())
		{
			return;
		}

		Checked++;

		EGF_CreatureTrait OldTrait = EGF_CreatureTrait::None;
		if (RepairCreatureTrait(Mon, OldTrait))
		{
			Fixed++;
			UE_LOG(LogTemp, Warning, TEXT("%s: %s - trait %s -> %s (slot %d)"),
				*Where,
				*Mon.GetDisplayName().ToString(),
				*UEnum::GetValueAsString(OldTrait),
				*UEnum::GetValueAsString(Mon.Trait),
				Mon.TraitSlot);
		}
	};

	for (int32 i = 0; i < Party.Num(); i++)
	{
		Repair(Party[i], FString::Printf(TEXT("Party %d"), i + 1));
	}

	for (int32 VaultPageIdx = 0; VaultPageIdx < VaultPages.Num(); VaultPageIdx++)
	{
		for (int32 SlotIdx = 0; SlotIdx < MaxVaultPageSize; SlotIdx++)
		{
			Repair(VaultPages[VaultPageIdx].Creature[SlotIdx],
				FString::Printf(TEXT("Vault %d Slot %d"), VaultPageIdx + 1, SlotIdx + 1));
		}
	}

	for (int32 SlotIdx = 0; SlotIdx < Sanctuary.Slots.Num(); SlotIdx++)
	{
		if (Sanctuary.Slots[SlotIdx].bOccupied)
		{
			Repair(Sanctuary.Slots[SlotIdx].Mon,
				FString::Printf(TEXT("Sanctuary %d"), SlotIdx + 1));
		}
	}

	if (Fixed > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("RepairAllTraits: corrected %d of %d Creature."), Fixed, Checked);

		if (bSaveIfChanged && !SaveToDisk())
		{
			UE_LOG(LogTemp, Error, TEXT("RepairAllTraits: fixed %d Creature but FAILED to save - "
				"the repair will run again next load."), Fixed);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("RepairAllTraits: all %d Creature already correct."), Checked);
	}

	return Fixed;
}

bool UGF_VaultSystem::InsertCreatureAtSlot(int32 VaultPageIndex, int32 SlotIndex, const FGF_CreatureInstanceData& Creature)
{
	if (!IsValidVaultPageIndex(VaultPageIndex) || SlotIndex < 0 || SlotIndex >= MaxVaultPageSize)
	{
		UE_LOG(LogTemp, Error, TEXT("InsertCreatureAtSlot: Invalid indices"));
		return false;
	}

	if (!Creature.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("InsertCreatureAtSlot: Cannot insert invalid Creature"));
		return false;
	}

	// Simply place Creature at the specified slot (overwrites whatever was there)
	VaultPages[VaultPageIndex].Creature[SlotIndex] = Creature;

	UE_LOG(LogTemp, Log, TEXT("Inserted %s at Vault %d Slot %d"),
		*Creature.GetDisplayName().ToString(), VaultPageIndex, SlotIndex);
	return true;
}

bool UGF_VaultSystem::MoveCreatureToSlot(int32 SourceVaultPage, int32 SourceSlot, int32 DestVaultPage, int32 DestSlot)
{
	if (!IsValidVaultPageIndex(SourceVaultPage) || !IsValidVaultPageIndex(DestVaultPage))
	{
		UE_LOG(LogTemp, Error, TEXT("MoveCreatureToSlot: Invalid box index"));
		return false;
	}

	if (SourceSlot < 0 || SourceSlot >= MaxVaultPageSize || DestSlot < 0 || DestSlot >= MaxVaultPageSize)
	{
		UE_LOG(LogTemp, Error, TEXT("MoveCreatureToSlot: Invalid slot indices"));
		return false;
	}

	// Check if source slot has a Creature
	if (!VaultPages[SourceVaultPage].Creature[SourceSlot].IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MoveCreatureToSlot: Source slot is empty"));
		return false;
	}

	// Skill Creature from source to destination (overwriting whatever was there)
	VaultPages[DestVaultPage].Creature[DestSlot] = VaultPages[SourceVaultPage].Creature[SourceSlot];

	// Clear source slot
	VaultPages[SourceVaultPage].Creature[SourceSlot] = FGF_CreatureInstanceData();

	UE_LOG(LogTemp, Log, TEXT("Moved Creature from Vault %d Slot %d to Vault %d Slot %d"),
		SourceVaultPage, SourceSlot, DestVaultPage, DestSlot);
	return true;
}

bool UGF_VaultSystem::SwapPartyWithVault(int32 PartyIndex, int32 VaultPageIndex, int32 VaultSlot)
{
	if (!IsValidPartyIndex(PartyIndex) || !IsValidVaultPageIndex(VaultPageIndex))
	{
		return false;
	}

	if (VaultSlot < 0 || VaultSlot >= MaxVaultPageSize)
	{
		UE_LOG(LogTemp, Error, TEXT("SwapPartyWithVault: Invalid box slot %d"), VaultSlot);
		return false;
	}

	// Can't swap if party Creature is last healthy one and box slot is empty
	if (Party.Num() == 1 && !Party[PartyIndex].bIsDowned &&
		!VaultPages[VaultPageIndex].Creature[VaultSlot].IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot swap: Would leave party without healthy Creature!"));
		return false;
	}

	// Swap party Creature with box Creature
	FGF_CreatureInstanceData Temp = Party[PartyIndex];
	Party[PartyIndex] = VaultPages[VaultPageIndex].Creature[VaultSlot];
	VaultPages[VaultPageIndex].Creature[VaultSlot] = Temp;

	// If we swapped an empty box slot into party, remove it
	if (!Party[PartyIndex].IsValid())
	{
		Party.RemoveAt(PartyIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("Swapped Party slot %d with Vault %d Slot %d"),
		PartyIndex, VaultPageIndex, VaultSlot);
	return true;
}

int32 UGF_VaultSystem::GetTotalStoredCreature() const
{
	int32 Total = 0;

	for (int32 VaultPageIdx = 0; VaultPageIdx < VaultPages.Num(); VaultPageIdx++)
	{
		for (int32 SlotIdx = 0; SlotIdx < MaxVaultPageSize; SlotIdx++)
		{
			if (VaultPages[VaultPageIdx].Creature[SlotIdx].IsValid())
			{
				Total++;
			}
		}
	}

	return Total;
}

bool UGF_VaultSystem::FindCreatureInStorage(int32 CreatureID, int32& OutVaultPageIndex, int32& OutSlotIndex) const
{
	for (int32 VaultPageIdx = 0; VaultPageIdx < VaultPages.Num(); VaultPageIdx++)
	{
		for (int32 SlotIdx = 0; SlotIdx < MaxVaultPageSize; SlotIdx++)
		{
			if (VaultPages[VaultPageIdx].Creature[SlotIdx].IsValid() &&
				VaultPages[VaultPageIdx].Creature[SlotIdx].CreatureID == CreatureID)
			{
				OutVaultPageIndex = VaultPageIdx;
				OutSlotIndex = SlotIdx;
				return true;
			}
		}
	}

	return false;
}

bool UGF_VaultSystem::CanStoreMoreCreature() const
{
	// Check if any box has an empty slot
	for (int32 VaultPageIdx = 0; VaultPageIdx < VaultPages.Num(); VaultPageIdx++)
	{
		for (int32 SlotIdx = 0; SlotIdx < MaxVaultPageSize; SlotIdx++)
		{
			if (!VaultPages[VaultPageIdx].Creature[SlotIdx].IsValid())
			{
				return true; // Found an empty slot
			}
		}
	}

	return false; // All slots full
}
//--------------------
// SAVE/LOAD
//--------------------

bool UGF_VaultSystem::SaveToDisk()
{
	bool bSuccess = UGameplayStatics::SaveGameToSlot(this, SaveSlotName, UserIndex);

	// The quest half below shouts when it fails; this half used to say nothing at all --
	// which is the half a player notices, because it is their Creature. A report of "my
	// data stopped saving" was unfalsifiable against a log that never mentioned the write.
	// Always log, success or failure: a save that silently did not happen must never again
	// be indistinguishable from one that did.
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("SaveToDisk: wrote '%s' with %d party Creature"),
			*SaveSlotName, Party.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SaveToDisk: '%s' WRITE FAILED - %d party Creature were NOT saved!"),
			*SaveSlotName, Party.Num());
	}

	// Save quest flags via the QuestSubsystem.
	//
	// This half used to be reached through GEngine->GetCurrentPlayWorld(), and when
	// that came back null the party was written while the story flags were not - a
	// save that looks fine and has silently lost the player's progression, with not
	// one line in the log to say so. Walk the world contexts instead, and treat a
	// miss as a failed save rather than a quiet one.
	bool bQuestSaved = false;

	if (GEngine)
	{
		UGameInstance* GI = GEngine->GetCurrentPlayWorld()
			? GEngine->GetCurrentPlayWorld()->GetGameInstance()
			: nullptr;

		if (!GI)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.OwningGameInstance)
				{
					GI = Context.OwningGameInstance;
					break;
				}
			}

			if (GI)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("SaveToDisk: no current play world - fell back to the world context's "
					     "GameInstance to write the quest flags."));
			}
		}

		if (GI)
		{
			if (UGF_QuestSubsystem* QuestSys = GI->GetSubsystem<UGF_QuestSubsystem>())
			{
				bQuestSaved = QuestSys->SaveStory(TEXT("QuestSlot"), 0);

				if (bQuestSaved)
				{
					UE_LOG(LogTemp, Log, TEXT("SaveToDisk: wrote QuestSlot with %d story flag(s)"),
						QuestSys->GetFlagCount());
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("SaveToDisk: QuestSlot WRITE FAILED - story flags were not saved!"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SaveToDisk: QuestSubsystem missing - story flags were not saved!"));
			}

			// Berry tree growth rides along with every save for the same reason: one
			// Blueprint save node has to write the whole run, or the halves desync -
			// berries in the bag but the tree still full is the exploit this closes.
			if (UGF_BerryGrowthSubsystem* BerrySys = GI->GetSubsystem<UGF_BerryGrowthSubsystem>())
			{
				BerrySys->SaveBerries();
			}
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("SaveToDisk: could not resolve a GameInstance - story flags and berries were "
				     "NOT saved. The party file on disk is now ahead of the story file."));
		}
	}

	if (bSuccess && bQuestSaved)
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully saved Creature data to slot: %s"), *SaveSlotName);
	}
	else if (bSuccess)
	{
		// Reported as a failed save on purpose. The player losing story progress
		// without being told is what this whole change exists to stop; a visible
		// "save failed" sends them to try again while the data is still in memory.
		UE_LOG(LogTemp, Error,
			TEXT("Creature data saved but the story half did NOT - reporting the save as failed so "
			     "the player is not told a half-written save succeeded."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save Creature data!"));
	}

	return bSuccess && bQuestSaved;
}

UGF_VaultSystem* UGF_VaultSystem::LoadFromDisk(bool& bSuccess)
{
	if (!DoesSaveExist())
	{
		UE_LOG(LogTemp, Warning, TEXT("No save file found. Creating new box system."));
		bSuccess = false;

		// Create new box system
		UGF_VaultSystem* NewVaultSystem = Cast<UGF_VaultSystem>(
			UGameplayStatics::CreateSaveGameObject(UGF_VaultSystem::StaticClass())
		);

		if (NewVaultSystem)
		{
			NewVaultSystem->InitializeVaultPages(MaxVaultPages);
		}

		return NewVaultSystem;
	}

	USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(SaveSlotName, UserIndex);
	UGF_VaultSystem* VaultSystem = Cast<UGF_VaultSystem>(LoadedGame);

	if (VaultSystem)
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully loaded Creature data from slot: %s"), *SaveSlotName);
		UE_LOG(LogTemp, Log, TEXT("Loaded %d Creature in party, %d boxes"),
			VaultSystem->Party.Num(), VaultSystem->VaultPages.Num());
		bSuccess = true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load Creature data!"));
		bSuccess = false;
	}

	return VaultSystem;
}

bool UGF_VaultSystem::DoesSaveExist()
{
	return UGameplayStatics::DoesSaveGameExist(SaveSlotName, UserIndex);
}

bool UGF_VaultSystem::DeleteSave()
{
	if (!DoesSaveExist())
	{
		return false;
	}

	bool bSuccess = UGameplayStatics::DeleteGameInSlot(SaveSlotName, UserIndex);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Deleted save file: %s"), *SaveSlotName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to delete save file!"));
	}

	return bSuccess;
}

//--------------------
// VALIDATION
//--------------------

bool UGF_VaultSystem::IsValidPartyIndex(int32 Index) const
{
	if (Index < 0 || Index >= Party.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid party index: %d (Party size: %d)"), Index, Party.Num());
		return false;
	}
	return true;
}

bool UGF_VaultSystem::IsValidVaultPageIndex(int32 VaultPageIndex) const
{
	if (VaultPageIndex < 0 || VaultPageIndex >= VaultPages.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid box index: %d (Total boxes: %d)"), VaultPageIndex, VaultPages.Num());
		return false;
	}
	return true;
}

bool UGF_VaultSystem::IsValidVaultSlot(int32 VaultPageIndex, int32 SlotIndex) const
{
	if (!IsValidVaultPageIndex(VaultPageIndex))
	{
		return false;
	}

	// Fixed: Use MaxVaultPageSize constant (30) instead of .Num()
	if (SlotIndex < 0 || SlotIndex >= MaxVaultPageSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid box slot: Vault %d, Slot %d (Max slots: %d)"),
			VaultPageIndex, SlotIndex, MaxVaultPageSize);
		return false;
	}

	return true;
}


//--------------------
// Check for caught status
//--------------------


void UGF_VaultSystem::MarkSpeciesAsCaught(int32 CompendiumNumber)
{
    if (CompendiumNumber > 0)
    {
        bool bAlreadyCaught = CaughtCreature.Contains(CompendiumNumber);
        CaughtCreature.Add(CompendiumNumber);
        SeenCreature.Add(CompendiumNumber);  // caught implies seen

        if (!bAlreadyCaught)
        {
            UE_LOG(LogTemp, Log, TEXT("VaultSystem: Marked Compendium #%d as caught! Total caught: %d"),
                CompendiumNumber, CaughtCreature.Num());
        }
    }
}

void UGF_VaultSystem::MarkSpeciesAsCaughtFromData(UGF_CreatureSpeciesData* SpeciesData)
{
    if (SpeciesData)
    {
        MarkSpeciesAsCaught(SpeciesData->CompendiumNumber);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultSystem: Cannot mark null SpeciesData as caught"));
    }
}

bool UGF_VaultSystem::HasCaughtSpecies(int32 CompendiumNumber) const
{
    return CaughtCreature.Contains(CompendiumNumber);
}

bool UGF_VaultSystem::HasCaughtSpeciesFromData(UGF_CreatureSpeciesData* SpeciesData) const
{
    if (SpeciesData)
    {
        return HasCaughtSpecies(SpeciesData->CompendiumNumber);
    }
    return false;
}

void UGF_VaultSystem::MarkSpeciesAsSeen(int32 CompendiumNumber)
{
    if (CompendiumNumber > 0)
    {
        SeenCreature.Add(CompendiumNumber);
    }
}

bool UGF_VaultSystem::HasSeenSpecies(int32 CompendiumNumber) const
{
    return SeenCreature.Contains(CompendiumNumber);
}

TMap<uint8, TMap<FName, int32>> UGF_VaultSystem::GetInventorySaveFormat() const
{
	TMap<uint8, TMap<FName, int32>> SaveFormat;
	for (const FGF_CategoryItemData& CategoryData : PlayerItems)
	{
		TMap<FName, int32> ItemMap;
		for (const FGF_ItemInstance& Item : CategoryData.Items)
		{
			if (Item.ItemName != NAME_None)
				ItemMap.Add(Item.ItemName, Item.Quantity);
		}
		if (ItemMap.Num() > 0)
			SaveFormat.Add((uint8)CategoryData.Category, ItemMap);
	}
	return SaveFormat;
}

void UGF_VaultSystem::SetInventoryFromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData)
{
	PlayerItems.Empty();
	for (const auto& CategoryPair : SaveData)
	{
		FGF_CategoryItemData CategoryData;
		CategoryData.Category = (EGF_ItemCategory)CategoryPair.Key;
		for (const auto& ItemPair : CategoryPair.Value)
		{
			FGF_ItemInstance Item;
			Item.ItemName = ItemPair.Key;
			Item.Quantity = ItemPair.Value;
			CategoryData.Items.Add(Item);
		}
		PlayerItems.Add(CategoryData);
	}
}

void UGF_VaultSystem::SetInventoryFromSaveFormat(const TMap<uint8, TMap<FName, int32>>& SaveData, UGF_ItemDataManager* DataManager)
{
    PlayerItems.Empty();

    if (!DataManager)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_VaultSystem: Cannot load inventory - ItemDataManager is null!"));
        return;
    }

    for (const auto& CategoryPair : SaveData)
    {
        FGF_CategoryItemData CategoryData;
        CategoryData.Category = (EGF_ItemCategory)CategoryPair.Key;

        for (const auto& ItemPair : CategoryPair.Value)
        {
            FName ItemName = ItemPair.Key;
            int32 Quantity = ItemPair.Value;

            // Validate the name and re-populate ItemID for runtime ID lookups.
            UGF_ItemData* ItemData = DataManager->GetItemByName(ItemName);
            if (ItemData)
            {
                FGF_ItemInstance Item;
                Item.ItemName = ItemName;
                Item.ItemID = ItemData->ItemID;
                Item.Quantity = Quantity;

                CategoryData.Items.Add(Item);

                UE_LOG(LogTemp, Log, TEXT("Loaded item: Name=%s (ID=%d, Qty: %d)"),
                    *Item.ItemName.ToString(), Item.ItemID, Quantity);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Could not find ItemData for '%s'! Item skipped."), *ItemName.ToString());
            }
        }

        if (CategoryData.Items.Num() > 0)
        {
            PlayerItems.Add(CategoryData);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Inventory loaded: %d categories, %d total items"),
        PlayerItems.Num(),
        [&]() { int32 Total = 0; for (const auto& Cat : PlayerItems) Total += Cat.Items.Num(); return Total; }());
}

//--------------------
// Held item stuff
//--------------------

bool UGF_VaultSystem::GiveHeldItemToPartyCreature(int32 PartyIndex, FName ItemName)
{
    if (!IsValidPartyIndex(PartyIndex))
    {
        return false;
    }

    Party[PartyIndex].GiveHeldItem(ItemName);
    return true;
}

FName UGF_VaultSystem::TakeHeldItemFromPartyCreature(int32 PartyIndex)
{
    if (!IsValidPartyIndex(PartyIndex))
    {
        return NAME_None;
    }

    return Party[PartyIndex].TakeHeldItem();
}

bool UGF_VaultSystem::PartyCreatureHasHeldItem(int32 PartyIndex) const
{
    if (!IsValidPartyIndex(PartyIndex))
    {
        return false;
    }

    return Party[PartyIndex].HasHeldItem();
}

FName UGF_VaultSystem::GetPartyCreatureHeldItem(int32 PartyIndex) const
{
    if (!IsValidPartyIndex(PartyIndex))
    {
        return NAME_None;
    }

    return Party[PartyIndex].GetHeldItem();
}

//--------------------
// BOX CREATURE MOVE ACCESS
//--------------------

bool UGF_VaultSystem::GetBoxCreatureMovesWithPP(
    int32 VaultPageIndex,
    int32 SlotIndex,
    TArray<TSubclassOf<AGF_SkillDefinition>>& OutSkills,
    TArray<int32>& OutCurrentUses,
    TArray<int32>& OutMaxUses)
{
    OutSkills.Empty();
    OutCurrentUses.Empty();
    OutMaxUses.Empty();

    // Get Creature data from box
    FGF_CreatureInstanceData CreatureData;
    if (!GetCreatureAtSlot(VaultPageIndex, SlotIndex, CreatureData))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetBoxCreatureMovesWithPP: No Creature at Vault %d Slot %d"),
            VaultPageIndex, SlotIndex);
        return false;
    }

    // Check if Creature has moves
    if (CreatureData.Skills.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetBoxCreatureMovesWithPP: Creature has no moves"));
        return false;
    }

    // Load each move and convert from soft reference to hard reference
    for (int32 i = 0; i < CreatureData.Skills.Num(); i++)
    {
        TSoftClassPtr<AGF_SkillDefinition> SoftMove = CreatureData.Skills[i];

        if (SoftMove.IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("GetBoxCreatureMovesWithPP: Skill %d is null"), i);
            continue;
        }

        // Load the move synchronously (for UI, this is fine)
        TSubclassOf<AGF_SkillDefinition> LoadedSkill = SoftMove.LoadSynchronous();

        if (LoadedSkill)
        {
            OutSkills.Add(LoadedSkill);

            // Add Uses if available
            if (CreatureData.CurrentUses.IsValidIndex(i) && CreatureData.MaxUses.IsValidIndex(i))
            {
                OutCurrentUses.Add(CreatureData.CurrentUses[i]);
                OutMaxUses.Add(CreatureData.MaxUses[i]);
            }
            else
            {
                // Default Uses if not set
                OutCurrentUses.Add(10);
                OutMaxUses.Add(10);
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("GetBoxCreatureMovesWithPP: Failed to load move %d"), i);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GetBoxCreatureMovesWithPP: Loaded %d moves for Vault %d Slot %d"),
        OutSkills.Num(), VaultPageIndex, SlotIndex);
    return OutSkills.Num() > 0;
}

TArray<TSubclassOf<AGF_SkillDefinition>> UGF_VaultSystem::GetVaultCreatureSkills(
    int32 VaultPageIndex,
    int32 SlotIndex)
{
    TArray<TSubclassOf<AGF_SkillDefinition>> Skills;
    TArray<int32> DummyCurrentUses;
    TArray<int32> DummyMaxUses;

    GetBoxCreatureMovesWithPP(VaultPageIndex, SlotIndex, Skills, DummyCurrentUses, DummyMaxUses);

    return Skills;
}

