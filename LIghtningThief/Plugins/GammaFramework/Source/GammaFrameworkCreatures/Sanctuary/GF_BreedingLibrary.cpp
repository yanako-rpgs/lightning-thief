// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_BreedingLibrary.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_SkillDefinition.h"
#include "GF_ItemInventorySystem.h"
#include "GF_ItemEnums.h"
#include "GF_CreatureMemoLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

const FName UGF_BreedingLibrary::AnchorStoneItemName = FName("AnchorStone");
const FName UGF_BreedingLibrary::UniqueCharmItemName = FName("UniqueCharm");

namespace
{
	// Unique odds work as N independent rolls: more rolls, more chances, which is how
	// classic implementations stack the ForeignPair method and the Unique Charm.
	//   1 roll  = 1/3500   (plain)
	//   3 rolls = ~1/1167  (Unique Charm)
	//   6 rolls = ~1/584   (ForeignPair)
	//   8 rolls = ~1/438   (ForeignPair + Charm)
	// 3500 matches the wild encounter rate in CreateWildCreature rather than the
	// series-standard 4096, so eggs and wild Creature use one number.
	constexpr int32 UniqueOddsDenominator = 3500;
	constexpr int32 ForeignPairBonusRolls = 5;
	constexpr int32 UniqueCharmBonusRolls = 2;
}

UGF_CreatureManagerSubsystem* UGF_BreedingLibrary::GetManager(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UGameInstance* GI = nullptr;

	if (GEngine)
	{
		if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
		{
			GI = World->GetGameInstance();
		}
	}

	// Covers the case where the context object is a game instance subsystem, whose
	// world can be null during startup and teardown.
	if (!GI)
	{
		GI = const_cast<UObject*>(WorldContextObject)->GetTypedOuter<UGameInstance>();
	}

	return GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
}

//--------------------
// COMPATIBILITY
//--------------------

bool UGF_BreedingLibrary::IsUniversalBreeder(const UGF_CreatureSpeciesData* Species)
{
	return Species && Species->bUniversalBreeder;
}

bool UGF_BreedingLibrary::IsUnbreedable(const UGF_CreatureSpeciesData* Species)
{
	if (!Species)
	{
		return true;
	}

	// A species with no egg data filled in yet defaults to untagged, so it
	// stays safely unbreedable until the data pass runs.
	return Species->BreedingGroups.Num() == 0;
}

bool UGF_BreedingLibrary::DoBreedingGroupsOverlap(const UGF_CreatureSpeciesData* A, const UGF_CreatureSpeciesData* B)
{
	if (!A || !B)
	{
		return false;
	}

	// Universal breeders pair with anything that can breed at all.
	if (A->bUniversalBreeder || B->bUniversalBreeder)
	{
		return !IsUnbreedable(A) && !IsUnbreedable(B);
	}

	for (const FName& AGroup : A->BreedingGroups)
	{
		if (AGroup.IsNone())
		{
			continue;
		}
		if (B->BreedingGroups.Contains(AGroup))
		{
			return true;
		}
	}

	return false;
}

EGF_SanctuaryCompatibility UGF_BreedingLibrary::GetCompatibility(const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B)
{
	// Eggs obviously can't breed.
	if (A.bIsEgg || B.bIsEgg)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	UGF_CreatureSpeciesData* SpeciesA = A.SpeciesData.LoadSynchronous();
	UGF_CreatureSpeciesData* SpeciesB = B.SpeciesData.LoadSynchronous();

	if (!SpeciesA || !SpeciesB)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	// Legendaries and other untagged-group species never produce eggs.
	if (IsUnbreedable(SpeciesA) || IsUnbreedable(SpeciesB))
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	const bool bUniversalA = IsUniversalBreeder(SpeciesA);
	const bool bUniversalB = IsUniversalBreeder(SpeciesB);

	// Two universal breeders have nothing to work with.
	if (bUniversalA && bUniversalB)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	// Exactly one Shifter: it can pair with anything breedable, and the only thing
	// that moves the needle is whether the other parent came from another tamer.
	if (bUniversalA || bUniversalB)
	{
		return (A.OriginalTamerID == B.OriginalTamerID)
			? EGF_SanctuaryCompatibility::Low
			: EGF_SanctuaryCompatibility::Medium;
	}

	// No Shifter involved — now gender and breeding groups matter.
	if (A.Gender == B.Gender)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	if (A.Gender == EGF_CreatureGender::Genderless || B.Gender == EGF_CreatureGender::Genderless)
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	if (!DoBreedingGroupsOverlap(SpeciesA, SpeciesB))
	{
		return EGF_SanctuaryCompatibility::Incompatible;
	}

	const bool bSameSpecies = (SpeciesA->SpeciesName == SpeciesB->SpeciesName);
	const bool bSameTamer = (A.OriginalTamerID == B.OriginalTamerID);

	if (bSameSpecies)
	{
		// Same species from two different tamers is the best pairing in the game.
		return bSameTamer ? EGF_SanctuaryCompatibility::Medium : EGF_SanctuaryCompatibility::High;
	}

	return bSameTamer ? EGF_SanctuaryCompatibility::Low : EGF_SanctuaryCompatibility::Medium;
}

