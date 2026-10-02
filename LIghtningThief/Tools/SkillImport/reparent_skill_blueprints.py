"""
Reparents every skill Blueprint under /Game/BPS/ABILITIES/Skills to AGF_Skill.

AGF_Skill is the AGF_SkillDefinition subclass that can actually perform: it adds
the On Play Attack event and the Notify Impact / Notify Finished nodes. The battle
only plays visuals for skills that inherit from it (AGF_Skill::PlayAttack), so a
skill parented straight to AGF_SkillDefinition deals damage with no animation.

Blueprints already under AGF_Skill (or a Blueprint child of it) are left alone.
Data on the class defaults -- name, description, element, power -- carries over,
since AGF_Skill inherits every one of those properties.
"""

import unreal

ROOT = "/Game/BPS/ABILITIES/Skills"
NEW_PARENT_PATH = "/Script/GammaFrameworkBattle.GF_Skill"


def main():
    new_parent = unreal.load_class(None, NEW_PARENT_PATH)
    if new_parent is None:
        unreal.log_error(f"SkillReparent: could not load {NEW_PARENT_PATH}.")
        return

    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

    reparented = already = 0
    for path in assets.list_assets(ROOT, recursive=True, include_folder=False):
        bp = assets.load_asset(path)
        if not isinstance(bp, unreal.Blueprint):
            continue

        generated = unreal.BlueprintEditorLibrary.generated_class(bp)
        if unreal.MathLibrary.class_is_child_of(generated, new_parent):
            already += 1
            continue

        unreal.BlueprintEditorLibrary.reparent_blueprint(bp, new_parent)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assets.save_loaded_asset(bp, only_if_is_dirty=False)
        reparented += 1

    unreal.log(f"SkillReparent: {reparented} reparented to AGF_Skill, {already} already under it.")


main()
