"""
Second-wave enum VALUE renames.

The first pass (value_renames.py) covered temperaments and traits. This one
covers everything the audit found afterwards -- enum values that were still
named after specific Pokemon items, balls, growth rates and moves.

Format: ENUM_NAME -> { OldIdentifier: (NewIdentifier, "New Display Name") }

The display name matters as much as the identifier. Several of these carried
the original item name in the UMETA label even where the identifier looked
innocent -- "HP Restore (Oran Berry)", "Plate (Type boost + Arceus form)" --
and UMETA labels are what a designer reads in the editor dropdown.

Guiding principle for the held-item effects: an EFFECT enum should be named
after the effect, not after one item that happens to cause it. "Leftovers"
tells you nothing; "RegenEachTurn" tells you everything, and lets any number of
items in a project share it without inventing a value each.
"""

HELD_ITEM_EFFECT = {
    "StatBoost":       ("FlatStatBoost",         "Flat Stat Boost"),
    "TypeBoost":       ("ElementBoost",          "Element Boost"),
    "HPRestore":       ("HPRestore",             "HP Restore"),
    "StatusCure":      ("StatusCure",            "Status Cure"),
    "Leftovers":       ("RegenEachTurn",         "Regenerate Each Turn"),
    "BlackSludge":     ("RegenIfVenom",          "Regenerate If Venom, Else Harm"),
    "FocusSash":       ("SurviveLethalOnce",     "Survive Lethal Hit (once, from full)"),
    "FocusBand":       ("SurviveLethalChance",   "Survive Lethal Hit (chance)"),
    "AssaultVest":     ("PoiseUpNoStatus",       "Poise Up, No Status Skills"),
    "AirBalloon":      ("FloatAboveTerra",       "Float (immune to Terra)"),
    "LifeOrb":         ("PowerAtHPCost",         "Power Boost With Recoil"),
    "ChoiceItem":      ("LockSkillPowerUp",      "Lock Skill, Power Up"),
    "ExpertBelt":      ("SuperEffectiveBoost",   "Super-Effective Boost"),
    "EXPBoost":        ("EXPBoost",              "EXP Boost"),
    "MoneyBoost":      ("MoneyBoost",            "Money Boost"),
    "EXPShare":        ("EXPShare",              "EXP Share"),
    "LuckyPunch":      ("CritBoost",             "Critical Hit Boost"),
    "DampRock":        ("ExtendRain",            "Extend Rain"),
    "HeatRock":        ("ExtendSun",             "Extend Harsh Sun"),
    "IcyRock":         ("ExtendHail",            "Extend Hail"),
    "SmoothRock":      ("ExtendSandstorm",       "Extend Sandstorm"),
    "Gem":             ("OneShotElementBoost",   "One-Time Element Boost"),
    "Plate":           ("HeldElementBoost",      "Held Element Boost"),
    "MegaStone":       ("FormChangeStone",       "Form Change Stone"),
    "ZCrystal":        ("BurstCharge",           "One-Time Burst Charge"),
    "TraitShield":     ("TraitShield",           "Trait Shield"),
    "DestinyKnot":     ("InheritMorePotential",  "Inherit More Potential"),
    "Everstone":       ("PreventEvolution",      "Prevent Evolution"),
    "UtilityUmbrella": ("WeatherImmunity",       "Weather Immunity"),
    "WeaknessPolicy":  ("BoostOnSuperEffective", "Boost When Hit Super-Effectively"),
}

# Catch devices. The first four are a strength ladder; the rest are conditional,
# and each is now named for the condition it keys off.
CORE_TYPE = {
    "Core":        ("Core",         "Core"),
    "GreatCore":   ("KeenCore",     "Keen Core"),
    "UltraCore":   ("WardenCore",   "Warden Core"),
    "MasterCore":  ("AbsoluteCore", "Absolute Core"),
    "SafariCore":  ("WildsCore",    "Wilds Core"),
    "NetCore":     ("MeshCore",     "Mesh Core"),
    "DiveCore":    ("DepthCore",    "Depth Core"),
    "NestCore":    ("CradleCore",   "Cradle Core"),
    "RepeatCore":  ("SnareCore",    "Snare Core"),
    "TimerCore":   ("VigilCore",    "Vigil Core"),
    "LuxuryCore":  ("ComfortCore",  "Comfort Core"),
    "PremierCore": ("TokenCore",    "Token Core"),
    "DuskCore":    ("NightCore",    "Night Core"),
    "HealCore":    ("MendCore",     "Mend Core"),
    "QuickCore":   ("SwiftCore",    "Swift Core"),
    "CherishCore": ("GiftCore",     "Gift Core"),
    # ShimmerCore was Gamma Emerald's own invention, not a borrowed name.
}