int32 UGF_BreedingLibrary::GetCompatibilityChance(EGF_SanctuaryCompatibility Compatibility)
{
	return static_cast<int32>(Compatibility);
}

FText UGF_BreedingLibrary::GetCompatibilityText(EGF_SanctuaryCompatibility Compatibility)
{
	switch (Compatibility)
	{
	case EGF_SanctuaryCompatibility::High:
		return NSLOCTEXT("Sanctuary", "CompatHigh", "The two seem to get along very well!");

	case EGF_SanctuaryCompatibility::Medium:
		return NSLOCTEXT("Sanctuary", "CompatMedium", "The two seem to get along.");

	case EGF_SanctuaryCompatibility::Low:
		return NSLOCTEXT("Sanctuary", "CompatLow", "The two don't seem to like each other.");

	case EGF_SanctuaryCompatibility::Incompatible:
	default:
		return NSLOCTEXT("Sanctuary", "CompatNone", "The two prefer to play with other creatures than with each other.");
	}
}

bool UGF_BreedingLibrary::RollForEgg(EGF_SanctuaryCompatibility Compatibility)
{
	const int32 Chance = GetCompatibilityChance(Compatibility);
	if (Chance <= 0)
	{
		return false;
	}

	// classic: an egg appears when the score beats a uniform 0-99 roll, so a score
	// of 70 really is a 70% chance every 256 steps.
	return Chance > FMath::RandRange(0, 99);
}

//--------------------
// EGG SPECIES RESOLUTION
//--------------------

UGF_CreatureSpeciesData* UGF_BreedingLibrary::GetBaseEggSpecies(const UObject* WorldContextObject, UGF_CreatureSpeciesData* Species)
{
	if (!Species)
	{
		return nullptr;
	}

	UGF_CreatureManagerSubsystem* Manager = GetManager(WorldContextObject);
	if (!Manager)
	{
		return Species;
	}

	// An explicit override always wins — that's what it's for.
	if (!Species->BabySpeciesOverride.IsNone())
	{
		if (UGF_CreatureSpeciesData* Override = Manager->GetCreatureSpeciesData(Species->BabySpeciesOverride))
		{
			return Override;
		}
	}

	// Walk the chain by NAME using the registry index -- nothing is loaded until the very
	// end, and then only the one base species. This used to call GetAllCreatureSpeciesData(),
	// loading all 118 (~4 GB) every time an egg was produced, and because the species cache
	// never evicts, a single breeding session kept that memory for the rest of the run.
	FName CurrentName = Species->SpeciesName;
	TSet<FName> Visited;
	Visited.Add(CurrentName);

	// The visited set stops a malformed evolution loop from hanging the game.
	for (int32 Depth = 0; Depth < 8; Depth++)
	{
		const FName PreEvolution = Manager->GetPreEvolutionName(CurrentName);
		if (PreEvolution.IsNone() || Visited.Contains(PreEvolution))
		{
			break;
		}

		Visited.Add(PreEvolution);
		CurrentName = PreEvolution;
	}

	if (CurrentName == Species->SpeciesName)
	{
		return Species;
	}

	UGF_CreatureSpeciesData* Base = Manager->GetCreatureSpeciesData(CurrentName);
	return Base ? Base : Species;
}

