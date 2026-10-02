// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureTraits.h"
#include "GF_ElementTypes.h"
#include "GF_Creature.h"
#include "GF_CreatureStatStageComponent.h"

//========================================================================================
// LOCAL HELPERS
//========================================================================================

namespace
{
	/** Read a move's CDO without spawning it. Null-safe. */
	FORCEINLINE const AGF_SkillDefinition* GetSkillCDO(TSubclassOf<AGF_SkillDefinition> Skill)
	{
		return Skill ? Skill->GetDefaultObject<AGF_SkillDefinition>() : nullptr;
	}

	/**
	 * Bare name. Used for LOG lines, where a "The wild " prefix just makes grepping
	 * harder, and as the fallback for the display variants below.
	 */
	FString GetActorName(const AGF_Creature* Creature)
	{
		return Creature ? Creature->Name.ToString() : TEXT("The Creature");
	}

	/**
	 * Battle-message name, matching the convention AGF_Creature::GetFlinchMessage already
	 * uses: the player's Creature is bare, a wild one gets "The wild ", an enemy
	 * tamer's gets "The foe's ". Without this, a mirror match reads
	 * "Direfang's Menace cut Direfang's Attack!" with no way to tell them apart.
	 *
	 * @param bSentenceStart  True when the name opens the sentence, so the article is
	 *                        capitalised. False mid-sentence, giving "the wild X".
	 */
	FString GetBattleName(const AGF_Creature* Creature, bool bSentenceStart)
	{
		if (!Creature)
		{
			return bSentenceStart ? TEXT("The Creature") : TEXT("the Creature");
		}

		const FString Name = Creature->Name.ToString();

		// The player's own Creature is referred to by bare name, never "the".
		if (Creature->isPlayerCreature)
		{
			return Name;
		}

		const FString Article = Creature->isWildCreature
			? (bSentenceStart ? TEXT("The wild ")  : TEXT("the wild "))
			: (bSentenceStart ? TEXT("The foe's ") : TEXT("the foe's "));

		return Article + Name;
	}

	/** Name for the start of a sentence: "The wild Direfang". */
	FString StartName(const AGF_Creature* Creature) { return GetBattleName(Creature, true); }

	/** Name for mid-sentence use: "the wild Direfang". */
	FString MidName(const AGF_Creature* Creature) { return GetBattleName(Creature, false); }

	/** True when the Creature has the given type on either slot. */
	bool ActorHasType(const AGF_Creature* Creature, EGF_Element Type)
	{
		if (!Creature)
		{
			return false;
		}
		return Creature->PrimaryElement == Type || Creature->SecondaryElement == Type;
	}

	/**
	 * Hardcoded classic type immunities that traits must not be allowed to bypass.
	 * Kept in step with UGF_BattleComponent::CanStatusSkillAffect.
	 */
	bool IsTypeImmuneToStatus(const AGF_Creature* Creature, EGF_STATUSEffect Status)
	{
		switch (Status)
		{
			case EGF_STATUSEffect::Burned:
				return ActorHasType(Creature, EGF_Element::Fire);
			case EGF_STATUSEffect::Poisoned:
				return ActorHasType(Creature, EGF_Element::Poison);
			case EGF_STATUSEffect::Paralyzed:
				return false;   // classic has no Electric paralysis immunity
			default:
				return false;
		}
	}
}

//========================================================================================
// LOOKUP / DISPLAY
//========================================================================================

FText UGF_CreatureTraitLibrary::GetTraitDisplayName(EGF_CreatureTrait Trait)
{
	switch (Trait)
	{
		case EGF_CreatureTrait::Dewshield:		return NSLOCTEXT("Traits", "Dewshield", "Dewshield");
		case EGF_CreatureTrait::Emberhide:		return NSLOCTEXT("Traits", "Emberhide", "Emberhide");
		case EGF_CreatureTrait::Unfazed:		return NSLOCTEXT("Traits", "Unfazed", "Unfazed");
		case EGF_CreatureTrait::Muffled:		return NSLOCTEXT("Traits", "Muffled", "Muffled");
		case EGF_CreatureTrait::Composed:		return NSLOCTEXT("Traits", "Composed", "Composed");
		case EGF_CreatureTrait::Unyielding:		return NSLOCTEXT("Traits", "Unyielding", "Unyielding");
		case EGF_CreatureTrait::Hazeform:		return NSLOCTEXT("Traits", "Hazeform", "Hazeform");
		case EGF_CreatureTrait::Sharpsight:			return NSLOCTEXT("Traits", "Sharpsight", "Sharpsight");
		case EGF_CreatureTrait::Titanstrength:		return NSLOCTEXT("Traits", "Titanstrength", "Titanstrength");
		case EGF_CreatureTrait::Innerforce:		return NSLOCTEXT("Traits", "Innerforce", "Innerforce");
		case EGF_CreatureTrait::Insulated:			return NSLOCTEXT("Traits", "Insulated", "Insulated");
		case EGF_CreatureTrait::Currentborne:		return NSLOCTEXT("Traits", "Currentborne", "Currentborne");
		case EGF_CreatureTrait::Sunfed:		return NSLOCTEXT("Traits", "Sunfed", "Sunfed");
		case EGF_CreatureTrait::Adrenaline:				return NSLOCTEXT("Traits", "Adrenaline", "Adrenaline");
		case EGF_CreatureTrait::Rootsurge:			return NSLOCTEXT("Traits", "Rootsurge", "Rootsurge");
		case EGF_CreatureTrait::Cinderrage:			return NSLOCTEXT("Traits", "Cinderrage", "Cinderrage");
		case EGF_CreatureTrait::Tidesurge:			return NSLOCTEXT("Traits", "Tidesurge", "Tidesurge");
		case EGF_CreatureTrait::Hivecall:			return NSLOCTEXT("Traits", "Hivecall", "Hivecall");
		case EGF_CreatureTrait::Ironskull:			return NSLOCTEXT("Traits", "Ironskull", "Ironskull");
		case EGF_CreatureTrait::Manyeyes:		return NSLOCTEXT("Traits", "Manyeyes", "Manyeyes");
		case EGF_CreatureTrait::Fleetfoot:			return NSLOCTEXT("Traits", "Fleetfoot", "Fleetfoot");
		case EGF_CreatureTrait::Livewire:			return NSLOCTEXT("Traits", "Livewire", "Livewire");
		case EGF_CreatureTrait::Venomspur:		return NSLOCTEXT("Traits", "Venomspur", "Venomspur");
		case EGF_CreatureTrait::Sporeburst:		return NSLOCTEXT("Traits", "Sporeburst", "Sporeburst");
		case EGF_CreatureTrait::Beguile:		return NSLOCTEXT("Traits", "Beguile", "Beguile");
		case EGF_CreatureTrait::Scorchhide:		return NSLOCTEXT("Traits", "Scorchhide", "Scorchhide");
		case EGF_CreatureTrait::Bramblehide:		return NSLOCTEXT("Traits", "Bramblehide", "Bramblehide");
		case EGF_CreatureTrait::Selfmend:		return NSLOCTEXT("Traits", "Selfmend", "Selfmend");
		case EGF_CreatureTrait::Moult:			return NSLOCTEXT("Traits", "Moult", "Moult");
		case EGF_CreatureTrait::Lightsleeper:		return NSLOCTEXT("Traits", "Lightsleeper", "Lightsleeper");
		case EGF_CreatureTrait::Echoform:			return NSLOCTEXT("Traits", "Echoform", "Echoform");
		case EGF_CreatureTrait::Backlash:		return NSLOCTEXT("Traits", "Backlash", "Backlash");
		case EGF_CreatureTrait::Sluggard:			return NSLOCTEXT("Traits", "Sluggard", "Sluggard");
		case EGF_CreatureTrait::Dustward:		return NSLOCTEXT("Traits", "Dustward", "Dustward");
		case EGF_CreatureTrait::Tightgrip:		return NSLOCTEXT("Traits", "Tightgrip", "Tightgrip");
		case EGF_CreatureTrait::Foulsap:		return NSLOCTEXT("Traits", "Foulsap", "Foulsap");
		case EGF_CreatureTrait::Bulwark:			return NSLOCTEXT("Traits", "Bulwark", "Bulwark");
		case EGF_CreatureTrait::Rainfed:			return NSLOCTEXT("Traits", "Rainfed", "Rainfed");
		case EGF_CreatureTrait::Lodestone:		return NSLOCTEXT("Traits", "Lodestone", "Lodestone");
		case EGF_CreatureTrait::Scavenger:			return NSLOCTEXT("Traits", "Scavenger", "Scavenger");
		case EGF_CreatureTrait::Beacon:		return NSLOCTEXT("Traits", "Beacon", "Beacon");
		case EGF_CreatureTrait::Menace:		return NSLOCTEXT("Traits", "Menace", "Menace");
		case EGF_CreatureTrait::Duststep:			return NSLOCTEXT("Traits", "Duststep", "Duststep");
		case EGF_CreatureTrait::Wakeful:		return NSLOCTEXT("Traits", "Wakeful", "Wakeful");
		case EGF_CreatureTrait::Quickening:		return NSLOCTEXT("Traits", "Quickening", "Quickening");
		case EGF_CreatureTrait::Aegis:		return NSLOCTEXT("Traits", "Aegis", "Aegis");
		case EGF_CreatureTrait::Hover:			return NSLOCTEXT("Traits", "Hover", "Hover");
		case EGF_CreatureTrait::Stillair:			return NSLOCTEXT("Traits", "Stillair", "Stillair");
		case EGF_CreatureTrait::Resolute:		return NSLOCTEXT("Traits", "Resolute", "Resolute");
		case EGF_CreatureTrait::Smother:				return NSLOCTEXT("Traits", "Smother", "Smother");
		case EGF_CreatureTrait::Steadymind:			return NSLOCTEXT("Traits", "Steadymind", "Steadymind");
		default:								return NSLOCTEXT("Traits", "NoTrait", "None");
	}
}

