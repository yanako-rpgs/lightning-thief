// Fill out your copyright notice in the Description page of Project Settings.


#include "GF_TamerMaster.h"
#include "GF_ElementTypes.h"
#include "GF_DiagLog.h"
#include "Settings/GF_SettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/Stack.h"   // FFrame::GetScriptCallstack - diagnostic in SendOutNextCreature

EGF_TamerAIDifficulty AGF_TamerMaster::GetEffectiveAIDifficulty() const
{
	const UGameInstance* GI = GetGameInstance();
	const UGF_SettingsSubsystem* Settings = GI ? GI->GetSubsystem<UGF_SettingsSubsystem>() : nullptr;
	if (!Settings)
	{
		return AIDifficulty;
	}

	const int32 Shifted = static_cast<int32>(AIDifficulty) + Settings->GetAIDifficultyOffset();
	const int32 MaxTier = static_cast<int32>(EGF_TamerAIDifficulty::Expert);

	return static_cast<EGF_TamerAIDifficulty>(FMath::Clamp(Shifted, 0, MaxTier));
}

// ============================================
// REMATCH TEAM SELECTION
// ============================================

bool AGF_TamerMaster::IsRematchRequested() const
{
	// The debug override is checked first so a tamer placed straight into a test map
	// fields its rematch team with no subsystem involved at all.
	if (bForceRematchTeam)
	{
		return true;
	}

	const UGameInstance* GI = GetGameInstance();
	const UGF_CreatureManagerSubsystem* CreatureManager = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;

	return CreatureManager ? CreatureManager->IsRematchBattle() : false;
}

bool AGF_TamerMaster::ShouldUseRematchTeam() const
{
	// An empty rematch team is not a rematch, whatever the flag says. Deciding that here
	// means InitializeTeam, the AI and anything else asking all get the same answer.
	return IsRematchRequested() && RematchTeamSetup.Num() > 0;
}

int32 AGF_TamerMaster::GetRematchRecommendedLevel() const
{
	const TArray<FGF_TamerCreatureSetup>& Source = RematchTeamSetup.Num() > 0 ? RematchTeamSetup : TeamSetup;

	int32 Highest = 0;
	for (const FGF_TamerCreatureSetup& Setup : Source)
	{
		Highest = FMath::Max(Highest, Setup.Level);
	}

	return Highest;
}

TArray<FGF_TamerCreatureSetup> AGF_TamerMaster::GetActiveTeamSetup() const
{
	// Same decision InitializeTeam made, so display and reality cannot drift apart.
	return ShouldUseRematchTeam() ? RematchTeamSetup : TeamSetup;
}

bool AGF_TamerMaster::HasRematchTeam(TSubclassOf<AGF_TamerMaster> TamerClass)
{
	if (!TamerClass)
	{
		return false;
	}

	const AGF_TamerMaster* CDO = TamerClass->GetDefaultObject<AGF_TamerMaster>();
	return CDO && CDO->RematchTeamSetup.Num() > 0;
}

int32 AGF_TamerMaster::GetRecommendedLevelForClass(TSubclassOf<AGF_TamerMaster> TamerClass)
{
	if (!TamerClass)
	{
		return 0;
	}

	const AGF_TamerMaster* CDO = TamerClass->GetDefaultObject<AGF_TamerMaster>();
	return CDO ? CDO->GetRematchRecommendedLevel() : 0;
}

TArray<FText> AGF_TamerMaster::GetRematchTeamPreview(TSubclassOf<AGF_TamerMaster> TamerClass)
{
	TArray<FText> Lines;

	if (!TamerClass)
	{
		return Lines;
	}

	const AGF_TamerMaster* CDO = TamerClass->GetDefaultObject<AGF_TamerMaster>();
	if (!CDO)
	{
		return Lines;
	}

	const TArray<FGF_TamerCreatureSetup>& Source =
		CDO->RematchTeamSetup.Num() > 0 ? CDO->RematchTeamSetup : CDO->TeamSetup;

	for (const FGF_TamerCreatureSetup& Setup : Source)
	{
		// A null species is an authoring slip, not a crash - show the slot so it is
		// obvious in the menu which entry needs filling in rather than silently
		// listing a five-Creature team as four.
		// Loads the slot's species. Bounded to one tamer's team and only when the
		// rematch menu opens, so it is not the load-everything pattern -- but it is
		// still a load, so do not call this to build a list of ALL tamers.
		const UGF_CreatureSpeciesData* Species = ResolveSetupSpecies(Setup);
		const FString SpeciesName = Species
			? Species->SpeciesName.ToString()
			: TEXT("<EMPTY SLOT>");

		Lines.Add(FText::FromString(FString::Printf(TEXT("Lv.%d %s"), Setup.Level, *SpeciesName)));
	}

	return Lines;
}

FName AGF_TamerMaster::GetTamerNameForClass(TSubclassOf<AGF_TamerMaster> TamerClass)
{
	if (!TamerClass)
	{
		return NAME_None;
	}

	const AGF_TamerMaster* CDO = TamerClass->GetDefaultObject<AGF_TamerMaster>();
	return CDO ? CDO->TamerName : NAME_None;
}

