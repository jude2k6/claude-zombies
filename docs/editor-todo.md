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

- Marquee/rubber-band multi-select — **DONE**. A left-drag beginning in empty
  space (`PickProxyAt` returns -1) sweeps a screen rectangle; release selects
  every proxy whose box centre projects inside it (`MarqueeSelect`, edscene.c),
  shift = additive. Below `ED_MARQUEE_MIN_PX` travel it falls back to a plain
  empty-click clear, so click-select is unchanged. Overlay drawn in window space
  after the viewport blit (`EdScene_DrawViewport`). Drag paths aren't
  screenshot-testable; verified by build + static render.
- Hierarchy is a flat `#id kind` list — double-click-to-frame DONE (a row
  double-click calls `EdScene_FrameSelected`); grouping + inline rename still open
  (`PanelHierarchy` in edpanels.c). Also: `F` now frames AND zooms the selection
  (was recenter-only) via the shared `FrameBox`/`EdScene_FrameSelected`.
- `SectorAt` silently dumps out-of-bounds placements into sector 0 (now in
  `edplace.c`) → spawn at wrong floor, no warning. PARTLY MITIGATED by item 7:
  the inline inspector ISSUES list (MapDoc_Validate) now flags "outside sector"
  once the entity is selected. A placement-time toast still wants a log path
  threaded into `PlaceAt` (it has `EdScene` but no `EdHost`).
- Play Test hardcodes the game binary name `shooter` — DONE. `EdScene_PlayTest`
  now resolves the binary from the open game's manifest (`binary` key → `id` →
  legacy `shooter`) via `Eng_GetGameRoot` + `EdProject_Read`. New `binary` field
  on `EdProject` (parsed + written, omitted when it would just echo `id`).
- Copy/paste of a SECTOR duplicates its `name` (`EdScene_CopySelection`/`Paste` in
  edscene.c) — verify `MapDoc_Save` tolerates duplicate sector names.
