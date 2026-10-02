// Fill out your copyright notice in the Description page of Project Settings.


#include "GF_Creature.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DClasses.h"
#include "Paper2DModule.h"
#include <iostream>
#include <string>
#include <cmath>
#include "Kismet/KismetMathLibrary.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureTraits.h"
#include "GF_CreatureStatStageComponent.h"








// Sets default values
AGF_Creature::AGF_Creature()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;



}



// Called when the game starts or when spawned
void AGF_Creature::BeginPlay()
{
	Super::BeginPlay();
	CreateAPs();
	GenerateStats();


}

// Called every frame
void AGF_Creature::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

#pragma region Creature Stat Core

//-----------------SET TEMPERAMENT------------------//

EGF_Temperament AGF_Creature::SetRandomTemperament()
{
	int32 natureCount = static_cast<int32>(EGF_Temperament::MAX);
	int32 RandomIndex = UKismetMathLibrary::RandomInteger(natureCount);



	return static_cast<EGF_Temperament>(RandomIndex);

}

void AGF_Creature::CreateRandomTemperament()
{
	CreatureTemperament = SetRandomTemperament();
}

//-----------------SET GENDER------------------//

void AGF_Creature::SetRandomGender()
{
	// DEBUG: Check if SpeciesData exists
	if (SpeciesData)
	{
		UE_LOG(LogTemp, Warning, TEXT("Using SpeciesData: %s, IsGenderless: %s, MaleRatio: %.1f"),
			*SpeciesData->SpeciesName.ToString(),
			SpeciesData->bIsGenderless ? TEXT("YES") : TEXT("NO"),
			SpeciesData->MaleRatio);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SpeciesData is NULL! Using fallback gender system."));
	}

	// Check if using Data Asset
	if (SpeciesData)
	{
		if (SpeciesData->bIsGenderless)
		{
			Gender = EGF_CreatureGender::Genderless;
			UE_LOG(LogTemp, Warning, TEXT("Set Gender to: Genderless"));
		}
		else
		{
			// Use MaleRatio from Data Asset
			float Roll = FMath::FRandRange(0.0f, 100.0f);
			if (Roll <= SpeciesData->MaleRatio)
			{
				Gender = EGF_CreatureGender::Male;
				UE_LOG(LogTemp, Warning, TEXT("Set Gender to: Male (Roll: %.1f)"), Roll);
			}
			else
			{
				Gender = EGF_CreatureGender::Female;
				UE_LOG(LogTemp, Warning, TEXT("Set Gender to: Female (Roll: %.1f)"), Roll);
			}
		}
	}
	else
	{
		// Fallback to old system if no SpeciesData
		UE_LOG(LogTemp, Warning, TEXT("Using old gender system"));
		if (!isGenderless)
		{
			int32 genders = FMath::RandRange(0, 1);
			if (genders == 0)
			{
				Gender = EGF_CreatureGender::Male;
			}
			else
			{
				Gender = EGF_CreatureGender::Female;
			}
		}
		else
		{
			Gender = EGF_CreatureGender::Genderless;
		}
	}

}

enum class EGF_Stat {
	HP,
	Attack,
	Defense,
	Magic,
	Poise,
	Speed
};

struct Temperament
{
	EGF_Temperament type;
	std::string name;
	EGF_Stat increasedStat;
	EGF_Stat decreasedStat;
};


//-----------------INIT TEMPERAMENT MODS------------------//

