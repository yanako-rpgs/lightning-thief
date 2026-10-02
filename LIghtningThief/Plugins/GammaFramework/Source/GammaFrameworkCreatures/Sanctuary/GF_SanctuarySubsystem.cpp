// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_SanctuarySubsystem.h"
#include "GF_BreedingLibrary.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_VaultSystem.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_CreatureTraits.h"
#include "GF_SkillDefinition.h"
#include "GF_GridMovementComponent.h"
#include "Settings/GF_SettingsSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
	// Highest level the sanctuary is allowed to raise a boarded Creature to.
	// Without this, boarding a Creature would be a free bypass of the difficulty
	// level cap, since sanctuary growth does not go through GiveEXP.
	int32 GetSanctuaryMaxLevel(const UGameInstance* GameInstance)
	{
		const UGF_SettingsSubsystem* Settings =
			GameInstance ? GameInstance->GetSubsystem<UGF_SettingsSubsystem>() : nullptr;

		return (Settings && Settings->IsLevelCapEnforced()) ? Settings->GetLevelCap() : 100;
	}

	/**
	 * True when someone in the party is carrying Emberhide or Scorchhide, which
	 * burns egg cycles twice as fast.
	 *
	 * This is a classic effect — classic had no hatch-speed trait — added deliberately
	 * as a quality-of-life change rather than for accuracy.
	 *
	 * Eggs are skipped on purpose. An egg carries the traits of the species it
	 * will hatch into, so counting them would let a Cindercrawl egg halve its own
	 * hatching time with an trait nothing in the party actually has yet.
	 */
	bool PartyHasHatchSpeedTrait(const UGF_VaultSystem* Vault)
	{
		if (!Vault)
		{
			return false;
		}

		for (const FGF_CreatureInstanceData& Mon : Vault->Party)
		{
			if (Mon.bIsEgg || !Mon.IsValid())
			{
				continue;
			}

			const EGF_CreatureTrait Trait = UGF_CreatureTraitLibrary::GetInstanceTrait(Mon);

			if (Trait == EGF_CreatureTrait::Emberhide || Trait == EGF_CreatureTrait::Scorchhide)
			{
				return true;
			}
		}

		return false;
	}
}

void UGF_SanctuarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Deliberately NOT resolving the manager subsystem here — subsystem creation
	// order isn't guaranteed and the save file may not be loaded yet. Everything
	// goes through GetManager() / GetSanctuaryData() lazily instead.
	UE_LOG(LogTemp, Log, TEXT("Sanctuary subsystem ready."));
}

void UGF_SanctuarySubsystem::Deinitialize()
{
	for (const TWeakObjectPtr<UGF_GridMovementComponent>& Source : BoundStepSources)
	{
		if (UGF_GridMovementComponent* Movement = Source.Get())
		{
			Movement->OnStepCompleted.RemoveDynamic(this, &UGF_SanctuarySubsystem::HandleStepCompleted);
		}
	}
	BoundStepSources.Empty();

	Super::Deinitialize();
}

//--------------------
// PLUMBING
//--------------------

UGF_CreatureManagerSubsystem* UGF_SanctuarySubsystem::GetManager() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UGF_CreatureManagerSubsystem>();
	}

	return nullptr;
}

UGF_VaultSystem* UGF_SanctuarySubsystem::GetVaultSystem() const
{
	UGF_CreatureManagerSubsystem* Manager = GetManager();
	return Manager ? Manager->VaultSystem : nullptr;
}

FGF_SanctuaryData* UGF_SanctuarySubsystem::GetSanctuaryData() const
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault)
	{
		return nullptr;
	}

	// A save file written before the sanctuary existed deserializes with an empty
	// slot array, so top it up before anyone indexes into it.
	if (Vault->Sanctuary.Slots.Num() != SANCTUARY_SLOT_COUNT)
	{
		Vault->Sanctuary.Slots.SetNum(SANCTUARY_SLOT_COUNT);
	}

	return &Vault->Sanctuary;
}

//--------------------
// STEP DRIVER
//--------------------