- Settings "Undo depth applies on restart" label is subtly wrong — DONE. Relabeled
  "Undo depth (next open)" (dropped the DISPLAY-section `*`=restart marker, which
  didn't match its real timing in `EngMapHistory_Init`).

## Recommended order for a fresh session
Items **1**–**8** are done (8's vertical handle; its "multi-sector" half remains).
What's left: the remaining Tier 3 tail (below) — all small and independent — plus
the one larger deferred refactor, the `edgizmo.c` extraction (see item 3). The
most-wanted tail item (marquee multi-select) is now done; the open tail is
hierarchy grouping/inline-rename, placement-time out-of-bounds toast, and the
SECTOR copy/paste duplicate-name check.

---

## Fresh critical review — 2026-06-20 (wave 2)

A second teardown after the UX waves landed. Grounded in the current code + live
`--shot` renders (iso/top/`--select 8`). Items above are NOT re-listed; these are
newly found or sharpened. Ranked by value-per-effort; line numbers may drift —
grep the named function.

> **STATUS — all 12 shipped (four parallel agents on disjoint files, integrated +
> built + verified by the main session).** Caveats: **T2-4** landed as a real group
> rotate/scale (per-member start arrays as file statics in edscene.c), not a
> primary-only fallback. **T2-5** converted `InspTexField` only; the spawn-mob /
> map-name buffers stay single-instance statics (noted, low value). **T3-4(b)** (minus
> sign renders as `~`) was confirmed to be an **editor-font glyph gap**, not a
> formatting bug — left for a font fix (out of scope for these files). **T2-3** is
> editor-side: a sector's gizmo Y *drag* is suppressed but the Y handle still draws
> (full visual suppression needs an engine axis-mask — deferred). Follow-on found
> during verification: moving ASSETS to the bottom zone (T1-1) made it the default
> tab, so the registration order was fixed to keep **PALETTE** the default bottom tab.

### Tier 1 — high value, do first

**T1-1. INSPECTOR is starved by ASSETS sharing the right zone — fields go below
the fold.  [Major] [M]**
`RegisterPanels` (`edpanels.c`) registers both INSPECTOR and ASSETS into
`ED_DOCK_RIGHT` with no `prefH`, so `DrawZone` (`edhost.c`) splits the zone 50/50.
The `--select 8` shot proves the cost: an OBSTACLE shows only `pos x` / `pos z` —
its `size x` / `size z` / `height` (the fields you actually came to edit) are
scrolled below the fold, with no scrollbar and no hint they exist. The UX review's
own §9 flagged "ASSETS still sits in the right zone" as the unfinished half of #6.
Fix: move the maps browser OUT of the right zone — either fold it into the bottom
dock as a third tab beside PALETTE/CONSOLE (it's browse-driven, the bottom is its
home), or drop it entirely and lean on `File ▸ Open` + recents (the panel is a
second File-Open, per the review's own §2 verdict). Either way INSPECTOR then owns
the full right-zone height and stops clipping. Cheap and the single biggest daily
papercut.

**T1-2. `EdScene_Init` swallows a hard parse failure and edits a half-built doc.
[Major] [S]**
`EdScene_Init` (`edscene.c`) calls `MapDoc_Parse` and only logs "parsed with
errors (continuing)" when the return is `> 0` — but a return of `0`-with-garbage
or a totally failed parse still leaves the editor live on whatever partial/empty
`MapDoc` came back, and (unlike `EdScene_Open`, which bails on `> 0`) there is no
abort path. A user who opens a corrupt map from the CLI silently starts editing an
empty/partial document and a `Save` will overwrite the original with the wreckage.
Fix: on a fatal parse make `EdScene_Init` start a fresh `untitled.map` (call
`EdScene_New`) and surface a console ERROR, so the on-disk file is never clobbered
by an unintended save. (`EdScene_Open` already has the right shape — mirror it.)

**T1-3. Placement silently dumps out-of-bounds entities into sector 0, no toast.
[Major] [S]**
`SectorAt` (`edplace.c`) returns sector 0 as a fallback when a click lands outside
every sector footprint, so a misplaced spawn/obstacle is silently parented to the
wrong floor — confirmed still open in the existing Tier-3 tail. The prior review
"partly mitigated" it via the inspector ISSUES list, but that only fires AFTER you
reselect the entity. `PlaceAt` has an `EdScene` but no `EdHost`/log path, which is
why the toast was deferred. Fix: have `PlaceAt` set a transient `s->placeWarn`
string (e.g. "placed outside all sectors → sector 0") when `SectorAt` falls back,
and have the `ToolStrip` overlay (`edpanels.c`, already drawing PLACING:) render it
for a couple seconds — no new host plumbing, the warning rides the existing
viewport overlay.

### Tier 2 — medium

**T2-1. Settings + Map Settings modals are fixed-height and overflow at high UI
scale.  [Major] [S]**
`SettingsModal` (`edmenus.c`) hard-codes `ph = 552 * sc` and `MapSettingsModal`
`ph = 500 * sc`; with the default 800px window and a 1080p/4K `ui.scale` (≈1.5–2.x)
the panel is taller than the window. `py` is clamped to `>= 6*sc` at the top but the
bottom (with the Save/Close buttons) runs off-screen, and neither modal scrolls —
so the buttons become unreachable. Fix: clamp `ph` to `area.height - 12*sc` and wrap
the body in a scissored, mouse-wheel-scrolled region (the per-entity inspector
already has this pattern in `PanelInspector` — lift it into a shared helper).

**T2-2. Inspector scroll uses a one-frame-lagged `600*sc` guess, so tall entities
clip on the first frame.  [Minor] [S]**
`PanelInspector` (`panel_inspector.c`) clamps `g_inspScroll` against a hard-coded
`maxScroll = 600 * sc` during draw and only computes the *real* content height
(`realMax`) AFTER drawing, applying it next frame. For a ramp sector (size/heights +
kind + axis + two link rows + ISSUES) the content exceeds 600px, so the last rows
are unreachable until a second frame re-clamps — and the comment even admits "we cap
by a generous estimate instead of a full two-pass render". Fix: do the cheap virtual
measure pass the comment describes (run the same field ladder accumulating `y` with
no draws), or persist last frame's measured height and clamp to it before drawing
(no estimate). The machinery is all there; it's just wired in the wrong order.

**T2-3. Sector still draws a dead gizmo Y-axis now that the height handle owns
vertical.  [Minor] [S]**
`EdScene_GizmoModeApplies` returns true for TRANSLATE on every kind, so a selected
SECTOR shows a full XYZ translate gizmo — but its Y arm does nothing useful
(`Eng_SetPos` only moves x/z; floor height is owned by the dedicated
`DrawSectorHeightHandle`). The prior review explicitly left this as "consider hiding
gizmo-Y for sectors". Two overlapping vertical grabs (gizmo-Y stalk + height handle)
sit on the same selection and confuse which one moves the floor. Fix: pass the
selection kind into the gizmo draw/hit-test and suppress the Y axis for SECTOR
(`UpdateGizmo` / `Eng_GizmoHitTest` in `edscene.c`), leaving X/Z translate + the
height handle as the only vertical control.

**T2-4. Rotate/Scale ignore the multi-selection — they act on the primary only,
silently.  [Minor] [S]**
`UpdateGizmo` (`edscene.c`) snapshots every member's start pos for a rigid group
TRANSLATE, but the ROTATE and SCALE branches call `Eng_SetYaw`/`Eng_SetScale` on
`s->selectedId` alone. Select five props, rotate, and only one turns — with no
feedback that the other four were ignored. Fix: either iterate `selIds` in the
rotate/scale branches (apply the same yaw delta / scale factor to each prop member,
skipping non-props), or — cheaper and honest — when `selCount > 1` and the mode is
rotate/scale, surface a "(multi: primary only)" note on the tool-strip so the
limitation is visible. Document whichever you pick.

**T2-5. `InspTexField` reads/writes surface tex unconditionally — fine today,
fragile as a static.  [Minor] [S]**
`InspTexField` (`panel_inspector.c`) keeps its edit buffer in function-`static`s
(`buf`/`edit`/`lastId`), the exact fragility class the prior review's item 5 fixed
for `InspFloatBox` by moving to the label-keyed slot table. It's only called for
WALL/OBSTACLE (one instance), so no live collision — but the spawn-mob and
map-name fields are the same pattern, and the next inspector field added in a static
will silently share a buffer with a sibling. Fix: fold `InspTexField` + the spawn-mob
+ map-name edit buffers into the same string-keyed `InspFloatSlot`-style table (or a
sibling for char buffers), eliminating the static class entirely. Low urgency, but
it closes the door the review left ajar.

### Tier 3 — tail

**T3-1. PALETTE category column clips its last button ("Perks") at default size.
[Minor] [S]**
In the iso/top shots the `PanelPalette` category column (`edpanels.c`) shows
Select / Geometry / Spawns / Props / Wallbuys but **Perks is cut off** below the
panel — the column is a fixed vertical stack (`by += bh + 2*sc` per row) with no
scroll, the same "taller than the panel, hides its own options" bug the review
killed for the old TOOLS dock, reborn in the category column. Fix: shrink the row
pitch / button height so all 6 categories + Select fit a short bottom zone, or make
the column scroll. Geometry being the default category masks it, but a user looking
for Perks can't see the button.

**T3-2. Console default `prefH=40` is below its own two-button header height.
[Minor] [S]**
`RegisterPanels` gives CONSOLE `prefH=40`; with the Clear + filter buttons at
`18*sc` plus the header that's barely one log line. In the bottom zone CONSOLE is
*tabbed* with PALETTE (so `prefH` is moot when tabbed), but if the bottom zone is
ever un-tabbed (or a plugin adds a third bottom panel, flipping `n>1` logic) the
console becomes a sliver. Harmless now; note it so the tab/stack assumption is
explicit. Fix: bump `prefH` to ~70 and/or assert the bottom zone stays tabbed.

**T3-3. SECTOR copy/paste duplicate-name (carried from the prior tail) — now
verifiable.  [Minor] [S]**
`EdScene_Paste` (`edscene.c`) copies the full `MapDocSector` struct including
`name`, so pasting a sector yields two sectors with the same name — and ramp `LINK`
resolves by name, so a duplicated-name sector pair makes ramp links ambiguous on
save/reparse. The prior tail asked to "verify `MapDoc_Save` tolerates duplicate
names"; the answer is it tolerates the write but the reparse/link is ambiguous. Fix:
on paste, clear or uniquify a pasted sector's name (e.g. append/rebump to `sec<id>`
like `EngMapEnt_Add` does) so every sector name stays unique.

**T3-4. Minor render/text polish.  [Trivial] [S]**
Two cosmetic glitches visible in the shots: (a) the empty-inspector hint "Use Tools
› Map Settings…" renders the `›`/`…` UTF-8 glyphs as `?` and is clipped on the right
(`PanelInspector`, `panel_inspector.c`) — use ASCII (`>` / `...`) or verify the font
has the glyphs; (b) negative numeric values render their minus sign as `~` in the
inspector boxes (`-33` shows as `~33.000`) — a font/glyph gap in `Eng_UiText`/the
editor font, worth a one-line check since every negative coordinate hits it.
