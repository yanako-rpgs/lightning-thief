"""
Enum VALUE renames for temperaments and traits.

Renaming EPokemonNature -> EGF_Temperament and EPokemonAbility -> EGF_CreatureTrait
changes the type but leaves the values -- Adamant, Jolly, Intimidate, Levitate --
which are the part that is actually somebody else's expression. The systems and
their effect logic are mechanics and stay exactly as they are; only the labels
move.

Every mapping is 1:1 and meaning-preserving, so the stat-modifier matrix and the
trait effect implementations keep working untouched.

These are applied SCOPED -- inside the enum body, and at `EnumName::Value` call
sites -- never as a bare word-boundary replacement. Several old values are
ordinary tokens that appear all over an Unreal codebase (`Static`, `Trace`,
`Pickup`, `Damp`), and replacing those globally corrupts trace calls and item
pickup code.
"""

# ── Temperaments (were natures) ───────────────────────────────────────────────
# Each still applies the same +10% / -10% stat pair; only the name changes.
import re

TEMPERAMENTS = {
    "Adamant":  "Ferocious",
    "Bashful":  "Meek",
    "Bold":     "Stalwart",
    "Brave":    "Valiant",
    "Calm":     "Serene",
    "Careful":  "Guarded",
    "Docile":   "Placid",
    "Gentle":   "Tender",
    "Hardy":    "Robust",
    "Hasty":    "Rushed",
    "Impish":   "Wily",
    "Jolly":    "Sprightly",
    "Lax":      "Slack",
    "Lonely":   "Solitary",
    "Mild":     "Temperate",
    "Modest":   "Humble",
    "Naive":    "Innocent",
    "Naughty":  "Unruly",
    "Quiet":    "Silent",
    "Quirky":   "Peculiar",
    "Rash":     "Reckless",
    "Relaxed":  "Languid",
    "Sassy":    "Brash",
    "Serious":  "Stoic",
    "Timid":    "Skittish",
}

# ── Traits (were abilities) ───────────────────────────────────────────────────
# Names chosen so the effect still reads correctly from the label: the thing that
# prevents burns is still obviously the thing that prevents burns.
TRAITS = {
    # Tier 1 - passive flags and multipliers
    "WaterVeil":    "Dewshield",      # cannot be burned
    "MagmaArmor":   "Emberhide",      # cannot be frozen
    "Oblivious":    "Unfazed",        # immune to infatuation
    "Soundproof":   "Muffled",        # immune to sound skills
    "InnerFocus":   "Composed",       # cannot flinch
    "ClearBody":    "Unyielding",     # stats cannot be lowered
    "WhiteSmoke":   "Hazeform",       # stats cannot be lowered
    "KeenEye":      "Sharpsight",     # accuracy cannot be lowered
    "HugePower":    "Titanstrength",  # doubles Attack
    "PurePower":    "Innerforce",     # doubles Attack
    "ThickFat":     "Insulated",      # halves Ember and Frost damage
    "SwiftSwim":    "Currentborne",   # Speed up in rain
    "Chlorophyll":  "Sunfed",         # Speed up in sun
    "Guts":         "Adrenaline",     # Attack up while statused
    "Overgrow":     "Rootsurge",      # Verdant boost at low HP
    "Blaze":        "Cinderrage",     # Ember boost at low HP
    "Torrent":      "Tidesurge",      # Tide boost at low HP
    "Swarm":        "Hivecall",       # Verdant/insect boost at low HP
    "RockHead":     "Ironskull",      # no recoil damage
    "CompoundEyes": "Manyeyes",       # accuracy up
    "RunAway":      "Fleetfoot",      # always escapes
    "Static":       "Livewire",       # paralyses on contact
    "PoisonPoint":  "Venomspur",      # poisons on contact
    "EffectSpore":  "Sporeburst",     # random status on contact
    "CuteCharm":    "Beguile",        # infatuates on contact
    "FlameBody":    "Scorchhide",     # burns on contact
    "RoughSkin":    "Bramblehide",    # damages on contact
    "NaturalCure":  "Selfmend",       # cures status on switch out
    "ShedSkin":     "Moult",          # chance to cure status each turn
    "EarlyBird":    "Lightsleeper",   # wakes sooner
    "Trace":        "Echoform",       # copies the opponent's trait
    "Synchronize":  "Backlash",       # passes status back
    "Truant":       "Sluggard",       # acts every other turn
    "ShieldDust":   "Dustward",       # blocks added effects
    "StickyHold":   "Tightgrip",      # held item cannot be taken
    "LiquidOoze":   "Foulsap",        # drain skills hurt the drainer
    "Sturdy":       "Bulwark",        # survives a one-hit KO
    "RainDish":     "Rainfed",        # heals in rain
    "MagnetPull":   "Lodestone",      # traps Ferrous creatures
    "Pickup":       "Scavenger",      # finds items
    "Illuminate":   "Beacon",         # raises the encounter rate
    "Intimidate":   "Menace",         # lowers the foe's Attack on entry
    "SandVeil":     "Duststep",       # evasion up in sand
    "VitalSpirit":  "Wakeful",        # cannot fall asleep
    "SpeedBoost":   "Quickening",     # Speed rises each turn
    "WonderGuard":  "Aegis",          # only super-effective hits land
    "Levitate":     "Hover",          # immune to Stone
    "AirLock":      "Stillair",       # negates weather
    "Steadfast":    "Resolute",       # Speed up when flinched
    "Damp":         "Smother",        # blocks self-destruct skills
    "OwnTempo":     "Steadymind",     # cannot be confused
}


# Old value names that are ordinary English and must NOT be renamed in prose.
# "Bold" is the rich-text style tag; the rest are common adjectives and words
# that appear in comments meaning what they normally mean.
PROSE_DENY = {
    "Bold", "Static", "Trace", "Pickup", "Damp", "Guts",
    "Calm", "Quiet", "Serious", "Mild", "Rash", "Hardy", "Lax", "Brave",
    "Timid", "Docile", "Gentle", "Careful", "Naive", "Modest", "Relaxed",
    "Lonely", "Tender", "Placid", "Robust", "Silent", "Meek",
}


def rename_in_prose(text):
    """
    Rename trait and temperament names where they appear in COMMENTS.

    The enum values were renamed long before this, but the comments explaining
    them were not -- "Clear Body / White Smoke prevent stat drops" survived
    intact next to an enum that now says Unyielding and Hazeform. Those comments
    are the documentation, so leaving them is worse than useless: it tells the
    reader the wrong name.

    Both spellings are replaced -- "ClearBody" and "Clear Body" -- longest
    first so the spaced form wins before the bare word is considered.
    """
    pairs = []
    for mapping in (TRAITS, TEMPERAMENTS):
        for old, new in mapping.items():
            if old in PROSE_DENY:
                continue
            pairs.append((display_name(old), display_name(new)))
            pairs.append((old, new))
    for old, new in sorted(pairs, key=lambda p: len(p[0]), reverse=True):
        text = re.sub(r"\b" + re.escape(old) + r"\b", new, text)
    return text


def display_name(identifier):
    """Ferocious -> "Ferocious";  Titanstrength -> "Titanstrength"."""
    out = []
    for i, ch in enumerate(identifier):
        if i and ch.isupper() and not identifier[i - 1].isupper():
            out.append(" ")
        out.append(ch)
    return "".join(out)
