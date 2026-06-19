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

**3. Walls can't be edited at their vertices.  [High, bigger]**
Sectors got edge handles (`DrawSectorHandles`/`BeginSectorResize` in edscene.c);
walls did not. Only way to move a wall endpoint is the translate gizmo (moves the
whole wall by its center). Inspector exposes only a texture field for walls.
- `src/editor/edscene.c`: sector-handle pattern to mirror (`SectorHandleBoxes`,
  `BeginSectorResize`, `UpdateSectorResize`, `DrawSectorHandles`).
- `src/editor/panel_inspector.c`: wall branch (`k == ENGMAPENT_WALL`) — add
  x1/z1/x2/z2 fields.
- Wall endpoints live on `MapDocWall` (x1,z1,x2,z2); set via the typed pointer
  like `edplace.c` PlaceAt's ED_PLACE_WALL case does.
- This is the natural moment to extract `edgizmo.c` (the deferred split): pull
  UpdateGizmo + sector-resize + new wall-handle code into one geometry-edit unit.

### Tier 2 — medium, mostly cheap (several easier after the split)

**4. Undo spam from sliders / typed inspector edits.  [Medium-High]**
Gizmo drags coalesce via a per-drag tag; inspector slider/color edits commit every
frame with tag 0, so one slider drag = dozens of undo steps.
- `src/editor/panel_inspector.c`: `InspSlider`, the fog sliders + GuiColorPicker in
  the map-properties branch — all call `EdHost_CommitEdit(h)` (tag 0).
- Fix: give an interaction a coalesce tag for its duration. Check whether
  `EdHost_CommitEdit` exposes a tag; if not, the underlying call is
  `EngMapHistory_Commit(&hist, &doc, tag)` (see `EdScene_Commit` / `UpdateGizmo`
  `dragTag` in edscene.c). May need a small host API addition.

**5. Inspector edit-buffer state is fragile function-statics.  [Medium]**
`g_inspFloatEdit[4]` / `g_inspFloatBuf[4]` — only 4 float slots, and the sector
inspector already uses all 4 (size x/z, y low/high). Add one field to any kind and
indices silently collide. Multiple inspectors can't coexist.
- `src/editor/panel_inspector.c`: `INSP_FLOAT_FIELDS`, `g_inspFloat*`, the per-kind
  `InspFloatBox(..., idx)` calls; also `InspTexField`'s own `static lastId`, the
  spawn-mob statics, the map-name statics.
- Fix: a small per-field keyed edit-state struct owned by the panel.

**6. No "frame all" / camera can get lost.  [Medium, small]**
`F` frames the selection only. Open an off-origin map → you stare at empty space.
- `src/editor/edscene.c`: the `KEY_F` handler in `EdScene_UpdateViewport`; camera
  state (`s->focus`, `s->cam`, `s->orthoH`). `EdScene_Open` is where to call a
  fit-on-load.
- Fix: add Home / "frame all" that fits the camera to doc bounds; call on Open.

**7. Numeric fields silently swallow garbage.  [Medium]**
`InspFloatBox` does `atof` on commit → `atof("abc")` = 0 silently zeroes the field.
Validation only shows as a red viewport outline + a console line that scrolls away.
- `src/editor/panel_inspector.c`: `InspFloatBox`.
- Validation source: `MapDoc_Validate` (used in edscene.c draw + `a_validate` in
  edmenus.c). Surface the selected entity's issues inline in the inspector.

**8. Sector handles are X/Z only, primary sector only.  [Medium]**
No vertical (height) handle in the 3D view; floor height is inspector-typing only.
Weak for a multi-floor engine. Same `DrawSectorHandles` cluster as item 3.

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
Items **1** + **2** are done. Next: **4** + **5** (contained in
panel_inspector.c). Tackle **3** (+ the `edgizmo.c` extraction) in its own
dedicated session.