bool UGF_BreedingLibrary::IdentifyParentRoles(const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B,
	int32& OutMotherIndex, int32& OutSkillParentIndex)
{
	OutMotherIndex = INDEX_NONE;
	OutSkillParentIndex = INDEX_NONE;

	if (GetCompatibility(A, B) == EGF_SanctuaryCompatibility::Incompatible)
	{
		return false;
	}

	const UGF_CreatureSpeciesData* SpeciesA = A.SpeciesData.LoadSynchronous();
	const UGF_CreatureSpeciesData* SpeciesB = B.SpeciesData.LoadSynchronous();

	const bool bUniversalA = IsUniversalBreeder(SpeciesA);
	const bool bUniversalB = IsUniversalBreeder(SpeciesB);

	if (bUniversalA || bUniversalB)
	{
		// The non-Shifter parent decides the species AND passes egg moves down,
		// whatever its gender. This is exactly why Shifter is the go-to partner
		// for handing a baby its egg moves.
		OutMotherIndex = bUniversalA ? 1 : 0;
		OutSkillParentIndex = OutMotherIndex;
		return true;
	}

	// Normal pair: the female carries the species, the male passes egg moves.
	OutMotherIndex = (A.Gender == EGF_CreatureGender::Female) ? 0 : 1;
	OutSkillParentIndex = (OutMotherIndex == 0) ? 1 : 0;
	return true;
}

UGF_CreatureSpeciesData* UGF_BreedingLibrary::ResolveEggSpecies(const UObject* WorldContextObject,
	const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B)
{
	int32 MotherIndex = INDEX_NONE;
	int32 SkillParentIndex = INDEX_NONE;
	if (!IdentifyParentRoles(A, B, MotherIndex, SkillParentIndex))
	{
		return nullptr;
	}

	const FGF_CreatureInstanceData& Mother = (MotherIndex == 0) ? A : B;

	UGF_CreatureSpeciesData* MotherSpecies = Mother.SpeciesData.LoadSynchronous();
	UGF_CreatureSpeciesData* BabySpecies = GetBaseEggSpecies(WorldContextObject, MotherSpecies);
	if (!BabySpecies)
	{
		return nullptr;
	}

	UGF_CreatureManagerSubsystem* Manager = GetManager(WorldContextObject);

	// BreedingCharm babies: Dewpaw only lays an Dewkit egg while the mother holds a
	// Tide Charm, and likewise Wardling/Wardkit with the Idle Charm.
	if (Manager && !BabySpecies->CharmBabySpecies.IsNone() && !BabySpecies->CharmItem.IsNone())
	{
		if (Mother.HeldItem == BabySpecies->CharmItem)
		{
			if (UGF_CreatureSpeciesData* CharmBaby = Manager->GetCreatureSpeciesData(BabySpecies->CharmBabySpecies))
			{
				BabySpecies = CharmBaby;
			}
		}
	}

	// Split-gender species stored as two separate entries (Barbling, Glimmer/Glimmara):
	// coin-flip between the female entry and its male counterpart.
	if (Manager && !BabySpecies->AlternateGenderSpecies.IsNone() && FMath::RandBool())
	{
		if (UGF_CreatureSpeciesData* Alternate = Manager->GetCreatureSpeciesData(BabySpecies->AlternateGenderSpecies))
		{
			BabySpecies = Alternate;
		}
	}

	return BabySpecies;
}

//--------------------
// EGG CREATION
//--------------------

void UGF_BreedingLibrary::InheritAPs(const FGF_CreatureInstanceData& Mother, const FGF_CreatureInstanceData& Father,
	FGF_CreatureInstanceData& Egg)
{
	// Order matters only in that it must match the switch below.
	const int32 MotherPotentials[6] = { Mother.HP_AP, Mother.Attack_AP, Mother.Defense_AP,
		Mother.Magic_AP, Mother.Poise_AP, Mother.Speed_AP };
	const int32 FatherPotentials[6] = { Father.HP_AP, Father.Attack_AP, Father.Defense_AP,
		Father.Magic_AP, Father.Poise_AP, Father.Speed_AP };

	// Pick three DISTINCT stats. classic has a bug here that lets the same stat be
	// chosen twice (so some eggs inherit only two); this picks three properly.
	TArray<int32> Available = { 0, 1, 2, 3, 4, 5 };
	for (int32 Picked = 0; Picked < 3; Picked++)
	{
		const int32 ListIndex = FMath::RandRange(0, Available.Num() - 1);
		const int32 Stat = Available[ListIndex];
		Available.RemoveAt(ListIndex);

		// Each inherited stat independently comes from either parent.
		const int32 Value = FMath::RandBool() ? MotherPotentials[Stat] : FatherPotentials[Stat];

		switch (Stat)
		{
		case 0: Egg.HP_AP = Value; break;
		case 1: Egg.Attack_AP = Value; break;
		case 2: Egg.Defense_AP = Value; break;
		case 3: Egg.Magic_AP = Value; break;
		case 4: Egg.Poise_AP = Value; break;
		case 5: Egg.Speed_AP = Value; break;
		default: break;
		}
	}

	// The three stats left keep the random values Initialize already rolled.
}