const std::map<EGF_Temperament, Temperament> TemperamentData =
{
	{ EGF_Temperament::Ferocious, { EGF_Temperament::Ferocious, "Ferocious", EGF_Stat::Attack, EGF_Stat::Magic } },
	{ EGF_Temperament::Meek, { EGF_Temperament::Meek, "Meek", EGF_Stat::Magic, EGF_Stat::Magic } },
	{ EGF_Temperament::Stalwart,    { EGF_Temperament::Stalwart,    "Stalwart",    EGF_Stat::Defense, EGF_Stat::Attack } },
	{ EGF_Temperament::Valiant,   { EGF_Temperament::Valiant,   "Valiant",   EGF_Stat::Attack, EGF_Stat::Speed } },
	{ EGF_Temperament::Serene,    { EGF_Temperament::Serene,    "Serene",    EGF_Stat::Poise, EGF_Stat::Attack } },
	{ EGF_Temperament::Guarded, { EGF_Temperament::Guarded, "Guarded", EGF_Stat::Poise, EGF_Stat::Magic } },
	{ EGF_Temperament::Placid,  { EGF_Temperament::Placid,  "Placid",  EGF_Stat::Defense, EGF_Stat::Defense } },
	{ EGF_Temperament::Tender,  { EGF_Temperament::Tender,  "Tender",  EGF_Stat::Poise, EGF_Stat::Defense } },
	{ EGF_Temperament::Robust,   { EGF_Temperament::Robust,   "Robust",   EGF_Stat::Attack, EGF_Stat::Attack } },
	{ EGF_Temperament::Rushed,   { EGF_Temperament::Rushed,   "Rushed",   EGF_Stat::Speed, EGF_Stat::Defense } },
	{ EGF_Temperament::Wily,  { EGF_Temperament::Wily,  "Wily",  EGF_Stat::Defense, EGF_Stat::Magic } },
	{ EGF_Temperament::Sprightly,   { EGF_Temperament::Sprightly,   "Sprightly",   EGF_Stat::Speed, EGF_Stat::Magic } },
	{ EGF_Temperament::Slack,     { EGF_Temperament::Slack,     "Slack",     EGF_Stat::Defense, EGF_Stat::Poise } },
	{ EGF_Temperament::Solitary,  { EGF_Temperament::Solitary,  "Solitary",  EGF_Stat::Attack, EGF_Stat::Defense } },
	{ EGF_Temperament::Temperate,    { EGF_Temperament::Temperate,    "Temperate",    EGF_Stat::Magic, EGF_Stat::Defense } },
	{ EGF_Temperament::Humble,  { EGF_Temperament::Humble,  "Humble",  EGF_Stat::Magic, EGF_Stat::Attack } },
	{ EGF_Temperament::Innocent,   { EGF_Temperament::Innocent,   "Innocent",   EGF_Stat::Speed, EGF_Stat::Poise } },
	{ EGF_Temperament::Unruly, { EGF_Temperament::Unruly, "Unruly", EGF_Stat::Attack, EGF_Stat::Poise } },
	{ EGF_Temperament::Silent,   { EGF_Temperament::Silent,   "Silent",   EGF_Stat::Magic, EGF_Stat::Speed } },
	{ EGF_Temperament::Peculiar,  { EGF_Temperament::Peculiar,  "Peculiar",  EGF_Stat::Poise, EGF_Stat::Poise } },
	{ EGF_Temperament::Reckless,    { EGF_Temperament::Reckless,    "Reckless",    EGF_Stat::Magic, EGF_Stat::Poise } },
	{ EGF_Temperament::Languid, { EGF_Temperament::Languid, "Languid", EGF_Stat::Defense, EGF_Stat::Speed } },
	{ EGF_Temperament::Brash,   { EGF_Temperament::Brash,   "Brash",   EGF_Stat::Poise, EGF_Stat::Speed } },
	{ EGF_Temperament::Stoic, { EGF_Temperament::Stoic, "Stoic", EGF_Stat::Speed, EGF_Stat::Speed } },
	{ EGF_Temperament::Skittish,   { EGF_Temperament::Skittish,   "Skittish",   EGF_Stat::Speed, EGF_Stat::Attack } }
};


const Temperament& GetTemperament(EGF_Temperament type)
{
	return TemperamentData.at(type); // Throws std::out_of_range if invalid, use with care
}


float GetTemperamentModifier(const Temperament& nature, EGF_Stat stat)
{


	if (stat == nature.increasedStat)
		return 1.1f;
	else if (stat == nature.decreasedStat)
		return 0.9f;
	else
		return 1.0f;


}

#pragma endregion



float AGF_Creature::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, class AActor* DamageCauser)
{
	const float OldHP = CurrentStats.CurrentHP;

	CurrentStats.CurrentHP -= DamageAmount;
	if (CurrentStats.CurrentHP < 0)
	{
		CurrentStats.CurrentHP = 0;
	}

	// Damage first, so a hurt reaction starts on the same frame the bar moves.
	NotifyDamageTaken(DamageAmount, /*bWasCritical*/ false);
	NotifyHealthChanged(OldHP, CurrentStats.CurrentHP);

	return DamageAmount;
}

void AGF_Creature::NotifyHealthChanged(float OldHP, float NewHP)
{
	OnHealthChanged(OldHP, NewHP);

	// Crossing the low-HP line is the one case where the resting animation has
	// to change on its own. Raised here so a subclass never polls for it.
	const bool bWasLow = (CurrentStats.MaxHP > 0.f) && (OldHP / CurrentStats.MaxHP) <= LowHPAnimationThreshold;
	const bool bIsLow  = IsAtLowHP();
	if (bWasLow != bIsLow && NewHP > 0.f && !IsPlayingOneShotAnimation())
	{
		// Not while a one-shot is playing. The HP that crosses this line is
		// almost always the damage that started a Hurt animation, so pushing an
		// idle here cuts that animation off two frames in -- the creature
		// flinches and is abruptly standing still. Returning to rest already
		// picks the low-HP idle, so the swap happens on its own a moment later.
		PlayAnimationState(bIsLow ? EGF_CreatureAnimState::IdleLowHP : EGF_CreatureAnimState::Idle);
	}

	if (NewHP <= 0.f && OldHP > 0.f)
	{
		OnCreatureDowned();
	}
}