FText UGF_CreatureTraitLibrary::GetTraitDescription(EGF_CreatureTrait Trait)
{
	switch (Trait)
	{
		case EGF_CreatureTrait::Dewshield:		return NSLOCTEXT("TraitDesc", "Dewshield", "Prevents the Creature from getting a burn.");
		case EGF_CreatureTrait::Emberhide:		return NSLOCTEXT("TraitDesc", "Emberhide", "Prevents the Creature from becoming frozen.");
		case EGF_CreatureTrait::Unfazed:		return NSLOCTEXT("TraitDesc", "Unfazed", "Prevents the Creature from becoming infatuated.");
		case EGF_CreatureTrait::Muffled:		return NSLOCTEXT("TraitDesc", "Muffled", "Gives full immunity to all sound-based moves.");
		case EGF_CreatureTrait::Composed:		return NSLOCTEXT("TraitDesc", "Composed", "Protects the Creature from flinching.");
		case EGF_CreatureTrait::Unyielding:		return NSLOCTEXT("TraitDesc", "Unyielding", "Prevents other Creature from lowering its stats.");
		case EGF_CreatureTrait::Hazeform:		return NSLOCTEXT("TraitDesc", "Hazeform", "Prevents other Creature from lowering its stats.");
		case EGF_CreatureTrait::Sharpsight:			return NSLOCTEXT("TraitDesc", "Sharpsight", "Prevents other Creature from lowering its accuracy.");
		case EGF_CreatureTrait::Titanstrength:		return NSLOCTEXT("TraitDesc", "Titanstrength", "Doubles the Creature's Attack stat.");
		case EGF_CreatureTrait::Innerforce:		return NSLOCTEXT("TraitDesc", "Innerforce", "Doubles the Creature's Attack stat.");
		case EGF_CreatureTrait::Insulated:			return NSLOCTEXT("TraitDesc", "Insulated", "Halves damage from Fire- and Ice-type moves.");
		case EGF_CreatureTrait::Currentborne:		return NSLOCTEXT("TraitDesc", "Currentborne", "Doubles the Creature's Speed in rain.");
		case EGF_CreatureTrait::Sunfed:		return NSLOCTEXT("TraitDesc", "Sunfed", "Doubles the Creature's Speed in harsh sunlight.");
		case EGF_CreatureTrait::Adrenaline:				return NSLOCTEXT("TraitDesc", "Adrenaline", "Boosts Attack by 50% if the Creature has a status condition.");
		case EGF_CreatureTrait::Rootsurge:			return NSLOCTEXT("TraitDesc", "Rootsurge", "Powers up Grass-type moves when the Creature is low on HP.");
		case EGF_CreatureTrait::Cinderrage:			return NSLOCTEXT("TraitDesc", "Cinderrage", "Powers up Fire-type moves when the Creature is low on HP.");
		case EGF_CreatureTrait::Tidesurge:			return NSLOCTEXT("TraitDesc", "Tidesurge", "Powers up Water-type moves when the Creature is low on HP.");
		case EGF_CreatureTrait::Hivecall:			return NSLOCTEXT("TraitDesc", "Hivecall", "Powers up Bug-type moves when the Creature is low on HP.");
		case EGF_CreatureTrait::Ironskull:			return NSLOCTEXT("TraitDesc", "Ironskull", "Protects the Creature from recoil damage.");
		case EGF_CreatureTrait::Manyeyes:		return NSLOCTEXT("TraitDesc", "Manyeyes", "Boosts the Creature's accuracy.");
		case EGF_CreatureTrait::Fleetfoot:			return NSLOCTEXT("TraitDesc", "Fleetfoot", "Enables a sure getaway from wild Creature.");
		case EGF_CreatureTrait::Livewire:			return NSLOCTEXT("TraitDesc", "Livewire", "Contact with the Creature may cause paralysis.");
		case EGF_CreatureTrait::Venomspur:		return NSLOCTEXT("TraitDesc", "Venomspur", "Contact with the Creature may poison the attacker.");
		case EGF_CreatureTrait::Sporeburst:		return NSLOCTEXT("TraitDesc", "Sporeburst", "Contact may poison, paralyze or put the attacker to sleep.");
		case EGF_CreatureTrait::Beguile:		return NSLOCTEXT("TraitDesc", "Beguile", "Contact with the Creature may cause infatuation.");
		case EGF_CreatureTrait::Scorchhide:		return NSLOCTEXT("TraitDesc", "Scorchhide", "Contact with the Creature may burn the attacker.");
		case EGF_CreatureTrait::Bramblehide:		return NSLOCTEXT("TraitDesc", "Bramblehide", "Hurts attackers that make contact.");
		case EGF_CreatureTrait::Selfmend:		return NSLOCTEXT("TraitDesc", "Selfmend", "Status conditions are cured when the Creature switches out.");
		case EGF_CreatureTrait::Moult:			return NSLOCTEXT("TraitDesc", "Moult", "The Creature may heal its own status condition each turn.");
		case EGF_CreatureTrait::Lightsleeper:		return NSLOCTEXT("TraitDesc", "Lightsleeper", "The Creature awakens from sleep twice as fast.");
		case EGF_CreatureTrait::Echoform:			return NSLOCTEXT("TraitDesc", "Echoform", "Copies the foe's trait when entering battle.");
		case EGF_CreatureTrait::Backlash:		return NSLOCTEXT("TraitDesc", "Backlash", "Passes a burn, poison or paralysis back to the foe.");
		case EGF_CreatureTrait::Sluggard:			return NSLOCTEXT("TraitDesc", "Sluggard", "The Creature can't attack on consecutive turns.");
		case EGF_CreatureTrait::Dustward:		return NSLOCTEXT("TraitDesc", "Dustward", "Blocks the added effects of attacks taken.");
		case EGF_CreatureTrait::Tightgrip:		return NSLOCTEXT("TraitDesc", "Tightgrip", "The Creature's held item can't be taken.");
		case EGF_CreatureTrait::Foulsap:		return NSLOCTEXT("TraitDesc", "Foulsap", "Damages attackers that use HP-draining moves.");
		case EGF_CreatureTrait::Bulwark:			return NSLOCTEXT("TraitDesc", "Bulwark", "Negates one-hit KO attacks.");
		case EGF_CreatureTrait::Rainfed:			return NSLOCTEXT("TraitDesc", "Rainfed", "The Creature gradually regains HP in rain.");
		case EGF_CreatureTrait::Lodestone:		return NSLOCTEXT("TraitDesc", "Lodestone", "Prevents Steel-type Creature from escaping.");
		case EGF_CreatureTrait::Scavenger:			return NSLOCTEXT("TraitDesc", "Scavenger", "The Creature may pick up items after a battle.");
		case EGF_CreatureTrait::Beacon:		return NSLOCTEXT("TraitDesc", "Beacon", "Raises the likelihood of meeting wild Creature.");
		case EGF_CreatureTrait::Menace:		return NSLOCTEXT("TraitDesc", "Menace", "Lowers the foe's Attack when entering battle.");
		case EGF_CreatureTrait::Duststep:			return NSLOCTEXT("TraitDesc", "Duststep", "Boosts evasion in a sandstorm.");
		case EGF_CreatureTrait::Wakeful:		return NSLOCTEXT("TraitDesc", "Wakeful", "Prevents the Creature from falling asleep.");
		case EGF_CreatureTrait::Quickening:		return NSLOCTEXT("TraitDesc", "Quickening", "Its Speed rises each turn.");
		case EGF_CreatureTrait::Aegis:		return NSLOCTEXT("TraitDesc", "Aegis", "Only super-effective moves will hit.");
		case EGF_CreatureTrait::Hover:			return NSLOCTEXT("TraitDesc", "Hover", "Gives full immunity to Ground-type moves.");
		case EGF_CreatureTrait::Stillair:			return NSLOCTEXT("TraitDesc", "Stillair", "Eliminates the effects of weather.");
		case EGF_CreatureTrait::Resolute:		return NSLOCTEXT("TraitDesc", "Resolute", "Raises Speed each time the Creature flinches.");
		default:								return FText::GetEmpty();
	}
}

