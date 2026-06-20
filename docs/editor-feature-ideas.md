# Editor Feature Ideas — DRAFT Backlog

> **Status: DRAFT — for discussion.** This is a prioritized backlog of missing or
> desirable features for the map editor (`src/editor/`). It goes beyond the §5 roadmap in
> `scene-builder.md` (which captures the immediately-next items already in view). Each
> entry notes effort (S/M/L), ties the feature to the editing workflow or the Krunker
> north-star, and flags any engine/game-seam constraint.
>
> **See also** [editor-ux-review.md](editor-ux-review.md) (2026-06-20) — a critical IA/UX
> teardown + proposed redesign that supersedes the §4.x layout-polish items here where they
> overlap (viewport-first frame, a Unity-style bottom-docked content browser replacing the
> TOOLS palette, a sector-tree Hierarchy, MAP PROPERTIES → a modal).
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
> **2.4 Game-Accurate Textured Rendering** shipped (now **lit**) — the `M` "material mode"
> toggle draws textured floors/walls/obstacles/props under a default editor lighting state;
> see [scene-builder.md](scene-builder.md) §5.7. Also shipped:
> **5.3 Asset Browser Panel** (the ASSETS panel — maps/models/textures from the content
> overlay, model thumbnails, click-a-map-to-open; `edassets.{c,h}` + `edthumb.{c,h}` —
> models are browse-only, props place from the Props palette). A later wave shipped
> **1.3 Multi-Select** (Shift-click; box-select still open), **1.4 Copy / Paste / Duplicate**
> (Ctrl+C/X/V/D + Ctrl+A select-all), **inspector per-surface texture override** (the `tex`
> field on walls/obstacles, §2.4-adjacent), and **autosave + crash recovery** (§4.7). Then
> **5.1 Play-Test Launch** (`Ctrl+R` — save + spawn the game on the current map) closed out
> the Top 5. (Beyond the editor backlog, the games-as-projects **New/Open Game** UI also
> shipped — see [game-projects.md](game-projects.md) §8 / §10.)

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

### 1.3 Multi-Select (Shift-click) — ✅ SHIPPED (box-select still open)
Hold Shift and click to add/remove from a selection set; drag an empty area to rubber-band
select all entities whose proxy AABBs intersect the 2D screen rectangle. Store
`selectedId[]` array instead of a single `selectedId` int. Move/delete/cycle all operate
on the set; gizmo shows at the centroid and translates all selected entities by the same
delta.

**As built:** `EdScene` now carries `selIds[]/selCount`; `selectedId` is the PRIMARY
(gizmo pivot + inspector target, the last-added member). `EdScene_SelectClick(id, additive)`
drives Shift-click in both the viewport and the Hierarchy panel; `Ctrl+A` =
`EdScene_SelectAll`. The move gizmo snapshots each member's start pos at grab and translates
the whole set by the primary's snapped delta (rigid group move, relative layout preserved).
Delete operates on the set. Two **divergences** from the sketch: the gizmo pivots on the
primary (not a computed centroid), and **rubber-band box-select is not done** — selection is
click + Shift-click only. Rotate/scale still act on the primary alone.

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

### 1.4 Copy / Paste / Duplicate — ✅ SHIPPED
`Ctrl+C` snapshots the selected entities' `MapDoc` data (positions, kinds, all fields)
into an editor clipboard; `Ctrl+V` re-adds them via `EngMapEnt_Add` at a fixed offset
from the original positions (e.g. +2 units X/Z) so they don't land exactly on top.
`Ctrl+D` (duplicate-in-place) is copy+paste in one action. After paste, the new entities
become the selection. All paste operations push one `EngMapHistory_Commit`.

**As built:** `EdScene.clip[]` holds full entity structs (a per-kind union, absolute
positions) so it never references live ids — cut-then-paste is safe. `Ctrl+C/X/V` =
copy/cut/paste, `Ctrl+D` = duplicate; the nudge is one grid cell when snapping (else 2
units), and `clipPasteSeq` steps each paste so repeated pastes fan out instead of stacking.
The new entities become the selection and the op commits once. The clone primitive lives in
the engine as `EngMapEnt_Clone` (used by duplicate); paste re-adds + copies the stored
struct with a re-minted id. Walls offset both endpoints; all other kinds use `Eng_SetPos`.
Also added: Edit-menu items + `Delete`-key binding alongside `X`.

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

### 2.4 Game-Accurate Textured Rendering — ✅ SHIPPED (lit)
Replace proxy boxes with actual sector floor/wall/obstacle/prop geometry rendered with the
map's textures, behind an additive **`M` "material mode"** toggle (proxy path stays the
default). Shipped in `EdScene_DrawViewport` — no content-registry hook was needed after all,
because the textured-draw helpers already live engine-side (`Eng_DrawTexturedFloorV` /
`Eng_DrawTexturedBoxV` in `gfx.h`) and take a raw `Texture2D*`; the editor resolves textures
itself via `Eng_LoadTextureByName` from `doc.textures`, and loads prop models via
`Eng_LoadModel`. No game header, seam intact.

