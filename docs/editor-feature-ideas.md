# Editor Feature Ideas — DRAFT Backlog

> **Status: DRAFT — for discussion.** This is a prioritized backlog of missing or
> desirable features for the map editor (`src/editor/`). It goes beyond the §5 roadmap in
> `scene-builder.md` (which captures the immediately-next items already in view). Each
> entry notes effort (S/M/L), ties the feature to the editing workflow or the Krunker
> north-star, and flags any engine/game-seam constraint.
>
> **Seam rule reminder:** The editor links `libengine.a` only — it must never `#include` a
> `src/game/` header. Features that need game-specific knowledge go through a
> content-registry hook (a registration function the game calls into the editor, never the
> other way around). This is called out where relevant.
>
> **SHIPPED so far** (a parallel feature wave): **1.1 Editable Inspector**,
> **1.2 Rotate/Scale Drag** (PROP), **2.1 Focus on Selection (`F`)**,
> **3.3 Map Metadata Panel** (shown in the Inspector when nothing is selected),
> **4.2 Hierarchy Search/Filter**, **4.5 Unsaved-Changes Guard**, and
> **4.6 Inline Validation Indicators** (red proxy outlines, live), and
> **3.1 Place All Entity Kinds** (the PLACE palette now covers every `EngMapEntKind`,
> grouped Spawns / Geometry / Buyables; walls are two-click, the rest drop-and-snap).
> On the engine side the **2.4** textured-render foundation (`Eng_DrawTexturedBoxV` /
> `…FloorV`) also landed — see [scene-builder.md](scene-builder.md) §5.7. Still open from
> the Top 5: **5.1 Play-Test Launch**. (Beyond the editor backlog, the games-as-projects
> **New/Open Game** UI also shipped — see [game-projects.md](game-projects.md) §8 / §10.)

---

## 1. Editing Power

### 1.1 Editable Inspector (position, yaw, scale, mob tag, facing)
Turn the read-only right panel into live input fields wired through `mapedit.h`. Position
fields (x, z) accept typed numbers and call `Eng_SetPos`; yaw/scale for props call
`Eng_SetYaw`/`Eng_SetScale`; spawn mob tag calls `Eng_SetSpawnMob`; window facing calls
`Eng_SetWindowDir`. Each confirmed edit calls `EdScene_Commit`.

**Why it matters:** Currently the only way to set a precise position is to drag — fine for
rough layout, broken for level design that needs walls on exact grid positions or entities
at known coordinates. Keyboard-entered values are the most reliable authoring path for
anything structural.

**Effort:** S — the mapedit.h mutators already exist and are fully typed. The raygui
`GuiTextBox` widget is already available in the editor; it is purely a panel wiring
exercise.

**Seam:** No constraint — all mutations go through engine-side `mapedit.h`.

---

### 1.2 Rotate / Scale Drag (gizmo modes 2 and 3)
Wire the existing `ENG_GIZMO_ROTATE` and `ENG_GIZMO_SCALE` drag modes in `edscene.c`
to `Eng_SetYaw`/`Eng_SetScale` (PROP only for now; obstacle resize via size setters
later). The gizmo math (`Eng_GizmoUpdateDrag` already returns `rotateRadians` and
`scale`) is done — this is purely the scene-side wiring that applies the delta each
frame, the same pattern as translate drag.

**Why it matters:** Placing props without yaw control forces a round-trip through a
future editable inspector; rotate drag is faster for the "spin until it looks right"
workflow that mirrors how Krunker players author maps.

**Effort:** S — the gizmo already returns the values; `Eng_SetYaw`/`Eng_SetScale`
already exist in `mapedit.h`. Two pages of code in `edscene.c`.

**Seam:** No constraint.

---

### 1.3 Multi-Select (box-select + Shift-click)
Hold Shift and click to add/remove from a selection set; drag an empty area to rubber-band
select all entities whose proxy AABBs intersect the 2D screen rectangle. Store
`selectedId[]` array instead of a single `selectedId` int. Move/delete/cycle all operate
on the set; gizmo shows at the centroid and translates all selected entities by the same
delta.

**Why it matters:** Real map editing is never one entity at a time. Placing a cluster of
mob spawns, moving a row of obstacles, or deleting a whole wing of a map all require
multi-select. This is the single feature that most separates "toy editor" from "real
editor" for everyday use.