void AGF_Creature::NotifyDamageTaken(float Amount, bool bWasCritical)
{
	OnDamageTaken(Amount, bWasCritical);
}

void AGF_Creature::NotifyStatStageChanged(EGF_StatStages Stat, int32 ActualChange, bool bAtLimit)
{
	OnStatStageChanged(Stat, ActualChange, bAtLimit);
}

void AGF_Creature::PlayAnimationState(EGF_CreatureAnimState State)
{
	// Recorded so a debugger can show what was REQUESTED beside what is actually
	// playing. A mis-wired Blueprint switch shows a different animation than the
	// one asked for, and without both halves visible that is very hard to see.
	LastAnimStateRequested = State;
	OnPlayAnimationState(State);
}

bool AGF_Creature::IsAtLowHP() const
{
	if (CurrentStats.MaxHP <= 0.0f)
	{
		return false;
	}
	return (CurrentStats.CurrentHP / CurrentStats.MaxHP) <= LowHPAnimationThreshold;
}

UPaperFlipbook* AGF_Creature::GetAnimationForState(EGF_CreatureAnimState State) const
{
	const bool bFemale = (Gender == EGF_CreatureGender::Female);

	// Same fallback chain the species asset applies, run against the copies held
	// on the actor -- the actor is the authority in battle, because a form change
	// can leave it holding art its species never had.
	struct FCandidate
	{
		const TMap<EGF_CreatureAnimState, UPaperFlipbook*>* Set;
	};

	TArray<FCandidate, TInlineAllocator<4>> Order;
	if (bFemale && isUnique) { Order.Add({ &SideAnimationsFemaleUnique }); }
	if (isUnique)            { Order.Add({ &SideAnimationsUnique }); }
	if (bFemale)             { Order.Add({ &SideAnimationsFemale }); }
	Order.Add({ &SideAnimations });

	for (const FCandidate& Candidate : Order)
	{
		if (UPaperFlipbook* const* Found = Candidate.Set->Find(State))
		{
			if (*Found)
			{
				return *Found;
			}
		}

		if (State == EGF_CreatureAnimState::IdleLowHP)
		{
			if (UPaperFlipbook* const* Idle = Candidate.Set->Find(EGF_CreatureAnimState::Idle))
			{
				if (*Idle)
				{
					return *Idle;
				}
			}
		}
	}

	return nullptr;
}

UPaperFlipbook* AGF_Creature::GetIdleAnimation() const
{
	return GetAnimationForState(
		IsAtLowHP() ? EGF_CreatureAnimState::IdleLowHP : EGF_CreatureAnimState::Idle);
}

bool AGF_Creature::IsDowned() const
{
	return CurrentStats.CurrentHP <= 0;
}

void AGF_Creature::Heal(float HealAmount)
{
	CurrentStats.CurrentHP += HealAmount;
	if (CurrentStats.CurrentHP > CurrentStats.MaxHP)
	{
		CurrentStats.CurrentHP = CurrentStats.MaxHP;
	}

}


//Start the AP roll
void AGF_Creature::CreateAPs()
{
	constexpr int32 MaxAP = FGF_CreatureInstanceData::MaxAP;

	APs.HP_AP = FMath::RandRange(0, MaxAP);

	APs.Attack_AP = FMath::RandRange(0, MaxAP);

	APs.Defense_AP = FMath::RandRange(0, MaxAP);

	APs.Magic_AP = FMath::RandRange(0, MaxAP);

	APs.Poise_AP = FMath::RandRange(0, MaxAP);

	APs.Speed_AP = FMath::RandRange(0, MaxAP);

}

//Calculate the Base Stats for the Creature from the APs and EPs
float AGF_Creature::CalculateBaseStats(float BaseStat, int32 AP, int32 EP, int32 Level, float temperamentMod)
{
	int rawVal = static_cast<int>(std::floor(((2 * BaseStat + AP + EP) * Level) / 100.0));

	int withModBonus = rawVal + 5;



	return static_cast<int>(std::floor(withModBonus * temperamentMod));

}

float AGF_Creature::CalculateBaseHP(float BaseStat, int32 AP, int32 EP, int32 Level)
{

	int hp = static_cast<int>(std::floor(((2 * BaseStat + AP + EP) * Level) / 100.0));



	return hp + Level + 10;

}


