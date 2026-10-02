// Fill out your copyright notice in the Description page of Project Settings.



#include "GF_CreatureManagerSubsystem.h"
#include "GF_ElementTypes.h"
#include "GF_DiagLog.h"

#include "Kismet/GameplayStatics.h"
#include "GF_ItemInventorySystem.h"
#include "GF_ItemDataManager.h"
#include "GF_BattleBridge.h"
#include "GF_CatchingLibrary.h"
#include "GF_CreatureTraits.h"
#include "GF_CreatureRules.h"
#include "GF_QuestSubsystem.h"
#include "Dialogue/GF_DialogueSubsystem.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "PaperFlipbook.h"
#include "GF_SimpleFollower.h"
#include "GF_RouteSubsystem.h"
#include "GF_RouteData.h"
#include "GF_CreatureMemoLibrary.h"
#include "GF_GridMovementComponent.h"
#include "Dialogue/GF_DialogueSubsystem.h"
#include "Settings/GF_SettingsSubsystem.h"
#include "Berries/GF_BerryGrowthSubsystem.h"
#include "UObject/Stack.h"   // FFrame::GetScriptCallstack - names the Blueprint that passed a bad party index



void UGF_CreatureManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    // Every subsystem this function fetches MUST be declared here. GetSubsystem
    // during collection init only returns subsystems that already exist, so without
    // a declared dependency the result depends on whatever order the collection
    // happens to use — and when it comes back null the guards below skip their work
    // *silently*, with no failed load and no crash to point at the cause.
    //
    // ItemDataManager: needed to restore the inventory and PlayerMoney from the save.
    // Missing it leaves the player with an empty bag and 0 money.
    // QuestSubsystem:  needed to load the story save. Missing it makes every quest
    // flag read false for the whole session.
    //
    // If you add another GetSubsystem call to this function, add it here too.
    Collection.InitializeDependency(UGF_ItemDataManager::StaticClass());
    Collection.InitializeDependency(UGF_QuestSubsystem::StaticClass());
    // GF_BerryGrowthSubsystem: needed to load which berry trees have been picked.
    // Missing it makes every tree ripe again at the start of every session.
    Collection.InitializeDependency(UGF_BerryGrowthSubsystem::StaticClass());

    Super::Initialize(Collection);

	SessionStartTime = FDateTime::Now();

	DefaultRespawnTile = FGF_GridCoordinate(124,2384);

    // The actor class every creature spawns as -- one Blueprint for the whole
    // roster, with species data driving its stats and animations.
    //
    // The project names its own class. A plugin that hardcodes an asset path is
    // wrong in every project except the one it was written for, and the failure
    // is silent: the load returns null, the base class is used instead, and a
    // creature spawns with no components and nothing on screen.
    //
    // Config/DefaultGame.ini:
    //   [GammaFramework.Creatures]
    //   CreatureActorClass=/Game/Godsmarch/Blueprints/VornMaster_G_BP.VornMaster_G_BP_C
    FString ConfiguredPath;
    if (GConfig && GConfig->GetString(TEXT("GammaFramework.Creatures"),
                                      TEXT("CreatureActorClass"),
                                      ConfiguredPath, GGameIni)
        && !ConfiguredPath.IsEmpty())
    {
        CreatureActorClass = LoadClass<AGF_Creature>(nullptr, *ConfiguredPath);

        UE_CLOG(!CreatureActorClass, LogTemp, Error,
            TEXT("CreatureManager: [GammaFramework.Creatures] CreatureActorClass is set to '%s' "
                 "but that class could not be loaded. Check the path and the _C suffix."),
            *ConfiguredPath);
    }

    // Legacy path, kept so Gamma Framework still boots without an ini entry.
    if (!CreatureActorClass)
    {
        CreatureActorClass = LoadClass<AGF_Creature>(nullptr,
            TEXT("/Game/BPS/PLAYER/CREATURE/UPDATED/BP_CreatureMaster.BP_CreatureMaster_C"));
    }

    if (!CreatureActorClass)
    {
        // Loud, because a creature spawned from the bare C++ class has no visual
        // components and will be invisible with no other symptom.
        UE_LOG(LogTemp, Error,
            TEXT("CreatureManager: no creature actor class resolved. Falling back to AGF_Creature, "
                 "which has NO visual components -- creatures will spawn invisible. Set "
                 "[GammaFramework.Creatures] CreatureActorClass in Config/DefaultGame.ini."));
        CreatureActorClass = AGF_Creature::StaticClass();
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("CreatureManager: creature actor class = %s"),
            *CreatureActorClass->GetName());
    }

    // Try to load existing save
    bool bSuccess = false;
    VaultSystem = UGF_VaultSystem::LoadFromDisk(bSuccess);
    bSaveDataLoaded = bSuccess;

    if (!bSuccess || !VaultSystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreatureManager: No save found, creating new box system"));

        // Create new box system
        VaultSystem = Cast<UGF_VaultSystem>(
            UGameplayStatics::CreateSaveGameObject(UGF_VaultSystem::StaticClass())
        );

        if (VaultSystem)
        {
            VaultSystem->InitializeVaultPages(14);
            UE_LOG(LogTemp, Log, TEXT("CreatureManager: Created new box system with 14 boxes"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("CreatureManager: Loaded save for %s with %d Creature in party"),
            *VaultSystem->PlayerName, VaultSystem->GetPartySize());

		if (VaultSystem->VaultPages.Num() == 0)
    {
        // Repair in memory only. This used to call SaveToDisk() here, and SaveToDisk()
        // also writes QuestSlot from the QuestSubsystem - which at this point in
        // Initialize() has not loaded yet and is therefore EMPTY. Any save that came
        // back with 0 boxes had its entire story file overwritten with nothing before
        // LoadStory() below ever got the chance to read it. The boxes ride out on the
        // player's next real save.
        UE_LOG(LogTemp, Warning, TEXT("Loaded save has 0 boxes! Initializing in memory (not saving - "
            "the quest flags have not been loaded yet and a write here would erase them)."));
        VaultSystem->InitializeVaultPages(14);
    }

        // Register anything the player owns but whose dex entry is missing. This is the
        // real Continue path: the main menu loads a save through Initialize(), not
        // through LoadGame() (which only runs for soft resets and the whiteout reload),
        // so hooking LoadGame() alone would never have reached a normal player.
        //
        // Safe this early because it loads NO assets and writes NO save - see the
        // function. Note the empty-index guard in BuildSpeciesIndex(): being the earliest
        // caller in the process is exactly the case it protects.
        BackfillCompendiumFromOwnedCreature();

        // Same deal for Husks carrying a real HP stat from before the fix.
        RepairFixedHPCreature();
    }



    // Create inventory system (MUST BE AFTER VaultSystem IS CREATED)
    Inventory = NewObject<UGF_ItemInventorySystem>(this);

    if (VaultSystem)
    {
        // ⭐ CRITICAL FIX: Get ItemDataManager to restore names
        UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();

        if (DataMgr)
        {
            // Set ItemDataManager reference in Inventory for name->ID lookups
            Inventory->SetItemDataManager(DataMgr);

            // Load inventory from VaultSystem using conversion WITH ItemDataManager
            Inventory->FromSaveFormat(VaultSystem->GetInventorySaveFormat(), DataMgr);
            Inventory->Money = VaultSystem->PlayerMoney;

            UE_LOG(LogTemp, Log, TEXT("CreatureManager: Loaded inventory with ₽%d"), Inventory->Money);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("CreatureManager: ItemDataManager not found! Inventory may not load correctly."));
        }

        // Starting items are no longer given automatically here.
        // Call GiveStartingItems() from Blueprint (e.g. your New Game flow) instead.
    }

	UGF_QuestSubsystem* QuestSys = GetGameInstance()->GetSubsystem<UGF_QuestSubsystem>();
	if (QuestSys)
	{
		bool bQuestLoaded = QuestSys->LoadStory("QuestSlot", 0 , true);
		if (bQuestLoaded)
		{
			UE_LOG(LogTemp, Warning, TEXT("Quest Save loaded! Flags loaded: %d"), QuestSys->GetFlagCount());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("No Quest Save found! Creating new one..."));
		}
	}
	else
	{
		// Previously silent. If this ever fires, every HasFlag call returns false
		// for the session because the story save was never read.
		UE_LOG(LogTemp, Error,
			TEXT("CreatureManager: QuestSubsystem unavailable during Initialize - quest flags were NOT loaded!"));
	}

	// Berry tree growth. Same reasoning as the quest slot above: without this read,
	// every harvested tree is ripe again at the start of every session.
	if (UGF_BerryGrowthSubsystem* BerrySys = GetGameInstance()->GetSubsystem<UGF_BerryGrowthSubsystem>())
	{
		BerrySys->LoadBerries();
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("CreatureManager: GF_BerryGrowthSubsystem unavailable during Initialize - berry "
			     "tree growth was NOT loaded!"));
	}

}

bool UGF_CreatureManagerSubsystem::CommitPlayerTransformToSave(AActor* PlayerActor, const TCHAR* CallSite)
{
    // WHY THIS EXISTS (tester log, patch 1.0.11, 2026-08-10):
    //
    //   12:38:55.863  Saved player at: X=10368.000 Y=93696.000 Z=66.424   <- real save, in front of the bag
    //   12:38:57.365  ActivateLevel /Game/MAPS/DemoIsland/MAP_StarterSelect
    //   12:38:59.968  GE Dialogue: SelectChoice(0) -> 'Yes'
    //   12:38:59.968  Saved player at: X=0.000 Y=0.000 Z=0.000            <- starter confirm saves again
    //   12:39:00.821  CreatureManager: Battle started
    //
    // Confirming the starter runs a full save, and by then the possessed pawn is the
    // starter-scene one sitting at the world origin - so the player's real overworld
    // position was overwritten with (0,0,0). On the next soft reset the save loaded
    // fine (23/15 flags restored, no error anywhere) and dropped the player at the
    // origin, which reads as "the editor's default spot in Hearthvale".
    //
    // Note bIsInBattle was still FALSE at that instant - the battle starts ~0.9s
    // later - so a battle check alone would not have caught it. Hence three
    // independent tests, any one of which is enough to reject the write.
    //
    // Rejecting means KEEPING what is already stored, which is by definition the last
    // position the player actually stood on. Everything else about the save proceeds
    // normally: the starter still gets committed, only the coordinates are protected.
    if (!VaultSystem)
    {
        return false;
    }

    const TCHAR* RejectReason = nullptr;

    if (!IsValid(PlayerActor))
    {
        RejectReason = TEXT("no player actor was passed");
    }
    // The overworld player is the only actor that owns a GridMovementComponent and is
    // possessed. Battle pawns, starter-scene pawns and cutscene rigs do not have one,
    // so this identifies "is this really the walking-around player" without this file
    // needing to know a single Blueprint class name.
    else if (!PlayerActor->FindComponentByClass<UGF_GridMovementComponent>())
    {
        RejectReason = TEXT("the actor has no GridMovementComponent, so it is not the overworld player");
    }
    // No legitimate tile in this game sits on the world origin - the map runs to tens
    // of thousands of units. Exactly (0,0,0) means an unplaced or scene-local pawn.
    else if (PlayerActor->GetActorLocation().IsNearlyZero(1.0f))
    {
        RejectReason = TEXT("the actor is at the world origin");
    }
    else if (bIsInBattle)
    {
        RejectReason = TEXT("a battle is in progress");
    }

    if (RejectReason)
    {
        UE_LOG(LogTemp, Error,
            TEXT("%s: REFUSED to overwrite the saved player position - %s. Keeping the last good "
                 "position %s on map '%s'. (Actor: %s)"),
            CallSite,
            RejectReason,
            *VaultSystem->PlayerLocation.ToString(),
            *VaultSystem->CurrentMapName,
            IsValid(PlayerActor) ? *PlayerActor->GetName() : TEXT("null"));
        return false;
    }

    VaultSystem->PlayerLocation = PlayerActor->GetActorLocation();
    VaultSystem->PlayerRotation = PlayerActor->GetActorRotation();

    // Only stamped alongside a position that was accepted - a map name from a scene
    // the player is not standing in is just as wrong as the coordinates.
    // NOTE: PlayerFacingVector is set from Blueprint before calling this function
    // (reads DirectionalVector directly from the PaperZD anim instance)
    if (UWorld* World = GetWorld())
    {
        VaultSystem->CurrentMapName = World->GetMapName();
        VaultSystem->CurrentMapName.RemoveFromStart(TEXT("UEDPIE_0_"));
    }

    UE_LOG(LogTemp, Log, TEXT("Saved player at: %s, map: %s"),
        *VaultSystem->PlayerLocation.ToString(),
        *VaultSystem->CurrentMapName);

    return true;
}

bool UGF_CreatureManagerSubsystem::SaveGameWithLocation(AActor* PlayerActor, AGF_SimpleFollower* FollowerActor)
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot save: VaultSystem is null"));
        return false;
    }

    // Update playtime before saving
    UpdatePlaytime();

    // Update save timestamp
    VaultSystem->LastSaveDateTime = FDateTime::Now();

    // If this is a new save, set creation time
    if (VaultSystem->SaveCreationDateTime.GetTicks() == 0)
    {
        VaultSystem->SaveCreationDateTime = VaultSystem->LastSaveDateTime;
    }

    // Save player location — validated, see CommitPlayerTransformToSave.
    CommitPlayerTransformToSave(PlayerActor, TEXT("SaveGameWithLocation"));

    // Save follower status (connected to SimpleFollower system)
    if (FollowerActor && IsValid(FollowerActor))
    {
        VaultSystem->bHasFollowerOut = true;
        VaultSystem->FollowerPartyIndex = FollowerActor->PartyIndex;

        FGF_CreatureInstanceData FollowerData;
        if (VaultSystem->GetPartyCreature(FollowerActor->PartyIndex, FollowerData))
        {
            UE_LOG(LogTemp, Log, TEXT("Saved with follower: %s (Party Index %d)"),
                *FollowerData.GetDisplayName().ToString(),
                FollowerActor->PartyIndex);
        }
    }
    else
    {
        VaultSystem->bHasFollowerOut = false;
        VaultSystem->FollowerPartyIndex = -1;
        UE_LOG(LogTemp, Log, TEXT("Saved without follower"));
    }

	// One entry per party slot, in party order — the title screen pairs this list with
	// nothing else, but anything that indexes it must line up with the party.
	//
	// IsNull() rather than IsValid(): IsValid() on a soft pointer means "already loaded
	// into memory", so a species that had not been loaded yet was silently skipped, which
	// shortened the list and shifted every later slot up one.
	VaultSystem->SavedPartyPreview.Empty();
	for (const FGF_CreatureInstanceData& Creature : VaultSystem->Party)
	{
		const UGF_CreatureSpeciesData* SpeciesData = Creature.SpeciesData.IsNull() ? nullptr : Creature.SpeciesData.LoadSynchronous();

		VaultSystem->SavedPartyPreview.Add(SpeciesData ? SpeciesData->CompendiumNumber : 0);
	}

    // Save inventory to VaultSystem
    if (Inventory)
    {
        TMap<uint8, TMap<FName, int32>> InventorySaveData = Inventory->ToSaveFormat();
        VaultSystem->SetInventoryFromSaveFormat(InventorySaveData);
        VaultSystem->PlayerMoney = Inventory->Money;
    }

    // Save current route so it can be restored on load
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UGF_RouteSubsystem* RouteSys = GI->GetSubsystem<UGF_RouteSubsystem>())
        {
            if (UGF_RouteData* CurrentRoute = RouteSys->GetCurrentRoute())
            {
                VaultSystem->SavedRoutePath = FSoftObjectPath(CurrentRoute);
            }
        }
    }

    // Perform save
    bool bSuccess = VaultSystem->SaveToDisk();

    if (bSuccess)
    {
        bHasUnsavedChanges = false;
        LastSaveTime = FDateTime::Now();

        UE_LOG(LogTemp, Log, TEXT("Game saved! Playtime: %.2f hours"),
            VaultSystem->TotalTimePlayed / 3600.0f);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Save failed!"));
    }

    return bSuccess;
}

void UGF_CreatureManagerSubsystem::GetSavedPartyPreview(TArray<UGF_CreatureSpeciesData*>& OutSpeciesDataArray) const
{
	OutSpeciesDataArray.Empty();

	if (!VaultSystem)
	{
		return;
	}

	// Read the LIVE party whenever there is one.
	//
	// SavedPartyPreview is only rebuilt when the game is saved, so after a party swap it
	// still holds the order the party had at the last save. The save screen pairs this
	// list with the egg flag read from the live party (GetPartyCreatureData -> bIsEgg), so
	// a stale order makes the two disagree and a slot draws someone else's species — swap
	// an egg with the Creature next to it and the Creature's slot shows what is inside the
	// egg. Building from the party here keeps index i meaning the same Creature in both.
	if (VaultSystem->Party.Num() > 0)
	{
		for (const FGF_CreatureInstanceData& Creature : VaultSystem->Party)
		{
			// IsNull(), not IsValid(): IsValid() means "already loaded into memory", so a
			// species that simply has not been loaded yet would be dropped and every slot
			// after it would shift up one — the same misalignment by a different route.
			OutSpeciesDataArray.Add(Creature.SpeciesData.IsNull() ? nullptr : Creature.SpeciesData.LoadSynchronous());
		}

		return;
	}

	// No live party — the title screen before a save is loaded. Fall back to the snapshot,
	// which is the only thing that exists at that point.
	for (int32 PokdexNumber : VaultSystem->SavedPartyPreview)
	{
		UGF_CreatureSpeciesData* SpeciesData = GetCreatureSpeciesDataByCompendiumNumber(PokdexNumber);
		if (SpeciesData)
		{
			OutSpeciesDataArray.Add(SpeciesData);
		}
	}
}

TArray<int32> UGF_CreatureManagerSubsystem::GetSavedPartyCompendiumNumbers() const
{
	if (!VaultSystem)
	{
		return TArray<int32>();
	}

	// Live party first, for the same reason as GetSavedPartyPreview: the stored snapshot
	// is one save behind, and anything pairing it with live party data by index breaks.
	if (VaultSystem->Party.Num() > 0)
	{
		TArray<int32> DexNumbers;
		DexNumbers.Reserve(VaultSystem->Party.Num());

		for (const FGF_CreatureInstanceData& Creature : VaultSystem->Party)
		{
			const UGF_CreatureSpeciesData* Species = Creature.SpeciesData.IsNull() ? nullptr : Creature.SpeciesData.LoadSynchronous();

			// 0 rather than a skipped entry — a hole must not shift every later slot.
			DexNumbers.Add(Species ? Species->CompendiumNumber : 0);
		}

		return DexNumbers;
	}

	return VaultSystem->SavedPartyPreview;
}

// ---------------------------------------------------------------------------
// SPECIES LOOKUP
//
// Every function below used to begin by loading all 119 species assets, because
// the only way to answer "which asset is Cindling?" was to load them all and
// compare SpeciesName. Each asset hard-references its full sprite set, so that
// one question cost ~4.18 GB and ~48s on a cold disk -- paid at boot, and the
// direct cause of the Steam Deck out-of-memory.
//
// Now a registry index answers the question from metadata (see BuildSpeciesIndex)
// and only the requested species is loaded.
// ---------------------------------------------------------------------------

void UGF_CreatureManagerSubsystem::BuildSpeciesIndex() const
{
	if (bSpeciesIndexBuilt)
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

#if WITH_EDITOR
	// A partial scan would silently index a fraction of the species and make
	// lookups fall through to the slow path for the rest.
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.WaitForCompletion();
	}
#endif

	// Where the project keeps its species assets.
	//
	// A plugin cannot know the host project's folder layout, and a hardcoded
	// path fails quietly: the scan returns nothing, every lookup falls through,
	// and stats recalculate against no species data while the rest of the
	// operation reports success.
	//
	// Config/DefaultGame.ini:
	//   [GammaFramework.Creatures]
	//   +SpeciesDataPaths=/Game/Godsmarch/CreatureData
	TArray<FString> SpeciesScanPaths;
	if (GConfig)
	{
		GConfig->GetArray(TEXT("GammaFramework.Creatures"),
		                  TEXT("SpeciesDataPaths"),
		                  SpeciesScanPaths, GGameIni);
	}

	// Legacy path, kept so Gamma Framework still boots without an ini entry.
	if (SpeciesScanPaths.Num() == 0)
	{
		SpeciesScanPaths.Add(TEXT("/Game/BPS/CreatureData/Creature"));
	}

	FARFilter Filter;
	Filter.ClassPaths.Add(UGF_CreatureSpeciesData::StaticClass()->GetClassPathName());
	for (const FString& ScanPath : SpeciesScanPaths)
	{
		Filter.PackagePaths.Add(FName(*ScanPath));
	}
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	SpeciesPathsByName.Empty(AssetDataList.Num());
	SpeciesPathsByDex.Empty(AssetDataList.Num());
	SpeciesSummaries.Empty(AssetDataList.Num());
	PreEvolutionByName.Empty();
	UntaggedSpeciesPaths.Empty();

	const FName SpeciesNameTag = GET_MEMBER_NAME_CHECKED(UGF_CreatureSpeciesData, SpeciesName);
	const FName CompendiumNumberTag = GET_MEMBER_NAME_CHECKED(UGF_CreatureSpeciesData, CompendiumNumber);
	const FName PrimaryElementTag = GET_MEMBER_NAME_CHECKED(UGF_CreatureSpeciesData, PrimaryElement);
	const FName SecondaryElementTag = GET_MEMBER_NAME_CHECKED(UGF_CreatureSpeciesData, SecondaryElement);
	const FName DisplayIconTag = GET_MEMBER_NAME_CHECKED(UGF_CreatureSpeciesData, DisplayIcon1);
	const FName BattleIdleTag = UGF_CreatureSpeciesData::BattleIdleAnimationTagName;
	const FName EvolutionTargetsTag = UGF_CreatureSpeciesData::EvolutionTargetsTagName;

	// Enum tags are stored by name ("Fire" or "EGF_Element::Fire" depending on how
	// the value was written), so try both spellings before giving up.
	const UEnum* TypeEnum = StaticEnum<EGF_Element>();
	auto ParseType = [TypeEnum](const FString& Raw) -> EGF_Element
	{
		if (Raw.IsEmpty() || !TypeEnum)
		{
			return EGF_Element::None;
		}
		int64 Value = TypeEnum->GetValueByNameString(Raw);
		if (Value == INDEX_NONE)
		{
			Value = TypeEnum->GetValueByNameString(FString::Printf(TEXT("EGF_Element::%s"), *Raw));
		}
		return (Value == INDEX_NONE) ? EGF_Element::None : static_cast<EGF_Element>(Value);
	};

	for (const FAssetData& AssetData : AssetDataList)
	{
		// GetTagValue reads registry metadata. It does NOT load the asset -- that is
		// the entire point of this function.
		FName TaggedName = NAME_None;
		const bool bHasName = AssetData.GetTagValue(SpeciesNameTag, TaggedName) && !TaggedName.IsNone();

		int32 TaggedDex = INDEX_NONE;
		const bool bHasDex = AssetData.GetTagValue(CompendiumNumberTag, TaggedDex) && TaggedDex > 0;

		if (!bHasName && !bHasDex)
		{
			// Asset predates the AssetRegistrySearchable tags. Resolved lazily.
			UntaggedSpeciesPaths.Add(AssetData.ToSoftObjectPath());
			continue;
		}

		if (bHasName)
		{
			SpeciesPathsByName.Add(TaggedName, AssetData.ToSoftObjectPath());
		}
		if (bHasDex)
		{
			SpeciesPathsByDex.Add(TaggedDex, AssetData.ToSoftObjectPath());
		}

		FGF_SpeciesSummary Summary;
		Summary.SpeciesName = TaggedName;
		Summary.CompendiumNumber = bHasDex ? TaggedDex : 0;
		Summary.bIsValid = true;

		FString TypeStr;
		if (AssetData.GetTagValue(PrimaryElementTag, TypeStr))
		{
			Summary.PrimaryElement = ParseType(TypeStr);
		}
		if (AssetData.GetTagValue(SecondaryElementTag, TypeStr))
		{
			Summary.SecondaryElement = ParseType(TypeStr);
		}

		// An AssetRegistrySearchable object property stores an export-text path
		// ("Class'/Game/Path.Asset'"), which has to be reduced to a plain object
		// path before FSoftObjectPath will accept it.
		FString IconStr;
		if (AssetData.GetTagValue(DisplayIconTag, IconStr) && !IconStr.IsEmpty() && IconStr != TEXT("None"))
		{
			Summary.DisplayIcon = TSoftObjectPtr<UPaperSprite>(
				FSoftObjectPath(FPackageName::ExportTextPathToObjectPath(IconStr)));
		}

		FString IdleStr;
		if (AssetData.GetTagValue(BattleIdleTag, IdleStr) && !IdleStr.IsEmpty() && IdleStr != TEXT("None"))
		{
			Summary.BattleIdleAnimation = TSoftObjectPtr<UPaperFlipbook>(
				FSoftObjectPath(FPackageName::ExportTextPathToObjectPath(IdleStr)));
		}

		// Invert "what I evolve into" into "what evolves into me". Written as a comma
		// separated list by UGF_CreatureSpeciesData::GetAssetRegistryTags.
		FString EvoStr;
		if (bHasName && AssetData.GetTagValue(EvolutionTargetsTag, EvoStr) && !EvoStr.IsEmpty())
		{
			TArray<FString> Targets;
			EvoStr.ParseIntoArray(Targets, TEXT(","), /*InCullEmpty*/ true);
			for (const FString& Target : Targets)
			{
				const FName TargetName(*Target.TrimStartAndEnd());
				if (!TargetName.IsNone() && TargetName != TaggedName)
				{
					// First writer wins: a species reachable from two pre-evolutions keeps
					// the first, which matches the old linear search's behaviour.
					PreEvolutionByName.FindOrAdd(TargetName, TaggedName);
				}
			}
		}

		SpeciesSummaries.Add(MoveTemp(Summary));
	}

	SpeciesSummaries.Sort([](const FGF_SpeciesSummary& A, const FGF_SpeciesSummary& B)
	{
		return A.CompendiumNumber < B.CompendiumNumber;
	});

	// Never latch an EMPTY index. This is the only cache in the species system, so if
	// something asks before the asset registry is populated, latching zero here would
	// make every species lookup fail for the rest of the session. The WaitForCompletion
	// above is editor-only, and a GameInstance subsystem's Initialize() is early enough
	// to be worth guarding against. Leaving the flag clear just costs the next caller
	// another registry query.
	bSpeciesIndexBuilt = (AssetDataList.Num() > 0);

	if (AssetDataList.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("CreatureManager: the asset registry returned NO species assets under %s. "
			     "Not caching that - the index will be rebuilt on the next lookup. If this repeats, "
			     "set [GammaFramework.Creatures] SpeciesDataPaths in Config/DefaultGame.ini to the "
			     "folder holding this project's species assets."),
			*FString::Join(SpeciesScanPaths, TEXT(", ")));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("CreatureManager: species index built -- %d tagged, %d untagged, 0 assets loaded."),
		SpeciesPathsByName.Num(), UntaggedSpeciesPaths.Num());

	if (UntaggedSpeciesPaths.Num() > 0 && !bWarnedAboutUntaggedSpecies)
	{
		bWarnedAboutUntaggedSpecies = true;
		UE_LOG(LogTemp, Warning,
			TEXT("CreatureManager: %d species asset(s) have no registry tags, so looking one up may load several. ")
			TEXT("Fix with: UnrealEditor-Cmd.exe <uproject> -run=ResavePackages -PackageFolder=%s -ProjectOnly"),
			UntaggedSpeciesPaths.Num(),
			*FString::Join(SpeciesScanPaths, TEXT(" -PackageFolder=")));
	}
}

void UGF_CreatureManagerSubsystem::RegisterSpeciesInIndex(UGF_CreatureSpeciesData* Species, const FSoftObjectPath& Path) const
{
	if (!Species)
	{
		return;
	}

	if (!Species->SpeciesName.IsNone())
	{
		SpeciesPathsByName.Add(Species->SpeciesName, Path);
		const_cast<UGF_CreatureManagerSubsystem*>(this)->LoadedSpeciesByName.Add(Species->SpeciesName, Species);
	}
	if (Species->CompendiumNumber > 0)
	{
		SpeciesPathsByDex.Add(Species->CompendiumNumber, Path);
	}
}

UGF_CreatureSpeciesData* UGF_CreatureManagerSubsystem::ResolveUntaggedSpecies(FName SpeciesName, int32 CompendiumNumber) const
{
	// Only reachable for assets that have not been resaved since the
	// AssetRegistrySearchable tags were added. Each iteration loads one asset, so
	// this walks the list rather than bulk-loading -- worst case it ends up where
	// the old code started, best case it stops on the first entry.
	for (int32 Index = 0; Index < UntaggedSpeciesPaths.Num(); ++Index)
	{
		const FSoftObjectPath Path = UntaggedSpeciesPaths[Index];
		UGF_CreatureSpeciesData* Species = Cast<UGF_CreatureSpeciesData>(Path.TryLoad());
		if (!Species)
		{
			UntaggedSpeciesPaths.RemoveAt(Index--);
			continue;
		}

		RegisterSpeciesInIndex(Species, Path);
		UntaggedSpeciesPaths.RemoveAt(Index--);

		const bool bNameMatch = !SpeciesName.IsNone() && Species->SpeciesName == SpeciesName;
		const bool bDexMatch = CompendiumNumber > 0 && Species->CompendiumNumber == CompendiumNumber;
		if (bNameMatch || bDexMatch)
		{
			return Species;
		}
	}

	return nullptr;
}

UGF_CreatureSpeciesData* UGF_CreatureManagerSubsystem::GetCreatureSpeciesDataByCompendiumNumber(int32 CompendiumNumber) const
{
	if (CompendiumNumber <= 0)
	{
		return nullptr;
	}

	BuildSpeciesIndex();

	if (const FSoftObjectPath* Path = SpeciesPathsByDex.Find(CompendiumNumber))
	{
		if (UGF_CreatureSpeciesData* Species = Cast<UGF_CreatureSpeciesData>(Path->TryLoad()))
		{
			RegisterSpeciesInIndex(Species, *Path);
			return Species;
		}
	}

	// Already-loaded species may not be in SpeciesPathsByDex if their asset was
	// untagged, so check what is resident before paying to load anything.
	for (const TPair<FName, TObjectPtr<UGF_CreatureSpeciesData>>& Pair : LoadedSpeciesByName)
	{
		if (Pair.Value && Pair.Value->CompendiumNumber == CompendiumNumber)
		{
			return Pair.Value;
		}
	}

	return ResolveUntaggedSpecies(NAME_None, CompendiumNumber);
}

TArray<UGF_CreatureSpeciesData*> UGF_CreatureManagerSubsystem::GetAllCreatureSpeciesData() const
{
	// Return cache if already built — avoids repeated registry scans and disk loads
	if (bSpeciesCacheBuilt)
	{
		return CachedAllSpecies;
	}

	// First call: scan registry, load assets, cache the result
	const_cast<UGF_CreatureManagerSubsystem*>(this)->PreloadAllSpeciesData();
	return CachedAllSpecies;
}

