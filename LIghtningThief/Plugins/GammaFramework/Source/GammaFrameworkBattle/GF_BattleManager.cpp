// Fill out your copyright notice in the Description page of Project Settings.


#include "GF_BattleManager.h"
#include "GF_Creature.h"
#include "Engine/Engine.h"
#include "GF_SkillDefinition.h"
#include "Kismet/GameplayStatics.h"



// Sets default values
AGF_BattleManager::AGF_BattleManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AGF_BattleManager::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void AGF_BattleManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}



void AGF_BattleManager::StartBattle()
{
	if (!PlayerCreature || !EnemyCreature)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Creature references were null in the Battle Manager!"));
			UE_LOG(LogTemp, Error, TEXT("Creature references were null in the Battle Manager!"))
		}
			return;
	}


	//if (GEngine && PlayerCreature && EnemyCreature)
	//{
		//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Green, FString::Printf(TEXT("Battle Manager: Battle has started with %s and %s !"), *PlayerCreature->Name, *EnemyCreature->Name);
	//	FString Message =  FString::Printf(TEXT("Battle Manager: Battle has started with %s and %s!"),  *PlayerCreature->Name.ToString(), *EnemyCreature->Name.ToString());
	//	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Green, Message);

	//}

	//if (PlayerCreature->CurrentStats.Speed > EnemyCreature->CurrentStats.Speed)
	//{
	//	isPlayerTurn = true;
	//	BattleState = EGF_Enum_BattleState::PlayerTurn;
	//	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Battle Manager: Players turn!"));
	//	UE_LOG(LogTemp, Error, TEXT("Battle Manager: Players turn!"))
	//}

	//else
	//{
	///	isPlayerTurn = false;
	//	BattleState = EGF_Enum_BattleState::EnemyTurn;
	//	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Battle Manager: Enemies turn!"));
	//	UE_LOG(LogTemp, Error, TEXT("Battle Manager: Enemies turn!"))
	//}


}

void AGF_BattleManager::PlayerTurn(TArray<TSubclassOf<AGF_SkillDefinition>> SelectedSkill)
{
		//FVector SpawnLocation = PlayerCreature->GetActorLocation();
		//FRotator SpawnRotation = PlayerCreature->GetActorRotation();


		//TSubclassOf<AGF_SkillDefinition> SelectedSkill = PlayerCreature->Skills[0];

		//FActorSpawnParameters SpawnInfo;
		//SpawnInfo.Owner = this;
		//SpawnInfo.Instigator = GetInstigator();


			//AGF_SkillDefinition* SpawnedSkill = GetWorld()->SpawnActor<TArray<TSubclassOf<AGF_SkillDefinition>>(SelectedSkill, SpawnLocation, SpawnRotation, SpawnInfo);

		//FString Message = FString::Printf(TEXT("%s used %!"), *PlayerCreature->Name.ToString(), *SelectedSkill);
		//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Green, Message);
}

void AGF_BattleManager::EnemyTurn()
{
	//if (BattleState != EGF_Enum_BattleState::EnemyTurn)
	//	return;

	//const TArray<TSubclassOf<AGF_SkillDefinition>>& Skills = EnemyCreature->GetSkills();
	//if (Skills.Num() > 0)
	//{
	//	int32 SkillIndex = FMath::RandRange(0, Skills.Num() - 1);
	//	ExecuteTurn(EnemyCreature, PlayerCreature, Skills[SkillIndex]);
	//	CheckDown();

	//}
}

void AGF_BattleManager::ExecuteTurn(AGF_Creature* AttackingCreature, AGF_Creature* TargetCreature, const TArray<TSubclassOf<AGF_SkillDefinition>> SelectedSkill)
{
	if (!AttackingCreature || TargetCreature)
		return;

	//float Damage = ((AttackingCreature->GetLevel() * SelectedSkill.) / 5.0f) + FMath::RandRange(0, 5);
}