**Why it matters:** Placing entities relative to grey boxes is disorienting; placing them
relative to the actual rendered floor/walls matches the player's experience.

**Lighting — ✅ SHIPPED (the former "still open"):** `editor_main` now calls `Eng_RenderLoad`
once after the window opens, and `DrawMaterialWorld` pushes a fixed **bright editor lighting
state** (high ambient, fog pushed far past any editor map) via `Eng_RenderSetLighting` and
wraps the draws in `Eng_RenderBeginWorld`/`EndWorld`. Floor/wall/obstacle (immediate-mode)
draws pick up the bound world shader automatically; prop models are stamped with
`Eng_RenderWorldShader()` per draw so they're lit too. **Still open:** game-specific draws
(perk machines, wallbuys rendered as their real models) would want the `libgame_edbridge.so`
plugin path for full parity — see [scene-builder.md](scene-builder.md) §5.7.

**Per-surface texture override — ✅ SHIPPED (companion to the above):** the Inspector now
has a **`tex`** text field for `WALL` and `OBSTACLE` (walls previously had no inspector
fields at all). It reads/writes the `MapDoc` `TEX` field via the engine accessors
`Eng_Get/SetSurfaceTex`; a blank string clears the override so the surface falls back to the
map's `wall_ext` slot. Material mode already honoured `texName` when drawing — this removes
the forced hand-edit of the `.map` text that was the only way to set it before.

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
**Draw — ✅ SHIPPED.** The SECTOR tool is now **RECT-drag**: press a corner, drag the X/Z
footprint with a live preview (a blue outline in `EdScene_DrawViewport`), release to create;
a plain click (or a sub-1-unit drag) falls back to a default 20×20 at the anchor. Built as
`UpdateSectorDrag` + `sectorDragging`/`sectorStart`/`sectorCur` state on `EdScene`; created
sectors get FLAT heights at y=0 and commit one undo step. Height (`yLow`/`yHigh`) and exact
size are then tuned in the **Inspector** (the scroll-wheel-while-dragging height widget was
dropped — wheel is zoom in the ortho views — in favour of the Inspector field).

**Resize — ✅ SHIPPED.** A selected sector shows four **edge handles** (W/E/N/S), hit-tested
as small AABBs against the pick ray (the same ray primitives selection uses — no
screen-projection convention needed, sectors being quads not point entities). Dragging an
edge moves it to the cursor's snapped ground position while the **opposite edge stays fixed**,
re-deriving centre+size each frame (drift-free, mirroring the gizmo drag convention) and
coalescing under one undo tag. Built as `BeginSectorResize`/`UpdateSectorResize` +
`DrawSectorHandles` (hover/active highlight) with `resizing`/`resizeEdge`/`resizeFixed` state;
the translate gizmo still moves the whole sector, edge handles resize it.

**Ramps — ✅ SHIPPED.** The sector Inspector now has a **FLAT/RAMP** toggle, and for a ramp
a **rise axis** (+X/+Z) toggle plus two **link** buttons (click to cycle through the map's
sectors, or `<none>`) that set `linkA`/`linkB` — the nav-edge the game turns into an AI
ramp connection. Switching to RAMP seeds a valid default (a rise + axis) so it validates
immediately; `yLow`/`yHigh` stay the Inspector height fields (low/high edge). New engine
mutators back it: `Eng_Set/GetSectorKind` + `Eng_Set/GetSectorRamp`. **Bug found + fixed
along the way:** editor-created sectors had no name, so `MapDoc_Save` emitted a nameless
`SECTOR`/`RAMP` line that wouldn't reparse (and ramp `LINK` resolves by name) —
`EngMapEnt_Add` now assigns a unique `sec<id>` default. Verified by a save→reparse→validate
round-trip. **In-viewport visualisation — ✅ SHIPPED:** every RAMP sector draws its inclined
surface as a wireframe (perimeter + 1/4/1/2/3/4 slope rungs) plus an uphill arrow along the
rise centre line, mirroring the game's `DocSectorY` interpolation (axis 1 → +X, axis 2 → +Z;
−axis edge yLow, +axis edge yHigh); the selected ramp draws brighter (`DrawRampOverlay`).
**Link-picking — ✅ SHIPPED:** each link row has a **Pick** button that arms a viewport
click-to-link mode (`s->linkPick`); the next click on a sector sets that link to the clicked
sector (strict containment, self excluded; clicking empty clears it), Esc cancels
(`UpdateLinkPick`). The cycle buttons remain as the no-aiming alternative. §3.2 is now fully
built — draw / resize / ramps (kind + axis + links + visualization + picking).

**Why it matters:** Sectors are the structural unit of every map — you can't build a room
layout without them. Before RECT-drag the only practical sector was the default square.

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

