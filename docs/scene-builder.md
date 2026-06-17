# Scene / Map Builder (`src/editor/`)

The in-engine **scene builder** — the tool that authors `.map` files visually. It is a
**sibling application to the game**, not a feature inside it: its own `GameModule`
hosted by `Eng_Run`, linking `libengine.a` and **nothing** from `src/game/`. This is
what makes the project *an engine with a toolkit* rather than *a game with a built-in
editor* — see [engine-layers.md](engine-layers.md) for the layering rationale, and
[engine-usage.md](engine-usage.md) §4 ("Building a second module") for the host pattern.

The editor is a **Unity-style IDE shell**: a top **menu bar** (File / Edit / View /
Tools / Help), resizable **dock zones** (Tools + Hierarchy on the left, Inspector on the
right, Console on the bottom) around a central **3D viewport**, and a **status bar**.
Everything visible is contributed through a **plugin API** — the built-in tools are
themselves first-party plugins, and third-party plugins (compiled-in *or* dynamically
loaded `.so` files in `./plugins`) extend it through the identical surface. See §3.

> **Status: working IDE shell (map *save* now wired).** Builds the `editor` binary: the
> menu/dock/status frame, fly/iso/top cameras in a confined viewport, place / select /
> move entities (translate **+ rotate/scale** gizmo), undo/redo, grid snapping, a
> Hierarchy list with a search filter, an **editable Inspector** (with a map-metadata
> view when nothing is selected), live **inline validation** (red outlines on bad
> entities), **focus-on-selection** (`F`), an **unsaved-changes guard**, a Console, a
> persisted settings dialog (`editor.cfg`), and `MapDoc_Validate` map-checking — plus
> `File ▸ Save` (`MapDoc_Save`) and a dynamic plugin loader. Not yet: game-accurate
> textured rendering in the viewport (entities still draw as proxy boxes — though the
> shared engine draw helpers it needs now exist, see
> [editor-textured-rendering-plan.md](editor-textured-rendering-plan.md)), and place
> tools for every entity kind. See §5.

## 1. What it is (and is not)

- **It edits a `MapDoc`** — the engine-neutral document (`mapdoc.h`), the same one the
  game instantiates at load. The editor never touches game rules; it knows only that a
  map has sectors and entities with stable ids, positions, sizes, and types.
- **Zero game knowledge.** No `src/game/` header, no zombie/perk/weapon concept. A
  custom inspector that understands game-specific entity flavours would come *later*
  via a registration hook (the content-registry pattern), never a direct dependency.
- **One document at a time.** The editor is a single-document tool: one `MapDoc`, one
  `EngMapHistory`, one selection. (Multi-doc/tabs are not a goal.)

## 2. The toolkit it's built on

Everything below ships in `libengine.a` already (Phase C of the engine roadmap):

| Primitive | Role in the editor |
|---|---|
| `mapdoc.h`   | the document (`MapDoc`, stable ids, parse/save) |
| `mapedit.h`  | id-addressed mutators (`Eng_SetPos`, `EngMapEnt_Add/Delete/Find`) + `EngMapHistory` undo/redo |
| `pick.h`     | cursor → world ray (`Eng_PickRayFromScreen`) + ray/AABB hit-test for selection |
| `gizmo.h`    | translate/rotate/scale handle hit-test + drag math; `Eng_GizmoDebugDraw` for handles |
| `debugdraw.h`| selection box + gizmo overlay |
| `gfx.h`      | draw the scene (grid, cubes, wires) |
| `ui.h`       | `Eng_Ui*` theme/text + raygui toolbar styling (shared with the game menus) |
| `cfg.h`      | `editor.cfg` settings persistence (key=value) |
| `app.h`      | `Eng_Run` + the `GameModule` host; `Eng_RequestClose` for `File ▸ Exit` |

`ui.h` is a *partial* `Eng_Ui*` facade (theme + scaled text + a tool button); the shell's
panels and dialogs use it for chrome but still call raygui `Gui*` directly for most
widgets. The IDE frame itself (menu bar, dock layout, splitters, status bar) is the
editor's own `edhost`, not engine UI — completing the `Eng_Ui*` facade so that frame
could move into the engine is the remaining UI work (§5, and step 1 in
[engine-layers.md](engine-layers.md)).

## 3. Architecture

The editor is **three layers + plugins**, all in `src/editor/`:

