// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GF_Creature.h"
#include "GF_BattleManager.generated.h"

class AGF_Creature;

UENUM(BlueprintType)
enum class EGF_Enum_BattleState : uint8
{

	Intro UMETA(DisplayName = "Intro"),
	PlayerTurn UMETA(DisplayName = "Player Turn"),
	EnemyTurn UMETA(DisplayName = "Enemy Turn"),
	Win UMETA(DisplayName = "Win"),
	Lose UMETA(DisplayName = "Lose"),
	DecidingWhatToDo UMETA(DisplayName = "Deciding What To Do")
};

USTRUCT(BlueprintType)
struct FGF_PartyCreature
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "Creature Party.", Category = "GammaEngine | Battle"))
	TSoftClassPtr<AGF_Creature> CreatureClass;
};

UCLASS()
class GAMMAFRAMEWORKBATTLE_API AGF_BattleManager : public AActor
{
	GENERATED_BODY()

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:
	// Sets default values for this actor's properties
	AGF_BattleManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaEngine | Battle", meta = (ExposeOnSpawn = true))
	AGF_Creature* PlayerCreature;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaEngine | Battle", meta = (ExposeOnSpawn = true))
	AGF_Creature* EnemyCreature;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaEngine | Battle")
	bool isPlayerTurn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaEngine | Battle")
	EGF_Enum_BattleState BattleState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "The move that is being readied for the next turn.", Category = "GammaEngine | Battle"))
	TSubclassOf<AGF_SkillDefinition> SkillInQueue;




	UFUNCTION(BlueprintCallable, Category = "GammaEngine | Battle")
	void StartBattle();

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Player Attack Command", keywords = "UndreamedPanic, Creature, Toolkit, GammaEngine", ToolTip = "This will tell the Battle Manager to do the Player Turn. (Example: Do player Attack)"), Category = "Gamma Framework | Battle Commands")
	void PlayerTurn(TArray<TSubclassOf<AGF_SkillDefinition>> SelectedSkill);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Enemy Attack Command", keywords = "UndreamedPanic, Creature, Toolkit, GammaEngine", ToolTip = "This will tell the Battle Manager to do the Enemy Turn. (Example: Do Enemy Attack)"), Category = "Gamma Framework | Battle Commands")
	void EnemyTurn();






private:
	void ExecuteTurn(AGF_Creature* AttackingCreature, AGF_Creature* TargetCreature, const TArray<TSubclassOf<AGF_SkillDefinition>> SelectedSkill);
	void CheckDown();



};
