// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_CreatureTraits.h"

// UPDATED: Now accepts tamer parameters
void FGF_CreatureInstanceData::Initialize(UGF_CreatureSpeciesData* InSpeciesData, int32 InLevel,
    const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills,
    FName TamerName, int32 TamerID)
{
	if (!InSpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot initialize Creature: SpeciesData is null!"));
		return;
	}

	// Store species reference
	SpeciesData = InSpeciesData;
	Level = InLevel;

	// Generate unique ID
	CreatureID = FMath::RandRange(0, 999999);
	UniqueID = FGuid::NewGuid();

	// ✅ SET TAMER INFO DURING INITIALIZATION
	if (!TamerName.IsNone() && TamerID != 0)
	{
		OriginalTamerName = TamerName;
		OriginalTamerID = TamerID;
		CurrentTamerName = TamerName;
		CurrentTamerID = TamerID;

		UE_LOG(LogTemp, Log, TEXT("Set tamer info during Initialize: %s (ID: %d)"),
		    *TamerName.ToString(), TamerID);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Creature initialized without tamer info - will be set later"));
	}

	// Generate random Potentials
	HP_Potential = FMath::RandRange(0, 31);
	Attack_Potential = FMath::RandRange(0, 31);
	Defense_Potential = FMath::RandRange(0, 31);
	Magic_Potential = FMath::RandRange(0, 31);
	Poise_Potential = FMath::RandRange(0, 31);
	Speed_Potential = FMath::RandRange(0, 31);

	// Training start at 0
	HP_Training = 0;
	Attack_Training = 0;
	Defense_Training = 0;
	Magic_Training = 0;
	Poise_Training = 0;
	Speed_Training = 0;

	// Random Temperament
	int32 TemperamentCount = static_cast<int32>(EGF_Temperament::MAX);
	int32 RandomTemperamentIndex = FMath::RandRange(0, TemperamentCount - 1);
	Temperament = static_cast<EGF_Temperament>(RandomTemperamentIndex);

	// Determine Gender
	if (InSpeciesData->bIsGenderless)
	{
		Gender = EGF_CreatureGender::Genderless;
	}
	else
	{
		float Roll = FMath::FRandRange(0.0f, 100.0f);
		Gender = (Roll <= InSpeciesData->MaleRatio) ? EGF_CreatureGender::Male : EGF_CreatureGender::Female;
	}

	// Unique chance (1/4096)
	bIsUnique = FMath::RandRange(1, 4096) == 1;

	// Trait — 50/50 between the species' two slots, or slot 0 when it only has one.
	// Baked in here so editing the species asset later can't retroactively change
	// a Creature the player already owns.
	Trait = UGF_CreatureTraitLibrary::RollSpeciesTrait(InSpeciesData, TraitSlot);

	// HP is intentionally left at 0 here — real MaxHP is calculated by RefreshStats()
	// inside InitializeFromInstanceData(). A value of 0 acts as a sentinel so
	// InitializeFromInstanceData knows to set CurrentHP = MaxHP (first spawn = full HP)
	// rather than restoring a saved value.
	MaxHP = 0;
	CurrentHP = 0;

	// ============================================
	// MOVES
	// ============================================
	Skills.Empty();
	CurrentUses.Empty();
	MaxUses.Empty();

	if (StartingSkills.Num() > 0)
	{
		// Use provided starting moves (for starters, eggs, gifts)
		UE_LOG(LogTemp, Warning, TEXT("Initializing %s with %d starting moves"),
			*InSpeciesData->SpeciesName.ToString(), StartingSkills.Num());

		for (int32 i = 0; i < FMath::Min(StartingSkills.Num(), 4); i++)
		{
			if (StartingSkills[i])
			{
				// Convert hard reference (TSubclassOf) to soft reference (TSoftClassPtr)
				TSoftClassPtr<AGF_SkillDefinition> SoftMove(StartingSkills[i]);
				Skills.Add(SoftMove);
				// Read Uses from move CDO
				int32 SkillUses = 10;
				if (const AGF_SkillDefinition* CDO = StartingSkills[i]->GetDefaultObject<AGF_SkillDefinition>())
				{
					SkillUses = CDO->MaxUses;
				}
				CurrentUses.Add(SkillUses);
				MaxUses.Add(SkillUses);

				UE_LOG(LogTemp, Warning, TEXT("Added move %d"), i);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Total moves added: %d"), Skills.Num());
	}
	else
	{
		// Learn moves naturally up to current level
		UE_LOG(LogTemp, Warning, TEXT("Learning moves naturally for %s up to level %d"),
			*InSpeciesData->SpeciesName.ToString(), InLevel);

		// Get all moves that should be known at this level
		TArray<FGF_LearnableSkill> ValidSkills;
		for (const FGF_LearnableSkill& LearnableSkill : InSpeciesData->LearnableSkills)
		{
			if (LearnableSkill.LearnLevel <= InLevel && !LearnableSkill.Skill.IsNull())
    {

        ValidSkills.Add(LearnableSkill);

        UE_LOG(LogTemp, Log, TEXT("   Found learnable move at level %d"),
            LearnableSkill.LearnLevel);
    }
		}

		// StableSort ascending — equal LearnLevel entries keep their definition order
		// (e.g. Ram and Bluster both at level 1: Ram stays first because it's first in the array)
		ValidSkills.StableSort([](const FGF_LearnableSkill& A, const FGF_LearnableSkill& B) {
			return A.LearnLevel < B.LearnLevel;
		});

		// Take the last up to 4 moves (most recently learned), keeping oldest-first order
		// e.g. Ram (L1), Bluster (L1), Siphon (L6) → slots 0,1,2 in that order
		const int32 StartIdx = FMath::Max(0, ValidSkills.Num() - 4);
		for (int32 i = StartIdx; i < ValidSkills.Num(); i++)
		{
			Skills.Add(ValidSkills[i].Skill);

			// Read Uses from move CDO
			int32 SkillUses = 10;
			if (TSubclassOf<AGF_SkillDefinition> LoadedClass = ValidSkills[i].Skill.LoadSynchronous())
			{
				if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
				{
					SkillUses = CDO->MaxUses;
				}
			}
			CurrentUses.Add(SkillUses);
			MaxUses.Add(SkillUses);
		}

		// FLOOR: nothing learnable at or below this level. Seven species are in that
		// position -- Cindercrawl's earliest is Ember at 8, Duskwing's is 6, Ironmote has none at
		// any level -- normally because the low-level move has no Blueprint yet and the
		// populate script skips it. Grant the earliest move the species DOES define
		// rather than hand back a Creature that cannot take a turn.
		if (Skills.Num() == 0)
		{
			FGF_CreatureInstanceData Probe;
			Probe.SpeciesData = InSpeciesData;
			for (const TSoftClassPtr<AGF_SkillDefinition>& Fallback : Probe.GetEarliestLearnableSkills())
			{
				if (Skills.Num() >= 4) break;

				Skills.Add(Fallback);
				int32 SkillUses = 10;
				if (TSubclassOf<AGF_SkillDefinition> LoadedClass = Fallback.LoadSynchronous())
				{
					if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
					{
						SkillUses = CDO->MaxUses;
					}
				}
				CurrentUses.Add(SkillUses);
				MaxUses.Add(SkillUses);
			}

			if (Skills.Num() > 0)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Initialize: %s learns nothing by level %d - granted its earliest move(s) instead "
						 "so it can battle. Add a low-level move to its learnset to fix this properly."),
					*InSpeciesData->SpeciesName.ToString(), InLevel);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Learned %d moves naturally"), Skills.Num());

		// A Creature with no moves cannot take a turn, so this is never survivable — it
		// reaches the player as a battler that soft-locks the fight. It always means the
		// species asset's level-up learnset (LearnableSkills) is empty or starts above this
		// level; LearnableTomeMoves does NOT count. Loud on purpose: the symptom shows up
		// much later, at the hatch or the battle, far from the asset that caused it.
		if (Skills.Num() == 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("Initialize: %s has NO usable level-up move at or below level %d, and no earliest-move "
					 "fallback either - every entry in LearnableSkills has a null Skill, or the list is empty "
					 "(Tome moves don't count). It will be created with zero moves and cannot battle."),
				*InSpeciesData->SpeciesName.ToString(), InLevel);
		}
	}

	// No status conditions
	StatusCondition = EGF_STATUSEffect::None;
	SleepCounter = 0;
	bIsDowned = false;

	UE_LOG(LogTemp, Warning, TEXT("Initialized Creature: %s (Level %d, ID: %d, Skills: %d, Tamer: %s)"),
		*InSpeciesData->SpeciesName.ToString(), Level, CreatureID, Skills.Num(),
		*OriginalTamerName.ToString());
}