void AGF_Creature::GenerateStats()
{
    CreateRandomTemperament();

    const Temperament& nature = GetTemperament(CreatureTemperament);
    float atkMod = GetTemperamentModifier(nature, EGF_Stat::Attack);
    float defMod = GetTemperamentModifier(nature, EGF_Stat::Defense);
    float spatkMod = GetTemperamentModifier(nature, EGF_Stat::Magic);
    float spdefMod = GetTemperamentModifier(nature, EGF_Stat::Poise);
    float speedMod = GetTemperamentModifier(nature, EGF_Stat::Speed);

    FGF_CreatureBaseStats SourceStats = SpeciesData ? SpeciesData->BaseStats : InstanceBaseStats;

    CurrentStats.MaxHP = CalculateBaseHP(SourceStats.HP, APs.HP_AP, EPs.HP_EP, InstanceBaseStats.Level);

    // Husk: see UGF_CreatureSpeciesData::HasFixedOneHP.
    if (SpeciesData && SpeciesData->HasFixedOneHP())
    {
        CurrentStats.MaxHP = 1.f;
    }

    CurrentStats.CurrentHP = CurrentStats.MaxHP;

    CurrentStats.Attack = CalculateBaseStats(SourceStats.Attack, APs.Attack_AP, EPs.Attack_EP, InstanceBaseStats.Level, atkMod);
    CurrentStats.Defense = CalculateBaseStats(SourceStats.Defense, APs.Defense_AP, EPs.Defense_EP, InstanceBaseStats.Level, defMod);
    CurrentStats.Magic = CalculateBaseStats(SourceStats.Magic, APs.Magic_AP, EPs.Magic_EP, InstanceBaseStats.Level, spatkMod);
    CurrentStats.Poise = CalculateBaseStats(SourceStats.Poise, APs.Poise_AP, EPs.Poise_EP, InstanceBaseStats.Level, spdefMod);
    CurrentStats.Speed = CalculateBaseStats(SourceStats.Speed, APs.Speed_AP, EPs.Speed_EP, InstanceBaseStats.Level, speedMod);
    CurrentStats.Level = InstanceBaseStats.Level;

    CreatureID = FMath::RandRange(0, 999999);
}


void AGF_Creature::RefreshStats()
{
    const Temperament& nature = GetTemperament(CreatureTemperament);
    float atkMod = GetTemperamentModifier(nature, EGF_Stat::Attack);
    float defMod = GetTemperamentModifier(nature, EGF_Stat::Defense);
    float spatkMod = GetTemperamentModifier(nature, EGF_Stat::Magic);
    float spdefMod = GetTemperamentModifier(nature, EGF_Stat::Poise);
    float speedMod = GetTemperamentModifier(nature, EGF_Stat::Speed);

    FGF_CreatureBaseStats SourceStats = SpeciesData ? SpeciesData->BaseStats : InstanceBaseStats;

    CurrentStats.MaxHP = CalculateBaseHP(SourceStats.HP, APs.HP_AP, EPs.HP_EP, InstanceBaseStats.Level);

    // Husk: see UGF_CreatureSpeciesData::HasFixedOneHP. This is the one that actually
    // broke it -- InitializeFromInstanceData calls RefreshStats() on every send-out and
    // ignores the stored MaxHP, then ExportToInstanceData writes the result back to the
    // party. Without this line the 1 HP survived exactly until the first battle.
    if (SpeciesData && SpeciesData->HasFixedOneHP())
    {
        CurrentStats.MaxHP = 1.f;
    }

    CurrentStats.Attack = CalculateBaseStats(SourceStats.Attack, APs.Attack_AP, EPs.Attack_EP, InstanceBaseStats.Level, atkMod);
    CurrentStats.Defense = CalculateBaseStats(SourceStats.Defense, APs.Defense_AP, EPs.Defense_EP, InstanceBaseStats.Level, defMod);
    CurrentStats.Magic = CalculateBaseStats(SourceStats.Magic, APs.Magic_AP, EPs.Magic_EP, InstanceBaseStats.Level, spatkMod);
    CurrentStats.Poise = CalculateBaseStats(SourceStats.Poise, APs.Poise_AP, EPs.Poise_EP, InstanceBaseStats.Level, spdefMod);
    CurrentStats.Speed = CalculateBaseStats(SourceStats.Speed, APs.Speed_AP, EPs.Speed_EP, InstanceBaseStats.Level, speedMod);
    CurrentStats.Level = InstanceBaseStats.Level;
}

const TArray<TSubclassOf<AGF_SkillDefinition>>& AGF_Creature::GetSkills() const
{
	return Skills;
}

//--------------------
// DATA INITIALIZATION FUNCTIONS
//--------------------

