# Options Screen — wiring guide

Everything the player can change lives in **`UGF_SettingsSubsystem`** (a Game
Instance subsystem). Get it in Blueprint with `Get Game Instance Subsystem ->
GE Settings Subsystem`.

## Where the values live

| Layer | What it is |
| --- | --- |
| `GEOptions` save slot | **Source of truth.** Separate from `CreatureSaveSlot`, so options survive New Game / Delete Save and work on the title screen before a save is loaded. |
| `UGF_SettingsSubsystem` | Runtime owner. Applies effects, broadcasts `OnSettingsChanged`, debounces the write to disk. |
| `UGF_VaultSystem` fields | **Read-only mirror.** Written by the subsystem after every change and after a save load. Existing Blueprints that read `VaultSystem->TextSpeed` etc. keep working. Do not write to them — the next change overwrites it. |

`VaultSystem->bEXPShareEnabled` mirrors the **effective** value, i.e. after the
difficulty profile has had its say. `VaultSystem->Difficulty` is a new field.

## Project Settings

**Project Settings → Game → Gamma Framework Settings** is where the options are
defined:

- **Dialogue Frames** — one entry per box skin the player can pick. Each has a
  display name, a texture, nine-slice margins and a tint. The saved `FrameID` is
  an **index into this array**, so only ever append once a build has shipped.
  A frame skins the **box art only** — dialogue text colour and font stay with
  the RichTextBlock's style DataTable and are never player-editable.
- **Text Speed** — chars/second for Slow / Normal / Fast. Instant bypasses the
  typewriter entirely.
- **Audio** — the sound mix and sound classes the volume sliders drive.
  Defaults point at `/Game/SOUNDS/Mix_GF_Master` + `SC_MASTER` / `SC_MUSIC` /
  `SC_SFX` / `SC_VOICE`. **Volume sliders do nothing until a sound mix is set.**
- **Video** — the resolution list offered in the menu, plus the aspect ratio
  guard. `bEnforceAspectRatio` is on by default at 16:9, and **every** path that
  sets a resolution goes through it: a non-conforming entry left in
  `SelectableResolutions` is logged and hidden from the menu, and a saved value
  that fails the check is snapped back on load. See "Aspect ratio" below for
  what this does and does not guarantee.
- **Difficulty** — the Easy / Normal / Hard profiles (see below).

## Building the menu

Each row is a getter to initialise the widget plus a setter on change. Setters
apply immediately and save themselves — there is no Apply button to wire.

The screen is built on `UINavOptionBox`, which works in `OptionIndex` ints, so
every option row has an **index-based** pair that plugs straight into
`SetOptionIndex` (on open) and `OnValueChanged` (on change). Use those rather
than the enum setters — no int/byte/enum conversion needed.

| Row | On open -> `SetOptionIndex` | `OnValueChanged` -> |
| --- | --- | --- |
| Text Speed | `GetTextSpeedIndex` | `SetTextSpeedByIndex` |
| Difficulty | `GetDifficultyIndex` | `SetDifficultyByIndex` |
| Window Mode | `GetWindowModeIndex` | `SetWindowModeByIndex` |
| Resolution | `GetResolutionIndex` | `SetResolutionByIndex` |
| EXP Share (Yes/No) | `GetEXPShareIndex` | `SetEXPShareByIndex` |
| Depth of Field (Yes/No) | `GetDOFIndex` | `SetDOFByIndex` |
| Auto Repel (Yes/No) | `GetAutoRepelIndex` | `SetAutoRepelByIndex` |
| Dialogue Frame | `GetFrameID` | `SetFrameID` |

Yes/No rows use **index 0 = Yes, 1 = No**. Every option index matches the order
of the matching `GetXLabels()` array; out-of-range values clamp.

Sliders and everything else:

| Row | Get | Set |
| --- | --- | --- |
| Master / Music / SFX | `GetMasterVolume`, `GetMusicVolume`, `GetSoundEffectsVolume` | `SetMasterVolume`, ... |
| Dialogue frame preview | `GetDialogueFrameCount`, `GetDialogueFrameBrush(Index)` | `CycleFrameID(+1/-1)` |
| Labels for an option box | `GetTextSpeedLabels`, `GetDifficultyLabels`, `GetWindowModeLabels`, `GetSelectableResolutions` | — |
| Enum form, if you prefer it | `GetTextSpeed`, `GetDifficulty`, `GetWindowMode` | `SetTextSpeed`, `SetDifficulty`, `SetWindowMode` |
| Auto Repel, bool form | `IsAutoRepelEnabled` | `SetAutoRepelEnabled` |

### You MUST guard the populate pass

`UUINavOptionBox::NativePreConstruct` calls `BaseConstruct()`, which calls
`Update()` with `bNotify = true`. `LastOptionIndex` starts at `-1`, so that first
update always counts as a change and **broadcasts `OnValueChanged` with the box's
designer-authored `OptionIndex`**. That happens while the widget tree is being
built — before the options screen's own Construct.

Left unguarded, every box writes its designer default into the subsystem the
moment the screen is created, clobbering the player's real settings and applying
them, and then your populate pass reads back the values that were just destroyed.
Symptom: every row shows defaults, and changes made last time are gone.