// Sets default values
AGF_TamerMaster::AGF_TamerMaster()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AGF_TamerMaster::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void AGF_TamerMaster::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AGF_TamerMaster::InitializeTeam()
{
	// Clear data if it already existed so no leftover issues
	Team.Empty();
	TeamInstanceData.Empty();
	CurrentCreatureIndex = 0;

	// ---- Which team is this? -----------------------------------------------------
	// BP_GF_TamerMaster calls this from the FIRST node of its Event BeginPlay, so this
	// is the only moment the choice can be made - by the time anything outside the actor
	// could reach it, the team is already built.
	const bool bRematchRequested = IsRematchRequested();
	const bool bUseRematchTeam   = bRematchRequested && RematchTeamSetup.Num() > 0;

	if (bRematchRequested && !bUseRematchTeam)
	{
		// Deliberately not fatal. Fielding the authored team is a wrong-looking fight;
		// fielding nothing is a battle the player can neither win nor leave.
		UE_LOG(LogTemp, Warning,
			TEXT("Tamer %s was asked for a REMATCH team but has none authored - falling back to "
			     "its normal team. Fill in RematchTeamSetup or take it off the rematch roster."),
			*TamerName.ToString());
	}

	const TArray<FGF_TamerCreatureSetup>& ActiveSetup = bUseRematchTeam ? RematchTeamSetup : TeamSetup;

	if (bUseRematchTeam)
	{
		// The rematch loadout replaces the authored one OUTRIGHT - ace slot, AI tier and
		// item stock included. Nothing carries over, so a rematch tamer with no
		// Rematch*Amount set throws no items however many the main team had.
		AceIndex          = RematchAceIndex;
		AIDifficulty      = RematchAIDifficulty;
		PotionAmount      = RematchPotionAmount;
		MajorSalveAmount = RematchMajorSalveAmount;
		FullStoreAmount   = RematchFullStoreAmount;

		UE_LOG(LogTemp, Log,
			TEXT("Tamer %s: REMATCH team - %d Creature, ace slot %d, AI tier %d, items P%d/S%d/F%d"),
			*TamerName.ToString(), ActiveSetup.Num(), AceIndex,
			static_cast<int32>(AIDifficulty), PotionAmount, MajorSalveAmount, FullStoreAmount);
	}

	if (ActiveSetup.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("This Tamer does NOT have a team!!"))
		return;
	}

     Team.SetNum(ActiveSetup.Num());

	//"Setup" = FTamerSetup struct which contains the species data and other info for the team
	for (const FGF_TamerCreatureSetup& Setup : ActiveSetup)
	{
		// One asset per slot, resolved here at battle start rather than held resident
		// by every tamer standing in a loaded sublevel.
		UGF_CreatureSpeciesData* SetupSpecies = ResolveSetupSpecies(Setup);
		if (!SetupSpecies)
		{
			UE_LOG(LogTemp, Warning, TEXT("Tamer %s has a null species in team! ADD SPECIES TO SLOT!"), *TamerName.ToString())
		}


		//Create instance data from the setup section
		FGF_CreatureInstanceData InstanceData;
		InstanceData.Initialize(SetupSpecies, Setup.Level, Setup.ForcedSkills, TamerName, TamerID);

		// Apply preset overrides — ensures consistent stats/gender across battles
		if (Setup.bPresetGender)
			InstanceData.Gender = Setup.Gender;

		if (Setup.bPresetTemperament)
			InstanceData.Temperament = Setup.Temperament;

		// Only ever forces unique ON — a false here leaves the 1/4096 roll alone.
		if (Setup.bForceUnique)
			InstanceData.bIsUnique = true;

		if (Setup.bPresetPotentials)
		{
			InstanceData.HP_Potential             = Setup.HP_Potential;
			InstanceData.Attack_Potential         = Setup.Attack_Potential;
			InstanceData.Defense_Potential        = Setup.Defense_Potential;
			InstanceData.Magic_Potential  = Setup.Magic_Potential;
			InstanceData.Poise_Potential = Setup.Poise_Potential;
			InstanceData.Speed_Potential          = Setup.Speed_Potential;
		}

		//Apply the HeldItem if there was one, otherwise do nothing
		if (!Setup.HeldItem.IsNone())
		{
			InstanceData.GiveHeldItem(Setup.HeldItem);
		}

		TeamInstanceData.Add(InstanceData);


	}

	// The ace never leads. If it's sitting in slot 0, start on the first non-ace Creature instead.
	if (IsAceLocked(CurrentCreatureIndex))
	{
		for (int32 i = 0; i < TeamInstanceData.Num(); ++i)
		{
			if (i != AceIndex && TeamInstanceData[i].IsValid())
			{
				CurrentCreatureIndex = i;
				break;
			}
		}

		// Forensic: this line plus the "sent out index" line below are what identify an
		// ace/index desync. They disagreed for five battles against one rematch tamer before anyone noticed.
		GF_DIAG("Tamer %s had its ace in the lead slot - leading with index %d instead.",
			*TamerName.ToString(), CurrentCreatureIndex);
	}
}

//Spawn the actual creature