void AGF_Creature::InitializeFromInstanceData(const FGF_CreatureInstanceData& InstanceData)
{
	if (!InstanceData.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot initialize from invalid instance data!"));
		return;
	}


	// Load species data
	SpeciesData = InstanceData.SpeciesData.LoadSynchronous();
	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load species data!"));
		return;
	}

	// Set basic info
	Name = InstanceData.GetDisplayName();
	CreatureID = InstanceData.CreatureID;
	UniqueID = InstanceData.UniqueID;
	Gender = InstanceData.Gender;
	CreatureTemperament = InstanceData.Temperament;
	isUnique = InstanceData.bIsUnique;
	isDowned = InstanceData.bIsDowned;

	CaughtCoreName = InstanceData.CaughtCoreName;

    UE_LOG(LogTemp, Log, TEXT("InitializeFromInstanceData: Set CaughtCoreName to '%s'"),
    *CaughtCoreName.ToString());

	// Set tamer info
	OriginalTamerName = InstanceData.OriginalTamerName;
	OriginalTamerID = InstanceData.OriginalTamerID;
	TamerName = InstanceData.CurrentTamerName;
	CurrentTamerID = InstanceData.CurrentTamerID;



	// Set APs
	APs.HP_AP = InstanceData.HP_AP;
	APs.Attack_AP = InstanceData.Attack_AP;
	APs.Defense_AP = InstanceData.Defense_AP;
	APs.Magic_AP = InstanceData.Magic_AP;
	APs.Poise_AP = InstanceData.Poise_AP;
	APs.Speed_AP = InstanceData.Speed_AP;

	// Set EPs
	EPs.HP_EP = InstanceData.HP_EP;
	EPs.Attack_EP = InstanceData.Attack_EP;
	EPs.Defense_EP = InstanceData.Defense_EP;
	EPs.Magic_EP = InstanceData.Magic_EP;
	EPs.Poise_EP = InstanceData.Poise_EP;
	EPs.Speed_EP = InstanceData.Speed_EP;

	// Set level and EXP
	InstanceBaseStats.Level = InstanceData.Level;
	CurrentEXP = InstanceData.CurrentEXP;
	EXPCurves = SpeciesData->ExpCurve;
	BaseEXP = SpeciesData->BaseEXP;

	// Set types from species data
	PrimaryElement = SpeciesData->PrimaryElement;
	SecondaryElement = SpeciesData->SecondaryElement;
	Weakness = SpeciesData->Weaknesses;
	Resistant = SpeciesData->Resistances;
	Immune = SpeciesData->Immunities;

	// Load moves
	// NOTE: Use IsNull() not IsValid() - IsValid() requires the asset to already be in memory,
	// which fails after save/load, silently skipping all moves → "Creature used None".
	// IsNull() just checks if the path is set, then LoadSynchronous forces the actual load.
	Skills.Empty();
	SkillUses.Empty();
	for (int32 i = 0; i < InstanceData.Skills.Num(); i++)
	{
		if (!InstanceData.Skills[i].IsNull())
		{
			TSubclassOf<AGF_SkillDefinition> LoadedSkill = InstanceData.Skills[i].LoadSynchronous();
			if (LoadedSkill)
			{
				Skills.Add(LoadedSkill);

				FGF_SkillUses Uses;
				Uses.CurrentUses = InstanceData.CurrentUses.IsValidIndex(i) ? InstanceData.CurrentUses[i] : 10;
				Uses.MaxUses = InstanceData.MaxUses.IsValidIndex(i) ? InstanceData.MaxUses[i] : 10;
				SkillUses.Add(Uses);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("InitializeFromInstanceData: Skill %d failed to load from path: %s"),
					i, *InstanceData.Skills[i].ToString());
			}
		}
	}

	HeldItem = InstanceData.HeldItem;

	// Set status
	Status = static_cast<EGF_STATUS>(InstanceData.StatusCondition);
	SleepCounter = InstanceData.SleepCounter;

	// Trait — seeds CurrentTrait/OriginalTrait and the passive actor flags
	// (Composed -> bIsImmuneToFlinch). Trace still needs the OnSwitchIn hook.
	UGF_CreatureTraitLibrary::InitializeTraitOnActor(this, InstanceData);

	// Recalculate all stats
	RefreshStats();

	// Set current HP (must be after RefreshStats so MaxHP is calculated).
	// CurrentHP == 0 is the sentinel written by FGF_CreatureInstanceData::Initialize()
	// meaning "never spawned yet" — start at full HP.
	// Any value > 0 is a real saved HP from SyncCurrentCreatureStatus() and should be restored.
	CurrentStats.CurrentHP = (InstanceData.CurrentHP <= 0.f)
		? CurrentStats.MaxHP        // First spawn: full HP
		: InstanceData.CurrentHP;   // Switch-back: restore saved HP

	// Ensure current HP doesn't exceed max
	if (CurrentStats.CurrentHP > CurrentStats.MaxHP)
	{
		CurrentStats.CurrentHP = CurrentStats.MaxHP;
	}

	// Load visuals from species data
	DisplayIcon1 = SpeciesData->DisplayIcon1;
	DisplayIcon2 = SpeciesData->DisplayIcon2;
	UniqueDisplayIcon1 = SpeciesData->UniqueDisplayIcon1;
	UniqueDisplayIcon2 = SpeciesData->UniqueDisplayIcon2;
	Call = SpeciesData->Call;
	SideAnimations = SpeciesData->SideAnimations;
	SideAnimationsUnique = SpeciesData->SideAnimationsUnique;
	SideAnimationsFemale = SpeciesData->SideAnimationsFemale;
	SideAnimationsFemaleUnique = SpeciesData->SideAnimationsFemaleUnique;

	UE_LOG(LogTemp, Warning, TEXT("Initialized Creature actor: %s (Level %d, HP: %.0f/%.0f)"),
		*Name.ToString(), InstanceBaseStats.Level, CurrentStats.CurrentHP, CurrentStats.MaxHP);

	// Last line on purpose. Everything a Blueprint subclass needs in order to
	// build its visuals -- species, stats, elements, gender, the unique flag and
	// all four flipbook maps -- is populated by this point, and nothing after it
	// can invalidate them.
	OnCreatureInitialized();
}



