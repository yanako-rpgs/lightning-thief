"""
World -> Creatures decoupling patches, applied by migrate_from_gamma_emerald.py.

GammaFrameworkWorld must not reference GammaFrameworkCreatures: UBT rejects a
circular module dependency, and the whole point of the split is that a project
can take the grid/dialogue/quest tech without the creature layer.

Gamma Emerald had six places where core systems reached into the creature code.
Two are fixed by widening a type; the other four now go through FGF_CreatureBridge,
whose hooks GammaFrameworkCreatures binds at module startup.

Each entry is (file_suffix, old_text, new_text). old_text must appear verbatim
in the migrated output -- the migration asserts on every miss rather than
silently skipping, because a patch that stops matching after an upstream edit
would otherwise reintroduce the dependency without saying so.
"""

INCLUDE_BRIDGE = '#include "GF_CreatureBridge.h"'

PATCHES = [

    # ── GF_ItemData.h ────────────────────────────────────────────────────────
    # Two includes, both removable. EGF_Element moved to the World module, so the species
    # include is simply obsolete. TeachableSkill widens to AActor: item data
    # only ever stores and forwards the class, it never touches skill members.
    ("GF_ItemData.h",
     '#include "GF_SkillDefinition.h"',
     '// Skill classes are referenced loosely here -- see TeachableSkill below.'),

    ("GF_ItemData.h",
     '#include "GF_CreatureSpeciesData.h"  // For EGF_Element',
     '#include "GF_ElementTypes.h"  // EGF_Element'),

    ("GF_ItemData.h",
     'TSoftClassPtr<AGF_SkillDefinition> TeachableSkill;',
     'TSoftClassPtr<AActor> TeachableSkill;   // AGF_SkillDefinition subclass.\n'
     '    // Deliberately loose: the World module owns items, Creatures owns\n'
     '    // skills, and World cannot name a Creatures type. Cast on that side.'),

    # ── GF_GridMovementComponent.cpp ─────────────────────────────────────────
    ("GF_GridMovementComponent.cpp",
     '#include "GF_CreatureManagerSubsystem.h"',
     INCLUDE_BRIDGE),

    ("GF_GridMovementComponent.cpp",
     '''        if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        {
            if (const UGF_CreatureManagerSubsystem* PartyManager = GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>())
            {
                FGF_CreatureInstanceData LeadCreature;
                if (PartyManager->GetPartyCreatureData(0, LeadCreature))
                {
                    EncounterChance *= UGF_CreatureTraitLibrary::GetEncounterRateMultiplier(
                        UGF_CreatureTraitLibrary::GetInstanceTrait(LeadCreature));
                }
            }
        }''',
     '''        // Returns 1.0 when the Creatures module is absent, leaving the rate alone.
        EncounterChance *= FGF_CreatureBridge::EncounterRateMultiplier(this);'''),

    ("GF_GridMovementComponent.cpp",
     '''                if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
                {
                    if (UGF_CreatureManagerSubsystem* CreatureMgr = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
                    {
                        if (LandedProps.bIsDungeonEntrance)
                        {
                            FGF_GridCoordinate OutdoorOffset = UGF_GridWorldSubsystem::GetDirectionOffset(CurrentFacing);
                            FGF_GridCoordinate ReturnTile = FlatLandedPos - OutdoorOffset;
                            ReturnTile.Z = 0;
                            CreatureMgr->RegisterDungeonEntrance(ReturnTile);
                        }
                        else // bIsDungeonExit
                        {
                            CreatureMgr->ClearDungeonState();
                        }
                    }
                }''',
     '''                if (LandedProps.bIsDungeonEntrance)
                {
                    if (FGF_CreatureBridge::RegisterDungeonEntrance)
                    {
                        FGF_GridCoordinate OutdoorOffset = UGF_GridWorldSubsystem::GetDirectionOffset(CurrentFacing);
                        FGF_GridCoordinate ReturnTile = FlatLandedPos - OutdoorOffset;
                        ReturnTile.Z = 0;
                        FGF_CreatureBridge::RegisterDungeonEntrance(this, ReturnTile);
                    }
                }
                else // bIsDungeonExit
                {
                    if (FGF_CreatureBridge::ClearDungeonState)
                    {
                        FGF_CreatureBridge::ClearDungeonState(this);
                    }
                }'''),

    # ── GF_GameResetLibrary.cpp ──────────────────────────────────────────────
    ("GF_GameResetLibrary.cpp",
     '#include "GF_CreatureManagerSubsystem.h"',
     INCLUDE_BRIDGE),

    ("GF_GameResetLibrary.cpp",
     '''    if (UGF_CreatureManagerSubsystem* PMS = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
    {
        PMS->DeleteSave();
        UE_LOG(LogTemp, Log, TEXT("GameReset: New Game - save deleted, clearing subsystems."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: CreatureManagerSubsystem missing - could not "
            "delete the save for New Game."));
    }''',
     '''    if (FGF_CreatureBridge::DeleteCreatureSave)
    {
        FGF_CreatureBridge::DeleteCreatureSave(GI);
        UE_LOG(LogTemp, Log, TEXT("GameReset: New Game - save deleted, clearing subsystems."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: creature layer not bound - could not "
            "delete the save for New Game."));
    }'''),

    ("GF_GameResetLibrary.cpp",
     '''    if (UGF_CreatureManagerSubsystem* PMS = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
    {
        const bool bLoaded = PMS->LoadGame();''',
     '''    if (FGF_CreatureBridge::LoadCreatureSave)
    {
        const bool bLoaded = FGF_CreatureBridge::LoadCreatureSave(GI);'''),

    ("GF_GameResetLibrary.cpp",
     '''        UE_LOG(LogTemp, Error, TEXT("GameReset: CreatureManagerSubsystem missing - state was cleared "
            "but NOT reloaded. The next run will look like a fresh save."));''',
     '''        UE_LOG(LogTemp, Error, TEXT("GameReset: creature layer not bound - state was cleared "
            "but NOT reloaded. The next run will look like a fresh save."));'''),

    # ── Dialogue/GF_DialogueSubsystem.cpp ────────────────────────────────────
    ("GF_DialogueSubsystem.cpp",
     '#include "GF_CreatureManagerSubsystem.h"',
     INCLUDE_BRIDGE),

    ("GF_DialogueSubsystem.cpp",
     '#include "GF_VaultSystem.h"',
     '// Player profile is read through FGF_CreatureBridge, not the vault directly.'),

    ("GF_DialogueSubsystem.cpp",
     '''            if (UGF_CreatureManagerSubsystem* PM = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
            {
                if (PM->VaultSystem)
                {
                    Gender = PM->VaultSystem->PlayerGender;''',
     '''            {
                {
                    Gender = static_cast<EGF_PlayerGender>(FGF_CreatureBridge::PlayerGender(this));'''),

    # ── Settings/GF_SettingsSubsystem.cpp ────────────────────────────────────
    ("GF_SettingsSubsystem.cpp",
     '#include "GF_CreatureManagerSubsystem.h"',
     INCLUDE_BRIDGE),

    ("GF_SettingsSubsystem.cpp",
     '#include "GF_VaultSystem.h"',
     '// The settings mirror is pushed through FGF_CreatureBridge.'),

    ("GF_SettingsSubsystem.cpp",
     '''	// The mirror writes into VaultSystem, so the manager has to exist first.
	Collection.InitializeDependency(UGF_CreatureManagerSubsystem::StaticClass());''',
     '''	// The mirror used to force the creature manager to initialise first. It no
	// longer can -- that would be a Core -> Creatures dependency. The direction is
	// inverted instead: GammaFrameworkCreatures declares a dependency on THIS
	// subsystem and calls RequestVaultSync() once its hooks are bound, which
	// re-pushes the mirror. Until then PushSettingsMirror is unbound and
	// SyncToVaultSystem is a no-op, exactly as it was when the manager was absent.'''),

    ("GF_SettingsSubsystem.cpp",
     '''UGF_VaultSystem* UGF_SettingsSubsystem::GetVaultSystem() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UGF_CreatureManagerSubsystem* Manager = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
		{
			return Manager->VaultSystem;
		}
	}
	return nullptr;
}

void UGF_SettingsSubsystem::SyncToVaultSystem()
{
	UGF_VaultSystem* Vault = GetVaultSystem();
	if (!Vault || !Save)
	{
		return;
	}

	Vault->TextSpeed          = TextSpeedToString(Save->TextSpeed);
	Vault->FrameID            = Save->FrameID;
	Vault->Difficulty         = DifficultyToString(Save->Difficulty);
	Vault->WindowedModes      = WindowModeToString(Save->WindowMode);
	Vault->Resolution         = Save->Resolution;
	Vault->MasterVolume       = Save->MasterVolume;
	Vault->SoundEffectsVolume = Save->SoundEffectsVolume;
	Vault->MusicVolume        = Save->MusicVolume;
	Vault->bEXPShareEnabled   = IsEXPShareActive();
	Vault->bDOFEnabled        = Save->bDOFEnabled;
	Vault->bAutoRepelEnabled  = Save->bAutoRepelEnabled;
}''',
     '''void UGF_SettingsSubsystem::RequestVaultSync()
{
	SyncToVaultSystem();
}

void UGF_SettingsSubsystem::SyncToVaultSystem()
{
	// No-op until GammaFrameworkCreatures binds the hook. Same observable
	// behaviour as the old "no vault yet, return early" path.
	if (!Save || !FGF_CreatureBridge::PushSettingsMirror)
	{
		return;
	}

	FGF_SettingsMirror Mirror;
	Mirror.TextSpeed          = TextSpeedToString(Save->TextSpeed);
	Mirror.FrameID            = Save->FrameID;
	Mirror.Difficulty         = DifficultyToString(Save->Difficulty);
	Mirror.WindowedModes      = WindowModeToString(Save->WindowMode);
	Mirror.Resolution         = Save->Resolution;
	Mirror.MasterVolume       = Save->MasterVolume;
	Mirror.SoundEffectsVolume = Save->SoundEffectsVolume;
	Mirror.MusicVolume        = Save->MusicVolume;
	Mirror.bEXPShareEnabled   = IsEXPShareActive();
	Mirror.bDOFEnabled        = Save->bDOFEnabled;
	Mirror.bAutoRepelEnabled  = Save->bAutoRepelEnabled;

	FGF_CreatureBridge::PushSettingsMirror(this, Mirror);
}'''),

    # Regex rather than a literal: this body is long and tab-indented, and an
    # exact transcription silently stopped matching once. The migration reports
    # any patch that fails to match, which is how that was caught.
    ("GF_SettingsSubsystem.cpp",
     "RE:" + r"const UGF_VaultSystem\* Vault = GetVaultSystem\(\);.*?"
             r"return FMath::Max\(Counted, Vault->TotalEmblemAmount\);",
     "\n".join([
         "// Counting the individual emblem flags is the Creatures side's job now --",
         "\t// it owns the save object they live on. The important part of the",
         "\t// original fix survives there: count the per-emblem bools rather than",
         "\t// trusting the TotalEmblemAmount counter, which is not kept in sync",
         "\t// when a trial is beaten and reports 0 for a player who has earned",
         "\t// several, pinning the level cap at its first-trial value and blocking",
         "\t// all EXP.",
         "\treturn FGF_CreatureBridge::EmblemCount(this);",
     ])),

    # ── Settings/GF_SettingsSubsystem.h ──────────────────────────────────────
    ("GF_SettingsSubsystem.h",
     '	UGF_VaultSystem* GetVaultSystem() const;',
     '''	// Re-push the settings mirror. GammaFrameworkCreatures calls this once its
	// FGF_CreatureBridge hooks are bound, replacing the old InitializeDependency
	// ordering guarantee that Core can no longer express.
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Settings")
	void RequestVaultSync();'''),

    # Dead once GetVaultSystem() is gone -- harmless, but it is the last thing in
    # the World module that names a Creatures type, and leaving it invites use.
    ("GF_SettingsSubsystem.h",
     "class UGF_VaultSystem;\n",
     ""),

    # ── Dialogue GiveCreature event ──────────────────────────────────────────
    # The whole event handler went through the creature manager. It now asks the
    # bridge for the two things it actually needs: grant the creature, and name
    # the vault page it landed in.
    ("GF_DialogueSubsystem.cpp",
     "RE:" + r"UGameInstance\* GI = GetGameInstance\(\);\s*\n\s*UGF_CreatureManagerSubsystem\* PMS.*?"
             r"UGF_CreatureSpeciesData\* Species = PMS->GetCreatureSpeciesData\(Event\.CreatureSpeciesName\);\s*\n"
             r"\s*if \(!Species\)\s*\n\s*\{.*?\n\s*\}",
     "\n".join([
         "            if (!FGF_CreatureBridge::GiveCreature)",
         "            {",
         '                UE_LOG(LogTemp, Error, TEXT("Dialogue GiveCreature: creature layer not bound"));',
         "                break;",
         "            }",
     ])),

    ("GF_DialogueSubsystem.cpp",
     "RE:" + r"const bool bGiven = PMS->GiveCreatureAtLevelTracked\(Species, "
             r"Event\.GiftCreatureLevel, VaultPageIndex\);",
     "const bool bGiven = FGF_CreatureBridge::GiveCreature(\n"
     "                this, Event.CreatureSpeciesName, Event.GiftCreatureLevel, VaultPageIndex);"),

    ("GF_DialogueSubsystem.cpp",
     "RE:" + r"if \(VaultPageIndex >= 0 && PMS->VaultSystem\)\s*\n\s*\{\s*\n"
             r"\s*DialogueVariables\.Add\(FName\(\"Vault\"\), "
             r"PMS->VaultSystem->GetVaultPageName\(VaultPageIndex\)\);\s*\n\s*\}",
     "\n".join([
         "if (VaultPageIndex >= 0 && FGF_CreatureBridge::GetVaultPageName)",
         "            {",
         '                DialogueVariables.Add(FName("Vault"),',
         "                    FGF_CreatureBridge::GetVaultPageName(this, VaultPageIndex));",
         "            }",
     ])),

    # ── GF_VaultSystem.h ─────────────────────────────────────────────────────
    # EGF_PlayerGender moves to Core so the dialogue system can name it. Strip
    # the duplicate definition here and pull in the Core header instead.
    ("GF_VaultSystem.h",
     '#include "GF_GridWorldSubsystem.h"',
     '#include "GF_GridWorldSubsystem.h"\n#include "GF_CreatureBridge.h"   // EGF_PlayerGender'),

    ("GF_VaultSystem.h",
     "RE:" + r"UENUM\s*\(BlueprintType\)\s*\nenum class EGF_PlayerGender : uint8\s*\{[^}]*\};",
     "// EGF_PlayerGender now lives in GF_CreatureBridge.h (World): the dialogue\n"
     "// system substitutes gendered text and cannot depend on Creatures."),

    # ── GF_CreatureManagerSubsystem.cpp ──────────────────────────────────────
    # ItemData::TeachableSkill widened to TSoftClassPtr<AActor> so the World module
    # could own items. Rebuild the typed pointer from its path on this side.
    ("GF_CreatureManagerSubsystem.cpp",
     "const TSoftClassPtr<AGF_SkillDefinition>& TomeMove = ItemData->TeachableSkill;",
     "const TSoftClassPtr<AGF_SkillDefinition> TomeMove(ItemData->TeachableSkill.ToSoftObjectPath());"),

    # Second creature include in the grid component, alongside the manager one.
    ("GF_GridMovementComponent.cpp",
     '#include "GF_CreatureTraits.h"\n',
     ''),

    # ── Dialogue/GF_DialogueTypes.h ──────────────────────────────────────────
    # FGF_DialogueEvent held TSubclassOf<ATrainerMaster>, the single property
    # dragging the creature layer into the dialogue system. The subsystem only
    # spawns it and hands it to the battle side, so it never needed the type.
    ("GF_DialogueTypes.h",
     "class AGF_TamerMaster;\n",
     ""),

    ("GF_DialogueTypes.h",
     "TSubclassOf<AGF_TamerMaster> TamerClass;",
     "TSubclassOf<AActor> TamerClass;   // an AGF_TamerMaster subclass, kept loose\n"
     "    // so GammaFrameworkWorld does not depend on GammaFrameworkCreatures."),

    # ── GF_SkillDefinition.h ─────────────────────────────────────────────────
    # Status conditions move to World: items cure them, and items live in World.
    ("GF_SkillDefinition.h",
     "RE:" + r"UENUM\s*\(BlueprintType\)\s*\nenum class EGF_STATUSEffect : uint8\s*\{[^}]*\};",
     "// EGF_STATUSEffect now lives in GF_StatusTypes.h (World), reached through\n"
     "// GF_ElementTypes.h. Items cure status and items live in the World module,\n"
     "// so the enum cannot live in Creatures."),

    # GF_CreatureTraits.h names UGF_BattleComponent only in comments, so the
    # include is dead weight -- and it is the last thing pulling Battle into
    # Creatures.
    ("GF_CreatureTraits.h",
     '#include "GF_BattleComponent.h"',
     '// UGF_BattleComponent is referenced in comments only; no include needed.'),

    # ══════════════════════════════════════════════════════════════════════════
    # Creatures -> Battle. Same rule one layer down: the data layer must survive
    # the battle system being replaced, so it cannot name a battle type.
    # ══════════════════════════════════════════════════════════════════════════

    # Skills ask the battle layer whether escape is blocked, rather than reaching
    # into UGF_BattleComponent and the trait library directly.
    ("GF_SkillDefinition.cpp",
     "RE:" + r"if \(UGF_BattleComponent::IsPartiallyTrapped\(User\)\s*\n"
             r"\s*&& !UGF_CreatureTraitLibrary::CanAlwaysFleeFromWild\("
             r"UGF_CreatureTraitLibrary::GetActorTrait\(User\)\)\)",
     "if (FGF_BattleBridge::EscapeBlocked(User))"),

    ("GF_SkillDefinition.cpp",
     '#include "GF_BattleComponent.h"',
     '#include "GF_BattleBridge.h"'),

    # The manager asks the bridge for the held-item EXP boost.
    ("GF_CreatureManagerSubsystem.cpp",
     "ExpGained = UGF_HeldItemBattleHelper::ApplyEXPBoostFromData(this, Data, ExpGained);",
     "ExpGained = FGF_BattleBridge::HeldItemEXPBoost(this, Data, ExpGained);"),

    ("GF_CreatureManagerSubsystem.cpp",
     '#include "GF_HeldItemBattleHelper.h"',
     '#include "GF_BattleBridge.h"'),

    # ══════════════════════════════════════════════════════════════════════════
    # Content that leaked into the framework.
    #
    # A reusable plugin must not hardcode one game's creatures. These two enums
    # were Gamma Emerald's roster and Pokemon's breeding taxonomy sitting in
    # what is supposed to be genre-agnostic code.
    # ══════════════════════════════════════════════════════════════════════════

    # EGF_PickedStarter was literally { Treecko, Torchic, Mudkip }. Which
    # creatures a game offers is the game's business, so the save just records
    # the species by name.
    ("GF_VaultSystem.h",
     "RE:" + r"UENUM\s*\(BlueprintType\)\s*\nenum class EGF_PickedStarter : uint8\s*\{[^}]*\};",
     "// EGF_PickedStarter used to hardcode one game's three starters. A framework\n"
     "// cannot know those, so the save records the chosen species by name instead\n"
     "// -- see PickedStarter below."),

    ("GF_VaultSystem.h",
     "RE:" + r"EGF_PickedStarter PickedStarter = EGF_PickedStarter::\w+;",
     "FName PickedStarter = NAME_None;   // species the player chose; project-defined"),

    # ── Breeding groups: enum -> data-driven tags ────────────────────────────
    # The old enum was Pokemon's egg-group taxonomy, Ditto and Undiscovered
    # included. Tags let each project define its own groups without editing
    # plugin C++, and the two magic values become honest flags.
    ("GF_CreatureSpeciesData.h",
     "RE:" + r"UENUM\s*\(BlueprintType\)\s*\nenum class EGF_BreedingGroup : uint8\s*\{[^}]*\};",
     "// Breeding groups are FName tags rather than an enum: every project defines\n"
     "// its own, and adding one must not mean editing plugin C++. Two species can\n"
     "// breed when their tag sets overlap. See BreedingGroups below."),

    ("GF_CreatureSpeciesData.h",
     "RE:" + r"EGF_BreedingGroup BreedingGroup1 = EGF_BreedingGroup::\w+;",
     "TArray<FName> BreedingGroups;\n\n"
     "\t// Breeds with any species that can breed at all, ignoring group overlap.\n"
     "\t// Replaces the old magic universal-breeder group value, which was a\n"
     "\t// species name masquerading as a taxonomy entry.\n"
     "\tUPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"Breeding\")\n"
     "\tbool bUniversalBreeder = false;"),

    # Remove the whole second-group block, not just the field: a UPROPERTY with
    # no member declaration after it is a UHT error ("Expected name").
    #
    # NOTE the \s* between the lines rather than \n. The migrated source carries
    # CRLF from Gamma Emerald, so a pattern written with a bare \n silently fails
    # to match -- and it fails INVISIBLY when you test it, because reading the
    # file back in Python text mode normalises the line endings first.
    ("GF_CreatureSpeciesData.h",
     "RE:" + r"[ \t]*//[^\r\n]*[Ss]econd [^\r\n]*group[^\r\n]*\s*"
             r"UPROPERTY\([^)]*\)\s*"
             r"EGF_BreedingGroup BreedingGroup2 = EGF_BreedingGroup::\w+;",
     "\t// (the second group folded into the BreedingGroups array above)"),

    ("GF_BreedingLibrary.cpp",
     "RE:" + r"bool UGF_BreedingLibrary::IsUniversalBreeder\(const UGF_CreatureSpeciesData\* Species\)\s*\{.*?\n\}",
     "bool UGF_BreedingLibrary::IsUniversalBreeder(const UGF_CreatureSpeciesData* Species)\n"
     "{\n"
     "\treturn Species && Species->bUniversalBreeder;\n"
     "}"),

    # Anchored on the CODE, never on the comment above it. Earlier passes rewrite
    # prose (Undiscovered -> untagged), so a pattern that quotes a comment breaks
    # the moment a rename table grows -- which is exactly how this one broke.
    # Same reason the enum values are matched as \w+ rather than by name.
    ("GF_BreedingLibrary.cpp",
     "RE:" + r"return Species->BreedingGroup1 == EGF_BreedingGroup::\w+\s*"
             r"\|\|\s*Species->BreedingGroup1 == EGF_BreedingGroup::\w+;",
     "return Species->BreedingGroups.Num() == 0;"),

    ("GF_BreedingLibrary.cpp",
     "RE:" + r"\tconst EGF_BreedingGroup AGroups\[2\].*?\n\treturn false;\n\}",
     "\t// Universal breeders pair with anything that can breed at all.\n"
     "\tif (A->bUniversalBreeder || B->bUniversalBreeder)\n"
     "\t{\n"
     "\t\treturn !IsUnbreedable(A) && !IsUnbreedable(B);\n"
     "\t}\n"
     "\n"
     "\tfor (const FName& AGroup : A->BreedingGroups)\n"
     "\t{\n"
     "\t\tif (AGroup.IsNone())\n"
     "\t\t{\n"
     "\t\t\tcontinue;\n"
     "\t\t}\n"
     "\t\tif (B->BreedingGroups.Contains(AGroup))\n"
     "\t\t{\n"
     "\t\t\treturn true;\n"
     "\t\t}\n"
     "\t}\n"
     "\n"
     "\treturn false;\n"
     "}"),

    # (the .h declaration is renamed by IDENTIFIER_FIXUPS already)
]

OPTIONAL_PATCHES = []
