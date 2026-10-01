#include "GF_ElementTypes.h"

namespace
{
	// Row = attacking element, column = defending element.
	//
	//   x2   super effective
	//   x0.5 resisted
	//   x0   immune
	//
	// The structure, rather than a table of arbitrary numbers:
	//
	//   Primal cycle      Ember > Verdant > Tide  > Ember
	//   Kinetic cycle     Gale  > Terra   > Spark > Gale
	//   Attrition cycle   Sinew > Ferrous > Chitin > Sinew
	//                     (muscle bends metal, metal crushes carapace, the
	//                      swarm outlasts the fighter)
	//   Opposed pair      Lumen <-> Umbra, mutually x2
	//   Wyrm              beats only itself, and Lumen is flatly immune to it --
	//                     a narrow, high-power element rather than a broad one
	//   Terra / Stone     Terra is soil: it grounds Spark and cannot touch Gale.
	//                     Stone is hard mineral: it knocks Gale out of the sky.
	//   Ferrous rule      Ferrous never both resists an element AND hits it x2.
	//                     It resisted 11 of 17 before that rule, which made it the
	//                     default correct answer in any defensive slot. Chitin,
	//                     Frost, Stone and Lumen are the four it beats offensively,
	//                     so they now do neutral damage back. Seven resistances
	//                     left -- still clearly the armoured element, no longer
	//                     strictly the best one.
	//
	// Five immunities, each doing a specific job:
	//   Neutral -> Umbra   (nothing mundane touches a shade)
	//   Terra   -> Gale    (no ground to throw)
	//   Spark   -> Terra   (grounded)
	//   Venom   -> Ferrous (nothing to poison)
	//   Wyrm    -> Lumen   (the one hard counter to the strongest element)
	struct FMatchup
	{
		EGF_Element Defending;
		float       Multiplier;
	};

