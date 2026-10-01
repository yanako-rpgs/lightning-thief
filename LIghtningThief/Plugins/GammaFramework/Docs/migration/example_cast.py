"""
The documentation cast.

Gamma Emerald's comments are unusually good -- most of them record a real bug
and why the guard exists ("a Treecko going 5 -> 7 was asked about 7, so Absorb
at 6 vanished"). That reasoning is worth keeping, but the worked examples were
all Pokemon.

So this is a small invented cast used ONLY in comments and log examples. None of
it is content the framework ships; it exists so a developer reading the source
sees a concrete example without reading somebody else's species list.

Grouped into families so the examples stay coherent when several appear in one
comment -- Sprigling evolving into Thornwood reads like a real line, whereas two
unrelated invented names would not.

DELIBERATELY NOT RENAMED -- ordinary English that happens to also be a move name
in another game: Protect, Rest, Dig, Fly, Dive, Bind, Counter, Reflect,
Teleport, Explosion, Substitute, Endure, Detect. Those describe mechanics here.
"""

SPECIES = {
    # starter families
    "Treecko": "Sprigling",   "Grovyle": "Thornling",   "Sceptile": "Thornwood",
    "Torchic": "Cindling",    "Combusken": "Emberkin",  "Blaziken": "Pyrelisk",
    "Mudkip": "Rivulet",      "Marshtomp": "Silthide",  "Swampert": "Rivergaunt",
    "Bulbasaur": "Budling",   "Venusaur": "Bloomcrown",
    "Charmander": "Cinderpup", "Charizard": "Emberwyrm",
    "Squirtle": "Shellpup",   "Blastoise": "Bastionshell",

    # the 1-HP shell used for every Aegis / one-hit-KO example
    "Shedinja": "Husk",
    "Nincada": "Grubling",    "Ninjask": "Skitterling",

    # common overworld examples
    "Pikachu": "Voltkit",     "Raichu": "Voltmane",
    "Poochyena": "Cubfang",   "Mightyena": "Direfang",
    "Zigzagoon": "Scampling", "Linoone": "Scamprowl",
    "Ralts": "Gleamling",     "Kirlia": "Gleamdancer", "Gardevoir": "Gleamwarden",
    "Wailmer": "Leviling",    "Wailord": "Leviath",
    "Magikarp": "Minnowling", "Gyarados": "Serpentide",
    "Eevee": "Kithling",      "Snorlax": "Slumberlith",
    "Absol": "Direomen",      "Sableye": "Gemgaze",     "Mawile": "Snaptrap",
    "Slakoth": "Idleling",    "Vigoroth": "Idlerouser", "Slaking": "Idlebrute",

    # metal family, used for the resistance examples
    "Magnemite": "Lodemote",  "Magneton": "Lodecluster",
    "Beldum": "Ironmote",     "Metang": "Ironclad",     "Metagross": "Ironmind",
    "Aron": "Oreling",        "Lairon": "Orehide",      "Aggron": "Oregarde",

    # oddities and legendaries
    "Ditto": "Shifter",       "Wobbuffet": "Wardling",
    "Mew": "Wisp",            "Mewtwo": "Wraithwisp",
    "Latias": "Aurelis",      "Latios": "Aurelian",
    "Rayquaza": "Skyrend",    "Kyogre": "Tidewrath",    "Groudon": "Terrawrath",
    "Regirock": "Stonewarden", "Regice": "Frostwarden", "Registeel": "Ironwarden",
    "Deoxys": "Voidform",     "Jirachi": "Wishmote",
    "Dialga": "Timewrend",    "Palkia": "Riftwrend",

    # Breeding-rule examples: baby forms, the gendered pair, and the
    # two-species-one-egg-group cases the sanctuary code documents.
    "Marill": "Dewpaw",       "Azurill": "Dewkit",
    "Wynaut": "Wardkit",
    "Nidoran": "Barbling",    "Nidorina": "Barbdoe",   "Nidorino": "Barbstag",
    "Volbeat": "Glimmer",     "Illumise": "Glimmara",
    "Slugma": "Cindercrawl",  "Magcargo": "Cindershell",
    "Nosepass": "Lodeface",   "Ludicolo": "Marshdancer",
}

SKILLS = {
    "Quick Attack":  "First Strike",
    "Fury Cutter":   "Rising Slash",
    "Close Combat":  "Onslaught",
    "Belly Drum":    "Blood Rite",
    "Sand-Attack":   "Grit Fling",
    "Sand Attack":   "Grit Fling",
    "Thunder Wave":  "Static Pulse",
    "Magical Leaf":  "Seeking Petal",
    "Light Screen":  "Veil",
    "Fire Spin":     "Ember Vortex",
    "Shadow Force":  "Phase Strike",
    "Absorb":        "Siphon",
    "Tackle":        "Ram",
    "Growl":         "Bluster",
    "Screech":       "Shriek",
    "Supersonic":    "Discord",
    "Overheat":      "Meltdown",
    "Flail":         "Death Throes",
    "Whirlwind":     "Displace",
    "Bide":          "Brace",
    "Splash":        "Flop",
    "Toxic":         "Envenom",
    "Haze":          "Nullify",
    "Wrap":          "Ensnare",
}


import re

# Multi-word entries must run before single words, and every replacement is
# word-bounded: "Mew" must not eat "Mewtwo", "Aron" must not eat "Aggron".
_ALL = dict(SKILLS)
_ALL.update(SPECIES)


def rename_examples(text):
    for old in sorted(_ALL, key=len, reverse=True):
        text = re.sub(r"\b" + re.escape(old) + r"\b", _ALL[old], text)
    return text