TArray<FGF_SpeciesSummary> UGF_CreatureManagerSubsystem::GetAllSpeciesSummaries() const
{
	BuildSpeciesIndex();
	return SpeciesSummaries;
}

FGF_SpeciesSummary UGF_CreatureManagerSubsystem::GetSpeciesSummary(FName SpeciesName) const
{
	BuildSpeciesIndex();

	for (const FGF_SpeciesSummary& Summary : SpeciesSummaries)
	{
		if (Summary.SpeciesName == SpeciesName)
		{
			return Summary;
		}
	}
	return FGF_SpeciesSummary();
}

FName UGF_CreatureManagerSubsystem::GetPreEvolutionName(FName SpeciesName) const
{
	if (SpeciesName.IsNone())
	{
		return NAME_None;
	}

	BuildSpeciesIndex();

	if (const FName* Found = PreEvolutionByName.Find(SpeciesName))
	{
		return *Found;
	}
	return NAME_None;
}

void UGF_CreatureManagerSubsystem::CancelSpeciesPreviewRequest()
{
	if (PreviewHandle.IsValid())
	{
		PreviewHandle->CancelHandle();
		PreviewHandle.Reset();
	}
}

FName UGF_CreatureManagerSubsystem::GetSpeciesNameForInstance(const FGF_CreatureInstanceData& CreatureData) const
{
	if (CreatureData.SpeciesData.IsNull())
	{
		return NAME_None;
	}

	// If it happens to be loaded already, read it straight off. Cheapest path, and it also covers
	// a species whose asset is somehow absent from the index.
	if (const UGF_CreatureSpeciesData* Loaded = CreatureData.SpeciesData.Get())
	{
		if (!Loaded->SpeciesName.IsNone())
		{
			return Loaded->SpeciesName;
		}
	}

	// Otherwise reverse the registry index: path -> name. Loads nothing.
	BuildSpeciesIndex();

	const FSoftObjectPath WantedPath = CreatureData.SpeciesData.ToSoftObjectPath();
	for (const TPair<FName, FSoftObjectPath>& Entry : SpeciesPathsByName)
	{
		if (Entry.Value == WantedPath)
		{
			return Entry.Key;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("GetSpeciesNameForInstance: '%s' is not in the species index, so its name cannot be ")
		TEXT("resolved without loading it. A preview request for this Creature will do nothing."),
		*WantedPath.ToString());

	return NAME_None;
}

void UGF_CreatureManagerSubsystem::RequestSpeciesPreviewAsync(FName SpeciesName, FGF_GESpeciesPreviewLoaded OnLoaded)
{
	// Whatever was in flight is for an entry the player has already moved off.
	CancelSpeciesPreviewRequest();

	const FGF_SpeciesSummary Summary = GetSpeciesSummary(SpeciesName);
	if (!Summary.bIsValid || Summary.BattleIdleAnimation.IsNull())
	{
		OnLoaded.ExecuteIfBound(nullptr);
		return;
	}

	// Already resident (revisiting an entry) -- answer on the spot rather than making the
	// caller wait a frame for a load that is not going to happen.
	if (UPaperFlipbook* Resident = Summary.BattleIdleAnimation.Get())
	{
		UE_LOG(LogTemp, Display, TEXT("SpeciesPreview '%s': already resident (0 ms)."), *SpeciesName.ToString());
		OnLoaded.ExecuteIfBound(Resident);
		return;
	}

	const TSoftObjectPtr<UPaperFlipbook> Soft = Summary.BattleIdleAnimation;
	const double RequestStart = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("SpeciesPreview '%s': requesting %s"),
		*SpeciesName.ToString(), *Soft.ToSoftObjectPath().ToString());

	// AsyncLoadHighPriority is the whole point: at default priority this project's
	// s.AsyncLoadingTimeLimit=1.85 stretches a preview load out to seconds.
	PreviewHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Soft.ToSoftObjectPath(),
		FStreamableDelegate::CreateWeakLambda(this, [this, Soft, OnLoaded, SpeciesName, RequestStart]()
		{
			PreviewHandle.Reset();
			UPaperFlipbook* Loaded = Soft.Get();
			UE_LOG(LogTemp, Display, TEXT("SpeciesPreview '%s': streamable finished in %.0f ms (loaded=%s)"),
				*SpeciesName.ToString(), (FPlatformTime::Seconds() - RequestStart) * 1000.0,
				Loaded ? TEXT("yes") : TEXT("NO"));
			OnLoaded.ExecuteIfBound(Loaded);
		}),
		FStreamableManager::AsyncLoadHighPriority,
		/*bManageActiveHandle*/ false,
		/*bStartStalled*/ false,
		TEXT("SpeciesPreview"));
}

int32 UGF_CreatureManagerSubsystem::GetTotalCreatureSpeciesCount() const
{
	if (bSpeciesCacheBuilt)
	{
		return CachedAllSpecies.Num();
	}

	// Answered from registry metadata. This used to load every species asset just
	// to call .Num() on the result.
	BuildSpeciesIndex();
	return SpeciesPathsByName.Num() + UntaggedSpeciesPaths.Num();
}

int32 UGF_CreatureManagerSubsystem::WarmSpecies(const TArray<FName>& SpeciesNames)
{
	int32 NewlyLoaded = 0;

	for (const FName& Name : SpeciesNames)
	{
		if (Name.IsNone() || LoadedSpeciesByName.Contains(Name))
		{
			continue;
		}
		if (GetCreatureSpeciesData(Name))
		{
			++NewlyLoaded;
		}
	}

	if (NewlyLoaded > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("CreatureManager: warmed %d species (%d now resident)."),
			NewlyLoaded, LoadedSpeciesByName.Num());
	}
	return NewlyLoaded;
}

void UGF_CreatureManagerSubsystem::PreloadAllSpeciesData()
{
	if (bSpeciesCacheBuilt)
	{
		return;
	}

	// Measured: ~4.18 GB resident, ~48s cold. Nothing in normal play should reach
	// this -- if it shows up in a tester's log, that is the bug.
	UE_LOG(LogTemp, Warning, TEXT("CreatureManager: loading ALL species data assets (expensive: ~4 GB resident)."));

	BuildSpeciesIndex();

	TSet<FSoftObjectPath> AllPaths;
	for (const TPair<FName, FSoftObjectPath>& Pair : SpeciesPathsByName)
	{
		AllPaths.Add(Pair.Value);
	}
	for (const TPair<int32, FSoftObjectPath>& Pair : SpeciesPathsByDex)
	{
		AllPaths.Add(Pair.Value);
	}
	for (const FSoftObjectPath& Path : UntaggedSpeciesPaths)
	{
		AllPaths.Add(Path);
	}

	CachedAllSpecies.Empty(AllPaths.Num());
	for (const FSoftObjectPath& Path : AllPaths)
	{
		if (UGF_CreatureSpeciesData* Species = Cast<UGF_CreatureSpeciesData>(Path.TryLoad()))
		{
			RegisterSpeciesInIndex(Species, Path);
			CachedAllSpecies.Add(Species);
		}
	}
	UntaggedSpeciesPaths.Empty();

	CachedAllSpecies.Sort([](const UGF_CreatureSpeciesData& A, const UGF_CreatureSpeciesData& B)
	{
		return A.CompendiumNumber < B.CompendiumNumber;
	});

	bSpeciesCacheBuilt = true;

	UE_LOG(LogTemp, Log, TEXT("CreatureManager: Cached %d species data assets."), CachedAllSpecies.Num());
}


void UGF_CreatureManagerSubsystem::GetSaveInfo(FString& OutSaveDate, FString& OutSaveTime, FString& OutPlaytime, FString& OutLocation, FString& OutGameStartDate) const
{
    if (!VaultSystem)
    {
        OutSaveDate = TEXT("No Save");
        OutSaveTime = TEXT("--:--");
        OutPlaytime = TEXT("0h 0m");
        OutLocation = TEXT("Unknown");
        OutGameStartDate = TEXT("Unknown");
        return;
    }

    // Format date: "Jan 03, 2026"
    OutSaveDate = VaultSystem->LastSaveDateTime.ToString(TEXT("%m/%d/%Y"));

    // Date the player first started this save
    OutGameStartDate = VaultSystem->SaveCreationDateTime.GetTicks() > 0
        ? VaultSystem->SaveCreationDateTime.ToString(TEXT("%m/%d/%Y"))
        : TEXT("Unknown");

    // Format time: "14:35" (24-hour)
    OutSaveTime = VaultSystem->LastSaveDateTime.ToString(TEXT("%H:%M"));

    // Format playtime: "15h 23m"
    int32 TotalSeconds = FMath::FloorToInt(VaultSystem->TotalTimePlayed);
    int32 Hours = TotalSeconds / 3600;
    int32 Minutes = (TotalSeconds % 3600) / 60;
    OutPlaytime = FString::Printf(TEXT("%dh %dm"), Hours, Minutes);

    // Get location — prefer route display name (with player name substitution), fall back to raw map name
    OutLocation = TEXT("Unknown");
    if (VaultSystem->SavedRoutePath.IsValid())
    {
        if (UGF_RouteData* SavedRoute = Cast<UGF_RouteData>(VaultSystem->SavedRoutePath.TryLoad()))
        {
            OutLocation = SavedRoute->GetFormattedDisplayName(VaultSystem->PlayerName).ToString();
        }
    }
    if (OutLocation == TEXT("Unknown") && !VaultSystem->CurrentMapName.IsEmpty())
    {
        OutLocation = VaultSystem->CurrentMapName;
    }
}

float UGF_CreatureManagerSubsystem::GetTotalPlaytimeHours() const
{
    return VaultSystem ? (VaultSystem->TotalTimePlayed / 3600.0f) : 0.0f;
}

FName UGF_CreatureManagerSubsystem::GetSavedRouteName() const
{
    if (!VaultSystem || !VaultSystem->SavedRoutePath.IsValid())
        return NAME_None;

    if (UGF_RouteData* Route = Cast<UGF_RouteData>(VaultSystem->SavedRoutePath.TryLoad()))
        return Route->RouteName;

    return NAME_None;
}

UGF_RouteData* UGF_CreatureManagerSubsystem::GetSavedRouteData() const
{
    if (!VaultSystem || !VaultSystem->SavedRoutePath.IsValid())
        return nullptr;

    return Cast<UGF_RouteData>(VaultSystem->SavedRoutePath.TryLoad());
}

void UGF_CreatureManagerSubsystem::UpdatePlaytime()
	{
	    if (!VaultSystem) return;

	    // Calculate time since session started
	    FDateTime Now = FDateTime::Now();
	    FTimespan SessionDuration = Now - SessionStartTime;

	    // Add to total playtime
	    VaultSystem->TotalTimePlayed += SessionDuration.GetTotalSeconds();



	    // Reset session start for next update
	    SessionStartTime = Now;
	}



void UGF_CreatureManagerSubsystem::Deinitialize()

{

	// Only auto-save on shutdown if we're NOT in battle

	// (prevents saving mid-battle state if PIE stops unexpectedly)


	///////////////////////////////////////
	///AUTO SAVE
	//////////////////////////////////////
	//if (VaultSystem && !bIsInBattle)

	//{

	//	SaveGame();

	//	UE_LOG(LogTemp, Log, TEXT("CreatureManager: Auto-saved on shutdown"));

	//}

	//else if (bIsInBattle)

	//{

	//	UE_LOG(LogTemp, Warning, TEXT("CreatureManager: Skipped auto-save (in battle)"));

	//}



	// Clean up any active Creature actors

	for (AGF_Creature* Actor : ActiveCreatureActors)

	{

		if (Actor && IsValid(Actor))

		{

			Actor->Destroy();

		}

	}

	ActiveCreatureActors.Empty();



	Super::Deinitialize();

}



//--------------------

// PARTY MANAGEMENT

//--------------------



int32 UGF_CreatureManagerSubsystem::GetPartySize() const

{

	return VaultSystem ? VaultSystem->GetPartySize() : 0;

}



bool UGF_CreatureManagerSubsystem::IsPartyFull() const

{

	return VaultSystem ? VaultSystem->IsPartyFull() : false;

}



bool UGF_CreatureManagerSubsystem::GetPartyCreatureData(int32 Index, FGF_CreatureInstanceData& OutData) const

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->GetPartyCreature(Index, OutData);

}



bool UGF_CreatureManagerSubsystem::SyncBattleDataToParty(int32 Index, const FGF_CreatureInstanceData& BattleData)
{
    if (!VaultSystem) return false;

    FGF_CreatureInstanceData Existing;
    if (!VaultSystem->GetPartyCreature(Index, Existing)) return false;

    // Copy only battle-relevant fields — leave Skills untouched
    Existing.CurrentHP       = BattleData.CurrentHP;
    Existing.StatusCondition = BattleData.StatusCondition;
    Existing.CurrentEXP      = BattleData.CurrentEXP;
    Existing.Level           = BattleData.Level;

    // Uses is NOT copied from the battle actor — see UpdatePartyCreatureBattleData().
    // The party entry owns Uses (UseSkill writes it); the actor's SkillUses copy is a read
    // cache for the usable/LastResort check and must never be written back over the real
    // value, or Uses spent on one move reappears on another in the next battle.

    return VaultSystem->UpdatePartyCreature(Index, Existing);
}

bool UGF_CreatureManagerSubsystem::UpdatePartyCreatureData(int32 Index, const FGF_CreatureInstanceData& UpdatedData)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->UpdatePartyCreature(Index, UpdatedData);

}



bool UGF_CreatureManagerSubsystem::SetCreatureNickname(int32 PartyIndex, FName Nickname)
{
    if (!VaultSystem) return false;

    FGF_CreatureInstanceData Data;
    if (!VaultSystem->GetPartyCreature(PartyIndex, Data)) return false;

    Data.Nickname = Nickname;
    MarkDirty();
    return VaultSystem->UpdatePartyCreature(PartyIndex, Data);
}

bool UGF_CreatureManagerSubsystem::AddToParty(const FGF_CreatureInstanceData& CreatureData)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->AddToParty(CreatureData);

}



bool UGF_CreatureManagerSubsystem::RemoveFromParty(int32 Index)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->RemoveFromParty(Index);

}



bool UGF_CreatureManagerSubsystem::SwapPartyCreature(int32 Index1, int32 Index2)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->SwapPartyCreature(Index1, Index2);

}



int32 UGF_CreatureManagerSubsystem::GetFirstHealthyCreatureIndex() const

{

	if (!VaultSystem)

	{

		return -1;

	}



	return VaultSystem->GetFirstHealthyCreatureIndex();

}



int32 UGF_CreatureManagerSubsystem::GetFirstHealthyCreatureListPosition() const

{

	if (!VaultSystem)

	{

		return 0;

	}



	// Walk the party counting only the members a party UI would actually create a row for.

	// ListPosition therefore tracks the child index, while i tracks the party slot.

	int32 ListPosition = 0;

	for (int32 i = 0; i < VaultSystem->Party.Num(); i++)

	{

		const FGF_CreatureInstanceData& Mon = VaultSystem->Party[i];



		// Eggs occupy a party slot but never get a row, so they advance i and not ListPosition.

		if (Mon.bIsEgg)

		{

			continue;

		}



		if (!Mon.bIsDowned && Mon.CurrentHP > 0)

		{

			return ListPosition;

		}



		ListPosition++;

	}



	UE_LOG(LogTemp, Warning,

		TEXT("GetFirstHealthyCreatureListPosition: no healthy Creature in the party - falling back to row 0."));

	return 0;

}

bool UGF_CreatureManagerSubsystem::GiveCreatureInstance(const FGF_CreatureInstanceData& CreatureData)
{
    // Thin wrapper so there's only one implementation to maintain.
    bool bWentToParty = false;
    int32 VaultPageIndex = -1;
    int32 SlotIndex = -1;
    return GiveCreatureInstanceTracked(CreatureData, bWentToParty, VaultPageIndex, SlotIndex);
}

bool UGF_CreatureManagerSubsystem::GiveCreatureInstanceTracked(const FGF_CreatureInstanceData& CreatureData,
    bool& bWentToParty, int32& OutVaultPageIndex, int32& OutSlotIndex)
{
    bWentToParty = false;
    OutVaultPageIndex = -1;
    OutSlotIndex = -1;

    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GiveCreatureInstanceTracked: VaultSystem is null!"));
        return false;
    }

    // Take a copy so the memo can be stamped on the way past. Everything below this
    // point uses Stamped, not CreatureData.
    FGF_CreatureInstanceData Stamped = CreatureData;

    // Where and when the player got it, for the summary screen's tamer memo.
    // "IfUnset" matters here: this is the function a completed trade calls, and a
    // traded Creature must keep the memo the ORIGINAL tamer gave it.
    UGF_CreatureMemoLibrary::StampMetInfoIfUnset(this, Stamped,
        Stamped.bIsEgg ? EGF_CreatureMetType::EggReceived : EGF_CreatureMetType::Caught);

    // An egg is not a caught Creature — registering it now would spoil what's inside
    // and flag the dex before the player has actually obtained the species. The
    // Compendium entry belongs at hatch time instead.
    const bool bShouldRegisterInDex = !Stamped.bIsEgg;

    auto RegisterIfWanted = [&]()
    {
        if (!bShouldRegisterInDex)
        {
            return;
        }
        if (UGF_CreatureSpeciesData* Species = Stamped.SpeciesData.LoadSynchronous())
        {
            VaultSystem->MarkSpeciesAsCaught(Species->CompendiumNumber);
        }
    };

    // Try party first — note the party is always attempted before any box, which is
    // what makes bWentToParty meaningful.
    const int32 PartySlotBefore = VaultSystem->Party.Num();
    if (VaultSystem->AddToParty(Stamped))
    {
        RegisterIfWanted();
        bHasUnsavedChanges = true;
        bWentToParty = true;
        OutSlotIndex = PartySlotBefore;   // AddToParty appends
        return true;
    }

    // Log why the party add failed so a genuine fault doesn't hide behind "party full"
    if (!VaultSystem->IsPartyFull())
    {
        UE_LOG(LogTemp, Error, TEXT("GiveCreatureInstanceTracked: AddToParty failed on non-full party - IsValid()=%s, SpeciesData.IsNull()=%s"),
            Stamped.IsValid() ? TEXT("true") : TEXT("false"),
            Stamped.SpeciesData.IsNull() ? TEXT("true") : TEXT("false"));
    }

    // Party full (or invalid Creature) — first empty slot across all boxes
    for (int32 VaultPageIdx = 0; VaultPageIdx < VaultSystem->VaultPages.Num(); VaultPageIdx++)
    {
        for (int32 SlotIdx = 0; SlotIdx < UGF_VaultSystem::MaxVaultPageSize; SlotIdx++)
        {
            if (!VaultSystem->VaultPages[VaultPageIdx].Creature[SlotIdx].IsValid())
            {
                if (VaultSystem->InsertCreatureAtSlot(VaultPageIdx, SlotIdx, Stamped))
                {
                    RegisterIfWanted();
                    bHasUnsavedChanges = true;
                    OutVaultPageIndex = VaultPageIdx;
                    OutSlotIndex = SlotIdx;

                    if (Stamped.bIsEgg)
                    {
                        UE_LOG(LogTemp, Warning,
                            TEXT("GiveCreatureInstanceTracked: an EGG went to box %d slot %d. "
                                 "Egg cycles only tick down in the party, so it will not hatch "
                                 "until the player moves it - tell them so."),
                            VaultPageIdx, SlotIdx);
                    }

                    return true;
                }
            }
        }
    }

    UE_LOG(LogTemp, Error, TEXT("GiveCreatureInstanceTracked: Party and all boxes are full!"));
    return false;
}

bool UGF_CreatureManagerSubsystem::GiveCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level,
    const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills)
{
    int32 UnusedVaultPageIndex = -1;
    return GiveCreatureTracked(SpeciesData, Level, StartingSkills, UnusedVaultPageIndex);
}

bool UGF_CreatureManagerSubsystem::GiveCreatureTracked(UGF_CreatureSpeciesData* SpeciesData, int32 Level,
    const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills, int32& OutVaultPageIndex,
    bool bOverrideUnique, bool bUniqueValue)
{
    OutVaultPageIndex = -1;

    if (!SpeciesData)
    {
        UE_LOG(LogTemp, Error, TEXT("GiveCreature: SpeciesData is null!"));
        return false;
    }

    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GiveCreature: VaultSystem is null!"));
        return false;
    }

    // Create new Creature instance
    FGF_CreatureInstanceData NewCreature;

    // ✅ INITIALIZE WITH TAMER INFO
    NewCreature.Initialize(
        SpeciesData,
        Level,
        StartingSkills,
        FName(*VaultSystem->PlayerName),  // <- Pass tamer name
        VaultSystem->PlayerID              // <- Pass tamer ID
    );

    // Initialize() always rolls unique at 1/4096. Scripted gifts that showed the Creature
    // to the player first (starter pick, gift events) already decided this, so stomp the
    // roll with the caller's value. Authoritative both ways — a false here means the
    // player does NOT get a surprise unique the selection screen never displayed.
    if (bOverrideUnique)
    {
        NewCreature.bIsUnique = bUniqueValue;
    }

    // Tamer memo. This path is the scripted-gift one — starters, in-game trades,
    // NPC gifts — so it stamps Gift and the current route. A script that wants
    // different wording (a named person, a place the player isn't standing in)
    // should build the instance itself and call StampMetInfo before handing it over.
    UGF_CreatureMemoLibrary::StampMetInfoIfUnset(this, NewCreature, EGF_CreatureMetType::Gift);

    // Initialize() leaves HP at 0 as a sentinel for actor spawning.
    // Since we're adding directly to the party (no actor spawn), calculate
    // stats now so the Creature is stored with correct MaxHP and full CurrentHP.
    // (Husk's fixed 1 HP is handled inside RecalculateStats.)
    RecalculateStats(NewCreature);
    NewCreature.CurrentHP = NewCreature.MaxHP;

    // Try to add to party first
    if (VaultSystem->AddToParty(NewCreature))
    {
        UE_LOG(LogTemp, Log, TEXT("Gave %s to player (added to party)"),
            *SpeciesData->SpeciesName.ToString());

        // Mark as caught in Compendium
        VaultSystem->MarkSpeciesAsCaughtFromData(SpeciesData);

        bHasUnsavedChanges = true;
        return true;
    }

    // Party full - try to add to first available box slot
    for (int32 VaultPageIdx = 0; VaultPageIdx < VaultSystem->VaultPages.Num(); VaultPageIdx++)
    {
        if (VaultSystem->InsertCreatureAtSlot(VaultPageIdx, 0, NewCreature))
        {
            OutVaultPageIndex = VaultPageIdx;

            UE_LOG(LogTemp, Log, TEXT("Gave %s to player (added to Vault %d) - Party was full"),
                *SpeciesData->SpeciesName.ToString(), VaultPageIdx + 1);

            // Mark as caught in Compendium
            VaultSystem->MarkSpeciesAsCaughtFromData(SpeciesData);

            bHasUnsavedChanges = true;
            return true;
        }
    }

    UE_LOG(LogTemp, Error, TEXT("Failed to give %s - Party and all boxes are full!"),
        *SpeciesData->SpeciesName.ToString());
    return false;
}

bool UGF_CreatureManagerSubsystem::GiveCreatureSimple(UGF_CreatureSpeciesData* SpeciesData)
{
    // Simple wrapper that uses default level 5 and natural moves
    TArray<TSubclassOf<AGF_SkillDefinition>> EmptySkills;
    return GiveCreature(SpeciesData, 5, EmptySkills);
}

bool UGF_CreatureManagerSubsystem::GiveCreatureAtLevel(UGF_CreatureSpeciesData* SpeciesData, int32 Level)
{
    TArray<TSubclassOf<AGF_SkillDefinition>> EmptySkills;
    return GiveCreature(SpeciesData, Level, EmptySkills);
}

bool UGF_CreatureManagerSubsystem::GiveCreatureAtLevelTracked(UGF_CreatureSpeciesData* SpeciesData, int32 Level, int32& OutVaultPageIndex)
{
    TArray<TSubclassOf<AGF_SkillDefinition>> EmptySkills;
    return GiveCreatureTracked(SpeciesData, Level, EmptySkills, OutVaultPageIndex);
}

bool UGF_CreatureManagerSubsystem::GiveCreatureAtLevelUnique(UGF_CreatureSpeciesData* SpeciesData, int32 Level, bool bUnique, int32& OutVaultPageIndex)
{
    TArray<TSubclassOf<AGF_SkillDefinition>> EmptySkills;
    return GiveCreatureTracked(SpeciesData, Level, EmptySkills, OutVaultPageIndex, /*bOverrideUnique=*/true, bUnique);
}



//--------------------

// ACTOR SPAWNING

//--------------------



AGF_Creature* UGF_CreatureManagerSubsystem::SpawnPartyCreature(int32 PartyIndex, FVector SpawnLocation, FRotator SpawnRotation, bool bIsPlayerCreature)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return nullptr;

	}



	FGF_CreatureInstanceData CreatureData;

	if (!VaultSystem->GetPartyCreature(PartyIndex, CreatureData))

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Failed to get party Creature at index %d"), PartyIndex);

		return nullptr;

	}



	AGF_Creature* SpawnedCreature = SpawnCreatureFromData(CreatureData, SpawnLocation, SpawnRotation, bIsPlayerCreature, false);



	// If this is a player Creature, track it as the current battle Creature

	if (SpawnedCreature && bIsPlayerCreature)

	{

		SetCurrentBattleCreature(SpawnedCreature, PartyIndex);

	}



	return SpawnedCreature;

}



AGF_Creature* UGF_CreatureManagerSubsystem::SpawnWildCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level, FVector SpawnLocation, FRotator SpawnRotation)

{

	if (!SpeciesData)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: SpeciesData is null!"));

		return nullptr;

	}



	// Create temporary wild Creature data

	FGF_CreatureInstanceData WildData;

	WildData.Initialize(SpeciesData, Level, TArray<TSubclassOf<AGF_SkillDefinition>>(), NAME_None, 0);



	return SpawnCreatureFromData(WildData, SpawnLocation, SpawnRotation, false, true);

}



AGF_Creature* UGF_CreatureManagerSubsystem::SpawnCreatureFromData(const FGF_CreatureInstanceData& Data, FVector Location, FRotator Rotation, bool bIsPlayer, bool bIsWild)

{

	UWorld* World = GetWorld();

	if (!World)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: World is null!"));

		return nullptr;

	}



	if (!Data.IsValid())

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Creature data is invalid!"));

		return nullptr;

	}



	// Spawn parameters

	FActorSpawnParameters SpawnParams;

	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;



	// Spawn the actor

	AGF_Creature* CreatureActor = World->SpawnActor<AGF_Creature>(CreatureActorClass, Location, Rotation, SpawnParams);



	if (!CreatureActor)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Failed to spawn Creature actor!"));

		return nullptr;

	}



	// Ownership BEFORE initialisation, not after.
	//
	// InitializeFromInstanceData ends by raising OnCreatureInitialized, which is
	// where a Blueprint builds its presentation -- and the first thing that layer
	// wants to know is which side it is on, to face the right way. Setting these
	// afterwards meant every creature read isPlayerCreature as false during the
	// one event that exists to configure it.

	CreatureActor->isPlayerCreature = bIsPlayer;

	CreatureActor->isWildCreature = bIsWild;

	// Initialize from data

	CreatureActor->InitializeFromInstanceData(Data);



	// Track this actor

	ActiveCreatureActors.Add(CreatureActor);



	UE_LOG(LogTemp, Log, TEXT("CreatureManager: Spawned %s at %s"),

		*Data.GetDisplayName().ToString(),

		*Location.ToString());



	return CreatureActor;

}



//--------------------

// SAVE/LOAD

//--------------------