void UGF_BreedingLibrary::InheritTemperament(const FGF_CreatureInstanceData& Mother, FGF_CreatureInstanceData& Egg)
{
	// classic AnchorStone rule: a mother holding an AnchorStone passes her nature down
	// half the time. (Later generations made this guaranteed.)
	if (Mother.HeldItem == AnchorStoneItemName && FMath::RandBool())
	{
		Egg.Temperament = Mother.Temperament;
	}
}

void UGF_BreedingLibrary::PushEggSkill(FGF_CreatureInstanceData& Egg, const TSoftClassPtr<AGF_SkillDefinition>& Skill)
{
	if (Skill.IsNull() || Egg.KnowsSkill(Skill))
	{
		return;
	}

	int32 SkillUses = 10;
	if (TSubclassOf<AGF_SkillDefinition> LoadedClass = Skill.LoadSynchronous())
	{
		if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
		{
			SkillUses = CDO->MaxUses;
		}
	}

	if (Egg.CanLearnMoreSkills())
	{
		Egg.Skills.Add(Skill);
		Egg.CurrentUses.Add(SkillUses);
		Egg.MaxUses.Add(SkillUses);
		return;
	}

	// Full moveset — the oldest move falls off the front, same as the real game.
	Egg.Skills.RemoveAt(0);
	Egg.CurrentUses.RemoveAt(0);
	Egg.MaxUses.RemoveAt(0);

	Egg.Skills.Add(Skill);
	Egg.CurrentUses.Add(SkillUses);
	Egg.MaxUses.Add(SkillUses);
}

void UGF_BreedingLibrary::BuildEggSkillset(const FGF_CreatureInstanceData& SkillParent, const FGF_CreatureInstanceData& OtherParent,
	const UGF_CreatureSpeciesData* BabySpecies, FGF_CreatureInstanceData& Egg)
{
	if (!BabySpecies)
	{
		return;
	}

	// Pass 1 — egg moves: moves the father knows that appear in the baby's egg
	// move list. This is the whole point of breeding for movesets.
	for (const TSoftClassPtr<AGF_SkillDefinition>& ParentSkill : SkillParent.Skills)
	{
		if (ParentSkill.IsNull())
		{
			continue;
		}

		if (BabySpecies->EggSkills.Contains(ParentSkill))
		{
			PushEggSkill(Egg, ParentSkill);
		}
	}

	// Pass 2 — Tome moves: anything the father knows that the baby would be allowed
	// to learn from a Tome, including moves flagged as universal Tomes.
	for (const TSoftClassPtr<AGF_SkillDefinition>& ParentSkill : SkillParent.Skills)
	{
		if (ParentSkill.IsNull() || Egg.KnowsSkill(ParentSkill))
		{
			continue;
		}

		bool bCanLearn = BabySpecies->LearnableTomeMoves.Contains(ParentSkill);

		if (!bCanLearn)
		{
			if (TSubclassOf<AGF_SkillDefinition> LoadedClass = ParentSkill.LoadSynchronous())
			{
				if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
				{
					bCanLearn = CDO->bUniversalTM;
				}
			}
		}

		if (bCanLearn)
		{
			PushEggSkill(Egg, ParentSkill);
		}
	}

	// Pass 3 — shared moves: anything BOTH parents know that is somewhere in the
	// baby's own level-up learnset gets handed over early.
	for (const TSoftClassPtr<AGF_SkillDefinition>& ParentSkill : SkillParent.Skills)
	{
		if (ParentSkill.IsNull() || Egg.KnowsSkill(ParentSkill))
		{
			continue;
		}

		if (!OtherParent.Skills.Contains(ParentSkill))
		{
			continue;
		}

		for (const FGF_LearnableSkill& Learnable : BabySpecies->LearnableSkills)
		{
			if (Learnable.Skill == ParentSkill)
			{
				PushEggSkill(Egg, ParentSkill);
				break;
			}
		}
	}
}