**Effort:** M — the pick and gizmo infrastructure is single-selection today; the proxy
array already covers all entity kinds. The main work is widening `EdScene` from one int
to an int array, updating the gizmo origin to a centroid, and making every verb
(translate, delete, cycle) iterate the selection. The Hierarchy panel needs Shift-click
support too.

**Seam:** No constraint — purely `EdScene`/`EdHost` territory.

---

### 1.4 Copy / Paste / Duplicate
`Ctrl+C` snapshots the selected entities' `MapDoc` data (positions, kinds, all fields)
into an editor clipboard; `Ctrl+V` re-adds them via `EngMapEnt_Add` at a fixed offset
from the original positions (e.g. +2 units X/Z) so they don't land exactly on top.
`Ctrl+D` (duplicate-in-place) is copy+paste in one action. After paste, the new entities
become the selection. All paste operations push one `EngMapHistory_Commit`.

**Why it matters:** Community map-making lives and dies by copy/paste. Hallways, repeated
rooms, symmetric layouts — all require it. This is table-stakes for any level editor.

**Effort:** M — the editor has no clipboard data structure yet. Building a small
`EdClipboard` struct (a POD array of entity descriptors, no MapDoc needed) plus the
paste wiring via `EngMapEnt_Add` + per-field `Eng_Set*` calls is straightforward.

**Seam:** No constraint — all `mapedit.h` mutators.

---

### 1.5 Alignment / Distribute
Select 3+ entities, then: "Align Left/Right/Top/Bottom" sets all to the min/max X or Z
of the selection; "Distribute Evenly" sets equal spacing between sorted positions. Both
go through `Eng_SetPos` and push one history commit.

**Why it matters:** Quick alignment is what makes grid-free placement viable — lay things
down roughly, then snap them into line in one action. The distribute command is the
polished version of "set snap step, nudge each entity" that saves 10 keystrokes per row.

**Effort:** S — pure application of existing mutators. Can ship as a `Tools` menu plugin.

**Seam:** No constraint.

---

### 1.6 Measure / Distance Tool
Show the world-space distance between two selected entities (or click-drag to measure
between two points in the viewport). Overlay the result via `debugdraw.h` and echo it to
the Console and the status bar.

**Why it matters:** Krunker-style FPS maps have strong layout intuitions about
distances — "how wide is a doorway", "can a player bhop across this gap". Without a
measure tool, authors are guessing at engine units.

**Effort:** S — `debugdraw.h` already draws lines; distance is a one-liner.

**Seam:** No constraint.

---

### 1.7 Undo History Visualizer
A panel (or Console output) that shows the undo stack as a numbered list of labels
("translate #7", "add SPAWN", "delete OBSTACLE #12"), with the current cursor highlighted
and Ctrl+click-to-jump-to-step. The step labels require storing a brief human-readable
description alongside each `EngMapHistory` commit — either adding a `char label[48]`
field to the snapshot ring, or by keeping a parallel label array in `EdScene`.

**Why it matters:** At depth 64 the undo ring is opaque — you can't know how many steps
back the "wrong turn" was. The visualizer restores the author's confidence to make
risky edits.

**Effort:** M — `EngMapHistory` stores snapshots but not labels; adding a label sidechannel
is a small engine change, then a panel plugin is straightforward.

**Seam:** The label field belongs in `mapedit.h`/`EngMapHistory` (engine side), but label
*content* is authored by the caller (the editor) — engine stores the slot, editor writes
the string. No game knowledge needed.

---

## 2. Viewport

