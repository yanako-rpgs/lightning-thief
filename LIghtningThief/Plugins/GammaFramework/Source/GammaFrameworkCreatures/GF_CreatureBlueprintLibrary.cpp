// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureBlueprintLibrary.h"
#include "GF_ElementTypes.h"
#include "PaperSprite.h"

FGF_CreatureInstanceData UGF_CreatureBlueprintLibrary::CreateWildCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level, bool ForceUnique, int32 UniqueOdds, int32 EliteUniqueOdds)
{
	FGF_CreatureInstanceData NewCreature;

	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateWildCreature: SpeciesData is null!"));
		return NewCreature;
	}

	// Clamp level to valid range
	Level = FMath::Clamp(Level, 1, 100);

	// Call Initialize first, it resets all struct fields, so unique must be set after.
	TArray<TSubclassOf<AGF_SkillDefinition>> EmptySkills;
	NewCreature.Initialize(SpeciesData, Level, EmptySkills, NAME_None, 0);

	// Determine unique AFTER Initialize() so the result isn't overwritten.
	if (ForceUnique)
	{
		NewCreature.bIsUnique = true;
	}
	else
	{
		NewCreature.bIsUnique = (FMath::RandRange(1, UniqueOdds) == 1);
	}

	UE_LOG(LogTemp, Log, TEXT("CreateWildCreature: Created %s (Level %d, Unique=%s)"),
		*SpeciesData->SpeciesName.ToString(), Level,
		NewCreature.bIsUnique ? TEXT("Yes") : TEXT("No"));

	return NewCreature;
}

FGF_CreatureInstanceData UGF_CreatureBlueprintLibrary::CreateCreatureWithSkills(
	UGF_CreatureSpeciesData* SpeciesData,
	int32 Level,
	const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills)
{
	FGF_CreatureInstanceData NewCreature;

	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateCreatureWithSkills: SpeciesData is null!"));
		return NewCreature;
	}

	// Clamp level to valid range
	Level = FMath::Clamp(Level, 1, 100);

	// Call Initialize with provided starting moves
	NewCreature.Initialize(SpeciesData, Level, StartingSkills, NAME_None, 0);

	UE_LOG(LogTemp, Log, TEXT("CreateCreatureWithSkills: Created %s (Level %d) with %d moves"),
		*SpeciesData->SpeciesName.ToString(), Level, StartingSkills.Num());

	return NewCreature;
}

FName UGF_CreatureBlueprintLibrary::GetCreatureDisplayName(const FGF_CreatureInstanceData& CreatureData)
{
	return CreatureData.GetDisplayName();
}

UPaperSprite* UGF_CreatureBlueprintLibrary::GetCreatureDisplayIcon(const FGF_CreatureInstanceData& CreatureData)
{
	return GetCreatureDisplayIcon1(CreatureData);
}

UPaperSprite* UGF_CreatureBlueprintLibrary::GetCreatureDisplayIcon1(const FGF_CreatureInstanceData& CreatureData)
{
	// An egg must never show what's inside it — this is the shared entry point every
	// screen uses (party, boxes, summary, the save screen's team row), so the egg check
	// belongs here rather than in each widget.
	if (UPaperSprite* EggIcon = GetEggDisplayIcon(CreatureData, 0))
	{
		return EggIcon;
	}

	// Use IsNull() not IsValid() - IsValid() requires asset to be in memory,
	// causing white textures after save/load. IsNull() just checks the path exists.
	if (CreatureData.SpeciesData.IsNull())
	{
		return nullptr;
	}

	UGF_CreatureSpeciesData* SpeciesData = CreatureData.SpeciesData.LoadSynchronous();
	if (!SpeciesData)
	{
		return nullptr;
	}

	if (CreatureData.bIsUnique && SpeciesData->UniqueDisplayIcon1)
	{
		return SpeciesData->UniqueDisplayIcon1;
	}

	return SpeciesData->DisplayIcon1;
}

UPaperSprite* UGF_CreatureBlueprintLibrary::GetCreatureDisplayIcon2(const FGF_CreatureInstanceData& CreatureData)
{
	// Frame 2 of the egg's bob animation — see GetCreatureDisplayIcon1.
	if (UPaperSprite* EggIcon = GetEggDisplayIcon(CreatureData, 1))
	{
		return EggIcon;
	}

	// Use IsNull() not IsValid() - same reason as Icon1
	if (CreatureData.SpeciesData.IsNull())
	{
		return nullptr;
	}

	UGF_CreatureSpeciesData* SpeciesData = CreatureData.SpeciesData.LoadSynchronous();
	if (!SpeciesData)
	{
		return nullptr;
	}

	if (CreatureData.bIsUnique && SpeciesData->UniqueDisplayIcon2)
	{
		return SpeciesData->UniqueDisplayIcon2;
	}

	return SpeciesData->DisplayIcon2;
}