void UGF_SanctuarySubsystem::BindStepSource(UGF_GridMovementComponent* MovementComponent)
{
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->OnStepCompleted.AddUniqueDynamic(this, &UGF_SanctuarySubsystem::HandleStepCompleted);

	BoundStepSources.RemoveAll([](const TWeakObjectPtr<UGF_GridMovementComponent>& Source)
	{
		return !Source.IsValid();
	});

	BoundStepSources.AddUnique(MovementComponent);

	UE_LOG(LogTemp, Log, TEXT("Sanctuary: now driven by %s"), *MovementComponent->GetName());
}

void UGF_SanctuarySubsystem::UnbindStepSource(UGF_GridMovementComponent* MovementComponent)
{
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_SanctuarySubsystem::HandleStepCompleted);
	BoundStepSources.Remove(MovementComponent);
}

void UGF_SanctuarySubsystem::HandleStepCompleted()
{
	SanctuaryStep();
}

void UGF_SanctuarySubsystem::SanctuaryStep()
{
	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		return;
	}

	// One step walked = one EXP for each boarded Creature. It's banked here and
	// only turned into levels when the player collects it.
	for (FGF_SanctuarySlot& Slot : Sanctuary->Slots)
	{
		if (Slot.bOccupied)
		{
			Slot.StepsAccumulated++;
		}
	}

	Sanctuary->StepCounter++;

	if (Sanctuary->StepCounter >= StepsPerCycle)
	{
		Sanctuary->StepCounter = 0;
		RunCycleTick();
	}

	// --- Announce any egg sitting at zero cycles, every step, not just on the step it
	// reached zero. A listener is allowed to refuse a hatch — the player gets spotted by
	// a tamer on that same tile, a cutscene is running, the game was just reloaded —
	// and if this only fired on the transition, that egg would sit at zero forever with
	// nothing left to re-trigger it. The listener is a gatekeeper that no-ops when it
	// can't hatch yet, so re-announcing costs nothing.
	//
	// Collect first, broadcast after: a listener is very likely to call HatchEgg straight
	// away, and mutating the party mid-iteration would invalidate this loop.
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault)
	{
		return;
	}

	TArray<int32> ReadyToHatch;

	for (int32 PartyIndex = 0; PartyIndex < Vault->Party.Num(); PartyIndex++)
	{
		const FGF_CreatureInstanceData& Mon = Vault->Party[PartyIndex];

		if (Mon.bIsEgg && Mon.EggCyclesRemaining <= 0)
		{
			ReadyToHatch.Add(PartyIndex);
		}
	}

	for (const int32 PartyIndex : ReadyToHatch)
	{
		OnEggReadyToHatch.Broadcast(PartyIndex);
	}
}

void UGF_SanctuarySubsystem::RunCycleTick()
{
	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Sanctuary || !Vault)
	{
		return;
	}

	// --- Egg roll: only when both slots are full and nothing is already waiting.
	if (!Sanctuary->bEggWaiting && GetOccupiedSlotCount() == SANCTUARY_SLOT_COUNT)
	{
		const EGF_SanctuaryCompatibility Compatibility = GetCompatibility();

		if (UGF_BreedingLibrary::RollForEgg(Compatibility))
		{
			Sanctuary->bEggWaiting = true;

			UE_LOG(LogTemp, Log, TEXT("Sanctuary: an egg appeared (compatibility %d%%)."),
				UGF_BreedingLibrary::GetCompatibilityChance(Compatibility));

			OnSanctuaryEggAvailable.Broadcast();

			if (UGF_CreatureManagerSubsystem* Manager = GetManager())
			{
				Manager->MarkDirty();
			}
		}
	}

	// --- Egg cycles: every egg in the party gets one cycle closer to hatching.
	// Announcing readiness is NOT done here — SanctuaryStep does it every step, so an
	// interrupted hatch can be picked up again later.
	// Resolved once per tick rather than per egg: the answer is a property of the
	// party, not of the egg being processed, and hoisting it keeps a party of six
	// eggs from re-scanning the party six times.
	const int32 CyclesThisTick = PartyHasHatchSpeedTrait(Vault) ? 2 : 1;

	for (int32 PartyIndex = 0; PartyIndex < Vault->Party.Num(); PartyIndex++)
	{
		FGF_CreatureInstanceData& Mon = Vault->Party[PartyIndex];

		if (!Mon.bIsEgg || Mon.EggCyclesRemaining <= 0)
		{
			continue;
		}

		Mon.EggCyclesRemaining -= CyclesThisTick;

		if (Mon.EggCyclesRemaining <= 0)
		{
			Mon.EggCyclesRemaining = 0;
			UE_LOG(LogTemp, Log, TEXT("Sanctuary: the egg in party slot %d is ready to hatch."), PartyIndex);
		}
	}

	if (UGF_CreatureManagerSubsystem* Manager = GetManager())
	{
		Manager->MarkDirty();
	}
}