bool UGF_CreatureManagerSubsystem::SaveGame()
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot save, VaultSystem is null!"));
        return false;
    }

	UpdatePlaytime();

    // Save inventory to VaultSystem format
    if (Inventory)
    {
        // Get ItemDataManager for proper name→ID conversion
        UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();

        VaultSystem->SetInventoryFromSaveFormat(Inventory->ToSaveFormat(), DataMgr);
        VaultSystem->PlayerMoney = Inventory->Money;
    }

    // Save current route so it can be restored on load
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UGF_RouteSubsystem* RouteSys = GI->GetSubsystem<UGF_RouteSubsystem>())
        {
            if (UGF_RouteData* CurrentRoute = RouteSys->GetCurrentRoute())
            {
                VaultSystem->SavedRoutePath = FSoftObjectPath(CurrentRoute);
            }
        }
    }

    // Game Corner values must be copied into the save object BEFORE SaveToDisk,
    // or the file is written with last save's balance (one-save-behind bug).
    VaultSystem->GameCornerCoins = GameCornerCoins;
    VaultSystem->MaxGameCornerCoins = MaxGameCornerCoins;
    VaultSystem->GameCornerPayout = GameCornerPayout;
    VaultSystem->GameCornerBetAmount = GameCornerBetAmount;

    bool bSuccess = VaultSystem->SaveToDisk();




    if (bSuccess)
    {
		UGF_QuestSubsystem* QuestSys = GetGameInstance()->GetSubsystem<UGF_QuestSubsystem>();
		if (QuestSys)
		{
			QuestSys->ConfirmTemporaryFlags();

			bool bQuestSaved = QuestSys->SaveStory("QuestSlot", 0);

			if (bQuestSaved)
			{
				UE_LOG(LogTemp, Log, TEXT("Quest data saved to file!"))
			}
		}

		bHasUnsavedChanges = false;
        LastSaveTime = FDateTime::Now();
        UE_LOG(LogTemp, Log, TEXT("CreatureManager: Game saved successfully!"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: Failed to save game!"));
    }


    return bSuccess;
}



void UGF_CreatureManagerSubsystem::ResetToBootState()
{
    // --- Saved state: rebuild empty. LoadGame() repopulates it from disk. ---------
    // Same rebuild DeleteSave() performs, and for the same reason: this must NOT be
    // conditional on a save existing, or an unsaved run carries its party forward.
    VaultSystem = Cast<UGF_VaultSystem>(
        UGameplayStatics::CreateSaveGameObject(UGF_VaultSystem::StaticClass()));

    if (VaultSystem)
    {
        VaultSystem->InitializeVaultPages(14);

        // Options are not part of the save, so re-stamp them onto the fresh object
        // or it reports struct defaults instead of the player's actual settings.
        if (UGF_SettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UGF_SettingsSubsystem>())
        {
            Settings->SyncToVaultSystem();
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: failed to create a fresh VaultSystem!"));
    }

    if (Inventory)
    {
        Inventory->ClearInventory();

        // A new game starts with money, exactly like GameCornerCoins below.
        // This used to be 0: ClearInventory() zeroes it (correct -- it is a clear), and then this
        // line zeroed it again, so every new game began broke. UGF_VaultSystem::PlayerMoney
        // defaults to 3000 but that is only the SAVED copy and is overwritten from the live value,
        // so the default never reached a player. A tester finished the opening with nothing.
        Inventory->Money = UGF_ItemInventorySystem::StartingMoney;
    }

    GameCornerCoins     = 100;
    MaxGameCornerCoins  = 99999;
    GameCornerPayout    = 0;
    GameCornerBetAmount = 0;

    // --- Unsaved runtime state ---------------------------------------------------
    // Actors here belong to the world being torn down. Drop the references without
    // destroying them; the level teardown owns their lifetime.
    ActiveCreatureActors.Empty();
    CurrentBattleCreature = nullptr;
    CurrentBattleCreaturePartyIndex = -1;
    bIsInBattle = false;
    bIsRematchBattle = false;
    bLastBattleWasRematch = false;
    LastCaughtVaultPageIndex = -1;

    // A queued move-learn would otherwise pop up in the next run against a party
    // index that no longer means anything.
    PendingSkillLearnQueue.Empty();
    bSkillLearnQueueEmptyAnnounced = false;
    PendingSkillToLearn = nullptr;
    PendingSkillLearnPartyIndex = -1;

    PendingPostBattleCutscene = NAME_None;
    PendingPostBattleCutsceneActorTag = NAME_None;

    bHasUnsavedChanges = false;
    bSaveDataLoaded    = false;

    // Playtime is measured from here, so a reset run does not inherit the old clock.
    SessionStartTime = FDateTime::Now();
    LastSaveTime     = FDateTime();

    // The species index, LoadedSpeciesByName and CachedAllSpecies are DELIBERATELY
    // kept. They cache asset data, not player state, and rebuilding costs a full
    // asset registry scan (plus re-loading whatever was already resident).

    UE_LOG(LogTemp, Log, TEXT("GameReset: CreatureManagerSubsystem reset to boot state."));
}

bool UGF_CreatureManagerSubsystem::LoadGame()
{
    bool bSuccess = false;
    UGF_VaultSystem* LoadedVaultSystem = UGF_VaultSystem::LoadFromDisk(bSuccess);

	UGF_QuestSubsystem* QuestSys = GetGameInstance()->GetSubsystem<UGF_QuestSubsystem>();
		if (QuestSys)
		{
			const bool bQuestLoaded = QuestSys->LoadStory("QuestSlot", 0, true);

			// This return value used to be discarded, so a story that failed to load
			// looked identical to one that loaded fine - and after a soft reset, which
			// empties the flags first, that meant resuming with none of them and no
			// indication anywhere. Never swallow it again.
			if (bQuestLoaded)
			{
				UE_LOG(LogTemp, Warning, TEXT("Loaded Quest Save slot 0 - %d story flag(s)."), QuestSys->GetFlagCount());
			}
			else if (QuestSys->DidStoryLoadFail())
			{
				UE_LOG(LogTemp, Error,
					TEXT("LoadGame: THE STORY DID NOT LOAD. The player has a quest save on disk that "
					     "will not read. They are about to play with %d flag(s) - every story gate is "
					     "open. Saving is blocked so the file is not overwritten."),
					QuestSys->GetFlagCount());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("No quest save present - starting with an empty story."));
			}
		}

		// Berry trees travel with the save for the same reason the quest flags do: a
		// soft reset or a Continue must not hand the player freshly ripe trees.
		if (UGF_BerryGrowthSubsystem* BerrySys = GetGameInstance()->GetSubsystem<UGF_BerryGrowthSubsystem>())
		{
			BerrySys->LoadBerries();
		}

    if (bSuccess && LoadedVaultSystem)
    {
        VaultSystem = LoadedVaultSystem;

        // Saves written before EvolveCreature() re-resolved traits carry evolved
        // Creature that kept their pre-evolution trait. Repair them once, before
        // anything reads the party. Passing true persists the correction immediately
        // so a tester who quits without saving doesn't lose it; the flag itself
        // rides out on the next normal save when nothing needed fixing.
        if (!VaultSystem->bTraitsRepaired)
        {
            // Set first: RepairAllTraits() may write the save, and that write has
            // to carry the flag or the sweep runs again on the next load.
            VaultSystem->bTraitsRepaired = true;
            VaultSystem->RepairAllTraits(/*bSaveIfChanged=*/true);
        }

        // Same class of bug, different field: EvolveCreature() never registered the new
        // species in the dex, so an evolved Creature sat in the party with a blank entry.
        // Unlike the trait repair this needs no flag and no save — see the function.
        BackfillCompendiumFromOwnedCreature();

        // And Husks that were saved with a real HP stat before the fix landed.
        RepairFixedHPCreature();

        // A real save is now live, so HasSaveData() must say so. Before this line it was
        // only ever set in Initialize(), which runs once per process - fine back when
        // nothing cleared it, but ResetToBootState() does. Leaving it false made
        // the player pawn Blueprint take its "brand new game" branch and skip restoring the player's
        // saved location and facing, so every soft reset dumped the player at spawn.
        bSaveDataLoaded = true;

        // The loaded save carries whatever settings were mirrored into it when it
        // was written. Those are stale — the options subsystem owns them — so
        // stamp the live values back over the fresh VaultSystem immediately.
        if (UGF_SettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UGF_SettingsSubsystem>())
        {
            Settings->SyncToVaultSystem();
        }

        UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();





        if (DataMgr)
        {
            TMap<uint8, TMap<FName, int32>> SavedInventory = VaultSystem->GetInventorySaveFormat();
            Inventory->FromSaveFormat(SavedInventory, DataMgr);
            Inventory->Money = VaultSystem->PlayerMoney;

			//Game Corner stuff
			GameCornerCoins = VaultSystem->GameCornerCoins;
			MaxGameCornerCoins = VaultSystem->MaxGameCornerCoins;
			GameCornerPayout = VaultSystem->GameCornerPayout;
			GameCornerBetAmount = VaultSystem->GameCornerBetAmount;

            // Restore the saved route (suppress music — the overworld will play music when ready)
            if (VaultSystem->SavedRoutePath.IsValid())
            {
                if (UGF_RouteSubsystem* RouteSys = GetGameInstance()->GetSubsystem<UGF_RouteSubsystem>())
                {
                    UGF_RouteData* SavedRoute = Cast<UGF_RouteData>(VaultSystem->SavedRoutePath.TryLoad());
                    if (SavedRoute)
                    {
                        RouteSys->SetMusicSuppressed(true);
                        RouteSys->SetCurrentRoute(SavedRoute);
                        RouteSys->SetMusicSuppressed(false);
                        UE_LOG(LogTemp, Log, TEXT("Game loaded: restored route '%s'"),
                            *SavedRoute->RouteName.ToString());
                    }
                }
            }

            UE_LOG(LogTemp, Log, TEXT("Game loaded successfully with %d items"),
                Inventory->GetTotalUniqueItems());

            return true;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Could not get ItemDataManager for loading!"));
            return false;
        }
    }
    else
    {
        // A failed load used to return here with VaultSystem untouched, which meant the
        // PREVIOUS run's party, boxes, bag and Compendium stayed live. Same shape as the
        // old DeleteSave bug: "nothing to load" is a reset, not a no-op. Rebuild empty
        // so a failure is visibly empty rather than silently someone else's save.
        UE_LOG(LogTemp, Warning, TEXT("No save file found or load failed - resetting to boot state "
            "rather than leaving the previous run's data live."));
        ResetToBootState();
        return false;
    }



}



bool UGF_CreatureManagerSubsystem::DoesSaveExist() const

{

	return UGF_VaultSystem::DoesSaveExist();

}



bool UGF_CreatureManagerSubsystem::DeleteSave()

{

	// bSuccess reports whether a file was actually deleted -- it is false when no
	// save existed.  The in-memory rebuild below must NOT be gated on it: New Game
	// after an unsaved run finds no file, and skipping the rebuild left the previous
	// run's party and boxes live for the next playthrough.
	const bool bSuccess = UGF_VaultSystem::DeleteSave();

	UGameplayStatics::DeleteGameInSlot("QuestSlot", 0);

	// Berry trees are part of the run, not the machine: New Game must start with
	// every tree ripe again.
	UGF_BerryGrowthSubsystem::DeleteBerrySave();



	// Create new empty box system

	VaultSystem = Cast<UGF_VaultSystem>(

		UGameplayStatics::CreateSaveGameObject(UGF_VaultSystem::StaticClass())

	);



	if (VaultSystem)

	{

		VaultSystem->InitializeVaultPages(14);

		// Options are not part of the deleted save — re-stamp them so the
		// fresh VaultSystem does not report struct defaults.

		if (UGF_SettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UGF_SettingsSubsystem>())

		{

			Settings->SyncToVaultSystem();

		}

	}



	return bSuccess;

}



void UGF_CreatureManagerSubsystem::CreateNewSave(const FString& PlayerName)

{

	VaultSystem = Cast<UGF_VaultSystem>(

		UGameplayStatics::CreateSaveGameObject(UGF_VaultSystem::StaticClass())

	);



	if (VaultSystem)

	{

		VaultSystem->InitializeVaultPages(14);

		VaultSystem->PlayerName = PlayerName;

		VaultSystem->PlayerID = FMath::RandRange(0, 999999);



		// A brand-new VaultSystem starts on struct defaults — stamp the player's
		// actual options onto it before the first write so the mirror is correct.

		if (UGF_SettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UGF_SettingsSubsystem>())

		{

			Settings->SyncToVaultSystem();

		}



		SaveGame();



		UE_LOG(LogTemp, Log, TEXT("CreatureManager: Created new save for %s"), *PlayerName);

	}

}



//--------------------

// ACTOR STATE MANAGEMENT

//--------------------



bool UGF_CreatureManagerSubsystem::UpdatePartyFromActor(AGF_Creature* CreatureActor, int32 PartyIndex)

{

	if (!CreatureActor || !IsValid(CreatureActor))

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: CreatureActor is null or invalid!"));

		return false;

	}



	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	// Export actor state to data

	FGF_CreatureInstanceData UpdatedData = CreatureActor->ExportToInstanceData();



	// Merge into the box system -- do NOT overwrite.
	//
	// This used to call UpdatePartyCreature(), which replaces the whole party entry with
	// whatever the battle actor holds. The actor is NOT updated during battle: GiveEXP()
	// writes level-ups, EXP and newly learned moves straight into VaultSystem and leaves the
	// actor sitting on its battle-start values. So switching a Creature out exported that
	// stale actor over the fresh record and silently rolled back everything it had just
	// earned -- a level 6 Sprigling went back to 5, its EXP dropped to the pre-battle figure,
	// and the move it learned that same turn was gone. Sending it back in respawned the
	// Creature the player started the battle with.
	//
	// UpdatePartyCreatureBattleData() is the same write the END of a battle already uses, and
	// it exists precisely because of this. It takes the battle-volatile fields (HP, status)
	// and preserves Level, EXP, Uses and Skills. Both paths write the party from a battle
	// actor, so both have to merge -- hardening only one of them left this door open.

	bool bSuccess = VaultSystem->UpdatePartyCreatureBattleData(PartyIndex, UpdatedData);



	if (bSuccess)

	{

		UE_LOG(LogTemp, Log, TEXT("CreatureManager: Saved %s to party slot %d (HP: %.0f/%.0f)"),

			*UpdatedData.GetDisplayName().ToString(),

			PartyIndex,

			UpdatedData.CurrentHP,

			UpdatedData.MaxHP);

	}



	return bSuccess;

}





//--------------------

// BOX MANAGEMENT

//--------------------



int32 UGF_CreatureManagerSubsystem::GetVaultPageCount() const

{

	return VaultSystem ? VaultSystem->VaultPages.Num() : 0;

}



int32 UGF_CreatureManagerSubsystem::GetVaultCreatureCount(int32 VaultPageIndex) const

{

	return VaultSystem ? VaultSystem->GetVaultCreatureCount(VaultPageIndex) : 0;

}



bool UGF_CreatureManagerSubsystem::GetVaultCreatureData(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->GetVaultCreature(VaultPageIndex, SlotIndex, OutData);

}



bool UGF_CreatureManagerSubsystem::MovePartyToVault(int32 PartyIndex, int32 VaultPageIndex)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->MovePartyToVault(PartyIndex, VaultPageIndex);

}



bool UGF_CreatureManagerSubsystem::MoveVaultToParty(int32 VaultPageIndex, int32 SlotIndex)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	return VaultSystem->MoveVaultToParty(VaultPageIndex, SlotIndex);

}



//--------------------

// HEALING

//--------------------



void UGF_CreatureManagerSubsystem::HealParty()

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return;

	}



	for (int32 i = 0; i < VaultSystem->GetPartySize(); i++)

	{

		FGF_CreatureInstanceData Data;

		if (VaultSystem->GetPartyCreature(i, Data))

		{

			// Heal HP

			Data.CurrentHP = Data.MaxHP;

			Data.bIsDowned = false;



			// Remove status

			Data.StatusCondition = EGF_STATUSEffect::None;

			Data.SleepCounter = 0;



			// Restore Uses

			for (int32 j = 0; j < Data.CurrentUses.Num(); j++)

			{

				Data.CurrentUses[j] = Data.MaxUses[j];

			}



			// A Creature Center heal must not turn an egg into a battle-ready party member.

			Data.NormalizeEggState();



			VaultSystem->UpdatePartyCreature(i, Data);

		}

	}



	MarkDirty();

	UE_LOG(LogTemp, Warning, TEXT("CreatureManager: Party fully healed!"));

}



bool UGF_CreatureManagerSubsystem::HealCreature(int32 PartyIndex)

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));

		return false;

	}



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		return false;

	}



	// Heal

	Data.CurrentHP = Data.MaxHP;

	Data.bIsDowned = false;

	Data.StatusCondition = EGF_STATUSEffect::None;

	Data.SleepCounter = 0;



	for (int32 j = 0; j < Data.CurrentUses.Num(); j++)

	{

		Data.CurrentUses[j] = Data.MaxUses[j];

	}



	// Healing an egg (a full heal walks the whole party) must not clear its egg state.

	Data.NormalizeEggState();



	bool bSuccess = VaultSystem->UpdatePartyCreature(PartyIndex, Data);



	if (bSuccess)

	{

		MarkDirty();

		UE_LOG(LogTemp, Log, TEXT("CreatureManager: Healed %s"), *Data.GetDisplayName().ToString());

	}



	return bSuccess;

}



//--------------------

// DEBUGGING

//--------------------



void UGF_CreatureManagerSubsystem::DebugPrintParty() const

{

	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Warning, TEXT("CreatureManager: VaultSystem is null!"));

		return;

	}





}



FString UGF_CreatureManagerSubsystem::GetPlayerName() const

{

	return VaultSystem ? VaultSystem->PlayerName : TEXT("Unknown");

}



void UGF_CreatureManagerSubsystem::SetPlayerName(const FString& NewName)

{

	if (VaultSystem)

	{

		VaultSystem->PlayerName = NewName;

	}

}



//////////////////

///MOVES

//////////////////



TArray<TSoftClassPtr<AGF_SkillDefinition>> UGF_CreatureManagerSubsystem::CheckLevelUpSkills(int32 PartyIndex, int32 NewLevel)

{

    TArray<TSoftClassPtr<AGF_SkillDefinition>> LearnableSkills;



    if (!VaultSystem)

    {

        return LearnableSkills;

    }



    FGF_CreatureInstanceData Data;

    if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

    {

        return LearnableSkills;

    }



    // Scan EVERY level gained, not just the final one.

    //

    // A single battle can grant more than one level, and each level has its own learnset

    // entry. Asking only about NewLevel drops the moves from every level skipped over --

    // Sprigling going 5 -> 7 off a shared Skyrend kill was asked about 7, so Siphon at 6

    // vanished without ever being offered and the player was never told. Falls back to the

    // single level when no start was recorded, which is the old behaviour.

    const int32* StartLevel = LevelBeforeEXPByPartyIndex.Find(PartyIndex);

    const int32 FirstLevelToCheck = StartLevel ? (*StartLevel + 1) : NewLevel;



    for (int32 Level = FirstLevelToCheck; Level <= NewLevel; ++Level)

    {

        for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : Data.GetSkillsToLearnAtLevel(Level))

        {

            // A move can sit at more than one level; only offer it once.

            LearnableSkills.AddUnique(Skill);

        }

    }



    if (StartLevel && FirstLevelToCheck < NewLevel)

    {

        UE_LOG(LogTemp, Warning,

            TEXT("%s gained %d levels (%d -> %d) - scanned every level for new moves."),

            *Data.GetDisplayName().ToString(), NewLevel - *StartLevel, *StartLevel, NewLevel);

    }



    // Consumed: this record describes the battle just resolved.

    LevelBeforeEXPByPartyIndex.Remove(PartyIndex);



    if (LearnableSkills.Num() > 0)

    {

        UE_LOG(LogTemp, Warning, TEXT("%s can learn %d new moves at level %d!"),

            *Data.GetDisplayName().ToString(), LearnableSkills.Num(), NewLevel);

    }



    return LearnableSkills;

}



void UGF_CreatureManagerSubsystem::TryLearnLevelUpSkills(int32 PartyIndex, int32 NewLevel)
{
    TArray<TSoftClassPtr<AGF_SkillDefinition>> SkillsToLearn = CheckLevelUpSkills(PartyIndex, NewLevel);
    if (SkillsToLearn.Num() == 0) return;

    FGF_CreatureInstanceData Data;
    if (!VaultSystem || !VaultSystem->GetPartyCreature(PartyIndex, Data)) return;

    for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : SkillsToLearn)
    {
        FGF_SkillLearnQueueEntry Entry;
        Entry.PartyIndex = PartyIndex;
        Entry.Skill = Skill;

        if (Data.CanLearnMoreSkills())
        {
            // Slot available — learn now, but queue for dialogue display
            // so "X learned Y!" shows before the battle continues
            Data.LearnSkill(Skill);
            UpdatePartyCreatureData(PartyIndex, Data);
            MarkDirty();
            Entry.bAlreadyLearned = true;
        }
        else
        {
            // Full — needs forget-a-move UI before the move is saved
            Entry.bAlreadyLearned = false;
        }

        PendingSkillLearnQueue.Add(Entry);

        // New work: this drain gets to announce its own completion.
        bSkillLearnQueueEmptyAnnounced = false;
        bSkillLearnedThisDrain         = false;
    }

    // Do NOT auto-fire OnSkillLearnRequired here.
    // The caller must invoke ProcessNextSkillLearn() after all TryLearnLevelUpSkills calls
    // finish. This prevents the forget-a-move delegate from firing simultaneously with
    // OnSkillLearned, which would overwrite dialogue variables and skip Creature that
    // need the forget-a-move UI.
}

bool UGF_CreatureManagerSubsystem::ConsumeSkillLearnedNotification()
{
    const bool bWasOurs = bSkillLearnedNotificationPending;
    bSkillLearnedNotificationPending = false;

    if (!bWasOurs)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("SkillLearn: an OnSkillLearned handler ran that this subsystem did not trigger - suppressed."));
    }

    return bWasOurs;
}

void UGF_CreatureManagerSubsystem::ResolveSkillLearn(int32 PartyIndex, int32 ForgetIndex)
{
    UE_LOG(LogTemp, Warning, TEXT("SkillLearn: ResolveSkillLearn(Party=%d, Forget=%d) -> %s"),
        PartyIndex, ForgetIndex,
        (ForgetIndex >= 0 && !PendingSkillToLearn.IsNull()) ? TEXT("WILL TEACH") : TEXT("skip"));

    if (!VaultSystem) return;

    TSoftClassPtr<AGF_SkillDefinition> LearnedSkill = PendingSkillToLearn;

    if (ForgetIndex >= 0 && !PendingSkillToLearn.IsNull())
    {
        // Player chose to forget a move — replace it with the new one
        if (TeachSkill(PartyIndex, PendingSkillToLearn, true, ForgetIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("SkillLearn: taught via Resolve -> broadcasting OnSkillLearned"));
            // Deliberately does NOT set bSkillLearnedNotificationPending.
            //
            // This is the replace path, and W_GF_MoveSlot already shows Creature_DecideOnNewSkill
            // ("1.. 2.. 3.. Poof! X forgot A and learned B!") the moment the player picks a move.
            // Letting the generic "X learned B!" line through as well plays two messages for one
            // event and pushes the tamer-defeat dialogue behind both.
            //
            // Only the auto-learn path below sets the token, because that is the one with no
            // message of its own. Listeners that want the event regardless still get the
            // broadcast -- the token only governs the dialogue.
            OnSkillLearned.Broadcast(PartyIndex, LearnedSkill);
        }
    }
    // ForgetIndex == -1: player skipped this move, nothing to learn

    PendingSkillToLearn = nullptr;
    PendingSkillLearnPartyIndex = -1;
    // Queue is NOT auto-popped here. The HUD calls ProcessNextSkillLearn()
    // when it is ready — after showing "learned X!" dialogue or on Don't Learn.
}

void UGF_CreatureManagerSubsystem::AnnounceSkillLearnQueueEmptyWhenIdle()
{
    SkillLearnDeferElapsed += 0.05f;

    const UGF_DialogueSubsystem* Dialogue = GetGameInstance()
        ? GetGameInstance()->GetSubsystem<UGF_DialogueSubsystem>()
        : nullptr;

    const bool bDialogueUp = Dialogue && Dialogue->IsDialogueActive();

    if (bDialogueUp)
    {
        bDeferSawLearnDialogue = true;
    }

    // Announce when the line has been shown and dismissed, or bail out after a couple of
    // seconds. The timeout matters: if Blueprint ever stops showing that dialogue, waiting
    // forever would strand the player at the end of a battle -- a worse bug than the one
    // this fixes.
    const bool bLineFinished = bDeferSawLearnDialogue && !bDialogueUp;

    // The timeout guards ONE case: the line never appears at all. Once it is on screen the
    // player decides when it closes, and there is no time limit on reading a message box.
    // Capping both cases released the battle-end sequence while "Sprigling learned Siphon!" was
    // still up, and the teardown cut the tamer-defeat line off behind it.
    // 1s, not 2s. This cap only covers "the line never appears", and Blueprint takes about
    // 600ms to open it. The case that actually burns the cap is the line having already been
    // shown AND closed before the queue reported empty -- there is no signal that distinguishes
    // that from "not opened yet", so the wait is spent either way and the player sees it as a
    // stall before the opponent sends out.
    const bool bWaitedTooLong = !bDeferSawLearnDialogue && SkillLearnDeferElapsed >= 1.0f;

    if (!bLineFinished && !bWaitedTooLong)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SkillLearnQueueEmptyDeferHandle);
    }

    bSkillLearnedThisDrain = false;

    UE_LOG(LogTemp, Warning,
        TEXT("SkillLearn: queue empty -> broadcasting OnSkillLearnQueueEmpty (%s)"),
        bLineFinished ? TEXT("learned-move line finished") : TEXT("timed out waiting for it"));

    OnSkillLearnQueueEmpty.Broadcast();
}

void UGF_CreatureManagerSubsystem::ProcessNextSkillLearn()
{
    if (PendingSkillLearnQueue.Num() > 0)
    {
        FGF_SkillLearnQueueEntry Next = PendingSkillLearnQueue[0];
        PendingSkillLearnQueue.RemoveAt(0);
        PendingSkillLearnPartyIndex = Next.PartyIndex;
        PendingSkillToLearn         = Next.Skill;

        UE_LOG(LogTemp, Warning,
            TEXT("SkillLearn: ProcessNext popped Party=%d AlreadyLearned=%s (%d still queued)"),
            Next.PartyIndex, Next.bAlreadyLearned ? TEXT("YES") : TEXT("no"), PendingSkillLearnQueue.Num());

        if (Next.bAlreadyLearned)
        {
            // Skill already saved — just show "X learned Y!" dialogue.
            // Blueprint must call ProcessNextSkillLearn() when the dialogue finishes.
            // See ResolveSkillLearn: the token must not outlive its own broadcast.
            bSkillLearnedNotificationPending = true;
            OnSkillLearned.Broadcast(Next.PartyIndex, Next.Skill);
            bSkillLearnedNotificationPending = false;

            // A "X learned Y!" line is now on its way from Blueprint. The queue-empty
            // announcement must wait for it -- see AnnounceSkillLearnQueueEmptyWhenIdle().
            bSkillLearnedThisDrain = true;

            // Nothing is pending on this path: the move is already on the Creature and no forget
            // choice will ever be made, so ResolveSkillLearn() -- the only other place that clears
            // these -- is never called.
            //
            // Leaving them set makes HasPendingSkillLearn() answer "yes" forever. Anything gating on
            // it then waits for a decision that cannot come: the battle-end sequence checks it
            // before continuing, so a Creature with a free move slot levelling up at the end of a
            // battle froze the game outright.
            PendingSkillToLearn         = nullptr;
            PendingSkillLearnPartyIndex = -1;
        }
        else
        {
            // Creature has 4 moves — show forget-a-move UI.
            // Blueprint calls ResolveSkillLearn() when player decides, which fires OnSkillLearned,
            // then Blueprint calls ProcessNextSkillLearn() when that dialogue finishes.
            OnSkillLearnRequired.Broadcast(Next.PartyIndex, Next.Skill);
        }
    }
    else
    {
        // All moves shown — safe to continue battle-end sequence
        // Announce ONCE per drain. See bSkillLearnQueueEmptyAnnounced -- listeners continue the
        // battle-end sequence on this, so a repeat tears down a dialogue that is already running.
        // A forget-a-move decision still outstanding is NOT an empty drain.
        //
        // The queue can be empty while PendingSkillToLearn is still set: ProcessNextSkillLearn
        // pops the entry, hands it to the forget UI, and waits for ResolveSkillLearn. Listeners
        // already know to ignore the announcement in that state -- the battle HUD gates on
        // HasPendingSkillLearn and does nothing -- but announcing anyway BURNED the one-shot
        // latch, so the real announcement after the player answered was suppressed and the
        // opponent never sent out. Answering "Don't Learn" hung the battle.
        //
        // Do not announce and do not latch: resolving the decision calls back in here.
        if (HasPendingSkillLearn())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("SkillLearn: queue empty but a forget-a-move decision is still pending - not announcing yet."));
            return;
        }

        if (bSkillLearnQueueEmptyAnnounced)
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("SkillLearn: queue already announced empty this drain - suppressing repeat broadcast."));
            return;
        }

        bSkillLearnQueueEmptyAnnounced = true;

        if (bSkillLearnedThisDrain)
        {
            // Hold the announcement until the learn dialogue has played. Latched above, so the
            // repeat calls that arrive in the meantime do not start a second wait.
            bDeferSawLearnDialogue = false;
            SkillLearnDeferElapsed  = 0.0f;

            if (UWorld* World = GetWorld())
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("SkillLearn: queue empty, but a learned-move line is pending - waiting for it before announcing."));

                World->GetTimerManager().SetTimer(
                    SkillLearnQueueEmptyDeferHandle,
                    FTimerDelegate::CreateUObject(this, &UGF_CreatureManagerSubsystem::AnnounceSkillLearnQueueEmptyWhenIdle),
                    0.05f, true);
                return;
            }
            // No world to time against: fall through and announce now rather than stall.
        }

        UE_LOG(LogTemp, Warning, TEXT("SkillLearn: queue empty -> broadcasting OnSkillLearnQueueEmpty"));
        OnSkillLearnQueueEmpty.Broadcast();
    }
}

bool UGF_CreatureManagerSubsystem::TeachSkill(int32 PartyIndex, TSoftClassPtr<AGF_SkillDefinition> Skill, bool bReplaceSkill, int32 ReplaceIndex)

{

    if (!VaultSystem)

    {

        return false;

    }



    FGF_CreatureInstanceData Data;

    if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

    {

        return false;

    }



    bool bSuccess = Data.LearnSkill(Skill, bReplaceSkill, ReplaceIndex);



    if (bSuccess)

    {

        VaultSystem->UpdatePartyCreature(PartyIndex, Data);

        MarkDirty();

    }



    return bSuccess;

}



FGF_CreatureInstanceData UGF_CreatureManagerSubsystem::CreateCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level)

{

	FGF_CreatureInstanceData NewCreature;

	NewCreature.Initialize(SpeciesData, Level, TArray<TSubclassOf<AGF_SkillDefinition>>(), FName(*VaultSystem->PlayerName), VaultSystem->PlayerID);

	return NewCreature;

}



FGF_CreatureInstanceData UGF_CreatureManagerSubsystem::CreateCreatureWithSkills(UGF_CreatureSpeciesData* SpeciesData, int32 Level, const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills)

{

	FGF_CreatureInstanceData NewCreature;

	NewCreature.Initialize(SpeciesData, Level, StartingSkills, FName(*VaultSystem->PlayerName), VaultSystem->PlayerID);

	return NewCreature;

}



TArray<TSubclassOf<AGF_SkillDefinition>> UGF_CreatureManagerSubsystem::GetPartyCreatureLoadedSkills(int32 PartyIndex)

{

	TArray<TSubclassOf<AGF_SkillDefinition>> LoadedSkills;



	if (!VaultSystem)

	{

		return LoadedSkills;

	}



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		return LoadedSkills;

	}



	// Load all soft references and convert to hard references

	for (const TSoftClassPtr<AGF_SkillDefinition>& SoftMove : Data.Skills)

	{

		if (SoftMove.IsValid())

		{

			// Load synchronously and convert to hard reference

			TSubclassOf<AGF_SkillDefinition> LoadedSkill = SoftMove.LoadSynchronous();

			if (LoadedSkill)

			{

				LoadedSkills.Add(LoadedSkill);

			}

		}

	}



	return LoadedSkills;

}



TArray<FGF_SkillEntry> UGF_CreatureManagerSubsystem::GetPartyCreatureSkillsWithUses(int32 PartyIndex)

{

	TArray<FGF_SkillEntry> SkillsWithUses;



		// Debug: Check subsystem

	UE_LOG(LogTemp, Warning, TEXT("=== GetPartyCreatureSkillsWithUses called for index %d ==="), PartyIndex);



	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("VaultSystem is NULL!"));

		return SkillsWithUses;

	}



	UE_LOG(LogTemp, Warning, TEXT("VaultSystem exists. Party size: %d"), VaultSystem->GetPartySize());



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		UE_LOG(LogTemp, Error, TEXT("Failed to get party Creature at index %d"), PartyIndex);

		return SkillsWithUses;

	}



	UE_LOG(LogTemp, Warning, TEXT("Got Creature data: %s (Level %d)"),

		*Data.GetDisplayName().ToString(), Data.Level);

	UE_LOG(LogTemp, Warning, TEXT("   Skills count: %d"), Data.Skills.Num());



	// An egg has no moves, so a battle asking for them means the caller is holding the
	// wrong party index — typically slot 0 rather than the Creature that was actually sent
	// out. The battle HUD should read Get Current Battle Creature Party Index, which
	// SpawnPartyCreature and SwitchBattleCreature both keep up to date.

	if (Data.bIsEgg)

	{

		UE_LOG(LogTemp, Warning,

			TEXT("Party index %d is an EGG - it has no moves. If this came from a battle, "

				 "the move list is being asked for the wrong slot (active Creature is index %d)."),

			PartyIndex, GetCurrentBattleCreaturePartyIndex());

	}



	// Load all soft references and combine with Uses data

	for (int32 i = 0; i < Data.Skills.Num(); i++)

	{

		UE_LOG(LogTemp, Warning, TEXT("   Processing move %d..."), i);



		if (Data.Skills[i].IsNull())

		{

			UE_LOG(LogTemp, Warning, TEXT("Skill %d: Soft reference is invalid"), i);

			continue;

		}



		FGF_SkillEntry SkillData;



		// Load the move class

		UE_LOG(LogTemp, Warning, TEXT("   Loading move %d..."), i);

		SkillData.SkillClass = Data.Skills[i].LoadSynchronous();



		if (!SkillData.SkillClass)

		{

			UE_LOG(LogTemp, Error, TEXT("Skill %d: Failed to load!"), i);

			continue;

		}



		UE_LOG(LogTemp, Warning, TEXT("Skill %d loaded successfully"), i);



		// Get Uses data (with bounds checking)

		SkillData.CurrentUses = Data.CurrentUses.IsValidIndex(i) ? Data.CurrentUses[i] : 10;

		SkillData.MaxUses = Data.MaxUses.IsValidIndex(i) ? Data.MaxUses[i] : 10;



		UE_LOG(LogTemp, Warning, TEXT("   Uses: %d/%d"), SkillData.CurrentUses, SkillData.MaxUses);



		SkillsWithUses.Add(SkillData);

	}



	UE_LOG(LogTemp, Warning, TEXT("=== Returning %d moves ==="), SkillsWithUses.Num());

	return SkillsWithUses;

}