FGF_CreatureInstanceData AGF_Creature::ExportToInstanceData() const
{
	FGF_CreatureInstanceData Data;

	// Species reference
	Data.SpeciesData = SpeciesData;

	// Basic info
	Data.CreatureID = CreatureID;
	Data.UniqueID = UniqueID;
	Data.Nickname = (Name != SpeciesData->SpeciesName) ? Name : FName();
	Data.Level = InstanceBaseStats.Level;
	Data.CurrentEXP = CurrentEXP;

	// Current stats
	Data.CurrentHP = CurrentStats.CurrentHP;
	Data.MaxHP = CurrentStats.MaxHP;

	// APs
	Data.HP_AP = APs.HP_AP;
	Data.Attack_AP = APs.Attack_AP;
	Data.Defense_AP = APs.Defense_AP;
	Data.Magic_AP = APs.Magic_AP;
	Data.Poise_AP = APs.Poise_AP;
	Data.Speed_AP = APs.Speed_AP;

	// EPs
	Data.HP_EP = EPs.HP_EP;
	Data.Attack_EP = EPs.Attack_EP;
	Data.Defense_EP = EPs.Defense_EP;
	Data.Magic_EP = EPs.Magic_EP;
	Data.Poise_EP = EPs.Poise_EP;
	Data.Speed_EP = EPs.Speed_EP;

	// Temperament & Gender
	Data.Temperament = CreatureTemperament;
	Data.Gender = Gender;

	// Skills
	Data.Skills.Empty();
	Data.CurrentUses.Empty();
	Data.MaxUses.Empty();
	for (int32 i = 0; i < Skills.Num(); i++)
	{
		Data.Skills.Add(Skills[i].Get());
		Data.CurrentUses.Add(SkillUses.IsValidIndex(i) ? SkillUses[i].CurrentUses : 10);
		Data.MaxUses.Add(SkillUses.IsValidIndex(i) ? SkillUses[i].MaxUses : 10);
	}

	Data.HeldItem  = HeldItem;

	// Status. Confusion is volatile — it lives on the battle actor only and must never
	// reach the party entry or the save file. Right now it shares the single Status slot
	// with burn/paralysis/etc., so it has to be filtered out here: switching out calls
	// ExportToInstanceData -> UpdatePartyCreatureBattleData, and without this a confused
	// Creature stays confused on the bench, after the battle, and across a save/load.
	Data.StatusCondition = (Status == EGF_STATUS::Confused)
		? EGF_STATUSEffect::None
		: static_cast<EGF_STATUSEffect>(Status);
	Data.SleepCounter = SleepCounter;

	// Trait — export the trait the Creature OWNS, never a Traced one, so a
	// Gleamling that traced Bulwark doesn't walk out of the battle keeping it.
	Data.Trait = (OriginalTrait != EGF_CreatureTrait::None) ? OriginalTrait : CurrentTrait;
	Data.TraitSlot = (SpeciesData && SpeciesData->Trait2 != EGF_CreatureTrait::None
		&& Data.Trait == SpeciesData->Trait2) ? 1 : 0;

	// Tamer info
	Data.OriginalTamerName = OriginalTamerName;
	Data.OriginalTamerID = OriginalTamerID;
	Data.CurrentTamerName = TamerName;
	Data.CurrentTamerID = CurrentTamerID;

	// Special properties
	Data.bIsUnique = isUnique;
	Data.bIsDowned = isDowned;

	 Data.CaughtCoreName = CaughtCoreName;

    //UE_LOG(LogTemp, Log, TEXT("ExportToInstanceData: Exporting CaughtCoreName '%s'"),
      //  *CaughtCoreName.ToString());


	return Data;
}