### 2.1 Focus on Selection (`F` key)
Press `F` to move the orbit/fly camera focus to the selected entity's world position
(taking floor Y from the entity's sector). In fly mode, smoothly slides the focus toward
the target over ~0.3 s; in ortho modes, instantly re-centers the view and adjusts zoom
to frame the selection.

**Why it matters:** Every professional 3D editor has this. Without it, navigating to a
specific entity means manually flying across the map — slow and disorienting. This is
probably the single smallest feature with the highest editing-friction payoff.

**Effort:** S — `EdScene` already stores `focus` and `orthoH`; updating them is four lines
plus an optional lerp.

**Seam:** No constraint.

---

### 2.2 Ortho Front / Side Views (`F4` / `F5`)
Two additional camera modes: front (+Z looking toward -Z) and side (+X looking toward -X),
both orthographic. Follow the existing `EdViewMode` pattern; add two enum values, two F-key
bindings, two View menu entries.

**Why it matters:** Top-down shows where things are, but NOT how tall they are. Front and
side views are how an author authors multi-floor height relationships, obstacle heights,
and ramp profiles — necessary as soon as the map gains vertical complexity.

**Effort:** S — `edscene.c` already has the ortho camera math for isometric; front/side
are just locked-yaw variants. The existing `Eng_PickRayFromScreen` handles ortho cameras
correctly already.

**Seam:** No constraint.

---

### 2.3 Camera Bookmarks
`Ctrl+1..9` saves the current camera state (position, yaw, pitch, mode, zoom) into a
named slot; `1..9` (unmodified, when no placement tool is armed) recalls it. Persisted in
`editor.cfg` as `bookmark.0.x`, `bookmark.0.yaw`, etc.

**Why it matters:** Large maps require frequent context-switching between distant areas
(arena center, spawn corridor, objective room). Bookmarks collapse "30 seconds of flying
around" to "one keystroke" — exactly what Krunker's community map-making workflow uses.

**Effort:** S — camera state is a small struct; `EngCfg_PutFloat` handles persistence.

**Seam:** No constraint.

---

### 2.4 Game-Accurate Textured Rendering
Replace proxy boxes with actual sector floor/wall/obstacle geometry rendered with the
game's textures — optionally, so the author can toggle between "debug boxes" and "looks
like the game" modes. Requires a content-registry hook: the editor can't call game render
code directly, so the engine needs a `Eng_RegisterContentRenderer` callback that the
game sets at startup (when embedded) or that a plugin provides (standalone editor).

**Why it matters:** Placing entities relative to grey boxes is disorienting; placing them
relative to the actual rendered floor/walls matches the player's experience. For community
maps this is the difference between "functional" and "polished."

**Effort:** L — the content-registry hook doesn't exist yet; standalone operation (without
the game binary) would need a default fallback renderer. The sector/wall rendering math
already lives in `gfx.h` but the textures are game-side.

**Seam:** Hard seam constraint. The editor cannot `#include` game render code; the only
clean path is a `Eng_ContentRegistry` hook (see `engine-layers.md §5`) that the game
binary registers at embed time. Standalone editor gets a texture-free fallback.

---

### 2.5 Gizmo Handle Scale / Visibility Improvements
Make the gizmo handles larger (or configurable in Settings), add a per-axis color-coded
label ("X" / "Y" / "Z") alongside each arm, and add a toggle to hide the gizmo for clean
screenshots. Minor UX polish but surprisingly impactful when placing small entities.

**Why it matters:** Current gizmo handles are easy to miss-click at typical ortho zoom
levels. The debug-draw gizmo is functional but not polished enough for daily use.

**Effort:** S — `Eng_GizmoDebugDraw` already draws the handles; tweaking scale and adding
text overlay is a small change in `edscene.c`.

**Seam:** No constraint.

---

## 3. Document / Content

### 3.1 Place All Entity Kinds (Walls, Obstacles, Props, Wallbuys, Perks, Sectors)
Extend the PLACE tool picker beyond the current four (Player spawn, Zombie spawn,
Barricade) to cover every `EngMapEntKind`: walls (two-click endpoints), obstacles
(drag footprint), props (drop + yaw), wallbuys (wall-snap + weapon picker), perks (drop),
sectors (drag + height). Each uses `EngMapEnt_Add` and the appropriate `Eng_Set*`
mutators. The place palette becomes a scrollable list grouped by category.

**Why it matters:** The editor currently can only place 4 of its ~9 entity kinds. You
cannot build a map's rooms (sectors), walls, or perks/wallbuys without hand-editing the
`.map` file. This is the largest content gap.

**Effort:** M–L depending on per-kind UX (walls especially need a two-click flow with
endpoint preview; sectors need a drag-resize interaction). Obstacles and props are simple
drop-and-snap. A phased approach works: drop-and-snap for all kinds first (~M), polished
flows per kind after (~+S each).

**Seam:** No seam constraint for generic placement. Game-specific entity parameters
(weapon choice for wallbuys, perk type for perks) are string fields in `MapDoc` — the
editor shows a text field or a hard-coded enum picker without needing game knowledge.

---

### 3.2 Sector-Authoring Tools (Draw / Resize)
A "draw sector" mode: click-drag to define a new sector's X/Z footprint; a height
widget (scroll wheel while dragging, or Inspector field) sets yLow. Resize existing
sectors by dragging their footprint edges (an AABB corner/edge handle distinct from
the entity gizmo). Ramp authoring: pick FLAT vs RAMP kind, set rampAxis, link to
adjacent sectors.