EGF_TraitTier UGF_CreatureTraitLibrary::GetTraitTier(EGF_CreatureTrait Trait)
{
	// Anything added after the original 41 is listed by name, because enum numbering
	// is append-only and no longer lines up with the tier ranges below.
	switch (Trait)
	{
		case EGF_CreatureTrait::Menace:	return EGF_TraitTier::EventHook;
		case EGF_CreatureTrait::Quickening:	return EGF_TraitTier::EventHook;
		case EGF_CreatureTrait::Resolute:	return EGF_TraitTier::EventHook;
		case EGF_CreatureTrait::Smother:			return EGF_TraitTier::EventHook;
		case EGF_CreatureTrait::Duststep:
		case EGF_CreatureTrait::Wakeful:
		case EGF_CreatureTrait::Steadymind:
		case EGF_CreatureTrait::Aegis:
		case EGF_CreatureTrait::Hover:
		case EGF_CreatureTrait::Stillair:		return EGF_TraitTier::Trivial;
		default:							break;
	}

	const uint8 Value = static_cast<uint8>(Trait);

	if (Value == 0)					return EGF_TraitTier::None;
	if (Value <= 21)				return EGF_TraitTier::Trivial;
	if (Value <= 39)				return EGF_TraitTier::EventHook;
	return EGF_TraitTier::OverworldOnly;
}

EGF_CreatureTrait UGF_CreatureTraitLibrary::GetSpeciesTraitBySlot(const UGF_CreatureSpeciesData* Species, int32 Slot)
{
	if (!Species)
	{
		return EGF_CreatureTrait::None;
	}

	return (Slot == 1) ? Species->Trait2 : Species->Trait1;
}

EGF_CreatureTrait UGF_CreatureTraitLibrary::RollSpeciesTrait(const UGF_CreatureSpeciesData* Species, int32& OutSlot)
{
	OutSlot = 0;

	if (!Species)
	{
		return EGF_CreatureTrait::None;
	}

	// Only roll when the species genuinely has a second trait.
	if (Species->Trait2 != EGF_CreatureTrait::None && FMath::RandBool())
	{
		OutSlot = 1;
		return Species->Trait2;
	}

	return Species->Trait1;
}

EGF_CreatureTrait UGF_CreatureTraitLibrary::GetInstanceTrait(const FGF_CreatureInstanceData& Creature)
{
	if (Creature.Trait != EGF_CreatureTrait::None)
	{
		return Creature.Trait;
	}

	// Back-compat: a Creature saved before traits existed has Trait == None.
	// Resolve it from the species so it isn't stuck blank forever.
	if (Creature.SpeciesData.IsNull())
	{
		return EGF_CreatureTrait::None;
	}

	const UGF_CreatureSpeciesData* Species = Creature.SpeciesData.LoadSynchronous();
	return GetSpeciesTraitBySlot(Species, Creature.TraitSlot);
}

EGF_CreatureTrait UGF_CreatureTraitLibrary::GetActorTrait(const AGF_Creature* Creature)
{
	if (!Creature)
	{
		return EGF_CreatureTrait::None;
	}

	if (Creature->CurrentTrait != EGF_CreatureTrait::None)
	{
		return Creature->CurrentTrait;
	}

	// Actor was spawned without InitializeTraitOnActor - fall back to the species.
	return GetSpeciesTraitBySlot(Creature->SpeciesData, 0);
}

