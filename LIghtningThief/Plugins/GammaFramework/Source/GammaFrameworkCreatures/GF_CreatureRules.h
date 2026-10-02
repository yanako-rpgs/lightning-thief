#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureRules.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Gamma Framework Creature Rules"))
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureRulesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	UPROPERTY(Config, EditAnywhere, Category = "Affinity", meta = (ClampMin = "0", ClampMax = "100"))
	int32 AffinityPerFoeDefeated = 1;

	UPROPERTY(Config, EditAnywhere, Category = "Skill Slots", meta = (ClampMin = "1", ClampMax = "4"))
	int32 StartingSkillSlots = 2;

	UPROPERTY(Config, EditAnywhere, Category = "Skill Slots", meta = (ClampMin = "1", ClampMax = "100"))
	int32 ThirdSkillSlotLevel = 7;

	UPROPERTY(Config, EditAnywhere, Category = "Skill Slots", meta = (ClampMin = "1", ClampMax = "100"))
	int32 FourthSkillSlotLevel = 12;

	UPROPERTY(Config, EditAnywhere, Category = "STAB", meta = (ClampMin = "1.0"))
	float DualElementSTAB = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "STAB", meta = (ClampMin = "1.0"))
	float SingleElementSTAB = 1.75f;

	UPROPERTY(Config, EditAnywhere, Category = "STAB", meta = (ClampMin = "0.0"))
	float AdaptabilitySTABBonus = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Critical Hits")
	TArray<float> CritChancePercentByStage = { 10.0f, 35.0f, 50.0f, 75.0f, 100.0f };

	UPROPERTY(Config, EditAnywhere, Category = "Protect")
	TArray<float> ProtectChancePercentByStreak = { 100.0f, 50.0f, 25.0f };

	// --------------------------------------------------------
	// HELPERS
	// --------------------------------------------------------

	static float GetCritChancePercent(int32 Stage);

	static float GetProtectChancePercent(int32 ConsecutiveUses);

	/** Hard ceiling on move slots. Nothing in the battle UI can show a fifth. */
	static constexpr int32 MaxSkillSlots = 4;

	/** How many moves a creature of this level may know (2, then 3 at Lv 7, 4 at Lv 12 by default). */
	static int32 GetSkillSlotsForLevel(int32 Level);

	/**
	 * STAB multiplier for a skill used by a creature with these elements.
	 * 1.0 when the skill's element is not one of the creature's. A creature is
	 * single-element when its secondary is None or repeats the primary.
	 */
	static float GetSTABMultiplier(EGF_Element SkillElement, EGF_Element Primary, EGF_Element Secondary,
		bool bHasAdaptability = false);
};