**Why it matters:** Sectors are the structural unit of every map — you can't build a room
layout without them. Currently the only sector is the default 40×40 square.

**Effort:** L — sector edge handles require custom hit-testing separate from the entity
gizmo (sectors are quads, not point entities). Ramp linking (setting `linkA`/`linkB`)
is a two-entity interaction that needs a dedicated drag or picker. Worth tackling after
the simpler entity placement kinds (3.1) ship.

**Seam:** No constraint — `Eng_SetSectorSize`, `Eng_SetSectorHeights`, `Eng_SetPos` are
all in `mapedit.h`.

---

### 3.3 Map Metadata Panel
A dedicated panel (or Inspector section when nothing is selected) for top-level
`MapDoc` fields: map name, arena half-extents (X/Z), atmosphere (fog color/range, sky
tint), and texture slot overrides. All editable via text fields and color pickers.

**Why it matters:** Map name and arena size are the first things an author sets on a new
map. Currently there is no UI path to set them — they default to 40×40 and must be
hand-edited in the `.map` file.

**Effort:** S — the fields are already in `MapDoc`; no new mapedit.h mutators needed,
just direct struct writes + one `EdScene_Commit` call. The raygui color picker
(`GuiColorPicker`) is already available.

**Seam:** No constraint.

---

### 3.4 Prefab System (Save / Load Selection as Prefab)
`File > Save Selection as Prefab…` writes the selected entities to a small `.prefab`
file (a subset of the `.map` grammar — the same parser, just no SECTOR-level block
required). `Edit > Insert Prefab…` reads it back, allocates fresh ids for all entities,
and drops them at the cursor. Prefabs could live in a `data/prefabs/` folder browsed in
an Asset panel (see 5.3).

**Why it matters:** Community map authors think in reusable chunks — doorways, corridors,
spawn rooms. Prefabs are how Krunker-style editors give authors a vocabulary of building
blocks. Without prefabs, every map starts from scratch and popular layouts get
copy-pasted by hand across files.