namespace
{
	/**
	 * Copies the party entry's Uses onto the live battle actor for the same Creature.
	 *
	 * The party entry is the ONLY source of truth for Uses. The battle actor carries its
	 * own SkillUses copy because the battle code reads it for "can this move still be used"
	 * (IsSkillUsable / LastResort), and that copy drifting away from the party's is what
	 * corrupted Uses across battles. Pushing after every write keeps the two identical.
	 *
	 * Slots are matched by MOVE CLASS, not by index: InitializeFromInstanceData skips a
	 * move that fails to load, so the actor's array can be compacted relative to the party's.
	 */
	void PushUsesToBattleActors(const TArray<AGF_Creature*>& Actors, const FGF_CreatureInstanceData& Data)
	{
		for (AGF_Creature* Actor : Actors)
		{
			if (!IsValid(Actor) || !Actor->isPlayerCreature || Actor->CreatureID != Data.CreatureID)
			{
				continue;
			}

			for (int32 PartySlot = 0; PartySlot < Data.Skills.Num(); PartySlot++)
			{
				if (Data.Skills[PartySlot].IsNull() || !Data.CurrentUses.IsValidIndex(PartySlot))
				{
					continue;
				}

				// Match on the LOADED CLASS, not on path strings.
				//
				// Comparing FSoftObjectPath(Actor->Skills[i].Get()) against
				// Data.Skills[i].ToSoftObjectPath() looks equivalent and is not: a TSoftClassPtr
				// path and a UClass's own path do not always agree on the "_C" suffix, and when
				// they disagree this loop matches nothing and pushes nothing -- silently. The
				// actor's Uses then stops tracking the party's and the two drift apart.
				//
				// That shipped: a Minnowling read 1/40 in the menu (party data) while its battle
				// actor was already at 0, so every click was correctly judged "no usable move"
				// and became LastResort. The player watched their turn evaporate with Uses on screen.
				UClass* PartySkillClass = Data.Skills[PartySlot].LoadSynchronous();
				if (!PartySkillClass)
				{
					continue;
				}

				for (int32 ActorSlot = 0; ActorSlot < Actor->Skills.Num(); ActorSlot++)
				{
					if (Actor->Skills[ActorSlot] != PartySkillClass)
					{
						continue;
					}

					if (Actor->SkillUses.IsValidIndex(ActorSlot))
					{
						Actor->SkillUses[ActorSlot].CurrentUses = Data.CurrentUses[PartySlot];

						if (Data.MaxUses.IsValidIndex(PartySlot) && Data.MaxUses[PartySlot] > 0)
						{
							Actor->SkillUses[ActorSlot].MaxUses = Data.MaxUses[PartySlot];
						}
					}

					break;
				}
			}
		}
	}
}

bool UGF_CreatureManagerSubsystem::UseSkill(int32 PartyIndex, int32 SkillIndex)

{

	if (!VaultSystem)

	{

		return false;

	}



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		return false;

	}



	// An egg never takes a turn, so the caller is holding a stale index — typically party
	// slot 0 because the lead was assumed to be the one sent out. Recovering here rather
	// than failing is what keeps the battle alive: an egg has no moves, so every attack
	// would be rejected, the turn would never resolve, and the player would be stuck in a
	// battle they cannot act in or run from.
	if (Data.bIsEgg)
	{
		const int32 ActiveIndex = GetCurrentBattleCreaturePartyIndex();

		FGF_CreatureInstanceData ActiveData;
		const bool bCanRecover = (ActiveIndex != PartyIndex)
			&& VaultSystem->GetPartyCreature(ActiveIndex, ActiveData)
			&& !ActiveData.bIsEgg;

		UE_LOG(LogTemp, Error,
			TEXT("UseSkill: party index %d is an EGG. %s"),
			PartyIndex,
			bCanRecover
				? *FString::Printf(TEXT("Using the active battle Creature at index %d instead."), ActiveIndex)
				: TEXT("No valid active Creature to fall back on - the move is dropped."));

#if DO_BLUEPRINT_GUARD
		// Names the Blueprint function and node that passed the bad index.
		UE_LOG(LogTemp, Error, TEXT("  Blueprint callstack:\n%s"), *FFrame::GetScriptCallstack());
#endif

		if (!bCanRecover)
		{
			return false;
		}

		PartyIndex = ActiveIndex;
		Data = ActiveData;
	}

	// Check if move index is valid
	if (!Data.Skills.IsValidIndex(SkillIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("UseSkill: SkillIndex %d out of range (Skills.Num=%d)"), SkillIndex, Data.Skills.Num());
		return false;
	}

	// Uses array can fall out of sync if moves were added after initialization — auto-repair it
	while (Data.CurrentUses.Num() < Data.Skills.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("UseSkill: Uses array out of sync, padding entry for move index %d"), Data.CurrentUses.Num());
		Data.CurrentUses.Add(35);
		Data.MaxUses.Add(35);
	}

	// Check if has Uses remaining
	if (Data.CurrentUses[SkillIndex] <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UseSkill: Skill has no Uses remaining! (SkillIndex=%d)"), SkillIndex);
		return false;
	}



	// Decrease Uses

	Data.CurrentUses[SkillIndex]--;



	// Update the party data

	VaultSystem->UpdatePartyCreature(PartyIndex, Data);

	// Keep the battle actor's own Uses copy in step with the party entry we just wrote.
	PushUsesToBattleActors(ActiveCreatureActors, Data);



	UE_LOG(LogTemp, Log, TEXT("Used move. Uses remaining (slot %d): %d/%d"),

		SkillIndex, Data.CurrentUses[SkillIndex], Data.MaxUses[SkillIndex]);



	return true;

}



int32 UGF_CreatureManagerSubsystem::CalculateEXPYield(const FGF_CreatureInstanceData& DefeatedCreature, bool bIsTamerBattle) const
{
	if (DefeatedCreature.SpeciesData.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("CalculateEXPYield: SpeciesData is invalid!"));
		return 0;
	}

	UGF_CreatureSpeciesData* Species = DefeatedCreature.SpeciesData.LoadSynchronous();
	if (!Species)
	{
		UE_LOG(LogTemp, Error, TEXT("CalculateEXPYield: Failed to load species data!"));
		return 0;
	}

	// ✅ DIAGNOSTIC LOGGING:
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(" EXP CALCULATION FOR DEFEAT:"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("   Species: %s"), *Species->SpeciesName.ToString());
	UE_LOG(LogTemp, Warning, TEXT("BaseEXP: %.1f   <- CHECK THIS VALUE!"), Species->BaseEXP);
	UE_LOG(LogTemp, Warning, TEXT("   Level: %d"), DefeatedCreature.Level);
	UE_LOG(LogTemp, Warning, TEXT("   Tamer Battle: %s"), bIsTamerBattle ? TEXT("YES") : TEXT("NO"));

	// Base EXP formula: (BaseEXP * Level) / 7
	float BaseYield = (Species->BaseEXP * DefeatedCreature.Level) / 7.0f;

	UE_LOG(LogTemp, Warning, TEXT("   Calculation: (%.1f * %d) / 7 = %.1f"),
		Species->BaseEXP, DefeatedCreature.Level, BaseYield);

	// Tamer battles give 1.5x EXP
	if (bIsTamerBattle)
	{
		BaseYield *= 1.5f;
		UE_LOG(LogTemp, Warning, TEXT("   Tamer Multiplier: %.1f * 1.5 = %.1f"), BaseYield / 1.5f, BaseYield);
	}

	int32 FinalEXP = FMath::RoundToInt(BaseYield);
	UE_LOG(LogTemp, Warning, TEXT("Final EXP: %d"), FinalEXP);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	return FinalEXP;
}



bool UGF_CreatureManagerSubsystem::GiveEXP(int32 PartyIndex, int32 ExpAmount, bool& OutLeveledUp, int32& OutNewLevel)

{

	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	UE_LOG(LogTemp, Warning, TEXT("GiveEXP CALLED - START DEBUGGING"));

	UE_LOG(LogTemp, Warning, TEXT("========================================"));



	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Warning, TEXT("VaultSystem is NULL!"));

		return false;

	}



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		UE_LOG(LogTemp, Warning, TEXT("Failed to get party Creature at index %d"), PartyIndex);

		return false;

	}



	// Remember where this Creature started so CheckPartyLevelUpSkills() can scan every level
	// crossed, not just the one it landed on. See LevelBeforeEXPByPartyIndex.
	LevelBeforeEXPByPartyIndex.Add(PartyIndex, Data.Level);

	// Re-arm the queue-empty announcement for THIS down.
	//
	// Re-arming only when a move is queued or a Creature is sent out is not enough: a tamer
	// battle downs several Creature in a row without the player ever switching, and a down
	// that teaches nothing queues nothing. The second such down then found the latch still
	// set from the first, suppressed its announcement, and the battle-end sequence never
	// continued -- the opponent simply never sent out their next Creature.
	//
	// Every down runs EXP through here, for every party member, so this is the one place
	// guaranteed to fire once per down and before the move-learn drain starts.
	bSkillLearnQueueEmptyAnnounced = false;

	UE_LOG(LogTemp, Warning, TEXT(" INITIAL STATE:"));

	UE_LOG(LogTemp, Warning, TEXT("   Creature: %s"), *Data.GetDisplayName().ToString());

	UE_LOG(LogTemp, Warning, TEXT("   Level: %d"), Data.Level);

	UE_LOG(LogTemp, Warning, TEXT("   CurrentHP: %.1f"), Data.CurrentHP);

	UE_LOG(LogTemp, Warning, TEXT("   MaxHP: %.1f"), Data.MaxHP);

	UE_LOG(LogTemp, Warning, TEXT("   CurrentEXP: %.1f"), Data.CurrentEXP);

	UE_LOG(LogTemp, Warning, TEXT("   ExpAmount to add: %d"), ExpAmount);

	UE_LOG(LogTemp, Warning, TEXT("   IsDowned: %s"), Data.bIsDowned ? TEXT("YES") : TEXT("NO"));



	// Don't give EXP to eggs, downed Creature, or level 100

	if (Data.bIsEgg || Data.bIsDowned || Data.Level >= 100)

	{

		UE_LOG(LogTemp, Warning, TEXT("Creature is downed or max level, no EXP given"));

		OutLeveledUp = false;

		return true;

	}

	// Difficulty level cap. Checked here rather than only in AwardEXPFromBattle so
	// every EXP source (sanctuary steps, items, scripted awards) respects it too.

	if (UGF_SettingsSubsystem* Settings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UGF_SettingsSubsystem>() : nullptr)

	{

		if (Settings->IsLevelCapped(Data.Level))

		{

			UE_LOG(LogTemp, Warning, TEXT("%s is at the level cap (%d), no EXP given"),

				*Data.GetDisplayName().ToString(), Settings->GetLevelCap());

			OutLeveledUp = false;

			return true;

		}

	}



	// Load species data to get EXP curve

	if (Data.SpeciesData.IsNull())

	{

		UE_LOG(LogTemp, Warning, TEXT("SpeciesData is invalid!"));

		return false;

	}



	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();

	if (!Species)

	{

		UE_LOG(LogTemp, Warning, TEXT("Failed to load species data!"));

		return false;

	}



	UE_LOG(LogTemp, Warning, TEXT("Species loaded: %s"), *Species->SpeciesName.ToString());



	int32 OldLevel = Data.Level;

	float OldCurrentHP = Data.CurrentHP;

	float OldMaxHP = Data.MaxHP;



	Data.CurrentEXP += ExpAmount;



	UE_LOG(LogTemp, Warning, TEXT(""));

	UE_LOG(LogTemp, Warning, TEXT("AFTER ADDING EXP:"));

	UE_LOG(LogTemp, Warning, TEXT("   CurrentEXP: %.1f (was %.1f)"), Data.CurrentEXP, Data.CurrentEXP - ExpAmount);

	UE_LOG(LogTemp, Warning, TEXT("   CurrentHP: %.1f (should be unchanged)"), Data.CurrentHP);

	UE_LOG(LogTemp, Warning, TEXT("   MaxHP: %.1f (should be unchanged)"), Data.MaxHP);



	// Calculate EXP needed for next level using proper curve

	int32 ExpForCurrentLevel = CalculateTotalEXPForLevel(Data.Level, Species->ExpCurve);

	int32 ExpForNextLevel = CalculateTotalEXPForLevel(Data.Level + 1, Species->ExpCurve);

	int32 ExpNeededForLevelUp = ExpForNextLevel - ExpForCurrentLevel;



	UE_LOG(LogTemp, Warning, TEXT(""));

	UE_LOG(LogTemp, Warning, TEXT("EXP CALCULATIONS:"));

	UE_LOG(LogTemp, Warning, TEXT("   EXP for Level %d: %d"), Data.Level, ExpForCurrentLevel);

	UE_LOG(LogTemp, Warning, TEXT("   EXP for Level %d: %d"), Data.Level + 1, ExpForNextLevel);

	UE_LOG(LogTemp, Warning, TEXT("   EXP needed for level up: %d"), ExpNeededForLevelUp);

	UE_LOG(LogTemp, Warning, TEXT("   Current EXP: %.1f"), Data.CurrentEXP);

	UE_LOG(LogTemp, Warning, TEXT("   Will level up: %s"), (Data.CurrentEXP >= ExpNeededForLevelUp) ? TEXT("YES") : TEXT("NO"));



	// Level up loop (in case they gain multiple levels)

	int32 LevelUpCount = 0;

	while (Data.CurrentEXP >= ExpNeededForLevelUp && Data.Level < 100)

	{

		LevelUpCount++;

		UE_LOG(LogTemp, Warning, TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("LEVEL UP #%d - STARTING"), LevelUpCount);

		UE_LOG(LogTemp, Warning, TEXT("Current Level: %d -> %d"), Data.Level, Data.Level + 1);

		UE_LOG(LogTemp, Warning, TEXT("   HP BEFORE level up: %.1f / %.1f"), Data.CurrentHP, Data.MaxHP);



		Data.Level++;

		Data.CurrentEXP -= ExpNeededForLevelUp;



		UE_LOG(LogTemp, Warning, TEXT("   EXP after consuming: %.1f"), Data.CurrentEXP);



		// Recalculate stats on level up (classic+ formula)

		float OldMaxHPBeforeCalc = Data.MaxHP;

		Data.MaxHP = FMath::FloorToInt(((2.0f * Species->BaseStats.HP + Data.HP_AP + Data.HP_EP) * Data.Level / 100.0f) + Data.Level + 10);

		// Husk: see UGF_CreatureSpeciesData::HasFixedOneHP.
		if (Species->HasFixedOneHP())
		{
			Data.MaxHP = 1;
		}



		UE_LOG(LogTemp, Warning, TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("STAT RECALCULATION:"));

		UE_LOG(LogTemp, Warning, TEXT("      Base HP: %d"), Species->BaseStats.HP);

		UE_LOG(LogTemp, Warning, TEXT("      HP AP: %d"), Data.HP_AP);

		UE_LOG(LogTemp, Warning, TEXT("      HP EP: %d"), Data.HP_EP);

		UE_LOG(LogTemp, Warning, TEXT("      New Level: %d"), Data.Level);

		UE_LOG(LogTemp, Warning, TEXT("      Old MaxHP: %.1f"), OldMaxHPBeforeCalc);

		UE_LOG(LogTemp, Warning, TEXT("      New MaxHP: %.1f"), Data.MaxHP);



		// Heal the HP gained from level up

		float HpGained = Data.MaxHP - OldMaxHPBeforeCalc;

		float OldCurrentHPBeforeHeal = Data.CurrentHP;

		Data.CurrentHP = FMath::Min(Data.CurrentHP + HpGained, Data.MaxHP);



		UE_LOG(LogTemp, Warning, TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("HP HEALING:"));

		UE_LOG(LogTemp, Warning, TEXT("      HP Gained from level: %.1f"), HpGained);

		UE_LOG(LogTemp, Warning, TEXT("      CurrentHP before heal: %.1f"), OldCurrentHPBeforeHeal);

		UE_LOG(LogTemp, Warning, TEXT("      CurrentHP after adding gained HP: %.1f"), OldCurrentHPBeforeHeal + HpGained);

		UE_LOG(LogTemp, Warning, TEXT("      CurrentHP after Min clamp: %.1f"), Data.CurrentHP);

		UE_LOG(LogTemp, Warning, TEXT("      HP AFTER level up: %.1f / %.1f"), Data.CurrentHP, Data.MaxHP);



		// Update for next iteration

		if (Data.Level < 100)

		{

			ExpForCurrentLevel = CalculateTotalEXPForLevel(Data.Level, Species->ExpCurve);

			ExpForNextLevel = CalculateTotalEXPForLevel(Data.Level + 1, Species->ExpCurve);

			ExpNeededForLevelUp = ExpForNextLevel - ExpForCurrentLevel;



			UE_LOG(LogTemp, Warning, TEXT("   Next level check: Need %d EXP, have %.1f"), ExpNeededForLevelUp, Data.CurrentEXP);

		}

	}



	if (LevelUpCount == 0)

	{

		UE_LOG(LogTemp, Warning, TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("NO LEVEL UP OCCURRED"));

		UE_LOG(LogTemp, Warning, TEXT("   HP should remain unchanged: %.1f / %.1f"), Data.CurrentHP, Data.MaxHP);

	}



	OutLeveledUp = (Data.Level > OldLevel);

	OutNewLevel = Data.Level;



	UE_LOG(LogTemp, Warning, TEXT(""));

	UE_LOG(LogTemp, Warning, TEXT("FINAL STATE BEFORE SAVING:"));

	UE_LOG(LogTemp, Warning, TEXT("   Level: %d (was %d)"), Data.Level, OldLevel);

	UE_LOG(LogTemp, Warning, TEXT("   CurrentHP: %.1f (was %.1f)"), Data.CurrentHP, OldCurrentHP);

	UE_LOG(LogTemp, Warning, TEXT("   MaxHP: %.1f (was %.1f)"), Data.MaxHP, OldMaxHP);

	UE_LOG(LogTemp, Warning, TEXT("   LeveledUp: %s"), OutLeveledUp ? TEXT("YES") : TEXT("NO"));



	// Save updated data

	UE_LOG(LogTemp, Warning, TEXT(""));

	UE_LOG(LogTemp, Warning, TEXT("SAVING TO BOXSYSTEM..."));

	VaultSystem->UpdatePartyCreature(PartyIndex, Data);



	// Verify what we just saved

	FGF_CreatureInstanceData VerifyData;

	if (VaultSystem->GetPartyCreature(PartyIndex, VerifyData))

	{

		UE_LOG(LogTemp, Warning, TEXT("VERIFICATION - Data in VaultSystem after save:"));

		UE_LOG(LogTemp, Warning, TEXT("   CurrentHP: %.1f"), VerifyData.CurrentHP);

		UE_LOG(LogTemp, Warning, TEXT("   MaxHP: %.1f"), VerifyData.MaxHP);



		if (VerifyData.CurrentHP != Data.CurrentHP || VerifyData.MaxHP != Data.MaxHP)

		{

			UE_LOG(LogTemp, Warning, TEXT("WARNING: DATA MISMATCH AFTER SAVE!"));

			UE_LOG(LogTemp, Warning, TEXT("   Expected: %.1f / %.1f"), Data.CurrentHP, Data.MaxHP);

			UE_LOG(LogTemp, Warning, TEXT("   Got: %.1f / %.1f"), VerifyData.CurrentHP, VerifyData.MaxHP);

		}

	}



	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	UE_LOG(LogTemp, Warning, TEXT("GiveEXP COMPLETE"));

	UE_LOG(LogTemp, Warning, TEXT("========================================"));



	return true;

}





int32 UGF_CreatureManagerSubsystem::AwardEXPFromBattle(const FGF_CreatureInstanceData& DefeatedCreature, const TArray<int32>& BattlerIndices, TMap<int32, int32>& OutLevelUps,TMap<int32, int32>& OutEXPGains, float YieldMultiplier)

{
	if (!VaultSystem)
	{
		return 0;
	}

	OutLevelUps.Empty();
	OutEXPGains.Empty();

	// Calculate base EXP yield
	int32 BaseEXP = CalculateEXPYield(DefeatedCreature, false);

	// Scaled before the share-out, and before the <= 0 test: a multiplier can only
	// raise the yield, so a Creature worth nothing stays worth nothing.
	if (!FMath::IsNearlyEqual(YieldMultiplier, 1.0f))
	{
		const int32 Unscaled = BaseEXP;
		BaseEXP = FMath::RoundToInt(BaseEXP * FMath::Max(0.0f, YieldMultiplier));

		UE_LOG(LogTemp, Warning, TEXT("EXP yield multiplier %.2fx: %d -> %d"),
			YieldMultiplier, Unscaled, BaseEXP);
	}

	if (BaseEXP <= 0)
	{
		return 0;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== AWARDING EXP ==="));
	UE_LOG(LogTemp, Warning, TEXT("Base EXP from defeated %s: %d"),
		*DefeatedCreature.GetDisplayName().ToString(), BaseEXP);

	int32 PartySize = VaultSystem->GetPartySize();
	int32 BattlerCount = BattlerIndices.Num();

	// Player options drive EXP Share and the difficulty EXP curve. Resolved once
	// so a settings change mid-battle cannot split one award across two rulesets.
	UGF_SettingsSubsystem* Settings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGF_SettingsSubsystem>()
		: nullptr;

	const bool  bEXPShareOn   = Settings ? Settings->IsEXPShareActive() : true;
	const float EXPShareRatio = Settings ? Settings->GetEXPShareRatio() : 0.25f;
	const float EXPMultiplier = Settings ? Settings->GetEXPMultiplier() : 1.0f;
	const bool  bCapEnforced  = Settings ? Settings->IsLevelCapEnforced() : false;
	const int32 LevelCap      = Settings ? Settings->GetLevelCap() : 100;

	// Affinity grows from battling, not from EXP, so it is handed out first and to
	// every battler still standing -- a level 100 or level-capped battler that earns
	// no EXP below still earns affinity here. GiveEXP re-reads the party, so the
	// raised APs feed straight into any level-up stat recalculation.
	const int32 AffinityGain = GetDefault<UGF_CreatureRulesSettings>()->AffinityPerFoeDefeated;
	if (AffinityGain > 0)
	{
		for (const int32 BattlerIndex : BattlerIndices)
		{
			FGF_CreatureInstanceData Battler;
			if (!VaultSystem->GetPartyCreature(BattlerIndex, Battler) || Battler.bIsDowned || Battler.IsEgg()
				|| Battler.IsMaxAffinity())
			{
				continue;
			}

			Battler.AddAffinity(AffinityGain);
			RecalculateStats(Battler);
			VaultSystem->UpdatePartyCreature(BattlerIndex, Battler);

			UE_LOG(LogTemp, Log, TEXT(" %s: affinity +%d (now %d)"),
				*Battler.GetDisplayName().ToString(), AffinityGain, Battler.Affinity);
		}
	}

	// EXP Share: battlers always get their full share; the rest get EXPShareRatio
	// of it, or nothing at all when the option is off.
	for (int32 i = 0; i < PartySize; i++)
	{
		FGF_CreatureInstanceData Data;
		if (!VaultSystem->GetPartyCreature(i, Data))
		{
			continue;
		}

		// Skip downed Creature
		if (Data.bIsDowned)
		{
			UE_LOG(LogTemp, Log, TEXT(" %s (Downed): No EXP"),
				*Data.GetDisplayName().ToString());
			continue;
		}

		// Skip level 100 Creature — they cannot gain EXP or level up
		if (Data.Level >= 100)
		{
			UE_LOG(LogTemp, Log, TEXT(" %s (Level 100): No EXP"),
				*Data.GetDisplayName().ToString());
			continue;
		}

		// Difficulty level cap — at the cap, EXP stops entirely until the next emblem.
		// Reported as a gain of 0 rather than omitted: the battle sequence drives its
		// post-KO flow off OutEXPGains, and when a whole party is capped, dropping
		// every entry leaves the map empty and the sequence stalls before the
		// send-out and defeat dialogue.
		if (bCapEnforced && Data.Level >= LevelCap)
		{
			OutEXPGains.Add(i, 0);
			UE_LOG(LogTemp, Log, TEXT(" %s (Level cap %d): No EXP"),
				*Data.GetDisplayName().ToString(), LevelCap);
			continue;
		}

		// Battlers get full EXP divided by number of battlers
		// Non-battlers get 50% EXP divided by number of non-battlers
		int32 ExpGained = 0;

		if (BattlerIndices.Contains(i))
		{
			// Battler gets full share
			ExpGained = BaseEXP / FMath::Max(1, BattlerCount);
			UE_LOG(LogTemp, Warning, TEXT(" %s (Battler): +%d EXP"),
				*Data.GetDisplayName().ToString(), ExpGained);
		}
		else
		{
			if (!bEXPShareOn)
			{
				UE_LOG(LogTemp, Log, TEXT(" %s (EXP Share off): No EXP"),
					*Data.GetDisplayName().ToString());
				continue;
			}

			ExpGained = FMath::RoundToInt(BaseEXP * EXPShareRatio);
			UE_LOG(LogTemp, Warning, TEXT(" %s (EXP Share): +%d EXP"),
				*Data.GetDisplayName().ToString(), ExpGained);
		}

		ExpGained = FGF_BattleBridge::HeldItemEXPBoost(this, Data, ExpGained);

		// Difficulty EXP curve, applied last so held-item boosts scale with it.
		// Anything that earned EXP keeps at least 1 point so the bar always moves.
		if (ExpGained > 0 && !FMath::IsNearlyEqual(EXPMultiplier, 1.0f))
		{
			ExpGained = FMath::Max(1, FMath::RoundToInt(ExpGained * EXPMultiplier));
		}


		//STORE THE EXP GAIN
		OutEXPGains.Add(i, ExpGained);

		// Give the EXP
		bool bLeveledUp = false;
		int32 NewLevel = 0;
		GiveEXP(i, ExpGained, bLeveledUp, NewLevel);

		// Store level up info
		if (bLeveledUp)
		{
			OutLevelUps.Add(i, NewLevel);  // Store the NEW level, not just "1"
		}
	}

	// Save after all EXP is awarded
	MarkDirty();

	UE_LOG(LogTemp, Warning, TEXT("=== EXP AWARDED ==="));

	return BaseEXP;
}



int32 UGF_CreatureManagerSubsystem::GetEXPToNextLevel(int32 PartyIndex) const

{

	if (!VaultSystem)

	{

		return 0;

	}



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		return 0;

	}



	if (Data.Level >= 100)

	{

		return 0;

	}



	// Load species to get curve

	if (Data.SpeciesData.IsNull())

	{

		return 0;

	}



	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();

	if (!Species)

	{

		return 0;

	}



	// Calculate EXP needed using proper curve

	int32 ExpForCurrentLevel = CalculateTotalEXPForLevel(Data.Level, Species->ExpCurve);

	int32 ExpForNextLevel = CalculateTotalEXPForLevel(Data.Level + 1, Species->ExpCurve);

	int32 ExpNeeded = (ExpForNextLevel - ExpForCurrentLevel) - Data.CurrentEXP;



	return FMath::Max(0, ExpNeeded);

}


int32 UGF_CreatureManagerSubsystem::GetEXPSpanForLevel(int32 PartyIndex, int32 Level) const
{
	if (!VaultSystem)
	{
		return 0;
	}

	FGF_CreatureInstanceData Data;
	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))
	{
		return 0;
	}

	// 100 is the ceiling, and a level nobody grows out of has no span.
	if (Level < 1 || Level >= 100)
	{
		return 0;
	}

	if (Data.SpeciesData.IsNull())
	{
		return 0;
	}

	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();
	if (!Species)
	{
		return 0;
	}

	// The curve is cumulative, so a single level is the gap between two of its
	// entries. Deliberately not clamped to Data.Level -- the caller is walking
	// levels the creature has already earned but the screen has not shown yet.
	const int32 Span = CalculateTotalEXPForLevel(Level + 1, Species->ExpCurve)
		- CalculateTotalEXPForLevel(Level, Species->ExpCurve);

	return FMath::Max(0, Span);
}



TMap<int32, FGF_LevelUpSkills> UGF_CreatureManagerSubsystem::CheckPartyLevelUpSkills(const TMap<int32, int32>& LevelUps)

{

	TMap<int32, FGF_LevelUpSkills> LearnableSkills;



	for (const TPair<int32, int32>& LevelUp : LevelUps)

	{

		int32 PartyIndex = LevelUp.Key;

		int32 NewLevel = LevelUp.Value;



		FGF_CreatureInstanceData Data;

		if (VaultSystem && VaultSystem->GetPartyCreature(PartyIndex, Data))

		{

			// Scan EVERY level gained, not just the final one.
			//
			// A single battle can grant more than one level, and each level has its own
			// learnset entry. Asking only about NewLevel drops the moves from every level
			// skipped over -- Sprigling going 5 -> 7 was asked about 7, so Siphon at 6 vanished
			// without ever being offered. Falls back to the single level when no start was
			// recorded, which is the old behaviour.
			const int32* StartLevel = LevelBeforeEXPByPartyIndex.Find(PartyIndex);
			const int32 FirstLevelToCheck = StartLevel ? (*StartLevel + 1) : NewLevel;

			TArray<TSoftClassPtr<AGF_SkillDefinition>> NewSkills;
			for (int32 Level = FirstLevelToCheck; Level <= NewLevel; ++Level)
			{
				for (const TSoftClassPtr<AGF_SkillDefinition>& Skill : Data.GetSkillsToLearnAtLevel(Level))
				{
					// A move can sit at more than one level; only offer it once.
					NewSkills.AddUnique(Skill);
				}
			}

			if (StartLevel && FirstLevelToCheck < NewLevel)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("%s gained %d levels (%d -> %d) - scanned every level for new moves."),
					*Data.GetDisplayName().ToString(), NewLevel - *StartLevel, *StartLevel, NewLevel);
			}

			// Consumed: this record describes the battle just resolved.
			LevelBeforeEXPByPartyIndex.Remove(PartyIndex);



			if (NewSkills.Num() > 0)

			{

				FGF_LevelUpSkills SkillWrapper;

				SkillWrapper.LearnableSkills = NewSkills;

				LearnableSkills.Add(PartyIndex, SkillWrapper);



				UE_LOG(LogTemp, Warning, TEXT("%s can learn %d new moves at level %d!"),

					*Data.GetDisplayName().ToString(), NewSkills.Num(), NewLevel);

			}

		}

	}



	return LearnableSkills;

}



