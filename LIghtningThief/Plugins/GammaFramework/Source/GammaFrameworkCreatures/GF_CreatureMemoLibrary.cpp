// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureMemoLibrary.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GF_VaultSystem.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureStatLibrary.h"
#include "GF_RouteData.h"
#include "GF_RouteSubsystem.h"

namespace
{
	/** Game instance behind any world context object, or null outside a running world. */
	UGameInstance* GetGameInstanceFrom(const UObject* WorldContextObject)
	{
		if (!GEngine || !WorldContextObject)
		{
			return nullptr;
		}

		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		return World ? World->GetGameInstance() : nullptr;
	}

	/**
	 * The route the player is standing in right now.
	 *
	 * Deliberately reads the live RouteSubsystem rather than the save's SavedRoutePath:
	 * SavedRoutePath is only refreshed when the game saves, so a Creature caught after
	 * the last save would be stamped with wherever the player last saved instead of
	 * where they actually caught it.
	 */
	UGF_RouteData* GetCurrentRouteFrom(const UObject* WorldContextObject)
	{
		if (UGameInstance* GI = GetGameInstanceFrom(WorldContextObject))
		{
			if (UGF_RouteSubsystem* RouteSys = GI->GetSubsystem<UGF_RouteSubsystem>())
			{
				return RouteSys->GetCurrentRoute();
			}
		}
		return nullptr;
	}

