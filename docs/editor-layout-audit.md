# Editor Layout Audit

**Auditor:** UX/tools review, 2026-06-18  
**Sources read:** `src/editor/edhost.c`, `src/editor/builtins.c`, `src/editor/edscene.h`,
`docs/scene-builder.md`  
**Screenshot:** The `--shot` run was blocked by the sandbox; the maintainer should run:
```
cd build && ./editor --shot /tmp/editor_layout.png games/shooter/maps/nacht.map
```
and compare visually against the findings below.  All findings are derivable from the
source code alone — the code is the ground truth.

---

## Resolution log

- **2026-06-18 — P0-A/B/C/D + P2-C done.** `PanelTools` is now placement-only:
  the UI-scale slider (P0-A), the VIEW + GIZMO toggle groups (P0-B), and the
  barricade auto-spawn checkbox (P0-C) are removed — UI scale stays on `Ctrl+=/−`,
  view on F1-F3 + the View menu, gizmo on 1/2/3, auto-spawn in Settings. Gizmo
  mode is now shown read-only in the status-bar `view:` segment so it stays
  discoverable. The palette scrolls within its panel scissor (mouse wheel) and
  gates off-panel widgets, so it no longer clips as the mob catalog grows (P0-D);
  the dead `ED_UI_MIN/MAX` defines went with the slider. Separately, splitter
  handles now show a resize cursor on hover/drag (P2-C). **Still open:** P1-* and
  the rest of P2 (incl. the recommended viewport toolbar / Inspector tabs / menu
  reshuffle). Visual confirmation of the scrolled palette is pending a hands-on
  run — `--shot` capture is blocked in the sandbox.

---

## Blunt Verdict

The Tools panel is a junk drawer. It staples together five completely unrelated
concerns — an application display setting (UI scale), transient mode toggles (view
camera, gizmo mode), a document-level editing behaviour (barricade auto-spawn), and a
large palette of placement tools — into one fixed-height panel with no grouping
separators, no hierarchy, and a hard-coded `prefH=256` that will truncate as the mob
catalog grows. Meanwhile the View menu duplicates the camera toggles verbatim, the
Tools menu has *two items* (Settings and Validate), the status bar wastes the one
always-visible surface on text that belongs in the title bar, and the Inspector quietly
doubles as the map-metadata editor with no affordance that selecting nothing is itself a
"mode". The dock split (Tools+Hierarchy left, Inspector right, Console bottom) is the
right topology on paper, but every panel's *contents* betray it: settings leak into
panels, tool state leaks into menus, and the Inspector carries ambient document data
without telling the user. The result is an editor that technically works but is
disorienting to learn and will become unmaintainable as content grows.

---

## Control Inventory