int32 UGF_CreatureManagerSubsystem::CalculateTotalEXPForLevel(int32 Level, EGF_EXPCurves Curve) const

{

	if (Level <= 1)

	{

		return 0;

	}



	int32 n = Level;

	float n2 = n * n;

	float n3 = n * n * n;



	switch (Curve)

	{

	case EGF_EXPCurves::Volatile:

		if (n < 50)

		{

			return FMath::RoundToInt((n3 * (100 - n)) / 50.0f);

		}

		else if (n >= 50 && n < 68)

		{

			return FMath::RoundToInt((n3 * (150 - n)) / 100.0f);

		}

		else if (n >= 68 && n < 98)

		{

			return FMath::RoundToInt((n3 * ((1911 - 10 * n) / 3.0f)) / 500.0f);

		}

		else // n >= 98

		{

			return FMath::RoundToInt((n3 * (160 - n)) / 100.0f);

		}



	case EGF_EXPCurves::Swift:

		return FMath::RoundToInt((4 * n3) / 5.0f);



	case EGF_EXPCurves::Steady:

		return FMath::RoundToInt(n3);



	case EGF_EXPCurves::Measured:

		return FMath::RoundToInt((6.0f / 5.0f) * n3 - 15 * n2 + 100 * n - 140);



	case EGF_EXPCurves::Gradual:

		return FMath::RoundToInt((5 * n3) / 4.0f);



	case EGF_EXPCurves::Uneven:

		if (n < 15)

		{

			return FMath::RoundToInt((n3 * (((n + 1) / 3.0f) + 24)) / 50.0f);

		}

		else if (n >= 15 && n < 36)

		{

			return FMath::RoundToInt((n3 * (n + 14)) / 50.0f);

		}

		else // n >= 36

		{

			return FMath::RoundToInt((n3 * ((n / 2.0f) + 32)) / 50.0f);

		}



	default:

		return FMath::RoundToInt(n3);

	}

}



int32 UGF_CreatureManagerSubsystem::CalculateEXPForLevel(int32 Level, EGF_EXPCurves Curve) const

{

	return CalculateTotalEXPForLevel(Level, Curve);

}

int32 UGF_CreatureManagerSubsystem::GetTotalEXP(int32 PartyIndex) const
{
    if (!VaultSystem)
    {
        return 0;
    }

    FGF_CreatureInstanceData Data;
    if (!VaultSystem->GetPartyCreature(PartyIndex, Data))
    {
        return 0;
    }

    // Load species to get curve
    if (Data.SpeciesData.IsNull())
    {
        return 0;
    }

    UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();
    if (!Species)
    {
        return 0;
    }

    // Calculate total cumulative EXP
    int32 ExpForCurrentLevel = CalculateTotalEXPForLevel(Data.Level, Species->ExpCurve);
    int32 TotalEXP = ExpForCurrentLevel + static_cast<int32>(Data.CurrentEXP);

    return TotalEXP;
}



float UGF_CreatureManagerSubsystem::GetEXPProgressPercent(int32 PartyIndex) const

{

	if (!VaultSystem)

	{

		return 0.0f;

	}



	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		return 0.0f;

	}



	if (Data.Level >= 100)

	{

		return 1.0f; // Max level

	}



	if (Data.SpeciesData.IsNull())

	{

		return 0.0f;

	}



	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();

	if (!Species)

	{

		return 0.0f;

	}



	// Calculate EXP range for current level

	int32 ExpForCurrentLevel = CalculateTotalEXPForLevel(Data.Level, Species->ExpCurve);

	int32 ExpForNextLevel = CalculateTotalEXPForLevel(Data.Level + 1, Species->ExpCurve);

	int32 ExpNeededForLevel = ExpForNextLevel - ExpForCurrentLevel;
	int32 ExpIntoCurrentLevel = Data.CurrentEXP - ExpForCurrentLevel;



	if (ExpNeededForLevel <= 0)

	{

		return 1.0f;

	}



	// Calculate progress (0.0 to 1.0)

	float Progress = (float)Data.CurrentEXP / (float)ExpNeededForLevel;

	return FMath::Clamp(Progress, 0.0f, 1.0f);

}





//--------------------

// BATTLE MANAGEMENT

//--------------------



AGF_Creature* UGF_CreatureManagerSubsystem::SwitchBattleCreature(

	AGF_Creature* CurrentCreatureActor,

	int32 CurrentPartyIndex,

	int32 NewPartyIndex,

	FVector SpawnLocation,

	FRotator SpawnRotation)

{

	UE_LOG(LogTemp, Warning, TEXT("SWITCHING CREATURE"));

	UE_LOG(LogTemp, Warning, TEXT("From Index: %d -> To Index: %d"), CurrentPartyIndex, NewPartyIndex);



	if (!VaultSystem)

	{

		UE_LOG(LogTemp, Error, TEXT("VaultSystem is null!"));

		return nullptr;

	}



	// Validate indices

	if (!VaultSystem->IsValidPartyIndex(CurrentPartyIndex))

	{

		UE_LOG(LogTemp, Error, TEXT("Invalid current party index: %d"), CurrentPartyIndex);

		return nullptr;

	}



	if (!VaultSystem->IsValidPartyIndex(NewPartyIndex))

	{

		UE_LOG(LogTemp, Error, TEXT("Invalid new party index: %d"), NewPartyIndex);

		return nullptr;

	}



	// Can't switch to same Creature

	if (CurrentPartyIndex == NewPartyIndex)

	{

		UE_LOG(LogTemp, Warning, TEXT("Cannot switch to same Creature!"));

		return nullptr;

	}



	// Check if new Creature is available

	if (!CanSwitchToCreature(NewPartyIndex, CurrentPartyIndex))

	{

		UE_LOG(LogTemp, Warning, TEXT("Cannot switch to Creature at index %d (downed or invalid)"), NewPartyIndex);

		return nullptr;

	}



	// Step 1: Save current Creature's state back to manager

	if (CurrentCreatureActor && IsValid(CurrentCreatureActor))

	{

		UE_LOG(LogTemp, Warning, TEXT("Saving current Creature state..."));



		if (!UpdatePartyFromActor(CurrentCreatureActor, CurrentPartyIndex))

		{

			UE_LOG(LogTemp, Error, TEXT("Failed to save current Creature state!"));

		}



		// Step 2: Destroy current Creature actor

		UE_LOG(LogTemp, Warning, TEXT("Destroying current Creature actor..."));

		ActiveCreatureActors.Remove(CurrentCreatureActor);

		CurrentCreatureActor->Destroy();

	}



	// Step 3: Spawn new Creature

	UE_LOG(LogTemp, Warning, TEXT("Spawning new Creature from index %d..."), NewPartyIndex);

	AGF_Creature* NewCreature = SpawnPartyCreature(NewPartyIndex, SpawnLocation, SpawnRotation, true);



	if (NewCreature)

	{

		FGF_CreatureInstanceData Data;

		if (VaultSystem->GetPartyCreature(NewPartyIndex, Data))

		{

			UE_LOG(LogTemp, Warning, TEXT("Successfully switched to %s!"), *Data.GetDisplayName().ToString());

		}

	}

	else

	{

		UE_LOG(LogTemp, Error, TEXT("Failed to spawn new Creature!"));

	}



	return NewCreature;

}



TArray<int32> UGF_CreatureManagerSubsystem::GetAvailableSwitchOptions(int32 CurrentPartyIndex) const

{

	TArray<int32> AvailableIndices;



	if (!VaultSystem)

	{

		return AvailableIndices;

	}



	int32 PartySize = VaultSystem->GetPartySize();



	for (int32 i = 0; i < PartySize; i++)

	{

		if (CanSwitchToCreature(i, CurrentPartyIndex))

		{

			AvailableIndices.Add(i);

		}

	}



	UE_LOG(LogTemp, Log, TEXT("Available switch options: %d Creature"), AvailableIndices.Num());



	return AvailableIndices;

}



bool UGF_CreatureManagerSubsystem::CanSwitchToCreature(int32 PartyIndex, int32 CurrentPartyIndex) const

{

	if (!VaultSystem)

	{

		return false;

	}



	// Can't switch to current Creature

	if (PartyIndex == CurrentPartyIndex)

	{

		return false;

	}



	// Check if index is valid

	if (!VaultSystem->IsValidPartyIndex(PartyIndex))

	{

		return false;

	}



	// Check if Creature is downed

	FGF_CreatureInstanceData Data;

	if (!VaultSystem->GetPartyCreature(PartyIndex, Data))

	{

		return false;

	}



	// Can't switch to downed Creature, and an egg can never be sent out

	if (Data.bIsEgg || Data.bIsDowned)

	{

		return false;

	}



	return true;

}



bool UGF_CreatureManagerSubsystem::HasHealthyCreatureAvailable(int32 CurrentPartyIndex) const

{

	if (!VaultSystem)

	{

		return false;

	}



	int32 PartySize = VaultSystem->GetPartySize();



	for (int32 i = 0; i < PartySize; i++)

	{

		// Skip current Creature

		if (i == CurrentPartyIndex)

		{

			continue;

		}



		FGF_CreatureInstanceData Data;

		if (VaultSystem->GetPartyCreature(i, Data))

		{

			// Found a healthy Creature — an egg doesn't count, it can't battle

			if (!Data.bIsEgg && !Data.bIsDowned)

			{

				return true;

			}

		}

	}



	return false;

}



int32 UGF_CreatureManagerSubsystem::GetHealthyCreatureCount() const

{

	if (!VaultSystem)

	{

		return 0;

	}



	int32 HealthyCount = 0;

	int32 PartySize = VaultSystem->GetPartySize();



	for (int32 i = 0; i < PartySize; i++)

	{

		FGF_CreatureInstanceData Data;

		if (VaultSystem->GetPartyCreature(i, Data))

		{

			// Eggs never count toward the battle-ready total.

			if (!Data.bIsEgg && !Data.bIsDowned)

			{

				HealthyCount++;

			}

		}

	}



	return HealthyCount;

}



bool UGF_CreatureManagerSubsystem::IsPartyCreatureCurrentlyActive(int32 PartyIndex) const

{

	// Simple check: just compare the cached party index

	// If CurrentBattleCreaturePartyIndex is -1, no Creature is active

	return (CurrentBattleCreaturePartyIndex == PartyIndex);

}



int32 UGF_CreatureManagerSubsystem::GetCurrentBattleCreaturePartyIndex() const

{

	// Return the cached party index

	return CurrentBattleCreaturePartyIndex;

}



void UGF_CreatureManagerSubsystem::SetCurrentBattleCreature(AGF_Creature* CreatureActor, int32 PartyIndex)

{

	// The actor is assigned only AFTER the index is validated, further down.

	//

	// It used to be set here, before the egg check, and the rejection path below returns

	// without touching CurrentBattleCreaturePartyIndex. A rejected call therefore left the

	// NEW actor paired with the OLD index -- and everything that saves battle state reads

	// that pair (GetCurrentBattleCreatureActor + GetCurrentBattleCreaturePartyIndex) and

	// writes the on-stage Creature's HP into whoever the stale index pointed at. Rejecting

	// half a change is worse than rejecting none of it: both fields have to move together.



	// An egg can never be the active battle Creature, so this index is provably wrong —
	// almost always a lead-slot 0 that assumes the first party member was sent out.
	// Keeping the old (correct) index matters: this value is what the battle uses to
	// decide whose turn it is, and pointing it at an egg means every move lands on a
	// Creature with no moves, the turn never resolves, and the battle soft-locks.

	FGF_CreatureInstanceData Data;

	if (VaultSystem && VaultSystem->GetPartyCreature(PartyIndex, Data) && Data.bIsEgg)

	{

		UE_LOG(LogTemp, Error,

			TEXT("SetCurrentBattleCreature: party index %d is an EGG - REJECTED, keeping index %d. "

				 "Whatever Blueprint made this call is passing the lead slot instead of the "

				 "Creature that was actually sent out; wire it from Get Current Battle Creature Party Index."),

			PartyIndex, CurrentBattleCreaturePartyIndex);

#if DO_BLUEPRINT_GUARD

		// Names the Blueprint function and node that passed the bad index.
		UE_LOG(LogTemp, Error, TEXT("  Blueprint callstack:\n%s"), *FFrame::GetScriptCallstack());

#endif

		return;

	}



	// A new send-out starts a fresh drain, so a battle that queues no move-learns at all still
	// gets its single announcement and the battle-end sequence still advances.
	bSkillLearnQueueEmptyAnnounced  = false;

	CurrentBattleCreature           = CreatureActor;

	CurrentBattleCreaturePartyIndex = PartyIndex;



	UE_LOG(LogTemp, Log, TEXT("Set current battle Creature to party index %d"), PartyIndex);

}


void UGF_CreatureManagerSubsystem::SyncActiveCreatureToSave()
{
	if (!VaultSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot sync battle data - VaultSystem is null!"));
		return;
	}

	int32 SyncedCount = 0;

	// Loop through ALL active Creature actors
	for (AGF_Creature* CreatureActor : ActiveCreatureActors)
	{
		if (!CreatureActor || !IsValid(CreatureActor))
			continue;

		// Only sync player Creature (skip wild/enemy)
		if (!CreatureActor->isPlayerCreature)
		{
			UE_LOG(LogTemp, Log, TEXT("Skipping %s (not player Creature)"),
				*CreatureActor->Name.ToString());
			continue;
		}

		// Export current actor data
		FGF_CreatureInstanceData ActorData = CreatureActor->ExportToInstanceData();

		// Find which party slot this Creature belongs to by matching CreatureID
		int32 PartyIndex = -1;
		for (int32 i = 0; i < VaultSystem->Party.Num(); i++)
		{
			if (VaultSystem->Party[i].CreatureID == ActorData.CreatureID)
			{
				PartyIndex = i;
				break;
			}
		}

		if (PartyIndex == -1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not find party slot for %s (CreatureID: %d)"),
				*ActorData.GetDisplayName().ToString(), ActorData.CreatureID);
			continue;
		}

		// Sync the battle data back to VaultSystem
		if (VaultSystem->UpdatePartyCreatureBattleData(PartyIndex, ActorData))
		{
			UE_LOG(LogTemp, Warning, TEXT("Synced %s (Slot %d) - Level: %d, EXP: %.0f, HP: %.0f/%.0f, Uses: %s"),
				*ActorData.GetDisplayName().ToString(),
				PartyIndex,
				ActorData.Level,
				ActorData.CurrentEXP,
				ActorData.CurrentHP,
				ActorData.MaxHP,
				*FString::JoinBy(ActorData.CurrentUses, TEXT("/"), [](int32 Uses) { return FString::FromInt(Uses); }));

			SyncedCount++;
		}
	}

	if (SyncedCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No Creature were synced - ActiveCreatureActors might be empty"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Synced %d Creature battle data to save system"), SyncedCount);
	}
}


bool UGF_CreatureManagerSubsystem::FirePostBattleCutsceneIfPending()
{
	if (PendingPostBattleCutscene == NAME_None)
		return false;

	const FName CutsceneName = PendingPostBattleCutscene;
	const FName ActorTag     = PendingPostBattleCutsceneActorTag;

	// Clear before broadcasting so re-entrant calls don't double-fire
	PendingPostBattleCutscene         = NAME_None;
	PendingPostBattleCutsceneActorTag = NAME_None;

	OnPostBattleCutsceneReady.Broadcast(CutsceneName, ActorTag);
	return true;
}

void UGF_CreatureManagerSubsystem::ClearCurrentBattleCreature()
{
	CurrentBattleCreature = nullptr;
	CurrentBattleCreaturePartyIndex = -1;



	UE_LOG(LogTemp, Log, TEXT("Cleared current battle Creature"));

}
void UGF_CreatureManagerSubsystem::StartBattle()
{
	bIsInBattle = true;

	// The previous battle's rematch verdict has been readable all the way to here; a new
	// battle starting is the point it stops meaning anything.
	bLastBattleWasRematch = false;

	GF_DIAG("CreatureManager: Battle started - auto-save disabled (rematch: %s)",
		bIsRematchBattle ? TEXT("YES") : TEXT("no"));
}

void UGF_CreatureManagerSubsystem::SetRematchBattle(bool bInIsRematch)
{
	bIsRematchBattle = bInIsRematch;

	// Logged unconditionally: if a tamer ever fields the wrong team, this line and the
	// one AGF_TamerMaster::InitializeTeam prints are the whole diagnosis, and Print String
	// does not reach a packaged log.
	UE_LOG(LogTemp, Log, TEXT("CreatureManager: next battle flagged as %s"),
		bInIsRematch ? TEXT("a REMATCH") : TEXT("a normal battle"));
}

void UGF_CreatureManagerSubsystem::EndBattle(bool bSaveImmediately)
{
	SyncActiveCreatureToSave();
	bIsInBattle = false;

	// Hand the rematch verdict over to the flag that outlives this call, then drop the live
	// one. Clearing it here is what stops the next ordinary tamer fielding a rematch team:
	// nothing else in the battle flow ever turns it off.
	bLastBattleWasRematch = bIsRematchBattle;
	bIsRematchBattle = false;

	ClearCurrentBattleCreature();

	// A battle end must NOT write to disk, whatever bSaveImmediately says.
	//
	// This used to call AutoSaveAtCheckpoint("Battle Victory"), and the parameter
	// defaults to true, so every battle - win, loss or run - committed the whole
	// save. UGF_VaultSystem::SaveToDisk() writes QuestSlot and the berry slot
	// alongside the party, so that also committed every story flag the player had
	// picked up but not saved.
	//
	// That is what let a tester beat the rival on Route 103, hard reset without saving,
	// and still find the Compendium sequence complete - and worse, hand Silas the
	// letter in Quarry Cave, reset, and come back to no Silas and a soft lock.
	// Progress must only become permanent when the player saves, exactly as the
	// games this one is modelled on behave.
	//
	// The parameter is kept so existing Blueprint call sites still compile, but it
	// no longer reaches disk. AutoSaveAtCheckpoint() is still available for a
	// DELIBERATE checkpoint - just never wire it to something the player can
	// trigger accidentally.
	MarkDirty();

	if (bSaveImmediately)
	{
		UE_LOG(LogTemp, Log,
			TEXT("CreatureManager: Battle ended - marked dirty. The old auto-save here committed "
			     "unsaved story flags and is deliberately gone."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("CreatureManager: Battle ended - marked dirty (not saved)."));
	}
}

void UGF_CreatureManagerSubsystem::MarkSpeciesAsCaught(int32 CompendiumNumber)
{
    if (VaultSystem)
    {
        VaultSystem->MarkSpeciesAsCaught(CompendiumNumber);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot mark species as caught - VaultSystem is null"));
    }
}

void UGF_CreatureManagerSubsystem::MarkSpeciesAsCaughtFromData(UGF_CreatureSpeciesData* SpeciesData)
{
    if (VaultSystem)
    {
        VaultSystem->MarkSpeciesAsCaughtFromData(SpeciesData);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot mark species as caught - VaultSystem is null"));
    }
}

bool UGF_CreatureManagerSubsystem::HasCaughtSpecies(int32 CompendiumNumber) const
{
    if (VaultSystem)
    {
        return VaultSystem->HasCaughtSpecies(CompendiumNumber);
    }
    return false;
}

bool UGF_CreatureManagerSubsystem::HasCaughtSpeciesFromData(UGF_CreatureSpeciesData* SpeciesData) const
{
    if (VaultSystem)
    {
        return VaultSystem->HasCaughtSpeciesFromData(SpeciesData);
    }
    return false;
}

int32 UGF_CreatureManagerSubsystem::GetTotalSpeciesCaught() const
{
    if (VaultSystem)
    {
        return VaultSystem->GetTotalSpeciesCaught();
    }
    return 0;
}

TArray<int32> UGF_CreatureManagerSubsystem::GetAllCaughtSpecies() const
{
    if (VaultSystem)
    {
        return VaultSystem->GetAllCaughtSpecies();
    }
    return TArray<int32>();
}

void UGF_CreatureManagerSubsystem::MarkAllPartyCreatureAsCaught()
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot mark party Creature - VaultSystem is null"));
        return;
    }

    int32 PartySize = VaultSystem->GetPartySize();
    for (int32 i = 0; i < PartySize; i++)
    {
        FGF_CreatureInstanceData Data;
        if (VaultSystem->GetPartyCreature(i, Data) && !Data.SpeciesData.IsNull())
        {
            UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();
            if (Species)
            {
                VaultSystem->MarkSpeciesAsCaught(Species->CompendiumNumber);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("CreatureManager: Marked all party Creature as caught"));
}

int32 UGF_CreatureManagerSubsystem::BackfillCompendiumFromOwnedCreature()
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("BackfillCompendiumFromOwnedCreature: VaultSystem is null"));
        return 0;
    }

    int32 Added = 0;

    // Resolves one stored Creature to a dex number WITHOUT loading its species asset:
    // path -> name off the registry index, then name -> number off the summary list.
    // Doing this with LoadSynchronous instead would drag in every species the player
    // has ever boxed, at ~35 MB of sprites each.
    auto RegisterOne = [this, &Added](const FGF_CreatureInstanceData& Mon)
    {
        if (Mon.SpeciesData.IsNull())
        {
            return;
        }

        const FName SpeciesName = GetSpeciesNameForInstance(Mon);
        if (SpeciesName.IsNone())
        {
            return;
        }

        const FGF_SpeciesSummary Summary = GetSpeciesSummary(SpeciesName);
        if (!Summary.bIsValid || Summary.CompendiumNumber <= 0)
        {
            return;
        }

        if (!VaultSystem->HasCaughtSpecies(Summary.CompendiumNumber))
        {
            VaultSystem->MarkSpeciesAsCaught(Summary.CompendiumNumber);
            Added++;

            UE_LOG(LogTemp, Warning, TEXT("Compendium backfill: '%s' (#%d) was owned but not registered - fixed."),
                *SpeciesName.ToString(), Summary.CompendiumNumber);
        }
    };

    for (const FGF_CreatureInstanceData& Mon : VaultSystem->Party)
    {
        RegisterOne(Mon);
    }

    for (const FGF_VaultPage& Vault : VaultSystem->VaultPages)
    {
        for (int32 Slot = 0; Slot < 30; Slot++)
        {
            RegisterOne(Vault.Creature[Slot]);
        }
    }

    if (Added > 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Compendium backfill: added %d missing entr%s. Total caught is now %d. "
                 "(Almost certainly evolutions - EvolveCreature did not register them before this build.)"),
            Added, Added == 1 ? TEXT("y") : TEXT("ies"), VaultSystem->CaughtCreature.Num());
    }

    return Added;
}

int32 UGF_CreatureManagerSubsystem::RepairFixedHPCreature()
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("RepairFixedHPCreature: VaultSystem is null"));
        return 0;
    }

    int32 Fixed = 0;

    auto RepairOne = [this, &Fixed](FGF_CreatureInstanceData& Mon, const FString& Where)
    {
        if (Mon.SpeciesData.IsNull())
        {
            return;
        }

        // Registry lookup, never a load. Asking the species asset directly would mean
        // LoadSynchronous on every stored Creature just to find the handful that matter.
        if (!UGF_CreatureSpeciesData::SpeciesNameHasFixedOneHP(GetSpeciesNameForInstance(Mon)))
        {
            return;
        }

        if (Mon.MaxHP == 1.0f && Mon.CurrentHP <= 1.0f)
        {
            return;
        }

        const float OldMax = Mon.MaxHP;
        const float OldCur = Mon.CurrentHP;

        Mon.MaxHP = 1.0f;

        // CurrentHP has to come down with it or the UI reads "25/1". A downed one stays
        // downed — this repairs a wrong stat, it does not revive anything.
        Mon.CurrentHP = (Mon.CurrentHP > 0.0f) ? 1.0f : 0.0f;

        Fixed++;
        UE_LOG(LogTemp, Warning, TEXT("Fixed-HP repair: %s (%s) had %.0f/%.0f -> now %.0f/%.0f"),
            *Mon.GetDisplayName().ToString(), *Where, OldCur, OldMax, Mon.CurrentHP, Mon.MaxHP);
    };

    for (int32 i = 0; i < VaultSystem->Party.Num(); i++)
    {
        RepairOne(VaultSystem->Party[i], FString::Printf(TEXT("Party %d"), i + 1));
    }

    for (int32 VaultPageIdx = 0; VaultPageIdx < VaultSystem->VaultPages.Num(); VaultPageIdx++)
    {
        for (int32 SlotIdx = 0; SlotIdx < VaultSystem->MaxVaultPageSize; SlotIdx++)
        {
            RepairOne(VaultSystem->VaultPages[VaultPageIdx].Creature[SlotIdx],
                FString::Printf(TEXT("Vault %d Slot %d"), VaultPageIdx + 1, SlotIdx + 1));
        }
    }

    // Same three storage locations RepairAllTraits walks — a Husk parked in the
    // sanctuary is just as wrong as one in the party.
    for (int32 SlotIdx = 0; SlotIdx < VaultSystem->Sanctuary.Slots.Num(); SlotIdx++)
    {
        if (VaultSystem->Sanctuary.Slots[SlotIdx].bOccupied)
        {
            RepairOne(VaultSystem->Sanctuary.Slots[SlotIdx].Mon,
                FString::Printf(TEXT("Sanctuary %d"), SlotIdx + 1));
        }
    }

    if (Fixed > 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Fixed-HP repair: corrected %d Creature carrying an HP stat their species cannot have."), Fixed);
    }

    return Fixed;
}

void UGF_CreatureManagerSubsystem::MarkSpeciesAsSeen(int32 CompendiumNumber)
{
    if (VaultSystem && CompendiumNumber > 0)
    {
        VaultSystem->MarkSpeciesAsSeen(CompendiumNumber);
        MarkDirty();
    }
}

void UGF_CreatureManagerSubsystem::MarkSpeciesAsSeenFromData(UGF_CreatureSpeciesData* SpeciesData)
{
    if (SpeciesData)
    {
        MarkSpeciesAsSeen(SpeciesData->CompendiumNumber);
    }
}

bool UGF_CreatureManagerSubsystem::HasSeenSpecies(int32 CompendiumNumber) const
{
    return VaultSystem && VaultSystem->HasSeenSpecies(CompendiumNumber);
}

int32 UGF_CreatureManagerSubsystem::GetTotalSpeciesSeen() const
{
    return VaultSystem ? VaultSystem->GetTotalSpeciesSeen() : 0;
}

TArray<int32> UGF_CreatureManagerSubsystem::GetAllSeenSpecies() const
{
    return VaultSystem ? VaultSystem->GetAllSeenSpecies() : TArray<int32>();
}

EGF_CompendiumEntryState UGF_CreatureManagerSubsystem::GetCompendiumEntryState(int32 CompendiumNumber) const
{
    if (HasCaughtSpecies(CompendiumNumber)) return EGF_CompendiumEntryState::Caught;
    if (HasSeenSpecies(CompendiumNumber))   return EGF_CompendiumEntryState::Seen;
    return EGF_CompendiumEntryState::Unknown;
}

void UGF_CreatureManagerSubsystem::GiveStartingItems()
{
	if (!Inventory)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot give starting items - Inventory is null"));
		return;
	}

	// Give starting money
	Inventory->Money = 3000;

	// Give starting items using NAMES (not IDs!)
	// Customize these names to match your ItemData assets
	Inventory->AddItemByName(EGF_ItemCategory::Items, FName("Potion"), 5);          // 5x Potion
	Inventory->AddItemByName(EGF_ItemCategory::Cores, FName("Simple Core"), 10);   // 10x Simple Core

	UE_LOG(LogTemp, Log, TEXT("CreatureManager: Gave starting items - ₽3000, 5 Potions, 10 Cores"));

	// IMPORTANT: Save immediately after giving starting items
	MarkDirty();
}

int32 UGF_CreatureManagerSubsystem::GetPlayerMoney() const
{
	return Inventory ? Inventory->Money : 0;
}

bool UGF_CreatureManagerSubsystem::AddPlayerMoney(int32 Amount)
{
	if (!Inventory) return false;
	Inventory->AddMoney(Amount);
	MarkDirty();
	return true;
}

bool UGF_CreatureManagerSubsystem::RemovePlayerMoney(int32 Amount)
{
	if (!Inventory) return false;
	if (!Inventory->RemoveMoney(Amount)) return false;
	MarkDirty();
	return true;
}

//--------------------
// TEACH MOVE SCREEN ("Change Skills")
//--------------------

// True if this move is flagged out of the teach-move screen (unfinished/unshippable moves).
static bool IsSkillExcludedFromPool(const TSoftClassPtr<AGF_SkillDefinition>& Skill)
{
	if (Skill.IsNull())
	{
		return true; // nothing to show for a null move
	}
	if (UClass* SkillClass = Skill.LoadSynchronous())
	{
		if (const AGF_SkillDefinition* CDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>())
		{
			return CDO->bExcludeFromSkillPool;
		}
	}
	return false;
}

TArray<FGF_SkillOption> UGF_CreatureManagerSubsystem::GetAvailableLevelUpSkills(int32 PartyIndex) const
{
	TArray<FGF_SkillOption> Options;

	FGF_CreatureInstanceData Data;
	if (!GetPartyCreatureData(PartyIndex, Data))
	{
		return Options;
	}

	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();
	if (!Species)
	{
		return Options;
	}

	for (const FGF_LearnableSkill& Learn : Species->LearnableSkills)
	{
		// Only moves it could already have learned by leveling up to its current level.
		if (Learn.Skill.IsNull() || Learn.LearnLevel > Data.Level || IsSkillExcludedFromPool(Learn.Skill))
		{
			continue;
		}

		// A learnset can list the same move more than once — show it only once.
		const bool bDuplicate = Options.ContainsByPredicate(
			[&Learn](const FGF_SkillOption& Existing) { return Existing.Skill == Learn.Skill; });
		if (bDuplicate)
		{
			continue;
		}

		FGF_SkillOption Option;
		Option.Skill = Learn.Skill;
		Option.SourceTomeItem = NAME_None;
		Option.LearnLevel = Learn.LearnLevel;
		Option.bAlreadyKnown = Data.KnowsSkill(Learn.Skill);
		Options.Add(Option);
	}

	return Options;
}