FGF_CreatureInstanceData UGF_BreedingLibrary::BuildGiftEgg(const UObject* WorldContextObject,
	UGF_CreatureSpeciesData* Species,
	FName OTName, int32 OTID,
	const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills,
	int32 EggCyclesOverride,
	int32 UniqueRolls,
	EGF_Element EggAppearanceType)
{
	FGF_CreatureInstanceData Egg;

	if (!Species)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildGiftEgg: Species is null - returning an invalid egg."));
		return Egg;
	}

	// No IdentifyParentRoles / ResolveEggSpecies here, on purpose. Gift eggs skip
	// every breeding rule, which is what lets an untagged-group legendary like
	// Bloomsprite or Aurelis come in an egg at all.
	Egg.Initialize(Species, 5, StartingSkills, OTName, OTID);

	// Eggs are always found in a plain Core in classic.
	Egg.CaughtCoreName = FName("Core");

	// Fill in real stats rather than leaving Initialize's MaxHP = 0 sentinel — an egg
	// sits in the party like any other member, and every widget that draws an HP bar
	// divides CurrentHP by MaxHP.
	if (UGF_CreatureManagerSubsystem* Manager = GetManager(WorldContextObject))
	{
		Manager->RecalculateStats(Egg);
		Egg.CurrentHP = Egg.MaxHP;
	}

	// Shininess is decided at hatch, same as a bred egg, so the player can save
	// before hatching. Initialize() already rolled it — clear that result.
	Egg.bIsUnique = false;
	Egg.EggUniqueRolls = FMath::Max(1, UniqueRolls);

	Egg.bIsEgg = true;
	Egg.EggSpeciesName = Species->SpeciesName;
	Egg.EggCyclesRemaining = (EggCyclesOverride > 0)
		? EggCyclesOverride
		: FMath::Max(1, Species->EggCycles);
	Egg.EggAppearanceType = EggAppearanceType;
	Egg.Affinity = 0;
	Egg.StatusCondition = EGF_STATUSEffect::None;

	// An egg carries the downed flag so every battle check skips it — see
	// FGF_CreatureInstanceData::NormalizeEggState. HatchEggInstance clears it.
	Egg.NormalizeEggState();

	UE_LOG(LogTemp, Log, TEXT("BuildGiftEgg: created a %s gift egg (%d cycles, %d moves, %d unique roll(s), appearance=%d)"),
		*Species->SpeciesName.ToString(), Egg.EggCyclesRemaining, Egg.Skills.Num(),
		Egg.EggUniqueRolls, (int32)Egg.EggAppearanceType);

	return Egg;
}

EGF_Element UGF_BreedingLibrary::GetEggAppearanceType(const UObject* WorldContextObject,
	const FGF_CreatureInstanceData& Egg)
{
	// An explicit choice always wins.
	if (Egg.EggAppearanceType != EGF_Element::None)
	{
		return Egg.EggAppearanceType;
	}

	// Otherwise take it from what's inside.
	if (!Egg.SpeciesData.IsNull())
	{
		if (const UGF_CreatureSpeciesData* Species = Egg.SpeciesData.LoadSynchronous())
		{
			return Species->PrimaryElement;
		}
	}

	return EGF_Element::None;
}