TAMER_HEAL_ITEM = {
    "Potion":      ("MinorHeal",  "Minor Heal"),
    "SuperPotion": ("MajorHeal",  "Major Heal"),
    "FullRestore": ("FullHeal",   "Full Heal"),
}

ITEM_TYPE = {
    "RareCandy": ("LevelBoost", "Level Boost"),
}

FISHING_ROD = {
    "OldRod":   ("SimpleRod", "Simple Rod"),
    "GoodRod":  ("KeenRod",   "Keen Rod"),
    "SuperRod": ("DeepRod",   "Deep Rod"),
}

SEMI_INVULNERABLE = {
    "ShadowForce": ("PhaseShifted", "Phase Shifted"),
}

# Growth rates. The six-curve idea is a mechanic and stays; the specific set of
# names was someone else's.
EXP_CURVES = {
    "Erratic":     ("Volatile", "Volatile"),
    "Fast":        ("Swift",    "Swift"),
    "MediumFast":  ("Steady",   "Steady"),
    "MediumSlow":  ("Measured", "Measured"),
    "Slow":        ("Gradual",  "Gradual"),
    "Fluctuating": ("Uneven",   "Uneven"),
}

# enum name -> mapping
ENUM_VALUE_MAPS = {
    "EGF_HeldItemEffect":        HELD_ITEM_EFFECT,
    "EGF_CoreType":              CORE_TYPE,
    "EGF_TamerHealItem":         TAMER_HEAL_ITEM,
    "EGF_ItemType":              ITEM_TYPE,
    "EGF_FishingRod":            FISHING_ROD,
    "EGF_SemiInvulnerableState": SEMI_INVULNERABLE,
    "EGF_EXPCurves":             EXP_CURVES,
}


import re


def _enum_body_span(text, enum_name):
    """Character span of one enum's { ... } body, or None."""
    m = re.search(r"enum class " + enum_name + r"\s*:\s*\w+\s*\{", text)
    if not m:
        return None
    start = text.index("{", m.start())
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i
    return None


def rename_enum_value_labels(text):
    """
    Rename enum values AND their UMETA display labels together.

    Kept separate from the temperament/trait pass because each value here needs
    a specific new label rather than one derived from the identifier. The label
    matters: several of these smuggled an item name the identifier did not --
    "HP Restore (Oran Berry)", "Plate (Type boost + Arceus form)" -- and the
    UMETA label is exactly what a designer sees in the editor dropdown.

    Renames are scoped to the enum body plus EnumName::Value call sites, never
    bare word-boundary replacement: values like Gem, Plate, Core and Fast are
    ordinary words elsewhere in the codebase.
    """
    for enum_name, mapping in ENUM_VALUE_MAPS.items():
        span = _enum_body_span(text, enum_name)
        if span:
            start, end = span
            body = text[start:end]
            for old, (new, label) in mapping.items():
                # Declaration line carrying a UMETA label: rewrite both halves.
                body = re.sub(
                    r"(^[ \t]*)" + old + r"\b([^\n]*?)UMETA\(DisplayName = \"[^\"]*\"\)",
                    lambda mm, n=new, l=label: (mm.group(1) + n + mm.group(2)
                                                + 'UMETA(DisplayName = "' + l + '")'),
                    body, flags=re.M)
                # Declaration line with no label at all.
                body = re.sub(r"(^[ \t]*)" + old + r"\b(?![\w(])",
                              lambda mm, n=new: mm.group(1) + n,
                              body, flags=re.M)
            text = text[:start] + body + text[end:]

        for old, (new, _label) in mapping.items():
            text = re.sub(r"\b" + enum_name + r"::" + old + r"\b",
                          enum_name + "::" + new, text)
    return text
