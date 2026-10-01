# Editor tools

Everything the plugin adds to the editor lives under one top-level **Gamma**
pulldown in the Level Editor menu bar.

```
Gamma
├── Creatures
│   └── Create Creature…
├── Dialogue
│   ├── Dialogue Editor
│   ├── Set Source File…
│   ├── Reimport All Dialogue
│   └── Create Missing Assets
└── Blueprints
    └── Export to JSON
```

> The menu used to be labelled **GE** / "Gamma Framework game tools" — a leftover
> from the project this was extracted from. Renamed to match `NAMING.md`. The
> menu id moved with it (`LevelEditor.MainMenu.GammaFramework`), so
> `ShutdownModule` removes the same name it registered.

---

## Create Creature

Two fields and a button. Making a species asset by hand is four steps in the
content browser — right-click, find the class in a long list, rename, then move
it to the right folder — repeated once per creature, sixty times.

| | |
|---|---|
| **Creature Data folder** | Where the element folders live. Defaults to `/Game/CreatureData`, and is remembered between sessions |
| **Type** | Every `EGF_Element` except `None` |
| **Creature name** | Stored on the asset as its `SpeciesName` |

The button stays disabled until both a type and a usable name are in, and the
window shows the exact path it is about to write before you commit to it.

```
/Game/CreatureData/Ember/DA_Ashling
 └── root ──────┘ └ type ┘ └ asset ┘
```

The element folder is created if it does not exist.

### Why filed by element

It is the one property that never changes over a creature's life, so the folder
never has to move. Filing by evolution stage or by region means moving assets
when the design shifts — and moving a `.uasset` leaves a redirector behind.

### Details that matter

**The display name and the file name part ways.** "Ember Drake" is a perfectly
good species name and a bad package name. The typed name goes onto the asset
verbatim as `SpeciesName`; only the file name is stripped to letters and digits,
giving `DA_EmberDrake`. Leading digits are dropped too, because a package name
cannot start with one.

**It will not overwrite.** Existence is checked on disk with
`FPackageName::DoesPackageExist`, not through the asset registry — an asset made
outside this session, or one the registry has not scanned yet, still counts.
Overwriting a species someone has already filled in is the one genuinely
destructive thing this tool could do, so it refuses and says which name clashed.

**It saves immediately** rather than leaving the asset dirty. The folder does not
exist on disk until something is written into it, and an unsaved asset in a
folder that is not there yet is the shape of bug that loses work on a crash.

**The name field clears after a create; the type and folder do not.** Making a
run of creatures of the same element is the normal way this gets used, and
re-picking the type each time would be the slow part all over again. Focus
returns to the name box, so the next creature is just typing and Enter.

"Open the asset after creating" is **off** by default for the same reason — an
asset editor opening on every create is exactly what you do not want when
entering twelve of them.

---

## Where the code is

| | |
|---|---|
| `GammaFrameworkEditor/Public/SGF_CreateCreatureWidget.h` | The window |
| `GammaFrameworkEditor/Private/SGF_CreateCreatureWidget.cpp` | Validation, naming, asset creation |
| `GammaFrameworkEditor/GammaFrameworkEditor.cpp` | Menu registration, window lifetime |

`UGF_CreatureEditorUtilities::CreateCreatureSpeciesAsset` in the Creatures module
predates this and is still there for Blueprint callers. It does not check for an
existing asset, does not set an element and does not create folders — prefer the
window.

## Worth adding later

- **Auto-assign `CompendiumNumber`** to the next free value by scanning the
  existing species assets. Currently it is left at its default and filled in by
  hand.
- **A secondary element field.** Deliberately left out: a dual-typed creature
  still belongs in exactly one folder, and asking for the second type up front
  invites filing decisions the folder layout does not support.

---

## Export blueprints to JSON

