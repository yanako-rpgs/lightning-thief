#pragma once

#include "CoreMinimal.h"
#include "GF_StatusTypes.generated.h"

/**
 * Persistent status conditions.
 *
 * Lives in Core rather than with the skill definitions that inflict them,
 * because items cure them and items are Core. Nothing here is creature-specific
 * -- burn, poison and sleep are ordinary RPG status effects.
 *
 * Included by GF_ElementTypes.h, so anything that already pulls in the element
 * enum gets these too.
 */
UENUM(BlueprintType)
enum class EGF_STATUSEffect : uint8
{
	None        UMETA(DisplayName = "None"),
	Burned      UMETA(DisplayName = "Burned"),
	Paralyzed   UMETA(DisplayName = "Paralyzed"),
	Poisoned    UMETA(DisplayName = "Poisoned"),
	Sleeping    UMETA(DisplayName = "Sleeping"),
	Frozen      UMETA(DisplayName = "Frozen"),
	Invigorated UMETA(DisplayName = "Invigorated"),
	Confused    UMETA(DisplayName = "Confused")
};
