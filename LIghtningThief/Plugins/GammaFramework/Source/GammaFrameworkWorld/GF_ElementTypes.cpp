#include "GF_ElementTypes.h"

namespace
{
	// Row = attacking element, column = defending element.
	//
	//   x2   super effective
	//   x0.5 resisted
	//   x0   immune
	//
	// Placeholder chart: the classic (Gen 6+) chart cut down to the Dokimon
	// elements, until Lightning Thief has its own. Fight plays Fighting, and
	// Light -- the element of the plain skills -- plays Normal.
	struct FMatchup
	{
		EGF_Element Defending;
		float       Multiplier;
	};

	// Only the non-1.0 cells are listed; everything unlisted is neutral damage.
	const TMap<EGF_Element, TArray<FMatchup>>& Chart()
	{
		static const TMap<EGF_Element, TArray<FMatchup>> Table = {
			{ EGF_Element::Light, {
				{ EGF_Element::Ghost, 0.0f },
			}},
			{ EGF_Element::Fire, {
				{ EGF_Element::Grass, 2.0f },  { EGF_Element::Ice, 2.0f },
				{ EGF_Element::Fire, 0.5f },   { EGF_Element::Water, 0.5f },
				{ EGF_Element::Dragon, 0.5f },
			}},
			{ EGF_Element::Water, {
				{ EGF_Element::Fire, 2.0f },
				{ EGF_Element::Water, 0.5f },  { EGF_Element::Grass, 0.5f },
				{ EGF_Element::Dragon, 0.5f },
			}},
			{ EGF_Element::Electric, {
				{ EGF_Element::Water, 2.0f },  { EGF_Element::Flying, 2.0f },
				{ EGF_Element::Electric, 0.5f }, { EGF_Element::Grass, 0.5f },
				{ EGF_Element::Dragon, 0.5f },
			}},
			{ EGF_Element::Grass, {
				{ EGF_Element::Water, 2.0f },
				{ EGF_Element::Fire, 0.5f },   { EGF_Element::Grass, 0.5f },
				{ EGF_Element::Poison, 0.5f }, { EGF_Element::Flying, 0.5f },
				{ EGF_Element::Dragon, 0.5f },
			}},
			{ EGF_Element::Ice, {
				{ EGF_Element::Grass, 2.0f },  { EGF_Element::Flying, 2.0f },
				{ EGF_Element::Dragon, 2.0f },
				{ EGF_Element::Fire, 0.5f },   { EGF_Element::Water, 0.5f },
				{ EGF_Element::Ice, 0.5f },
			}},
			{ EGF_Element::Fight, {
				{ EGF_Element::Light, 2.0f },   { EGF_Element::Ice, 2.0f },
				{ EGF_Element::Dark, 2.0f },
				{ EGF_Element::Poison, 0.5f }, { EGF_Element::Flying, 0.5f },
				{ EGF_Element::Fairy, 0.5f },
				{ EGF_Element::Ghost, 0.0f },
			}},
			{ EGF_Element::Poison, {
				{ EGF_Element::Grass, 2.0f },  { EGF_Element::Fairy, 2.0f },
				{ EGF_Element::Poison, 0.5f }, { EGF_Element::Ghost, 0.5f },
			}},
			{ EGF_Element::Flying, {
				{ EGF_Element::Grass, 2.0f },  { EGF_Element::Fight, 2.0f },
				{ EGF_Element::Electric, 0.5f },
			}},
			{ EGF_Element::Ghost, {
				{ EGF_Element::Ghost, 2.0f },
				{ EGF_Element::Dark, 0.5f },
				{ EGF_Element::Light, 0.0f },
			}},
			{ EGF_Element::Dragon, {
				{ EGF_Element::Dragon, 2.0f },
				{ EGF_Element::Fairy, 0.0f },
			}},
			{ EGF_Element::Dark, {
				{ EGF_Element::Ghost, 2.0f },
				{ EGF_Element::Fight, 0.5f },  { EGF_Element::Dark, 0.5f },
				{ EGF_Element::Fairy, 0.5f },
			}},
			{ EGF_Element::Fairy, {
				{ EGF_Element::Fight, 2.0f },  { EGF_Element::Dragon, 2.0f },
				{ EGF_Element::Dark, 2.0f },
				{ EGF_Element::Fire, 0.5f },   { EGF_Element::Poison, 0.5f },
			}},
		};
		return Table;
	}
}

float UGF_ElementLibrary::GetMatchup(EGF_Element Attacking, EGF_Element Defending)
{
	if (Attacking == EGF_Element::None || Defending == EGF_Element::None
		|| Attacking == EGF_Element::MAX || Defending == EGF_Element::MAX)
	{
		return 1.0f;
	}

	if (const TArray<FMatchup>* Row = Chart().Find(Attacking))
	{
		for (const FMatchup& Cell : *Row)
		{
			if (Cell.Defending == Defending)
			{
				return Cell.Multiplier;
			}
		}
	}
	return 1.0f;
}