TArray<FGF_SkillOption> UGF_CreatureManagerSubsystem::GetAllLevelUpSkills(int32 PartyIndex) const
{
	TArray<FGF_SkillOption> Options;

	FGF_CreatureInstanceData Data;
	if (!GetPartyCreatureData(PartyIndex, Data))
	{
		return Options;
	}

	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();
	if (!Species)
	{
		return Options;
	}

	for (const FGF_LearnableSkill& Learn : Species->LearnableSkills)
	{
		// Whole learnset — no level cap. Still de-duplicate repeated entries.
		if (Learn.Skill.IsNull() || IsSkillExcludedFromPool(Learn.Skill))
		{
			continue;
		}

		const bool bDuplicate = Options.ContainsByPredicate(
			[&Learn](const FGF_SkillOption& Existing) { return Existing.Skill == Learn.Skill; });
		if (bDuplicate)
		{
			continue;
		}

		FGF_SkillOption Option;
		Option.Skill = Learn.Skill;
		Option.SourceTomeItem = NAME_None;
		Option.LearnLevel = Learn.LearnLevel;
		Option.bAlreadyKnown = Data.KnowsSkill(Learn.Skill);
		Options.Add(Option);
	}

	// Sort by learn level ascending so the list reads like a level-up progression.
	Options.Sort([](const FGF_SkillOption& A, const FGF_SkillOption& B)
	{
		return A.LearnLevel < B.LearnLevel;
	});

	return Options;
}

TArray<FGF_SkillOption> UGF_CreatureManagerSubsystem::GetAvailableTomeMoves(int32 PartyIndex) const
{
	TArray<FGF_SkillOption> Options;

	FGF_CreatureInstanceData Data;
	if (!GetPartyCreatureData(PartyIndex, Data))
	{
		return Options;
	}

	UGF_CreatureSpeciesData* Species = Data.SpeciesData.LoadSynchronous();
	if (!Species || !Inventory)
	{
		return Options;
	}

	// Start from the Tomes the player actually owns — this is what makes the tab
	// "only Tomes you have" rather than the full compatibility list.
	const TArray<FGF_ItemInstance> OwnedTMs = Inventory->GetItemsInCategory(EGF_ItemCategory::Tomes);
	for (const FGF_ItemInstance& TomeItem : OwnedTMs)
	{
		if (TomeItem.Quantity <= 0)
		{
			continue;
		}

		UGF_ItemData* ItemData = GetItemDataFromInstance(TomeItem);
		if (!ItemData || ItemData->TeachableSkill.IsNull())
		{
			continue;
		}

		// HMs are handled by the automated field-move system, not taught here.
		if (ItemData->bIsHM)
		{
			continue;
		}

		const TSoftClassPtr<AGF_SkillDefinition> TomeMove(ItemData->TeachableSkill.ToSoftObjectPath());

		// Unfinished/unshippable moves never appear, even if a Tome item points at one.
		if (IsSkillExcludedFromPool(TomeMove))
		{
			continue;
		}

		// Compatibility: either explicitly listed for this species, or a universal move.
		bool bCompatible = Species->LearnableTomeMoves.Contains(TomeMove);
		if (!bCompatible)
		{
			if (UClass* SkillClass = TomeMove.LoadSynchronous())
			{
				if (const AGF_SkillDefinition* CDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>())
				{
					bCompatible = CDO->bUniversalTM;
				}
			}
		}

		if (!bCompatible)
		{
			continue;
		}

		// Two different owned Tomes could teach the same move — list it once.
		const bool bDuplicate = Options.ContainsByPredicate(
			[&TomeMove](const FGF_SkillOption& Existing) { return Existing.Skill == TomeMove; });
		if (bDuplicate)
		{
			continue;
		}

		FGF_SkillOption Option;
		Option.Skill = TomeMove;
		Option.SourceTomeItem = ItemData->ItemName;
		Option.bAlreadyKnown = Data.KnowsSkill(TomeMove);
		Options.Add(Option);
	}

	return Options;
}

bool UGF_CreatureManagerSubsystem::TeachSkillToPartyCreature(int32 PartyIndex, TSoftClassPtr<AGF_SkillDefinition> Skill, int32 ReplaceSlotIndex)
{
	if (Skill.IsNull())
	{
		return false;
	}

	FGF_CreatureInstanceData Data;
	if (!GetPartyCreatureData(PartyIndex, Data))
	{
		return false;
	}

	// No duplicate moves — mirrors the greyed-out "Known" entries in the UI.
	if (Data.KnowsSkill(Skill))
	{
		return false;
	}

	// Backstop: never teach a move that's been pulled from the pool (unfinished moves).
	if (IsSkillExcludedFromPool(Skill))
	{
		return false;
	}

	// Resolve the move's max Uses from its default object so we can refill the slot.
	int32 NewMaxUses = 10;
	if (UClass* SkillClass = Skill.LoadSynchronous())
	{
		if (const AGF_SkillDefinition* CDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>())
		{
			NewMaxUses = CDO->MaxUses;
		}
	}

	// Make sure the parallel Uses arrays line up with Skills before we index into them.
	Data.NormalizeUses();

	if (Data.Skills.IsValidIndex(ReplaceSlotIndex))
	{
		// Replace the picked slot and restore its Uses to max.
		Data.Skills[ReplaceSlotIndex] = Skill;
		Data.MaxUses[ReplaceSlotIndex] = NewMaxUses;
		Data.CurrentUses[ReplaceSlotIndex] = NewMaxUses;
	}
	else if (Data.CanLearnMoreSkills())
	{
		// No existing move in that slot and there's room — append into the empty slot.
		Data.Skills.Add(Skill);
		Data.MaxUses.Add(NewMaxUses);
		Data.CurrentUses.Add(NewMaxUses);
	}
	else
	{
		// Full moveset and no valid slot to replace.
		return false;
	}

	// Write back into the party array — the struct is a value type, so this is what persists.
	return UpdatePartyCreatureData(PartyIndex, Data);
}

bool UGF_CreatureManagerSubsystem::SwapPartyCreatureSkillSlots(int32 PartyIndex, int32 SlotA, int32 SlotB)
{
	if (SlotA == SlotB)
	{
		return true; // no-op, but not a failure
	}

	FGF_CreatureInstanceData Data;
	if (!GetPartyCreatureData(PartyIndex, Data))
	{
		return false;
	}

	// Keep the parallel Uses arrays aligned before we move anything.
	Data.NormalizeUses();

	if (!Data.Skills.IsValidIndex(SlotA) || !Data.Skills.IsValidIndex(SlotB))
	{
		return false;
	}

	// Swap the move and its Uses together so Uses stays attached to its move.
	Data.Skills.Swap(SlotA, SlotB);
	Data.CurrentUses.Swap(SlotA, SlotB);
	Data.MaxUses.Swap(SlotA, SlotB);

	return UpdatePartyCreatureData(PartyIndex, Data);
}

FGF_SkillDisplayInfo UGF_CreatureManagerSubsystem::GetSkillDisplayInfo(TSoftClassPtr<AGF_SkillDefinition> Skill)
{
	FGF_SkillDisplayInfo Info;

	if (Skill.IsNull())
	{
		return Info;
	}

	UClass* SkillClass = Skill.LoadSynchronous();
	if (!SkillClass)
	{
		return Info;
	}

	const AGF_SkillDefinition* CDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
	if (!CDO)
	{
		return Info;
	}

	Info.bValid = true;
	Info.Name = CDO->Name;
	Info.Description = CDO->Description;
	Info.Type = CDO->Type;
	Info.Category = CDO->Split;
	Info.Power = CDO->Power;
	Info.Accuracy = CDO->Accuracy;
	Info.MaxUses = CDO->MaxUses;
	Info.Priority = CDO->Priority;

	return Info;
}

UGF_ItemData* UGF_CreatureManagerSubsystem::GetItemDataFromInstance(const FGF_ItemInstance& ItemInstance) const
{
	UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();
	if (!DataMgr)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: ItemDataManager not found!"));
		return nullptr;
	}

	// Look up by name (preferred)
	if (ItemInstance.ItemName != NAME_None)
	{
		return DataMgr->GetItemByName(ItemInstance.ItemName);
	}

	// Fallback to ID
	if (ItemInstance.ItemID > 0)
	{
		return DataMgr->GetItemByID(ItemInstance.ItemID);
	}

	UE_LOG(LogTemp, Warning, TEXT("CreatureManager: ItemInstance has no valid name or ID!"));
	return nullptr;
}

//--------------------
// HEALING ITEMS (OUT OF BATTLE)
//--------------------

int32 UGF_CreatureManagerSubsystem::CalculateHealAmount(UGF_ItemData* Item, const FGF_CreatureInstanceData& Creature) const
{
	if (!Item)
	{
		return 0;
	}

	// Revives only work on downed Creature; all other heal types only work on non-downed Creature.
	const bool bIsDowned = (Creature.CurrentHP <= 0.f);

	if (Item->ItemType == EGF_ItemType::Revive)
	{
		if (!bIsDowned)
		{
			// Can't use a Revive on a healthy Creature.
			return 0;
		}

		// Percentage-based revive (Revive = 50 %, Max Revive = 100 %)
		const int32 RestoreAmount = FMath::FloorToInt(Creature.MaxHP * Item->HPRestorePercentage / 100.f);
		return FMath::Max(RestoreAmount, 1); // Always restore at least 1 HP
	}

	// HP-restoring berries (Oran, Vigor, ...) are authored as EGF_ItemType::Berry with their
	// restore value in HPRestoreAmount or the held-item field BerryHPRestore. Used from the
	// bag they behave exactly like a Potion, so route them through the same path.
	const bool bIsHPRestoreBerry =
		Item->ItemType == EGF_ItemType::Berry &&
		(Item->bRestorePercentage || Item->HPRestoreAmount > 0 || Item->BerryHPRestore > 0);

	if (Item->ItemType == EGF_ItemType::HPRestore || bIsHPRestoreBerry)
	{
		if (bIsDowned)
		{
			// Normal heal items can't revive downed Creature.
			return 0;
		}

		const float MissingHP = Creature.MaxHP - Creature.CurrentHP;
		if (MissingHP <= 0.f)
		{
			// Already at full HP — nothing to heal.
			return 0;
		}

		int32 RestoreAmount = 0;
		if (Item->bRestorePercentage)
		{
			// Percentage heal (e.g. Max Potion = 100 %)
			RestoreAmount = FMath::FloorToInt(Creature.MaxHP * Item->HPRestorePercentage / 100.f);
		}
		else
		{
			// Flat heal (e.g. Potion = 20, Major Salve = 50, Mend Berry = 10).
			// Berries that only fill in the held-item field fall back to it.
			RestoreAmount = (Item->HPRestoreAmount > 0) ? Item->HPRestoreAmount : Item->BerryHPRestore;
		}

		// Cap at the missing HP so we never overheal.
		return FMath::Min(RestoreAmount, FMath::FloorToInt(MissingHP));
	}

	// Item is not a healing type.
	return 0;
}

int32 UGF_CreatureManagerSubsystem::ApplyHealingItem(UGF_ItemData* Item, FGF_CreatureInstanceData& Creature, bool bConsumeItem)
{
	if (!Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyHealingItem: null Item passed."));
		return 0;
	}

	const int32 ActualHeal = CalculateHealAmount(Item, Creature);
	if (ActualHeal <= 0)
	{
		return 0;
	}

	// Apply the heal.
	Creature.CurrentHP = FMath::Clamp(Creature.CurrentHP + static_cast<float>(ActualHeal), 0.f, Creature.MaxHP);

	// Consume one from inventory if requested.
	if (bConsumeItem && Inventory)
	{
		Inventory->RemoveItemByName(Item->Category, Item->ItemName, 1);
	}

	MarkDirty();

	UE_LOG(LogTemp, Log, TEXT("ApplyHealingItem: [%s] restored %d HP to %s (now %.0f / %.0f)."),
		*Item->ItemName.ToString(), ActualHeal, *Creature.Nickname.ToString(),
		Creature.CurrentHP, Creature.MaxHP);

	return ActualHeal;
}

//--------------------
// VITAMINS (disabled -- see CalculateVitaminGain)
//--------------------

namespace
{
	// Member pointer so the same lookup works for const and non-const Creature
	int32 FGF_CreatureInstanceData::* GetTrainingMemberForVitamin(EGF_VitaminStat Stat)
	{
		switch (Stat)
		{
		case EGF_VitaminStat::HP:             return &FGF_CreatureInstanceData::HP_EP;
		case EGF_VitaminStat::Attack:         return &FGF_CreatureInstanceData::Attack_EP;
		case EGF_VitaminStat::Defense:        return &FGF_CreatureInstanceData::Defense_EP;
		case EGF_VitaminStat::Magic:  return &FGF_CreatureInstanceData::Magic_EP;
		case EGF_VitaminStat::Poise: return &FGF_CreatureInstanceData::Poise_EP;
		case EGF_VitaminStat::Speed:          return &FGF_CreatureInstanceData::Speed_EP;
		}
		return nullptr;
	}
}

int32 UGF_CreatureManagerSubsystem::CalculateVitaminGain(UGF_ItemData* Item, const FGF_CreatureInstanceData& Creature) const
{
	if (!Item || Item->ItemType != EGF_ItemType::Vitamin)
	{
		return 0;
	}

	// Dokimon: EPs are earned one per level and freely re-allocated, and their total
	// is pinned to the level (FGF_CreatureInstanceData::GetEPBudget). A vitamin adding
	// points on top would break that, so vitamins currently have no effect -- the
	// item use reports "It won't have any effect." Give them a new job before
	// putting any in the game.
	return 0;
}

int32 UGF_CreatureManagerSubsystem::ApplyVitaminItem(UGF_ItemData* Item, FGF_CreatureInstanceData& Creature, bool bConsumeItem)
{
	const int32 ActualGain = CalculateVitaminGain(Item, Creature);
	if (ActualGain <= 0)
	{
		return 0;
	}

	int32 FGF_CreatureInstanceData::* TrainingMember = GetTrainingMemberForVitamin(Item->VitaminStat);
	Creature.*TrainingMember += ActualGain;

	// HP Up changes MaxHP right away. Raise CurrentHP by the same amount so the
	// missing-HP gap stays constant (like a level up); downed Creature stay at 0.
	if (Item->VitaminStat == EGF_VitaminStat::HP)
	{
		if (UGF_CreatureSpeciesData* Species = Creature.SpeciesData.LoadSynchronous())
		{
			const bool bWasDowned = Creature.CurrentHP <= 0.f;
			const float OldMaxHP = Creature.MaxHP;

			Creature.MaxHP = FMath::FloorToInt(
				((2.0f * Species->BaseStats.HP + Creature.HP_AP + Creature.HP_EP) * Creature.Level) / 100.0f
			) + Creature.Level + 10;

			// Husk: see UGF_CreatureSpeciesData::HasFixedOneHP. HP Up still banks the EP
			// (so the value is there if it is ever traded into something else), it just
			// buys no HP.
			if (Species->HasFixedOneHP())
			{
				Creature.MaxHP = 1;
			}

			if (!bWasDowned)
			{
				Creature.CurrentHP = FMath::Clamp(Creature.CurrentHP + (Creature.MaxHP - OldMaxHP), 1.f, Creature.MaxHP);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ApplyVitaminItem: Could not load SpeciesData for %s - MaxHP not updated."),
				*Creature.Nickname.ToString());
		}
	}

	// Consume one from inventory if requested.
	if (bConsumeItem && Inventory)
	{
		Inventory->RemoveItemByName(Item->Category, Item->ItemName, 1);
	}

	MarkDirty();

	UE_LOG(LogTemp, Log, TEXT("ApplyVitaminItem: [%s] added %d EP to %s (stat %d, now %d)."),
		*Item->ItemName.ToString(), ActualGain, *Creature.GetDisplayName().ToString(),
		static_cast<int32>(Item->VitaminStat), Creature.*TrainingMember);

	return ActualGain;
}

int32 UGF_CreatureManagerSubsystem::ApplyHealingItemToParty(int32 PartyIndex, UGF_ItemData* Item, bool bConsumeItem)
{
	FGF_CreatureInstanceData Creature;
	if (!GetPartyCreatureData(PartyIndex, Creature))
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyHealingItemToParty: no Creature at party index %d."), PartyIndex);
		return 0;
	}

	const int32 ActualHeal = ApplyHealingItem(Item, Creature, bConsumeItem);
	if (ActualHeal > 0)
	{
		UpdatePartyCreatureData(PartyIndex, Creature);
	}
	return ActualHeal;
}

int32 UGF_CreatureManagerSubsystem::ApplyVitaminToParty(int32 PartyIndex, UGF_ItemData* Item, bool bConsumeItem)
{
	FGF_CreatureInstanceData Creature;
	if (!GetPartyCreatureData(PartyIndex, Creature))
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyVitaminToParty: no Creature at party index %d."), PartyIndex);
		return 0;
	}

	const int32 ActualGain = ApplyVitaminItem(Item, Creature, bConsumeItem);
	if (ActualGain > 0)
	{
		UpdatePartyCreatureData(PartyIndex, Creature);
	}
	return ActualGain;
}

int32 UGF_CreatureManagerSubsystem::ApplyGrowthCandyToParty(int32 PartyIndex, UGF_ItemData* Item, bool bConsumeItem)
{
	if (!Item || Item->ItemType != EGF_ItemType::LevelBoost)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyGrowthCandyToParty: item is null or not a Growth Candy."));
		return 0;
	}

	FGF_CreatureInstanceData Creature;
	if (!GetPartyCreatureData(PartyIndex, Creature))
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyGrowthCandyToParty: no Creature at party index %d."), PartyIndex);
		return 0;
	}

	// "It won't have any effect." — GiveEXP would refuse these anyway, but checking
	// here keeps the item from being consumed and gives the bag UI a clean 0.
	if (Creature.bIsDowned || Creature.Level >= 100)
	{
		return 0;
	}

	// Exactly enough EXP to reach the next level: the Creature lands on the new
	// level with 0 surplus, discarding partial progress like classic Growth Candy.
	const int32 ExpNeeded = GetEXPToNextLevel(PartyIndex);

	bool bLeveledUp = false;
	int32 NewLevel = 0;
	if (!GiveEXP(PartyIndex, ExpNeeded, bLeveledUp, NewLevel) || !bLeveledUp)
	{
		return 0;
	}

	if (bConsumeItem && Inventory)
	{
		Inventory->RemoveItemByName(Item->Category, Item->ItemName, 1);
	}

	MarkDirty();

	UE_LOG(LogTemp, Log, TEXT("ApplyGrowthCandyToParty: %s grew to level %d."),
		*Creature.GetDisplayName().ToString(), NewLevel);

	return NewLevel;
}

//--------------------
// Uses RESTORE (ETHER / ELIXIR / LEPPA BERRY)
//--------------------

int32 UGF_CreatureManagerSubsystem::RestoreSkillUses(int32 PartyIndex, int32 SkillIndex, int32 UsesToRestore, UGF_ItemData* Item, bool bConsumeItem)
{
	FGF_CreatureInstanceData Creature;
	if (!GetPartyCreatureData(PartyIndex, Creature))
	{
		UE_LOG(LogTemp, Warning, TEXT("RestoreSkillUses: no Creature at party index %d."), PartyIndex);
		return 0;
	}

	if (Creature.Skills.Num() == 0)
	{
		return 0;
	}

	// -1 = every move (Elixir), otherwise a single slot (Ether).
	const bool bAllSkills = (SkillIndex < 0);
	if (!bAllSkills && !Creature.Skills.IsValidIndex(SkillIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("RestoreSkillUses: SkillIndex %d out of range (Skills.Num=%d)."), SkillIndex, Creature.Skills.Num());
		return 0;
	}

	// Uses arrays can fall out of sync if moves were added after initialization. CurrentUses and
	// MaxUses go stale INDEPENDENTLY, so repair them separately — a MaxUses of 0 makes every move
	// look "already full" and this node would silently restore nothing.
	// NormalizeUses() would be safe here now that it no longer refills spent Uses, but the
	// per-slot repair below is kept because it also reports OldCurrentUsesNum for the
	// "restored nothing" diagnostics.
	const int32 SkillCount = Creature.Skills.Num();
	const int32 OldCurrentUsesNum = Creature.CurrentUses.Num();

	if (Creature.CurrentUses.Num() < SkillCount) { Creature.CurrentUses.SetNum(SkillCount); }
	if (Creature.MaxUses.Num() < SkillCount) { Creature.MaxUses.SetNum(SkillCount); }

	for (int32 Slot = 0; Slot < SkillCount; ++Slot)
	{
		if (Creature.Skills[Slot].IsNull())
		{
			continue;
		}

		const bool bNeedsMaxUses = (Creature.MaxUses[Slot] <= 0);
		const bool bNeedsCurrentUses = (Slot >= OldCurrentUsesNum); // entry we just created, never a real 0
		if (!bNeedsMaxUses && !bNeedsCurrentUses)
		{
			continue;
		}

		int32 SkillUses = 10; // fallback, same as NormalizeUses
		if (TSubclassOf<AGF_SkillDefinition> LoadedClass = Creature.Skills[Slot].LoadSynchronous())
		{
			if (const AGF_SkillDefinition* CDO = LoadedClass->GetDefaultObject<AGF_SkillDefinition>())
			{
				SkillUses = CDO->MaxUses;
			}
		}

		if (bNeedsMaxUses)
		{
			UE_LOG(LogTemp, Warning, TEXT("RestoreSkillUses: move slot %d had MaxUses 0 - repairing to %d from the move CDO."), Slot, SkillUses);
			Creature.MaxUses[Slot] = SkillUses;
		}
		if (bNeedsCurrentUses)
		{
			UE_LOG(LogTemp, Warning, TEXT("RestoreSkillUses: CurrentUses array was short - filling move slot %d with %d."), Slot, SkillUses);
			Creature.CurrentUses[Slot] = SkillUses;
		}
	}

	// 0 or negative = "restore everything" (Max Ether / Max Elixir).
	const bool bFullSalve = (UsesToRestore <= 0);

	const int32 FirstSlot = bAllSkills ? 0 : SkillIndex;
	const int32 LastSlot = bAllSkills ? SkillCount - 1 : SkillIndex;

	int32 TotalRestored = 0;
	for (int32 Slot = FirstSlot; Slot <= LastSlot; ++Slot)
	{
		if (Creature.Skills[Slot].IsNull())
		{
			continue;
		}

		const int32 Missing = Creature.MaxUses[Slot] - Creature.CurrentUses[Slot];
		if (Missing <= 0)
		{
			continue;
		}

		const int32 Restored = bFullSalve ? Missing : FMath::Min(UsesToRestore, Missing);
		Creature.CurrentUses[Slot] += Restored;
		TotalRestored += Restored;
	}

	// Nothing was missing — "It won't have any effect." Leave the item in the bag.
	// Log the Uses the node actually saw, so a wrong PartyIndex / SkillIndex is obvious.
	if (TotalRestored <= 0)
	{
		FString UsesState;
		for (int32 Slot = FirstSlot; Slot <= LastSlot; ++Slot)
		{
			UsesState += FString::Printf(TEXT("[%d] %d/%d  "), Slot, Creature.CurrentUses[Slot], Creature.MaxUses[Slot]);
		}

		UE_LOG(LogTemp, Warning, TEXT("RestoreSkillUses: nothing to restore on %s (party index %d, move index %d) - Uses is already full. Saw: %s"),
			*Creature.GetDisplayName().ToString(), PartyIndex, SkillIndex, *UsesState);

		return 0;
	}

	UpdatePartyCreatureData(PartyIndex, Creature);

	// An Ether used mid-battle has to reach the battle actor too, or the restored move
	// still reads as out of Uses until the next battle.
	PushUsesToBattleActors(ActiveCreatureActors, Creature);

	if (Item && bConsumeItem && Inventory)
	{
		Inventory->RemoveItemByName(Item->Category, Item->ItemName, 1);
	}

	MarkDirty();

	UE_LOG(LogTemp, Log, TEXT("RestoreSkillUses: restored %d Uses to %s (%s)."),
		TotalRestored, *Creature.GetDisplayName().ToString(),
		bAllSkills ? TEXT("all moves") : *FString::Printf(TEXT("move slot %d"), SkillIndex));

	return TotalRestored;
}

//--------------------
// CATCHING SYSTEM
//--------------------

bool UGF_CreatureManagerSubsystem::AttemptCatch(
	const FGF_CreatureInstanceData& WildCreature,
	FName CoreName,
	float StatusModifier,
	const FGF_CatchContext& CatchContext,
	FGF_CatchResult& OutCatchResult,
	FGF_CreatureInstanceData& OutCaughtCreature
)
{
	if (!Inventory || !VaultSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot attempt catch - Inventory or VaultSystem is null!"));
		return false;
	}

	// Check if player has the core
	if (!HasCore(CoreName))
	{
		UE_LOG(LogTemp, Warning, TEXT("CreatureManager: Don't have any %s!"), *CoreName.ToString());
		return false;
	}

	// Get ItemDataManager
	UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();
	if (!DataMgr)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: ItemDataManager not found!"));
		return false;
	}

	// Get core ItemData BY NAME
	UGF_ItemData* CoreData = DataMgr->GetItemByName(CoreName);
	if (!CoreData)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Core '%s' not found in ItemDataManager!"),
			*CoreName.ToString());
		return false;
	}

	// Consume the core BEFORE catch calculation
	if (!UseCore(CoreName))
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Failed to consume core!"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("CreatureManager: Attempting to catch %s with %s..."),
		*WildCreature.GetDisplayName().ToString(), *CoreName.ToString());

	// Calculate catch attempt using CatchingLibrary.
	// The battle supplies turn/terrain info; the Compendium flag is ours to fill in
	// since only this subsystem can see the dex.
	int32 CompendiumCount = VaultSystem->GetTotalSpeciesCaught();

	FGF_CatchContext EffectiveContext = CatchContext;
	EffectiveContext.bSpeciesAlreadyCaught =
		HasCaughtSpeciesFromData(WildCreature.SpeciesData.LoadSynchronous());

	OutCatchResult = UGF_CatchingLibrary::CalculateCatchAttempt(
		WildCreature,
		CoreData,
		EffectiveContext,
		StatusModifier,
		CompendiumCount
	);

	// If caught, add to party/box
	if (OutCatchResult.bCaught)
	{
		// Copy wild Creature data
		OutCaughtCreature = WildCreature;
		OutCaughtCreature.CaughtCoreName = CoreName;

		UE_LOG(LogTemp, Warning, TEXT("AttemptCatch: Set CaughtCoreName to '%s'"),
        *OutCaughtCreature.CaughtCoreName.ToString());

		// Add to storage
		if (AddCaughtCreature(OutCaughtCreature))
		{
			// Mark species as caught in Compendium
			if (WildCreature.SpeciesData)
			{
				MarkSpeciesAsCaughtFromData(WildCreature.SpeciesData.Get());
			}

			// Mark dirty only — actual save happens in EndBattle once the
			// player is back in the overworld so the saved location is correct.
			MarkDirty();

			UE_LOG(LogTemp, Log, TEXT("CreatureManager: Successfully caught %s! (Shakes: %d, Critical: %s)"),
				*OutCaughtCreature.GetDisplayName().ToString(),
				OutCatchResult.ShakeCount,
				OutCatchResult.bCriticalCapture ? TEXT("YES") : TEXT("NO"));

			return true;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("CreatureManager: Caught Creature but failed to add to storage!"));
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("CreatureManager: %s broke free after %d shakes!"),
			*WildCreature.GetDisplayName().ToString(), OutCatchResult.ShakeCount);

		// Creature broke free - don't save
		return false;
	}
}

bool UGF_CreatureManagerSubsystem::HasCore(FName CoreName) const
{
	if (!Inventory)
		return false;

	return Inventory->HasItemByName(EGF_ItemCategory::Cores, CoreName, 1);
}

int32 UGF_CreatureManagerSubsystem::GetCoreCount(FName CoreName) const
{
	if (!Inventory)
		return 0;

	return Inventory->GetItemQuantityByName(EGF_ItemCategory::Cores, CoreName);
}

bool UGF_CreatureManagerSubsystem::UseCore(FName CoreName)
{
	if (!Inventory)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot use core - Inventory is null!"));
		return false;
	}

	bool bSuccess = Inventory->RemoveItemByName(EGF_ItemCategory::Cores, CoreName, 1);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("CreatureManager: Used 1x %s (Remaining: %d)"),
			*CoreName.ToString(), GetCoreCount(CoreName));
	}

	return bSuccess;
}

TArray<FGF_ItemInstance> UGF_CreatureManagerSubsystem::GetAvailableCores() const
{
	if (!Inventory)
		return TArray<FGF_ItemInstance>();

	return Inventory->GetItemsInCategory(EGF_ItemCategory::Cores);
}

bool UGF_CreatureManagerSubsystem::AddCaughtCreature(const FGF_CreatureInstanceData& CaughtCreature, bool bSendToVault)
{
	UGF_CreatureSpeciesData* Species = CaughtCreature.SpeciesData.LoadSynchronous();

	if (!VaultSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot add caught Creature - VaultSystem is null!"));
		return false;
	}

	if (!CaughtCreature.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot add invalid Creature!"));
		return false;
	}

	LastCaughtVaultPageIndex = -1;  // reset; -1 means "went to party"

	// Stamp the origin memo. NOTHING on the catch path was doing this: AttemptCatch copies
	// the wild Creature and sets CaughtCoreName, and a wild Creature is never stamped, so every
	// Creature the player caught arrived with MetDate == 0 -- which UGF_CreatureMemoLibrary reads
	// as "no origin" and reports as "Where this Creature met you is a mystery." That is why it
	// was EVERY caught Creature and not just some: the gift path (GiveCreatureTracked) stamps,
	// eggs stamp on hatch, and catching was the one funnel with no stamp at all.
	//
	// Here rather than in AttemptCatch because this is the single choke point every caught
	// Creature passes through, party or box. IfUnset, so anything that already carries a real
	// memo (a traded Creature routed through here) keeps the place it was actually met.
	FGF_CreatureInstanceData Stamped = CaughtCreature;
	UGF_CreatureMemoLibrary::StampMetInfoIfUnset(this, Stamped, EGF_CreatureMetType::Caught);

	// Try to add to party first (unless forced to box)
	if (!bSendToVault && !VaultSystem->IsPartyFull())
	{
		if (VaultSystem->AddToParty(Stamped))
		{
			if (Species)
			{
				MarkSpeciesAsCaught(Species->CompendiumNumber);
			}


			UE_LOG(LogTemp, Log, TEXT("CreatureManager: Added %s to party (Slot %d)"),
				*CaughtCreature.GetDisplayName().ToString(), VaultSystem->GetPartySize() - 1);
			return true;
		}
	}

	// Party full or forced to box - find first box with space
	for (int32 VaultPageIndex = 0; VaultPageIndex < VaultSystem->VaultPages.Num(); VaultPageIndex++)
	{
		if (VaultSystem->GetVaultCreatureCount(VaultPageIndex) < VaultSystem->MaxVaultPageSize)
		{
			if (VaultSystem->AddToVault(VaultPageIndex, Stamped))
			{
				LastCaughtVaultPageIndex = VaultPageIndex;
				if (Species)
					{
						MarkSpeciesAsCaught(Species->CompendiumNumber);
					}

				UE_LOG(LogTemp, Log, TEXT("CreatureManager: Added %s to Vault %d"),
					*CaughtCreature.GetDisplayName().ToString(), VaultPageIndex + 1);
				return true;
			}
		}
	}






	// All boxes full!
	UE_LOG(LogTemp, Error, TEXT("CreatureManager: Cannot add %s - All boxes are full!"),
		*CaughtCreature.GetDisplayName().ToString());
	return false;
}