`.uasset` is a binary package. The only way to answer "what does this widget
actually do" is to open it in the editor and look, which is fine for a person
and useless for everything else: a diff that says `Bin 4823 bytes changed`, a
review that cannot be done outside the editor, a script that wants to check
every button has a hover sound, an assistant asked to change a graph it has no
way to read.

This writes the asset out as text. It reads; it never writes to a package.

### What comes out

One `.json` per blueprint, mirroring the content folder structure under
`<Project>/Saved/BlueprintJSON/`:

```
Saved/BlueprintJSON/Game/Godsmarch/Blueprints/Widgets/Battle/SkillMenu_G_WBP.json
```

| Key | |
|---|---|
| `parentClass`, `interfaces` | What it derives from and implements |
| `variables` | Name, type, default, category, metadata |
| `widgetTree` | The UMG hierarchy: each widget, its class, its slot |
| `animations` | Name, time range, and which widgets each one drives |
| `bindings` | Property bindings — widget, property, and the function behind it |
| `components` | Simple Construction Script tree, for actor blueprints |
| `graphs` | Event graph, functions, macros, interface functions, collapsed graphs |

A graph is a flat list of nodes. Every node carries a GUID, its class, its
title, its position, and its pins; every pin carries its type, its value, and
the node GUID and pin name at the far end of each wire. That is enough to walk
the graph without a picture of it.

### Two things keep the file readable

**Properties are diffed against the archetype.** A `UButton` has several hundred
inherited properties and a designer touched four of them. Only those four land
in the file. The same pass covers nodes, so a call node's function reference and
a cast node's target type come out without the exporter knowing those node
classes exist.

**Object references come out as paths, not as nested dumps.** Otherwise the
export follows the first hard reference it finds and never comes back — a widget
references its font, which references its typeface, which references a font face.

Hidden pins that are neither wired nor overridden are dropped too. They say
nothing, and there are a great many of them.

### From the editor

**Gamma → Blueprints → Export to JSON** exports whatever blueprints are selected
in the content browser. Selecting nothing exports every widget blueprint in the
project. The toast that follows links to the output folder.

### From the command line

For a batch run, a git hook, or a build step — no editor window needed:

```
"<Engine>/Binaries/Win64/UnrealEditor-Cmd.exe" "<Project>.uproject" ^
    -run=GF_ExportBlueprints -Path=/Game/Godsmarch/Blueprints/Widgets -WidgetsOnly
```

| Switch | |
|---|---|
| `-Path=/Game/UI,/Game/Menus` | Only these folders, searched recursively. Default: everything |
| `-Asset=/Game/UI/MyMenu` | Specific assets, by package name |
| `-Out=<dir\|file>` | Where to write. Default `Saved/BlueprintJSON` |
| `-WidgetsOnly` | Widget blueprints only |
| `-Combine` | One JSON array in one file, rather than a file each |
| `-Compact` | No indentation. Smaller file, same content |
| `-NoGraphs`, `-NoProperties`, `-NoWidgetTree` | Leave a section out |
| `-HiddenPins` | Keep the hidden pins after all |

It exits non-zero when nothing matched or a file could not be written, so a
caller can tell an empty export from a failed one.

### Where the code is

| | |
|---|---|
| `GammaFrameworkEditor/Public/GF_BlueprintJsonExporter.h` | Options and the API |
| `GammaFrameworkEditor/Private/GF_BlueprintJsonExporter.cpp` | The whole export |
| `GammaFrameworkEditor/Public/GF_ExportBlueprintsCommandlet.h` | Switch reference |
| `GammaFrameworkEditor/Private/GF_ExportBlueprintsCommandlet.cpp` | Argument parsing |
| `GammaFrameworkEditor/GammaFrameworkEditor.cpp` | The menu entry |

### Worth knowing

- The export reflects the **saved** asset when run as a commandlet, and the
  **in-memory** asset when run from the menu. Unsaved edits show up in one and
  not the other.
- There is no importer. This is a one-way door, deliberately: round-tripping a
  blueprint through JSON is a much larger problem than reading one.