//--------------------
// DEPOSIT / WITHDRAW
//--------------------

int32 UGF_SanctuarySubsystem::GetFirstFreeSlot() const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		return INDEX_NONE;
	}

	for (int32 i = 0; i < Sanctuary->Slots.Num(); i++)
	{
		if (!Sanctuary->Slots[i].bOccupied)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

bool UGF_SanctuarySubsystem::HasFreeSlot() const
{
	return GetFirstFreeSlot() != INDEX_NONE;
}

int32 UGF_SanctuarySubsystem::GetOccupiedSlotCount() const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FGF_SanctuarySlot& Slot : Sanctuary->Slots)
	{
		if (Slot.bOccupied)
		{
			Count++;
		}
	}

	return Count;
}

bool UGF_SanctuarySubsystem::CanDepositPartyCreature(int32 PartyIndex, FText& OutReason) const
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault || !Vault->Party.IsValidIndex(PartyIndex))
	{
		OutReason = NSLOCTEXT("Sanctuary", "DepositNoMon", "There's no creature there.");
		return false;
	}

	if (!HasFreeSlot())
	{
		OutReason = NSLOCTEXT("Sanctuary", "DepositFull", "We're already looking after two creatures.");
		return false;
	}

	const FGF_CreatureInstanceData& Mon = Vault->Party[PartyIndex];

	if (Mon.bIsEgg)
	{
		OutReason = NSLOCTEXT("Sanctuary", "DepositEgg", "We can't raise an EGG for you.");
		return false;
	}

	// The player has to keep something that can actually battle.
	int32 RemainingUsable = 0;
	for (int32 i = 0; i < Vault->Party.Num(); i++)
	{
		if (i != PartyIndex && !Vault->Party[i].bIsEgg)
		{
			RemainingUsable++;
		}
	}

	if (RemainingUsable < 1)
	{
		OutReason = NSLOCTEXT("Sanctuary", "DepositLastMon", "You can't leave yourself without a creature!");
		return false;
	}

	OutReason = FText::GetEmpty();
	return true;
}

bool UGF_SanctuarySubsystem::DepositPartyCreature(int32 PartyIndex, int32& OutSlotIndex)
{
	OutSlotIndex = INDEX_NONE;

	FText Reason;
	if (!CanDepositPartyCreature(PartyIndex, Reason))
	{
		UE_LOG(LogTemp, Warning, TEXT("Sanctuary: deposit refused - %s"), *Reason.ToString());
		return false;
	}

	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	UGF_VaultSystem* Vault = GetVaultSystem();
	UGF_CreatureManagerSubsystem* Manager = GetManager();
	if (!Sanctuary || !Vault || !Manager)
	{
		return false;
	}

	const int32 SlotIndex = GetFirstFreeSlot();
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	FGF_SanctuarySlot& Slot = Sanctuary->Slots[SlotIndex];
	Slot.Mon = Vault->Party[PartyIndex];
	Slot.bOccupied = true;
	Slot.LevelOnDeposit = Slot.Mon.Level;
	Slot.StepsAccumulated = 0;

	// A Creature handed over stops following the player around.
	Slot.Mon.bIsFollowerOut = false;

	if (Vault->bHasFollowerOut && Vault->FollowerPartyIndex == PartyIndex)
	{
		Vault->bHasFollowerOut = false;
		Vault->FollowerPartyIndex = -1;
	}

	Manager->RemoveFromParty(PartyIndex);

	// The pair changed, so any egg the man was holding for the old pair is void.
	Sanctuary->bEggWaiting = false;

	Manager->MarkDirty();

	OutSlotIndex = SlotIndex;

	UE_LOG(LogTemp, Log, TEXT("Sanctuary: took in %s (Lv %d) into slot %d"),
		*Slot.Mon.GetDisplayName().ToString(), Slot.LevelOnDeposit, SlotIndex);

	return true;
}