FString UGF_CreatureManagerSubsystem::GetLastCaughtVaultPageName() const
{
	if (!VaultSystem || LastCaughtVaultPageIndex < 0)
		return FString();

	return VaultSystem->GetVaultPageName(LastCaughtVaultPageIndex);
}

bool UGF_CreatureManagerSubsystem::HasStorageSpace() const
{
	if (!VaultSystem)
		return false;

	// Check if party has space
	if (!VaultSystem->IsPartyFull())
		return true;

	// Check if any box has space
	for (int32 VaultPageIndex = 0; VaultPageIndex < VaultSystem->VaultPages.Num(); VaultPageIndex++)
	{
		if (VaultSystem->GetVaultCreatureCount(VaultPageIndex) < VaultSystem->MaxVaultPageSize)
			return true;
	}

	return false;
}

float UGF_CreatureManagerSubsystem::GetCatchProbability(
	const FGF_CreatureInstanceData& WildCreature,
	FName CoreName,
	const FGF_CatchContext& CatchContext,
	float StatusModifier
) const
{
	// Get ItemDataManager
	UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();
	if (!DataMgr)
		return 0.0f;

	// Get core data
	UGF_ItemData* CoreData = DataMgr->GetItemByName(CoreName);
	if (!CoreData)
		return 0.0f;

	// Calculate probability using CatchingLibrary.
	// Same as AttemptCatch: the dex flag is ours to supply, the rest comes from the caller.
	FGF_CatchContext EffectiveContext = CatchContext;
	EffectiveContext.bSpeciesAlreadyCaught =
		HasCaughtSpeciesFromData(WildCreature.SpeciesData.LoadSynchronous());

	return UGF_CatchingLibrary::GetCatchProbabilityPercent(
		WildCreature, CoreData, EffectiveContext, StatusModifier);
}

//--------------------
// CORE VISUAL HELPERS
//--------------------

UGF_ItemData* UGF_CreatureManagerSubsystem::GetCaughtCoreData(const FGF_CreatureInstanceData& Creature) const
{
    // Get ItemDataManager from GameInstance subsystem
    UGF_ItemDataManager* DataMgr = GetGameInstance()->GetSubsystem<UGF_ItemDataManager>();
    if (!DataMgr)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: ItemDataManager is null!"));
        return nullptr;
    }

	 UE_LOG(LogTemp, Warning, TEXT("GetCaughtCoreData: Looking for ball named '%s'"),
        *Creature.CaughtCoreName.ToString());


    // Get the ball's ItemData
    UGF_ItemData* CoreData = DataMgr->GetItemByName(Creature.CaughtCoreName);

    if (!CoreData)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreatureManager: Could not find core '%s', using Simple Core fallback"),
            *Creature.CaughtCore.ToString());

        // Fallback to the baseline core. This must match the ItemName on the
        // Simple Core data asset, or every unresolved core yields a null flipbook.
        CoreData = DataMgr->GetItemByName(FName("Simple Core"));
    }

	if (!CoreData)
        {
            // ✅ ADD THIS DEBUG LOG
            UE_LOG(LogTemp, Error, TEXT("GetCaughtCoreData: Even Core fallback failed!"));
        }

	   else
    {
        UE_LOG(LogTemp, Log, TEXT("GetCaughtCoreData: Found ball '%s' successfully!"),
            *CoreData->DisplayName.ToString());
    }

    return CoreData;
}

UPaperFlipbook* UGF_CreatureManagerSubsystem::GetCaughtCoreThrowAnimation(const FGF_CreatureInstanceData& Creature) const
{
    UGF_ItemData* CoreData = GetCaughtCoreData(Creature);
    if (!CoreData)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: No ball data found for throw animation!"));
        return nullptr;
    }

    return CoreData->ThrowFlipbook;
}

UPaperFlipbook* UGF_CreatureManagerSubsystem::GetCaughtCoreOpenAnimation(const FGF_CreatureInstanceData& Creature) const
{
    UGF_ItemData* CoreData = GetCaughtCoreData(Creature);
    if (!CoreData)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: No ball data found for open animation!"));
        return nullptr;
    }

    return CoreData->OpenFlipbook;
}

UPaperSprite* UGF_CreatureManagerSubsystem::GetCaughtCoreIcon(const FGF_CreatureInstanceData& Creature) const
{
    UGF_ItemData* CoreData = GetCaughtCoreData(Creature);
    if (!CoreData)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: No ball data found for icon!"));
        return nullptr;
    }

    return CoreData->IdleCoreSprite;
}

//--------------------
// BATTLE HELPERS
//--------------------

TArray<int32> UGF_CreatureManagerSubsystem::GetHealthyCreatureIndices() const
{
    TArray<int32> HealthyIndices;

    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));
        return HealthyIndices;
    }

    const TArray<FGF_CreatureInstanceData>& Party = VaultSystem->Party;

    for (int32 i = 0; i < Party.Num(); i++)
    {
        // Eggs occupy a party slot but are never battle-eligible.
        if (!Party[i].bIsEgg && !Party[i].bIsDowned && Party[i].CurrentHP > 0)
        {
            HealthyIndices.Add(i);
        }
    }

    if (HealthyIndices.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreatureManager: No healthy Creature found! Party wiped?"));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("CreatureManager: Found %d healthy Creature: %s"),
            HealthyIndices.Num(), *FString::JoinBy(HealthyIndices, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));
    }

    return HealthyIndices;
}

//--------------------
// BOX-ONLY ACCESS (WRAPPER FUNCTIONS)
//--------------------

bool UGF_CreatureManagerSubsystem::GetAllCreaturesInVaultPage(int32 VaultPageIndex, TArray<FGF_CreatureInstanceData>& OutCreature) const
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));
        return false;
    }

    OutCreature = VaultSystem->GetAllCreaturesInVaultPage(VaultPageIndex);
	return VaultSystem->IsValidVaultPageIndex(VaultPageIndex);
}

bool UGF_CreatureManagerSubsystem::GetCreatureAtSlot(int32 VaultPageIndex, int32 SlotIndex, FGF_CreatureInstanceData& OutData) const
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));
        return false;
    }

    return VaultSystem->GetCreatureAtSlot(VaultPageIndex, SlotIndex, OutData);
}

bool UGF_CreatureManagerSubsystem::IsSlotEmpty(int32 VaultPageIndex, int32 SlotIndex) const
{
    if (!VaultSystem)
    {
        return true;
    }

    return VaultSystem->IsSlotEmpty(VaultPageIndex, SlotIndex);
}

bool UGF_CreatureManagerSubsystem::GetBoxCreatureMovesWithPP(
    int32 VaultPageIndex,
    int32 SlotIndex,
    TArray<TSubclassOf<AGF_SkillDefinition>>& OutSkills,
    TArray<int32>& OutCurrentUses,
    TArray<int32>& OutMaxUses)
{
    OutSkills.Empty();
    OutCurrentUses.Empty();
    OutMaxUses.Empty();

    // Get box system - FIX: Access it correctly
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GetBoxCreatureMovesWithPP: Vault system not initialized"));
        return false;
    }

    // Get Creature data
    FGF_CreatureInstanceData CreatureData;
    if (!VaultSystem->GetCreatureAtSlot(VaultPageIndex, SlotIndex, CreatureData))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetBoxCreatureMovesWithPP: No Creature at Vault %d Slot %d"),
            VaultPageIndex, SlotIndex);
        return false;
    }

    // Check if Creature has moves
    if (CreatureData.Skills.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetBoxCreatureMovesWithPP: Creature has no moves"));
        return false;
    }

    // Load each move and convert from soft reference to hard reference
    for (int32 i = 0; i < CreatureData.Skills.Num(); i++)
    {
        TSoftClassPtr<AGF_SkillDefinition> SoftMove = CreatureData.Skills[i];

        if (SoftMove.IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("GetBoxCreatureMovesWithPP: Skill %d is null"), i);
            continue;
        }

        // Load the move synchronously (for UI, this is fine)
        TSubclassOf<AGF_SkillDefinition> LoadedSkill = SoftMove.LoadSynchronous();

        if (LoadedSkill)
        {
            OutSkills.Add(LoadedSkill);

            // Add Uses if available
            if (CreatureData.CurrentUses.IsValidIndex(i) && CreatureData.MaxUses.IsValidIndex(i))
            {
                OutCurrentUses.Add(CreatureData.CurrentUses[i]);
                OutMaxUses.Add(CreatureData.MaxUses[i]);
            }
            else
            {
                // Default Uses if not set
                OutCurrentUses.Add(10);
                OutMaxUses.Add(10);
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("GetBoxCreatureMovesWithPP: Failed to load move %d"), i);
        }
    }

    return OutSkills.Num() > 0;
}



TArray<TSubclassOf<AGF_SkillDefinition>> UGF_CreatureManagerSubsystem::GetVaultCreatureSkills(
    int32 VaultPageIndex,
    int32 SlotIndex)
{
    TArray<TSubclassOf<AGF_SkillDefinition>> Skills;
    TArray<int32> DummyCurrentUses;
    TArray<int32> DummyMaxUses;

    GetBoxCreatureMovesWithPP(VaultPageIndex, SlotIndex, Skills, DummyCurrentUses, DummyMaxUses);

    return Skills;
}


//--------------------
// BOX SWAPPING (WRAPPER FUNCTIONS)
//--------------------

bool UGF_CreatureManagerSubsystem::SwapCreatureInVault(int32 VaultPageIndex, int32 SlotA, int32 SlotB)
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));
        return false;
    }

    bool bSuccess = VaultSystem->SwapCreatureInVault(VaultPageIndex, SlotA, SlotB);

    if (bSuccess)
    {
        // Auto-save after swapping
        MarkDirty();
    }

    return bSuccess;
}

bool UGF_CreatureManagerSubsystem::SwapCreatureBetweenVaultPages(int32 VaultPageA, int32 SlotA, int32 VaultPageB, int32 SlotB)
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));
        return false;
    }

    bool bSuccess = VaultSystem->SwapCreatureBetweenVaultPages(VaultPageA, SlotA, VaultPageB, SlotB);

    if (bSuccess)
    {
        // Auto-save after swapping
        MarkDirty();
    }

    return bSuccess;
}

bool UGF_CreatureManagerSubsystem::SwapPartyWithVault(int32 PartyIndex, int32 VaultPageIndex, int32 VaultSlot)
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatureManager: VaultSystem is null!"));
        return false;
    }

    bool bSuccess = VaultSystem->SwapPartyWithVault(PartyIndex, VaultPageIndex, VaultSlot);

    if (bSuccess)
    {
        // Auto-save after swapping
        MarkDirty();
        UE_LOG(LogTemp, Log, TEXT("Swapped party Creature %d with box %d slot %d"),
            PartyIndex, VaultPageIndex, VaultSlot);
    }

    return bSuccess;
}

int32 UGF_CreatureManagerSubsystem::GetTotalStoredCreature() const
{
    if (!VaultSystem)
    {
        return 0;
    }

    return VaultSystem->GetTotalStoredCreature();
}

bool UGF_CreatureManagerSubsystem::CanStoreMoreCreature() const
{
    if (!VaultSystem)
    {
        return false;
    }

    return VaultSystem->CanStoreMoreCreature();
}

/////////////////
///EVOLUTION
////////////////

void UGF_CreatureManagerSubsystem::EvolveCreature(FGF_CreatureInstanceData& Creature, FName NewSpecies)
{
    // Guarded here as well as in CheckForEvolution, not instead of it.
    // CheckForEvolution is only the question "may this evolve?"; this is the
    // function that actually does it, and Blueprint calls it directly (see the
    // note below about BP_GF_EvolveCreature). A flag enforced only at the asking
    // end would be trivially bypassed by anything that never asked.
    if (Creature.bCannotEvolve)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("EvolveCreature: '%s' is flagged bCannotEvolve; refusing to evolve it into '%s'."),
            *Creature.GetDisplayName().ToString(), *NewSpecies.ToString());
        return;
    }

    // No need to store APs/EPs separately - they're already in the struct!
    // Just preserve experience
    int32 CurrentExp = Creature.CurrentEXP;

    // Update species reference
    UGF_CreatureSpeciesData* NewSpeciesData = GetCreatureSpeciesData(NewSpecies);
    Creature.SpeciesData = NewSpeciesData;

    // Evolution keeps the trait SLOT, not the trait itself — a slot-0
    // Cubfang (Fleetfoot) becomes a slot-0 Direfang (Menace). Without
    // this the cached Trait from the pre-evolution sticks forever, because
    // GetInstanceTrait() only falls back to the species when it reads None.
    if (NewSpeciesData)
    {
        // Register the evolution in the Compendium. Nothing else on this path does it:
        // MarkSpeciesAsCaught is called when a Creature is CAUGHT or GIVEN, and an
        // evolution is neither, so a player who levelled a Sprigling all the way to
        // Thornwood had two blank dex slots for Creature sitting in their party.
        // Goes here rather than in EvolvePartyCreature/EvolveVaultCreature so the direct
        // EvolveCreature() calls from Blueprint are covered too.
        if (VaultSystem)
        {
            VaultSystem->MarkSpeciesAsCaughtFromData(NewSpeciesData);
            bHasUnsavedChanges = true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("EvolveCreature: no VaultSystem - '%s' will not be registered in the Compendium."),
                *NewSpecies.ToString());
        }

        // Clamp: an evolution with only one trait can't honour slot 1.
        if (Creature.TraitSlot == 1 && NewSpeciesData->Trait2 == EGF_CreatureTrait::None)
        {
            Creature.TraitSlot = 0;
        }

        const EGF_CreatureTrait EvolvedTrait =
            UGF_CreatureTraitLibrary::GetSpeciesTraitBySlot(NewSpeciesData, Creature.TraitSlot);

        if (EvolvedTrait != EGF_CreatureTrait::None)
        {
            Creature.Trait = EvolvedTrait;
        }
    }

    // Recalculate stats with new base stats
    RecalculateStats(Creature);

    // Check for new moves learned on evolution
    CheckEvolutionSkills(Creature);

    // Trigger evolution animation/event
    OnCreatureEvolved.Broadcast(Creature);
}

bool UGF_CreatureManagerSubsystem::CheckForEvolution(const FGF_CreatureInstanceData& Creature, EGF_EvolutionTrigger Trigger)
{
    // Event distributions that are meant to stay in their given form. Checked
    // before anything else so no trigger, condition or level can get past it.
    if (Creature.bCannotEvolve)
    {
        return false;
    }

    // Same trap as GetEvolutionForTrigger: an unloaded soft pointer is not the
    // same as a creature that cannot evolve.
    UGF_CreatureSpeciesData* SpeciesData = Creature.SpeciesData.Get();
    if (!SpeciesData && !Creature.SpeciesData.IsNull())
    {
        SpeciesData = Creature.SpeciesData.LoadSynchronous();
    }

    if (!SpeciesData)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("CheckForEvolution: no species data on this creature (soft path '%s'). "
                 "Reporting false."),
            *Creature.SpeciesData.ToString());
        return false;
    }

    for (const FGF_EvolutionMethod& Evolution : SpeciesData->Evolutions)
    {
        if (Evolution.Trigger != Trigger) continue;

        // Check trigger-specific requirements
        bool bMeetsBasicRequirements = false;
        switch (Trigger)
        {
            case EGF_EvolutionTrigger::Level:
                bMeetsBasicRequirements = Creature.Level >= Evolution.RequiredLevel;
                break;
            case EGF_EvolutionTrigger::Item:
            case EGF_EvolutionTrigger::Trade:
                bMeetsBasicRequirements = true;
                break;
            case EGF_EvolutionTrigger::Affinity:
                bMeetsBasicRequirements = Creature.Affinity >= Evolution.RequiredAffinity;
                break;
        }

        if (!bMeetsBasicRequirements) continue;

        if (!MeetsEvolutionCondition(Creature, Evolution.AdditionalCondition, Evolution.RequiredLocation))
        {
            continue; // Doesn't meet this evolution's condition, try next
        }

        // Found a valid evolution!
        return true;
    }

    return false;
}

bool UGF_CreatureManagerSubsystem::MeetsEvolutionCondition(const FGF_CreatureInstanceData& Creature, EGF_EvolutionCondition Condition, FName RequiredLocation) const
{
    switch (Condition)
    {
        case EGF_EvolutionCondition::None:
            return true; // No additional condition

        case EGF_EvolutionCondition::PersonalityLow:
        {
            // Larvling → Silkpod (personality < 50%)
            // Use CreatureID as personality value (already random)
            int32 PersonalityValue = Creature.CreatureID % 10;
            return PersonalityValue < 5;
        }

        case EGF_EvolutionCondition::PersonalityHigh:
        {
            // Larvling → Shellpod (personality >= 50%)
            int32 PersonalityValue = Creature.CreatureID % 10;
            return PersonalityValue >= 5;
        }

        case EGF_EvolutionCondition::TimeDay:
        {
            // Kithling → Solkit (daytime + affinity)
            // TODO: Implement time of day system
            return true; // Placeholder
        }

        case EGF_EvolutionCondition::TimeNight:
        {
            // Kithling → Umbrakit (nighttime + affinity)
            // TODO: Implement time of day system
            return false; // Placeholder
        }

        case EGF_EvolutionCondition::GenderMale:
            return Creature.Gender == EGF_CreatureGender::Male;

        case EGF_EvolutionCondition::GenderFemale:
            return Creature.Gender == EGF_CreatureGender::Female;

        case EGF_EvolutionCondition::LocationSpecific:
        {
            // Lodeface → Lodecrest (Voltmere Trial), Kithling → Mosskit (Moss Rock), etc.
            // The area the player is standing in comes from the RouteSubsystem, so
            // RequiredLocation must match a UGF_RouteData's RouteName, NOT its DisplayName
            // and NOT the level/map asset name.
            if (RequiredLocation.IsNone())
            {
                // IsValid() is deliberate here, unlike the guards above: this is a log
                // line, and forcing a species load (~35 MB) just to name it would cost
                // more than the degraded message. Reads "Creature" if it is not resident.
                UE_LOG(LogTemp, Warning, TEXT("MeetsEvolutionCondition: %s has a LocationSpecific evolution with an empty RequiredLocation - fill it in on the species asset."),
                    Creature.SpeciesData.IsValid() ? *Creature.SpeciesData->SpeciesName.ToString() : TEXT("Creature"));
                return false;
            }

            const UGF_RouteSubsystem* RouteSys = GetGameInstance() ? GetGameInstance()->GetSubsystem<UGF_RouteSubsystem>() : nullptr;
            if (!RouteSys)
            {
                UE_LOG(LogTemp, Warning, TEXT("MeetsEvolutionCondition: no RouteSubsystem - cannot evaluate LocationSpecific."));
                return false;
            }

            // Fails closed when the player is somewhere with no RouteVolume: we don't
            // know where they are, so we don't hand out a location-gated evolution.
            const FName CurrentLocation = RouteSys->GetCurrentRouteName();
            const bool bMatches = !CurrentLocation.IsNone() && CurrentLocation == RequiredLocation;

            UE_LOG(LogTemp, Log, TEXT("MeetsEvolutionCondition: LocationSpecific needs '%s', player is in '%s' -> %s"),
                *RequiredLocation.ToString(),
                CurrentLocation.IsNone() ? TEXT("<no route set>") : *CurrentLocation.ToString(),
                bMatches ? TEXT("MATCH") : TEXT("no match"));

            return bMatches;
        }

        default:
            return true;
    }
}

bool UGF_CreatureManagerSubsystem::EvolvePartyCreature(int32 PartyIndex, FName NewSpecies)
{
    if (!VaultSystem || !VaultSystem->Party.IsValidIndex(PartyIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("EvolvePartyCreature: Invalid party index %d"), PartyIndex);
        return false;
    }

    // Get REFERENCE to party Creature
    FGF_CreatureInstanceData& PartyCreature = VaultSystem->Party[PartyIndex];

    // Store old name for logging. IsValid() on purpose -- log-only, so it is not worth
    // loading the species to name it; falls back to None when it is not resident.
    FName OldSpecies = PartyCreature.SpeciesData.IsValid() ?
        PartyCreature.SpeciesData->SpeciesName :
        NAME_None;

    // Evolve using the reference (this modifies the actual party Creature)
    EvolveCreature(PartyCreature, NewSpecies);

    // Mark as needing save
    MarkDirty();

    UE_LOG(LogTemp, Log, TEXT("Party Creature %d evolved: %s -> %s"),
        PartyIndex,
        *OldSpecies.ToString(),
        *NewSpecies.ToString());

    return true;
}

FName UGF_CreatureManagerSubsystem::GetEvolutionForTrigger(const FGF_CreatureInstanceData& Creature, EGF_EvolutionTrigger Trigger) const
{
    // Get() on a soft pointer is null whenever the asset is not already in
    // memory, which says nothing about whether the creature can evolve. This
    // used to return None silently, and the caller could not tell "no
    // evolution" from "the species asset was not loaded yet" -- the data on
    // disk was correct the whole time.
    UGF_CreatureSpeciesData* SpeciesData = Creature.SpeciesData.Get();
    if (!SpeciesData && !Creature.SpeciesData.IsNull())
    {
        SpeciesData = Creature.SpeciesData.LoadSynchronous();
    }

    if (!SpeciesData)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GetEvolutionForTrigger: no species data on this creature (soft path '%s'). "
                 "Returning None, which reads as 'cannot evolve'."),
            *Creature.SpeciesData.ToString());
        return NAME_None;
    }

    for (const FGF_EvolutionMethod& Evolution : SpeciesData->Evolutions)
    {
        if (Evolution.Trigger != Trigger) continue;

        // Check basic requirements
        bool bMeetsBasicRequirements = false;
        switch (Trigger)
        {
            case EGF_EvolutionTrigger::Level:
                bMeetsBasicRequirements = Creature.Level >= Evolution.RequiredLevel;
                break;
            case EGF_EvolutionTrigger::Item:
            case EGF_EvolutionTrigger::Trade:
                bMeetsBasicRequirements = true;
                break;
            case EGF_EvolutionTrigger::Affinity:
                bMeetsBasicRequirements = Creature.Affinity >= Evolution.RequiredAffinity;
                break;
        }

        if (!bMeetsBasicRequirements)
        {
            // Say so out loud: a silent None here reads as "the lookup is broken"
            // when the creature is simply not high enough level yet.
            UE_LOG(LogTemp, Warning,
                TEXT("GetEvolutionForTrigger: %s -> %s needs level %d, creature is level %d."),
                *SpeciesData->SpeciesName.ToString(), *Evolution.EvolvedSpecies.ToString(),
                Evolution.RequiredLevel, Creature.Level);
            continue;
        }

        if (!MeetsEvolutionCondition(Creature, Evolution.AdditionalCondition, Evolution.RequiredLocation))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("GetEvolutionForTrigger: %s -> %s failed its additional condition."),
                *SpeciesData->SpeciesName.ToString(), *Evolution.EvolvedSpecies.ToString());
            continue;
        }

        // Found valid evolution!
        return Evolution.EvolvedSpecies;
    }

    return NAME_None;
}

bool UGF_CreatureManagerSubsystem::EvolveVaultCreature(int32 VaultPageIndex, int32 SlotIndex, FName NewSpecies)
{
    // Validate VaultSystem exists
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("EvolveVaultCreature: VaultSystem is null"));
        return false;
    }

    // Validate box index
    if (VaultPageIndex < 0 || VaultPageIndex >= VaultSystem->VaultPages.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("EvolveVaultCreature: Invalid box index %d"), VaultPageIndex);
        return false;
    }

    // Validate slot index (Creature is fixed-size array [30])
    if (SlotIndex < 0 || SlotIndex >= 30)
    {
        UE_LOG(LogTemp, Error, TEXT("EvolveVaultCreature: Invalid slot index %d"), SlotIndex);
        return false;
    }

    // Get REFERENCE to box Creature
    FGF_CreatureInstanceData& VaultCreature = VaultSystem->VaultPages[VaultPageIndex].Creature[SlotIndex];

    // Check if slot is actually occupied (not empty)
    if (VaultCreature.SpeciesData.IsNull())
    {
        UE_LOG(LogTemp, Error, TEXT("EvolveVaultCreature: No Creature at Vault %d Slot %d"), VaultPageIndex, SlotIndex);
        return false;
    }

    // Evolve
    EvolveCreature(VaultCreature, NewSpecies);

    // Mark as needing save
    MarkDirty();

    UE_LOG(LogTemp, Log, TEXT("Vault Creature evolved in Vault %d Slot %d -> %s"),
        VaultPageIndex, SlotIndex, *NewSpecies.ToString());

    return true;
}

bool UGF_CreatureManagerSubsystem::CreateHuskFromSkitterling(int32 SourcePartyIndex, FName CoreItemName, int32& OutHuskPartyIndex)
{
    OutHuskPartyIndex = -1;

    if (!VaultSystem || !VaultSystem->Party.IsValidIndex(SourcePartyIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("CreateHuskFromSkitterling: invalid party index %d"), SourcePartyIndex);
        return false;
    }

    // Husk only ever appears when there is somewhere in the PARTY to put it. No box
    // fallback: a Husk that silently lands in a box is worse than no Husk, because
    // the player is never told the Core was spent.
    if (IsPartyFull())
    {
        UE_LOG(LogTemp, Log, TEXT("CreateHuskFromSkitterling: party is full - no Husk, and the caller must NOT consume a Core."));
        return false;
    }

    UGF_CreatureSpeciesData* HuskSpecies = GetCreatureSpeciesData(FName("Husk"));
    if (!HuskSpecies)
    {
        UE_LOG(LogTemp, Error, TEXT("CreateHuskFromSkitterling: no species asset named 'Husk' - check DA_Husk's SpeciesName."));
        return false;
    }

    // The whole point of this function: Husk is a COPY. Everything the copy
    // constructor carries (Skills, Uses, APs, EPs, Temperament, EXP, OT, met memo, affinity,
    // shininess) is inherited, and only what follows is deliberately changed.
    FGF_CreatureInstanceData Husk = VaultSystem->Party[SourcePartyIndex];

    // Its own identity — sharing CreatureID would make UpdatePartyCreature's identity
    // guard redirect writes meant for one onto the other.
    Husk.CreatureID = FMath::RandRange(0, 999999);
    Husk.UniqueID  = FGuid::NewGuid();
    Husk.Nickname  = NAME_None;   // a nickname belongs to the Skitterling, not the shell

    Husk.SpeciesData = HuskSpecies;

    // Aegis comes from the species, but keep the inherited slot so a future
    // second trait on Husk still lines up.
    if (Husk.TraitSlot == 1 && HuskSpecies->Trait2 == EGF_CreatureTrait::None)
    {
        Husk.TraitSlot = 0;
    }
    const EGF_CreatureTrait HuskTrait =
        UGF_CreatureTraitLibrary::GetSpeciesTraitBySlot(HuskSpecies, Husk.TraitSlot);
    if (HuskTrait != EGF_CreatureTrait::None)
    {
        Husk.Trait = HuskTrait;
    }

    if (HuskSpecies->bIsGenderless)
    {
        Husk.Gender = EGF_CreatureGender::Genderless;
    }

    // Never inherit the held item. If that item is the ball being spent it is about to be
    // consumed off the Skitterling (see the end of this function); if it is anything else it
    // belongs to the Skitterling. Either way the shell arrives empty-handed.
    Husk.HeldItem = NAME_None;

    // Fresh out of the ball: never the active follower, never inheriting the Skitterling's
    // battle damage or status.
    Husk.bIsFollowerOut   = false;
    Husk.StatusCondition  = EGF_STATUSEffect::None;
    Husk.SleepCounter     = 0;
    Husk.bIsDowned       = false;

    // Caught in whichever ball the caller spent. None = keep the Skitterling's ball.
    if (!CoreItemName.IsNone())
    {
        Husk.CaughtCoreName = CoreItemName;
    }

    // Stats come from Husk's bases at the inherited level. RecalculateStats now pins
    // MaxHP to 1 by itself, but it also preserves the source Creature's HP *percentage* —
    // and floor(1 * 0.5) is 0, so a Husk copied off a damaged Skitterling would be born
    // downed. Set both explicitly: a freshly made Husk is always 1/1.
    RecalculateStats(Husk);
    Husk.MaxHP     = 1;
    Husk.CurrentHP = 1;

    // Inheriting an empty move list is not survivable in battle, and it means the
    // Skitterling itself was already broken — say so here rather than at the soft-lock.
    if (Husk.Skills.Num() == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("CreateHuskFromSkitterling: the source Creature in slot %d has NO moves, so Husk inherited none. "
                 "Fix whatever emptied the Skitterling's move list - Husk is only the messenger."),
            SourcePartyIndex);
    }

    if (!AddToParty(Husk))
    {
        UE_LOG(LogTemp, Error, TEXT("CreateHuskFromSkitterling: AddToParty failed even though the party was not full."));
        return false;
    }

    // Report where it actually landed rather than assuming the last slot.
    OutHuskPartyIndex = VaultSystem->Party.IndexOfByPredicate(
        [&Husk](const FGF_CreatureInstanceData& Mon) { return Mon.UniqueID == Husk.UniqueID; });

    // Consume the ball, HERE, so no caller has to remember to.
    //
    // BP_GF_EvolveCreature's CheckForSkitterlingCore finds the ball by reading the Skitterling's
    // HELD item, and nothing was ever clearing it: the Skitterling walked away still holding the
    // ball it paid with, while a separate "Remove Item by Name" took a SECOND ball out of the
    // bag. Consuming the exact item the caller named is the only interpretation that cannot
    // double-charge.
    //
    // Matched exactly on purpose: a caller that spends a ball from the bag instead passes that
    // ball's name, the held item will not match, and an unrelated held item is left alone.
    //
    // After AddToParty, never before: the party-full bail above returns without charging
    // anything, and AddToParty appends (so SourcePartyIndex still names the Skitterling) but may
    // have reallocated, which is why this re-indexes instead of holding a reference.
    if (!CoreItemName.IsNone()
        && VaultSystem->Party.IsValidIndex(SourcePartyIndex)
        && VaultSystem->Party[SourcePartyIndex].HeldItem == CoreItemName)
    {
        VaultSystem->Party[SourcePartyIndex].HeldItem = NAME_None;

        UE_LOG(LogTemp, Log,
            TEXT("CreateHuskFromSkitterling: consumed the held '%s' from slot %d - do NOT also remove one from the bag."),
            *CoreItemName.ToString(), SourcePartyIndex);
    }

    VaultSystem->MarkSpeciesAsCaughtFromData(HuskSpecies);
    MarkDirty();

    // Unconditional summary: a silent run must never be ambiguous, and every field the
    // Husk bug turned on (level, move count, ball) is readable in one line.
    UE_LOG(LogTemp, Warning,
        TEXT("Husk created from slot %d -> party slot %d | Lv %d | %d moves inherited | trait %d slot %d | ball '%s' | unique %d"),
        SourcePartyIndex, OutHuskPartyIndex, Husk.Level, Husk.Skills.Num(),
        static_cast<int32>(Husk.Trait), Husk.TraitSlot,
        *Husk.CaughtCoreName.ToString(), Husk.bIsUnique ? 1 : 0);

    return true;
}