bool UGF_CreatureTraitLibrary::DoesCreatureHaveTrait(const AGF_Creature* Creature, EGF_CreatureTrait Trait)
{
	return Trait != EGF_CreatureTrait::None && GetActorTrait(Creature) == Trait;
}

void UGF_CreatureTraitLibrary::InitializeTraitOnActor(AGF_Creature* Creature, const FGF_CreatureInstanceData& Data)
{
	if (!Creature)
	{
		return;
	}

	const EGF_CreatureTrait Resolved = GetInstanceTrait(Data);

	Creature->CurrentTrait  = Resolved;
	Creature->OriginalTrait = Resolved;

	// Passive flags the rest of the battle code already reads off the actor.
	Creature->bIsImmuneToFlinch = DoesTraitBlockFlinch(Resolved);

	ResetTruantState(Creature);

	UE_LOG(LogTemp, Log, TEXT("Trait init: %s has %s"),
		*GetActorName(Creature), *GetTraitDisplayName(Resolved).ToString());
}

//========================================================================================
// TIER 1 - DAMAGE / STAT MODIFIERS
//========================================================================================

float UGF_CreatureTraitLibrary::GetAttackStatMultiplier(EGF_CreatureTrait Trait, bool bIsPhysical, EGF_STATUSEffect Status)
{
	switch (Trait)
	{
		case EGF_CreatureTrait::Titanstrength:
		case EGF_CreatureTrait::Innerforce:
			// classic: doubles the Attack stat, so special attackers get nothing.
			return bIsPhysical ? 2.0f : 1.0f;

		case EGF_CreatureTrait::Adrenaline:
			// Any non-volatile status counts. Confusion does not.
			if (bIsPhysical
				&& Status != EGF_STATUSEffect::None
				&& Status != EGF_STATUSEffect::Confused
				&& Status != EGF_STATUSEffect::Invigorated)
			{
				return 1.5f;
			}
			return 1.0f;

		default:
			return 1.0f;
	}
}

bool UGF_CreatureTraitLibrary::DoesTraitIgnoreBurnAttackDrop(EGF_CreatureTrait Trait)
{
	// A burned Guts user hits HARDER in classic - the burn halving is skipped entirely.
	return Trait == EGF_CreatureTrait::Adrenaline;
}

float UGF_CreatureTraitLibrary::GetLowHPTypeBoost(EGF_CreatureTrait Trait, EGF_Element SkillElement, float CurrentHP, float MaxHP)
{
	if (MaxHP <= 0.0f)
	{
		return 1.0f;
	}

	EGF_Element BoostedType = EGF_Element::None;
	switch (Trait)
	{
		case EGF_CreatureTrait::Rootsurge:	BoostedType = EGF_Element::Grass;	break;
		case EGF_CreatureTrait::Cinderrage:	BoostedType = EGF_Element::Fire;	break;
		case EGF_CreatureTrait::Tidesurge:	BoostedType = EGF_Element::Water;	break;
		// Hivecall boosted the bug element, which Dokimon does not have; it falls
		// through to "no boost" until it is given a new element.
		default:						return 1.0f;
	}

	if (SkillElement != BoostedType)
	{
		return 1.0f;
	}

	// classic threshold: 1/3 of max HP or less.
	return (CurrentHP <= MaxHP / 3.0f) ? 1.5f : 1.0f;
}

float UGF_CreatureTraitLibrary::GetDefenderDamageMultiplier(EGF_CreatureTrait DefenderTrait, EGF_Element SkillElement)
{
	if (DefenderTrait == EGF_CreatureTrait::Insulated
		&& (SkillElement == EGF_Element::Fire || SkillElement == EGF_Element::Ice))
	{
		return 0.5f;
	}

	return 1.0f;
}

bool UGF_CreatureTraitLibrary::DoesTraitGrantTypeImmunity(EGF_CreatureTrait DefenderTrait, EGF_Element SkillElement)
{
	// Hover granted ground immunity; Dokimon has no ground element, so no trait
	// grants a type immunity right now.
	return false;
}

bool UGF_CreatureTraitLibrary::DoesWonderGuardBlock(EGF_CreatureTrait DefenderTrait, float TypeEffectiveness)
{
	// classic: anything that isn't super effective does nothing. A 0x matchup is already
	// immune by the type chart, so this only has to catch 0.25x / 0.5x / 1x.
	return DefenderTrait == EGF_CreatureTrait::Aegis && TypeEffectiveness <= 1.0f;
}

float UGF_CreatureTraitLibrary::GetEvasionAccuracyMultiplier(EGF_CreatureTrait DefenderTrait, EGF_WeatherType Weather)
{
	if (DefenderTrait == EGF_CreatureTrait::Duststep && Weather == EGF_WeatherType::Sandstorm)
	{
		return 0.8f;
	}

	return 1.0f;
}

EGF_WeatherType UGF_CreatureTraitLibrary::GetEffectiveWeather(EGF_WeatherType Weather, const AGF_Creature* CreatureA, const AGF_Creature* CreatureB)
{
	if (Weather == EGF_WeatherType::None)
	{
		return Weather;
	}

	if (GetActorTrait(CreatureA) == EGF_CreatureTrait::Stillair
		|| GetActorTrait(CreatureB) == EGF_CreatureTrait::Stillair)
	{
		return EGF_WeatherType::None;
	}

	return Weather;
}

float UGF_CreatureTraitLibrary::GetAccuracyMultiplier(EGF_CreatureTrait Trait)
{
	return (Trait == EGF_CreatureTrait::Manyeyes) ? 1.3f : 1.0f;
}

float UGF_CreatureTraitLibrary::GetSpeedMultiplier(EGF_CreatureTrait Trait, EGF_WeatherType Weather)
{
	if (Trait == EGF_CreatureTrait::Currentborne && Weather == EGF_WeatherType::Rain)
	{
		return 2.0f;
	}

	if (Trait == EGF_CreatureTrait::Sunfed && Weather == EGF_WeatherType::HarshSun)
	{
		return 2.0f;
	}

	return 1.0f;
}

float UGF_CreatureTraitLibrary::GetRecoilDamage(EGF_CreatureTrait AttackerTrait, TSubclassOf<AGF_SkillDefinition> Skill, float DamageDealt, float AttackerMaxHP)
{
	const AGF_SkillDefinition* SkillCDO = GetSkillCDO(Skill);
	if (!SkillCDO || !SkillCDO->bHasRecoil || SkillCDO->RecoilPercentage <= 0.0f)
	{
		return 0.0f;
	}

	if (AttackerTrait == EGF_CreatureTrait::Ironskull)
	{
		return 0.0f;
	}

	// Dokimon recoil is a share of the user's MAX HP, not of the damage dealt, so a
	// Cannonball costs the same whatever it hit. DamageDealt only gates it: callers
	// pass 0 when nothing was hit, and a recoil skill that hit nothing costs nothing.
	if (DamageDealt <= 0.0f)
	{
		return 0.0f;
	}
	const float Recoil = AttackerMaxHP * (SkillCDO->RecoilPercentage / 100.0f);

	// Never less than 1 once a recoil move connects, never more than the user's max HP.
	return FMath::Clamp(FMath::Max(1.0f, FMath::FloorToFloat(Recoil)), 0.0f, FMath::Max(1.0f, AttackerMaxHP));
}