| File | Role |
|---|---|
| `edscene.{c,h}` | **The document + 3D view.** `EdScene` owns the open `MapDoc`, its `EngMapHistory`, the camera (fly/iso/top), selection, the gizmo drag, placement tools, and the persisted settings. Knows nothing about menus or panels. |
| `edhost.{c,h}`  | **The shell** (the IDE frame). Owns layout (menu bar, dock zones, splitters, status bar), input routing, and the **plugin registration API**. Owns no editing logic — it hosts a viewport rect and calls into the scene. |
| `builtins.c`    | **First-party plugins** — the default IDE (menus, the Tools/Hierarchy/Inspector/Console panels, the Settings dialog + Validate, the status segments), each written against the same `EdHost_*` API a third-party plugin uses. |
| `editor_main.c` | **Wiring.** `main()` loads settings, opens the window, builds the host, registers the built-ins, loads dynamic plugins, runs `Eng_Run`. |
| `edfiledialog.{c,h}` + `vendor/tinyfiledialogs.{c,h}` | **Native file picker** (Open/Save dialogs), editor-local — vendored, compiled into the `editor` target only, never the engine. |

- **Proxies** — `EdScene_RebuildProxies()` walks the `MapDoc` each frame and emits one
  `EdProxy { id, kind, BoundingBox, Color }` per entity: a box derived from the entity's
  position + size, Y taken from its sector's floor (entities inherit Y from their sector,
  per `mapdoc.h`). Proxies are what gets **drawn** and **picked** — the bridge between the
  abstract document and screen-space interaction. Rebuilding every frame keeps the view
  trivially correct after any edit (move/add/delete/undo).
- **The viewport renders to a `RenderTexture`** sized to the central rect and is blitted
  into place, so the 3D scene is confined to the viewport (not the whole window) and
  picking/gizmo math uses **viewport-relative** mouse coords + size — correct regardless
  of how the dock zones are resized around it.

### The edit loop (pick → gizmo → mapedit → history)

This is the load-bearing flow, in `edscene.c`:

1. **Select** — left-click casts a pick ray (`Eng_PickRayFromScreen`), ray-tests every
   proxy AABB (`Eng_PickRayAABB`), nearest hit wins → `selectedId`. (The Hierarchy panel
   selects the same id by clicking a row.)
2. **Grab** — with something selected, `Eng_GizmoHitTest` reports the hovered axis. On
   left-press over a handle, `Eng_GizmoBeginDrag` snapshots the grab; the scene records
   the entity's start position and mints a fresh **coalesce tag**.
3. **Drag** — each frame `Eng_GizmoUpdateDrag` returns the offset *since grab* (drift-
   free), the scene recomputes `pos = startPos + delta`, writes it with `Eng_SetPos`,
   then `EngMapHistory_Commit(tag)`. Because every frame commits under the **same tag**,
   the whole drag collapses into **one undo step**.
4. **Undo/redo** — `Ctrl+Z` / `Ctrl+Y` swap the doc to the previous/next snapshot.

### Plugins (the extensibility seam)

Everything the shell shows is contributed through the `EdHost_Add*` API
(`EdHost_AddMenuItem`, `EdHost_AddPanel`, `EdHost_AddStatusItem`, `EdHost_Log`), plus a
small stable context surface (`EdHost_Doc`, `EdHost_SelectedId`, `EdHost_Select`,
`EdHost_CommitEdit`, …). A plugin is just an `EdRegisterFn` carried in an `EdPluginDesc`,
and reaches the editor **two ways that converge on one API**:

- **Compiled-in** — linked into the `editor` binary and registered directly via
  `EdHost_RegisterBuiltin`. The four built-ins in `builtins.c` are exactly this.
- **Dynamic** — a `.so` dropped in `./plugins`, `dlopen`'d at launch. Each exports
  `const EdPluginDesc *ed_plugin_main(void)`; the loader version-checks `abiVersion`
  against `ED_PLUGIN_ABI` and calls its `registerFn`. The `editor` binary is linked with
  `ENABLE_EXPORTS` (`-rdynamic`) so the plugin's `EdHost_*`/`Eng_*` calls resolve against
  the host's symbol table — a dynamic plugin links *nothing*.

A built-in and a third-party plugin are therefore written identically. In-tree plugins
may include `edscene.h` to drive the document directly; third-party plugins should stay on
the `EdHost_*` accessors (the ABI-stable subset). `src/editor/plugins/example_plugin.c`
is a complete worked example (it adds a `Plugins ▸ Say hello` menu item and a status
segment), built to `build/plugins/example_plugin.so`.