FGF_CreatureInstanceData UGF_BreedingLibrary::BuildEgg(const UObject* WorldContextObject,
	const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B,
	FName OTName, int32 OTID)
{
	FGF_CreatureInstanceData Egg;

	int32 MotherIndex = INDEX_NONE;
	int32 SkillParentIndex = INDEX_NONE;
	if (!IdentifyParentRoles(A, B, MotherIndex, SkillParentIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildEgg: the two parents can't breed - returning an invalid egg."));
		return Egg;
	}

	const FGF_CreatureInstanceData& Mother = (MotherIndex == 0) ? A : B;
	const FGF_CreatureInstanceData& SkillParent = (SkillParentIndex == 0) ? A : B;
	const FGF_CreatureInstanceData& OtherParent = (SkillParentIndex == 0) ? B : A;

	UGF_CreatureSpeciesData* BabySpecies = ResolveEggSpecies(WorldContextObject, A, B);
	if (!BabySpecies)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildEgg: could not resolve the baby species."));
		return Egg;
	}

	// Level 5 with the moves the species naturally knows at level 5 — the egg
	// moveset is layered on top of that below.
	Egg.Initialize(BabySpecies, 5, TArray<TSubclassOf<AGF_SkillDefinition>>(), OTName, OTID);

	InheritAPs(Mother, SkillParent, Egg);
	InheritTemperament(Mother, Egg);
	BuildEggSkillset(SkillParent, OtherParent, BabySpecies, Egg);

	// Eggs are always found in a plain Core in classic.
	Egg.CaughtCoreName = FName("Core");

	// Fill in real stats now rather than leaving Initialize's MaxHP = 0 sentinel.
	// An egg sits in the party like any other member, so every widget that draws
	// an HP bar will divide CurrentHP by MaxHP -- a zero there is a divide-by-zero
	// the moment the party screen opens.
	if (UGF_CreatureManagerSubsystem* Manager = GetManager(WorldContextObject))
	{
		Manager->RecalculateStats(Egg);
		Egg.CurrentHP = Egg.MaxHP;
	}

	// Shininess is deliberately NOT decided here — it's rolled in HatchEggInstance so a
	// player can stockpile eggs, hatch them one at a time, and reset if none of them
	// come out unique. Initialize() already rolled it, so clear that result.
	Egg.bIsUnique = false;
	Egg.EggUniqueRolls = GetForeignPairUniqueRolls(A, B);

	Egg.bIsEgg = true;
	Egg.EggSpeciesName = BabySpecies->SpeciesName;
	Egg.EggCyclesRemaining = FMath::Max(1, BabySpecies->EggCycles);
	Egg.Affinity = 0;
	Egg.StatusCondition = EGF_STATUSEffect::None;

	// An egg carries the downed flag so every battle check skips it — see
	// FGF_CreatureInstanceData::NormalizeEggState. HatchEggInstance clears it.
	Egg.NormalizeEggState();

	UE_LOG(LogTemp, Log, TEXT("BuildEgg: created a %s egg (%d cycles, %d moves, %d unique roll(s))"),
		*BabySpecies->SpeciesName.ToString(), Egg.EggCyclesRemaining, Egg.Skills.Num(), Egg.EggUniqueRolls);

	return Egg;
}

int32 UGF_BreedingLibrary::GetForeignPairUniqueRolls(const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B)
{
	// The real ForeignPair method keys off the parents' game LANGUAGE. There's no language
	// on a Creature here, so this uses the nearest thing the data has: parents that came
	// from two different tamers. Breeding something you caught with something you were
	// given or traded for is what earns the bonus.
	const bool bDifferentTamers = (A.OriginalTamerID != B.OriginalTamerID);

	return 1 + (bDifferentTamers ? ForeignPairBonusRolls : 0);
}

int32 UGF_BreedingLibrary::GetEggUniqueRolls(const UObject* WorldContextObject, const FGF_CreatureInstanceData& Egg)
{
	int32 Rolls = FMath::Max(1, Egg.EggUniqueRolls);

	if (const UGF_CreatureManagerSubsystem* Manager = GetManager(WorldContextObject))
	{
		if (Manager->Inventory && Manager->Inventory->HasItemByName(EGF_ItemCategory::KeyItems, UniqueCharmItemName))
		{
			Rolls += UniqueCharmBonusRolls;
		}
	}

	return Rolls;
}

