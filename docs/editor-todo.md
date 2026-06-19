# Editor TODO — from the 2026-06-19 critical review

A highly-critical agent reviewed the map editor (`src/editor/`). This file
captures the findings as actionable work, ranked. Each item has file pointers so
a cold session can act without re-deriving context. Line numbers may have drifted
since the review — grep the named function if a number is stale.

## Just done (items 1 + 2)

- **1. Placement palette → its own LEFT dock.** `DrawPlaceTools` now lives in a
  dedicated always-visible `PanelTools` registered in `ED_DOCK_LEFT` above
  HIERARCHY (with its own scroll + scissor, mirroring `PanelAssets`). Removed
  from `PanelAssets`, which is now browse-only (Maps/Models/Textures) and no
  longer hides anything behind the asset filter. (`src/editor/edpanels.c`)
- **2. Rotate/Scale gizmos no longer silent no-op.** New
  `EdScene_GizmoModeApplies(s)` (edscene.c/.h) gates the gizmo: `UpdateGizmo`
  early-returns without drawing when the mode can't act on the selection
  (rotate/scale on a non-prop), and the status bar (`st_view`, edpanels.c)
  appends " (n/a)" so the user sees why no handle appears.

## Just done (commit a51d11b)

Split the two god-files into focused translation units (pure mechanical, no
behaviour change, clean `-Wall -Wextra` build):

- `edscene.c` (1671 → 1115 lines) → split out `edsettings.c`, `edcatalog.c`,
  `edmat.c`, **`edplace.c`** (placement tools). Shared former-statics now in
  `edscene_internal.h` (EdSnap, SectorFloorY, FindProxy, SelSetSingle,
  ED_SECTOR_MIN_SIZE, ED_MARKER).
- `builtins.c` (deleted) → `ed_recents.c`, `edmenus.c`, `panel_inspector.c`,
  `edpanels.c`. Cross-file calls via `builtins_internal.h`; public `EdBuiltin_*`
  entry points stay in `builtins.h`.

DEFERRED on purpose: splitting `edgizmo.c` / sector-resize out of `edscene.c`.
Those are entangled with selection state + the viewport loop; carve that seam
when doing the wall-endpoint work (item 3), designed around real usage.

---

## Ranked remaining work

### Tier 1 — high value, do first

**1. Placement palette → its own persistent LEFT dock.  [DONE]**
Shipped: `PanelTools` in `ED_DOCK_LEFT` above HIERARCHY; Assets is browse-only.

**2. Rotate/Scale gizmos silently no-op on most kinds.  [DONE]**
Shipped: `EdScene_GizmoModeApplies` gates the gizmo draw + status-bar "(n/a)" hint.

**3. Walls can't be edited at their vertices.  [DONE]**
Shipped: wall endpoint editing both ways. Viewport — two endpoint handles
(`SelectedWall`/`WallHandleBoxes`/`BeginWallEdit`/`UpdateWallEdit`/`DrawWallHandles`
in edscene.c, mirroring the sector edge-handle flow; new `s->wallEditing`/`wallVert`
state). Inspector — `x1/z1/x2/z2` fields in the WALL branch (the generic pos x/z
block still moves the whole wall). Verified via `--shot --select <id>`.
DEFERRED: the `edgizmo.c` extraction — a large mechanical move of the interaction
seam with no user-visible payoff, and its drag paths can't be regression-tested by
the screenshot harness. Do it as a focused refactor when the seam next changes.

**Camera fix (found while doing 3/6).** `EdUpdateCamera` was gated behind
`inputAllowed`, so the camera froze at the `Init` pose whenever the mouse wasn't
over the viewport — meaning frame-all (item 6) and off-viewport menu view-switches
silently didn't apply, and `--shot` could never show framing. Split out
`DeriveCamera` (pure derivation from focus/orthoH/view) and run it every frame;
only the mouse-driven mutation stays gated. Also added a `--select <id>` CLI hook
(with `--shot`) that preselects + frames an entity, so CI can verify
selection-dependent UI (inspector, gizmo, edit handles).

### Tier 2 — medium, mostly cheap (several easier after the split)