**Effort:** M — the `.map` grammar already handles entity serialization; the prefab format
is a subset. The main new work is the "re-id on insert" step (allocate fresh ids, remap
sectorId references to the target map's sector indices).

**Seam:** No constraint — the file format and `MapDoc`/`mapedit.h` APIs are engine-side
already.

---

### 3.5 Door Editor
Inline wall-door authoring: select a wall, open a door sub-inspector that lets the
author set `door.present`, `door.center`, `door.width`, `door.cost`, and `door.name`.
Visualize the door as a differently-colored segment within the wall's proxy box. Spawn
lock-by-door: when editing a SPAWN, a "Locked by" field lists all named doors in the map
as a dropdown.

**Why it matters:** Doors are the core progression mechanic of the current game (and a
natural fit for Krunker-mode area-unlock). Currently doors can only be authored by
hand-editing the `.map` file; the Inspector doesn't show them at all.

**Effort:** M — `MapDocDoorSpec` is embedded in `MapDocWall`; no new mapedit.h mutators
are needed (direct struct write + commit). The dropdown of door names requires iterating
`doc.walls` to collect `door.name` strings — straightforward.

**Seam:** No constraint.

---

## 4. UX / IDE

### 4.1 Command Palette (`Ctrl+P`)
A fuzzy-search overlay over the viewport. Type a few letters to filter all registered
menu items and show the matching set as a scrollable list; Enter executes the top match.
The palette is fed by `EdHost`'s existing menu-item registry — no new registration is
needed, just a read API.

**Why it matters:** As the menu tree grows (more entity kinds, more tools), deep menus
slow down expert users. A command palette turns any action into "Ctrl+P, type 3 letters,
Enter." This is the feature most associated with "professional tool" vs "toy."

**Effort:** M — the fuzzy match is a few dozen lines; the overlay itself is a modal
(`EdHost_SetModal`); the menu-item registry needs a read API added to `edhost.h`.

**Seam:** `EdHost` needs a `EdHost_ForEachMenuItem` iterator for the palette to enumerate
registered items — a one-line addition to the plugin ABI. No game knowledge.

---

### 4.2 Hierarchy Search / Filter
A text input at the top of the HIERARCHY panel that filters the entity list by id,
kind name, or mob tag substring. Non-matching entities are greyed or hidden.

**Why it matters:** At 100+ entities the hierarchy list is a wall of `#id kind` strings.
Filtering to "show me all SPAWN MOB ZOMBIE entries" or "all OBSTACLE entities in sector 3"
is essential for large maps.

**Effort:** S — the panel already iterates `EdScene.proxies`; adding a `GuiTextBox` filter
string and a `strstr` check per row is a small change to `builtins.c`.

**Seam:** No constraint.

---

### 4.3 Dockable / Tabbed Panels + Layout Persistence
Allow panels within a dock zone to be reordered by drag, stacked as tabs (click the
title to bring to front), and have their preferred heights saved to `editor.cfg` on
shutdown. The current fixed-stack layout is functional but inflexible.

**Why it matters:** Different workflows need different panel layouts — a texture author
needs a big Inspector; a flow-layout author needs a big Hierarchy. Saved layouts let each
author configure once and work efficiently forever.

**Effort:** M — `edhost.c` hard-codes the left/right/bottom zone stacking; tab state and
order would require `EdHost` to track per-panel visibility flags and a concept of "active
tab." Layout persistence is `EngCfg_PutFloat` calls at shutdown. No new `EdHost_*` API
changes visible to plugins.

**Seam:** No constraint.

---

### 4.4 Keybinding Configuration
A "Keybindings" section in the Settings dialog that lets the author remap common
actions (view switches, place tool cycle, undo/redo, delete, gizmo mode) to alternate
keys. Persisted in `editor.cfg` as `key.undo = Z`, `key.delete = Delete`, etc. The
shortcut strings in registered `EdMenuItem` entries would be derived from these at
startup.

**Why it matters:** Power users on non-QWERTY layouts or small keyboards suffer from
hard-coded key assumptions. Remappable keys is table stakes for any tool used more than
casually.

**Effort:** M — requires a small key-name→raylib-keycode lookup table and a pass over
`edscene.c`'s hardcoded `IsKeyPressed(KEY_X)` guards to check a config table instead.

**Seam:** No constraint.

---

### 4.5 Unsaved-Changes Guard ("Dirty" prompt on New / Open / Exit)
When the user triggers New, Open, or Exit with `dirty == true`, show a modal
("You have unsaved changes. Save, Discard, or Cancel?"). Currently the editor logs a
warning on exit with unsaved changes but does not block.

**Why it matters:** Accidental data loss. This is a basic correctness expectation of any
file-editing tool. Low effort and high user trust.

**Effort:** S — one modal with three buttons, gated on `EdScene.dirty`. `EdHost_SetModal`
is already the right mechanism.

**Seam:** No constraint.

---

### 4.6 Inline Validation Indicators
Rather than "run Validate map from the Tools menu and read the Console," surface
validation errors live: highlight offending entities in the proxy draw (a red outline
on their AABB via `debugdraw.h`) and show a warning count badge in the status bar. Rerun
`MapDoc_Validate` after every commit (it is read-only and fast).

**Why it matters:** The current flow requires a manual validation step; inline indicators
make every save automatically "is this map broken?" without a context switch. This is how
Unity/Godot handle scene validation.

**Effort:** S — `MapDoc_Validate` is already engine-side and fast; the editor just needs
to call it post-commit and color-code the relevant proxy. The `debugdraw.h` overlay is
already used for gizmos.

**Seam:** Validation logic is already in `mapdoc.h` (engine side). The draw call
(`Eng_DebugDrawBox` or similar in `debugdraw.h`) is also engine-side. No constraint.

---

### 4.x Residual layout polish (from the 2026-06-18 layout audit)

The layout audit's P0/P1 findings were fixed (placement-only Tools panel, menu
reshuffle + submenus, status-bar/window-title cleanup, Console clear/filter, the
launcher); these **P2 polish** items and a couple of deferred ideas remain — folded
here so the audit file could be retired (full audit + resolution log is in git):

- **Place-tool feedback in the viewport** — armed tool shows no cursor change / ghost
  preview; only the highlighted palette button. Add a viewport overlay ("PLACING: …")
  or a ghost entity. *(P2-A)*
- **Hierarchy rows** — show `KIND (descriptor)` (e.g. `SPAWN (ZOMBIE)`) not just
  `#id KIND`; optional drag-to-reorder. *(P2-B)*
- **Settings dialog** — separate restart-required settings (VSync/FPS/window) into a
  banner-marked section instead of a `*` in the label. *(P2-D)*