int32 UGF_SanctuarySubsystem::GetWithdrawCost(int32 SlotIndex) const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary || !Sanctuary->Slots.IsValidIndex(SlotIndex) || !Sanctuary->Slots[SlotIndex].bOccupied)
	{
		return 0;
	}

	const FGF_SanctuarySlot& Slot = Sanctuary->Slots[SlotIndex];
	const int32 ProjectedLevel = CalculateLevelAfterBankedEXP(Slot.Mon, Slot.StepsAccumulated);
	const int32 LevelsGained = FMath::Max(0, ProjectedLevel - Slot.LevelOnDeposit);

	// Flat 100 to board it, plus 100 for every level it picked up.
	return 100 + (100 * LevelsGained);
}

bool UGF_SanctuarySubsystem::CanWithdraw(int32 SlotIndex, FText& OutReason) const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	UGF_CreatureManagerSubsystem* Manager = GetManager();

	if (!Sanctuary || !Manager || !Sanctuary->Slots.IsValidIndex(SlotIndex) || !Sanctuary->Slots[SlotIndex].bOccupied)
	{
		OutReason = NSLOCTEXT("Sanctuary", "WithdrawEmpty", "There's no creature there.");
		return false;
	}

	if (Manager->IsPartyFull())
	{
		OutReason = NSLOCTEXT("Sanctuary", "WithdrawPartyFull", "Your party is full.");
		return false;
	}

	if (Manager->GetPlayerMoney() < GetWithdrawCost(SlotIndex))
	{
		OutReason = NSLOCTEXT("Sanctuary", "WithdrawNoMoney", "You don't have enough money.");
		return false;
	}

	OutReason = FText::GetEmpty();
	return true;
}

bool UGF_SanctuarySubsystem::WithdrawCreature(int32 SlotIndex, FGF_CreatureInstanceData& OutCreature,
	TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutLearnedSkills,
	TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutForgottenSkills)
{
	OutLearnedSkills.Reset();
	OutForgottenSkills.Reset();

	FText Reason;
	if (!CanWithdraw(SlotIndex, Reason))
	{
		UE_LOG(LogTemp, Warning, TEXT("Sanctuary: withdrawal refused - %s"), *Reason.ToString());
		return false;
	}

	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	UGF_CreatureManagerSubsystem* Manager = GetManager();
	if (!Sanctuary || !Manager)
	{
		return false;
	}

	const int32 Cost = GetWithdrawCost(SlotIndex);
	if (Cost > 0 && !Manager->RemovePlayerMoney(Cost))
	{
		return false;
	}

	FGF_SanctuarySlot& Slot = Sanctuary->Slots[SlotIndex];

	FGF_CreatureInstanceData Mon = Slot.Mon;
	ApplySanctuaryGrowth(SlotIndex, Mon, Slot.StepsAccumulated, OutLearnedSkills, OutForgottenSkills);

	if (!Manager->AddToParty(Mon))
	{
		// Shouldn't happen — CanWithdraw already checked for room — but never
		// clear the slot on failure or the Creature would vanish.
		UE_LOG(LogTemp, Error, TEXT("Sanctuary: AddToParty failed after charging %d. Refunding."), Cost);
		Manager->AddPlayerMoney(Cost);
		return false;
	}

	Slot = FGF_SanctuarySlot();

	// classic compacts the sanctuary: if slot 0 was
	// just emptied while slot 1 is occupied, the remaining Creature shifts down.
	// A lone boarder is therefore ALWAYS in slot 0 — dialogue flows rely on this.
	if (SlotIndex == 0 && Sanctuary->Slots.Num() > 1 && Sanctuary->Slots[1].bOccupied)
	{
		Sanctuary->Slots[0] = Sanctuary->Slots[1];
		Sanctuary->Slots[1] = FGF_SanctuarySlot();
	}

	// Losing a parent invalidates a waiting egg.
	Sanctuary->bEggWaiting = false;

	Manager->MarkDirty();

	OutCreature = Mon;

	UE_LOG(LogTemp, Log, TEXT("Sanctuary: returned %s at Lv %d for %d (learned %d, forgot %d)"),
		*Mon.GetDisplayName().ToString(), Mon.Level, Cost, OutLearnedSkills.Num(), OutForgottenSkills.Num());

	return true;
}

