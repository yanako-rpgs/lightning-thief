"""
Creates or updates one AGF_Skill Blueprint per row of moves.csv.

moves.csv comes from gml_to_csv.py (the Dokimon init_moves.gml). Every gameplay
field is filled in: name, description, element, category, power, accuracy, uses,
priority, targeting, status, flinch, crit, stat changes, protect, first-turn,
recharge, recoil, healing, cleansing and multi-hit. Visuals are not touched --
the On Play Attack graph is yours.

Assets live in /Game/BPS/ABILITIES/Skills/<Element>/BP_Move_<AssetName>, which is
where the gift command and the debugger look for skills.

Existing assets are skipped, so a re-run never overwrites hand edits. Run with
--update (or SKILL_IMPORT_ARGS=--update in the environment) to rewrite those
fields on existing assets too; that also moves an asset whose element changed
into the right folder. The Blueprint graph is never modified either way.

Run from the editor:   Tools > Execute Python Script... > this file
or headless:
  UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript -script="<this file>"
"""

import csv
import os
import sys

import unreal

CSV_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "moves.csv")
ROOT = "/Game/BPS/ABILITIES/Skills"
# AGF_Skill, not AGF_SkillDefinition: only AGF_Skill children get the On Play
# Attack event and Notify Impact / Notify Finished, and the battle only plays
# visuals for them.
PARENT_CLASS_PATH = "/Script/GammaFrameworkBattle.GF_Skill"

UPDATE_EXISTING = "--update" in sys.argv or "--update" in os.environ.get("SKILL_IMPORT_ARGS", "")


def enum(enum_type, name):
    return getattr(enum_type, name.upper())


def build_stat_changes(spec):
    changes = []
    for part in filter(None, spec.split(";")):
        side, stat, stages = part.split(":")
        change = unreal.GF_SkillStatChange()
        change.set_editor_property("stat", enum(unreal.GF_BattleStat, stat))
        change.set_editor_property("stages", int(stages))
        change.set_editor_property("affects_self", side == "Self")
        changes.append(change)
    return changes


def apply_row(cdo, row):
    flag = lambda key: row[key] == "1"
    uses = int(row["Uses"])
    min_hits, max_hits = int(row["MinHits"]), int(row["MaxHits"])
    recoil = float(row["RecoilPercent"])
    flinch = int(row["FlinchChance"])
    status_chance = int(row["StatusChance"])

    props = {
        "name": unreal.Name(row["Name"]),
        "description": row["Description"],
        "type": enum(unreal.GF_Element, row["Element"]),
        "split": enum(unreal.GF_SkillCategory, row["Category"]),
        "power": float(row["Power"]),
        "accuracy": float(row["Accuracy"]),     # 0 = cannot miss
        "max_uses": uses,
        "current_uses": uses,
        "priority": int(row["Priority"]),
        "target_shape": unreal.GF_SkillTargetShape.SELF if flag("TargetSelf") else unreal.GF_SkillTargetShape.SINGLE,
        "status": enum(unreal.GF_STATUSEffect, row["Status"]) if row["Status"] else unreal.GF_STATUSEffect.NONE,
        "status_chance": status_chance if row["Status"] else 0,
        "can_cause_flinch": flinch > 0,
        "flinch_chance": flinch,
        "is_high_crit_ratio": flag("HighCrit"),
        "affects_stat_stage": False,
        "stat_stage_chance": int(row["StatChance"]),
        "stat_changes": build_stat_changes(row["StatChanges"]),
        "is_protect_skill": flag("Protect"),
        "first_turn_only": flag("FirstTurn"),
        "requires_recharge": flag("Recharge"),
        "has_recoil": recoil > 0,
        "recoil_percentage": recoil if recoil > 0 else 25.0,
        "heal_amount": int(row["HealAmount"]),
        "is_refresh": flag("CleanseStatus"),
        "cleanses_stat_changes": flag("CleanseStats"),
        "is_looping_skill": max_hits > 1,
        "min_hits": max(1, min_hits),
        "max_hits": max(1, max_hits),
    }
    for key, value in props.items():
        cdo.set_editor_property(key, value)


def existing_skills(assets):
    """Asset name -> package path for every Blueprint already under ROOT."""
    found = {}
    for path in assets.list_assets(ROOT, recursive=True, include_folder=False):
        package = path.split(".")[0]
        found[package.rsplit("/", 1)[1]] = package
    return found


def main():
    parent = unreal.load_class(None, PARENT_CLASS_PATH)
    if parent is None:
        unreal.log_error(f"SkillImport: could not load {PARENT_CLASS_PATH}. Is the GammaFramework plugin built?")
        return

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    existing = existing_skills(assets)

    created = updated = moved = skipped = 0
    with open(CSV_PATH, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            folder = f"{ROOT}/{row['Element']}"
            asset = f"BP_Move_{row['AssetName']}"
            path = f"{folder}/{asset}"

            if asset in existing:
                if not UPDATE_EXISTING:
                    skipped += 1
                    continue
                if existing[asset] != path:
                    if not assets.rename_asset(existing[asset], path):
                        unreal.log_error(f"SkillImport: could not move {existing[asset]} to {path}")
                        continue
                    moved += 1
                bp = assets.load_asset(path)
                updated += 1
            else:
                factory = unreal.BlueprintFactory()
                factory.set_editor_property("parent_class", parent)
                bp = asset_tools.create_asset(asset, folder, unreal.Blueprint, factory)
                if bp is None:
                    unreal.log_error(f"SkillImport: failed to create {path}")
                    continue
                created += 1

            cdo = unreal.get_default_object(unreal.BlueprintEditorLibrary.generated_class(bp))
            cdo.modify()
            apply_row(cdo, row)

            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            assets.save_loaded_asset(bp, only_if_is_dirty=False)

    # A folder an update emptied (an element no move uses any more) goes too.
    for folder in assets.list_assets(ROOT, recursive=False, include_folder=True):
        folder = folder.rstrip("/")
        if not assets.list_assets(folder, recursive=True, include_folder=False):
            assets.delete_directory(folder)

    unreal.log(f"SkillImport: {created} created, {updated} updated ({moved} moved to a new element folder), "
               f"{skipped} skipped (already existed).")


main()
