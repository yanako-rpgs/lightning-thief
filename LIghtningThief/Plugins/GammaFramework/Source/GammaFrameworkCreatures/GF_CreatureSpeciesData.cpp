#include "GF_CreatureSpeciesData.h"

#include "PaperFlipbook.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/SoftObjectPath.h"


// ---------------------------------------------------------------------------
// ASSET REGISTRY TAGS
// ---------------------------------------------------------------------------

bool UGF_CreatureSpeciesData::SpeciesNameHasFixedOneHP(FName InSpeciesName)
{
	// The one place in the project that names the species. Everything else asks here.
	return InSpeciesName == FName("Husk");
}

bool UGF_CreatureSpeciesData::HasFixedOneHP() const
{
	return SpeciesNameHasFixedOneHP(SpeciesName);
}


const FName UGF_CreatureSpeciesData::BattleIdleAnimationTagName(TEXT("BattleIdleAnimation"));
const FName UGF_CreatureSpeciesData::EvolutionTargetsTagName(TEXT("EvolutionTargets"));

void UGF_CreatureSpeciesData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	// The Compendium preview needs this one flipbook and nothing else. Publishing its path
	// lets the browse view load ~7.7 MB instead of the ~45 MB the whole species costs.
	if (UPaperFlipbook* const* IdlePtr = SideAnimations.Find(EGF_CreatureAnimState::Idle))
	{
		if (*IdlePtr)
		{
			Context.AddTag(FAssetRegistryTag(
				BattleIdleAnimationTagName,
				FSoftObjectPath(*IdlePtr).ToString(),
				FAssetRegistryTag::TT_Hidden));
		}
	}

	// Comma-separated species names this one evolves INTO. Breeding inverts these into a
	// pre-evolution map so it can walk to a base form without loading anything.
	if (Evolutions.Num() > 0)
	{
		TArray<FString> Targets;
		Targets.Reserve(Evolutions.Num());
		for (const FGF_EvolutionMethod& Evolution : Evolutions)
		{
			if (!Evolution.EvolvedSpecies.IsNone())
			{
				Targets.Add(Evolution.EvolvedSpecies.ToString());
			}
		}

		if (Targets.Num() > 0)
		{
			Context.AddTag(FAssetRegistryTag(
				EvolutionTargetsTagName,
				FString::Join(Targets, TEXT(",")),
				FAssetRegistryTag::TT_Hidden));
		}
	}
}

//======================================================================================
// SIDE-ON ANIMATIONS
//======================================================================================

UPaperFlipbook* UGF_CreatureSpeciesData::GetSideAnimation(
	EGF_CreatureAnimState State, bool bUnique, bool bFemale, EGF_CreatureAnimVariant& OutVariant) const
{
	OutVariant = EGF_CreatureAnimVariant::None;

	// Most specific set first. Unique outranks Female because the unique palette
	// is what a player is actively looking for; a missing female silhouette on a
	// species that never drew one is not a visible loss.
	struct FCandidate
	{
		const TMap<EGF_CreatureAnimState, UPaperFlipbook*>* Set;
		EGF_CreatureAnimVariant Variant;
	};

	TArray<FCandidate, TInlineAllocator<4>> Order;
	if (bFemale && bUnique) { Order.Add({ &SideAnimationsFemaleUnique, EGF_CreatureAnimVariant::FemaleUnique }); }
	if (bUnique)            { Order.Add({ &SideAnimationsUnique,       EGF_CreatureAnimVariant::Unique }); }
	if (bFemale)            { Order.Add({ &SideAnimationsFemale,       EGF_CreatureAnimVariant::Female }); }
	Order.Add({ &SideAnimations, EGF_CreatureAnimVariant::Normal });

	for (const FCandidate& Candidate : Order)
	{
		if (UPaperFlipbook* const* Found = Candidate.Set->Find(State))
		{
			if (*Found)
			{
				OutVariant = Candidate.Variant;
				return *Found;
			}
		}

		// Low-HP art is optional per species: fall back to that same set's Idle
		// before dropping to a less specific set, so a unique creature at low HP
		// stays unique rather than reverting to the normal palette.
		if (State == EGF_CreatureAnimState::IdleLowHP)
		{
			if (UPaperFlipbook* const* Idle = Candidate.Set->Find(EGF_CreatureAnimState::Idle))
			{
				if (*Idle)
				{
					OutVariant = Candidate.Variant;
					return *Idle;
				}
			}
		}
	}

	return nullptr;
}

UPaperFlipbook* UGF_CreatureSpeciesData::FindSideAnimation(EGF_CreatureAnimState State, bool bUnique, bool bFemale) const
{
	EGF_CreatureAnimVariant Unused = EGF_CreatureAnimVariant::None;
	return GetSideAnimation(State, bUnique, bFemale, Unused);
}