//--------------------
// GROWTH
//--------------------

int32 UGF_SanctuarySubsystem::CalculateLevelAfterBankedEXP(const FGF_CreatureInstanceData& Mon, int32 BankedEXP) const
{
	UGF_CreatureManagerSubsystem* Manager = GetManager();
	if (!Manager)
	{
		return Mon.Level;
	}

	UGF_CreatureSpeciesData* Species = Mon.SpeciesData.LoadSynchronous();
	if (!Species)
	{
		return Mon.Level;
	}

	// CurrentEXP is progress INTO the current level, so rebuild the cumulative
	// total before adding the banked steps.
	const int32 TotalEXP = Manager->CalculateEXPForLevel(Mon.Level, Species->ExpCurve)
		+ FMath::FloorToInt(Mon.CurrentEXP)
		+ FMath::Max(0, BankedEXP);

	// Same ceiling ApplySanctuaryGrowth uses, so the "will come out at level N"
	// preview never promises growth the cap will refuse to deliver.
	const int32 MaxLevel = GetSanctuaryMaxLevel(GetGameInstance());

	int32 Level = Mon.Level;
	while (Level < MaxLevel && Manager->CalculateEXPForLevel(Level + 1, Species->ExpCurve) <= TotalEXP)
	{
		Level++;
	}

	return Level;
}

void UGF_SanctuarySubsystem::ApplySanctuaryGrowth(int32 SlotIndex, FGF_CreatureInstanceData& Mon, int32 BankedEXP,
	TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutLearnedSkills,
	TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutForgottenSkills) const
{
	UGF_CreatureManagerSubsystem* Manager = GetManager();
	UGF_CreatureSpeciesData* Species = Mon.SpeciesData.LoadSynchronous();

	if (!Manager || !Species)
	{
		return;
	}

	const int32 StartLevel = Mon.Level;

	// Level 100 normally, or the difficulty level cap when one is enforced.
	const int32 MaxLevel = GetSanctuaryMaxLevel(GetGameInstance());

	if (BankedEXP > 0 && Mon.Level < MaxLevel)
	{
		const int32 TotalEXP = Manager->CalculateEXPForLevel(Mon.Level, Species->ExpCurve)
			+ FMath::FloorToInt(Mon.CurrentEXP)
			+ BankedEXP;

		int32 NewLevel = Mon.Level;
		while (NewLevel < MaxLevel && Manager->CalculateEXPForLevel(NewLevel + 1, Species->ExpCurve) <= TotalEXP)
		{
			NewLevel++;
		}

		Mon.Level = NewLevel;

		// The ceiling swallows any surplus, same as the real game does at 100.
		// Under a level cap that means banked steps past the cap are lost rather
		// than held in reserve for the next emblem.
		Mon.CurrentEXP = (NewLevel >= MaxLevel)
			? 0.0f
			: static_cast<float>(TotalEXP - Manager->CalculateEXPForLevel(NewLevel, Species->ExpCurve));
	}

	// Auto-learn everything it reached while boarded. The sanctuary never asks the
	// player what to forget — when all four slots are full the oldest move is
	// pushed off the front. That's the classic behaviour and it's why people warn
	// you about leaving a Creature in the sanctuary too long.
	for (int32 Level = StartLevel + 1; Level <= Mon.Level; Level++)
	{
		for (const FGF_LearnableSkill& Learnable : Species->LearnableSkills)
		{
			if (Learnable.LearnLevel != Level || Learnable.Skill.IsNull())
			{
				continue;
			}

			if (Mon.KnowsSkill(Learnable.Skill))
			{
				continue;
			}

			int32 SkillUses = 10;
			if (TSubclassOf<AGF_SkillDefinition> LoadedClass = Learnable.Skill.LoadSynchronous())
			{
				if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
				{
					SkillUses = CDO->MaxUses;
				}
			}

			if (Mon.Skills.Num() >= Mon.GetSkillSlotCount())
			{
				OutForgottenSkills.Add(Mon.Skills[0]);

				Mon.Skills.RemoveAt(0);
				if (Mon.CurrentUses.IsValidIndex(0)) { Mon.CurrentUses.RemoveAt(0); }
				if (Mon.MaxUses.IsValidIndex(0))     { Mon.MaxUses.RemoveAt(0); }
			}

			Mon.Skills.Add(Learnable.Skill);
			Mon.CurrentUses.Add(SkillUses);
			Mon.MaxUses.Add(SkillUses);

			OutLearnedSkills.Add(Learnable.Skill);
		}

		OnSanctuaryLevelGained.Broadcast(SlotIndex, Level);
	}

	// The sanctuary hands them back in perfect shape: full HP, full Uses, no status.
	Mon.bIsDowned = false;
	Mon.StatusCondition = EGF_STATUSEffect::None;
	Mon.SleepCounter = 0;

	Manager->RecalculateStats(Mon);
	Mon.CurrentHP = Mon.MaxHP;

	Mon.NormalizeUses();
	for (int32 i = 0; i < Mon.CurrentUses.Num(); i++)
	{
		if (Mon.MaxUses.IsValidIndex(i))
		{
			Mon.CurrentUses[i] = Mon.MaxUses[i];
		}
	}
}