bool UGF_BreedingLibrary::HatchEggInstance(const UObject* WorldContextObject, FGF_CreatureInstanceData& Egg)
{
	if (!Egg.bIsEgg)
	{
		return false;
	}

	// --- Unique is decided HERE, not when the egg was created. Stockpiling eggs and
	// hatching them one at a time is therefore a real strategy: each hatch is its own
	// roll, and reloading a save from before the hatch rerolls it.
	const int32 UniqueRolls = GetEggUniqueRolls(WorldContextObject, Egg);

	Egg.bIsUnique = false;
	for (int32 Roll = 0; Roll < UniqueRolls; Roll++)
	{
		if (FMath::RandRange(1, UniqueOddsDenominator) == 1)
		{
			Egg.bIsUnique = true;
			break;
		}
	}

	Egg.bIsEgg = false;
	Egg.EggCyclesRemaining = 0;
	Egg.EggUniqueRolls = 1;

	// A hatched Creature starts noticeably friendlier than a caught one.
	Egg.Affinity = 0;
	Egg.Level = FMath::Max(5, Egg.Level);
	Egg.CurrentEXP = 0;
	Egg.bIsDowned = false;
	Egg.StatusCondition = EGF_STATUSEffect::None;
	Egg.SleepCounter = 0;

	// The egg already carried real stats, so this just re-derives them (harmless,
	// and it covers an egg saved before stats were filled in at creation).
	// RecalculateStats preserves the HP percentage, which is 100% for an egg, so
	// the newborn comes out at full health.
	if (UGF_CreatureManagerSubsystem* Manager = GetManager(WorldContextObject))
	{
		Manager->RecalculateStats(Egg);

		// An egg written by an older build could still have MaxHP = 0.
		if (Egg.MaxHP > 0.f && Egg.CurrentHP <= 0.f)
		{
			Egg.CurrentHP = Egg.MaxHP;
		}
	}

	// An egg carries the moveset it was built with, so an egg created while its species
	// asset still had an empty (or too-high) level-up learnset hatches moveless — and no
	// amount of fixing the asset afterwards repairs an egg already sitting in a save.
	// Rebuild it here from the learnset as it exists NOW, so correcting the data is enough.
	if (Egg.Skills.Num() == 0)
	{
		TArray<TSoftClassPtr<AGF_SkillDefinition>> Learned;
		for (int32 AtLevel = 1; AtLevel <= Egg.Level; AtLevel++)
		{
			Learned.Append(Egg.GetSkillsToLearnAtLevel(AtLevel));
		}

		// FLOOR: an egg hatches at level 5, and seven species learn nothing that early
		// (Cindercrawl's earliest is Ember at 8, Duskwing's is 6). Fall back to the species'
		// earliest move so a hatchling is never unable to battle. Same rule Initialize uses.
		if (Learned.Num() == 0)
		{
			Learned = Egg.GetEarliestLearnableSkills();
			if (Learned.Num() > 0)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("HatchEggInstance: %s learns nothing by level %d - granted its earliest move(s) instead."),
					*Egg.GetDisplayName().ToString(), Egg.Level);
			}
		}

		// Keep the four most recently learned, oldest first — same rule Initialize uses.
		const int32 StartIdx = FMath::Max(0, Learned.Num() - 4);
		for (int32 i = StartIdx; i < Learned.Num(); i++)
		{
			Egg.LearnSkill(Learned[i]);
		}

		if (Egg.Skills.Num() > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("HatchEggInstance: %s hatched with an empty moveset - rebuilt %d move(s) from the "
					 "level-%d learnset. The egg was created before its species asset had one."),
				*Egg.GetDisplayName().ToString(), Egg.Skills.Num(), Egg.Level);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("HatchEggInstance: %s hatched with NO moves - no usable entry in LearnableSkills at "
					 "learnset to rebuild from (LearnableSkills is empty - Tome moves don't count). "
					 "It cannot battle until that asset is filled in."),
				*Egg.GetDisplayName().ToString());
		}
	}

	Egg.NormalizeUses();

	// Tamer memo. Deliberately a full re-stamp, not "if unset": the egg's memo said
	// where it was RECEIVED, and once it hatches the memo should say where it hatched,
	// which is what the games show. Level is forced to the hatch level rather than the
	// egg's stored one so it always reads "Egg hatched." against a sane number.
	UGF_CreatureMemoLibrary::StampMetInfo(WorldContextObject, Egg, EGF_CreatureMetType::Hatched, Egg.Level);

	UE_LOG(LogTemp, Log, TEXT("HatchEggInstance: %s hatched at level %d (%d unique roll(s), unique=%s)"),
		*Egg.GetDisplayName().ToString(), Egg.Level, UniqueRolls,
		Egg.bIsUnique ? TEXT("YES") : TEXT("no"));

	return true;
}
