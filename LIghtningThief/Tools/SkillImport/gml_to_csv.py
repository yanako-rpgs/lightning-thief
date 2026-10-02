"""
Converts the Dokimon GameMaker move data into moves.csv for create_skill_blueprints.py.

  python gml_to_csv.py <init_moves.gml> <english_moves.txt>

init_moves.gml holds one BUILD_MOVEDEX_ARRAY call per move; its first two
arguments are line ids into english_moves.txt (name, description). Argument
order and the optional key/value pairs follow BUILD_MOVEDEX_ARRAY_EXT.

Dokimon rules applied here rather than in the engine:
  - a flinch chance flinches, with or without the flinch flag
  - accuracy -1 means "cannot miss", which the plugin spells 0
  - support moves with nothing aimed at the enemy target the user
  - X-Slash, Double Cross and Combo Hit hit more than once (from their
    descriptions; the GML has no two-hit flag on them)
"""

import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "moves.csv")

ELEMENTS = {"light": "Light", "fire": "Fire", "grass": "Grass", "water": "Water", "electric": "Electric",
            "dark": "Dark", "flying": "Flying", "fight": "Fight", "poison": "Poison", "dragon": "Dragon",
            "fairy": "Fairy", "ghost": "Ghost", "ice": "Ice"}
TYPES = {"physical": "Physical", "magical": "Magic", "support": "Status"}
STATUSES = {"burn": "Burned", "poison": "Poisoned", "paralyze": "Paralyzed", "sleep": "Sleeping"}
STATS = {"atk": "Attack", "def": "Defense", "mgk_atk": "Magic", "mgk_def": "Poise",
         "spd": "Speed", "eva": "Evasion", "acc": "Accuracy"}

# Hits per move where the description promises more than one and the GML can't say so.
MULTI_HIT = {"X-Slash": (2, 2), "Double Cross": (2, 2), "Combo Hit": (2, 5)}

COLUMNS = ["Id", "Name", "AssetName", "Element", "Category", "Power", "Accuracy", "Uses", "Priority",
           "TargetSelf", "Status", "StatusChance", "FlinchChance", "HighCrit", "StatChance", "StatChanges",
           "Protect", "FirstTurn", "Recharge", "RecoilPercent", "HealAmount", "CleanseStatus", "CleanseStats",
           "MinHits", "MaxHits", "Description"]


def load_text(path):
    text = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"^(\d+) (.*)$", line.rstrip("\n"))
        if m:
            text[int(m.group(1))] = m.group(2).strip()
    return text


def asset_name(name):
    return "".join(w[:1].upper() + w[1:] for w in re.split(r"[^A-Za-z0-9]+", name) if w)


def parse_calls(source):
    # Strip comments, then pull every BUILD_MOVEDEX_ARRAY( ... ); including multi-line ones.
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)
    for m in re.finditer(r"BUILD_MOVEDEX_ARRAY\((.*?)\);", source, flags=re.S):
        args = [a.strip() for a in m.group(1).replace("\n", " ").split(",")]
        yield [a for a in args if a != ""]


def convert(args, text):
    name_id, desc_id = int(args[0]), int(args[1])
    power, element, mtype, accuracy, uses = args[4], args[5], args[6], args[7], args[8]
    pairs = args[9:]
    if len(pairs) % 2:
        raise ValueError(f"odd key/value list for text id {name_id}: {pairs}")

    row = {c: "" for c in COLUMNS}
    row.update(Id=name_id // 2, Name=text[name_id], Description=text[desc_id],
               Element=ELEMENTS[element.split(".")[1]], Category=TYPES[mtype.split(".")[1]],
               Power=max(0, int(power)), Accuracy=max(0, int(accuracy)), Uses=int(uses),
               Priority=0, StatusChance=0, FlinchChance=0, StatChance=100, RecoilPercent=0, HealAmount=0,
               MinHits=1, MaxHits=1)
    row["AssetName"] = asset_name(row["Name"])
    for flag in ("TargetSelf", "HighCrit", "Protect", "FirstTurn", "Recharge", "CleanseStatus", "CleanseStats"):
        row[flag] = 0

    changes = []
    for key, value in zip(pairs[0::2], pairs[1::2]):
        key = key.split(".")[1]
        if key == "chance_stat":       row["StatChance"] = int(value)
        elif key == "chance_status":   row["StatusChance"] = int(value)
        elif key == "status":          row["Status"] = STATUSES[value.split(".")[1]]
        elif key == "chance_flinch":   row["FlinchChance"] = int(value)
        elif key == "flinch":          pass  # a flinch chance alone flinches
        elif key == "hi_crit":         row["HighCrit"] = int(value)
        elif key == "priority":        row["Priority"] = int(value)
        elif key == "protect":         row["Protect"] = int(value)
        elif key == "firstturn":       row["FirstTurn"] = int(value)
        elif key == "recharge":        row["Recharge"] = int(value)
        elif key == "recoil":          pass  # implied by recoil_amnt
        elif key == "recoil_amnt":     row["RecoilPercent"] = int(value)
        elif key == "healing":         row["HealAmount"] = int(value)
        elif key == "cleanse_status":  row["CleanseStatus"] = int(value)
        elif key == "cleanse_stats":   row["CleanseStats"] = int(value)
        elif key in STATS:             changes.append(f"Self:{STATS[key]}:{int(value):+d}")
        elif key.startswith("e_") and key[2:] in STATS:
            changes.append(f"Enemy:{STATS[key[2:]]}:{int(value):+d}")
        else:
            raise ValueError(f"unknown move field '{key}' on {row['Name']}")

    row["StatChanges"] = ";".join(changes)

    # Support moves with no effect aimed at the enemy are cast on the user.
    aims_at_enemy = row["Status"] or any(c.startswith("Enemy:") for c in changes)
    row["TargetSelf"] = int(row["Category"] == "Status" and not aims_at_enemy)

    if row["Name"] in MULTI_HIT:
        row["MinHits"], row["MaxHits"] = MULTI_HIT[row["Name"]]
    return row


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    text = load_text(sys.argv[2])
    rows = [convert(args, text) for args in parse_calls(open(sys.argv[1], encoding="utf-8").read())]
    rows.sort(key=lambda r: r["Id"])

    ids = [r["Id"] for r in rows]
    if len(set(ids)) != len(ids):
        sys.exit("duplicate move ids")

    with open(OUT, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)
    print(f"{len(rows)} moves -> {OUT}")


main()