//--------------------
// UI QUERIES
//--------------------

bool UGF_SanctuarySubsystem::GetSlotCreature(int32 SlotIndex, FGF_CreatureInstanceData& OutCreature) const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary || !Sanctuary->Slots.IsValidIndex(SlotIndex) || !Sanctuary->Slots[SlotIndex].bOccupied)
	{
		return false;
	}

	OutCreature = Sanctuary->Slots[SlotIndex].Mon;
	return true;
}

int32 UGF_SanctuarySubsystem::GetProjectedLevel(int32 SlotIndex) const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary || !Sanctuary->Slots.IsValidIndex(SlotIndex) || !Sanctuary->Slots[SlotIndex].bOccupied)
	{
		return 0;
	}

	const FGF_SanctuarySlot& Slot = Sanctuary->Slots[SlotIndex];
	return CalculateLevelAfterBankedEXP(Slot.Mon, Slot.StepsAccumulated);
}

int32 UGF_SanctuarySubsystem::GetBankedEXP(int32 SlotIndex) const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary || !Sanctuary->Slots.IsValidIndex(SlotIndex))
	{
		return 0;
	}

	return Sanctuary->Slots[SlotIndex].StepsAccumulated;
}

bool UGF_SanctuarySubsystem::GetSlotPreview(int32 SlotIndex, FGF_SanctuarySlotPreview& OutPreview) const
{
	OutPreview = FGF_SanctuarySlotPreview();

	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary || !Sanctuary->Slots.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const FGF_SanctuarySlot& Slot = Sanctuary->Slots[SlotIndex];
	if (!Slot.bOccupied)
	{
		return false;
	}

	OutPreview.bOccupied = true;
	OutPreview.DisplayName = Slot.Mon.GetDisplayName();
	OutPreview.Gender = Slot.Mon.Gender;
	OutPreview.bIsUnique = Slot.Mon.bIsUnique;
	OutPreview.LevelOnDeposit = Slot.LevelOnDeposit;
	OutPreview.CurrentLevel = CalculateLevelAfterBankedEXP(Slot.Mon, Slot.StepsAccumulated);
	OutPreview.LevelsGained = FMath::Max(0, OutPreview.CurrentLevel - Slot.LevelOnDeposit);
	OutPreview.Cost = 100 + (100 * OutPreview.LevelsGained);

	if (UGF_CreatureSpeciesData* Species = Slot.Mon.SpeciesData.LoadSynchronous())
	{
		OutPreview.Species = Species;
		OutPreview.SpeciesName = Species->SpeciesName;
	}

	return true;
}

//--------------------
// BREEDING
//--------------------

EGF_SanctuaryCompatibility UGF_SanctuarySubsystem::GetCompatibility() const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary || Sanctuary->Slots.Num() < SANCTUARY_SLOT_COUNT)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	if (!Sanctuary->Slots[0].bOccupied || !Sanctuary->Slots[1].bOccupied)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	return UGF_BreedingLibrary::GetCompatibility(Sanctuary->Slots[0].Mon, Sanctuary->Slots[1].Mon);
}