- **Wall 2-click state** — surface "click 2 of 2" in the viewport, not only the palette
  button label. *(P2-E)*
- **Help ▸ Controls** — a modal/overlay instead of dumping to the Console (scrolls away).
  *(P2-F)*
- **Viewport top-bar** (recommended-layout idea) — a thin strip over the viewport showing
  active view / gizmo / place-tool badges; would also host the place-tool feedback above.
- **Validation error badge** — a `[3E 1W]` count in the status bar (red/gold) when
  `Validate map` finds issues (deferred P1-F; needs validation results plumbed to the
  status segment).

---

## 5. Pipeline / Integration

### 5.1 Play-Test the Map in the Game ("Launch Game")
`File > Play Test` (or `Ctrl+R`) saves the current map then launches the game binary
(`./shooter data/maps/<current>.map` or equivalent) as a child process. On exit the
editor regains focus.

**Why it matters:** The entire point of the editor is to make maps for the game. Every
iteration cycle that requires "save, switch terminal, run game, quit, switch back" costs
30 seconds of friction. A one-keystroke launch path cuts that to 5 seconds. This is how
Krunker's in-game editor works (the distinction is packaging, not architecture — see
`engine-layers.md §5`).

**Effort:** S–M — on Linux/macOS this is `fork`+`execvp`; on Windows `CreateProcess`.
The current `--check` headless mode in the editor binary is a similar lifecycle pattern.
No new engine code needed; this is purely editor-side `posix_spawn` or equivalent.

**Seam:** The editor launches the game as a separate OS process — no include dependency,
just a string path. Clean.

---

### 5.2 Live Reload (Watch File on Disk)
When the game is running the current map, poll `mtime` on the `.map` file; when it
changes (because the game saved it or the editor saved it), automatically reload — in the
editor, `EdScene_Open`; in the game (would need a `MapDoc_Reload` trigger, game-side).
The editor half is all that's in scope here.

**Why it matters:** For the future "edit while playing" Krunker-style workflow, the
editor saving and the game reloading must happen without a user action. Even without
the game side, auto-reload after an external tool modifies the file is useful.

**Effort:** S — `stat()`-based file watch in `edscene.c`, triggered in the update loop.

**Seam:** No constraint for the editor-side reload. The game-side hot-reload is a
separate feature in `src/game/`.

---

### 5.3 Asset Browser Panel
A right-panel tab listing the available assets: maps in `data/maps/`, props (GLB files)
in `data/models/`, textures in `data/textures/`. Click a map to open it; drag a prop
onto the viewport to place it as a `PROP` entity with `name` set to the filename (less
extension). A small thumbnail preview where raylib can render one.

**Why it matters:** Currently the only asset navigation is via the native file picker.
An in-editor browser keeps the author's hands in the tool, is filterable/searchable, and
is the prerequisite for drag-to-place prop authoring (the main workflow for decorating
maps with 3D models).

**Effort:** M — directory listing via `dirent.h` (or platform equivalent, already used by
the plugin loader); prop thumbnail requires a small off-screen render of the GLB.

**Seam:** Prop model loading (the GLB loader) is engine-side (`gfx.h`). Texture names are
just strings — the editor stores them without needing to render them. No game knowledge.

---

### 5.4 Export / Import Interop
An "Export as…" menu item that writes the map in an alternate format (e.g. JSON or a
human-diff-friendly YAML representation) for use with external tools. An "Import from…"
path does the reverse. Both go through the same `MapDoc` in-memory representation.

**Why it matters:** As community maps grow, authors will want to diff/merge map files in
git, use scripted map generators, or import from other tools. The `.map` grammar is
readable but not designed for programmatic manipulation. A JSON export is the standard
interop path.

**Effort:** M — requires a JSON serializer for `MapDoc` (the inverse of `MapDoc_Parse`
but targeting JSON). This is a pure `mapdoc.c` addition; no editor work beyond the menu
item.

**Seam:** The serializer belongs in `mapdoc.c` (engine-side), exported via `mapdoc.h`.
No game knowledge needed.

---

## 6. Plugin Hooks (Missing Seams for Third Parties)

### 6.1 `EdHost_ForEachMenuItem` — command palette prerequisite
A read-only iterator over the registered menu items so a plugin (e.g. a command palette)
can enumerate them. Currently the menu registry is write-only from the plugin surface.

**Effort:** S — one new function in `edhost.h` / `edhost.c`.