//========================================================================================
// TIER 1 - IMMUNITIES AND BLOCKS
//========================================================================================

bool UGF_CreatureTraitLibrary::DoesTraitBlockStatus(EGF_CreatureTrait Trait, EGF_STATUSEffect Status, const FString& CreatureName, FString& OutMessage)
{
	OutMessage.Reset();

	const bool bBlocked =
		(Trait == EGF_CreatureTrait::Dewshield   && Status == EGF_STATUSEffect::Burned)
	 || (Trait == EGF_CreatureTrait::Emberhide  && Status == EGF_STATUSEffect::Frozen)
	 || (Trait == EGF_CreatureTrait::Wakeful && Status == EGF_STATUSEffect::Sleeping)
	 || (Trait == EGF_CreatureTrait::Steadymind    && Status == EGF_STATUSEffect::Confused);

	if (bBlocked)
	{
		OutMessage = FString::Printf(TEXT("%s's %s prevents it!"),
			*CreatureName, *GetTraitDisplayName(Trait).ToString());
	}

	return bBlocked;
}

bool UGF_CreatureTraitLibrary::DoesTraitBlockInfatuation(EGF_CreatureTrait Trait)
{
	return Trait == EGF_CreatureTrait::Unfazed;
}

bool UGF_CreatureTraitLibrary::DoesTraitBlockFlinch(EGF_CreatureTrait Trait)
{
	return Trait == EGF_CreatureTrait::Composed;
}

bool UGF_CreatureTraitLibrary::DoesTraitBlockMove(EGF_CreatureTrait DefenderTrait, TSubclassOf<AGF_SkillDefinition> Skill, const FString& CreatureName, FString& OutMessage)
{
	OutMessage.Reset();

	if (DefenderTrait != EGF_CreatureTrait::Muffled)
	{
		return false;
	}

	const AGF_SkillDefinition* SkillCDO = GetSkillCDO(Skill);
	if (!SkillCDO || !SkillCDO->bIsSoundSkill)
	{
		return false;
	}

	OutMessage = FString::Printf(TEXT("%s's Muffled blocks %s!"),
		*CreatureName, *SkillCDO->Name.ToString());
	return true;
}

bool UGF_CreatureTraitLibrary::DoesDampBlockSkill(EGF_CreatureTrait UserTrait, EGF_CreatureTrait TargetTrait, TSubclassOf<AGF_SkillDefinition> Skill, const FString& UserName, const FString& TargetName, FString& OutMessage)
{
	OutMessage.Reset();

	const AGF_SkillDefinition* SkillCDO = GetSkillCDO(Skill);
	if (!SkillCDO || !SkillCDO->bIsSelfKOSkill)
	{
		return false;
	}

	// Damp on EITHER side stops it. The target is named first when both have it,
	// matching the classic message order.
	const bool bTargetHasDamp = (TargetTrait == EGF_CreatureTrait::Smother);
	const bool bUserHasDamp   = (UserTrait   == EGF_CreatureTrait::Smother);

	if (!bTargetHasDamp && !bUserHasDamp)
	{
		return false;
	}

	OutMessage = FString::Printf(TEXT("%s's Damp prevents %s!"),
		bTargetHasDamp ? *TargetName : *UserName, *SkillCDO->Name.ToString());
	return true;
}

bool UGF_CreatureTraitLibrary::CanStatStageBeLowered(EGF_CreatureTrait Trait, EGF_StatStages StatType, bool bSelfInflicted, const FString& CreatureName, FString& OutMessage)
{
	OutMessage.Reset();

	// A Creature lowering its OWN stats (Meltdown, Onslaught, Blood Rite) always works.
	if (bSelfInflicted)
	{
		return true;
	}

	// Only the "...Down" entries are drops; everything else is a boost and always allowed.
	const bool bIsDrop =
		StatType == EGF_StatStages::AttackDown   || StatType == EGF_StatStages::DefenseDown ||
		StatType == EGF_StatStages::MagicDown    || StatType == EGF_StatStages::PoiseDown   ||
		StatType == EGF_StatStages::SpeedDown    || StatType == EGF_StatStages::AccuracyDown ||
		StatType == EGF_StatStages::EvasionDown;

	if (!bIsDrop)
	{
		return true;
	}

	const bool bBlocksEverything =
		Trait == EGF_CreatureTrait::Unyielding || Trait == EGF_CreatureTrait::Hazeform;

	const bool bBlocksAccuracyOnly =
		Trait == EGF_CreatureTrait::Sharpsight && StatType == EGF_StatStages::AccuracyDown;

	if (bBlocksEverything || bBlocksAccuracyOnly)
	{
		OutMessage = FString::Printf(TEXT("%s's %s prevents stat loss!"),
			*CreatureName, *GetTraitDisplayName(Trait).ToString());
		return false;
	}

	return true;
}

bool UGF_CreatureTraitLibrary::DoesOHKOSkillFail(EGF_CreatureTrait DefenderTrait, TSubclassOf<AGF_SkillDefinition> Skill, const FString& CreatureName, FString& OutMessage)
{
	OutMessage.Reset();

	if (DefenderTrait != EGF_CreatureTrait::Bulwark)
	{
		return false;
	}

	const AGF_SkillDefinition* SkillCDO = GetSkillCDO(Skill);
	if (!SkillCDO || !SkillCDO->bKO)
	{
		return false;
	}

	OutMessage = FString::Printf(TEXT("%s's Bulwark protects it!"), *CreatureName);
	return true;
}

bool UGF_CreatureTraitLibrary::ShouldSecondaryEffectApply(EGF_CreatureTrait DefenderTrait)
{
	return DefenderTrait != EGF_CreatureTrait::Dustward;
}

bool UGF_CreatureTraitLibrary::CanHeldItemBeTaken(EGF_CreatureTrait TargetTrait)
{
	return TargetTrait != EGF_CreatureTrait::Tightgrip;
}

bool UGF_CreatureTraitLibrary::CanAlwaysFleeFromWild(EGF_CreatureTrait Trait)
{
	return Trait == EGF_CreatureTrait::Fleetfoot;
}

bool UGF_CreatureTraitLibrary::IsSwitchBlockedByTrait(const AGF_Creature* Switcher, const AGF_Creature* Opponent, FString& OutMessage)
{
	OutMessage.Reset();

	if (!Switcher || !Opponent)
	{
		return false;
	}

	// Fleetfoot beats every trapping effect in classic.
	if (GetActorTrait(Switcher) == EGF_CreatureTrait::Fleetfoot)
	{
		return false;
	}

	// Lodestone trapped the steel element, which Dokimon does not have, so it
	// traps nothing until it is given a new target.

	return false;
}