**4. Undo spam from sliders / typed inspector edits.  [DONE]**
Shipped: new host API `EdHost_CommitEditTagged` + `EdHost_NewEditTag` (over
`EdScene_CommitTagged` / `EdScene_NextTag`, sharing the gizmo's `g_nextTag` well).
The inspector's continuous widgets (prop yaw/scale sliders, fog colour/start/end,
sky colour) commit via `InspCommitCont`, which holds one coalesce tag per drag
(reset to 0 each frame the mouse is up, at the top of `PanelInspector`). Discrete
edits (`InspFloatBox` toggle-out, text fields, toggles) keep tag 0. The two gizmo
tag sites now also call `EdScene_NextTag`.

**5. Inspector edit-buffer state is fragile function-statics.  [DONE]**
Shipped: `InspFloatBox` is now keyed by its field LABEL via `InspFloatSlot`
(string-keyed `InspFloatState` table, 8 slots), replacing the 4-slot index array.
This also fixed a live latent bug — `pos x/z` (old idx 0/1) collided with
obstacle/sector `size x/z` (also idx 0/1) on the same entity. Slots are released
on selection change. NOTE: `InspTexField`, the spawn-mob and map-name statics
were left as-is (single-instance, no collision); fold them in only if a second
inspector instance is ever needed.

**6. No "frame all" / camera can get lost.  [DONE]**
Shipped: `EdScene_FrameAll` fits the camera to the union of all proxy bounds
(footprint-diagonal so it holds at any iso yaw; aspect-corrected; clamped to the
zoom range). `Home` always frames all; `F` frames the selection or, with nothing
selected, frames all (no longer a dead key). `EdScene_Open`/`New` set
`s->framePending`, consumed on the next viewport update once vpW/vpH are known.

**7. Numeric fields silently swallow garbage.  [DONE]**
Shipped: `InspFloatBox` parses with `strtod` and requires the whole buffer to be
numeric — non-numeric input is rejected (value untouched, buffer refreshes next
frame) with a console warning, instead of `atof` zeroing the field. Also surfaces
the selected entity's `MapDoc_Validate` issues inline under an "ISSUES" header in
the inspector (red = error, gold = warn), not just as a scroll-away console line.

**8. Sector handles are X/Z only, primary sector only.  [DONE (vertical handle)]**
Shipped: a floating height handle above the selected sector's footprint centre
(`SectorHeightHandlePos`/`BeginSectorHeight`/`UpdateSectorHeight`/
`DrawSectorHeightHandle` in edscene.c). Dragging it vertically raises/lowers the
floor — FLAT keeps yHigh==yLow, RAMP shifts both edges to preserve rise. The
vertical drag reuses the gizmo Y-axis translate constraint (`s->heightDrag`).
Verified via `--shot --view iso --select <sector-id>`.
STILL OPEN (the "primary sector only" half): handles show only for the primary
selection — multi-sector editing is unchanged. Note: the sector's translate gizmo
still draws a no-op Y axis; consider hiding gizmo-Y for sectors now that the
height handle owns vertical editing.

### Tier 3 — tail (smaller)

- No marquee/rubber-band multi-select (`PickSelection` in edscene.c is click +
  shift-click only).
- Hierarchy is a flat `#id kind` list — no grouping/rename/double-click-to-frame
  (`PanelHierarchy` in edpanels.c).
- `SectorAt` silently dumps out-of-bounds placements into sector 0 (now in
  `edplace.c`) → spawn at wrong floor, no warning.
- Play Test hardcodes the game binary name `shooter`
  (`EdScene_PlayTest` in edscene.c) — should read the project manifest
  (`edproject.h`), since the editor is meant to be game-agnostic.
- Copy/paste of a SECTOR duplicates its `name` (`EdScene_CopySelection`/`Paste` in
  edscene.c) — verify `MapDoc_Save` tolerates duplicate sector names.
- Settings "Undo depth applies on restart" label is subtly wrong — it takes effect
  on next Open (`SettingsModal` in edmenus.c; `EngMapHistory_Init` in edscene.c).

## Recommended order for a fresh session
Items **1**–**8** are done (8's vertical handle; its "multi-sector" half remains).
What's left: the Tier 3 tail (below) — all small and independent — plus the one
larger deferred refactor, the `edgizmo.c` extraction (see item 3). Good next picks
from the tail: Play-Test binary name (game-agnostic goal) and the misleading
undo-depth label are quick; marquee select is the most-wanted bigger one.
