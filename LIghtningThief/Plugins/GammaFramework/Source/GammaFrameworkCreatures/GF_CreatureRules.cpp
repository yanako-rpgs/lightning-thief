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

float UGF_CreatureRulesSettings::GetCritChancePercent(int32 Stage)
{
	const TArray<float>& Table = GetDefault<UGF_CreatureRulesSettings>()->CritChancePercentByStage;
	if (Table.Num() == 0)
	{
		return 0.0f;
	}
	return Table[FMath::Clamp(Stage, 0, Table.Num() - 1)];
}

float UGF_CreatureRulesSettings::GetProtectChancePercent(int32 ConsecutiveUses)
{
	const TArray<float>& Table = GetDefault<UGF_CreatureRulesSettings>()->ProtectChancePercentByStreak;
	return Table.IsValidIndex(ConsecutiveUses) ? Table[ConsecutiveUses] : 0.0f;
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
