# Gamma Framework — Dialogue Syntax Reference

All dialogue files use the `.gfdlg` extension and are plain text.
Import them via the Content Browser to create a `GF_DialogueAsset`.

---

## Table of Contents

1. [File structure](#1-file-structure)
2. [Header directives](#2-header-directives)
3. [Message lines](#3-message-lines)
4. [Commands — modifiers](#4-commands--modifiers-apply-to-the-preceding-message)
5. [Commands — events](#5-commands--events-create-a-new-event-node)
6. [Commands — flow control](#6-commands--flow-control)
7. [Choice nodes](#7-choice-nodes)
8. [Comments](#8-comments)
9. [Text substitution tokens](#9-text-substitution-tokens)
10. [Multi-dialogue files](#10-multi-dialogue-files)
11. [Widget types](#11-widget-types)
12. [Complete example](#12-complete-example)
13. [Blueprint API quick reference](#13-blueprint-api-quick-reference)

---

## 1. File structure

```
# Optional comment
@id   MyDialogueID
@widget Overworld

Speaker: Line of dialogue text.
Another line — inherits the last speaker.
: Nameless line with no speaker tag.
Just plain text — also treated as nameless.

[event commands]

> Choice text -> label_name

@label label_name
...more lines...
```

Lines are processed top to bottom.
Blank lines and `#` comment lines are skipped.

---

## 2. Header directives

These must appear **before any dialogue lines**.

| Directive | Example | Description |
|-----------|---------|-------------|
| `@id` | `@id Rhea_PreBattle` | Sets the `DialogueID` (FName) used by subsystem delegates. |
| `@widget` | `@widget Overworld` | Picks the widget style. See [Widget types](#11-widget-types). |
| `@label` | `@label pc_branch` | Defines a jump target. Can appear anywhere in the file. |

---

## 3. Message lines

```gfdlg
Rhea: Hello, tamer!          # Speaker: text
Another line.                     # Inherits "Rhea" as speaker
: No speaker shown here.          # Leading colon = no speaker
Just plain text.                  # Also no speaker
```

- `Speaker: text` — speaker name shown in the name box; sets the **persistent speaker** for subsequent plain lines.
- `: text` — nameless message; clears the persistent speaker just for this line.
- Plain text — uses whatever speaker was set by the last `Speaker: …` line (or none).

---

## 4. Commands — modifiers (apply to the preceding message)

These lines **do not create a new node**; they modify the most recently created message node.
Place them on the line immediately after the message they affect.

### `[loud]`
Switches the dialogue box to its "loud" visual variant (e.g. shaking border, bold colours).

```gfdlg
Rhea: You dare challenge me?!
[loud]
```

### `[hold N]`
Locks player input for **N seconds** after the line appears. The player cannot advance until the lock expires.

```gfdlg
Narrator: A wild CREATURE appeared!
[hold 1.5]
```

### `[pause N]`
Auto-advances to the **next node** after **N seconds** without any player input. Does **not** close the dialogue box.

```gfdlg
Narrator: ...
[pause 2.0]
```

### `[autoend N]`
Closes the dialogue automatically after **N seconds**. Combine with `[hold N]` — the hold fires first.

```gfdlg
Sign: SLATEHAVEN CITY — A Stone Grey City.
[autoend 3.0]
```

### `[sound /Game/Path/To/SoundAsset]`
Plays a sound the instant this message node is displayed (fanfare, jingle, etc.).

```gfdlg
Prof. Hale: You received BUDLING!
[sound /Game/Audio/SFX/ItemReceived]
```

### `[duck /Game/Path/To/SoundMix]`
Pushes a **Sound Mix** the instant this message displays — use it to duck/muffle the OST under a
fanfare or jingle. The mix is a `USoundMix` asset; configure its `SoundClassAdjusters` (volume +
optional low-pass) and `FadeInTime` / `FadeOutTime` on the asset itself.
Stack it after `[sound …]` on the same line.

```gfdlg
: You obtained an <B>HM01</>!
[sound /Game/SOUNDS/SFX/SND_TomeGET]
[duck /Game/SOUNDS/MIX/SMM_MuffleOST]
[hold 3.0]
```

### `[unduck]`
Pops **every** Sound Mix this conversation pushed, restoring the OST. As a safety net, all pushed
mixes are also popped automatically when the dialogue ends (or is reset), so a skipped/aborted
conversation never leaves the music muffled.

```gfdlg
CUTTER: That HIDDEN MACHINE, or HM for short, is <G>CUT</>.
[unduck]
```

---

## 5. Commands — events (create a new event node)

These lines **create a new Event node** in the sequence.

### `[item ITEM_ID]` / `[item ITEM_ID count=N]`
Gives the player an item. Fires the `GiveItem` event.

```gfdlg
: You found a POTION!
[item POTION]

: You received 3 GREAT BALLS!
[item GREAT_BALL count=3]
```

### `[creature Species level=N]` / `[creature Species level=N full->label]`
Gives the player a creatures. If party is full the creatures goes to the PC and jumps to `label`.

```gfdlg
Prof. Hale: Here, take this BUDLING!
[creature Budling level=5 full->pc_branch]
...
@label pc_branch
Prof. Hale: Your party is full — sent to the PC!
```

- `level=N` — the gift creatures's level (default `5`).
- `full->label` — which label to jump to when the party is full and the creatures was sent to a box. Omit for no branching.
- When the creatures goes to a box, the destination box's name is available as `{Vault}` in subsequent message nodes:

```gfdlg
[creature Lunarite level=25 full->box_branch]
: {Creature} was added to your party!
[end]

@label box_branch
: {Creature} was sent to BOX "{Vault}"!
[end]
```

### `[flag FLAG_NAME]` / `[flag FLAG_NAME false]`
Sets (or clears) a quest flag.

```gfdlg
[flag GOT_STARTER]           # Sets flag to true
[flag GOT_STARTER false]     # Sets flag to false
```

### `[shop SHOP_ID]`
Opens a shop by ID. Fires the `OpenShop` event.

```gfdlg
Shopkeeper: What would you like?
[shop SLATEHAVEN_MART]
```

### `[battle]`
Triggers a tamer battle. Fires the `StartBattle` event — wire up your battle logic on `OnDialogueEvent`.

```gfdlg
Brock: Prepare yourself!
[battle]
```

### `[hide]` / `[hide N]`
**Hides the dialogue box.** Useful for cutscene camera moves between lines.

- `[hide]` — waits indefinitely. Call `ResumeDialogue()` from Blueprint when ready.
- `[hide 1.5]` — auto-resumes after **1.5 seconds**.

```gfdlg
Narrator: The ancient doors opened slowly...
[hide 2.0]
Narrator: Inside, a powerful light pulsed.
```

### `[flow EventName]` / `[flow EventName delay=N]`
Calls a Blueprint **Custom Event** named `EventName` directly **on the caller actor**
(the actor passed into `StartDialogue`). No delegate binding needed — exactly one
actor receives it, exactly once. **Prefer this over `[CustomName]` for NPC-specific
logic** (shop purchases, conditional branches): multicast `OnDialogueEvent` bindings
run on every bound actor, which multi-executes your logic when several NPCs share a class.

```gfdlg
[flow BuyMon_Lunarite delay=-1]    # calls the "BuyMon_Lunarite" custom event on the NPC,
                                   # then waits — jump with JumpToLabel() from that event
```

- `delay=N` — same semantics as custom events: `0` = continue immediately, `>0` = wait N seconds, `-1` = wait until `ResumeDialogue()`/`JumpToLabel()`.

### `[SomeCustomName]` / `[SomeCustomName delay=N]`
Fires a **custom event** named `SomeCustomName` via `OnDialogueEvent`.
Bind to this in Blueprint to do anything the built-in events don't cover.
**Note:** this is a multicast broadcast — every bound Blueprint receives it.
For logic that must run once on one NPC, use `[flow …]` instead.

```gfdlg
[ShowCutscene]

[PlayFanfare delay=2.0]    # Dialogue waits 2 seconds before continuing
```

- `delay=N` — how many seconds to pause after the event fires before auto-advancing. Use `-1` to wait forever (call `ResumeDialogue()` manually).

---

## 6. Commands — flow control

### `[end]`
Ends the dialogue at this point. The previous node's `NextNodeIndex` is set to `-1`.

```gfdlg
Rhea: Come back when you're ready.
[end]
```

### `[jump label_name]`
Unconditionally jumps to `@label label_name`.

```gfdlg
[jump main_loop]
```

### `@label name`
Defines a jump target. Can appear anywhere in the file — before or after the nodes that reference it.

```gfdlg
@label after_battle
Rhea: Well done, tamer.
```

---

## 7. Choice nodes

Start choice options with `>`. Consecutive `>` lines are grouped into one choice node automatically.

```gfdlg
NPC: Would you like a tour?
> Yes, please -> yes_branch
> No thanks   -> [end]

@label yes_branch
NPC: Great! Follow me!
```

- `> Text -> label` — jumps to `@label label` when selected.
- `> Text -> [end]` — ends dialogue when selected.

Each choice can also fire an event by setting `OnSelected` in the Data Asset directly, but this is not yet supported via `.gfdlg` syntax — configure in the editor instead.

---

## 8. Comments

Lines starting with `#` are ignored entirely.

```gfdlg
# This is a developer comment
Rhea: Hello tamer!   # Inline comments are NOT supported — entire line must start with #
```

---

## 9. Text substitution tokens

These tokens are replaced at **runtime** in speaker names, dialogue text, and choice labels.

| Token | Replaced with | Set by |
|-------|---------------|--------|
| `{PlayerName}` | The player's save-file name | Automatic (from save) |
| `{Creature}` | Name of the last creatures given via `[creature …]` | Automatic (from last GiveCreature event) |
| `{Vault}` | Name of the box the last `[creature …]` gift was sent to (party was full) | Automatic (from last GiveCreature event) |
| `{YourKey}` | Any custom string | `SetDialogueVariable("YourKey", "value")` in Blueprint before `StartDialogue` |

**Examples:**

```gfdlg
Prof. Hale: Well done, {PlayerName}!

Prof. Hale: {Creature} was sent to the PC!

Nurse Joy: That'll be {Cost} PokéDollars.
```

For `{Cost}`, call before starting dialogue:
```blueprint
DialogueSubsystem → SetDialogueVariable("Cost", "300")
DialogueSubsystem → StartDialogue(Asset, Caller)
```

Variables are **cleared automatically** when dialogue ends.

---

## 10. Multi-dialogue files

A single `.gfdlg` file can contain multiple separate conversations using the `@dialogue` directive.
Each `@dialogue` block is imported as its own `GF_DialogueAsset`.

```gfdlg
@dialogue Rhea_PreBattle
@widget Overworld
Rhea: I'm the Rock-type Trial Leader!
[battle]

@dialogue Rhea_Defeat
Rhea: ...I lost? Incredible!
[end]

@dialogue Rhea_FinalMessage
Rhea: Rock-types are your friends too, you know.
[end]
```

> **Tip:** Name your file `DA_Dialogue_Rhea.gfdlg` and keep all of one NPC's conversations in it.

---

## 11. Widget types

Set via `@widget` in the file header, or `WidgetType` on the `GF_DialogueAsset`.

| Value | Visual | Use case |
|-------|--------|----------|
| `Overworld` | White box, grey border | NPC dialogue, signs, items (default) |
| `Cinematic` | Black gradient, full-width | Intro sequences, cutscenes |
| `Battle` | Battle text box | In-battle messages |

---

## 12. Complete example

```gfdlg
@id   Rhea_PreBattle
@widget Overworld

# ── Intro ────────────────────────────────────────
Rhea: Oh? {PlayerName}! I've been waiting for you.
I'm Rhea, the Slatehaven City Trial Leader.
[hold 0.5]

Rhea: I use Rock-type creatures...
Using items and creatures I gathered from all over...
I became a Trial Leader!

# ── Dramatic pause ──────────────────────────────
[hide 1.0]

# ── Challenge ────────────────────────────────────
Rhea: I'll show you what I mean!
[loud]

# ── Start fight ──────────────────────────────────
[battle]
```

```gfdlg
@id Rhea_Defeat
@widget Overworld

Rhea: ...I lost? I see.
I need to study harder.

# ── Reward ───────────────────────────────────────
: You received the BOULDER BADGE!
[item BOULDER_BADGE]
[sound /Game/Audio/SFX/EmblemGet]
[hold 2.0]

# ── Flag ─────────────────────────────────────────
[flag BEAT_RHEA]

# ── Closing ──────────────────────────────────────
Rhea: TM39 Rock Tomb is also my gift to you.
: Received TM39!
[item TM39]

Rhea: Come visit again someday, {PlayerName}.
[end]
```

---

## 13. Blueprint API quick reference

### Starting / controlling dialogue

| Function | Description |
|----------|-------------|
| `StartDialogue(Asset, Caller)` | Begin a conversation. `Caller` is the NPC/object — used for `ContinueFlow` events. |
| `AdvanceDialogue()` | Skill to the next node (call on player confirm, after typewriter finishes). |
| `SelectChoice(Index)` | Pick a choice by index (call from choice button). |
| `NotifyChoiceHovered(Index)` | Report the highlighted choice (call from the button's hover / UINav navigate event). Fires `OnDialogueChoiceHovered` on change only. |
| `ClearChoiceHover()` | Broadcast `OnDialogueChoiceHovered` with index `-1` so listeners drop their preview. |
| `EndDialogue()` | Force-close dialogue immediately. |
| `BeginCloseDialogue()` | Signal widget to play its closing animation; call `EndDialogue()` when done. |
| `HideDialogue()` | Hide the box manually (also triggered by `[hide]` nodes). |
| `ResumeDialogue()` | Resume after `[hide]` or a `delay=-1` custom event. |

### Delegates (bind on your NPC/GameMode Blueprint)

| Delegate | Fires when | Parameters |
|----------|-----------|------------|
| `OnDialogueStarted` | Dialogue begins | `DialogueID` |
| `OnDialogueFinished` | Dialogue ends naturally | `DialogueID` |
| `OnDialogueClosing` | Vault is about to close | — |
| `OnDialogueNodeReached` | Any node is displayed | `DialogueID`, `NodeIndex` |
| `OnDialogueChoicesReady` | A choice node is shown | `Choices` array |
| `OnDialogueChoiceHovered` | The highlighted choice changes (or clears) | `ChoiceIndex` (-1 = none), `Choice` struct |
| `OnDialogueEvent` | An event node fires | `DialogueID`, `Event` struct |
| `OnInputUnlocked` | `[hold N]` timer expires | — |

### Subsystem state queries

| Function | Returns |
|----------|---------|
| `IsDialogueActive()` | `bool` — is a conversation running? |
| `IsInputLocked()` | `bool` — is a `[hold N]` timer active? |
| `IsWaitingForChoice()` | `bool` — is a choice node waiting for input? |
| `GetCurrentDialogueID()` | `FName` |
| `GetCurrentNodeIndex()` | `int32` |
| `GetCurrentCaller()` | `AActor*` — the NPC passed into `StartDialogue` |
| `GetCurrentNode(OutNode)` | Fills `OutNode` with the active `FGF_DialogueNode` |

### Dynamic text

| Function | Description |
|----------|-------------|
| `SetDialogueVariable("Key", "Value")` | Register a `{Key}` substitution before `StartDialogue`. |
| `ClearDialogueVariables()` | Clears all variables (auto-called on dialogue end). |
| `SetNodeText(NodeIndex, NewText)` | Override a specific node's text at runtime. Call before `ResumeDialogue()`. |
| `SetPendingItemIcon(Texture)` | Queue an item icon to display on the first node. Call right after `StartDialogue`. |

### Widget events (implement in your Widget Blueprint)

| Event | When it fires |
|-------|--------------|
| `OnNodeReached(DialogueID, NodeIndex)` | Each new message node |
| `OnNodeStyle(bLoud)` | Each new message — `bLoud` true when `[loud]` was used |
| `OnChoicesReady(Choices)` | Choice node arrived — spawn/show your buttons |
| `OnTypingStarted()` | Typewriter begins — hide your advance arrow |
| `OnTypingComplete()` | All characters revealed — show your advance arrow |
| `OnDialogueEnded(DialogueID)` | Dialogue finished — hide the widget |
| `OnDialogueClosing()` | Vault closing — play your outro animation, then call `EndDialogue` |
| `OnShowItemAnimation(Icon)` | Item icon ready — play slide-in animation |