AGF_Creature* AGF_TamerMaster::SpawnCreatureAtIndex(int32 Index, FVector SpawnLocation, FRotator SpawnRotation)
{
    UE_LOG(LogTemp, Error, TEXT("=== SpawnCreatureAtIndex ==="));
    UE_LOG(LogTemp, Error, TEXT("Index: %d"), Index);
    GF_DIAG("Tamer sent out team index %d (CurrentCreatureIndex is %d).", Index, CurrentCreatureIndex);
    UE_LOG(LogTemp, Error, TEXT("Team.Num(): %d"), Team.Num());
    UE_LOG(LogTemp, Error, TEXT("Team.IsValidIndex(%d): %s"), Index, Team.IsValidIndex(Index) ? TEXT("YES") : TEXT("NO"));

    // Check 1: Is TeamInstanceData valid?
    if (!TeamInstanceData.IsValidIndex(Index))
    {
        UE_LOG(LogTemp, Error, TEXT("FAIL: TeamInstanceData invalid at index %d"), Index);
        return nullptr;
    }
    UE_LOG(LogTemp, Error, TEXT("PASS: TeamInstanceData valid"));

    // Check 2: Is the data valid?
    if (!TeamInstanceData[Index].IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FAIL: TeamInstanceData[%d].IsValid() = false"), Index);
        return nullptr;
    }
    UE_LOG(LogTemp, Error, TEXT("PASS: TeamInstanceData[%d] is valid"), Index);

    // Check 3: Does a Creature already exist here?
    if (Team.IsValidIndex(Index) && IsValid(Team[Index]))  // <- Use IsValid() not just != nullptr
		{
		    UE_LOG(LogTemp, Error, TEXT("EARLY RETURN: Team[%d] already has a Creature: %s"), Index, *Team[Index]->GetName());
		    return Team[Index];
		}
    UE_LOG(LogTemp, Error, TEXT("PASS: Team[%d] is empty, will spawn new"), Index);

    // Check 4: Is CreatureClassToSpawn set?
    if (!CreatureClassToSpawn)
    {
        UE_LOG(LogTemp, Error, TEXT("FAIL: CreatureClassToSpawn is NULL!"));
        return nullptr;
    }
    UE_LOG(LogTemp, Error, TEXT("PASS: CreatureClassToSpawn = %s"), *CreatureClassToSpawn->GetName());

    // Spawn
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    UE_LOG(LogTemp, Error, TEXT("Spawning at Location: %s"), *SpawnLocation.ToString());

    AGF_Creature* SpawnedCreature = GetWorld()->SpawnActor<AGF_Creature>(
        CreatureClassToSpawn,
        SpawnLocation,
        SpawnRotation,
        SpawnParams
    );

    if (SpawnedCreature)
    {
        SpawnedCreature->InitializeFromInstanceData(TeamInstanceData[Index]);
        SpawnedCreature->isWildCreature = false;
        SpawnedCreature->isOnStage = true;
        SpawnedCreature->isPlayerCreature = false;
        Team[Index] = SpawnedCreature;

        // Write the actor's calculated MaxHP (and starting CurrentHP) back into TeamInstanceData
        // so that AI functions (ShouldConsiderSwitching, FindBestSwitchTarget) can compute
        // HP percentages correctly. Without this, MaxHP stays 0 (the Initialize sentinel).
        TeamInstanceData[Index].MaxHP     = SpawnedCreature->CurrentStats.MaxHP;
        TeamInstanceData[Index].CurrentHP = SpawnedCreature->CurrentStats.CurrentHP;

        UE_LOG(LogTemp, Error, TEXT("SUCCESS: Spawned %s"), *SpawnedCreature->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FAIL: SpawnActor returned nullptr!"));
    }

    return SpawnedCreature;
}

AGF_Creature* AGF_TamerMaster::SpawnCurrentCreature(FVector SpawnLocation, FRotator SpawnRotation)
{
	return SpawnCreatureAtIndex(CurrentCreatureIndex, SpawnLocation, SpawnRotation);
}

AGF_Creature* AGF_TamerMaster::GetCurrentCreature() const
{
	if (Team.IsValidIndex(CurrentCreatureIndex))
	{
		return Team[CurrentCreatureIndex];
	}
	return nullptr;
}

bool AGF_TamerMaster::GetCurrentCreatureData(FGF_CreatureInstanceData& OutData) const
{
    if (TeamInstanceData.IsValidIndex(CurrentCreatureIndex))
    {
        OutData = TeamInstanceData[CurrentCreatureIndex];
        return true;
    }
    return false;
}

bool AGF_TamerMaster::IsAceLocked(int32 Index) const
{
    if (AceIndex < 0 || Index != AceIndex || !TeamInstanceData.IsValidIndex(Index))
    {
        return false;
    }

    // The ace unlocks the moment nobody else on the team can fight.
    for (int32 i = 0; i < TeamInstanceData.Num(); ++i)
    {
        if (i == AceIndex)
        {
            continue;
        }

        if (TeamInstanceData[i].IsValid() && !TeamInstanceData[i].bIsDowned)
        {
            return true;
        }
    }

    return false;
}

bool AGF_TamerMaster::GetNextCreatureData(FGF_CreatureInstanceData& OutData) const
{
    // Forward pass: from current+1 to end
    for (int32 i = CurrentCreatureIndex + 1; i < TeamInstanceData.Num(); ++i)
    {
        if (TeamInstanceData[i].IsValid() && !TeamInstanceData[i].bIsDowned && !IsAceLocked(i))
        {
            OutData = TeamInstanceData[i];
            return true;
        }
    }

    // Ensnare-around pass: from 0 to current (mirrors SwitchToNextCreature)
    for (int32 i = 0; i < CurrentCreatureIndex; ++i)
    {
        if (TeamInstanceData[i].IsValid() && !TeamInstanceData[i].bIsDowned && !IsAceLocked(i))
        {
            OutData = TeamInstanceData[i];
            return true;
        }
    }

    return false;
}

bool AGF_TamerMaster::SwitchToNextCreature()
{
    UE_LOG(LogTemp, Warning, TEXT("=== SwitchToNextCreature ==="));
    UE_LOG(LogTemp, Warning, TEXT("Looking for Creature after index %d"), CurrentCreatureIndex);

    for (int32 i = CurrentCreatureIndex + 1; i < TeamInstanceData.Num(); i++)
    {
        UE_LOG(LogTemp, Log, TEXT("  Checking [%d]: Valid=%s, Downed=%s, HP=%.0f"),
            i,
            TeamInstanceData[i].IsValid() ? TEXT("Yes") : TEXT("No"),
            TeamInstanceData[i].bIsDowned ? TEXT("Yes") : TEXT("No"),
            TeamInstanceData[i].CurrentHP);

        // bIsDowned is the authoritative check — CurrentHP in TeamInstanceData may be 0
        // for Creature that haven't been spawned yet (sentinel value), so we don't use it here.
        if (TeamInstanceData[i].IsValid() && !TeamInstanceData[i].bIsDowned && !IsAceLocked(i))
        {
            CurrentCreatureIndex = i;
            UE_LOG(LogTemp, Warning, TEXT("  Found valid Creature at index %d!"), i);
            return true;
        }
    }

    // Ensnare around from beginning
    for (int32 i = 0; i < CurrentCreatureIndex; i++)
    {
        UE_LOG(LogTemp, Log, TEXT("  Checking [%d] (wrap): Valid=%s, Downed=%s, HP=%.0f"),
            i,
            TeamInstanceData[i].IsValid() ? TEXT("Yes") : TEXT("No"),
            TeamInstanceData[i].bIsDowned ? TEXT("Yes") : TEXT("No"),
            TeamInstanceData[i].CurrentHP);

        if (TeamInstanceData[i].IsValid() && !TeamInstanceData[i].bIsDowned && !IsAceLocked(i))
        {
            CurrentCreatureIndex = i;
            UE_LOG(LogTemp, Warning, TEXT("  Found valid Creature at index %d (wrap)!"), i);
            return true;
        }
    }

    UE_LOG(LogTemp, Error, TEXT("  No valid Creature found!"));
    return false;
}

void AGF_TamerMaster::OnBattleTurnPassed_Implementation()
{
    if (TurnsUntilCanSwitch > 0)
        TurnsUntilCanSwitch--;
}

void AGF_TamerMaster::SyncCurrentCreatureStatus()
{
    if (!Team.IsValidIndex(CurrentCreatureIndex) || !IsValid(Team[CurrentCreatureIndex]))
    {
        return;
    }

    if (!TeamInstanceData.IsValidIndex(CurrentCreatureIndex))
    {
        return;
    }

    AGF_Creature* CurrentCreature = Team[CurrentCreatureIndex];

    // Sync HP (both Max and Current so AI HP% calculations stay accurate)
    TeamInstanceData[CurrentCreatureIndex].MaxHP     = CurrentCreature->CurrentStats.MaxHP;
    TeamInstanceData[CurrentCreatureIndex].CurrentHP = CurrentCreature->CurrentStats.CurrentHP;

    // Check if downed
    if (CurrentCreature->CurrentStats.CurrentHP <= 0)
    {
        const bool bAlreadyDowned = TeamInstanceData[CurrentCreatureIndex].bIsDowned;
        TeamInstanceData[CurrentCreatureIndex].bIsDowned = true;
        TeamInstanceData[CurrentCreatureIndex].CurrentHP = 0;
        UE_LOG(LogTemp, Warning, TEXT("Synced: %s is downed"), *CurrentCreature->Name.ToString());

        // Notify the UI (only fire once, not every sync)
        if (!bAlreadyDowned)
        {
            OnTamerCreatureDowned.Broadcast(CurrentCreatureIndex);
        }
    }
}

void AGF_TamerMaster::NotifyCreatureDowned(int32 Index)
{
    if (!TeamInstanceData.IsValidIndex(Index))
    {
        return;
    }

    const bool bAlreadyDowned = TeamInstanceData[Index].bIsDowned;
    TeamInstanceData[Index].bIsDowned = true;
    TeamInstanceData[Index].CurrentHP  = 0;

    if (!bAlreadyDowned)
    {
        UE_LOG(LogTemp, Warning, TEXT("NotifyCreatureDowned: Tamer Creature at index %d downed"), Index);
        OnTamerCreatureDowned.Broadcast(Index);

        if (!HasUsableCreature())
        {
            isDefeated = true;
            OnTamerDefeated.Broadcast();
        }
    }
}

bool AGF_TamerMaster::SendOutNextCreature(FVector SpawnLocation, FRotator SpawnRotation)
{
    // Diagnostic. A legitimate send-out always follows a down, so "downed=YES" is the
    // normal case; "downed=no" means something asked us to recall a living Creature, which
    // is the shape of the bug that deleted a healthy enemy mid-battle. The script callstack
    // names the Blueprint that called us — this has no reproduction steps, so the one line
    // in a tester's log is the only chance to identify the caller. Remove once identified.
    {
        const AGF_Creature* Current = Team.IsValidIndex(CurrentCreatureIndex) ? Team[CurrentCreatureIndex] : nullptr;
        const bool bCurrentValid = IsValid(Current);
        UE_LOG(LogTemp, Warning,
            TEXT("SendOutNextCreature: tamer=%s team=%d index=%d current=%s hp=%.0f downed=%s\nCaller:\n%s"),
            *GetName(),
            TeamInstanceData.Num(),
            CurrentCreatureIndex,
            bCurrentValid ? *Current->Name.ToString() : TEXT("<none>"),
            bCurrentValid ? Current->CurrentStats.CurrentHP : -1.0f,
            (bCurrentValid && Current->CurrentStats.CurrentHP <= 0.0f) ? TEXT("YES") : TEXT("no"),
            *FFrame::GetScriptCallstack());
    }

    LastSpawnedCreatureActor = nullptr;

    // Ensure Team array is properly sized
    if (Team.Num() != TeamInstanceData.Num())
    {
        Team.SetNum(TeamInstanceData.Num());
    }

    // Sync HP/downed status back to instance data before destroying the actor
    SyncCurrentCreatureStatus();

    // Confirm a replacement exists BEFORE recalling the current Creature.
    // Recalling first and only then discovering there is no next Creature leaves the
    // battle with no enemy actor at all: nothing to target, nothing to take a turn.
    // GetNextCreatureData mirrors SwitchToNextCreature's search exactly (same validity,
    // down and ace-lock checks, same forward-then-wrap order) without advancing the
    // index, so if it succeeds the SwitchToNextCreature call below cannot fail.
    FGF_CreatureInstanceData NextData;
    if (!GetNextCreatureData(NextData))
    {
        // Only a genuine "out of Creature" if the one on the field is actually down.
        // This can also be reached by a spurious call on a LIVING Creature (the dialogue
        // event EnemySendsOutNextCreature fires on a voluntary player switch, where nothing
        // downed) — that must neither delete it nor end the battle.
        const bool bCurrentIsDown = TeamInstanceData.IsValidIndex(CurrentCreatureIndex)
                                 && TeamInstanceData[CurrentCreatureIndex].bIsDowned;
        if (bCurrentIsDown)
        {
            isDefeated = true;
        }
        return false;
    }

    // Destroy current Creature actor
    if (Team.IsValidIndex(CurrentCreatureIndex) && Team[CurrentCreatureIndex])
    {
        Team[CurrentCreatureIndex]->Destroy();
        Team[CurrentCreatureIndex] = nullptr;
    }

    // Find next Creature
    if (!SwitchToNextCreature())
    {
        isDefeated = true;
        return false;
    }

    // Spawn and store as AActor*
    LastSpawnedCreatureActor = SpawnCurrentCreature(SpawnLocation, SpawnRotation);

    return LastSpawnedCreatureActor != nullptr;
}

bool AGF_TamerMaster::HasUsableCreature() const
{
    for (const FGF_CreatureInstanceData& Data : TeamInstanceData)
    {
        // bIsDowned is authoritative — don't use CurrentHP which may be 0 (sentinel) for unspawned Creature
        if (Data.IsValid() && !Data.bIsDowned)
        {
            return true;
        }
    }
    return false;
}

int32 AGF_TamerMaster::GetRemainingCreatureCount() const
{
    int32 Count = 0;
    for (const FGF_CreatureInstanceData& Data : TeamInstanceData)
    {
        if (Data.IsValid() && !Data.bIsDowned)
        {
            Count++;
        }
    }
    return Count;
}

int32 AGF_TamerMaster::GetRewardMoney() const
{
    // classic formula: BasePayout * HighestCreatureLevel * 4
    // Uses the HIGHEST level Creature on the tamer's team, not the last one.
    // Source: the classic payout rule

    int32 HighestLevel = 1;

    if (TeamInstanceData.Num() > 0)
    {
        for (const FGF_CreatureInstanceData& Data : TeamInstanceData)
        {
            if (Data.Level > HighestLevel)
            {
                HighestLevel = Data.Level;
            }
        }
    }
    else if (TeamSetup.Num() > 0)
    {
        for (const FGF_TamerCreatureSetup& Setup : TeamSetup)
        {
            if (Setup.Level > HighestLevel)
            {
                HighestLevel = Setup.Level;
            }
        }
    }

    return BasePayout * HighestLevel * 4;
}

AGF_Creature* AGF_TamerMaster::SwitchToSpecificCreature(int32 NewIndex, FVector SpawnLocation, FRotator SpawnRotation)
{
    LastSpawnedCreatureActor = nullptr;

    // Ensure Team array is properly sized
    if (Team.Num() != TeamInstanceData.Num())
    {
        Team.SetNum(TeamInstanceData.Num());
    }

    UE_LOG(LogTemp, Warning, TEXT("=== SwitchToSpecificCreature ==="));
    UE_LOG(LogTemp, Warning, TEXT("Switching from index %d to index %d"), CurrentCreatureIndex, NewIndex);

    if (!TeamInstanceData.IsValidIndex(NewIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid index %d!"), NewIndex);
        return nullptr;
    }

    if (TeamInstanceData[NewIndex].bIsDowned)
    {
        UE_LOG(LogTemp, Error, TEXT("Creature at index %d is downed!"), NewIndex);
        return nullptr;
    }

    // The ace stays in its ball until it's the only one left — refuse even an explicit request.
    if (IsAceLocked(NewIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("Index %d is this tamer's ace and is still held in reserve - refusing switch."), NewIndex);
        return nullptr;
    }

    // Find whichever Creature is currently on stage and recall it.
    // We scan by isOnStage rather than trusting CurrentCreatureIndex, because Blueprint
    // may have already written NewIndex into CurrentCreatureIndex before calling us.
    int32 OldIndex = -1;
    for (int32 i = 0; i < Team.Num(); i++)
    {
        if (i != NewIndex && Team.IsValidIndex(i) && IsValid(Team[i]) && Team[i]->isOnStage)
        {
            OldIndex = i;
            break;
        }
    }

    if (OldIndex != -1)
    {
        // Sync HP directly from the live actor into its instance slot
        if (TeamInstanceData.IsValidIndex(OldIndex))
        {
            TeamInstanceData[OldIndex].MaxHP     = Team[OldIndex]->CurrentStats.MaxHP;
            TeamInstanceData[OldIndex].CurrentHP = Team[OldIndex]->CurrentStats.CurrentHP;
            if (Team[OldIndex]->CurrentStats.CurrentHP <= 0.f)
            {
                TeamInstanceData[OldIndex].bIsDowned = true;
                TeamInstanceData[OldIndex].CurrentHP  = 0;
            }
            UE_LOG(LogTemp, Warning, TEXT("Recalled %s from index %d - saved HP %.0f/%.0f"),
                *Team[OldIndex]->Name.ToString(), OldIndex,
                TeamInstanceData[OldIndex].CurrentHP, TeamInstanceData[OldIndex].MaxHP);
        }
        Team[OldIndex]->Destroy();
        Team[OldIndex] = nullptr;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SwitchToSpecificCreature: no on-stage Creature found to recall (NewIndex=%d)"), NewIndex);
    }

    // Switch to the new index
    CurrentCreatureIndex = NewIndex;

    // Spawn the new Creature
    AGF_Creature* NewCreature = SpawnCurrentCreature(SpawnLocation, SpawnRotation);

    if (NewCreature)
    {
        UE_LOG(LogTemp, Warning, TEXT("Successfully sent out %s!"), *NewCreature->Name.ToString());
    }

    return NewCreature;
}


FGF_TamerAIDecision AGF_TamerMaster::DecideAction(const FGF_CreatureInstanceData& PlayerCreature)
{
    FGF_TamerAIDecision Decision;

    if (!TeamInstanceData.IsValidIndex(CurrentCreatureIndex))
    {
        Decision.ReasonForDecision = "No valid Creature!";
        return Decision;
    }

    const FGF_CreatureInstanceData& CurrentCreature = TeamInstanceData[CurrentCreatureIndex];

    // Resolved once per decision so every branch below agrees on the tier.
    const EGF_TamerAIDifficulty EffectiveDifficulty = GetEffectiveAIDifficulty();

    // Consider throwing a healing item before anything else.
    // Skipped for Random AI to keep it deliberately dumb.
    if (EffectiveDifficulty != EGF_TamerAIDifficulty::Random)
    {
        int32 HealAmount = 0;
        EGF_TamerHealItem Item = ChooseHealingItem(CurrentCreature, HealAmount);
        if (Item != EGF_TamerHealItem::None)
        {
            Decision.Decision          = EGF_TamerDecision::UseItem;
            Decision.ItemToUse         = Item;
            Decision.ItemHealAmount    = HealAmount;
            Decision.ReasonForDecision = TEXT("Using healing item - HP low");
            return Decision;
        }
    }

    // Random AI - just pick any move
    if (EffectiveDifficulty == EGF_TamerAIDifficulty::Random)
    {
        if (CurrentCreature.Skills.Num() > 0)
        {
            Decision.Decision = EGF_TamerDecision::Attack;
            Decision.SkillIndex = FMath::RandRange(0, CurrentCreature.Skills.Num() - 1);

            if (!CurrentCreature.Skills[Decision.SkillIndex].IsNull())
            {
                Decision.SelectedSkill = CurrentCreature.Skills[Decision.SkillIndex].LoadSynchronous();
            }
            Decision.ReasonForDecision = "Random selection";

            GF_DIAG("TamerAI: %s (team index %d) randomly chose slot %d = '%s'.",
                *CurrentCreature.GetDisplayName().ToString(), CurrentCreatureIndex,
                Decision.SkillIndex,
                Decision.SelectedSkill ? *Decision.SelectedSkill->GetName() : TEXT("<null>"));
        }
        return Decision;
    }

    // Expert AI - consider switching first, but only if cooldown has expired
    if (EffectiveDifficulty == EGF_TamerAIDifficulty::Expert && bCanSwitch && TurnsUntilCanSwitch <= 0)
    {
        if (ShouldConsiderSwitching(CurrentCreature, PlayerCreature))
        {
            int32 BestSwitch = FindBestSwitchTarget(PlayerCreature);
            if (BestSwitch != -1 && BestSwitch != CurrentCreatureIndex)
            {
                Decision.Decision = EGF_TamerDecision::SwitchCreature;
                Decision.SwitchToIndex = BestSwitch;
                Decision.ReasonForDecision = FString::Printf(TEXT("Switching to better matchup (index %d)"), BestSwitch);
                TurnsUntilCanSwitch = SwitchCooldownTurns;  // Reset cooldown
                return Decision;
            }
        }
    }

    // Evaluate moves and pick the best one
    FGF_SkillEvaluation BestSkill = GetBestSkill(CurrentCreature, PlayerCreature);

    if (BestSkill.SkillClass)
    {
        Decision.Decision = EGF_TamerDecision::Attack;
        Decision.SkillIndex = BestSkill.SkillIndex;
        Decision.SelectedSkill = BestSkill.SkillClass;
        Decision.ReasonForDecision = FString::Printf(TEXT("Best move (Score: %.1f, Effectiveness: %.1fx)"),
            BestSkill.Score, BestSkill.TypeEffectiveness);

        // What the AI actually picked, for the Creature it actually picked it for. Compare this
        // against the move the battle ends up playing -- if they differ, the decision was
        // discarded downstream rather than computed wrong here.
        GF_DIAG("TamerAI: %s (team index %d) chose slot %d = '%s'.",
            *CurrentCreature.GetDisplayName().ToString(), CurrentCreatureIndex,
            BestSkill.SkillIndex,
            BestSkill.SkillClass ? *BestSkill.SkillClass->GetName() : TEXT("<null>"));
    }
    else
    {
        // No valid moves - lastResort or switch
        Decision.ReasonForDecision = "No valid moves available!";
    }

    return Decision;
}

FGF_SkillEvaluation AGF_TamerMaster::GetBestSkill(const FGF_CreatureInstanceData& AttackerData, const FGF_CreatureInstanceData& DefenderData)
{
    TArray<FGF_SkillEvaluation> AllSkills = EvaluateAllSkills(AttackerData, DefenderData);

    if (AllSkills.Num() == 0)
    {
        return FGF_SkillEvaluation();
    }

    const EGF_TamerAIDifficulty EffectiveDifficulty = GetEffectiveAIDifficulty();

    // For Basic AI, just return highest power move
    if (EffectiveDifficulty == EGF_TamerAIDifficulty::Basic)
    {
        // Sort by raw power only
        AllSkills.Sort([](const FGF_SkillEvaluation& A, const FGF_SkillEvaluation& B) {
            return A.Score > B.Score;
        });
        return AllSkills[0];
    }

    // For Smart/Expert, consider status moves occasionally
    if ((EffectiveDifficulty == EGF_TamerAIDifficulty::Smart || EffectiveDifficulty == EGF_TamerAIDifficulty::Expert)
        && DefenderData.StatusCondition == EGF_STATUSEffect::None)
    {
        // Check if we have a status move and should use it
        for (const FGF_SkillEvaluation& Eval : AllSkills)
        {
            if (Eval.SkillClass)
            {
                AGF_SkillDefinition* TempSkill = Eval.SkillClass.GetDefaultObject();
                if (TempSkill && TempSkill->Split == EGF_SkillCategory::Status && TempSkill->Status != EGF_STATUSEffect::None)
                {
                    if (FMath::FRand() < StatusSkillChance)
                    {
                        return Eval;
                    }
                }
            }
        }
    }

    // Return highest scored move
    AllSkills.Sort([](const FGF_SkillEvaluation& A, const FGF_SkillEvaluation& B) {
        return A.Score > B.Score;
    });

    return AllSkills[0];
}

TArray<FGF_SkillEvaluation> AGF_TamerMaster::EvaluateAllSkills(const FGF_CreatureInstanceData& AttackerData, const FGF_CreatureInstanceData& DefenderData)
{
    TArray<FGF_SkillEvaluation> Evaluations;

    for (int32 i = 0; i < AttackerData.Skills.Num(); i++)
    {
        if (AttackerData.Skills[i].IsNull())
        {
            continue;
        }

        // Check Uses
        bool bHasUses = true;
        if (AttackerData.CurrentUses.IsValidIndex(i))
        {
            bHasUses = AttackerData.CurrentUses[i] > 0;
        }

        if (!bHasUses)
        {
            continue;
        }

        TSubclassOf<AGF_SkillDefinition> SkillClass = AttackerData.Skills[i].LoadSynchronous();
        if (!SkillClass)
        {
            continue;
        }

        FGF_SkillEvaluation Eval;
        Eval.SkillIndex = i;
        Eval.SkillClass = SkillClass;
        Eval.bHasUses = bHasUses;
        Eval.Score = CalculateSkillScore(AttackerData, DefenderData, SkillClass, i);

        // Get type effectiveness for display
        AGF_SkillDefinition* SkillDefaults = SkillClass.GetDefaultObject();
        if (SkillDefaults)
        {
            UGF_CreatureSpeciesData* DefenderSpecies = DefenderData.SpeciesData.LoadSynchronous();
            if (DefenderSpecies)
            {
                Eval.TypeEffectiveness = GetTypeEffectiveness(
                    SkillDefaults->Type,
                    DefenderSpecies->PrimaryElement,
                    DefenderSpecies->SecondaryElement
                );
            }

            // Check STAB
            UGF_CreatureSpeciesData* AttackerSpecies = AttackerData.SpeciesData.LoadSynchronous();
            if (AttackerSpecies)
            {
                EGF_Element MoveTypeAsCreature = static_cast<EGF_Element>(static_cast<uint8>(SkillDefaults->Type));
                Eval.bHasSTAB = (MoveTypeAsCreature == AttackerSpecies->PrimaryElement ||
                                 MoveTypeAsCreature == AttackerSpecies->SecondaryElement);
            }
        }

        Evaluations.Add(Eval);
    }

    return Evaluations;
}

float AGF_TamerMaster::CalculateSkillScore(const FGF_CreatureInstanceData& Attacker, const FGF_CreatureInstanceData& Defender,
                                          TSubclassOf<AGF_SkillDefinition> SkillClass, int32 SkillIndex)
{
    if (!SkillClass)
    {
        return 0.0f;
    }

    AGF_SkillDefinition* SkillDefaults = SkillClass.GetDefaultObject();
    if (!SkillDefaults)
    {
        return 0.0f;
    }

    float Score = 0.0f;

    // Status moves get a flat score
    if (SkillDefaults->Split == EGF_SkillCategory::Status)
    {
        // Higher score if opponent doesn't have a status
        if (Defender.StatusCondition == EGF_STATUSEffect::None && SkillDefaults->Status != EGF_STATUSEffect::None)
        {
            Score = 50.0f * (SkillDefaults->StatusChance / 100.0f);

            // Bonus for sleep/paralysis which are very useful
            if (SkillDefaults->Status == EGF_STATUSEffect::Sleeping || SkillDefaults->Status == EGF_STATUSEffect::Paralyzed)
            {
                Score *= 1.5f;
            }
        }
        return Score;
    }

    // Start with base power
    Score = SkillDefaults->Power;

    // Apply type effectiveness
    UGF_CreatureSpeciesData* DefenderSpecies = Defender.SpeciesData.LoadSynchronous();
    if (DefenderSpecies)
    {
        float Effectiveness = GetTypeEffectiveness(
            SkillDefaults->Type,
            DefenderSpecies->PrimaryElement,
            DefenderSpecies->SecondaryElement
        );
        Score *= Effectiveness;

        // Heavily penalize immune moves
        if (Effectiveness == 0.0f)
        {
            return 0.0f;
        }
    }

    // Apply STAB bonus
    UGF_CreatureSpeciesData* AttackerSpecies = Attacker.SpeciesData.LoadSynchronous();
    if (AttackerSpecies)
    {
        EGF_Element MoveTypeAsCreature = static_cast<EGF_Element>(static_cast<uint8>(SkillDefaults->Type));
        if (MoveTypeAsCreature == AttackerSpecies->PrimaryElement ||
            MoveTypeAsCreature == AttackerSpecies->SecondaryElement)
        {
            Score *= 1.5f;
        }
    }

    // Consider accuracy
    Score *= (SkillDefaults->Accuracy / 100.0f);

    // Bonus for high crit moves
    if (SkillDefaults->isHighCritRatio)
    {
        Score *= 1.1f;
    }

    // Bonus for moves that can inflict status
    if (SkillDefaults->Status != EGF_STATUSEffect::None && Defender.StatusCondition == EGF_STATUSEffect::None)
    {
        Score += 10.0f * (SkillDefaults->StatusChance / 100.0f);
    }

    return Score;
}

bool AGF_TamerMaster::ShouldConsiderSwitching(const FGF_CreatureInstanceData& CurrentCreature, const FGF_CreatureInstanceData& PlayerCreature)
{
    // Never switch if we have a significant level advantage — just attack
    int32 OurLevelAdvantage = CurrentCreature.Level - PlayerCreature.Level;
    if (OurLevelAdvantage >= LevelDisadvantageThreshold)
        return false;




    // REASON 1: Our HP is too low
    // Guard against MaxHP == 0 (sentinel for not-yet-spawned, or before first sync)
    float OurHPPercent = (CurrentCreature.MaxHP > 0.f)
        ? CurrentCreature.CurrentHP / CurrentCreature.MaxHP
        : 1.0f;
    if (OurHPPercent < SwitchHPThreshold)
    {
        UE_LOG(LogTemp, Log, TEXT("AI considering switch: Our HP is low (%.0f%%)"), OurHPPercent * 100.f);
        return true;
    }

    // REASON 2: Player is significantly higher level
    int32 LevelDifference = PlayerCreature.Level - CurrentCreature.Level;
    if (LevelDifference >= LevelDisadvantageThreshold)
    {
        UE_LOG(LogTemp, Log, TEXT("AI considering switch: Player is %d levels higher"), LevelDifference);
        return true;
    }

    // REASON 3: Player still has high HP and we are badly damaged — we're not doing enough
    // Raised threshold to 50% so this only triggers when we're nearly half HP, not 70%
    float PlayerHPPercent = (PlayerCreature.MaxHP > 0.f)
        ? PlayerCreature.CurrentHP / PlayerCreature.MaxHP
        : 1.0f;
    if (PlayerHPPercent >= PlayerHPThresholdToSwitch && OurHPPercent < 0.5f)
    {
        UE_LOG(LogTemp, Log, TEXT("AI considering switch: Player at %.0f%% HP, we're at %.0f%% - not effective"),
            PlayerHPPercent * 100.f, OurHPPercent * 100.f);
        return true;
    }

    // REASON 4+: Type-based checks (need species data)
    UGF_CreatureSpeciesData* CurrentSpecies = CurrentCreature.SpeciesData.LoadSynchronous();
    UGF_CreatureSpeciesData* PlayerSpecies = PlayerCreature.SpeciesData.LoadSynchronous();

    if (!CurrentSpecies || !PlayerSpecies)
        return false;

    // REASON 4: Only switch for 4x (double) weakness — a normal 2x weakness isn't worth switching for
    // (this was the biggest culprit — switching on ANY weakness is way too aggressive)

    // REASON 5: 4x weakness
    for (EGF_Element SuperWeakness : CurrentSpecies->SuperWeaknesses)
    {
        if (SuperWeakness == PlayerSpecies->PrimaryElement || SuperWeakness == PlayerSpecies->SecondaryElement)
        {
            UE_LOG(LogTemp, Log, TEXT("AI considering switch: 4x weak to player's type!"));
            return true;
        }
    }

    // REASON 6: Player has super effective moves against us
    for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : PlayerCreature.Skills)
    {
        if (Skill.IsNull()) continue;

        TSubclassOf<AGF_SkillDefinition> SkillClass = Skill.LoadSynchronous();
        if (!SkillClass) continue;

        AGF_SkillDefinition* SkillDefaults = SkillClass.GetDefaultObject();
        if (SkillDefaults)
        {
            float Effectiveness = GetTypeEffectiveness(
                SkillDefaults->Type,
                CurrentSpecies->PrimaryElement,
                CurrentSpecies->SecondaryElement
            );

            if (Effectiveness >= 4.0f)  // Only switch for 4x, not a regular 2x
            {
                UE_LOG(LogTemp, Log, TEXT("AI considering switch: Player has 4x super effective move"));
                return true;
            }
        }
    }  // <- This closes the player moves loop

    // REASON 7: ALL of our damaging moves are resisted or immune (0x or 0.5x)
    // A neutral (1x) or better move means we should stay in and fight
    bool bHasAtLeastNeutralSkill = false;
    for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : CurrentCreature.Skills)
    {
        if (Skill.IsNull()) continue;

        TSubclassOf<AGF_SkillDefinition> SkillClass = Skill.LoadSynchronous();
        if (!SkillClass) continue;

        AGF_SkillDefinition* SkillDefaults = SkillClass.GetDefaultObject();
        if (SkillDefaults && SkillDefaults->Power > 0)
        {
            float Effectiveness = GetTypeEffectiveness(
                SkillDefaults->Type,
                PlayerSpecies->PrimaryElement,
                PlayerSpecies->SecondaryElement
            );

            if (Effectiveness >= 1.0f)  // Neutral or super effective - worth staying in
            {
                bHasAtLeastNeutralSkill = true;
                break;
            }
        }
    }

    if (!bHasAtLeastNeutralSkill)
    {
        UE_LOG(LogTemp, Log, TEXT("AI considering switch: All moves resisted or immune - no point staying in"));
        return true;
    }

    return false;
}