	// Only the non-1.0 cells are listed; everything unlisted is neutral damage.
	const TMap<EGF_Element, TArray<FMatchup>>& Chart()
	{
		static const TMap<EGF_Element, TArray<FMatchup>> Table = {
			{ EGF_Element::Neutral, {
				{ EGF_Element::Ferrous, 0.5f }, { EGF_Element::Stone, 0.5f },
				{ EGF_Element::Umbra, 0.0f },
			}},
			{ EGF_Element::Sinew, {
				{ EGF_Element::Neutral, 2.0f }, { EGF_Element::Stone, 2.0f },
				{ EGF_Element::Ferrous, 2.0f }, { EGF_Element::Frost, 2.0f },
				{ EGF_Element::Umbra, 2.0f },
				{ EGF_Element::Gale, 0.5f },    { EGF_Element::Venom, 0.5f },
				{ EGF_Element::Aether, 0.5f },  { EGF_Element::Lumen, 0.5f },
			}},
			{ EGF_Element::Ember, {
				{ EGF_Element::Verdant, 2.0f }, { EGF_Element::Frost, 2.0f },
				{ EGF_Element::Ferrous, 2.0f }, { EGF_Element::Chitin, 2.0f },
				{ EGF_Element::Tide, 0.5f },    { EGF_Element::Stone, 0.5f },
				{ EGF_Element::Ember, 0.5f },   { EGF_Element::Wyrm, 0.5f },
			}},
			{ EGF_Element::Tide, {
				{ EGF_Element::Ember, 2.0f },   { EGF_Element::Stone, 2.0f },
				{ EGF_Element::Terra, 2.0f },
				{ EGF_Element::Verdant, 0.5f }, { EGF_Element::Tide, 0.5f },
				{ EGF_Element::Spark, 0.5f },   { EGF_Element::Wyrm, 0.5f },
			}},
			{ EGF_Element::Verdant, {
				{ EGF_Element::Tide, 2.0f },    { EGF_Element::Stone, 2.0f },
				{ EGF_Element::Terra, 2.0f },
				{ EGF_Element::Ember, 0.5f },   { EGF_Element::Gale, 0.5f },
				{ EGF_Element::Venom, 0.5f },   { EGF_Element::Chitin, 0.5f },
				{ EGF_Element::Verdant, 0.5f }, { EGF_Element::Ferrous, 0.5f },
				{ EGF_Element::Wyrm, 0.5f },
			}},
			{ EGF_Element::Chitin, {
				{ EGF_Element::Sinew, 2.0f },   { EGF_Element::Verdant, 2.0f },
				{ EGF_Element::Aether, 2.0f },  { EGF_Element::Umbra, 2.0f },
				{ EGF_Element::Ember, 0.5f },   { EGF_Element::Gale, 0.5f },
				{ EGF_Element::Venom, 0.5f },
				{ EGF_Element::Lumen, 0.5f },   { EGF_Element::Stone, 0.5f },
			}},
			{ EGF_Element::Gale, {
				{ EGF_Element::Terra, 2.0f },   { EGF_Element::Verdant, 2.0f },
				{ EGF_Element::Sinew, 2.0f },   { EGF_Element::Chitin, 2.0f },
				{ EGF_Element::Ferrous, 0.5f }, { EGF_Element::Spark, 0.5f },
				{ EGF_Element::Stone, 0.5f },
			}},
			{ EGF_Element::Terra, {
				{ EGF_Element::Spark, 2.0f },   { EGF_Element::Ember, 2.0f },
				{ EGF_Element::Venom, 2.0f },   { EGF_Element::Stone, 2.0f },
				{ EGF_Element::Ferrous, 2.0f },
				{ EGF_Element::Verdant, 0.5f }, { EGF_Element::Chitin, 0.5f },
				{ EGF_Element::Gale, 0.0f },    // no ground to throw
			}},
			{ EGF_Element::Stone, {
				{ EGF_Element::Ember, 2.0f },   { EGF_Element::Frost, 2.0f },
				{ EGF_Element::Gale, 2.0f },    { EGF_Element::Chitin, 2.0f },
				{ EGF_Element::Sinew, 0.5f },   { EGF_Element::Terra, 0.5f },
			}},
			{ EGF_Element::Spark, {
				{ EGF_Element::Gale, 2.0f },    { EGF_Element::Tide, 2.0f },
				{ EGF_Element::Ferrous, 2.0f },
				{ EGF_Element::Verdant, 0.5f }, { EGF_Element::Spark, 0.5f },
				{ EGF_Element::Wyrm, 0.5f },
				{ EGF_Element::Terra, 0.0f },   // grounded
			}},
			{ EGF_Element::Ferrous, {
				{ EGF_Element::Chitin, 2.0f },  { EGF_Element::Frost, 2.0f },
				{ EGF_Element::Stone, 2.0f },   { EGF_Element::Lumen, 2.0f },
				{ EGF_Element::Ember, 0.5f },   { EGF_Element::Tide, 0.5f },
				{ EGF_Element::Spark, 0.5f },   { EGF_Element::Ferrous, 0.5f },
			}},
			{ EGF_Element::Frost, {
				{ EGF_Element::Verdant, 2.0f }, { EGF_Element::Gale, 2.0f },
				{ EGF_Element::Terra, 2.0f },   { EGF_Element::Wyrm, 2.0f },
				{ EGF_Element::Ember, 0.5f },   { EGF_Element::Tide, 0.5f },
				{ EGF_Element::Frost, 0.5f },
			}},
			{ EGF_Element::Venom, {
				{ EGF_Element::Verdant, 2.0f }, { EGF_Element::Lumen, 2.0f },
				{ EGF_Element::Terra, 0.5f },   { EGF_Element::Stone, 0.5f },
				{ EGF_Element::Venom, 0.5f },   { EGF_Element::Umbra, 0.5f },
				{ EGF_Element::Ferrous, 0.0f }, // nothing to poison
			}},
			{ EGF_Element::Umbra, {
				{ EGF_Element::Aether, 2.0f },  { EGF_Element::Lumen, 2.0f },
				{ EGF_Element::Umbra, 0.5f },   { EGF_Element::Sinew, 0.5f },
			}},
			{ EGF_Element::Lumen, {
				{ EGF_Element::Umbra, 2.0f },   { EGF_Element::Sinew, 2.0f },
				{ EGF_Element::Wyrm, 2.0f },
				{ EGF_Element::Ember, 0.5f },   { EGF_Element::Venom, 0.5f },
				{ EGF_Element::Lumen, 0.5f },
			}},
			{ EGF_Element::Aether, {
				{ EGF_Element::Sinew, 2.0f },   { EGF_Element::Venom, 2.0f },
				{ EGF_Element::Aether, 0.5f },  { EGF_Element::Ferrous, 0.5f },
				{ EGF_Element::Umbra, 0.5f },
			}},
			{ EGF_Element::Wyrm, {
				{ EGF_Element::Wyrm, 2.0f },
				{ EGF_Element::Ferrous, 0.5f }, { EGF_Element::Stone, 0.5f },
				{ EGF_Element::Lumen, 0.0f },   // the hard counter
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
		case EGF_Element::Neutral: return NSLOCTEXT("GammaFramework", "Element_Neutral", "Neutral");
		case EGF_Element::Sinew:   return NSLOCTEXT("GammaFramework", "Element_Sinew",   "Sinew");
		case EGF_Element::Ember:   return NSLOCTEXT("GammaFramework", "Element_Ember",   "Ember");
		case EGF_Element::Tide:    return NSLOCTEXT("GammaFramework", "Element_Tide",    "Tide");
		case EGF_Element::Verdant: return NSLOCTEXT("GammaFramework", "Element_Verdant", "Verdant");
		case EGF_Element::Chitin:  return NSLOCTEXT("GammaFramework", "Element_Chitin",  "Chitin");
		case EGF_Element::Gale:    return NSLOCTEXT("GammaFramework", "Element_Gale",    "Gale");
		case EGF_Element::Terra:   return NSLOCTEXT("GammaFramework", "Element_Terra",   "Terra");
		case EGF_Element::Stone:   return NSLOCTEXT("GammaFramework", "Element_Stone",   "Stone");
		case EGF_Element::Spark:   return NSLOCTEXT("GammaFramework", "Element_Spark",   "Spark");
		case EGF_Element::Ferrous: return NSLOCTEXT("GammaFramework", "Element_Ferrous", "Ferrous");
		case EGF_Element::Frost:   return NSLOCTEXT("GammaFramework", "Element_Frost",   "Frost");
		case EGF_Element::Venom:   return NSLOCTEXT("GammaFramework", "Element_Venom",   "Venom");
		case EGF_Element::Umbra:   return NSLOCTEXT("GammaFramework", "Element_Umbra",   "Umbra");
		case EGF_Element::Lumen:   return NSLOCTEXT("GammaFramework", "Element_Lumen",   "Lumen");
		case EGF_Element::Aether:  return NSLOCTEXT("GammaFramework", "Element_Aether",  "Aether");
		case EGF_Element::Wyrm:    return NSLOCTEXT("GammaFramework", "Element_Wyrm",    "Wyrm");
		default:                   return NSLOCTEXT("GammaFramework", "Element_None",    "None");
	}
}

FLinearColor UGF_ElementLibrary::GetElementColor(EGF_Element Element)
{
	switch (Element)
	{
		case EGF_Element::Neutral: return FLinearColor(0.66f, 0.66f, 0.46f, 1.f);
		case EGF_Element::Sinew:   return FLinearColor(0.75f, 0.33f, 0.25f, 1.f);
		case EGF_Element::Ember:   return FLinearColor(0.94f, 0.38f, 0.05f, 1.f);
		case EGF_Element::Tide:    return FLinearColor(0.24f, 0.53f, 0.92f, 1.f);
		case EGF_Element::Verdant: return FLinearColor(0.34f, 0.75f, 0.29f, 1.f);
		case EGF_Element::Chitin:  return FLinearColor(0.65f, 0.73f, 0.18f, 1.f);
		case EGF_Element::Gale:    return FLinearColor(0.66f, 0.79f, 0.96f, 1.f);
		// Terra and Stone are both earthy, so they are pulled apart deliberately:
		// Terra warm sand, Stone cool grey. Two browns would be unreadable as
		// adjacent type badges.
		case EGF_Element::Terra:   return FLinearColor(0.82f, 0.66f, 0.34f, 1.f);
		case EGF_Element::Stone:   return FLinearColor(0.58f, 0.55f, 0.50f, 1.f);
		case EGF_Element::Spark:   return FLinearColor(0.98f, 0.82f, 0.18f, 1.f);
		case EGF_Element::Ferrous: return FLinearColor(0.60f, 0.64f, 0.70f, 1.f);
		case EGF_Element::Frost:   return FLinearColor(0.60f, 0.89f, 0.90f, 1.f);
		case EGF_Element::Venom:   return FLinearColor(0.64f, 0.31f, 0.65f, 1.f);
		case EGF_Element::Umbra:   return FLinearColor(0.27f, 0.22f, 0.30f, 1.f);
		case EGF_Element::Lumen:   return FLinearColor(0.96f, 0.80f, 0.86f, 1.f);
		case EGF_Element::Aether:  return FLinearColor(0.78f, 0.36f, 0.72f, 1.f);
		case EGF_Element::Wyrm:    return FLinearColor(0.42f, 0.36f, 0.84f, 1.f);
		default:                   return FLinearColor(0.50f, 0.50f, 0.50f, 1.f);
	}
}

FString UGF_ElementLibrary::GetElementAssetToken(EGF_Element Element)
{
	switch (Element)
	{
		case EGF_Element::Sinew:   return TEXT("SINEW");
		case EGF_Element::Ember:   return TEXT("EMBER");
		case EGF_Element::Tide:    return TEXT("TIDE");
		case EGF_Element::Verdant: return TEXT("VERDANT");
		case EGF_Element::Chitin:  return TEXT("CHITIN");
		case EGF_Element::Gale:    return TEXT("GALE");
		case EGF_Element::Terra:   return TEXT("TERRA");
		case EGF_Element::Stone:   return TEXT("STONE");
		case EGF_Element::Spark:   return TEXT("SPARK");
		case EGF_Element::Ferrous: return TEXT("FERROUS");
		case EGF_Element::Frost:   return TEXT("FROST");
		case EGF_Element::Venom:   return TEXT("VENOM");
		case EGF_Element::Umbra:   return TEXT("UMBRA");
		case EGF_Element::Lumen:   return TEXT("LUMEN");
		case EGF_Element::Aether:  return TEXT("AETHER");
		case EGF_Element::Wyrm:    return TEXT("WYRM");
		// Neutral and None share the plain sheet.
		default:                   return TEXT("000");
	}
}
