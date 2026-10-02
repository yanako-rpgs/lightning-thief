"""
Creates one AGF_Skill Blueprint per row of moves.csv.

Fills in Name, Description, element (Type) and Split (Physical / Magic / Status)
only. Power, accuracy, uses, priority and every effect are left at their defaults
for a designer to set by hand.

Assets land in /Game/BPS/ABILITIES/Skills/<Element>/BP_Move_<AssetName>, which is
where the gift command and the debugger already look for skills.

Existing assets are skipped, so re-running never overwrites hand edits. Pass
--update to rewrite Name / Description / Type / Split on existing assets too.

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


def main():
    parent = unreal.load_class(None, PARENT_CLASS_PATH)
    if parent is None:
        unreal.log_error(f"SkillImport: could not load {PARENT_CLASS_PATH}. Is the GammaFramework plugin built?")
        return

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

    created = updated = skipped = 0
    with open(CSV_PATH, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            folder = f"{ROOT}/{row['Element']}"
            asset = f"BP_Move_{row['AssetName']}"
            path = f"{folder}/{asset}"

            if assets.does_asset_exist(path):
                if not UPDATE_EXISTING:
                    skipped += 1
                    continue
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
            cdo.set_editor_property("name", unreal.Name(row["Name"]))
            cdo.set_editor_property("description", row["Description"])
            cdo.set_editor_property("type", getattr(unreal.GF_Element, row["Element"].upper()))
            cdo.set_editor_property("split", getattr(unreal.GF_SkillCategory, row["Category"].upper()))

            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            assets.save_loaded_asset(bp, only_if_is_dirty=False)

    unreal.log(f"SkillImport: {created} created, {updated} updated, {skipped} skipped (already existed).")


main()