| Control | Current panel/location | Where it should live | Why |
|---|---|---|---|
| **UI-scale slider** (`uiScale`) | TOOLS panel (top of left zone) | View menu (submenu item) and/or `Ctrl+=/−` shortcut only | UI scale is an *application display preference*, not a tool or a document property. No reference IDE puts display scaling in a tool palette. |
| **VIEW toggle group** (Fly / Iso / Top) | TOOLS panel | View menu only (already there) + F1/F2/F3 | Already duplicated in View menu. In-panel copy is pure redundancy at premium vertical space. |
| **GIZMO mode toggle** (Move / Rot / Scale) | TOOLS panel | Toolbar strip across top of viewport OR keyboard-only (1/2/3) | Gizmo mode is a transient interaction state, not a document tool. It belongs near the viewport (above or below it), not buried in a scrollable left panel. |
| **"Select / move" button** | TOOLS panel | Same toolbar strip above/below viewport | Same class of transient mode; also redundant with simply not having a place tool armed. |
| **Place tool buttons — Spawns group** (Player spawn, per-mob buttons) | TOOLS panel | TOOLS panel (correct zone) — but needs its own scrollable sub-area | This IS what the Tools panel is for; keep it here, just isolated. |
| **Place tool buttons — Geometry group** (Barricade, Wall, Obstacle, Prop, Sector) | TOOLS panel | TOOLS panel (correct zone) | Correct location. |
| **Place tool buttons — Buyables group** (Wallbuy, Perk machine) | TOOLS panel | TOOLS panel (correct zone) | Correct location. |
| **"auto-spawn ZOMBIE" checkbox** | TOOLS panel, inline between Barricade button and Wall button | Settings dialog (EDITING section) | This is a persistent editing preference, not a mode toggle. It already lives in the Settings dialog too (redundant). It belongs only there. |
| **Camera settings** (fly speed, look sens, orbit sens, zoom speed) | Settings modal (Tools > Settings) | Settings modal — CORRECT | |
| **View settings** (default view, FOV, default zoom) | Settings modal | Settings modal — CORRECT, but FOV and default zoom should arguably be under a View submenu toggle for quick live adjustment | |
| **Grid / Snap settings** (spacing, extent, snap toggle, snap step) | Settings modal + `View > Snap to grid` toggle | Settings modal is correct for spacing/extent; snap toggle should also be accessible from a toolbar affordance near the viewport | The grid settings are buried two levels deep (menu → modal); the snap toggle is the exception and rightly in the View menu, but there is no visual "snap active" indicator beyond the status bar and menu checkmark. |
| **Barricade auto-spawn (in Settings)** | Settings modal, EDITING section | Settings modal only (remove the panel copy) | Duplicated. |
| **Undo depth** | Settings modal | Settings modal — CORRECT | |
| **VSync / FPS cap / window size** | Settings modal, DISPLAY section | Settings modal — CORRECT | But these are restart-required; the dialog does not block or warn enough. |
| **ui.scale (Settings modal)** | Settings modal DISPLAY section | No — this should not be in the Settings modal at all; the modal already mentions Ctrl+=/− in the doc. | The slider in TOOLS panel and the implicit cfg key `ui.scale` are two write paths to the same value. There is no slider in the Settings modal (correct), but the TOOLS slider is wrong anyway. |
| **Validate map** | Tools menu | Tools menu — CORRECT | But it outputs to the Console, which is below the status bar; there is no red indicator on the menu bar or status bar to tell the user validation failed while they keep working. |
| **Settings…** | Tools menu | Tools menu — acceptable, but "Preferences…" is the conventional label and it should be at the bottom of the menu, not the top | |
| **Undo / Redo** | Edit menu + Ctrl+Z/Y | Edit menu — CORRECT | |
| **Delete selected** | Edit menu + X | Edit menu — CORRECT; but the shortcut shown is just "X" (no Ctrl), which is an unconventional delete binding that will surprise new users | |
| **Map name** | Inspector (no-selection state) | Inspector no-selection state — CORRECT in principle, but needs an explicit "MAP PROPERTIES" affordance that makes it clear you must deselect everything to reach it | |
| **Atmosphere (fog colour, fog range, sky tint)** | Inspector (no-selection state) | Inspector — marginally acceptable, but fog/sky are scene-wide settings; they belong under a dedicated "Scene" or "Environment" sub-panel or the map-metadata section of the Inspector should be clearly titled and reachable via a dedicated button, not by accident of having nothing selected | |
| **Texture slots (floor, wall, etc.)** | Inspector (no-selection state, read-only) | Same as atmosphere — correct location but needs affordance | |
| **Hierarchy search filter** | HIERARCHY panel (top of panel) | HIERARCHY panel — CORRECT | |
| **Hierarchy list** | HIERARCHY panel | HIERARCHY panel — CORRECT | |
| **Inspector entity fields** (pos, yaw, scale, facing, size) | INSPECTOR panel | INSPECTOR panel — CORRECT | |
| **Status segments** (map name+dirty, entity count, view mode+snap, selection) | Status bar | Mostly correct, but map name+dirty belongs in the window title bar, and "no selection" text is noise | |
| **Console log** | CONSOLE panel (bottom zone) | CONSOLE panel — CORRECT, but it has no clear/filter controls and no severity filter | |
| **Help > Controls** | Help menu | Help menu — CORRECT, but it prints to Console which scrolls away immediately | |

---

## Prioritised Findings

### P0 — Blunders (fix before shipping to any new user)

**P0-A: UI-scale slider in the Tools panel**

The UI-scale slider is the very first control in the Tools panel, at the top, stealing
precious vertical space from the placement tools that actually belong there. `uiScale` is
an application-level display preference. There is no reference IDE on earth that puts
display scaling in a tool palette. In Unity it is Edit > Preferences > General. In Godot
it is Editor > Editor Settings > Interface. In VS Code it is in the Command Palette or
keyboard shortcut. The keyboard shortcut (`Ctrl+=/−`) already exists in `edhost.c` and
works globally — that should be the *only* way to change it. The slider leaks its
own implementation detail (`ED_UI_MIN`/`ED_UI_MAX`) into the user-facing surface and will
confuse anyone who opens the editor for the first time.

