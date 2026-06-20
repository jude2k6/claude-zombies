# Editor UI/UX Review + Redesign — 2026-06-20

> **Status: DESIGN + waves 1–4 SHIPPED.** A critical UX teardown of the map editor
> (`src/editor/`) and a proposed redesign, captured from a 2026-06-20 review session. Both
> implementation waves are done and on `main`: §7 #1–#5 + #8 (wave 1), then #6 + its model-path
> prereq + #7 (wave 2). What's left is future polish (tabbed docking, browser recents,
> drag-to-place; ASSETS maps-browser relocated to bottom-dock tab — done). See
> [§9 Implementation status](#9-implementation-status). This is the *target*; the canonical
> "how it works today" reference stays [scene-builder.md](scene-builder.md), and the broader
> backlog is [editor-feature-ideas.md](editor-feature-ideas.md) (this doc supersedes its §4.x
> layout polish where they overlap). The asset-browser redesign here is the UI half of the
> content-import work in [game-projects.md](game-projects.md) §8 and
> [editor-content-extensibility.md](editor-content-extensibility.md) §6.

---

## Verdict

The editor has the *chrome* of a Unity-style IDE but the *proportions* of a debug overlay.
In every screenshot the 3D viewport — the one surface that matters in a 3D map editor — is
the **smallest region on screen**, boxed into a ~33%-wide centre column while four panels of
mostly-idle chrome eat the other two-thirds. The information architecture is upside down: a
permanent placement palette and a maps-browser squat in prime real estate, the empty-selection
Inspector shows a giant fog colour picker nobody asked for, and the Hierarchy is a redundant
flat list. This is accreted layout, not designed layout, and it needs a viewport-first reset.

The whole redesign is achievable through the existing `EdHost_*` plugin API — the rot is in
*what* gets registered and *where it docks*, not in the extensibility seam, and not in the engine.

---

## 1. The information architecture is wrong

The frame treats all four dock zones as **equal-priority permanent furniture**. They are not.
A 3D editor has one hero surface (the viewport) and a small set of *contextual* surfaces that
should be present when relevant and recede when not. Right now everything is always-on,
always-competing, and the viewport pays the bill.

Good dock IA assigns each zone a job:

- **Left = browse / create** (what's in the scene / what can I add?)
- **Right = inspect** (the selection and its properties)
- **Bottom = browse-assets + diagnostics** (content browser, console)
- **Viewport = the work**, with a thin tool-strip for *modal* state (active tool, view, gizmo).

This editor scrambles those roles: browse-driven (`ASSETS` = maps to open) and inspect-driven
(`INSPECTOR`) are stacked in the *same* right zone; create-driven (`TOOLS` placement) is a
permanent left fixture even though placement is a transient modal activity; the console carves
a bite out of the viewport instead of spanning the window bottom.

### The three complaints (all confirmed)

- **TOOLS palette shouldn't be a left dock — Critical.** `DrawPlaceTools` (`edpanels.c`) is a
  permanent left-dock vertical button stack for a *modal* activity (arm tool → click → done).
  The stack is taller than the panel, so it **scrolls and hides its own options** (in the iso
  screenshot "Barricade" is already clipped and Sector/Props/Wallbuys/Perks are off-screen),
  and it costs ~210px of width full-time. Placement belongs in an asset-browser surface and/or a
  viewport tool-strip, not a permanent palette. See §3.
- **MAP PROPERTIES empty state is useless — Major.** When nothing is selected the Inspector
  shows "MAP PROPERTIES" + a full ~80×80 fog colour picker + a read-only textures dump
  (`panel_inspector.c`). This is the *default* state (every empty click, every map open), and
  map atmosphere is a once-per-map edit handed the most prominent slot. → move to a
  **Map Settings…** modal; the empty Inspector should just say "Nothing selected".
- **ASSETS tab doesn't make sense there — Major.** `PanelAssets` (`edpanels.c`) lists *maps*
  and opens one on click — i.e. a second **File ▸ Open**, mislabeled "assets" and parked in the
  *inspect* zone next to the entity Inspector it has nothing to do with. → delete / fold map-open
  into the start screen + File menu; the *name* ASSETS is reborn as the content browser (§4).

### The ones the complaints didn't name

- **A. Viewport starvation — Critical (the #1 issue).** Default left 210px + right 240px docks
  + a console that bites the centre column = a postage-stamp viewport. Every real tool (Unity,
  Unreal, Godot, Hammer, TrenchBroom, Krunker) gives the viewport the dominant share; this one
  inverts it.
- **B. Console steals from the viewport, not the full width — Major.** `ComputeLayout`
  (`edhost.c`) makes the bottom zone span only the *centre* column, so the console shrinks the
  viewport vertically while the side docks run full-height beside it. A log is the lowest-priority
  surface; it should span the window bottom (under the docks) and default collapsed.
- **C. Menu bar order is wrong — Minor.** Renders File / Edit / View / **Help** / Tools (Help not
  last); "Tools" holds one item (`Validate map`); "Preferences…" hides under Edit.
- **D. Place-mode has no viewport feedback — Major** (once the palette moves). Arming a tool
  changes only a highlighted button in a panel you may have scrolled past — no cursor change, no
  ghost. You can't tell what clicking will spawn.
- **E. No viewport tool-strip — Major.** Active view (iso/top/fly), gizmo mode, snap and grid
  are crammed into one run-on status-bar segment at the window bottom, far from the viewport they
  describe. These are *viewport modes* and belong on the viewport.
- **F. Inspector clips its own fields — Major.** A medium entity (obstacle) overflows the
  fixed-flex Inspector — "height" is cut off with no scrollbar. Fields silently vanishing below
  the fold is a data-integrity hazard.

---

## 2. Per-panel teardown

| Panel | Severity | What's wrong | Fix |
|---|---|---|---|
| **TOOLS** (`DrawPlaceTools`) | Critical | permanent dock for a modal activity; scrolls and hides its own options; ~210px full-time; "Select/move" is a *mode* atop a "placement" list | delete the dock; placement → content browser (§4) + viewport tool-strip (§5) |
| **HIERARCHY** | Major | a flat `#id kind` list — redundant with viewport-click + search; ignores the sector tree the map format already implies | rebuild as a **sector → entities tree** (§6) or cut and fold its unique value into a `Ctrl+F` quick-select |
| **INSPECTOR** (`panel_inspector.c`) | Major | useless MAP PROPERTIES empty state (giant fog picker + texture dump); clips fields with no scrollbar; "Map properties…" button is a deselect hack | empty state = quiet "Nothing selected"; map fields → Map Settings modal; make it scroll |
| **ASSETS** (`PanelAssets`) | Major | a second File ▸ Open mislabeled "assets", in the wrong (inspect) zone; empty grey thumbnails | repurpose as the §4 content browser, or delete + fold map-open into start screen/File |
| **CONSOLE** (`PanelConsole`) | Minor | spans only the centre column (bites the viewport); open by default showing boot spam | full window-bottom width; default collapsed, expand on click / new error |
| **Menu bar** (`edmenus.c`) | Minor | Help not last; lone-item Tools; Preferences misplaced | reorder File/Edit/View/Tools/Help |
| **Status bar** (`edhost.c`) | Minor | carries viewport-mode info (view/gizmo/snap) 720px from the viewport | keep map/dirty/count/selection; relocate view/gizmo/snap to the viewport strip (§5) |
| **Viewport** (`EdScene_DrawViewport`) | Critical | the smallest interactive region on screen; zero overlaid affordances (no tool-strip, no place ghost, no add entry-point) | make it dominant (§7); overlay a tool-strip (§5) |

The one panel doing its job: HIERARCHY's *mechanics* (search, click-to-select, double-click-to-frame)
are fine — it's the flat *structure* that's wrong (§6). And the **plugin/registration architecture**
(`EdHost_Add*`) is sound — every fix below goes through it.

---

## 3. Proposed frame (target layout)

Core principle: **viewport-first; docks are narrow, role-pure, collapsible; modal activities
(place, map-settings) leave the permanent frame.**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ File   Edit   View   Tools   Help                                        │  menu bar
├──────────────┬──────────────────────────────────────────┬───────────────┤
│              │ [Select][+Add ▾] | iso▾  move  snap  grid │               │  ← viewport tool-strip
│  HIERARCHY   │··········································· │  INSPECTOR    │
│  (sector     │                                          │  (selection   │
│   tree, §6)  │                                          │   only;       │
│              │            VIEWPORT  (HERO)              │   scrolls)    │
│  ▾ courtyard │          dominant, ~55–60% width         │               │
│    #8 obst   │                                          │  #8 obstacle  │
│    #12 spawn │       place-ghost + "PLACING: Wall"      │  pos/size/    │
│  ▾ hallway   │                                          │  height …     │
│    #14 wall  │                                          │               │
│    #15 wallb │··········································· │  ─ ISSUES ─   │
├──────────────┴───────────────┬──────────────────────────┴───────────────┤
│ [ Console ] [ Assets ]        │  ← bottom dock: content browser + console (full width, §4)
│  …content browser / log…      │     collapsed by default to a one-line strip
├───────────────────────────────┴───────────────────────────────────────────┤
│ default.map *  | 30 ents | sel #8 obstacle                               │  status bar
└────────────────────────────────────────────────────────────────────────────┘
```

What moves: viewport becomes the hero; TOOLS dock deleted (placement → §4 + §5); left zone =
HIERARCHY (sector tree); right zone = INSPECTOR only (scrolls); bottom zone = content browser +
console (full width, collapsed); map properties → a modal; viewport gets a tool-strip.

---

## 4. The asset / content browser — Unity-faithful

The replacement for the TOOLS palette is a **bottom-docked, two-pane content browser**, modeled
directly on Unity's **Project window**.

### How Unity does it (the model)

Unity has two surfaces for "get a thing into the scene", and the split is the point:

- **The Project window** = the asset catalog. Two columns: a **folder tree** on the left (with a
  pinned **Favorites** section that also holds *saved searches*) and a **thumbnail-tile grid** on
  the right. A **size slider** scales tiles (far-left collapses the grid to a one-column list); a
  **search bar** with `t:`/`l:` type/label tokens; a one/two-column **layout toggle**. Thumbnails
  are **auto-rendered previews**. Primary placement gesture = **drag a tile into the Scene**.
- **The GameObject "+" menu** = the *other* surface, for things that aren't assets — primitives
  (Cube, Plane), lights, cameras. No file to drag, so they're spawned from a menu.

So: **assets are dragged from a browser; primitives are spawned from a menu.** Unreal mirrors
this (Content Browser vs Place Actors). Godot collapses both into one undifferentiated FileSystem
dock — the widely-cited anti-pattern.

> **Layout note (corrects a common misconception):** Unity's Project window lives at the **bottom
> of the screen by default** (tabbed with Console), *not* on the left/right. Hierarchy is left,
> Inspector is right, Project is bottom. The folder tree people picture "on the left" is the left
> *column inside* the bottom-docked Project window — not a left dock of the whole editor. That is
> exactly the structure proposed below.

```
Unity Project window — two-column, bottom-docked
┌──────────────────────────────────────────────────────────────────┐
│ Create ▾    🔍 t:prefab zombie___    ⬡Type▾   ☰/▦         ◀──●──▶ │
├───────────────────────┬──────────────────────────────────────────┤
│ ★ Favorites           │  ┌────┐ ┌────┐ ┌────┐ ┌────┐             │
│   • Enemies (query)   │  │ 🧟 │ │ 🧟 │ │ 📦 │ │ 🛢 │             │  ← rendered-preview grid
│ ▼ Assets              │  └────┘ └────┘ └────┘ └────┘             │
│   ▶ Prefabs           │  Zombie  Runner  Crate   Barrel          │
│   ▶ Models            │                                          │
│   ▶ Materials         │            [ drag a tile → Scene ]       │
└───────────────────────┴──────────────────────────────────────────┘
```

### The recommended browser for THIS editor

Bottom-docked (sibling tab of Console), with its **own internal two-pane split**: a **folder
tree** on the left edge of the panel + a **thumbnail grid** filling the rest.

```
┌───────────┬────────────────────────────────────────┬───────────┐
│ Hierarchy │              Viewport                   │ Inspector │
├───────────┴────────────────────────────────────────┴───────────┤
│ [ Console ] [ Assets ]                                          │  ← panel tabs
├─────────────────────────────────────────────────────────────────┤
│ Create ▾   🔍 jugg______   ⬡ Type ▾   ☰/▦              ◀──●──▶ │  ← create / search / type filter / layout / SIZE
├──────────────────────┬──────────────────────────────────────────┤
│ ★ Favorites          │  Assets ▸ Props          (game ⊕ library) │  ← breadcrumb + overlay origin
│   ★ Recents          │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│   ★ Player spawn     │  │ 🛢   │ │ 📦   │ │ 🪑   │ │ 🗄   │      │  ← thumbnail GRID (GLB previews
│   ─────────────────  │  │ GLB  │ │ GLB  │ │ GLB  │ │ GLB  │      │     via edthumb; wraps to fill width)
│ ▼ Assets             │  └──────┘ └──────┘ └──────┘ └──────┘      │
│   ▸ Geometry  (prim) │   Barrel   Crate    Chair    Locker       │
│   ▸ Spawns    /mobs  │  ┌──────┐ ┌──────┐                        │
│   ▾ Props     /props │  │ 🛋   │ │ 🪟   │      ░ game-local tile  │
│       (4 game)       │  │ GLB  │ │ GLB  │      ▒ library (stock)  │
│       (12 library)   │  └──────┘ └──────┘                        │
│   ▸ Wallbuys  /weap. │   Sofa     Window                         │
│   ▸ Perks     /perks │                                           │
├──────────────────────┴──────────────────────────────────────────┤
│ Props/barrel  ·  library/props/barrel/barrel.glb  ·  Import ⤓    │  ← selection path + import-to-game action
└─────────────────────────────────────────────────────────────────┘
```

**Why bottom, not left/right:** a thumbnail grid wants width — tiles **wrap** left-to-right, so
20 props become 2–3 short rows instead of a 20-tall scroll (the current TOOLS bug). The left/right
zones are already spoken for (Hierarchy / Inspector, both *selection-driven*); the browser is
*browse-driven* and the bottom is the free, correctly-shaped zone. This matches Unity's default.
(Unity only goes vertical via its one-column *list* mode — i.e. it gives up the grid to fit a
narrow side dock; that's the fallback, reachable here via the size slider's far-left/`☰` setting.)

**Why a folder tree, not tabs:** an earlier sketch proposed horizontal category tabs because the
catalog is only ~5 fixed buckets. The tree is the more Unity-faithful and more **scalable** choice:
it nests and scrolls independently of the grid, so when `props/` grows to 40 files or gains
sub-folders (`props/industrial/`, `props/furniture/`) it just expands — no regression to the
scrolling-stack problem. Tabs were a small-catalog shortcut.

**Tree → content folders.** Each node is a real content subdir of the engine overlay
(`Eng_ContentDirs`, game-over-library), unioned across both roots:

```
Assets
├─ Geometry   ← SYNTHETIC (no folder): Wall / Sector / Obstacle / Barricade
│              primitives — flat gesture icons, NOT thumbnails
├─ Spawns     → <game>/mobs/    ⊕ <library>/mobs/     (*.mob)
├─ Props      → <game>/props/   ⊕ <library>/props/    (*.prop)
├─ Wallbuys   → <game>/weapons/ ⊕ <library>/weapons/  (*.weapon)
└─ Perks      → <game>/perks/   ⊕ <library>/perks/    (*.perk)
```

**Affordances → existing code:** search = reuse `ContainsCI` (`edpanels.c`); thumbnails =
`edthumb.c` `EdThumb_Model` (off-screen GLB render); size/layout toggle; `★ Recents` = a
last-placed ring on `EdScene` (the cheap, high-value slice of Favorites); `Import ⤓` on the status
line = the planned copy-on-import surface ([game-projects.md](game-projects.md) §6), which this
layout finally gives a home; per-tile game-vs-library badge from the overlay origin.

**Assets vs primitives — keep them in one browser.** Unity/Unreal split assets (browser) from
primitives (menu) because their primitive set is large and standing (lights, cameras, volumes…).
Here the primitive set is **four items** (Wall, Sector, Obstacle, Barricade) and they are the
*core map-building verbs* — exiling them to a buried "+Add" menu would be hostile. So: one browser,
with **Geometry as the first node**, rendered as **flat gesture-icon tiles** (a 2-point line for
Wall, a drag-rectangle for Sector, a box for Obstacle) — never thumbnails ("a thumbnail of a wall"
is meaningless). The thumbnail-vs-icon look *is* the assets-vs-primitives distinction, expressed
as "real folder vs synthetic folder" inside one coherent surface.

**The one real plumbing blocker:** the catalog defs `EdMobDef`/`EdPropDef` (`edscene.h:62-78`)
carry only `id`/`name`/`tint` — **no model path** (deliberate: "the game owns the model — the
seam"). `EdThumb_Model` needs a `.glb` path, so the folder scan (`EdScene_ScanMobs` & siblings)
must be extended to read and stash each def's model path before any asset tile can show a real
thumbnail. That's the single new piece of wiring; `edthumb`/`edassets` are otherwise ready.

**Handoff to placement (out of scope for the browser):** clicking a tile sets exactly what
`DrawPlaceTools` sets today — `s->placeTool` + the relevant `placeMobId`/`placePropId`/
`placeWeaponId`/`placePerkId` — so the downstream placement path (ghosts, click gestures, snapping
in `edplace.c`) is unchanged; the browser is a nicer front-end for arming the same tool state. The
one upgrade worth making later is **drag-from-tile-into-viewport** (Unity's primary gesture)
alongside click-to-arm.

---

## 5. Viewport tool-strip

A thin overlay at the viewport top (new), replacing the run-on status-bar segment:

```
[Select] [+ Add ▾] | view▾  gizmo-mode  snap  grid       PLACING: Wall (click 1 of 2)
```

`[+ Add ▾]` is a quick flyout mirror of the browser's tree (Spawns/Geometry/Props/…) for fast
arming without the bottom panel open; `view/gizmo/snap/grid` are the viewport modes moved off the
status bar to where they apply; the `PLACING: …` badge + a ground ghost fix the no-feedback gap
(§1-D). This wants a new `EdHost_AddViewportOverlay`-style hook (see
[editor-feature-ideas.md](editor-feature-ideas.md) §6.3) so the strip is contributed through the
plugin API rather than hacked into the frame.

---

## 6. The Hierarchy — make it a sector tree (or cut it)

**Purpose problem:** as a flat `#id kind` list the Hierarchy is the weakest panel — it overlaps
almost entirely with viewport-click + search. Its only unique value flat is selecting things you
*can't* click (overlapping / hidden / off-screen / tiny entities), search/filter-to-select, and a
manifest of map contents. Thin — not enough to justify a permanent dock.

**The fix:** this map format already has a tree the panel is ignoring. Every entity belongs to a
**sector** (the `.map` grammar has no ungrouped entities — `sectorId` is mandatory on every
entity). So the natural, load-bearing Hierarchy is **sector → contained entities**:

```
▾ Sector "courtyard"  (#1)
    #8  obstacle  4×4
    #9  prop      barrel
    #12 spawn     ZOMBIE
▾ Sector "hallway"  (#2)
    #14 wall
    #15 wallbuy   MP5
▸ Sector "spawn_room"  (#3)        ← collapsed
```

That version has real jobs the flat list can't do: navigate the map by room (collapse sectors
you're not in); catch placement bugs ("why is this spawn under sector 0?" — ties to the
out-of-bounds → sector-0 dump the first review flagged); per-sector select/frame/hide; and it
*mirrors the file*, so it's a real scene graph, not a redundant entity dump. Also add the kind
**descriptor** (`SPAWN (ZOMBIE)`, `obstacle 4×4`) instead of `#12 spawn`. The grouping data
(`sectorId`) is already on every entity, so this is cheap.

**Alternative — cut it:** if the sector tree isn't wanted, delete the panel and fold its unique
value (search + select-hidden) into a viewport `Ctrl+F` quick-select overlay, reclaiming the left
dock. **Recommendation: build the sector tree** — it's genuinely useful for multi-room and
multi-floor maps and the grouping is nearly free.

---

## 7. Prioritized fix list

| # | Fix | Severity | Effort | File(s) |
|---|-----|----------|--------|---------|
| 1 | Rebalance zones so the viewport dominates (≥55% width) | Critical | M | `edhost.c` (`EdHost_Create` defaults, `ComputeLayout`) |
| 2 | Console spans full window bottom, not the centre column | Major | S | `edhost.c` (`ComputeLayout` bottom rect) |
| 3 | Remove MAP PROPERTIES empty state → Map Settings… modal; quiet "Nothing selected" | Major | M | `panel_inspector.c`, `edmenus.c` |
| 4 | Make the Inspector scroll (stop clipping fields) | Major | S | `panel_inspector.c` |
| 5 | Hierarchy → sector tree + kind descriptors | Major | M | `edpanels.c` (`PanelHierarchy`) |
| 6 | Content browser (bottom dock: tree + thumbnail grid) replacing the TOOLS palette | Critical | L | `edpanels.c` (`PanelAssets`/`DrawPlaceTools`), `edassets.{c,h}`, `edthumb.{c,h}`, `edscene.{c,h}` (model path on defs) |
| 7 | Viewport tool-strip + `PLACING:` ghost; relocate view/gizmo/snap off the status bar | Major | M | `edhost.c` / `EdScene_DrawViewport`; new `EdHost_AddViewportOverlay` |
| 8 | Menu order File/Edit/View/Tools/Help; tidy lone Tools item + Preferences | Minor | S | `edmenus.c` |

**Order:** do **1–4 first** (highest impact-per-effort; they answer two of the three complaints
with S/M work). **6 + 7** are the structural redesign (place-as-browse + viewport tool-strip) —
the most valuable and the heaviest. **5, 8** ride along.

---

## 8. Rules the redesign keeps

- **Through the plugin API, not the frame.** Every panel/strip/overlay is registered via
  `EdHost_Add*`; the one new seam is a viewport-overlay hook (§5). No engine change, no game header.
- **No `games/shooter/src/` include.** Thumbnails resolve a model *path* the editor reads itself
  (`Eng_LoadModel` / `edthumb`); the def's `behaviour`/stats stay game-only, uninspected.
- **The seam stays where it is.** The content browser drives the *same* `placeTool` state the old
  palette did; it's a front-end swap, not a new editing path.

---

## 9. Implementation status

First wave (2026-06-20) — parallel agents on disjoint files, integrated + verified by the main
session:

| # | Fix | Files | Status |
|---|-----|-------|--------|
| 1 | Viewport-dominant zone rebalance (left 210→160, right 240→180) | `edhost.c` | ✅ done — dock bases trimmed; viewport dominates at fullscreen. At low-res + high UI scale (`ui.scale≈2.09`) the docks still read wide; full dominance lands when #6 removes the TOOLS palette. |
| 2 | Console full window-width, default short (bottomH 150→80) | `edhost.c` | ✅ done — bottom zone now spans 0..W under the side docks; splitters adjusted |
| 3 | MAP PROPERTIES → Map Settings… modal; empty Inspector = "Nothing selected" | `panel_inspector.c`, `edmenus.c` | ✅ done — body factored to `PanelInspector_DrawMapProps`; modal under **Tools ▸ Map Settings…** (also gives the lone Tools menu a second item, #8) |
| 4 | Inspector scrolls (stop clipping fields) | `panel_inspector.c` | ✅ done — mouse-wheel scroll (`g_inspScroll`), reset on selection change. Pane is still short while ASSETS shares the right zone (resolved by #6) |
| 5 | Hierarchy → sector tree + kind descriptors | `edpanels.c` | ✅ done — sectors are collapsible parents; children indented with descriptors (`obstacle 4×4`, `wall [door]`, `PLAYER`) |
| 8 | Menu order File/Edit/View/Tools/Help | `edmenus.c` | ✅ done — Help is now last; Tools holds Map Settings + Validate |
| 6 | Content browser (bottom dock: category column + thumbnail/icon grid) replacing the TOOLS palette | `edpanels.c`, `edthumb`, `edscene`, `edcatalog` | ✅ done — `PanelPalette`: a category column (Select / Geometry / Spawns / Props / Wallbuys / Perks) + a wrapping tile grid with a search box. Asset kinds (mobs/props/wallbuys/perks) show real GLB thumbnails; Geometry shows procedural gesture glyphs. TOOLS dropped from the left dock (HIERARCHY now owns it); browser lives in the bottom dock over a short CONSOLE. Clicking a tile arms the same `placeTool` state. |
| 6-prereq | Model path on `EdMobDef`/`EdPropDef` (thumbnail blocker) | `edscene.h`, `edcatalog.c` | ✅ done — defs carry a `model` field (the bare filename, resolved like the game via `Eng_LoadModel`→`models/`). `EdThumb_Model` renders it. |
| 7 | Viewport tool-strip + `PLACING:` ghost; new `EdHost_AddViewportOverlay` | `edhost.{c,h}`, `edpanels.c`, `edscene.c` | ✅ done — host reserves a band atop the viewport + a `EdHost_AddViewportOverlay` hook; `ToolStrip` draws `[Select] [view] [gizmo] [snap] [grid]` (clickable) + a right-aligned `PLACING: …` cue; view/gizmo/snap/grid moved off the status bar. `EdScene_DrawViewport` draws a snapped ground ghost (+ wall rubber-band) when a tool is armed. |

Wave-1 (2026-06-20) — **#1, #2, #3, #4, #5, #8 shipped.** Three parallel worktree agents on
disjoint file sets, integrated + built + screenshot-verified by the main session: **A** =
`edhost.c` (#1+#2); **B** = `panel_inspector.c` + `edmenus.c` (#3+#4+#8, grouped because the Map
Settings modal spans both); **C** = `edpanels.c` (#5). (Worktree builds stalled re-cloning raylib
via FetchContent + a Bash-permission prompt, so the main session pulled the disjoint edits into the
main tree and built once where raylib was cached.)

Wave-2 (2026-06-20) — **#6 (+ prereq) and #7 shipped**, done in the main session (not
parallelized — they're entangled across `edpanels.c`/`edscene.c`/`edhost.c` and #6 needs the model
-path prereq first), built + screenshot-verified incrementally.

Wave-3 (later pass) — **dock-zone collapse + PALETTE/CONSOLE tabs shipped.** Clicking a zone's
splitter collapses it to a thin rail (with an expand arrow; drag the rail back out to restore), so
the viewport can take the whole frame; the short bottom zone now shows PALETTE and CONSOLE as
**tabs** (one full-height at a time) instead of the clipped stack. Both are in `edhost.c`
(`zoneCollapsed` / `zoneTabbed`, opt-in per zone so the tall side zones still stack). The same pass
also expanded the menus (View: Frame selection/all, Material mode; Tools: Map statistics, Rescan
content; Help: Controls as a modal).

Wave-4 (later pass) — **content-browser Recents shipped.** A `Recent` category (first in the
palette column, hidden until something is used) shows a small MRU ring of recently-armed
placeables, one click away regardless of category. Backed by an `EdScene` ring
(`recents[]`/`EdScene_PushRecent`, pushed from `PaletteArm`), resolved back to live catalog defs
each frame (so thumbnails/active state stay correct, stale entries drop), and **persisted in
editor.cfg** (`place.recent.*`, via the existing `EdScene_PutSettingKeys`/`LoadSettings` path).
Verified by a ring-logic link-test (order/dedup/cap/geometry/none) + editor `--check`.

Remaining future polish (not blocking): favourites/saved-searches beyond Recents;
drag-from-tile-into-viewport placement; per-panel **drag-to-rearrange between zones + saved
layouts** (collapse/tab state is runtime-only today). **ASSETS relocated** (the §2 "repurpose/relocate" is now done):
`PanelAssets` is registered as `ED_DOCK_BOTTOM` in `RegisterPanels` (`edpanels.c`) and appears
as a tab beside PALETTE and CONSOLE — INSPECTOR now owns the full right zone.