FName FGF_CreatureInstanceData::GetDisplayName() const
{
	// An egg never reveals what's inside — not even a nickname, since eggs
	// can't be nicknamed until they hatch.
	if (bIsEgg)
	{
		return FName("Egg");
	}

	if (!Nickname.IsNone())
	{
		return Nickname;
	}

	// Use IsNull() not IsValid() - IsValid() requires the asset to already be in memory,
	// which fails after save/load and returns "Unknown" even when the path is valid.
	if (!SpeciesData.IsNull())
	{
		UGF_CreatureSpeciesData* LoadedData = SpeciesData.LoadSynchronous();
		if (LoadedData)
		{
			return LoadedData->SpeciesName;
		}

	}

	return FName("Unknown");
}

void FGF_CreatureInstanceData::NormalizeEggState()
{
    if (bIsEgg)
    {
        bIsDowned = true;
    }
}

void FGF_CreatureInstanceData::NormalizeUses()
{
    for (int32 i = 0; i < Skills.Num(); i++)
    {
        if (Skills[i].IsNull()) continue;

        int32 SkillUses = 10; // fallback
        if (TSubclassOf<AGF_SkillDefinition> LoadedClass = Skills[i].LoadSynchronous())
        {
            if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
            {
                SkillUses = CDO->MaxUses;
            }
        }

        // Fill a MISSING CurrentUses entry only.
        //
        // 0 is a legitimate value — it's exactly what a move looks like once its last
        // Uses is spent. This used to treat 0 as a "never initialised" sentinel and refill
        // to max, which meant spent Uses silently came back (UGF_VaultSystem::GetPartyCreature
        // calls this on every read), Ethers were pointless, and LastResort could never
        // trigger because no move ever stayed empty. The genuinely-uninitialised case is
        // already covered by the missing-entry branch.
        if (!CurrentUses.IsValidIndex(i))
        {
            CurrentUses.Add(SkillUses);
        }

        // Fill missing MaxUses entry. A MaxUses of 0 IS invalid — it would make every move
        // read as permanently full — so that one is still repaired from the CDO.
        if (!MaxUses.IsValidIndex(i))
        {
            MaxUses.Add(SkillUses);
        }
        else if (MaxUses[i] <= 0)
        {
            MaxUses[i] = SkillUses;
        }

        // Keep CurrentUses inside [0, MaxUses]: catches negatives from bad edits, and the
        // case where MaxUses shrank (or was just repaired above) below the stored current.
        CurrentUses[i] = FMath::Clamp(CurrentUses[i], 0, MaxUses[i]);
    }
}