bool UGF_CreatureTraitLibrary::DoesDrainHurtInstead(EGF_CreatureTrait DefenderTrait)
{
	return DefenderTrait == EGF_CreatureTrait::Foulsap;
}

//========================================================================================
// TIER 2 - EVENT HOOKS
//========================================================================================

FGF_TraitContactResult UGF_CreatureTraitLibrary::OnContactMade(AGF_Creature* Attacker, AGF_Creature* Defender, TSubclassOf<AGF_SkillDefinition> Skill)
{
	FGF_TraitContactResult Result;

	// Every early-out below says why it left. A silent "Triggered = false" is
	// indistinguishable from a wiring mistake from inside Blueprint.
	//
	// The genuine faults (null pins) stay at Warning. The "working as intended"
	// reasons are Verbose so they cost nothing normally — turn them on with
	//     log LogTemp Verbose
	// in the console when a contact trait isn't firing and you need to know
	// whether it's the move, the trait, or the roll.
	if (!Attacker || !Defender)
	{
		UE_LOG(LogTemp, Warning, TEXT("OnContactMade: Attacker or Defender is null."));
		return Result;
	}

	const AGF_SkillDefinition* SkillCDO = GetSkillCDO(Skill);
	if (!SkillCDO)
	{
		UE_LOG(LogTemp, Warning, TEXT("OnContactMade: Skill is null."));
		return Result;
	}

	if (!SkillCDO->bMakesContact)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("OnContactMade: '%s' is not a contact move - nothing to do."),
			*SkillCDO->Name.ToString());
		return Result;
	}

	const EGF_CreatureTrait DefenderTrait = GetActorTrait(Defender);

	// Sentence-start for whoever the message leads with, mid-sentence for the other.
	const FString DefenderName    = StartName(Defender);
	const FString DefenderNameMid = MidName(Defender);
	const FString AttackerName    = StartName(Attacker);
	const FString AttackerNameMid = MidName(Attacker);

	UE_LOG(LogTemp, Verbose,
		TEXT("OnContactMade: %s hit %s with '%s'. Reading the DEFENDER's trait: %s. "
			 "If that's the wrong Creature's trait, the Attacker/Defender pins are swapped."),
		*AttackerName, *DefenderName, *SkillCDO->Name.ToString(),
		*GetTraitDisplayName(DefenderTrait).ToString());

	// Everything below inflicts a status - skip if the attacker already has one.
	// Bramblehide is the exception and is handled first.
	if (DefenderTrait == EGF_CreatureTrait::Bramblehide)
	{
		const float Damage = FMath::Max(1.0f, FMath::FloorToFloat(Attacker->CurrentStats.MaxHP / 16.0f));

		Result.bTriggered		= true;
		Result.DamageToAttacker = Damage;
		Result.Message			= FString::Printf(TEXT("%s was hurt by %s's Bramblehide!"),
									*AttackerName, *DefenderNameMid);
		return Result;
	}

	if (DefenderTrait == EGF_CreatureTrait::Beguile)
	{
		// classic: only fires between opposite genders, and never on a genderless Creature.
		const bool bOppositeGenders =
			Attacker->Gender != EGF_CreatureGender::Genderless &&
			Defender->Gender != EGF_CreatureGender::Genderless &&
			Attacker->Gender != Defender->Gender;

		if (!bOppositeGenders)
		{
			return Result;
		}

		if (DoesTraitBlockInfatuation(GetActorTrait(Attacker)))
		{
			return Result;
		}

		if (FMath::RandRange(1, 100) <= 30)
		{
			Result.bTriggered  = true;
			Result.bInfatuate  = true;
			Result.Message     = FString::Printf(TEXT("%s's Beguile infatuated %s!"),
									*DefenderName, *AttackerNameMid);
		}
		return Result;
	}

	// The remaining contact traits all apply a non-volatile status, which can't
	// stack on top of an existing one.
	if (Attacker->Status != EGF_STATUS::None)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("OnContactMade: %s already has a status, so %s can't apply another."),
			*AttackerName, *GetTraitDisplayName(DefenderTrait).ToString());
		return Result;
	}

	EGF_STATUSEffect StatusToApply = EGF_STATUSEffect::None;
	int32 Chance = 0;

	switch (DefenderTrait)
	{
		case EGF_CreatureTrait::Livewire:
			StatusToApply = EGF_STATUSEffect::Paralyzed;
			Chance = 30;
			break;

		case EGF_CreatureTrait::Venomspur:
			StatusToApply = EGF_STATUSEffect::Poisoned;
			Chance = 30;
			break;

		case EGF_CreatureTrait::Scorchhide:
			StatusToApply = EGF_STATUSEffect::Burned;
			Chance = 30;
			break;

		case EGF_CreatureTrait::Sporeburst:
			// classic: a flat 30% to fire, then an even split between the three statuses.
			Chance = 30;
			switch (FMath::RandRange(0, 2))
			{
				case 0:  StatusToApply = EGF_STATUSEffect::Poisoned;	break;
				case 1:  StatusToApply = EGF_STATUSEffect::Paralyzed;	break;
				default: StatusToApply = EGF_STATUSEffect::Sleeping;	break;
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose,
				TEXT("OnContactMade: %s's %s is not an on-contact trait."),
				*DefenderName, *GetTraitDisplayName(DefenderTrait).ToString());
			return Result;
	}

	if (FMath::RandRange(1, 100) > Chance)
	{
		UE_LOG(LogTemp, Verbose, TEXT("OnContactMade: %s's %s rolled and missed its %d%% chance."),
			*DefenderName, *GetTraitDisplayName(DefenderTrait).ToString(), Chance);
		return Result;
	}

	// Respect type immunities and the attacker's own protective traits.
	if (IsTypeImmuneToStatus(Attacker, StatusToApply))
	{
		return Result;
	}

	FString BlockMessage;
	if (DoesTraitBlockStatus(GetActorTrait(Attacker), StatusToApply, AttackerName, BlockMessage))
	{
		return Result;
	}

	FString StatusVerb;
	switch (StatusToApply)
	{
		case EGF_STATUSEffect::Paralyzed:	StatusVerb = TEXT("paralyzed");			break;
		case EGF_STATUSEffect::Poisoned:	StatusVerb = TEXT("poisoned");			break;
		case EGF_STATUSEffect::Burned:		StatusVerb = TEXT("burned");			break;
		case EGF_STATUSEffect::Sleeping:	StatusVerb = TEXT("put to sleep");		break;
		default:						StatusVerb = TEXT("affected");			break;
	}

	Result.bTriggered		= true;
	Result.StatusToApply	= StatusToApply;
	Result.Message			= FString::Printf(TEXT("%s's %s %s %s!"),
								*DefenderName,
								*GetTraitDisplayName(DefenderTrait).ToString(),
								*StatusVerb,
								*AttackerNameMid);
	return Result;
}