	FString GetPlayerNameFrom(const UObject* WorldContextObject)
	{
		if (UGameInstance* GI = GetGameInstanceFrom(WorldContextObject))
		{
			if (UGF_CreatureManagerSubsystem* Manager = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
			{
				return Manager->GetPlayerName();
			}
		}
		return FString();
	}

	/**
	 * Is this Creature somebody else's originally?
	 *
	 * Tamer ID, not name. IDs are RandRange(0, 999999) so they do collide, but
	 * a name comparison is worse: any player who happens to be called JOHN would
	 * be told they personally met an event Creature in a town they have never
	 * visited. A zero ID means the Creature predates tamer stamping, and is
	 * treated as the player's own rather than accusing them of trading.
	 */
	bool IsOutsider(const UObject* WorldContextObject, const FGF_CreatureInstanceData& Creature)
	{
		if (Creature.OriginalTamerID == 0)
		{
			return false;
		}

		if (UGameInstance* GI = GetGameInstanceFrom(WorldContextObject))
		{
			if (UGF_CreatureManagerSubsystem* Manager = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
			{
				if (Manager->VaultSystem)
				{
					return Creature.OriginalTamerID != Manager->VaultSystem->PlayerID;
				}
			}
		}

		return false;
	}

	/**
	 * The origin as one sentence, phrased for whoever actually met the Creature.
	 *
	 * Assembled fragment by fragment because any of date, place and level can be
	 * missing, and the alternative is eight format strings for the combinations.
	 * That does mean the fragments are concatenated rather than living in one
	 * translatable sentence -- a real cost if this is ever localised, and the
	 * point at which this should become a small set of whole-sentence formats.
	 */
	FText BuildMetSentence(const FGF_CreatureInstanceData& Creature, const FGF_CreatureMemoInfo& Info)
	{
		if (Creature.bIsEgg || !Info.bHasOrigin)
		{
			return FText::GetEmpty();
		}

		const FText Who = (Info.bIsOutsider && !Info.OriginalTamerName.IsEmpty())
			? Info.OriginalTamerName
			: NSLOCTEXT("CreatureMemo", "MetByYou", "You");

		// FromName, because GetDisplayName returns an FName and FText::Format has
		// no overload for one -- it fails as an unhelpful static_assert deep in
		// Text.h rather than at the call site.
		FString Sentence = FText::Format(
			NSLOCTEXT("CreatureMemo", "MetWhoWhat", "{0} met {1}"),
			Who, FText::FromName(Creature.GetDisplayName())).ToString();

		if (!Info.DateLine.IsEmpty())
		{
			Sentence += FText::Format(
				NSLOCTEXT("CreatureMemo", "MetOnDate", " on {0}"), Info.DateLine).ToString();
		}

		if (!Info.LocationLine.IsEmpty())
		{
			Sentence += FText::Format(
				NSLOCTEXT("CreatureMemo", "MetInPlace", " in {0}"), Info.LocationLine).ToString();
		}

		if (Creature.MetLevel > 0)
		{
			Sentence += FText::Format(
				NSLOCTEXT("CreatureMemo", "MetAtLv", " at Lv. {0}"),
				FText::AsNumber(Creature.MetLevel)).ToString();
		}

		Sentence.AppendChar(TEXT('.'));
		return FText::FromString(Sentence);
	}

	/** Joins the non-empty lines with newlines, so a missing line never leaves a blank row. */
	FText JoinLines(const TArray<FText>& Lines)
	{
		FString Joined;
		for (const FText& Line : Lines)
		{
			if (Line.IsEmpty())
			{
				continue;
			}
			if (!Joined.IsEmpty())
			{
				Joined.AppendChar(TEXT('\n'));
			}
			Joined.Append(Line.ToString());
		}
		return FText::FromString(Joined);
	}
}

//====================================================================================
// READING
//====================================================================================

bool UGF_CreatureMemoLibrary::HasOriginInfo(const FGF_CreatureInstanceData& Creature)
{
	// The date is the sentinel, not the route: a Creature can be obtained somewhere
	// with no RouteVolume (a cutscene interior, a debug give) and still have a
	// perfectly good memo.
	return Creature.MetDate.GetTicks() != 0;
}

FText UGF_CreatureMemoLibrary::GetTemperamentDisplayName(EGF_Temperament Temperament)
{
	const UEnum* TemperamentEnum = StaticEnum<EGF_Temperament>();
	if (!TemperamentEnum)
	{
		return FText::GetEmpty();
	}

	const int32 Index = TemperamentEnum->GetIndexByValue(static_cast<int64>(Temperament));
	if (Index == INDEX_NONE)
	{
		return FText::GetEmpty();
	}

	// UMETA(DisplayName) metadata is EDITOR ONLY — it is stripped from cooked builds.
	// Reading it alone would give "Ferocious" in the editor and an empty string in the
	// packaged game, which reads on screen as a bare " nature." with nothing in front
	// of it. Fall back to the raw enumerator name, which always survives the cook.
	const FText Display = TemperamentEnum->GetDisplayNameTextByIndex(Index);
	if (!Display.IsEmpty())
	{
		return Display;
	}

	return FText::FromString(TemperamentEnum->GetNameStringByIndex(Index));
}

FText UGF_CreatureMemoLibrary::GetTemperamentLine(EGF_Temperament Temperament)
{
	const FText Name = GetTemperamentDisplayName(Temperament);
	if (Name.IsEmpty())
	{
		return FText::GetEmpty();
	}

	return FText::Format(NSLOCTEXT("CreatureMemo", "TemperamentLine", "{0} temperament."), Name);
}

FText UGF_CreatureMemoLibrary::GetTemperamentEffectLine(EGF_Temperament Temperament)
{
	const FString Boosted  = UGF_CreatureStatLibrary::GetTemperamentBoostedStat(Temperament);
	const FString Hindered = UGF_CreatureStatLibrary::GetTemperamentHinderedStat(Temperament);

	// The five neutral natures report "None" for both halves — they genuinely have
	// nothing to say, so say nothing rather than printing "Raises None".
	if (Boosted.IsEmpty() || Boosted == TEXT("None") || Hindered.IsEmpty() || Hindered == TEXT("None"))
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		NSLOCTEXT("CreatureMemo", "TemperamentEffectLine", "Raises {0}, lowers {1}."),
		FText::FromString(Boosted),
		FText::FromString(Hindered));
}

FText UGF_CreatureMemoLibrary::FormatMetDate(const FDateTime& MetDate)
{
	if (MetDate.GetTicks() == 0)
	{
		return FText::GetEmpty();
	}

	static const TCHAR* MonthAbbreviations[] =
	{
		TEXT("Jan."), TEXT("Feb."), TEXT("Mar."), TEXT("Apr."),
		TEXT("May"),  TEXT("Jun."), TEXT("Jul."), TEXT("Aug."),
		TEXT("Sep."), TEXT("Oct."), TEXT("Nov."), TEXT("Dec.")
	};

	const int32 MonthIndex = FMath::Clamp(MetDate.GetMonth(), 1, 12) - 1;

	// FromString rather than AsNumber for the year on purpose: AsNumber groups
	// thousands, so the year would come out as "2,026".
	return FText::Format(
		NSLOCTEXT("CreatureMemo", "MetDate", "{0} {1}, {2}"),
		FText::FromString(MonthAbbreviations[MonthIndex]),
		FText::AsNumber(MetDate.GetDay()),
		FText::FromString(FString::FromInt(MetDate.GetYear())));
}

FText UGF_CreatureMemoLibrary::GetMetLocationText(const FGF_CreatureInstanceData& Creature, const FString& PlayerName)
{
	// An explicit override always wins — it exists precisely for origins that aren't
	// a route the player walked into.
	if (!Creature.MetLocationOverride.IsEmpty())
	{
		return FText::FromString(Creature.MetLocationOverride);
	}

	if (Creature.MetRoutePath.IsValid())
	{
		if (UGF_RouteData* Route = Cast<UGF_RouteData>(Creature.MetRoutePath.TryLoad()))
		{
			return Route->GetFormattedDisplayName(PlayerName);
		}
	}

	return FText::GetEmpty();
}

FText UGF_CreatureMemoLibrary::GetEggProgressText(const FGF_CreatureInstanceData& Creature)
{
	if (!Creature.bIsEgg)
	{
		return FText::GetEmpty();
	}

	// classic thresholds, in egg cycles (one cycle = 256 steps). Species start at 20-ish,
	// so most eggs open on the "doesn't seem close" line and walk down from there.
	const int32 Cycles = Creature.EggCyclesRemaining;

	if (Cycles > 40)
	{
		return NSLOCTEXT("CreatureMemo", "EggVeryFar", "It looks like this Egg will take a long time to hatch.");
	}
	if (Cycles > 10)
	{
		return NSLOCTEXT("CreatureMemo", "EggFar", "What will hatch from this? It doesn't seem close to hatching.");
	}
	if (Cycles > 5)
	{
		return NSLOCTEXT("CreatureMemo", "EggClose", "It appears to move occasionally. It may be close to hatching.");
	}

	return NSLOCTEXT("CreatureMemo", "EggImminent", "Sounds can be heard coming from inside! It will hatch soon!");
}

FGF_CreatureMemoInfo UGF_CreatureMemoLibrary::GetTamerMemo(const UObject* WorldContextObject, const FGF_CreatureInstanceData& Creature)
{
	FGF_CreatureMemoInfo Info;

	Info.bIsEgg     = Creature.bIsEgg;
	Info.bHasOrigin = HasOriginInfo(Creature);

	// An egg never reveals what's inside, and that includes its nature — showing
	// "Ferocious nature." on an egg would leak the roll before it hatches.
	if (!Creature.bIsEgg)
	{
		Info.TemperamentLine       = GetTemperamentLine(Creature.Temperament);
		Info.TemperamentEffectLine = GetTemperamentEffectLine(Creature.Temperament);
	}

	Info.DateLine     = FormatMetDate(Creature.MetDate);
	Info.LocationLine = GetMetLocationText(Creature, GetPlayerNameFrom(WorldContextObject));

	switch (Creature.MetType)
	{
		case EGF_CreatureMetType::Hatched:
			Info.MetLine = NSLOCTEXT("CreatureMemo", "EggHatched", "Egg hatched.");
			break;

		case EGF_CreatureMetType::EggReceived:
			Info.MetLine = NSLOCTEXT("CreatureMemo", "EggReceived", "Egg received.");
			break;

		default:
			// Caught, Gift and unstamped-but-dated all read the same way in classic.
			// A MetLevel of 0 means the level was never recorded, so drop the line
			// entirely rather than claiming "Met at Lv. 0".
			if (Creature.MetLevel > 0)
			{
				Info.MetLine = FText::Format(
					NSLOCTEXT("CreatureMemo", "MetAtLevel", "Met at Lv. {0}."),
					FText::AsNumber(Creature.MetLevel));
			}
			break;
	}

	Info.EggProgressLine = GetEggProgressText(Creature);

	// Outsider status decides whose story this is. Compared on the tamer ID
	// rather than the name: two players can pick the same name, and a memo that
	// says "You met it" about somebody else's Creature is worse than one that is
	// merely impersonal.
	const FString PlayerName = GetPlayerNameFrom(WorldContextObject);
	Info.OriginalTamerName = FText::FromName(Creature.OriginalTamerName);
	Info.bIsOutsider = IsOutsider(WorldContextObject, Creature);

	if (!Creature.MemoNote.IsEmpty())
	{
		Info.NoteLine = FText::FromString(Creature.MemoNote);
	}
	else if (Creature.bCannotEvolve)
	{
		// Never leave this silent. A Creature that refuses to evolve with no
		// explanation anywhere reads as a bug, and gets reported as one.
		Info.NoteLine = NSLOCTEXT("CreatureMemo", "CannotEvolve", "It cannot evolve.");
	}

	Info.MetSentence = BuildMetSentence(Creature, Info);

	if (Creature.bIsEgg)
	{
		// While it's an egg the whole panel is the hatch hint — the received date and
		// place are still on the struct if the widget wants to show them separately.
		Info.MemoText = Info.EggProgressLine;
	}
	else if (Info.bHasOrigin)
	{
		Info.MemoText = JoinLines({ Info.DateLine, Info.LocationLine, Info.MetLine, Info.TemperamentLine, Info.NoteLine });
	}
	else
	{
		// Creature from before the memo system, and anything a scripted event forgot to
		// stamp. Temperament is still real data, so lead with it and be honest about the rest.
		Info.MemoText = JoinLines({
			Info.TemperamentLine,
			NSLOCTEXT("CreatureMemo", "UnknownOrigin", "Where this Creature met you is a mystery.") });
	}

	return Info;
}

//====================================================================================
// WRITING
//====================================================================================

void UGF_CreatureMemoLibrary::StampMetInfo(const UObject* WorldContextObject, FGF_CreatureInstanceData& Creature,
	EGF_CreatureMetType MetType, int32 MetLevelOverride, const FString& LocationOverride)
{
	Creature.MetType  = MetType;
	Creature.MetDate  = FDateTime::Now();
	Creature.MetLevel = (MetLevelOverride > 0) ? MetLevelOverride : Creature.Level;

	Creature.MetLocationOverride = LocationOverride;

	// Only look up a route when no override was given — an override means the caller
	// is naming the place itself and the player's current route is irrelevant.
	Creature.MetRoutePath = FSoftObjectPath();
	if (LocationOverride.IsEmpty())
	{
		if (UGF_RouteData* Route = GetCurrentRouteFrom(WorldContextObject))
		{
			Creature.MetRoutePath = FSoftObjectPath(Route);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("StampMetInfo: %s -> type %d, Lv. %d, route '%s'"),
		*Creature.GetDisplayName().ToString(),
		static_cast<int32>(MetType),
		Creature.MetLevel,
		Creature.MetRoutePath.IsValid() ? *Creature.MetRoutePath.ToString() : *LocationOverride);
}

bool UGF_CreatureMemoLibrary::StampMetInfoIfUnset(const UObject* WorldContextObject, FGF_CreatureInstanceData& Creature,
	EGF_CreatureMetType MetType, int32 MetLevelOverride, const FString& LocationOverride)
{
	if (HasOriginInfo(Creature))
	{
		return false;
	}

	StampMetInfo(WorldContextObject, Creature, MetType, MetLevelOverride, LocationOverride);
	return true;
}
