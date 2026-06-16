# Scene / Map Builder (`src/editor/`)

The in-engine **scene builder** — the tool that authors `.map` files visually. It is a
**sibling application to the game**, not a feature inside it: its own `GameModule`
hosted by `Eng_Run`, linking `libengine.a` and **nothing** from `src/game/`. This is
what makes the project *an engine with a toolkit* rather than *a game with a built-in
editor* — see [engine-layers.md](engine-layers.md) for the layering rationale, and
[engine-usage.md](engine-usage.md) §4 ("Building a second module") for the host pattern.

> **Status: skeleton.** Builds the `editor` binary; loads a map, flies a camera, draws
> entities as boxes, picks to select, and drags the translate gizmo to move the
> selection (proving the whole toolkit stack composes). Not yet: asset rendering, a
> tool palette, add/delete UI, rotate/scale drag, or save. See §5 roadmap.

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
| `app.h`      | `Eng_Run` + the `GameModule` host |

The missing one is **`ui.h`** (the `Eng_Ui*` 2D widget facade) — until it lands the
editor draws its HUD with raw `DrawText`. Building that facade is step 1 in
[engine-layers.md](engine-layers.md); the inspector/tool-palette panels wait on it.

## 3. Architecture

`src/editor/editor_main.c` is the whole skeleton today (one translation unit):

- **`ed` (singleton state)** — the open `MapDoc`, its `EngMapHistory`, the fly camera,
  the current selection id + gizmo mode, the active drag, and a per-frame **proxy list**.
- **Proxies** — `RebuildProxies()` walks the `MapDoc` each frame and emits one
  `EdProxy { id, kind, BoundingBox, Color }` per entity: a box derived from the entity's
  position + size, with Y taken from its sector's floor (entities inherit Y from their
  sector, per `mapdoc.h`). Proxies are what gets **drawn** and **picked** — they're the
  bridge between the abstract document and screen-space interaction. Rebuilding every
  frame keeps the view trivially correct after any edit (move/add/delete/undo).
- **The frame** (`EdFrame`): rebuild proxies → fly cam → mode hotkeys → undo/redo →
  click-select (unless the click lands on a gizmo handle) → gizmo drag.
- **The draw** (`EdDraw`): grid + a filled/wireframe cube per proxy (selection in
  yellow), the debug-draw overlay, then the text HUD.

### The edit loop (pick → gizmo → mapedit → history)

This is the load-bearing flow the skeleton proves end-to-end:

1. **Select** — left-click casts a pick ray (`Eng_PickRayFromScreen`), ray-tests every
   proxy AABB (`Eng_PickRayAABB`), nearest hit wins → `ed.selectedId`.
2. **Grab** — with something selected, `Eng_GizmoHitTest` reports the hovered axis. On
   left-press over a handle, `Eng_GizmoBeginDrag` snapshots the grab; the editor records
   the entity's start position and mints a fresh **coalesce tag**.
3. **Drag** — each frame `Eng_GizmoUpdateDrag` returns the offset *since grab* (drift-
   free), the editor recomputes `pos = startPos + delta` and writes it with `Eng_SetPos`,
   then `EngMapHistory_Commit(tag)`. Because every frame commits under the **same tag**,
   the entire drag collapses into **one undo step**.
4. **Undo/redo** — `Ctrl+Z` / `Ctrl+Y` swap `ed.doc` to the previous/next snapshot.

## 4. Running it

```
cmake --build build --target editor
build/editor                       # opens the tool window on data/maps/default.map
build/editor data/maps/foo.map     # open a specific map
build/editor --check [map]         # headless: parse, build proxies, print counts, exit
```

`--check` is the editor's CI/smoke hook (the analogue of the game's `--validate`): it
loads a map and reports entity counts without opening a window, so the editor stays
buildable-and-loadable in headless environments.

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

**Editing controls (all views):** **LMB** select or drag a translate handle; **1/2/3**
switch gizmo mode (only translate drags for now); **Ctrl+Z / Ctrl+Y** undo / redo.
Selection/drag is suppressed while a camera button (RMB/MMB) is held so navigation
clicks don't double as edits.

## 5. Roadmap (this tool)

Small, independently shippable steps; none blocks the game.

1. **`ui.h` facade**, then an **entity inspector** panel (edit the selected entity's
   fields: position, yaw/scale, sizes) and a **tool palette**.
2. **Add / delete** entities (`EngMapEnt_Add/Delete`) with a placement ray
   (`Eng_PickRayGroundY`) and a kind picker.
3. **Rotate / scale** drag (gizmo already returns `rotateRadians` / `scale`; wire them
   to `Eng_SetYaw` / `Eng_SetScale` / size setters).
4. **Save** (`MapDoc_Save`) + new-map / open-map flow; dirty-state tracking.
5. **Real scene rendering** — draw actual sector/wall/prop geometry (optionally share
   the game's look via the content registry) instead of proxy boxes.
6. **Packaging** — keep the standalone `editor` binary; later, optionally embed the same
   `GameModule` as an in-game "edit this map" screen (Krunker-style). The code is
   identical either way because it depends on the toolkit, not the game.

## 6. Rules this tool must keep

- **No `src/game/` include, ever** (the whole point — the build links engine only).
- **Edit through `mapedit.h` by stable id**, not by array index — indices shift on
  add/delete; ids don't.
- **Every committed edit is one `EngMapHistory_Commit`**; continuous drags coalesce
  under a tag so undo granularity matches user intent.
