#pragma once

#include "CoreMinimal.h"
#include "GF_WeatherTypes.generated.h"

/**
 * Weather state.
 *
 * Lives in World rather than with the battle component that used to declare it,
 * because weather is not exclusively a battle concept -- the overworld has it
 * too, creature traits read it, and items react to it. Putting it here lets all
 * three name it without anyone depending on the battle module.
 *
 * Included by GF_ElementTypes.h, so anything already pulling in the element enum
 * gets this as well.
 */
UENUM(BlueprintType)
enum class EGF_WeatherType : uint8
{
	None      UMETA(DisplayName = "None"),
	HarshSun  UMETA(DisplayName = "Harsh Sunlight"),
	Rain      UMETA(DisplayName = "Rain"),
	Sandstorm UMETA(DisplayName = "Sandstorm"),
	Hail      UMETA(DisplayName = "Hail")
};