FText UGF_SanctuarySubsystem::GetCompatibilityText() const
{
	if (GetOccupiedSlotCount() < SANCTUARY_SLOT_COUNT)
	{
		return NSLOCTEXT("Sanctuary", "CompatNotEnough", "I'm only looking after one creature right now.");
	}

	return UGF_BreedingLibrary::GetCompatibilityText(GetCompatibility());
}

bool UGF_SanctuarySubsystem::IsEggWaiting() const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	return Sanctuary && Sanctuary->bEggWaiting;
}

bool UGF_SanctuarySubsystem::CollectEgg(FGF_CreatureInstanceData& OutEgg)
{
	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	UGF_CreatureManagerSubsystem* Manager = GetManager();
	UGF_VaultSystem* Vault = GetVaultSystem();

	if (!Sanctuary || !Manager || !Vault)
	{
		UE_LOG(LogTemp, Warning, TEXT("CollectEgg: no save data loaded yet (sanctuary=%d manager=%d box=%d)."),
			Sanctuary != nullptr, Manager != nullptr, Vault != nullptr);
		return false;
	}

	if (!Sanctuary->bEggWaiting)
	{
		UE_LOG(LogTemp, Warning, TEXT("CollectEgg: there is no egg waiting. Walk 256 steps with two "
			"compatible Creature boarded, or call DebugForceEgg first. (Compatibility is currently %d%%.)"),
			UGF_BreedingLibrary::GetCompatibilityChance(GetCompatibility()));
		return false;
	}

	if (GetOccupiedSlotCount() < SANCTUARY_SLOT_COUNT)
	{
		// A parent was taken out between the roll and the pickup.
		UE_LOG(LogTemp, Warning, TEXT("CollectEgg: a parent was withdrawn, so the egg is void."));
		Sanctuary->bEggWaiting = false;
		return false;
	}

	if (Manager->IsPartyFull())
	{
		UE_LOG(LogTemp, Warning, TEXT("CollectEgg: no room in the party for the egg - make a slot free."));
		return false;
	}

	FGF_CreatureInstanceData Egg = UGF_BreedingLibrary::BuildEgg(
		this,
		Sanctuary->Slots[0].Mon,
		Sanctuary->Slots[1].Mon,
		FName(*Vault->PlayerName),
		Vault->PlayerID);

	if (!Egg.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Sanctuary: failed to build the egg - clearing the pending flag."));
		Sanctuary->bEggWaiting = false;
		return false;
	}

	if (!Manager->AddToParty(Egg))
	{
		UE_LOG(LogTemp, Warning, TEXT("CollectEgg: AddToParty refused the egg - leaving it with the sanctuary man."));
		return false;
	}

	Sanctuary->bEggWaiting = false;
	Sanctuary->EggsCollected++;

	Manager->MarkDirty();

	OutEgg = Egg;

	UE_LOG(LogTemp, Log, TEXT("Sanctuary: player collected a %s egg (%d cycles)."),
		*Egg.EggSpeciesName.ToString(), Egg.EggCyclesRemaining);

	return true;
}

void UGF_SanctuarySubsystem::RejectEgg()
{
	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		return;
	}

	Sanctuary->bEggWaiting = false;

	if (UGF_CreatureManagerSubsystem* Manager = GetManager())
	{
		Manager->MarkDirty();
	}
}

//--------------------
// EGG HATCHING
//--------------------

int32 UGF_SanctuarySubsystem::GetEggReadyToHatch() const
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault)
	{
		return INDEX_NONE;
	}

	for (int32 i = 0; i < Vault->Party.Num(); i++)
	{
		if (Vault->Party[i].bIsEgg && Vault->Party[i].EggCyclesRemaining <= 0)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

int32 UGF_SanctuarySubsystem::GetEggCyclesRemaining(int32 PartyIndex) const
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault || !Vault->Party.IsValidIndex(PartyIndex) || !Vault->Party[PartyIndex].bIsEgg)
	{
		return INDEX_NONE;
	}

	return Vault->Party[PartyIndex].EggCyclesRemaining;
}