bool UGF_CreatureTraitLibrary::OnSwitchIn(AGF_Creature* Entering, AGF_Creature* Opponent, FString& OutMessage)
{
	OutMessage.Reset();

	if (!Entering)
	{
		return false;
	}

	// Sluggard's cadence restarts every time the Creature comes back out.
	ResetTruantState(Entering);

	const EGF_CreatureTrait EnteringTrait = GetActorTrait(Entering);

	UE_LOG(LogTemp, Verbose, TEXT("OnSwitchIn: %s (%s) entering against %s"),
		*GetActorName(Entering),
		*GetTraitDisplayName(EnteringTrait).ToString(),
		Opponent ? *GetActorName(Opponent) : TEXT("NOBODY (Opponent pin is null)"));

	//----------------------------------------------------------------------------
	// Menace — drop the opponent's Attack one stage on entry.
	//----------------------------------------------------------------------------
	if (EnteringTrait == EGF_CreatureTrait::Menace)
	{
		if (!Opponent)
		{
			// Loud, because this is the one failure that leaves no other trace and it's
			// the easiest thing to get wrong: Menace reads the OTHER Creature, so both
			// must already be on the field. Calling this during the entering Creature's own
			// send-out — before the opponent actor exists — hands us a null here and the
			// trait silently does nothing. Same applies to Trace.
			UE_LOG(LogTemp, Warning,
				TEXT("OnSwitchIn: %s has Menace but the Opponent pin is NULL, so there's "
					 "nothing to lower. Call this only once BOTH Creature are on the field, and "
					 "make sure the Opponent pin is wired to the other side's active Creature."),
				*GetActorName(Entering));
			return false;
		}

		UGF_CreatureStatStageComponent* OpponentStages =
			Opponent->FindComponentByClass<UGF_CreatureStatStageComponent>();

		if (!OpponentStages)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("OnSwitchIn: %s has Menace but %s has no StatStageComponent, so there's "
					 "nothing to lower."),
				*GetActorName(Entering), *GetActorName(Opponent));
			return false;
		}

		// Routed through ApplyStatStageChange rather than touching the stage directly,
		// so Unyielding / Hazeform block it automatically and the -6 clamp applies.
		// bSelfInflicted stays false — this is very much done TO the opponent.
		const int32 Applied = OpponentStages->ApplyStatStageChange(EGF_StatStages::AttackDown, -1, false);

		if (Applied == 0)
		{
			// Either an trait refused it or Attack is already bottomed out at -6.
			if (OpponentStages->bBlockedByTrait)
			{
				OutMessage = OpponentStages->LastTraitBlockMessage;
				return true;
			}
			return false;
		}

		OutMessage = FString::Printf(TEXT("%s's Menace cut %s's Attack!"),
			*StartName(Entering), *MidName(Opponent));
		return true;
	}

	if (EnteringTrait != EGF_CreatureTrait::Echoform || !Opponent)
	{
		return false;
	}

	const EGF_CreatureTrait Copied = GetActorTrait(Opponent);

	// Trace fails against a Creature with no trait, and copying Trace itself is a no-op.
	if (Copied == EGF_CreatureTrait::None || Copied == EGF_CreatureTrait::Echoform)
	{
		return false;
	}

	Entering->CurrentTrait = Copied;

	// Traced traits take effect immediately, including the passive actor flags.
	Entering->bIsImmuneToFlinch = DoesTraitBlockFlinch(Copied);

	OutMessage = FString::Printf(TEXT("%s traced %s's %s!"),
		*StartName(Entering),
		*MidName(Opponent),
		*GetTraitDisplayName(Copied).ToString());

	return true;
}

bool UGF_CreatureTraitLibrary::OnSwitchOut(AGF_Creature* Leaving, FGF_CreatureInstanceData& Data, FString& OutMessage)
{
	OutMessage.Reset();

	if (!Leaving)
	{
		return false;
	}

	bool bDidSomething = false;

	// Selfmend is checked against the trait the Creature is LEAVING with, so a
	// Traced Selfmend counts and a Traced-away one does not.
	if (GetActorTrait(Leaving) == EGF_CreatureTrait::Selfmend
		&& Leaving->Status != EGF_STATUS::None)
	{
		Leaving->Status = EGF_STATUS::None;
		Leaving->SleepCounter = 0;

		// Write it through so the cure survives back into the party data.
		Data.StatusCondition = EGF_STATUSEffect::None;
		Data.SleepCounter = 0;

		OutMessage = FString::Printf(TEXT("%s's Selfmend healed its status!"),
			*StartName(Leaving));
		bDidSomething = true;
	}

	// A Traced trait only lasts as long as the Creature is on the field.
	if (Leaving->OriginalTrait != EGF_CreatureTrait::None
		&& Leaving->CurrentTrait != Leaving->OriginalTrait)
	{
		Leaving->CurrentTrait = Leaving->OriginalTrait;
		Leaving->bIsImmuneToFlinch = DoesTraitBlockFlinch(Leaving->OriginalTrait);
	}

	ResetTruantState(Leaving);

	return bDidSomething;
}

FGF_TraitEndOfTurnResult UGF_CreatureTraitLibrary::OnEndOfTurn(AGF_Creature* Creature, EGF_WeatherType Weather)
{
	FGF_TraitEndOfTurnResult Result;

	if (!Creature)
	{
		UE_LOG(LogTemp, Warning, TEXT("OnEndOfTurn: Creature is null."));
		return Result;
	}

	if (Creature->IsDowned())
	{
		UE_LOG(LogTemp, Verbose, TEXT("OnEndOfTurn: %s has downed, skipping."),
			*GetActorName(Creature));
		return Result;
	}

	const EGF_CreatureTrait Trait = GetActorTrait(Creature);
	const FString Name = StartName(Creature);   // every end-of-turn message leads with it

	// CurrentTrait is seeded by InitializeTraitOnActor. If it's still None here,
	// this actor was spawned down a path that never called it, and GetActorTrait has
	// silently fallen back to species slot 0 — which is the WRONG trait for every
	// two-trait species (a Marshdancer reads as Currentborne instead of Rainfed).
	// Loud, because it fails as "the trait just never triggers".
	if (Creature->CurrentTrait == EGF_CreatureTrait::None)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("OnEndOfTurn: %s has no CurrentTrait - InitializeTraitOnActor was never "
				 "called on it. Falling back to species slot 0 (%s), which may be the wrong "
				 "trait. Call Initialize Trait On Actor wherever this Creature is spawned."),
			*Name, *GetTraitDisplayName(Trait).ToString());
	}

	UE_LOG(LogTemp, Verbose, TEXT("OnEndOfTurn: %s | trait=%s | weather=%d | HP %.0f/%.0f"),
		*Name, *GetTraitDisplayName(Trait).ToString(), (int32)Weather,
		Creature->CurrentStats.CurrentHP, Creature->CurrentStats.MaxHP);

	switch (Trait)
	{
		case EGF_CreatureTrait::Moult:
		{
			if (Creature->Status == EGF_STATUS::None)
			{
				break;
			}

			// classic: 1/3 chance each turn.
			if (FMath::RandRange(1, 3) != 1)
			{
				break;
			}

			Creature->Status = EGF_STATUS::None;
			Creature->SleepCounter = 0;

			Result.bTriggered	= true;
			Result.bStatusCured	= true;
			Result.Message		= FString::Printf(TEXT("%s's Moult cured its status!"), *Name);
			break;
		}

		case EGF_CreatureTrait::Rainfed:
		{
			if (Weather != EGF_WeatherType::Rain)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("OnEndOfTurn: %s has Rainfed but it isn't raining (weather=%d)."),
					*Name, (int32)Weather);
				break;
			}

			const float MissingHP = Creature->CurrentStats.MaxHP - Creature->CurrentStats.CurrentHP;
			if (MissingHP <= 0.0f)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("OnEndOfTurn: %s's Rainfed has nothing to heal - already at full HP."),
					*Name);
				break;
			}

			const float Heal = FMath::Min(
				FMath::Max(1.0f, FMath::FloorToFloat(Creature->CurrentStats.MaxHP / 16.0f)),
				MissingHP);

			Result.bTriggered	= true;
			Result.HealAmount	= Heal;
			Result.Message		= FString::Printf(TEXT("%s's Rainfed restored its HP a little!"), *Name);
			break;
		}

		case EGF_CreatureTrait::Quickening:
		{
			UGF_CreatureStatStageComponent* Stages =
				Creature->FindComponentByClass<UGF_CreatureStatStageComponent>();

			if (!Stages)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("OnEndOfTurn: %s has Quickening but no StatStageComponent."), *Name);
				break;
			}

			// Self-inflicted, so Unyielding and friends correctly don't interfere.
			// Returns 0 once Speed is already capped at +6, which is not a failure.
			if (Stages->ApplyStatStageChange(EGF_StatStages::SpeedUp, 1, true) == 0)
			{
				break;
			}

			Result.bTriggered	= true;
			Result.Message		= FString::Printf(TEXT("%s's Quickening raised its Speed!"), *Name);
			break;
		}

		default:
			break;
	}

	return Result;
}