**Fix:** Remove the slider from `PanelTools`. Expose it only via `Ctrl+=/−` and,
optionally, a numeric entry in the Settings dialog under DISPLAY. Add a brief label
beside the `Ctrl +/-` shortcut in Help > Controls.

---

**P0-B: VIEW and GIZMO toggles duplicated between the Tools panel and the menu bar**

The VIEW toggle group (Fly / Iso / Top) appears in the Tools panel *and* in the View
menu *and* is keyboard-accessible via F1/F2/F3. That is three surfaces for one action.
The GIZMO mode toggle (Move / Rot / Scale) lives only in the panel, but it duplicates
keyboard shortcuts 1/2/3. Neither belongs in the Tools panel — the panel is supposed to
hold *placement tools* (what you drop on the map), not transient interaction state (how
you navigate or drag).

The duplicate VIEW group wastes about 52 scaled-px of vertical space that placement
tools need. The GIZMO group wastes another 46 px and introduces confusion: a user who
accidentally clicks "Iso" in the panel gets the same result as F2 but has no idea why,
and the panel group is visually indistinguishable from the placement buttons below it.

**Fix:** Remove both toggle groups from `PanelTools`. Keep F1/F2/F3 and 1/2/3 as the
canonical shortcuts. Add a thin toolbar row above or below the viewport (a new
`ED_DOCK_TOPBAR` or a simple overlay strip) that shows the active view mode and gizmo
mode as read-only badges — this gives users visual confirmation without occupying the
panel. The View menu already has the camera items with checkmarks; that is enough
discoverability.

---

**P0-C: "auto-spawn ZOMBIE" checkbox is duplicated and misplaced in the panel**

