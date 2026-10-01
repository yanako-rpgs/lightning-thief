#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GF_BattleParticipantInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UGF_BattleParticipantInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by any actor that participates in a battle as an opposing tamer.
 * Allows the battle system to notify participants of turn events without
 * hard references to TamerMaster or any specific class.
 * Scales cleanly to double battles — just call on every participant.
 */
class GAMMAFRAMEWORKBATTLE_API IGF_BattleParticipantInterface
{
    GENERATED_BODY()

public:
    // Called at the end of every turn resolution, before the action menu reappears.
    // Tamers use this to tick down switch cooldowns, etc.
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Battle")
    void OnBattleTurnPassed();
};