## 4. Running it

```
cmake --build build --target editor editor_example_plugin
build/editor                       # opens the IDE on data/maps/default.map
build/editor data/maps/foo.map     # open a specific map
build/editor --check [map]         # headless: parse, build proxies, print counts, exit
build/editor --shot out.png [map]  # headed: render a frame, screenshot to out.png, quit
```

`--check` is the editor's CI/smoke hook (the analogue of the game's `--validate`): it
loads a map and reports entity counts without opening a window, so the editor stays
buildable-and-loadable in headless environments. It also runs `MapDoc_Validate` and
prints any geometry/integrity issues, exiting non-zero on a parse error or a
validation ERROR — so a broken map fails CI. `--shot` is the *headed* verify hook
(analogue of the game's `--screenshot-*` modes): it settles the UI for ~½ s, captures
one frame, and quits — handy for eyeballing the shell after a change.

Dynamic plugins are loaded from **`./plugins`** relative to the working directory; the
bundled example builds to `build/plugins/example_plugin.so`, so launching from `build/`
(or symlinking `plugins → build/plugins`) picks it up. Missing folder = no plugins, no
error.

### Settings + validation

The **Tools** menu carries two map utilities (a built-in plugin, `maptools`):

- **Settings…** — a modal dialog: camera speeds/sensitivity, default view + FOV + zoom,
  grid spacing/extent, **grid snapping** (toggle + step; snaps both ground placement and
  gizmo drags), barricade auto-spawn, undo depth, and window/vsync/FPS (applied on
  restart). Persisted to **`editor.cfg`** via the engine's `cfg.{c,h}` key=value helper —
  searched from the CWD like the game's `settings.cfg`, loaded before the window opens,
  and saved on demand or at shutdown. `ui.scale` falls back to the display recommendation
  when absent. (`View ▸ Snap to grid` toggles snapping straight from the menu.)
- **Validate map** — runs `MapDoc_Validate` and prints each issue to the **Console**
  panel (errors red, warnings gold). The same check backs `--check`.

`MapDoc_Validate` (engine-side, game-clean) checks sector well-formedness, that placed
entities reference a real sector and (where it makes sense) sit inside it, non-degenerate
walls, valid facings, and that the map has the spawns it needs. Perimeter entities — mob
spawns, windows, wallbuys — are deliberately NOT containment-checked (they live on/outside
the arena edge by design).

### View modes

The editor opens in the **isometric** view. Switch with **F1 / F2 / F3**:

| Key | View | Projection | Navigation |
|-----|------|-----------|------------|
| **F1** | Fly (no-clip) | perspective | hold **RMB** to mouselook, **WASD/Q/E** to fly (Shift = fast) |
| **F2** | Isometric orbit | orthographic | **RMB-drag** rotates the viewpoint, **MMB-drag** pans, **wheel** zooms |
| **F3** | Top-down | orthographic | north-up; **MMB-drag** pans, **wheel** zooms |

All three share one set of look angles (`yaw`/`pitch`) and an orbit `focus`, so
toggling between them keeps you looking at roughly the same place — the fly cam tracks
a focus 20 u ahead, and the ortho views pivot around it. Orthographic projection is
what gives top-down and isometric their flat, distortion-free "blueprint" feel; picking
and the gizmo work unchanged under it because `Eng_PickRayFromScreen`
(`GetScreenToWorldRayEx`) builds a correct ray for an ortho camera, and handle sizing
switches from distance-based (fly) to zoom-based (ortho).

**Editing controls (all views):** **LMB** select or drag a gizmo handle; **1/2/3**
switch gizmo mode — translate drags any entity, **rotate/scale drag PROPs** (the only
kind with a yaw/scale, per `mapedit.h`); **F** focuses the view on the selection;
**Ctrl+Z / Ctrl+Y** undo / redo. Selection/drag is suppressed while a camera button
(RMB/MMB) is held so navigation clicks don't double as edits. Rotate/scale drags
coalesce into one undo step under a drag tag, exactly like translate.

### The IDE frame (menu bar, panels, status bar)

The shell wraps the viewport in a Unity-style frame, all built by the `menus` / `panels` /
`statusbar` / `maptools` built-in plugins:

- **Menu bar** (top): **File** (New, Open… `Ctrl+O`, Save `Ctrl+S`, Save As…, recent
  files, Reload, Exit — see below), **Edit** (Undo/Redo/Delete, enabled-state aware),
  **View** (Fly/Iso/Top with a check on the active one, Snap to grid), **Tools**
  (Settings…, Validate map), **Help** (Controls, About — they print to the Console). Click
  a title to open; hover-switches between open menus; click an item or click away to
  close. While a menu is open the viewport ignores clicks.

#### File menu (open / save / recents)

`Open…` and `Save As…` pop a **native OS file dialog** (filtered to `*.map`), so paths are
chosen with the real system picker rather than a hand-rolled browser. The dialog is behind
a one-file abstraction, `edfiledialog.{c,h}` — `EdFileDialog_Open` / `EdFileDialog_Save` —
implemented with the vendored **tinyfiledialogs** (`src/editor/vendor/`), which shells out
to zenity / kdialog / etc. per platform. Swapping the backend means editing only that one
file. `New` resets to a blank document with a single default 40×40 sector (so the map is
immediately saveable — the grammar has no ungrouped entities), titled `untitled.map`;
`Save` on an untitled map falls through to `Save As…`.

**Recents** are the last 8 opened/saved maps, persisted as `recent.0..7` keys in
`editor.cfg` and shown as live items at the bottom of the File menu. They're contributed
through a small **dynamic-menu** hook the shell grew for exactly this — a menu may register
an `EdMenuDynFn` provider (`EdHost_SetMenuDynamic`) that emits items each frame the menu is
open, drawn after the static items with an automatic separator. (Static menu items are
registered once; recents change at runtime, hence the hook.)
- **Left zone**: **TOOLS** (UI-scale slider, view + gizmo toggles, place-tool buttons, the
  *auto-spawn ZOMBIE* checkbox) over **HIERARCHY** (a scrollable list of every entity by
  `#id kind`, with a **search box** at the top that filters rows by id/kind/mob substring;
  click a row to select — it mirrors the viewport selection).
- **Right zone**: **INSPECTOR** — **editable** fields for the selection: position (all
  kinds), spawn mob, window facing, prop yaw/scale, obstacle size, sector size/heights —
  each wired through `mapedit.h` and pushing one undo step per change. When **nothing** is
  selected it shows a **map-metadata** view instead (map name + atmosphere fog/sky via
  text fields and colour pickers, writing straight into the `MapDoc`).
- **Bottom zone**: **CONSOLE** — log output (plugin loads, save/validate results, Help).
- **Status bar** (bottom): map name + dirty flag, entity count, view mode + snap, and the
  selection — each a registered status segment.

Zones resize by dragging the **splitters** between them and the viewport. Panels stack
within a zone; a panel may request a fixed height (`prefH`) — TOOLS does, so it keeps its
slot while HIERARCHY/CONSOLE flex to fill the rest. Every menu/panel action has a hotkey
equivalent and vice-versa.

**UI scale.** The whole frame's font and layout scale together by one factor, seeded from
the display: ~1.0 at 720p, ~1.5 at 1080p, up to 3.0 on a 4K monitor. Adjust it live with
the **UI** slider in TOOLS or **Ctrl + `=` / `-`** (range 70 %–300 %).

> Chrome is **raygui** styled through the `Eng_Ui*` facade (`ui.h`); raygui `Gui*` is
> called directly for sliders/toggles/checkboxes. The IDE frame itself (menu bar, dock
> layout, splitters) is hand-rolled in `edhost.c`. Completing the `Eng_Ui*` facade — so
> the frame could become an engine-side toolkit — is the remaining UI work (§5;
> [engine-layers.md](engine-layers.md) Open #1).

### Placing spawns & barricades

| Key / action | Effect |
|---|---|
| **P** | Cycle the place tool: *(select)* → **PLAYER spawn** → **ZOMBIE spawn** → **BARRICADE** → … |
| **LMB** (tool armed) | Drop the tool's entity where the cursor ray meets the ground (`Eng_PickRayGroundY`) |
| **O** | Toggle *barricade auto-spawn* — when ON, placing a barricade also drops a paired `ZOMBIE` spawn just outside it (at `pos − facing·5`) |
| **R** | Edit the selection in place: cycle a barricade's facing (`+x→+z→-x→-z`), or flip a spawn between PLAYER and ZOMBIE |
| **X** | Delete the selected entity |

Spawns are colour-coded: **PLAYER = blue**, **mob = red**; barricades (windows) are
gold. A placed barricade auto-faces the arena interior (its normal points to the
origin). All placement/retag/delete actions push one undo step.

This is the generic spawn model in action: spawns carry a free-form mob tag
(`SPAWN MOB <tag>`), so a *new mob is a new tag* — the editor's ZOMBIE tool is just
the common case, not a hardcoded type. See [engine-layers.md](engine-layers.md) and
the engine roadmap for the "primitives not policy" rationale.

> **Saving caveat (important):** the `.map` grammar has **no ungrouped entities** —
> every placed entity must belong to a SECTOR to be written by `MapDoc_Save`. The
> editor therefore assigns each placed entity to the sector under the cursor (falling
> back to sector 0). `mapedit.h`'s `Eng_SetSector` is what does this; anything added
> with `sectorId == -1` would be silently dropped on save.

## 5. Roadmap (this tool)

Small, independently shippable steps; none blocks the game.

1. ~~**IDE shell + plugin API**~~ **Done** — the `edhost` menu bar / dock zones / status
   bar, with `EdHost_Add*` registration and a compiled-in + dynamic (`.so`) plugin loader;
   the default tools ship as first-party plugins (§3).
2. ~~**Tool palette / Hierarchy / Console**~~ **Done** (panels plugin).
3. ~~**Add / delete** entities~~ **Done for spawns + barricades** (P/LMB/X, the barricade
   auto-spawn, retag/aim via R, optional grid snap); **generalise the place picker** to
   the other kinds (walls, obstacles, props, wallbuys, perks) — a menu/palette of kinds.
   Design for the data-driven entity/mob/asset/behaviour catalog behind this is in
   [editor-content-extensibility.md](editor-content-extensibility.md).
4. ~~**Save / Open / New + file picker**~~ **Done** — `File ▸ Save`/`Save As…` (`MapDoc_Save`),
   `Open…`/`New`, a recents list, and the `*` dirty marker; `Open…`/`Save As…` use a native
   OS dialog via `edfiledialog` + tinyfiledialogs (§"File menu").
5. ~~**Editable Inspector**~~ **Done** — the right panel edits position (all kinds),
   spawn mob, window facing, prop yaw/scale, obstacle size, sector size/heights through
   `mapedit.h` (one undo step per change); a **map-metadata** view (name + atmosphere)
   shows when nothing is selected. (Also shipped alongside: a Hierarchy search filter,
   inline validation outlines, `F` focus-on-selection, and an unsaved-changes guard.)
6. ~~**Rotate / scale** drag~~ **Done** — gizmo modes 2/3 drag PROPs via
   `Eng_SetYaw` / `Eng_SetScale` (using the gizmo's `rotateRadians` / per-axis `scale`),
   coalesced under a drag tag like translate.
7. **Real scene rendering** — draw actual sector/wall/prop geometry (optionally share the
   game's look via the content registry) instead of proxy boxes. **Foundation landed:**
   `Eng_DrawTexturedBoxV` / `Eng_DrawTexturedFloorV` now live in the engine (`gfx.{c,h}`),
   lifted out of the game's `render.c` so the editor can reuse them
   ([editor-textured-rendering-plan.md](editor-textured-rendering-plan.md) Phase 1, step 1);
   the editor-side `M` toggle that calls them is the remaining work.
8. **Docking polish** — tabbed panels + drag-to-rearrange + saved layouts (today's zones
   are fixed-position, splitter-resizable).
9. **Packaging** — keep the standalone `editor` binary; later, optionally embed the same
   `GameModule` as an in-game "edit this map" screen (Krunker-style). The code is identical
   either way because it depends on the toolkit, not the game.

## 6. Rules this tool must keep

- **No `src/game/` include, ever** (the whole point — the build links engine only).
- **Edit through `mapedit.h` by stable id**, not by array index — indices shift on
  add/delete; ids don't.
- **Every committed edit is one `EngMapHistory_Commit`**; continuous drags coalesce
  under a tag so undo granularity matches user intent.
- **Add UI through the `EdHost_*` API, not by hacking the frame.** New tools are plugins
  (built-in or `.so`); keep the shell ignorant of any specific tool. Third-party plugins
  stay on the stable `EdHost_*` accessors, not `edscene.h` internals — that boundary is
  what keeps `ED_PLUGIN_ABI` meaningful.