void UGF_CreatureManagerSubsystem::ExecuteTrade(int32 PartyIndex, const FGF_NPCTradeOffer& TradeOffer)
{
    // Validate party and index
    if (!VaultSystem || !VaultSystem->Party.IsValidIndex(PartyIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid party index for trade: %d"), PartyIndex);
        return;
    }

    // Store player's Creature (optional - for trade records)
    FGF_CreatureInstanceData PlayerCreature = VaultSystem->Party[PartyIndex];

    // Get species data for NPC's offered Creature
    UGF_CreatureSpeciesData* OfferedSpeciesData = GetCreatureSpeciesData(TradeOffer.OfferedSpecies);
    if (!OfferedSpeciesData)
    {
        UE_LOG(LogTemp, Error, TEXT("Could not find species data for: %s"), *TradeOffer.OfferedSpecies.ToString());
        return;
    }

    // Decide the received Creature's level: keep the player's Creature's level when
    // retaining, otherwise use the level the NPC offered.
    const int32 ResultLevel = TradeOffer.bRetainPlayerLevelAndGender
        ? PlayerCreature.Level
        : TradeOffer.OfferedLevel;

    // Create NPC's Creature using existing function (rolls fresh APs/nature; stats
    // are recalculated from ResultLevel when the Creature is next spawned)
    FGF_CreatureInstanceData NPCCreature = CreateCreature(OfferedSpeciesData, ResultLevel);

    // Carry over the traded-away Creature's gender, unless the new species can't
    // hold it (genderless species, or the player's mon was itself genderless).
    if (TradeOffer.bRetainPlayerLevelAndGender
        && !OfferedSpeciesData->bIsGenderless
        && PlayerCreature.Gender != EGF_CreatureGender::Genderless)
    {
        NPCCreature.Gender = PlayerCreature.Gender;
    }

    // Set traded Creature properties
    NPCCreature.Nickname = TradeOffer.OfferedNickname;
    NPCCreature.OriginalTamerName = TradeOffer.OfferedTamerName;
    NPCCreature.CurrentTamerName = TradeOffer.OfferedTamerName;
    NPCCreature.OriginalTamerID = TradeOffer.OfferedTamerID;
    NPCCreature.CurrentTamerID = TradeOffer.OfferedTamerID;

    if (!TradeOffer.HeldItem.IsNone())
    {
        NPCCreature.HeldItem = TradeOffer.HeldItem;
    }

    // CreateCreature leaves HP at 0 as a sentinel for actor spawning. Since we're
    // writing straight into the party (no actor spawn), calculate stats now so the
    // Creature is stored with correct MaxHP and full CurrentHP.
    // (Husk's fixed 1 HP is handled inside RecalculateStats.)
    RecalculateStats(NPCCreature);
    NPCCreature.CurrentHP = NPCCreature.MaxHP;

    // Replace in party
    VaultSystem->Party[PartyIndex] = NPCCreature;

    // Check for trade evolution
    if (CheckForEvolution(VaultSystem->Party[PartyIndex], EGF_EvolutionTrigger::Trade))
    {
        // Creature can evolve! Add TRIGGER UI here
        UE_LOG(LogTemp, Log, TEXT("%s can evolve through trade!"), *NPCCreature.GetDisplayName().ToString());
        // TODO: Trigger evolution UI/animation
        // For now, you could auto-evolve or prompt the player
    }

    // Save changes
     MarkDirty();

    UE_LOG(LogTemp, Log, TEXT("Trade complete: Gave %s, received %s"),
        *PlayerCreature.GetDisplayName().ToString(),
        *NPCCreature.GetDisplayName().ToString());
}

void UGF_CreatureManagerSubsystem::CheckEvolutionSkills(FGF_CreatureInstanceData& Creature)
{
    // Get species data
    UGF_CreatureSpeciesData* SpeciesData = Creature.SpeciesData.Get();
    if (!SpeciesData) return;

    // Check what moves this species can learn at level 0 or 1 (evolution moves)
    TArray<TSoftClassPtr<AGF_SkillDefinition>> EvolutionSkills = Creature.GetSkillsToLearnAtLevel(0);
    if (EvolutionSkills.Num() == 0)
    {
        EvolutionSkills = Creature.GetSkillsToLearnAtLevel(1);
    }

    // TODO: Trigger move learning UI if there are evolution moves
    if (EvolutionSkills.Num() > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("%s can learn %d new moves on evolution!"),
            *Creature.GetDisplayName().ToString(), EvolutionSkills.Num());
    }
}

void UGF_CreatureManagerSubsystem::RecalculateStats(FGF_CreatureInstanceData& Creature)
{
    UGF_CreatureSpeciesData* SpeciesData = Creature.SpeciesData.Get();
    if (!SpeciesData)
    {
        UE_LOG(LogTemp, Error, TEXT("RecalculateStats: No species data"));
        return;
    }

    // Get base stats from new species
    const FGF_CreatureBaseStats& BaseStats = SpeciesData->BaseStats;

    // Calculate HP using Creature formula
    // HP = floor(((2 * Base + AP + EP) * Level) / 100) + Level + 10
    int32 HPStat = FMath::FloorToInt(
        ((2 * BaseStats.HP + Creature.HP_AP + Creature.HP_EP) * Creature.Level) / 100.0f
    ) + Creature.Level + 10;

    // Husk: see UGF_CreatureSpeciesData::HasFixedOneHP. Every caller of this function
    // now gets the rule for free -- evolution, breeding, the sanctuary, scripted gifts
    // and NPC trades all used to need their own copy of the name check.
    if (SpeciesData->HasFixedOneHP())
    {
        HPStat = 1;
    }

    // Preserve HP percentage when evolving
    float HPPercentage = Creature.MaxHP > 0 ? (Creature.CurrentHP / Creature.MaxHP) : 1.0f;

    // Update MaxHP
    Creature.MaxHP = HPStat;

    // Restore same HP percentage
    Creature.CurrentHP = FMath::FloorToInt(Creature.MaxHP * HPPercentage);

	UE_LOG(LogTemp, Log, TEXT("Recalculated stats for %s: HP=%d"),
        *Creature.GetDisplayName().ToString(), HPStat);
}

int32 UGF_CreatureManagerSubsystem::AllocatePartyEP(int32 PartyIndex, EGF_CreatureStat Stat, int32 Delta)
{
    FGF_CreatureInstanceData Data;
    if (!VaultSystem || !VaultSystem->GetPartyCreature(PartyIndex, Data) || Data.IsEgg())
    {
        return 0;
    }

    const int32 Moved = Data.AllocateEP(Stat, Delta);
    if (Moved == 0)
    {
        return 0;
    }

    RecalculateStats(Data);
    VaultSystem->UpdatePartyCreature(PartyIndex, Data);
    MarkDirty();
    return Moved;
}

bool UGF_CreatureManagerSubsystem::ResetPartyEPs(int32 PartyIndex)
{
    FGF_CreatureInstanceData Data;
    if (!VaultSystem || !VaultSystem->GetPartyCreature(PartyIndex, Data) || Data.IsEgg())
    {
        return false;
    }

    Data.ResetEPs();
    RecalculateStats(Data);
    VaultSystem->UpdatePartyCreature(PartyIndex, Data);
    MarkDirty();
    return true;
}

bool UGF_CreatureManagerSubsystem::AddPartyAffinity(int32 PartyIndex, int32 Delta)
{
    FGF_CreatureInstanceData Data;
    if (!VaultSystem || !VaultSystem->GetPartyCreature(PartyIndex, Data) || Data.IsEgg())
    {
        return false;
    }

    Data.AddAffinity(Delta);
    RecalculateStats(Data);
    VaultSystem->UpdatePartyCreature(PartyIndex, Data);
    MarkDirty();
    return true;
}

int32 UGF_CreatureManagerSubsystem::GenerateNPCTamerID() const
{
    return FMath::RandRange(10000, 99999);
}

void UGF_CreatureManagerSubsystem::SaveCreatureData()
{
    // Use your existing save system
    if (VaultSystem)
    {
        VaultSystem->SaveToDisk();
    }
}

UGF_CreatureSpeciesData* UGF_CreatureManagerSubsystem::GetCreatureSpeciesData(FName SpeciesName) const
{
    if (SpeciesName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("GetCreatureSpeciesData: SpeciesName is None!"));
        return nullptr;
    }

    // Already resident? Free.
    if (const TObjectPtr<UGF_CreatureSpeciesData>* Resident = LoadedSpeciesByName.Find(SpeciesName))
    {
        if (*Resident)
        {
            return *Resident;
        }
    }

    // Registry metadata says which single asset this is. Load only that one.
    BuildSpeciesIndex();

    if (const FSoftObjectPath* Path = SpeciesPathsByName.Find(SpeciesName))
    {
        if (UGF_CreatureSpeciesData* Species = Cast<UGF_CreatureSpeciesData>(Path->TryLoad()))
        {
            RegisterSpeciesInIndex(Species, *Path);
            return Species;
        }
    }

    if (UGF_CreatureSpeciesData* Species = ResolveUntaggedSpecies(SpeciesName, INDEX_NONE))
    {
        return Species;
    }

    UE_LOG(LogTemp, Error, TEXT("GetCreatureSpeciesData: Could not find species '%s' (%d indexed, %d untagged)"),
        *SpeciesName.ToString(), SpeciesPathsByName.Num(), UntaggedSpeciesPaths.Num());
    return nullptr;
}

bool UGF_CreatureManagerSubsystem::ManualSave()
{
    if (!VaultSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot save: VaultSystem is null"));
        return false;
    }

    // Update playtime before saving
    UpdatePlaytime();

    // Update save timestamp
    VaultSystem->LastSaveDateTime = FDateTime::Now();

    // If this is a new save, set creation time
    if (VaultSystem->SaveCreationDateTime.GetTicks() == 0)
    {
        VaultSystem->SaveCreationDateTime = VaultSystem->LastSaveDateTime;
    }

    // Save player location — validated, see CommitPlayerTransformToSave. GetPlayerPawn
    // returns whatever is possessed right now, which during a cutscene, a battle or the
    // starter scene is NOT the overworld player.
    CommitPlayerTransformToSave(UGameplayStatics::GetPlayerPawn(GetWorld(), 0), TEXT("ManualSave"));

	// One entry per party slot, in party order — the title screen pairs this list with
	// nothing else, but anything that indexes it must line up with the party.
	//
	// IsNull() rather than IsValid(): IsValid() on a soft pointer means "already loaded
	// into memory", so a species that had not been loaded yet was silently skipped, which
	// shortened the list and shifted every later slot up one.
	VaultSystem->SavedPartyPreview.Empty();
	for (const FGF_CreatureInstanceData& Creature : VaultSystem->Party)
	{
		const UGF_CreatureSpeciesData* SpeciesData = Creature.SpeciesData.IsNull() ? nullptr : Creature.SpeciesData.LoadSynchronous();

		VaultSystem->SavedPartyPreview.Add(SpeciesData ? SpeciesData->CompendiumNumber : 0);
	}

    // Save follower status - set to none for basic save
    // Use SaveGameWithLocation from Blueprint when you have a follower reference
    VaultSystem->bHasFollowerOut = false;
    VaultSystem->FollowerPartyIndex = -1;

    // Save inventory to VaultSystem
    if (Inventory)
    {
        TMap<uint8, TMap<FName, int32>> InventorySaveData = Inventory->ToSaveFormat();
        VaultSystem->SetInventoryFromSaveFormat(InventorySaveData);
        VaultSystem->PlayerMoney = Inventory->Money;
    }

    // Perform save
    bool bSuccess = VaultSystem->SaveToDisk();

    if (bSuccess)
    {
        bHasUnsavedChanges = false;
        LastSaveTime = FDateTime::Now();

        UE_LOG(LogTemp, Log, TEXT("Manual save successful! Playtime: %.2f hours"),
            VaultSystem->TotalTimePlayed / 3600.0f);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Manual save failed!"));
    }

    return bSuccess;
}

bool UGF_CreatureManagerSubsystem::ShouldSpawnFollower(int32& OutFollowerIndex) const
{
    if (!VaultSystem) return false;

    if (VaultSystem->bHasFollowerOut && VaultSystem->FollowerPartyIndex >= 0)
    {
        OutFollowerIndex = VaultSystem->FollowerPartyIndex;

        // Validate the party index is still valid
        if (VaultSystem->Party.IsValidIndex(OutFollowerIndex))
        {
            UE_LOG(LogTemp, Log, TEXT("Should spawn follower: Party Index %d"), OutFollowerIndex);
            return true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Saved follower index %d is invalid (party size: %d)"),
                OutFollowerIndex, VaultSystem->Party.Num());
        }
    }

    return false;
}

FVector UGF_CreatureManagerSubsystem::GetSavedPlayerLocation() const
{
    return VaultSystem ? VaultSystem->PlayerLocation : FVector::ZeroVector;
}

FRotator UGF_CreatureManagerSubsystem::GetSavedPlayerRotation() const
{
    return VaultSystem ? VaultSystem->PlayerRotation : FRotator::ZeroRotator;
}



bool UGF_CreatureManagerSubsystem::AutoSaveAtCheckpoint(FString CheckpointName)
{
    UE_LOG(LogTemp, Log, TEXT("Auto-saving at checkpoint: %s"), *CheckpointName);

    bool bSuccess = ManualSave(); // Use same save logic

    if (bSuccess)
    {
        // Show "Saving..." indicator briefly
        // You can broadcast an event here for UI
    }

    return bSuccess;
}

bool UGF_CreatureManagerSubsystem::HasUnsavedChanges() const
{
    return bHasUnsavedChanges;
}

///////////////////////////////
/// Transfer Core System
///////////////////////////////

bool UGF_CreatureManagerSubsystem::TransferCreatureToNewCore(int32 PartyIndex, FName NewCoreName, FGF_CreatureInstanceData& OutUpdatedCreature)
{
	if (!VaultSystem)
	{
		//Nothing found, box system is null
		return false;
	}

	FGF_CreatureInstanceData CreatureData;
	if (!GetPartyCreatureData(PartyIndex, CreatureData))
	{
		return false;
	}

	FName OldCoreName = CreatureData.CaughtCoreName;
	CreatureData.CaughtCoreName = NewCoreName;

	if (!UpdatePartyCreatureData(PartyIndex, CreatureData))
	{
		return false;
	}

	OutUpdatedCreature = CreatureData;

	// Remove the transfered core from bag
	UseCore(NewCoreName);

	MarkDirty();

	return true;
}

bool UGF_CreatureManagerSubsystem::TransferVaultCreatureToNewCore(int32 VaultPageIndex, int32 SlotIndex, FName NewCoreName, FGF_CreatureInstanceData& OutUpdatedCreature)
{
	if (!VaultSystem)
	{
		return false;
	}

	FGF_CreatureInstanceData CreatureData;
	if (!VaultSystem->GetVaultCreature(VaultPageIndex, SlotIndex, CreatureData))
	{
		return false;
	}

	FName OldCoreName = CreatureData.CaughtCoreName;
	CreatureData.CaughtCoreName = NewCoreName;

	if (VaultSystem->UpdateVaultCreature(VaultPageIndex, SlotIndex, CreatureData))
	{
		return false;
	}

	OutUpdatedCreature = CreatureData;

	MarkDirty();

	return true;
}

void UGF_CreatureManagerSubsystem::RemoveCore(FName Core)
{
	if (Core == FString("None"))
	{
		return;
	}

	UseCore(Core);
}

//----------------------------------------
// OVERWORLD PERSISTANT DATA FOR SAVE/LOAD
//

void UGF_CreatureManagerSubsystem::RegisterOverworldCreature(int32 UniqueID, FName SpeciesName, int32 Level, bool bIsUnique, FVector Location, uint8 FacingDirection, const FString& MapName)
{
    if (!VaultSystem || UniqueID < 0)
        return;

    // Check if already registered (update instead of duplicate)
    for (FGF_OverworldCreatureSaveData& Existing : VaultSystem->SavedOverworldCreature)
    {
        if (Existing.UniqueID == UniqueID)
        {
            Existing.SpeciesName = SpeciesName;
            Existing.Level = Level;
            Existing.bIsUnique = bIsUnique;
            Existing.Location = Location;
            Existing.FacingDirection = FacingDirection;
            Existing.bHasBeenRemoved = false;
            MarkDirty();
            return;
        }
    }

    // Create new entry
    FGF_OverworldCreatureSaveData NewData;
    NewData.UniqueID = UniqueID;
    NewData.SpeciesName = SpeciesName;
    NewData.Level = Level;
    NewData.bIsUnique = bIsUnique;
    NewData.Location = Location;
    NewData.FacingDirection = FacingDirection;
    NewData.bHasBeenRemoved = false;

	NewData.MapName = MapName;

    //UWorld* World = GetWorld();
    //if (World)
    //{
     //   NewData.MapName = World->GetMapName();
      //  NewData.MapName.RemoveFromStart(TEXT("UEDPIE_0_"));
    //}

    VaultSystem->SavedOverworldCreature.Add(NewData);
    MarkDirty();

    UE_LOG(LogTemp, Log, TEXT("Registered overworld Creature ID:%d (%s) Unique=%s"),
        UniqueID, *SpeciesName.ToString(), bIsUnique ? TEXT("YES") : TEXT("NO"));
}

void UGF_CreatureManagerSubsystem::UpdateOverworldCreature(int32 UniqueID, FVector NewLocation, uint8 NewFacingDirection)
{
    if (!VaultSystem)
        return;

    for (FGF_OverworldCreatureSaveData& Data : VaultSystem->SavedOverworldCreature)
    {
        if (Data.UniqueID == UniqueID)
        {
            Data.Location = NewLocation;
            Data.FacingDirection = NewFacingDirection;
            MarkDirty();
            return;
        }
    }
}

void UGF_CreatureManagerSubsystem::RemoveOverworldCreature(int32 UniqueID)
{
    if (!VaultSystem)
        return;

    for (FGF_OverworldCreatureSaveData& Data : VaultSystem->SavedOverworldCreature)
    {
        if (Data.UniqueID == UniqueID)
        {
            Data.bHasBeenRemoved = true;
            MarkDirty();
            UE_LOG(LogTemp, Log, TEXT("Overworld Creature removed: ID %d"), UniqueID);
            return;
        }
    }
}

TArray<FGF_OverworldCreatureSaveData> UGF_CreatureManagerSubsystem::GetSavedOverworldCreatureForMap(const FString& MapName) const
{
    TArray<FGF_OverworldCreatureSaveData> Result;
    if (!VaultSystem)
        return Result;

    // If empty string passed, auto-detect current map
    FString SearchMap = MapName;
    if (SearchMap.IsEmpty())
    {
        UWorld* World = GetWorld();
        if (World)
        {
            SearchMap = World->GetMapName();
            SearchMap.RemoveFromStart(TEXT("UEDPIE_0_"));
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("GetSavedOverworldCreatureForMap: Searching for map '%s', total saved: %d"),
        *SearchMap, VaultSystem->SavedOverworldCreature.Num());

    for (const FGF_OverworldCreatureSaveData& Data : VaultSystem->SavedOverworldCreature)
    {
        UE_LOG(LogTemp, Warning, TEXT("  Entry: ID=%d Map='%s' Removed=%s"),
            Data.UniqueID, *Data.MapName, Data.bHasBeenRemoved ? TEXT("YES") : TEXT("NO"));

        if (!Data.bHasBeenRemoved && Data.MapName == SearchMap)
        {
            Result.Add(Data);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("  Found %d matching Creature"), Result.Num());
    return Result;
}

bool UGF_CreatureManagerSubsystem::HasSavedOverworldCreature(int32 UniqueID) const
{
    if (!VaultSystem)
        return false;

    for (const FGF_OverworldCreatureSaveData& Data : VaultSystem->SavedOverworldCreature)
    {
        if (Data.UniqueID == UniqueID)
            return true; // Return true even if removed - caller checks bHasBeenRemoved
    }
    return false;
}

bool UGF_CreatureManagerSubsystem::GetOverworldCreatureData(int32 UniqueID, FGF_OverworldCreatureSaveData& OutData) const
{
    if (!VaultSystem)
        return false;

    for (const FGF_OverworldCreatureSaveData& Data : VaultSystem->SavedOverworldCreature)
    {
        if (Data.UniqueID == UniqueID && !Data.bHasBeenRemoved)
        {
            OutData = Data;
            return true;
        }
    }
    return false;
}

void UGF_CreatureManagerSubsystem::ClearOverworldCreatureForMap(const FString& MapName)
{
    if (!VaultSystem)
        return;

    VaultSystem->SavedOverworldCreature.RemoveAll([&MapName](const FGF_OverworldCreatureSaveData& Data)
    {
        return Data.MapName == MapName;
    });
    MarkDirty();
}

int32 UGF_CreatureManagerSubsystem::GenerateOverworldCreatureID()
{
    if (!VaultSystem)
        return -1;

    return VaultSystem->NextOverworldCreatureID++;
}


void UGF_CreatureManagerSubsystem::ReleaseCreature(int32 PartyIndex, int32 VaultPageIndex, int32 VaultSlot)
{
	if (!VaultSystem)
	{
		return;
	}

	if (PartyIndex >= 0)
	{
		if (VaultSystem->GetPartySize() <= 1)
		{
			return;
		}

		if (!VaultSystem->IsValidPartyIndex(PartyIndex))
		{
			return;
		}

		FGF_CreatureInstanceData Released;
		VaultSystem->GetPartyCreature(PartyIndex, Released);

		VaultSystem->RemoveFromParty(PartyIndex);
	}

	else
	{
		if (!VaultSystem->IsValidVaultSlot(VaultPageIndex ,VaultSlot))
		{
			return;
		}

		if (VaultSystem->IsSlotEmpty(VaultPageIndex, VaultSlot))
		{
			return;
		}

		FGF_CreatureInstanceData Released;
		VaultSystem->GetCreatureAtSlot(VaultPageIndex, VaultSlot, Released);

		VaultSystem->RemoveFromVault(VaultPageIndex, VaultSlot);
	}

	// Same reason EndBattle no longer saves: SaveToDisk() writes QuestSlot and the
	// berry slot too, so releasing a Creature quietly committed every unsaved story
	// flag with it. Releasing is now undone by resetting without saving, which is
	// how the games this one follows behave anyway.
	MarkDirty();
}

void UGF_CreatureManagerSubsystem::GoToLastPokeCenter(UGF_GridMovementComponent* GridMovementComponent)
{
    if (!GridMovementComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("GoToLastPokeCenter: GridMovementComponent is null"));
        return;
    }

    if (!VaultSystem || !VaultSystem->bHasVisitedPokeCenter)
    {
        // Player has never healed at a Creature Center — use default (mother's house tile)
        GridMovementComponent->TeleportToGrid(DefaultRespawnTile);
    }
    else
    {
        GridMovementComponent->TeleportToGrid(VaultSystem->LastPokeCenterTile);
    }

    // Suppress gravity and poll for floor geometry — fires OnFloorConfirmed once the
    // destination sub-level is loaded so the loading screen knows when to dismiss.
    GridMovementComponent->WaitForFloor();
}

void UGF_CreatureManagerSubsystem::RegisterPokeCenterHeal(const FGF_GridCoordinate& HealTile)
{
    if (!VaultSystem) return;

    VaultSystem->LastPokeCenterTile    = HealTile;
    VaultSystem->bHasVisitedPokeCenter = true;
    MarkDirty();
}

void UGF_CreatureManagerSubsystem::RegisterDungeonEntrance(const FGF_GridCoordinate& EntranceTile)
{
    if (!VaultSystem) return;

    VaultSystem->LastDungeonEntranceTile = EntranceTile;
    VaultSystem->bIsInDungeon            = true;
    MarkDirty();
}

void UGF_CreatureManagerSubsystem::ClearDungeonState()
{
    if (!VaultSystem || !VaultSystem->bIsInDungeon) return;

    VaultSystem->bIsInDungeon = false;
    MarkDirty();
}

bool UGF_CreatureManagerSubsystem::CanUseEscapeRope() const
{
    if (!VaultSystem || !VaultSystem->bIsInDungeon)
        return false;

    // Blocked during battle...
    if (bIsInBattle)
        return false;

    // ...and during dialogue / cutscenes.
    if (const UGF_DialogueSubsystem* Dialogue = GetGameInstance()->GetSubsystem<UGF_DialogueSubsystem>())
    {
        if (Dialogue->IsDialogueActive())
            return false;
    }

    return true;
}

bool UGF_CreatureManagerSubsystem::IsInDungeon() const
{
    return VaultSystem && VaultSystem->bIsInDungeon;
}

FGF_GridCoordinate UGF_CreatureManagerSubsystem::GetLastDungeonEntrance() const
{
    return VaultSystem ? VaultSystem->LastDungeonEntranceTile : FGF_GridCoordinate();
}

bool UGF_CreatureManagerSubsystem::UseEscapeRope(UGF_GridMovementComponent* GridMovementComponent)
{
    if (!GridMovementComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("UseEscapeRope: GridMovementComponent is null"));
        return false;
    }

    // Single source of truth: not in a dungeon, or blocked by battle/cutscene.
    if (!CanUseEscapeRope())
    {
        return false;
    }

    GridMovementComponent->TeleportToGrid(VaultSystem->LastDungeonEntranceTile);

    // Suppress gravity and poll for floor geometry — fires OnFloorConfirmed once the
    // destination sub-level is loaded so the loading screen / fade knows when to dismiss.
    GridMovementComponent->WaitForFloor();

    VaultSystem->bIsInDungeon = false;
    MarkDirty();
    return true;
}

bool UGF_CreatureManagerSubsystem::CanUseTeleport() const
{
    // Blocked during battle...
    if (bIsInBattle)
        return false;

    // ...and during dialogue / cutscenes.
    if (const UGF_DialogueSubsystem* Dialogue = GetGameInstance()->GetSubsystem<UGF_DialogueSubsystem>())
    {
        if (Dialogue->IsDialogueActive())
            return false;
    }

    // Caves/dungeons belong to Escape Rope, never Teleport. Checked separately from
    // the area data so a dungeon floor with no RouteVolume of its own still blocks.
    if (IsInDungeon())
        return false;

    // Outdoors only — decided by the current area's data asset.
    const UGF_RouteSubsystem* RouteSys = GetGameInstance()->GetSubsystem<UGF_RouteSubsystem>();
    const UGF_RouteData* CurrentRoute = RouteSys ? RouteSys->GetCurrentRoute() : nullptr;

    // No area data = we don't know where we are, so fail closed rather than risk
    // teleporting the player out of a scripted sequence.
    if (!CurrentRoute)
        return false;

    return CurrentRoute->AllowsTeleportOut();
}

bool UGF_CreatureManagerSubsystem::UseTeleport(UGF_GridMovementComponent* GridMovementComponent)
{
    if (!GridMovementComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("UseTeleport: GridMovementComponent is null"));
        return false;
    }

    // Single source of truth: indoors/cave, or blocked by battle/cutscene.
    if (!CanUseTeleport())
    {
        return false;
    }

    // Same destination as a whiteout, never-healed fallback included.
    // GoToLastPokeCenter() already calls WaitForFloor() for us, so the loading
    // screen / fade can dismiss on OnFloorConfirmed exactly like Escape Rope.
    GoToLastPokeCenter(GridMovementComponent);
    return true;
}


void UGF_CreatureManagerSubsystem::SetLostOrWonBattle(bool LostBattle)
{
	if (LostBattle)
	{
		VaultSystem->bLastBattleLost = true;
		VaultSystem->bLastBattleWon = false;
	}
	else

		VaultSystem->bLastBattleLost = false;
		VaultSystem->bLastBattleWon = true;
}

void UGF_CreatureManagerSubsystem::ResetWonLostBattleBools()
{
	VaultSystem->bLastBattleWon = false;
	VaultSystem->bLastBattleLost = false;
}

void UGF_CreatureManagerSubsystem::AddPartyMemberToRecentlyDowned(const int32& PartyIndex)
{
	if (PartyIndex < 0 || PartyIndex > 6)
	{
		return;
	}

	VaultSystem->PartyMembersThatRecentlyDowned.Add(PartyIndex);

}



// ---------------------------------------------------------------------------
// SPECIES LOADING DIAGNOSTICS
//
// These exist because "did the lazy path actually run?" is otherwise invisible:
// loading a save resolves party members through FGF_CreatureInstanceData's soft
// pointer and never touches the indexed lookups, so a normal session can finish
// without either code path logging anything.
// ---------------------------------------------------------------------------

static UGF_CreatureManagerSubsystem* GEBoot_GetCreatureManager(UWorld* World)
{
	if (!World || !World->GetGameInstance())
	{
		return nullptr;
	}
	return World->GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>();
}

static FAutoConsoleCommandWithWorldAndArgs GESpeciesIndexCmd(
	TEXT("gf.Species.Index"),
	TEXT("Build/print the species asset index: how many are tagged, untagged, and currently resident."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGF_CreatureManagerSubsystem* Manager = GEBoot_GetCreatureManager(World);
			if (!Manager)
			{
				UE_LOG(LogTemp, Error, TEXT("gf.Species.Index: no CreatureManagerSubsystem (are you in PIE?)"));
				return;
			}

			// Goes through the index, loads nothing.
			const int32 Total = Manager->GetTotalCreatureSpeciesCount();

			UE_LOG(LogTemp, Display,
				TEXT("gf.Species.Index: %d species known, %d currently resident in memory."),
				Total, Manager->GetLoadedSpeciesCount());
		}));

static FAutoConsoleCommandWithWorldAndArgs GESpeciesFindCmd(
	TEXT("gf.Species.Find"),
	TEXT("gf.Species.Find <SpeciesName> -- look up one species and report how many assets that cost."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGF_CreatureManagerSubsystem* Manager = GEBoot_GetCreatureManager(World);
			if (!Manager)
			{
				UE_LOG(LogTemp, Error, TEXT("gf.Species.Find: no CreatureManagerSubsystem (are you in PIE?)"));
				return;
			}
			if (Args.Num() == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("gf.Species.Find: needs a species name, e.g. 'gf.Species.Find Cindling'"));
				return;
			}

			const int32 Before = Manager->GetLoadedSpeciesCount();
			const FName Wanted(*Args[0]);

			UGF_CreatureSpeciesData* Species = Manager->GetCreatureSpeciesData(Wanted);
			const int32 After = Manager->GetLoadedSpeciesCount();

			if (Species)
			{
				UE_LOG(LogTemp, Display,
					TEXT("gf.Species.Find: found '%s' (dex #%d). Loaded %d asset(s) to do it; %d now resident."),
					*Species->SpeciesName.ToString(), Species->CompendiumNumber, After - Before, After);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("gf.Species.Find: '%s' not found. Loaded %d asset(s) looking; %d now resident."),
					*Args[0], After - Before, After);
			}
		}));
