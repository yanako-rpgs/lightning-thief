// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GF_CreatureTraitTypes.generated.h"

/**
 * Every trait in the game, strict classic (no Hidden Traits).
 *
 * Values are assigned EXPLICITLY and must never be renumbered — the value is what
 * gets written into a save file via FGF_CreatureInstanceData::Trait. Add new traits
 * at the end with the next free number; never insert in the middle.
 *
 * Grouped by authoring tier:
 *   Tier 1 - passive flag or single multiplier folded into an existing formula
 *   Tier 2 - needs a real battle-event hook (contact, switch, entry, end of turn)
 *   Tier 3 - overworld only, no battle code
 */
UENUM(BlueprintType)
enum class EGF_CreatureTrait : uint8
{
	None			= 0		UMETA(DisplayName = "None"),

	//------------------------------------------------------------------
	// TIER 1 - trivial passive flags / multipliers
	//------------------------------------------------------------------
	Dewshield		= 1		UMETA(DisplayName = "Dewshield"),
	Emberhide		= 2		UMETA(DisplayName = "Emberhide"),
	Unfazed		= 3		UMETA(DisplayName = "Unfazed"),
	Muffled		= 4		UMETA(DisplayName = "Muffled"),
	Composed		= 5		UMETA(DisplayName = "Composed"),
	Unyielding		= 6		UMETA(DisplayName = "Unyielding"),
	Hazeform		= 7		UMETA(DisplayName = "Hazeform"),
	Sharpsight			= 8		UMETA(DisplayName = "Sharpsight"),
	Titanstrength		= 9		UMETA(DisplayName = "Titanstrength"),
	Innerforce		= 10	UMETA(DisplayName = "Innerforce"),
	Insulated		= 11	UMETA(DisplayName = "Insulated"),
	Currentborne		= 12	UMETA(DisplayName = "Currentborne"),
	Sunfed		= 13	UMETA(DisplayName = "Sunfed"),
	Adrenaline			= 14	UMETA(DisplayName = "Adrenaline"),
	Rootsurge		= 15	UMETA(DisplayName = "Rootsurge"),
	Cinderrage			= 16	UMETA(DisplayName = "Cinderrage"),
	Tidesurge			= 17	UMETA(DisplayName = "Tidesurge"),
	Hivecall			= 18	UMETA(DisplayName = "Hivecall"),
	Ironskull		= 19	UMETA(DisplayName = "Ironskull"),
	Manyeyes	= 20	UMETA(DisplayName = "Manyeyes"),
	Fleetfoot			= 21	UMETA(DisplayName = "Fleetfoot"),

	//------------------------------------------------------------------
	// TIER 2 - needs a battle-event hook
	//------------------------------------------------------------------
	Livewire			= 22	UMETA(DisplayName = "Livewire"),
	Venomspur		= 23	UMETA(DisplayName = "Venomspur"),
	Sporeburst		= 24	UMETA(DisplayName = "Sporeburst"),
	Beguile		= 25	UMETA(DisplayName = "Beguile"),
	Scorchhide		= 26	UMETA(DisplayName = "Scorchhide"),
	Bramblehide		= 27	UMETA(DisplayName = "Bramblehide"),
	Selfmend		= 28	UMETA(DisplayName = "Selfmend"),
	Moult		= 29	UMETA(DisplayName = "Moult"),
	Lightsleeper		= 30	UMETA(DisplayName = "Lightsleeper"),
	Echoform			= 31	UMETA(DisplayName = "Echoform"),
	Backlash		= 32	UMETA(DisplayName = "Backlash"),
	Sluggard			= 33	UMETA(DisplayName = "Sluggard"),
	Dustward		= 34	UMETA(DisplayName = "Dustward"),
	Tightgrip		= 35	UMETA(DisplayName = "Tightgrip"),
	Foulsap		= 36	UMETA(DisplayName = "Foulsap"),
	Bulwark			= 37	UMETA(DisplayName = "Bulwark"),
	Rainfed		= 38	UMETA(DisplayName = "Rainfed"),
	Lodestone		= 39	UMETA(DisplayName = "Lodestone"),

	//------------------------------------------------------------------
	// TIER 3 - overworld only
	//------------------------------------------------------------------
	Scavenger			= 40	UMETA(DisplayName = "Scavenger"),
	Beacon		= 41	UMETA(DisplayName = "Beacon"),

	//------------------------------------------------------------------
	// ADDED AFTER THE ORIGINAL 41
	// Numbering is append-only — the value is what sits in save files, so new
	// entries go here regardless of which tier they belong to. Tier is resolved
	// by name in GetTraitTier(), not by value range.
	//------------------------------------------------------------------
	Menace		= 42	UMETA(DisplayName = "Menace"),		// Tier 2 - on entry
	Duststep		= 43	UMETA(DisplayName = "Duststep"),		// Tier 1 - evasion in sandstorm
	Wakeful		= 44	UMETA(DisplayName = "Wakeful"),	// Tier 1 - blocks sleep
	Quickening		= 45	UMETA(DisplayName = "Quickening"),		// Tier 2 - end of turn
	Aegis		= 46	UMETA(DisplayName = "Aegis"),	// Tier 1 - damage immunity
	Hover		= 47	UMETA(DisplayName = "Hover"),		// Tier 1 - Ground immunity
	Stillair			= 48	UMETA(DisplayName = "Stillair"),		// Tier 1 - suppresses weather
	Resolute		= 49	UMETA(DisplayName = "Resolute"),		// Tier 2 - on flinch
	Smother			= 50	UMETA(DisplayName = "Smother"),			// Tier 2 - blocks Selfdestruct / Explosion
	Steadymind		= 51	UMETA(DisplayName = "Steadymind"),		// Tier 1 - blocks confusion
};

/** Authoring/complexity tier an trait belongs to. Informational — drives no logic. */
UENUM(BlueprintType)
enum class EGF_TraitTier : uint8
{
	None			= 0		UMETA(DisplayName = "None"),
	Trivial			= 1		UMETA(DisplayName = "Tier 1 - Trivial"),
	EventHook		= 2		UMETA(DisplayName = "Tier 2 - Event Hook"),
	OverworldOnly	= 3		UMETA(DisplayName = "Tier 3 - Overworld Only"),
};