int32 AGF_TamerMaster::FindBestSwitchTarget(const FGF_CreatureInstanceData& PlayerCreature)
{
    int32 BestIndex = -1;
    float BestScore = -1000.0f;

    UGF_CreatureSpeciesData* PlayerSpecies = PlayerCreature.SpeciesData.LoadSynchronous();
    if (!PlayerSpecies)
    {
        return -1;
    }

    for (int32 i = 0; i < TeamInstanceData.Num(); i++)
    {
        // Skip current Creature, downed Creature, and an ace that's still being held back
        if (i == CurrentCreatureIndex || !TeamInstanceData[i].IsValid() || TeamInstanceData[i].bIsDowned || IsAceLocked(i))
        {
            continue;
        }

        UGF_CreatureSpeciesData* CandidateSpecies = TeamInstanceData[i].SpeciesData.LoadSynchronous();
        if (!CandidateSpecies)
        {
            continue;
        }

        float Score = 0.0f;

        //Level consideration
        int32 CandidateLevel = TeamInstanceData[i].Level;
        int32 CurrentLevel = TeamInstanceData[CurrentCreatureIndex].Level;

        // Bonus for switching to a higher level Creature
        if (CandidateLevel > CurrentLevel)
        {
            Score += (CandidateLevel - CurrentLevel) * 3.0f;  // +3 points per level higher
        }

        // Bonus if candidate is closer to player's level
        int32 CurrentLevelGap = FMath::Abs(PlayerCreature.Level - CurrentLevel);
        int32 CandidateLevelGap = FMath::Abs(PlayerCreature.Level - CandidateLevel);
        if (CandidateLevelGap < CurrentLevelGap)
        {
            Score += (CurrentLevelGap - CandidateLevelGap) * 2.0f;  // +2 points per level closer
        }

        // HP consideration — guard against MaxHP == 0 (unspawned Creature have sentinel value)
        // Treat MaxHP == 0 as full HP (Creature hasn't been in battle yet, so it's at full health)
        float CandidateHPPercent = (TeamInstanceData[i].MaxHP > 0.f)
            ? TeamInstanceData[i].CurrentHP / TeamInstanceData[i].MaxHP
            : 1.0f;
        float CurrentHPPercent = (TeamInstanceData[CurrentCreatureIndex].MaxHP > 0.f)
            ? TeamInstanceData[CurrentCreatureIndex].CurrentHP / TeamInstanceData[CurrentCreatureIndex].MaxHP
            : 1.0f;

        // Bonus for switching to a Creature with more HP
        if (CandidateHPPercent > CurrentHPPercent)
        {
            Score += 20.0f;  // Significant bonus for healthier Creature
        }


        // Bonus for resisting player's types
        if (CandidateSpecies->Resistances.Contains(PlayerSpecies->PrimaryElement))
        {
            Score += 50.0f;
        }
        if (CandidateSpecies->Resistances.Contains(PlayerSpecies->SecondaryElement))
        {
            Score += 30.0f;
        }

        // Big bonus for immunity
        if (CandidateSpecies->Immunities.Contains(PlayerSpecies->PrimaryElement))
        {
            Score += 100.0f;
        }
        if (CandidateSpecies->Immunities.Contains(PlayerSpecies->SecondaryElement))
        {
            Score += 75.0f;
        }

        // Penalty for being weak to player
        if (CandidateSpecies->Weaknesses.Contains(PlayerSpecies->PrimaryElement))
        {
            Score -= 40.0f;
        }
        if (CandidateSpecies->SuperWeaknesses.Contains(PlayerSpecies->PrimaryElement))
        {
            Score -= 80.0f;
        }

        // Bonus for having super effective moves
        for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : TeamInstanceData[i].Skills)
        {
            if (Skill.IsNull()) continue;

            TSubclassOf<AGF_SkillDefinition> SkillClass = Skill.LoadSynchronous();
            if (!SkillClass) continue;

            AGF_SkillDefinition* SkillDefaults = SkillClass.GetDefaultObject();
            if (SkillDefaults)
            {
                float Effectiveness = GetTypeEffectiveness(
                    SkillDefaults->Type,
                    PlayerSpecies->PrimaryElement,
                    PlayerSpecies->SecondaryElement
                );
                if (Effectiveness >= 4.0f)
                {
                    Score += 60.0f;
                }
                else if (Effectiveness >= 2.0f)
                {
                    Score += 30.0f;
                }
            }
        }

        // Scale by HP percentage (prefer healthy Creature)
        Score *= CandidateHPPercent;

        UE_LOG(LogTemp, Log, TEXT("Switch candidate [%d] %s Lv.%d - Score: %.1f"),
            i, *CandidateSpecies->SpeciesName.ToString(), CandidateLevel, Score);

        if (Score > BestScore)
        {
            BestScore = Score;
            BestIndex = i;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Best switch: Index %d, Score %.1f"), BestIndex, BestScore);

    // Lower threshold or remove it entirely
    return (BestScore > 10.0f) ? BestIndex : -1;  // Lowered from 20 to 10
}

int32 AGF_TamerMaster::GetFutureCreatureCount() const
{
    int32 Count = 0;
    for (int32 i = 0; i < TeamInstanceData.Num(); i++)
    {
        if (i == CurrentCreatureIndex)
        {
            continue;  // don't count the Creature currently on the field
        }
        if (TeamInstanceData[i].IsValid() && !TeamInstanceData[i].bIsDowned)
        {
            Count++;
        }
    }
    return Count;
}

EGF_TamerHealItem AGF_TamerMaster::ChooseHealingItem(const FGF_CreatureInstanceData& Current, int32& OutHealAmount) const
{
    OutHealAmount = 0;

    // Need a valid MaxHP to compute a percentage
    if (Current.MaxHP <= 0.f)
    {
        return EGF_TamerHealItem::None;
    }

    const float HPPercent = Current.CurrentHP / Current.MaxHP;
    if (HPPercent >= HealHPThreshold)
    {
        return EGF_TamerHealItem::None;  // Healthy enough - don't waste an item
    }

    // --- Item conservation: don't blow every item on an early/weak Creature ---
    // The current Creature's ace (the last one) hasn't earned a heal at the cost of the team.
    // Reserve a fraction of the remaining stock until no Creature are left behind this one.
    const int32 FutureMons = GetFutureCreatureCount();
    if (FutureMons > 0)
    {
        const int32 ItemsLeft = PotionAmount + MajorSalveAmount + FullStoreAmount;
        const int32 Reserved  = FMath::CeilToInt(ItemsLeft * ItemConservation);
        if (ItemsLeft - Reserved <= 0)
        {
            // Everything left is earmarked for later Creature — hold the line and let this one fight on.
            return EGF_TamerHealItem::None;
        }
    }

    const int32 Missing = FMath::RoundToInt(Current.MaxHP - Current.CurrentHP);

    // Heal values are hardcoded to match the standard item assets (Potion 20, Major Salve 50).
    // Prefer the smallest item that mostly covers the missing HP, then fall back to bigger ones.
    if (PotionAmount > 0 && Missing <= 20)        { OutHealAmount = 20;                            return EGF_TamerHealItem::MinorHeal; }
    if (MajorSalveAmount > 0 && Missing <= 50)   { OutHealAmount = 50;                            return EGF_TamerHealItem::MajorHeal; }
    if (FullStoreAmount > 0)                      { OutHealAmount = FMath::RoundToInt(Current.MaxHP); return EGF_TamerHealItem::FullHeal; }
    if (MajorSalveAmount > 0)                    { OutHealAmount = 50;                            return EGF_TamerHealItem::MajorHeal; }
    if (PotionAmount > 0)                         { OutHealAmount = 20;                            return EGF_TamerHealItem::MinorHeal; }

    return EGF_TamerHealItem::None;  // No items left in stock
}

FGF_TamerHealResult AGF_TamerMaster::PreviewHealingItem(EGF_TamerHealItem Item) const
{
    FGF_TamerHealResult Result;

    AGF_Creature* Mon = GetCurrentCreature();
    if (!Mon)
    {
        return Result;  // bSuccess stays false
    }

    // Check stock and the nominal heal value for this item (no mutation here)
    bool bInStock = false;
    int32 Heal = 0;
    switch (Item)
    {
        case EGF_TamerHealItem::MinorHeal:      bInStock = PotionAmount > 0;      Heal = 20; break;
        case EGF_TamerHealItem::MajorHeal: bInStock = MajorSalveAmount > 0; Heal = 50; break;
        case EGF_TamerHealItem::FullHeal: bInStock = FullStoreAmount > 0;   Heal = FMath::RoundToInt(Mon->CurrentStats.MaxHP); break;
        default:                            return Result;
    }

    if (!bInStock)
    {
        return Result;  // out of stock - bSuccess stays false
    }

    const int32 MaxHP = FMath::RoundToInt(Mon->CurrentStats.MaxHP);
    Result.OldHP        = FMath::RoundToInt(Mon->CurrentStats.CurrentHP);
    Result.NewHP        = FMath::Min(Result.OldHP + Heal, MaxHP);  // clamp to MaxHP
    Result.AmountHealed = Result.NewHP - Result.OldHP;
    Result.bStatusCured = (Item == EGF_TamerHealItem::FullHeal && Mon->Status != EGF_STATUS::None);
    Result.bSuccess     = true;

    return Result;
}

bool AGF_TamerMaster::ConsumeHealingItem(EGF_TamerHealItem Item)
{
    switch (Item)
    {
        case EGF_TamerHealItem::MinorHeal:      if (PotionAmount      <= 0) return false; PotionAmount--;      return true;
        case EGF_TamerHealItem::MajorHeal: if (MajorSalveAmount <= 0) return false; MajorSalveAmount--; return true;
        case EGF_TamerHealItem::FullHeal: if (FullStoreAmount   <= 0) return false; FullStoreAmount--;   return true;
        default:                            return false;
    }
}

void AGF_TamerMaster::CureStatus(int32 Index)
{
    if (!TeamInstanceData.IsValidIndex(Index))
    {
        return;
    }

    // Persistent copy — survives a switch-out so the status doesn't come back on re-send
    TeamInstanceData[Index].StatusCondition = EGF_STATUSEffect::None;

    // Live actor, if this Creature is currently on stage
    if (Team.IsValidIndex(Index) && IsValid(Team[Index]))
    {
        Team[Index]->Status = EGF_STATUS::None;            // major status (burn/poison/sleep/para/freeze)
        Team[Index]->SleepCounter = 0;
        Team[Index]->ResetVolatileStatuses();           // confusion etc.
    }

    // Let any bound UI clear the status icon
    OnTamerCreatureStatusChanged.Broadcast(Index, EGF_STATUSEffect::None);
}

FGF_TamerHealResult AGF_TamerMaster::ApplyHealingItem(EGF_TamerHealItem Item)
{
    // Preview validates stock + on-stage Creature and gives us the before/after values
    FGF_TamerHealResult Result = PreviewHealingItem(Item);
    if (!Result.bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("ApplyHealingItem: nothing to do (out of stock or no Creature)"));
        return Result;
    }

    AGF_Creature* Mon = GetCurrentCreature();  // guaranteed valid since Preview succeeded

    ConsumeHealingItem(Item);

    // Instant HP apply (non-animated path). For a tween, skip ApplyHealingItem and drive
    // CurrentStats.CurrentHP yourself between Result.OldHP and Result.NewHP.
    Mon->CurrentStats.CurrentHP = Result.NewHP;

    // Full Salve cures status
    if (Result.bStatusCured)
    {
        CureStatus(CurrentCreatureIndex);
    }

    // Persist HP into instance data for the AI / switches
    SyncCurrentCreatureStatus();

    UE_LOG(LogTemp, Warning, TEXT("Tamer %s healed %s: %d -> %d HP (status cured: %s)"),
        *TamerName.ToString(), *Mon->Name.ToString(),
        Result.OldHP, Result.NewHP, Result.bStatusCured ? TEXT("yes") : TEXT("no"));

    return Result;
}