Wrap it:

1. `Settings → BeginLoadingUI` **before** `Create Widget` for the options screen.
   Arming it from inside the options widget is too late — the boxes have already
   fired.
2. Create and add the widget, run your populate/LoadSettings pass.
3. `Settings → EndLoadingUI`.

While armed, every setter is a no-op. Begin/End are counted, so nesting is safe.
The guard also auto-clears on level change so a widget torn down mid-populate
cannot leave settings permanently unwritable; `ClearLoadingUIGuard` forces it off
if you ever need to.

Note that `SetOptionIndex` itself calls `Update(false)` and does **not** notify,
so populating a row is not what fires the bogus event — construction is.

Notes:

- **Grey out the EXP Share row when `IsEXPShareToggleAllowed` is false** — Easy
  forces it on and Hard forces it off, so `SetEXPShareEnabled` is a no-op there.
  Re-read the row whenever difficulty changes.
- For a live frame preview, feed `GetDialogueFrameBrush(Index)` into a UImage's
  brush as the player scrolls.
- `SaveSettings` and `ResetToDefaults` are there if you want explicit buttons.
- Video changes are **skipped in PIE** on purpose — resolution and fullscreen
  only apply in a real game session. Test them in Standalone.

## Dialogue box

`UGF_DialogueWidgetBase` consumes both dialogue-facing options automatically:

- **Text speed** — `TypewriterSpeed` is pulled from the settings on construct
  and refreshed live if the player changes it mid-line. Instant reveals the
  whole line at once. Set `bUseSettingsTextSpeed = false` on a widget that must
  type at a fixed pace (cutscenes).
- **Frame** — name a `UImage` **`FrameImage`** in your UMG hierarchy and the
  chosen frame's brush is applied to it. For anything else the frame owns (the
  advance-arrow art, say), override the **`OnDialogueFrameChanged(Frame)`**
  event. Set `bUseSettingsDialogueFrame = false` for boxes with a fixed look
  (chat bubbles). Text styling is not part of this — see above.

## Defaults are load-bearing

`GEOptions.sav` is delta-encoded: UE only writes properties that differ from the
class defaults, and anything omitted loads back as whatever the default currently
is. Two consequences:

- A near-empty save file is normal, not a bug. It means the player has changed
  almost nothing.
- **Changing a default in `UGF_SettingsSave` retroactively changes every existing
  save** for players who never touched that option. That is usually what you
  want (it is how DOF was switched back on without asking anyone to delete their
  options), but it means a default is a live decision, not just a starting value.

Defaults that describe the game's intended presentation — DOF is on, text speed
is Fast — must match how the game is meant to look and feel out of the box.
The settings system applies them on startup whether or not the player has ever
opened the menu, so a wrong default actively overrides your art direction.

## Aspect ratio

`bEnforceAspectRatio` guarantees that **the resolution setting can only ever hold
a 16:9 value**. It does not guarantee the game renders 16:9. Three things sit
outside its reach:

- Windowed mode lets the player drag the window to any shape.
- Fullscreen on a 16:10 or ultrawide display gets the monitor's shape.
- Anything that drives `UGameUserSettings` directly, bypassing this subsystem.

For a hard guarantee, set **Constrain Aspect Ratio** on the gameplay camera with
Aspect Ratio `1.777778`. That letterboxes/pillarboxes the render regardless of
window shape. This matters for more than framing here: the grid camera is fixed,
so a wider viewport reveals more of the map than the level was built to show.

## Difficulty

Each profile is tunable in Project Settings without a recompile:

| | Easy | Normal | Hard |
| --- | --- | --- | --- |
| EXP multiplier | 1.5x | 1.0x | 1.0x |
| Tamer AI shift | -1 tier | none | +1 tier |
| Level cap | off | off | on, by emblem count |
| EXP Share | forced **on**, 50% | player's choice, 25% | forced **off** |

- **AI shift** moves every tamer's authored `AIDifficulty` by that many steps
  (clamped to Random..Expert). A Trial Leader set to Expert stays Expert on Hard;
  a route tamer set to Smart gets promoted. Read
  `ATamerMaster::GetEffectiveAIDifficulty()` rather than `AIDifficulty` in any
  new AI code.
- **Level cap** blocks EXP entirely once a Creature reaches the cap for the
  player's current emblem count. Defaults track the trial leader aces
  (15/19/24/29/31/33/42/46/58). Use `GetLevelCap` / `IsLevelCapped` to show it in
  the party or tamer card UI.

  Two enforcement points, because the sanctuary does not route through `GiveEXP`:
  - `UCreatureManagerSubsystem::GiveEXP` — covers battle EXP and Rare Candy
    (which spends exactly one level's EXP through `GiveEXP`, so a capped Creature
    refuses the candy and the item is not consumed).
  - `UGF_SanctuarySubsystem::ApplySanctuaryGrowth` / `CalculateLevelAfterBankedEXP`
    — the growth and the "will come out at level N" preview share one ceiling.

  The cap **only limits the player's party**. Wild and tamer Creature levels are
  authored data and are untouched.