void AGF_Creature::RecalculateStatsFromInstanceData(const FGF_CreatureInstanceData& InstanceData)
{


	InstanceBaseStats.Level = InstanceData.Level;

	// Update APs
	APs.HP_AP = InstanceData.HP_AP;
	APs.Attack_AP = InstanceData.Attack_AP;
	APs.Defense_AP = InstanceData.Defense_AP;
	APs.Magic_AP = InstanceData.Magic_AP;
	APs.Poise_AP = InstanceData.Poise_AP;
	APs.Speed_AP = InstanceData.Speed_AP;

	// Update EPs
	EPs.HP_EP = InstanceData.HP_EP;
	EPs.Attack_EP = InstanceData.Attack_EP;
	EPs.Defense_EP = InstanceData.Defense_EP;
	EPs.Magic_EP = InstanceData.Magic_EP;
	EPs.Poise_EP = InstanceData.Poise_EP;
	EPs.Speed_EP = InstanceData.Speed_EP;

	// Recalculate all stats
	RefreshStats();


	// Restore current HP from instance data
	CurrentStats.CurrentHP = InstanceData.CurrentHP;


	// Make sure current HP doesn't exceed new max HP
	if (CurrentStats.CurrentHP > CurrentStats.MaxHP)
	{
		CurrentStats.CurrentHP = CurrentStats.MaxHP;
	}
}

bool AGF_Creature::ApplyFlinch(FString& OutTraitMessage)
{
    OutTraitMessage.Reset();

    // Can't flinch if already flinched
    if (bIsFlinched)
    {
        UE_LOG(LogTemp, Log, TEXT("%s is already flinched!"), *Name.ToString());
        return false;
    }

    // Can't flinch if already moved this turn
    if (bHasMovedThisTurn)
    {
        UE_LOG(LogTemp, Log, TEXT("%s already moved - flinch has no effect!"), *Name.ToString());
        return false;
    }

    // Check for flinch immunity (Composed, etc.)
    if (bIsImmuneToFlinch)
    {
        UE_LOG(LogTemp, Log, TEXT("%s is immune to flinching!"), *Name.ToString());
        return false;
    }

    // Apply flinch!
    bIsFlinched = true;
    UE_LOG(LogTemp, Log, TEXT("%s FLINCHED!"), *Name.ToString());

    // Resolute turns the flinch into a Speed boost. Self-inflicted, so Unyielding
    // and friends correctly don't interfere; returns 0 once Speed is capped at +6.
    if (UGF_CreatureTraitLibrary::GetActorTrait(this) == EGF_CreatureTrait::Resolute)
    {
        if (UGF_CreatureStatStageComponent* Stages = FindComponentByClass<UGF_CreatureStatStageComponent>())
        {
            if (Stages->ApplyStatStageChange(EGF_StatStages::SpeedUp, 1, true) != 0)
            {
                OutTraitMessage = FString::Printf(
                    TEXT("%s's Resolute raised its Speed!"), *Name.ToString());
                UE_LOG(LogTemp, Log, TEXT("%s"), *OutTraitMessage);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("ApplyFlinch: %s has Resolute but no StatStageComponent."), *Name.ToString());
        }
    }

    return true;
}

bool AGF_Creature::ApplyStatusCondition(EGF_STATUSEffect NewStatus, bool bOverrideExisting,
	int32 MinSleepTurns, int32 MaxSleepTurns)
{
	if (NewStatus == EGF_STATUSEffect::None)
	{
		return false;
	}

	// Non-volatile statuses don't stack — one at a time, first come first served.
	if (Status != EGF_STATUS::None && !bOverrideExisting)
	{
		UE_LOG(LogTemp, Log, TEXT("ApplyStatusCondition: %s already has a status, ignoring."),
			*Name.ToString());
		return false;
	}

	// EGF_STATUSEffect and EGF_STATUS are declared separately but share an identical member
	// order, so the cast is safe. This is the only reason this function needs to exist —
	// Blueprint can't make that hop on its own.
	Status = static_cast<EGF_STATUS>(NewStatus);

	if (NewStatus == EGF_STATUSEffect::Sleeping)
	{
		const int32 MinTurns = FMath::Max(1, MinSleepTurns);
		const int32 MaxTurns = FMath::Max(MinTurns, MaxSleepTurns);
		SleepCounter = FMath::RandRange(MinTurns, MaxTurns);
	}
	else
	{
		SleepCounter = 0;
	}

	UE_LOG(LogTemp, Log, TEXT("%s is now %d (sleep counter %d)"),
		*Name.ToString(), (int32)Status, SleepCounter);

	// Raised here rather than at the resolver, because a condition also arrives
	// from contact traits, held items and end-of-turn effects -- and every one
	// of those routes through this function.
	OnStatusConditionChanged(NewStatus, /*bCleared*/ false);

	return true;
}