UPaperSprite* UGF_CreatureBlueprintLibrary::GetEggDisplayIcon(const FGF_CreatureInstanceData& CreatureData, int32 Frame)
{
	if (!CreatureData.bIsEgg)
	{
		return nullptr;
	}

	// Which egg graphic: the explicit choice a gift egg was built with, otherwise the
	// primary type of what's inside. Same rule as UGF_BreedingLibrary::GetEggAppearanceType,
	// repeated here so this stays a pure function with no world context to thread through.
	EGF_Element Appearance = CreatureData.EggAppearanceType;
	if (Appearance == EGF_Element::None && !CreatureData.SpeciesData.IsNull())
	{
		if (const UGF_CreatureSpeciesData* Species = CreatureData.SpeciesData.LoadSynchronous())
		{
			Appearance = Species->PrimaryElement;
		}
	}

	// Sprite names are keyed by type token: SPR_GRASS_icon_EGG_Sprite_0. There is no
	// NORMAL sheet — the plain egg is SPR_000, which is also the fallback for an
	// unresolved type. Spelled out rather than derived from the enum because UMETA
	// DisplayNames are stripped from cooked builds.
	const FString TypeTokenStr = UGF_ElementLibrary::GetElementAssetToken(Appearance);
	const TCHAR* TypeToken = *TypeTokenStr;

	const int32 FrameIndex = FMath::Clamp(Frame, 0, 1);

	auto LoadEggSprite = [](const TCHAR* Token, int32 InFrame) -> UPaperSprite*
	{
		const FString AssetName = FString::Printf(TEXT("SPR_%s_icon_EGG_Sprite_%d"), Token, InFrame);
		const FString ObjectPath = FString::Printf(
			TEXT("/Game/SPRITES/UI/PARTY/PokeIcons/Eggs/%s.%s"), *AssetName, *AssetName);

		return Cast<UPaperSprite>(FSoftObjectPath(ObjectPath).TryLoad());
	};

	if (UPaperSprite* Sprite = LoadEggSprite(TypeToken, FrameIndex))
	{
		return Sprite;
	}

	// A missing type sheet falls back to the plain egg rather than to the species icon,
	// which would spoil what's inside.
	if (UPaperSprite* Fallback = LoadEggSprite(TEXT("000"), FrameIndex))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GetEggDisplayIcon: no SPR_%s_icon_EGG_Sprite_%d - using the plain egg."),
			TypeToken, FrameIndex);
		return Fallback;
	}

	UE_LOG(LogTemp, Error,
		TEXT("GetEggDisplayIcon: no egg sprites found under /Game/SPRITES/UI/PARTY/PokeIcons/Eggs/. "
			 "The species icon would spoil the egg, so this slot draws empty."));
	return nullptr;
}

UGF_CreatureSpeciesData* UGF_CreatureBlueprintLibrary::GetCreatureSpeciesData(const FGF_CreatureInstanceData& CreatureData)
{
	// Use IsNull() not IsValid() - same reason as above
	if (CreatureData.SpeciesData.IsNull())
	{
		return nullptr;
	}

	return CreatureData.SpeciesData.LoadSynchronous();
}

bool UGF_CreatureBlueprintLibrary::IsCreatureDataValid(const FGF_CreatureInstanceData& CreatureData)
{
	return CreatureData.IsValid();
}

// =========================================================
// Compendium Formatting
// =========================================================

FString UGF_CreatureBlueprintLibrary::MetersToFeetInches(float Meters)
{
	if (Meters <= 0.0f)
	{
		return TEXT("0'00\"");
	}

	// Round to nearest whole inch — matches how official Compendium entries are rounded.
	// e.g. 0.5m = 19.685" → rounds to 20" → 1'08"
	const int32 TotalInches = FMath::RoundToInt(Meters * 39.3701f);
	const int32 Feet        = TotalInches / 12;
	const int32 Inches      = TotalInches % 12;

	// %02d zero-pads inches: 8 → "08"
	return FString::Printf(TEXT("%d'%02d\""), Feet, Inches);
}

FString UGF_CreatureBlueprintLibrary::KgToPounds(float Kg)
{
	if (Kg <= 0.0f)
	{
		return TEXT("0.0 lbs");
	}

	return FString::Printf(TEXT("%.1f lbs"), Kg * 2.20462f);
}

FString UGF_CreatureBlueprintLibrary::FormatHeightMetric(float Meters)
{
	return FString::Printf(TEXT("%.1f m"), Meters);
}

FString UGF_CreatureBlueprintLibrary::FormatWeightMetric(float Kg)
{
	return FString::Printf(TEXT("%.1f kg"), Kg);
}