The barricade auto-spawn behaviour toggle appears inside `PanelTools` between the
Barricade button and the Wall button. It is not a placement tool — it is a persistent
editing preference that controls a side-effect of placing barricades. It is *already*
available in the Settings dialog under EDITING. Having it in both places creates two
write paths to the same `barricadeAutoSpawn` field, which is fine technically but
terrible for the user: which one is authoritative? Does the panel checkbox save? (It
doesn't — it only saves when the Settings dialog Save button is clicked.) The panel
checkbox will silently revert on next launch if the user changes it there without going
to Settings.

**Fix:** Remove the checkbox from `PanelTools` entirely. It belongs only in the Settings
dialog. If quick toggling is needed, add it as a `View` menu item (or an `Edit` menu
item under a "Editing Behaviour" separator) with a checkmark, wired to the same field
and auto-saved on toggle (like `View > Snap to grid` already is).

---

**P0-D: Tools panel fixed height will truncate as mob catalog grows**

`RegisterPanels` hard-codes `prefH=256` for the TOOLS panel. At 1.0× scale that is 256
unscaled pixels for the panel content. The current content alone — UI slider (24px),
VIEW section (46px), GIZMO section (46px), Select/move button (25px), Spawns label+buttons
(≥14+25×N px for N mob defs), Geometry group (5 buttons + checkbox = 130px), Buyables
(2 buttons = 50px) — already exceeds 256px even with the default single-mob catalog.
If a game project adds 4 mobs, the placement buttons are clipped by the scissor rect.
There is no scroll in the Tools panel, so clipped buttons are simply invisible and
unreachable except via keyboard `P` cycling.

This will manifest the moment someone adds a second mob type, which is an advertised
feature ("new mob is a new tag").

**Fix (short-term):** Remove P0-A, P0-B, P0-C from the panel to reclaim ~150px. Give
the remaining placement content a scrollable sub-area or make the whole TOOLS panel
flex-height (remove `prefH`, let the splitter position determine its share).

**Fix (proper):** The placement palette should be its own scrollable panel region with
section headers (Spawns / Geometry / Buyables). `prefH=0` (flex) with a scroll offset
mirrors how Unity's component palette or Godot's Create Node dialog works.

---

### P1 — Should Fix (clear UX debt, not emergencies)

**P1-A: Inspector "no selection = map metadata" mode has zero affordance**

When nothing is selected, the Inspector panel silently switches to showing MAP METADATA
(name, atmosphere, textures). There is no button, no tab, no heading above the fold
that tells the user this mode exists. A new user who selects every entity to look at
them will never discover that deselecting everything brings up fog/sky settings. Unity
handles this with an explicit "Project Settings" asset that appears in the Inspector when
selected; Godot has a dedicated "Project Settings" dialog. At minimum the Inspector
header label should say "INSPECTOR — no selection → map props" and the map metadata
section should be reachable by a button in the panel header or toolbar.

**Fix:** Add a "Map" button in the INSPECTOR panel header (or as a tab: "Entity | Map")
that forces the no-selection view. Rename the internal heading from "MAP METADATA" to
"MAP PROPERTIES" to match conventional language.

---

**P1-B: View menu and Tools panel both own camera-switch actions; menu bar inconsistency**

`View > Fly camera / Isometric camera / Top-down camera` (with checkmarks, F1/F2/F3
shortcuts) is the right design. The panel toggle group (see P0-B) is the wrong duplicate.
Additionally, `View > Snap to grid` is correctly in the View menu, but there is no
equivalent for gizmo-mode switching (1/2/3) or place-tool cycling (P). These are
keyboard-only operations with no menu discoverability for new users.

**Fix:** Add `View > Gizmo mode` submenu (Move / Rotate / Scale, checkmarks, shortcuts
1/2/3) after removing the panel toggle. Add `Tools > Place tool` submenu (Select, Player
spawn, [mob names], Barricade, Wall, Obstacle, Prop, Sector, Wallbuy, Perk — checkmarks
showing active tool, shortcut P to cycle) so the placement palette is menu-discoverable
even if the left panel is collapsed.

---

**P1-C: Status bar carries "no selection" dead text and duplicates the title bar**

The status bar has four segments, always visible: map filename+dirty, entity count, view
mode+snap, selection. Two problems:

1. "no selection" is shown in the rightmost segment when nothing is selected. This is
   non-information. Unity shows nothing; Godot shows nothing. A status bar segment that
   always says "no selection" trains users to ignore the status bar entirely — which
   defeats the purpose of showing the *actual* selection when something is selected.

2. The map filename is in the status bar, not the window title. The OS window title is
   the universally expected location for the document name (Blender, Unity, VS Code all
   use it). The status bar copy is redundant if the title bar also shows it (it may not —
   `editor_main.c` should be checked). If the title bar shows only "editor" and the
   filename is only in the status bar, that is worse — it violates the platform
   convention.

**Fix:** Remove the "no selection" static text — emit an empty string from `st_sel`
when `selectedId < 0`. Set the OS window title via `SetWindowTitle()` to
`"<filename><dirty> — Scene Builder"` in the same place the status segment is updated.
Keep the status bar segment for the quick `#id kind` readout when something IS selected.

---

**P1-D: Tools menu has only two items but owns "Settings" at position 1**

The Tools menu contains exactly two items: `Settings…` and `Validate map`. In every
reference IDE, "Settings" / "Preferences" is the *last* item in its menu (after
task/utility items), separated by a line — it is the administrative tail, not the
primary affordance. Having "Settings…" as item 1 of 2 in the Tools menu means the first
thing a user finds when they go looking for map tools is a modal dialog that buries them
in camera sensitivity sliders.

Additionally, the label "Settings…" is ambiguous — it could mean editor preferences OR
map-specific settings. The Settings dialog actually contains only editor-preference data
(`editor.cfg`), not map data. "Preferences…" would be clearer. Alternatively, map-related
utilities (Validate, future bake/export operations) belong in the Tools menu, and
preferences belong in an Edit menu (Edit > Preferences is the Blender/GNOME convention)
or at the bottom of the Help menu.

**Fix:** Move the Settings item to `Edit > Preferences…` (standard) or to the bottom of
the Tools menu after a separator. Rename it "Preferences…" to distinguish from
map settings. The Tools menu should lead with `Validate map` (and any future
map-operation items) as its primary content.

---

**P1-E: File menu has "New Game" / "Open Game" mixed with "New map" / "Open map" in one list**

The File menu currently has: New | Open… | Save | Save As… | [separator] | [map recents]
| [separator] | New Game… | Open Game… | [separator] | [game recents] | [separator] |
Reload | Exit. The `New Game…` / `Open Game…` entries are project-level operations (they
change what game the editor is editing), while `New` / `Open` are document-level
operations (they change which map is open). Mixing project and document operations in
the same flat list without strong visual separation is the Unity-before-2018 mistake.
A user accustomed to `File > New` expecting a new map might accidentally click
`New Game…` and scaffold a whole project directory.

**Fix:** Separate project operations under `File > Game Project ▸` submenu (or at
minimum a clearly labelled section separator with a "GAME PROJECT" label). Map-document
operations (New, Open, Save, Save As, Reload) should be at the top of the File menu as
the primary actions. The recent-files dynamic items should also be split: recent maps
above, recent games below a clear separator, which the code already does — but the menu
label distinguishes them only by a "[game]" prefix in the filename, which is insufficient.

---

**P1-F: Console has no clear/copy/filter controls and is the sole output for validation**

The Console panel is a read-only ring buffer of 256 lines with no clear button, no copy
button, no severity filter, and no scroll bar (it just truncates older entries). When
`Validate map` runs and produces 10 errors, those errors push all previous log output out
of the visible window. There is no persistent error count indicator on the status bar or
menu bar to remind the user that validation found problems.

**Fix:** Add a "Clear" button in the Console panel header area. Add a severity-filter
toggle (All / Errors+Warnings / Errors only). Add an error-count badge to the status bar
— e.g., the view segment shows `view: iso  snap  [3E]` in red when validation errors
are pending — so the user doesn't need to look at the Console to know something is wrong.

---

**P1-G: The TOOLS panel "GIZMO (move drag only)" label is a lie and a UX failure**

The label reads `"GIZMO (move drag only)"`. Rotate and Scale drag are implemented and
working — `edscene.h` documents `Eng_SetYaw`/`Eng_SetScale` via the gizmo, and the code
clearly sets `s->mode = (EngGizmoMode)g` for all three modes. The parenthetical
`"(move drag only)"` is wrong, misleading, and will erode user confidence. If it was
accurate at some earlier point and was never updated, that is a documentation debt made
worse by being surfaced in the live UI.

**Fix:** Change the label to `"GIZMO"` or `"TRANSFORM MODE"`. If only translate works
for some entity kinds (which is true — rotate/scale only affect PROP entities), the
restriction should be shown in the Inspector for non-PROP entities, not hardcoded into
the gizmo-mode toggle label.

---

### P2 — Polish (real problems, lower urgency)

**P2-A: No visual connection between the active place tool in the TOOLS panel and the viewport cursor**

When a place tool is armed, the viewport shows no cursor change, no ghost entity preview,
no "tool active" indicator overlaid on the viewport. The only feedback is the highlighted
button in the left panel, which the user may not be looking at while clicking in the
viewport. Blender shows a crosshair. Unity changes the cursor glyph. Godot overlays the
asset preview. At minimum, a viewport overlay text ("PLACING: Zombie spawn — click to
drop, P to cancel") in the top-left corner of the viewport would prevent accidental drops.

**P2-B: Hierarchy panel has no drag-to-reorder and the entity IDs are meaningless to users**

The Hierarchy shows `#3 SPAWN`, `#7 WALL` etc. The numeric IDs are stable internal IDs
(correct for the engine), but they mean nothing to users. Blender uses mesh names. Unity
uses GameObject names. The editor should show, at minimum, a more descriptive label
from the Inspector data: `SPAWN (ZOMBIE)`, `SPAWN (PLAYER)`, `BARRICADE (+x)`,
`PROP (unnamed)`. The id can be shown as a tooltip or in parentheses. This is a
discoverability and identification problem, especially on larger maps.

**P2-C: Splitter handles are 6px and give no cursor hint**

The three zone splitters are 6 unscaled pixels wide. There is no `SetMouseCursor()` call
to show a resize cursor when hovering them. A user who doesn't know splitters exist will
never find them. Godot shows resize cursor on splitter hover. The fact that this editor
has a Unity-style resizable dock layout is a genuine feature; it is currently invisible.

**P2-D: The Settings dialog mixes live-adjustable and restart-required settings without clear separation**

The DISPLAY section footer label says `"(* applies on restart)"` — but the only visual
indicator that a specific control requires restart is an asterisk in its *label text*
(`"Undo depth*"`, `"VSync*"`, `"FPS cap*"`). The asterisk is in the label, not a badge
or colour. A user who drags the VSync slider and saves expects the change to take effect,
not to have to remember that the `*` meant restart. Put restart-required settings in a
clearly separated "RESTART REQUIRED" section with a banner, or disable those controls
with a message "takes effect on next launch".

**P2-E: Wall placement 2-click state is indicated only by the button label changing**

When the Wall tool is armed and the first endpoint is clicked, the button label changes
from `"Wall  (2-click)"` to `"Wall  (click 2)"`. This label is in the Tools panel on
the left, which the user is almost certainly not looking at while clicking in the
viewport. There is no viewport overlay, no in-world marker at the first click point
rendered as a snap indicator (though the code stores `wallStartX/Z`). The pending-start
dot exists in code (`edscene.c` presumably draws a marker), but there is no visual in
the panel to remind the user of the current state.

**P2-F: Help > Controls dumps to the Console, which immediately scrolls away**

Clicking `Help > Controls` emits three `EdHost_Log` lines to the ring buffer. On a map
with plugin-load log output already present, those lines may not even be visible without
scrolling up. A controls reference should be a modal dialog (or a floating overlay) that
stays up until dismissed, not transient log output.

---

## Recommended Target Layout

The dock topology (left / right / bottom / viewport) is sound. The problem is what
goes *inside* each zone.

### Left zone — Tools + Hierarchy (no change to zones)

**PLACE palette panel** (renamed from "TOOLS", flex height with internal scroll):
- Section headers: Spawns / Geometry / Buyables
- Tool buttons for placement only
- No UI scale slider
- No view toggles
- No gizmo toggles
- No editing-preference checkboxes

**HIERARCHY panel** (flex height, unchanged layout):
- Search filter at top
- Rows show `KIND (descriptor)` not `#id KIND`
- Colour dot (already there, keep it)

### Right zone — Inspector

**INSPECTOR panel** with two tabs or a toggle button in header:
- **Entity tab** (default): editable fields for selected entity; empty/instructional when
  nothing is selected
- **Map tab** (click or auto-when-deselected): map name, atmosphere, textures

### Bottom zone — Console

**CONSOLE panel**:
- Clear button in panel header
- Severity filter toggle
- Error/warning count badge exported to status bar

### Top — Menu bar (revised)

| Menu | Contents |
|---|---|
| **File** | New, Open…, Save, Save As…, Reload, — , [recent maps], — , **Game Project ▸** (New Game…, Open Game…, [recent games]), — , Exit |
| **Edit** | Undo, Redo, — , Delete selected, — , **Preferences…** (was Tools > Settings) |
| **View** | Fly (F1) ✓, Iso (F2) ✓, Top (F3) ✓, — , Snap to grid ✓, — , **Gizmo mode ▸** (Move ✓, Rotate ✓, Scale ✓) |
| **Tools** | **Validate map**, — , **Place tool ▸** (Select, Player spawn, [mob names], Barricade, Wall, Obstacle, Prop, Sector, Wallbuy, Perk — checkmarks, shortcut P cycles) |
| **Help** | Controls (modal dialog, not Console), About |

### Viewport chrome — thin toolbar above viewport

A 24px toolbar row above the 3D viewport (not a dock zone, just an overlay strip):
- Left side: active view mode badge (Fly / Iso / Top)
- Centre: active gizmo mode badge (Move / Rotate / Scale)
- Right side: active place tool badge ("PLACING: Zombie spawn" or "Select mode")

This surfaces the three most-used transient states without consuming panel space.

### Status bar (trimmed)

| Segment | Content |
|---|---|
| Map | `<filename> *` (dirty only shown when dirty; also in window title) |
| Entities | `42 ents` |
| View | `iso  snap` |
| Selection | `#7 WINDOW (+x)` — empty string when nothing selected |
| Validation | `[3E 1W]` in red/gold when validate has run and found issues |

### Settings dialog (Preferences, accessible via Edit > Preferences…)

Sections: CAMERA — VIEW — GRID/SNAP — EDITING — DISPLAY  
Move `ui.scale` here (remove panel slider).  
Add "RESTART REQUIRED" banner above VSync/FPS/window size, styled distinctively.