### 4.7 Autosave + Crash Recovery — ✅ SHIPPED
Periodically write a recovery copy of the document while it is dirty, and offer to restore
it after a crash. **As built:** `EdScene_AutosaveTick` (called once per frame from
`editor_main`) writes `<map>.autosave` every `ED_AUTOSAVE_SECS` (30s) of dirty edit-time;
a clean manual save (`EdScene_Save`) deletes the file, so the on-disk map stays
authoritative. On load, if a `<map>.autosave` is newer than the map itself,
`EdBuiltins_RecoveryGuard` (also per-frame, idempotent per path → covers initial load *and*
mid-session Open) raises a **RECOVER UNSAVED WORK** modal with Restore / Discard.
`untitled.map` (the unsaved scratch name) is never autosaved or recovered. Helpers:
`EdScene_RecoveryAvailable / RestoreRecovery / DiscardRecovery`.

**Seam:** No constraint — `EdScene` + raylib FS (`GetFileModTime`/`FileExists`) only.

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

### 5.1 Play-Test the Map in the Game ("Launch Game") — ✅ SHIPPED
`File > Play Test` (`Ctrl+R`) saves the current map then launches the game binary on it as a
detached child; the editor keeps running.

**As built:** `EdScene_PlayTest` (one entry point — the menu item and the `Ctrl+R` global
shortcut both call it) saves, resolves the game binary next to the editor binary via
`GetApplicationDirectory()`, and `posix_spawn`s it with the map path as argv. `SIGCHLD` is
set to `SIG_IGN` so finished play-tests are auto-reaped (no zombies) and the spawn never
blocks the frame loop. Untitled scratch maps are refused (no on-disk file to hand the game).
Game side: `src/game/main.c` treats a positional `.map` arg as a **boot map**, and
`GameMod_Init` calls the new `Menu_StartSoloOnMap`, dropping straight into solo play on it
instead of the menu. (POSIX-only for now; a Windows `CreateProcess` branch is the port.)

**Why it matters:** The entire point of the editor is to make maps for the game. Every
iteration cycle that requires "save, switch terminal, run game, quit, switch back" costs
30 seconds of friction. A one-keystroke launch path cuts that to 5 seconds.

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

### 5.3 Asset Browser Panel — ✅ SHIPPED
A right-zone panel listing the available assets: maps, models (GLB), textures — scanned
from the **content overlay** (`Eng_ContentDirs`, game-over-library), not a hardcoded
`data/` path. Implemented as `edassets.{c,h}` (the de-duped index, held on `EdScene`) +
the `PanelAssets` built-in + `edthumb.{c,h}` (off-screen GLB thumbnail cache). Click a map
to open it (through the unsaved-changes guard); models/textures are browse-only previews
(props place from the catalog-backed Props palette), with per-model thumbnails rendered
off-screen and cached.

> **Implementation note worth keeping:** the host wraps every panel `draw` in a
> `BeginScissorMode` (edhost.c). `BeginTextureMode` honours the active GL scissor, so an
> off-screen thumbnail render under that scissor is clipped to the panel rect and produces
> an empty texture. `PanelAssets` pre-renders thumbnails with the scissor **lifted**
> (`EndScissorMode` → render → restore), before its own list draw.

Still future (the literal-doc wishlist beyond what shipped): true **drag**-to-place props
(today the Props palette is click-to-arm), texture-slot assignment from a texture click,
and a copy-on-import action (stock library asset → game folder, the
[game-projects.md](game-projects.md) overlay's writable side).

**Resolved — model-as-prop seam gap:** an earlier build let a Models-row click stamp
`prop.name` = the model file stem, which the game **silently skipped at load** when no
matching `.prop` existed (`Props_IndexByName`; `level.c`: "unknown prop — skipping"). This
is now closed by **restricting placement to catalog-backed props**: the Models section is
**browse-only** (clicking just logs a hint pointing at the Props palette), so the only way
to place a prop is the catalog **Props** palette section — which always resolves and always
renders in-game. Raw-`.glb` placement (scaffold a `.prop` on drop, or a game-side raw-model
fallback) remains a possible future convenience, not a correctness gap.

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

## Top 5 to Do Next — ✅ ALL SHIPPED

> The original Top 5 are all done: **#1 Editable Inspector**, **#2 Rotate/Scale Drag**,
> **#3 Focus on Selection**, **#4 Place All Entity Kinds**, and **#5 Unsaved-Changes Guard +
> Inline Validation** — plus the "surprise pick" **Play-Test Launch (5.1)**. The list below
> is kept as the original rationale. For the *next* wave, pull from the medium backlog
> (§1.5–§1.7, §2.2–§2.3, §3.2/§3.4/§3.5, §4.1/§4.3/§4.4) or the plugin seams (§6).

These were the five features ranked by the ratio of
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

### Surprise high-value / low-effort pick: Play-Test Launch (5.1) — ✅ SHIPPED
Launching `shooter <current_map>` as a detached child turned out to be exactly the
`posix_spawn` + menu-item it was predicted to be (plus a small game-side boot-map path). It
collapses the edit/test cycle from a 30-second context switch to a single keystroke
(`Ctrl+R`) — the visceral "I pressed a button and my map is running" moment. See §5.1.