void AGF_Creature::ClearStatusCondition()
{
	const EGF_STATUSEffect Previous = GetStatusAsEffect();

	Status = EGF_STATUS::None;
	SleepCounter = 0;

	// Only when there was something to clear, so a routine end-of-battle sweep
	// over a healthy party does not fire a cure effect on every creature.
	if (Previous != EGF_STATUSEffect::None)
	{
		OnStatusConditionChanged(Previous, /*bCleared*/ true);
	}
}

EGF_STATUSEffect AGF_Creature::GetStatusAsEffect() const
{
	return static_cast<EGF_STATUSEffect>(Status);
}

bool AGF_Creature::CanBeFlinched() const
{
    // Can't flinch if:
    // 1. Already flinched this turn
    // 2. Already moved this turn (flinch is useless)
    // 3. Immune to flinch (Composed trait)

    if (bIsFlinched)
    {
        return false;
    }

    if (bHasMovedThisTurn)
    {
        return false;
    }

    if (bIsImmuneToFlinch)
    {
        return false;
    }

    return true;
}

void AGF_Creature::MarkAsMovedThisTurn()
{
    bHasMovedThisTurn = true;
    UE_LOG(LogTemp, Verbose, TEXT("%s marked as moved this turn"), *Name.ToString());
}

void AGF_Creature::ResetVolatileStatuses()
{
    // Reset all volatile (battle-only) statuses
    // Call this when:
    // - Creature switches out
    // - Battle ends
    // - Creature downs

    bIsFlinched = false;
    bHasMovedThisTurn = false;
    bIsConfused = false;
    ConfusionTurnsRemaining = 0;
    bHasActedSinceEntering = false;
    bMustRecharge = false;

    // Confusion is currently applied through the non-volatile Status slot rather than
    // bIsConfused, so clearing the flags above isn't enough — drop the status too.
    // Only Confused: burn/paralysis/poison/sleep/freeze all survive a switch out.
    if (Status == EGF_STATUS::Confused)
    {
        Status = EGF_STATUS::None;
        SleepCounter = 0;
    }

    UE_LOG(LogTemp, Log, TEXT("%s: All volatile statuses cleared"), *Name.ToString());
}

void AGF_Creature::ResetTurnFlags()
{
    // Reset turn-specific flags at the start of each new turn
    // Call this at the START of each battle turn

    bIsFlinched = false;
    bHasMovedThisTurn = false;

    UE_LOG(LogTemp, Verbose, TEXT("%s: Turn flags reset"), *Name.ToString());
}

FString AGF_Creature::GetFlinchMessage() const
{
    // Format the flinch message based on Creature ownership
    // Similar to how stat stage messages work

    FString DisplayName = Name.ToString();

    if (isPlayerCreature)
    {
        // Player's Creature - just use name
        return FString::Printf(TEXT("%s flinched and couldn't move!"), *DisplayName);
    }
    else if (isWildCreature)
    {
        // Wild Creature
        return FString::Printf(TEXT("The wild %s flinched and couldn't move!"), *DisplayName);
    }
    else
    {
        // Enemy tamer's Creature
        return FString::Printf(TEXT("The foe's %s flinched and couldn't move!"), *DisplayName);
    }
}

void AGF_Creature::SetSkillSlot(int32 SlotIndex, TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    if (!SkillClass) return;
    if (SlotIndex < 0 || SlotIndex > 3) return;

    // Expand array if needed
    while (Skills.Num() <= SlotIndex)
        Skills.Add(nullptr);

    Skills[SlotIndex] = SkillClass;
}
bool AGF_Creature::IsPlayingOneShotAnimation() const
{
	switch (LastAnimStateRequested)
	{
	case EGF_CreatureAnimState::Call:
	case EGF_CreatureAnimState::Attack:
	case EGF_CreatureAnimState::Hurt:
	case EGF_CreatureAnimState::Down:
		return true;
	default:
		return false;
	}
}
