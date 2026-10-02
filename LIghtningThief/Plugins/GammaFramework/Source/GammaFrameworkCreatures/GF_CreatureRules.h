#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureRules.generated.h"

/**
 * The tunable half of the Dokimon: Lightning Thief creature rules -- how fast
 * affinity grows, when move slots open, how strong STAB is.
 *
 * Edit in Project Settings > Game > Gamma Framework Creature Rules.
 *
 * The fixed half (AP cap 50, affinity cap 100, 1 EP per level) lives on
 * FGF_CreatureInstanceData as constants, because save and trade validation
 * have to agree on them and must not drift with a config edit.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Gamma Framework Creature Rules"))
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureRulesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	// --------------------------------------------------------
	// AFFINITY
	// --------------------------------------------------------

	/**
	 * Affinity each battler gains per foe defeated while it is in the fight.
	 * 1 means roughly a hundred knockouts from 0 to max affinity.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Affinity", meta = (ClampMin = "0", ClampMax = "100"))
	int32 AffinityPerFoeDefeated = 1;

	// --------------------------------------------------------
	// SKILL SLOTS
	// --------------------------------------------------------

	/** Move slots a creature has before any unlock. */
	UPROPERTY(Config, EditAnywhere, Category = "Skill Slots", meta = (ClampMin = "1", ClampMax = "4"))
	int32 StartingSkillSlots = 2;

	/** Level that opens the third move slot. */
	UPROPERTY(Config, EditAnywhere, Category = "Skill Slots", meta = (ClampMin = "1", ClampMax = "100"))
	int32 ThirdSkillSlotLevel = 7;

	/** Level that opens the fourth move slot. */
	UPROPERTY(Config, EditAnywhere, Category = "Skill Slots", meta = (ClampMin = "1", ClampMax = "100"))
	int32 FourthSkillSlotLevel = 12;

	// --------------------------------------------------------
	// STAB
	// --------------------------------------------------------

	/** Same-element bonus for a creature with two elements. */
	UPROPERTY(Config, EditAnywhere, Category = "STAB", meta = (ClampMin = "1.0"))
	float DualElementSTAB = 1.5f;

	/** Same-element bonus for a single-element creature -- stronger, per the GDD. */
	UPROPERTY(Config, EditAnywhere, Category = "STAB", meta = (ClampMin = "1.0"))
	float SingleElementSTAB = 1.75f;

	/** Added on top of either STAB value by the Adaptability-style trait. */
	UPROPERTY(Config, EditAnywhere, Category = "STAB", meta = (ClampMin = "0.0"))
	float AdaptabilitySTABBonus = 0.5f;

	// --------------------------------------------------------
	// HELPERS
	// --------------------------------------------------------

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