bool FGF_CreatureInstanceData::LearnSkill(TSoftClassPtr<AGF_SkillDefinition> NewSkill, bool bReplaceSkill, int32 ReplaceIndex)
{
    if (NewSkill.IsNull())
    {
        return false;
    }

    // Check if already knows this move
    if (KnowsSkill(NewSkill))
    {
        UE_LOG(LogTemp, Warning, TEXT("%s already knows this move!"), *GetDisplayName().ToString());
        return false;
    }

    // Resolve Uses from the move CDO — fall back to 10 if not loaded
    int32 SkillUses = 10;
    if (TSubclassOf<AGF_SkillDefinition> LoadedClass = NewSkill.LoadSynchronous())
    {
        if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
        {
            SkillUses = CDO->MaxUses;
        }
    }

    // If has space, just add it
    if (Skills.Num() < 4)
    {
        Skills.Add(NewSkill);
        CurrentUses.Add(SkillUses);
        MaxUses.Add(SkillUses);

        UE_LOG(LogTemp, Log, TEXT("%s learned a new move! (Slot %d) Uses: %d"), *GetDisplayName().ToString(), Skills.Num() - 1, SkillUses);
        return true;
    }

    // If full and we want to replace
    if (bReplaceSkill && Skills.IsValidIndex(ReplaceIndex))
    {
        Skills[ReplaceIndex] = NewSkill;
        CurrentUses[ReplaceIndex] = SkillUses;
        MaxUses[ReplaceIndex] = SkillUses;

        UE_LOG(LogTemp, Log, TEXT("%s replaced a move at slot %d Uses: %d"), *GetDisplayName().ToString(), ReplaceIndex, SkillUses);
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("%s cannot learn move - move list full!"), *GetDisplayName().ToString());
    return false;
}