---

### 6.2 `EdHost_OnCommit` — post-edit notification hook
A registration API: `EdHost_AddCommitHook(h, fn, user)` — called after every
`EdHost_CommitEdit` / `EdScene_Commit`. Lets plugins react to document changes (e.g.
a live validation overlay, a dependent panel that auto-refreshes).

**Effort:** S — a simple callback list in `EdHost`.

---

### 6.3 `EdHost_AddViewportOverlay` — custom 2D overlay per frame
A registration API that lets a plugin draw directly into the viewport after the 3D scene
and gizmo layers. Returns a `Rectangle` of the viewport in screen coords; the plugin
calls raygui or raylib 2D draw commands. Required for a "tape measure" or "snap preview"
overlay that a third-party plugin would naturally implement.

**Effort:** S — a callback list in `EdHost` called during viewport draw, after the scene
blit.

---

### 6.4 Content-Registry Hook for Game-Specific Inspectors
`Eng_RegisterContentExtension(kind, name, drawFn)` — called by the *game binary* when
the editor is embedded, to register a custom inspector draw function for a specific
entity kind (e.g. "when kind == WALLBUY, show a weapon picker enum"). This is the only
sanctioned path for the game to contribute UI to the editor without breaking the seam.

**Effort:** M — requires agreeing on the ABI (the `drawFn` signature), adding the
registry to `libengine.a` (as a toolkit primitive, not game-side), and wiring the
Inspector panel to call registered extensions after the generic fields.

**Seam:** This IS the seam feature. The registration function must live in the engine
(so both the editor and the game can call it), but the implementations live game-side.

---

### 6.5 `EdHost_AddPlaceTool` — plugin-contributed place tools
Lets a plugin register a new entry in the PLACE palette (beyond Player/Zombie/Barricade)
with a custom click callback. Required for any plugin that adds entity kinds not in the
base `EngMapEntKind` enum.

**Effort:** S — mirrors the existing `EdHost_AddMenuItem` / `EdHost_AddPanel` pattern.

---

## Top 5 to Do Next

These are the five features that should ship before any others, ranked by the ratio of
(editing friction removed) / (implementation cost), with the Krunker north-star as the
tiebreaker.

### #1 — Editable Inspector (1.1) — S effort
The single most-asked-for feature in any editor that shows read-only fields. It unblocks
precise placement of everything (walls at exact grid positions, perks at known coordinates)
without requiring the author to hand-edit the `.map` file. The mapedit.h mutators are
100% ready; this is a wiring exercise. Ship first.

### #2 — Rotate / Scale Drag (1.2) — S effort
The gizmo modes 2 and 3 are already computed but silently discarded. Two pages of code
in `edscene.c` complete them. Props become useful to place once you can yaw them. This
and #1 together make the editor feel finished for basic authoring.

### #3 — Focus on Selection, F key (2.1) — S effort
Camera navigation is the hidden productivity killer. Four lines of code in `edscene.c`
make "find that entity I just placed" instantaneous instead of a 10-second fly-around.
Surprisingly high impact per line of code — the classic hidden gem.

### #4 — Place All Entity Kinds (3.1) — M effort
The editor currently cannot place the majority of its own entity kinds. Sectors, walls,
obstacles, and props must be hand-authored in the `.map` file. This is the widest
feature gap between "toy editor" and "real editor." Start with drop-and-snap for all
kinds (no special two-click flows yet) — that alone covers 80% of the need at ~half
the effort.

### #5 — Unsaved-Changes Guard (4.5) + Inline Validation (4.6) — S+S effort
Group these because they are both correctness features that should ship before authors
start relying on the editor for real maps. The guard prevents data loss; the live
validation overlay means authors stop hitting `Tools > Validate map` manually after every
change. Together they are one afternoon's work and they make the tool trustworthy.

---

### Honorable mention: Focus on Selection (#3 above)
Technically already in the Top 5, but worth restating: **four lines of code, immediate
daily impact on every editing session**. If only one feature ships this sprint, make it
this one.

### Surprise high-value / low-effort pick: Play-Test Launch (5.1)
Launching `./shooter <current_map>` as a child process is a `posix_spawn` call and a
menu item — maybe 50 lines total including error handling. It collapses the edit/test
cycle from a 30-second context switch to a single keystroke. In terms of perceived
editor quality, nothing else at S effort comes close to the visceral "I pressed a button
and my map is running" moment.
