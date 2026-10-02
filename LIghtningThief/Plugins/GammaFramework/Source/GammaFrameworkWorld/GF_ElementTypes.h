#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_StatusTypes.h"    // EGF_STATUSEffect, re-exported for convenience
#include "GF_WeatherTypes.h"   // EGF_WeatherType, likewise
#include "GF_ElementTypes.generated.h"

/**
 * The one element enum.
 *
 * The codebase this was ported from declared the same element list eight
 * separate times -- primary and secondary variants for both creatures and
 * skills, plus separate weakness, resistance and immunity copies -- and carried
 * a converter library whose entire job was translating between two identical
 * copies of it. All eight collapse here, and the converter is gone.
 *
 * The roster is the thirteen Dokimon elements from the Lightning Thief GDD.
 * Neutral is not one of them: it is kept only as the element of plain, typeless
 * skills (Clobber, Cut, Protect...), and no creature should be given it.
 *
 * The chart is a classic-chart placeholder for now. See GetMatchup.
 */
UENUM(BlueprintType)
enum class EGF_Element : uint8
{
	None      UMETA(DisplayName = "None"),

	// Typeless skills only.
	Neutral   UMETA(DisplayName = "Neutral"),

	Fire      UMETA(DisplayName = "Fire"),
	Grass     UMETA(DisplayName = "Grass"),
	Water     UMETA(DisplayName = "Water"),
	Electric  UMETA(DisplayName = "Electric"),
	Dark      UMETA(DisplayName = "Dark"),
	Light     UMETA(DisplayName = "Light"),
	Flying    UMETA(DisplayName = "Flying"),
	Fight     UMETA(DisplayName = "Fight"),
	Poison    UMETA(DisplayName = "Poison"),
	Dragon    UMETA(DisplayName = "Dragon"),
	Fairy     UMETA(DisplayName = "Fairy"),
	Ghost     UMETA(DisplayName = "Ghost"),
	Ice       UMETA(DisplayName = "Ice"),

	MAX       UMETA(Hidden)
};

/**
 * Which offensive/defensive stat pair a skill resolves against.
 *
 * The classic Special split is reframed as magic here, so the category
 * names match the stat names exactly:
 *   Physical -> Attack vs Defense
 *   Magic    -> Magic  vs Poise
 *   Status   -> no damage
 */
UENUM(BlueprintType)
enum class EGF_SkillCategory : uint8
{
	Physical UMETA(DisplayName = "Physical"),
	Magic    UMETA(DisplayName = "Magic"),
	Status   UMETA(DisplayName = "Status"),
};

/**
 * How many seats a skill reaches.
 *
 * Authored on the skill asset, which lives in the Creatures module, and read by
 * the battle flow, which lives above it -- so like EGF_SkillCategory it belongs
 * here in the shared layer rather than in either of them.
 *
 * A single-battle system has no use for this: every skill hit the one creature
 * opposite. It exists because the board is four wide.
 */
UENUM(BlueprintType)
enum class EGF_SkillTargetShape : uint8
{
	/** One chosen seat, on either side. */
	Single     UMETA(DisplayName = "Single"),

	/** The user only. Recover, Focus Energy, Brace. */
	Self       UMETA(DisplayName = "Self"),

	/** Every living seat on the opposing side. Takes the spread damage cut. */
	AllEnemies UMETA(DisplayName = "All Enemies"),

	/** Every living seat on the user's own side, the user included. */
	AllAllies  UMETA(DisplayName = "All Allies"),

	/** Everything on the field. */
	Everyone   UMETA(DisplayName = "Everyone"),
};

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_ElementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Single attacker-vs-defender multiplier. 0.0 / 0.5 / 1.0 / 2.0. */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Elements")
	static float GetMatchup(EGF_Element Attacking, EGF_Element Defending);

	/**
	 * Full effectiveness against a creature, second element included.
	 * Pass EGF_Element::None as Defending2 for a single-element creature.
	 */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Elements")
	static float GetEffectiveness(EGF_Element Attacking, EGF_Element Defending1, EGF_Element Defending2);

	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Elements")
	static FText GetElementDisplayName(EGF_Element Element);

	/** UI tint for an element -- debugger pages, type badges, skill buttons. */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Elements")
	static FLinearColor GetElementColor(EGF_Element Element);

	/**
	 * Uppercase token used to build per-element asset names, e.g.
	 * SPR_FIRE_icon_EGG_Sprite_0. Returns "000" for None and Neutral, which is
	 * the plain/fallback sheet.
	 */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Elements")
	static FString GetElementAssetToken(EGF_Element Element);
};