bool FGF_CreatureInstanceData::ForgetSkill(int32 SkillIndex)
{
    if (!Skills.IsValidIndex(SkillIndex))
    {
        return false;
    }

    Skills.RemoveAt(SkillIndex);
    CurrentUses.RemoveAt(SkillIndex);
    MaxUses.RemoveAt(SkillIndex);

    UE_LOG(LogTemp, Log, TEXT("%s forgot a move"), *GetDisplayName().ToString());
    return true;
}

bool FGF_CreatureInstanceData::KnowsSkill(TSoftClassPtr<AGF_SkillDefinition> Skill) const
{
    for (const TSoftClassPtr<AGF_SkillDefinition>& ExistingSkill : Skills)
    {
        if (ExistingSkill == Skill)
        {
            return true;
        }
    }
    return false;
}

TArray<TSoftClassPtr<AGF_SkillDefinition>> FGF_CreatureInstanceData::GetEarliestLearnableSkills() const
{
    TArray<TSoftClassPtr<AGF_SkillDefinition>> Earliest;

    if (SpeciesData.IsNull())
    {
        return Earliest;
    }

    UGF_CreatureSpeciesData* Species = SpeciesData.LoadSynchronous();
    if (!Species)
    {
        return Earliest;
    }

    int32 LowestLevel = TNumericLimits<int32>::Max();
    for (const FGF_LearnableSkill& LearnableSkill : Species->LearnableSkills)
    {
        if (!LearnableSkill.Skill.IsNull())
        {
            LowestLevel = FMath::Min(LowestLevel, LearnableSkill.LearnLevel);
        }
    }

    if (LowestLevel == TNumericLimits<int32>::Max())
    {
        return Earliest;   // species genuinely has no usable move at any level
    }

    for (const FGF_LearnableSkill& LearnableSkill : Species->LearnableSkills)
    {
        if (!LearnableSkill.Skill.IsNull() && LearnableSkill.LearnLevel == LowestLevel)
        {
            Earliest.Add(LearnableSkill.Skill);
        }
    }

    return Earliest;
}

TArray<TSoftClassPtr<AGF_SkillDefinition>> FGF_CreatureInstanceData::GetSkillsToLearnAtLevel(int32 AtLevel) const
{
    TArray<TSoftClassPtr<AGF_SkillDefinition>> NewSkills;

    // IsNull(), not IsValid(): IsValid() means "already loaded into memory", so this
    // rejected any species that simply had not been loaded yet and returned an empty
    // learnset. Harmless while boot preloaded all 118 species -- it was always true --
    // but with species loaded on demand it made a hatched Creature come out with no
    // moves. LoadSynchronous two lines down was always the intent.
    if (SpeciesData.IsNull())
    {
        return NewSkills;
    }

    UGF_CreatureSpeciesData* Species = SpeciesData.LoadSynchronous();
    if (!Species)
    {
        return NewSkills;
    }

    // Check all learnable moves for this level
    for (const FGF_LearnableSkill& LearnableSkill : Species->LearnableSkills)
    {
        if (LearnableSkill.LearnLevel == AtLevel)
        {
            NewSkills.Add(LearnableSkill.Skill);
        }
    }

    return NewSkills;
}


void FGF_CreatureInstanceData::GiveHeldItem(FName ItemName)
{
    if (!HeldItem.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("%s is already holding %s!"),
            *GetDisplayName().ToString(), *HeldItem.ToString());
        return;
    }

    HeldItem = ItemName;
    UE_LOG(LogTemp, Log, TEXT("%s is now holding %s"),
        *GetDisplayName().ToString(), *ItemName.ToString());
}

FName FGF_CreatureInstanceData::TakeHeldItem()
{
    if (HeldItem.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("%s is not holding any item!"),
            *GetDisplayName().ToString());
        return NAME_None;
    }

    FName RemovedItem = HeldItem;
    HeldItem = NAME_None;

    UE_LOG(LogTemp, Log, TEXT("Took %s from %s"),
        *RemovedItem.ToString(), *GetDisplayName().ToString());

    return RemovedItem;
}