float UGF_ElementLibrary::GetEffectiveness(EGF_Element Attacking, EGF_Element Defending1, EGF_Element Defending2)
{
	const float First = GetMatchup(Attacking, Defending1);
	if (Defending2 == EGF_Element::None || Defending2 == Defending1)
	{
		return First;
	}
	return First * GetMatchup(Attacking, Defending2);
}

FText UGF_ElementLibrary::GetElementDisplayName(EGF_Element Element)
{
	// Deliberately not UEnum::GetDisplayNameTextByValue: UMETA(DisplayName) is
	// stripped from cooked builds, so enum-derived UI text goes blank in a
	// packaged game. Every user-facing element label has to be spelled out here.
	switch (Element)
	{
		case EGF_Element::Fire:     return NSLOCTEXT("GammaFramework", "Element_Fire",     "Fire");
		case EGF_Element::Grass:    return NSLOCTEXT("GammaFramework", "Element_Grass",    "Grass");
		case EGF_Element::Water:    return NSLOCTEXT("GammaFramework", "Element_Water",    "Water");
		case EGF_Element::Electric: return NSLOCTEXT("GammaFramework", "Element_Electric", "Electric");
		case EGF_Element::Dark:     return NSLOCTEXT("GammaFramework", "Element_Dark",     "Dark");
		case EGF_Element::Light:    return NSLOCTEXT("GammaFramework", "Element_Light",    "Light");
		case EGF_Element::Flying:   return NSLOCTEXT("GammaFramework", "Element_Flying",   "Flying");
		case EGF_Element::Fight:    return NSLOCTEXT("GammaFramework", "Element_Fight",    "Fight");
		case EGF_Element::Poison:   return NSLOCTEXT("GammaFramework", "Element_Poison",   "Poison");
		case EGF_Element::Dragon:   return NSLOCTEXT("GammaFramework", "Element_Dragon",   "Dragon");
		case EGF_Element::Fairy:    return NSLOCTEXT("GammaFramework", "Element_Fairy",    "Fairy");
		case EGF_Element::Ghost:    return NSLOCTEXT("GammaFramework", "Element_Ghost",    "Ghost");
		case EGF_Element::Ice:      return NSLOCTEXT("GammaFramework", "Element_Ice",      "Ice");
		default:                    return NSLOCTEXT("GammaFramework", "Element_None",     "None");
	}
}

FLinearColor UGF_ElementLibrary::GetElementColor(EGF_Element Element)
{
	switch (Element)
	{
		case EGF_Element::Fire:     return FLinearColor(0.94f, 0.38f, 0.05f, 1.f);
		case EGF_Element::Grass:    return FLinearColor(0.34f, 0.75f, 0.29f, 1.f);
		case EGF_Element::Water:    return FLinearColor(0.24f, 0.53f, 0.92f, 1.f);
		case EGF_Element::Electric: return FLinearColor(0.98f, 0.82f, 0.18f, 1.f);
		case EGF_Element::Dark:     return FLinearColor(0.27f, 0.22f, 0.30f, 1.f);
		// Light and Electric are both yellow, so Light is pulled toward cream to
		// keep the two badges readable side by side.
		case EGF_Element::Light:    return FLinearColor(0.98f, 0.94f, 0.72f, 1.f);
		case EGF_Element::Flying:   return FLinearColor(0.66f, 0.79f, 0.96f, 1.f);
		case EGF_Element::Fight:    return FLinearColor(0.75f, 0.33f, 0.25f, 1.f);
		case EGF_Element::Poison:   return FLinearColor(0.64f, 0.31f, 0.65f, 1.f);
		case EGF_Element::Dragon:   return FLinearColor(0.42f, 0.36f, 0.84f, 1.f);
		case EGF_Element::Fairy:    return FLinearColor(0.96f, 0.62f, 0.80f, 1.f);
		case EGF_Element::Ghost:    return FLinearColor(0.45f, 0.38f, 0.62f, 1.f);
		case EGF_Element::Ice:      return FLinearColor(0.60f, 0.89f, 0.90f, 1.f);
		default:                    return FLinearColor(0.50f, 0.50f, 0.50f, 1.f);
	}
}

FString UGF_ElementLibrary::GetElementAssetToken(EGF_Element Element)
{
	switch (Element)
	{
		case EGF_Element::Fire:     return TEXT("FIRE");
		case EGF_Element::Grass:    return TEXT("GRASS");
		case EGF_Element::Water:    return TEXT("WATER");
		case EGF_Element::Electric: return TEXT("ELECTRIC");
		case EGF_Element::Dark:     return TEXT("DARK");
		case EGF_Element::Light:    return TEXT("LIGHT");
		case EGF_Element::Flying:   return TEXT("FLYING");
		case EGF_Element::Fight:    return TEXT("FIGHT");
		case EGF_Element::Poison:   return TEXT("POISON");
		case EGF_Element::Dragon:   return TEXT("DRAGON");
		case EGF_Element::Fairy:    return TEXT("FAIRY");
		case EGF_Element::Ghost:    return TEXT("GHOST");
		case EGF_Element::Ice:      return TEXT("ICE");
		// None gets the plain sheet.
		default:                    return TEXT("000");
	}
}
