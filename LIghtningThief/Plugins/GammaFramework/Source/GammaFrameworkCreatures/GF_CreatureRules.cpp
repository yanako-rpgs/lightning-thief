#include "GF_CreatureRules.h"

int32 UGF_CreatureRulesSettings::GetSkillSlotsForLevel(int32 Level)
{
	const UGF_CreatureRulesSettings* Rules = GetDefault<UGF_CreatureRulesSettings>();

	int32 Slots = Rules->StartingSkillSlots;
	if (Level >= Rules->ThirdSkillSlotLevel)
	{
		++Slots;
	}
	if (Level >= Rules->FourthSkillSlotLevel)
	{
		++Slots;
	}
	return FMath::Clamp(Slots, 1, MaxSkillSlots);
}

float UGF_CreatureRulesSettings::GetSTABMultiplier(EGF_Element SkillElement, EGF_Element Primary, EGF_Element Secondary,
	bool bHasAdaptability)
{
	// None never earns STAB: a creature with no secondary element would otherwise
	// "match" every skill that was left without an element.
	if (SkillElement == EGF_Element::None)
	{
		return 1.0f;
	}

	if (SkillElement != Primary && SkillElement != Secondary)
	{
		return 1.0f;
	}

	const UGF_CreatureRulesSettings* Rules = GetDefault<UGF_CreatureRulesSettings>();
	const bool bSingleElement = (Secondary == EGF_Element::None || Secondary == Primary);

	const float Base = bSingleElement ? Rules->SingleElementSTAB : Rules->DualElementSTAB;
	return bHasAdaptability ? Base + Rules->AdaptabilitySTABBonus : Base;
}
