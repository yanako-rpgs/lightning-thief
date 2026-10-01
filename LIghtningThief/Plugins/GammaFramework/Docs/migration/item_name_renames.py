"""
Item-name renames -- the long tail.

After the enums were cleaned, the item names survived in three quieter places:

  1. comments explaining a mechanic ("Gen 3 Everstone rule", "Leftovers / Shell Bell")
  2. hardcoded FName strings in framework code (the Scavenger loot pools)
  3. magic constants (UGF_BreedingLibrary::EverstoneItemName)

All three are user-visible in the sense that matters: a developer reading this
plugin should not be reading someone else's item catalogue.

Applied to ALL text including comments, because every entry here is either
multi-word or distinctive enough that it cannot collide with ordinary prose.

DELIBERATELY NOT RENAMED -- these predate Pokemon and are ordinary RPG words:
  Potion, Revive, Repel, Ether, Escape Rope, Stardust, Berry, Gem, Plate
Only the specific product lines built on them are renamed (Super Potion, Full
Restore), so the generic words stay available for a project to use.
"""

ITEM_NAMES = {
    # -- held items, named for what they do ----------------------------------
    "Leftovers":      "Sustain Charm",
    "Shell Bell":     "Siphon Shell",
    "ShellBell":      "SiphonShell",
    "Black Sludge":   "Foul Sludge",
    "BlackSludge":    "FoulSludge",
    "Focus Sash":     "Last Stand Sash",
    "FocusSash":      "LastStandSash",
    "Focus Band":     "Grit Band",
    "FocusBand":      "GritBand",
    "Assault Vest":   "Bulwark Vest",
    "AssaultVest":    "BulwarkVest",
    "Life Orb":       "Blood Orb",
    "LifeOrb":        "BloodOrb",
    "Air Balloon":    "Lift Balloon",
    "AirBalloon":     "LiftBalloon",
    "Expert Belt":    "Adept Belt",
    "ExpertBelt":     "AdeptBelt",
    "Choice Band":    "Resolve Band",
    "ChoiceBand":     "ResolveBand",
    "Weakness Policy": "Reprisal Charm",
    "WeaknessPolicy": "ReprisalCharm",
    "Utility Umbrella": "Warding Parasol",
    "Lucky Punch":    "Keen Talon",
    "LuckyPunch":     "KeenTalon",
    "Lucky Egg":      "Fortune Egg",
    "LuckyEgg":       "FortuneEgg",
    "Amulet Coin":    "Coin Charm",
    "AmuletCoin":     "CoinCharm",
    "Destiny Knot":   "Bloodline Knot",
    "DestinyKnot":    "BloodlineKnot",
    "Everstone":      "AnchorStone",
    "Damp Rock":      "Rain Stone",
    "DampRock":       "RainStone",
    "Heat Rock":      "Sun Stone",
    "HeatRock":       "SunStone",
    "Icy Rock":       "Hail Stone",
    "IcyRock":        "HailStone",
    "Smooth Rock":    "Sand Stone",
    "SmoothRock":     "SandStone",
    "Mega Stone":     "Form Stone",
    "MegaStone":      "FormStone",
    "Z-Crystal":      "Burst Crystal",
    "ZCrystal":       "BurstCrystal",
    "Charcoal":       "Ember Charm",

    # -- consumables ----------------------------------------------------------
    "Rare Candy":     "Growth Candy",
    "RareCandy":      "GrowthCandy",
    "Super Potion":   "Major Salve",
    "SuperPotion":    "MajorSalve",
    "Full Restore":   "Full Salve",
    "FullRestore":    "FullSalve",
    "HeartScale":     "MemoryScale",
    "Heart Scale":    "Memory Scale",

    # -- berries --------------------------------------------------------------
    "Oran Berry":     "Mend Berry",
    "OranBerry":      "MendBerry",
    "Sitrus Berry":   "Vigor Berry",
    "SitrusBerry":    "VigorBerry",
    "Sitrus":         "Vigor",       # bare, in prose lists like "(Oran, Sitrus, ...)"
    "Lum Berry":      "Cleanse Berry",
    "LumBerry":       "CleanseBerry",

    # -- catch devices: align the loose STRINGS with the EGF_CoreType enum,
    #    which enum_value_renames.py already renamed. Missing these left the
    #    Scavenger loot pools handing out items whose names no longer exist.
    "GreatCore":      "KeenCore",
    "UltraCore":      "WardenCore",
    "MasterCore":     "AbsoluteCore",

    # -- species name that survived inside a UMETA label ----------------------
    "Arceus":         "a signature species",

    # Breeding items that decide which baby form hatches.
    "Sea Incense":    "Tide Charm",
    "Lax Incense":    "Idle Charm",
    "SeaIncense":     "TideCharm",
    "LaxIncense":     "IdleCharm",
    # Identifier forms first (longest wins), then the bare word. All are
    # space-free: "Incense" -> "Breeding Charm" would splice a space into
    # IncenseBaby and produce "Breeding CharmBaby", which will not compile.
    "IncenseBabySpecies": "CharmBabySpecies",
    "IncenseBaby":    "CharmBaby",
    "IncenseItem":    "CharmItem",
    "Incense":        "BreedingCharm",
}

# Applied with word boundaries so "Oran" cannot eat "Orange" (the dialogue
# decorator uses "Orange" as a text style name) and "Gem" is left alone.
WORD_BOUNDED = {"Oran", "Gem", "Plate", "Ether", "Repel", "Revive"}

# Identifier fixups: renames whose old form is embedded in a function name, so
# the word-bounded species/item passes cannot reach them.
IDENTIFIER_FIXUPS = {
    "IsDittoGroup": "IsUniversalBreeder",
    "DittoGroup":   "UniversalBreeder",
    # Breeding-taxonomy words that survived in comments after the enum went away.
    "Undiscovered group": "no breeding tags",
    "Undiscovered":  "untagged",
    "HumanLike":     "Humanoid",
    "egg group":     "breeding group",
    "Egg group":     "Breeding group",
    "egg groups":    "breeding groups",
    "Egg Group":     "Breeding Group",

    # Local variables the word-bounded Ditto rename could not reach.
    "bDittoA":       "bUniversalA",
    "bDittoB":       "bUniversalB",
    "Two Dittos":    "Two universal breeders",
    "Dittos":        "universal breeders",

    # Generation numbering and "the real games" reference another product's
    # release history, which means nothing inside a framework and announces
    # exactly what this was ported from.
    "Gen 5-6":       "classic",
    "Gen 5/6":       "classic",
    "Gen 3":         "classic",
    "Gen 4":         "classic",
    "Gen 5":         "classic",
    "Gen 6":         "classic",
    "Generation 3":  "classic",
    "the real games": "classic implementations",
    "in the real games": "in classic implementations",
    "in the games":  "in the genre",
    "mainline":      "classic",
}


import re


def rename_item_names(text):
    """Replace item names everywhere, longest first so multi-word wins."""
    for old in sorted(ITEM_NAMES, key=len, reverse=True):
        new = ITEM_NAMES[old]
        if old in WORD_BOUNDED:
            text = re.sub(r"\b" + re.escape(old) + r"\b", new, text)
        else:
            text = text.replace(old, new)
    for old, new in IDENTIFIER_FIXUPS.items():
        text = text.replace(old, new)
    return text