float AGF_TamerMaster::GetTypeEffectiveness(EGF_Element SkillElement, EGF_Element DefenderType1, EGF_Element DefenderType2)
{
    return UGF_ElementLibrary::GetEffectiveness(SkillElement, DefenderType1, DefenderType2);
}

float AGF_TamerMaster::GetSingleTypeEffectiveness(EGF_Element AttackType, EGF_Element DefenseType)
{
    return UGF_ElementLibrary::GetMatchup(AttackType, DefenseType);
}

bool AGF_TamerMaster::HasTypeAdvantage(const FGF_CreatureInstanceData& Attacker, const FGF_CreatureInstanceData& Defender)
{
    UGF_CreatureSpeciesData* AttackerSpecies = Attacker.SpeciesData.LoadSynchronous();
    UGF_CreatureSpeciesData* DefenderSpecies = Defender.SpeciesData.LoadSynchronous();

    if (!AttackerSpecies || !DefenderSpecies)
    {
        return false;
    }

    // Check if any of attacker's moves are super effective
    for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : Attacker.Skills)
    {
        if (Skill.IsNull()) continue;

        TSubclassOf<AGF_SkillDefinition> SkillClass = Skill.LoadSynchronous();
        if (!SkillClass) continue;

        AGF_SkillDefinition* SkillDefaults = SkillClass.GetDefaultObject();
        if (SkillDefaults)
        {
            float Effectiveness = GetTypeEffectiveness(
                SkillDefaults->Type,
                DefenderSpecies->PrimaryElement,
                DefenderSpecies->SecondaryElement
            );
            if (Effectiveness >= 2.0f)
            {
                return true;
            }
        }
    }

    return false;
}











UGF_CreatureSpeciesData* AGF_TamerMaster::ResolveSetupSpecies(const FGF_TamerCreatureSetup& Setup)
{
	// LoadSynchronous, not Get()/Resolve(): the whole point of the soft ref is that
	// the asset is NOT already in memory, and a plain resolve would return null.
	// This is the same trap the Blueprint autocast falls into.
	return Setup.Species.IsNull() ? nullptr : Setup.Species.LoadSynchronous();
}