bool UGF_SanctuarySubsystem::HatchEgg(int32 PartyIndex, FGF_CreatureInstanceData& OutHatched)
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	UGF_CreatureManagerSubsystem* Manager = GetManager();

	if (!Vault || !Manager || !Vault->Party.IsValidIndex(PartyIndex))
	{
		return false;
	}

	FGF_CreatureInstanceData Mon = Vault->Party[PartyIndex];
	if (!UGF_BreedingLibrary::HatchEggInstance(this, Mon))
	{
		return false;
	}

	Manager->UpdatePartyCreatureData(PartyIndex, Mon);

	// A hatched Creature counts as caught for the Compendium.
	if (UGF_CreatureSpeciesData* Species = Mon.SpeciesData.LoadSynchronous())
	{
		Manager->MarkSpeciesAsCaughtFromData(Species);
	}

	Manager->MarkDirty();

	OutHatched = Mon;
	return true;
}

//--------------------
// DEBUG
//--------------------

void UGF_SanctuarySubsystem::DebugResetSanctuary()
{
	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		return;
	}

	for (FGF_SanctuarySlot& Slot : Sanctuary->Slots)
	{
		Slot = FGF_SanctuarySlot();
	}

	Sanctuary->bEggWaiting = false;
	Sanctuary->StepCounter = 0;

	if (UGF_CreatureManagerSubsystem* Manager = GetManager())
	{
		Manager->MarkDirty();
	}

	UE_LOG(LogTemp, Warning, TEXT("DebugResetSanctuary: slots emptied, waiting egg cleared, step counter reset. Save the game to persist."));
}

void UGF_SanctuarySubsystem::DebugForceEgg()
{
	FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		return;
	}

	if (GetOccupiedSlotCount() < SANCTUARY_SLOT_COUNT)
	{
		UE_LOG(LogTemp, Warning, TEXT("Sanctuary debug: need two Creature boarded to force an egg."));
		return;
	}

	Sanctuary->bEggWaiting = true;
	OnSanctuaryEggAvailable.Broadcast();
}

void UGF_SanctuarySubsystem::DebugFastHatch()
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault)
	{
		return;
	}

	int32 EggCount = 0;
	for (FGF_CreatureInstanceData& Mon : Vault->Party)
	{
		if (Mon.bIsEgg)
		{
			EggCount++;
			Mon.EggCyclesRemaining = FMath::Min(Mon.EggCyclesRemaining, 1);
		}
	}

	if (EggCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DebugFastHatch: there are no eggs in the party. "
			"Collect one first - DebugForceEgg then CollectEgg."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DebugFastHatch: %d egg(s) set to 1 cycle. Walk 256 steps "
		"(or DebugAddSteps 256) to hatch."), EggCount);
}

void UGF_SanctuarySubsystem::DebugAddSteps(int32 Steps)
{
	for (int32 i = 0; i < Steps; i++)
	{
		SanctuaryStep();
	}
}

void UGF_SanctuarySubsystem::DebugPrintSanctuary() const
{
	const FGF_SanctuaryData* Sanctuary = GetSanctuaryData();
	if (!Sanctuary)
	{
		UE_LOG(LogTemp, Warning, TEXT("Sanctuary: no save data loaded."));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("--- SANCTUARY ---"));
	UE_LOG(LogTemp, Warning, TEXT("Step counter: %d/%d   Egg waiting: %s   Eggs collected: %d"),
		Sanctuary->StepCounter, StepsPerCycle,
		Sanctuary->bEggWaiting ? TEXT("YES") : TEXT("no"),
		Sanctuary->EggsCollected);

	for (int32 i = 0; i < Sanctuary->Slots.Num(); i++)
	{
		const FGF_SanctuarySlot& Slot = Sanctuary->Slots[i];
		if (!Slot.bOccupied)
		{
			UE_LOG(LogTemp, Warning, TEXT("Slot %d: empty"), i);
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("Slot %d: %s  deposited Lv %d -> now Lv %d  (%d EXP banked, fee %d)"),
			i,
			*Slot.Mon.GetDisplayName().ToString(),
			Slot.LevelOnDeposit,
			CalculateLevelAfterBankedEXP(Slot.Mon, Slot.StepsAccumulated),
			Slot.StepsAccumulated,
			GetWithdrawCost(i));
	}

	UE_LOG(LogTemp, Warning, TEXT("Compatibility: %s"), *GetCompatibilityText().ToString());
}