bool UGF_CreatureTraitLibrary::OnStatusInflicted(AGF_Creature* Afflicted, AGF_Creature* Inflictor, EGF_STATUSEffect Status, EGF_STATUSEffect& OutStatusToReflect, FString& OutMessage)
{
	OutStatusToReflect = EGF_STATUSEffect::None;
	OutMessage.Reset();

	if (!Afflicted || !Inflictor || Afflicted == Inflictor)
	{
		return false;
	}

	if (GetActorTrait(Afflicted) != EGF_CreatureTrait::Backlash)
	{
		return false;
	}

	// classic Backlash only bounces back burn, poison and paralysis.
	if (Status != EGF_STATUSEffect::Burned
		&& Status != EGF_STATUSEffect::Poisoned
		&& Status != EGF_STATUSEffect::Paralyzed)
	{
		return false;
	}

	// The inflictor can't take a second status, and type immunity still applies.
	if (Inflictor->Status != EGF_STATUS::None || IsTypeImmuneToStatus(Inflictor, Status))
	{
		return false;
	}

	FString BlockMessage;
	if (DoesTraitBlockStatus(GetActorTrait(Inflictor), Status, StartName(Inflictor), BlockMessage))
	{
		return false;
	}

	OutStatusToReflect = Status;
	OutMessage = FString::Printf(TEXT("%s's Backlash passed it on to %s!"),
		*StartName(Afflicted), *MidName(Inflictor));

	return true;
}

bool UGF_CreatureTraitLibrary::TruantCanActThisTurn(AGF_Creature* Creature, FString& OutMessage)
{
	OutMessage.Reset();

	if (!Creature || GetActorTrait(Creature) != EGF_CreatureTrait::Sluggard)
	{
		return true;
	}

	if (Creature->bTruantLoafingThisTurn)
	{
		// Loafing this turn - it acts again next turn.
		Creature->bTruantLoafingThisTurn = false;
		OutMessage = FString::Printf(TEXT("%s is loafing around!"), *StartName(Creature));
		return false;
	}

	// It acts this turn, so next turn it loafs.
	Creature->bTruantLoafingThisTurn = true;
	return true;
}

void UGF_CreatureTraitLibrary::ResetTruantState(AGF_Creature* Creature)
{
	if (Creature)
	{
		// A Sluggard Creature always gets to act on the turn it comes out.
		Creature->bTruantLoafingThisTurn = false;

		// Same moment, same reason: runs on every send-out and switch-in, which is
		// exactly when a creature's "first turn" restarts and a pending recharge
		// stops mattering.
		Creature->bHasActedSinceEntering = false;
		Creature->bMustRecharge = false;
	}
}

int32 UGF_CreatureTraitLibrary::GetSleepCounterDecrement(EGF_CreatureTrait Trait)
{
	return (Trait == EGF_CreatureTrait::Lightsleeper) ? 2 : 1;
}

//========================================================================================
// TIER 3 - OVERWORLD
//========================================================================================

bool UGF_CreatureTraitLibrary::RollPickupItem(EGF_CreatureTrait Trait, int32 Level, FName& OutItem)
{
	OutItem = NAME_None;

	if (Trait != EGF_CreatureTrait::Scavenger)
	{
		return false;
	}

	// classic rate: 10% per eligible party Creature after a won battle.
	if (FMath::RandRange(1, 100) > 10)
	{
		return false;
	}

	// Level-banded pools, based on the classic table but checked against the DA_ item
	// assets that actually exist in this project — Hyper Potion, Nugget and Max Repel
	// aren't built yet, so Ether, Stardust and Memory Scale stand in for them.
	// Nothing else in the trait reads these names; swap freely as items are added.
	static const FName EarlyPool[] = {
		TEXT("Potion"), TEXT("Antidote"), TEXT("MajorSalve"), TEXT("KeenCore"),
		TEXT("Repel"), TEXT("EscapeRope"), TEXT("Awakening"), TEXT("ParalyzeHeal")
	};

	static const FName MidPool[] = {
		TEXT("MajorSalve"), TEXT("KeenCore"), TEXT("FullHeal"), TEXT("Revive"),
		TEXT("Ether"), TEXT("WardenCore"), TEXT("Repel"), TEXT("EscapeRope")
	};

	static const FName LatePool[] = {
		TEXT("Ether"), TEXT("WardenCore"), TEXT("Revive"), TEXT("FullSalve"),
		TEXT("GrowthCandy"), TEXT("Stardust"), TEXT("FullHeal"), TEXT("MemoryScale")
	};

	const FName* Pool = EarlyPool;
	int32 PoolSize = UE_ARRAY_COUNT(EarlyPool);

	if (Level >= 41)
	{
		Pool = LatePool;
		PoolSize = UE_ARRAY_COUNT(LatePool);
	}
	else if (Level >= 21)
	{
		Pool = MidPool;
		PoolSize = UE_ARRAY_COUNT(MidPool);
	}

	OutItem = Pool[FMath::RandRange(0, PoolSize - 1)];
	return true;
}

float UGF_CreatureTraitLibrary::GetEncounterRateMultiplier(EGF_CreatureTrait LeadTrait)
{
	return (LeadTrait == EGF_CreatureTrait::Beacon) ? 2.0f : 1.0f;
}
