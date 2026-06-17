# Editor Textured Rendering Plan

Today the editor viewport draws every map entity as a flat-shaded proxy
box.  The maintainer wants an optional "looks like the game" rendering mode:
real sector floors, textured walls, properly-sized obstacles, prop geometry.
The hard constraint is that the editor links **only** `libengine.a` and must
never include a `src/game/` header.  This plan covers what exists, the
realistic options, a recommendation, and a phased delivery path.

---

## 1. What already exists on the engine side

### 1.1 Engine primitives the editor can use today

| API | Location | What it gives |
|-----|----------|---------------|
| `Eng_GfxBeginQuads / Eng_GfxTexCoord / Eng_GfxVertex / Eng_GfxEndQuads` | `src/engine/gfx.{c,h}` | Immediate-mode textured quads via rlgl |
| `Eng_GfxDrawCubeV / Eng_GfxDrawCubeWiresV / Eng_GfxDrawPlane` | `gfx.h` | Untextured primitives the editor already uses |
| `Eng_GfxDrawModel / Eng_GfxDrawModelEx` | `gfx.h` | Draw any loaded `Model` with optional transform |
| `Eng_LoadModel / Eng_LoadTexture / Eng_LoadTextureByName` | `src/engine/content.{c,h}` | Load `.glb` / textures via the engine registry + path probing |
| `Eng_ModelGet / Eng_TextureGet` | `content.h` | Unwrap a handle back to the underlying raylib value |
| `Anim_Load / Anim_Draw` | `src/engine/anim.{c,h}` | Load and draw skinned glTF/GLB models with animation |
| `Eng_RenderLoad / Eng_RenderBeginWorld / Eng_RenderEndWorld / Eng_RenderSetLighting` | `src/engine/eng_render.{c,h}` | World-shader bookends (fog + sun + ambient uniforms) |
| `Eng_RenderWorldShaderLoaded / Eng_RenderWorldShader` | `eng_render.h` | Guard + raw shader handle for stamping materials |

Everything listed is already part of `libengine.a`.  The editor can call all
of it without touching a game header.

### 1.2 What lives only in the game

| Concern | Location | Why it is game-side |
|---------|----------|---------------------|
| `DrawTexturedBox` / `DrawTexturedFloor` | `src/game/render.c` | Static helpers; reference `Assets_ResolveTexture`, `g_world`, `BeginTileVariation` — all game globals |
| `Assets_ResolveTexture` / `textures[]` / `textureLoaded[]` | `src/game/assets.{c,h}` | Boot-slot texture registry; knows about `TEX_FLOOR`, `TEX_WALL_EXT`, etc. |
| `propModels[]` / `propModelLoaded[]` | `src/game/assets.h` | Per-kind model array keyed by `PropId` (a game enum) |
| `MapDocTextures` → actual GL textures | `level.c` | Instantiation step that maps texture *names* in the .map to loaded handles |
| `DrawArena` / `DrawObstacles` / `DrawInteriorWalls` / `DrawFloors` | `render.c` | High-level draw functions; depend on `g_world`, `assets.h`, lighting state |
| Lighting state (`fogStart`, `sunColor`, `ambientColor`, …) | `assets.h` (externs) | Game-owned per-frame state, though `EngLighting` in `eng_render.h` is engine-owned |

The engine's `content.h` is already enough to **load** models and textures by
path.  The **naming convention** (which path maps to which surface, which .glb
maps to which prop kind) is the only thing that is game-knowledge.

---

## 2. Options

### Option A — Content-registry / draw-callback hook

The editor exposes a per-kind draw callback that the host (or a thin "game
bridge" shared object loaded as a plugin) can register.  The editor calls:

```c
if (g_drawCallbacks[proxy->kind])
    g_drawCallbacks[proxy->kind](h, proxy, cam);
else
    DrawProxyBox(proxy);   // current fallback
```

The game ships a `libgame_edbridge.so` (a separate, optional dynamic library)
that registers these callbacks.  The bridge links both `libengine.a` AND the
game's `assets.o` and `render.o`, so it can call `DrawArena` / `DrawObstacles`
/ `DrawProps` directly without touching the editor binary.

**Seam implications**:
- Clean separation: the editor binary never links game code.
- Requires the bridge to be built alongside the game.
- The editor's plugin API (`EdPluginDesc`) is already a clean seam for this;
  the bridge just needs `dlopen` access and a dependency on the game's objects.
- Adds build complexity; the bridge re-links portions of game code.

**Best for**: full "looks identical to the game" rendering while keeping the
editor binary completely free of game code.

### Option B — Promote shared scene rendering into the engine

Move the generic surface-drawing logic (`DrawTexturedBox`, `DrawTexturedFloor`,
tiled UV math) into the engine as `Eng_DrawTexturedBox` / `Eng_DrawTexturedFloor`
/ `Eng_DrawSectorGeom`.  The game calls these (with its resolved texture
handles); the editor calls them too (with its own texture handles loaded from
the .map's `TEXTURES` block).

**Seam implications**:
- The most natural long-term home: the engine already owns `Eng_GfxBeginQuads`
  and the immediate-mode helpers that `DrawTexturedBox` is built on.
- `TILE_SIZE` and the UV formula go into a shared engine constant.
- The engine does NOT know about `TEX_FLOOR`, `TEX_WALL_EXT`, etc. — those
  TextureId enums stay game-side.  The engine function takes a raw `Texture2D*`
  (or `EngTexture` handle).
- The editor is responsible for loading textures from the `.map` TEXTURES block
  and passing them in.
- The game's existing call sites (`DrawTexturedBox(…, tid, …)`) grow a thin
  shim that resolves `tid → Texture2D*` before delegating to the engine function.

**Best for**: the cheapest first step with no plugin infrastructure needed.

### Option C — Editor loads map-referenced assets via the engine content API

The editor reads the `MapDocTextures` block from the parsed `MapDoc` (e.g.
`doc.textures.floor`, `doc.textures.wall_ext`) and calls `Eng_LoadTextureByName`
to load each one.  It then draws sector floors, wall boxes, and obstacle boxes
using `Eng_GfxBeginQuads` (same technique as `DrawTexturedBox` in the game)
with those textures.  For `.glb` props, the editor calls `Eng_LoadModel` with
the prop's `name` field, caches the handles, and calls `Eng_GfxDrawModel`.

This is self-contained (no plugin/bridge needed) and fully within the
engine-only constraint.  The naming convention (which path to load for a prop
name) is the only part that requires knowledge currently encoded in the game's
`assets.c`.

**Seam implications**:
- The editor takes on asset-loading responsibility for a subset of game content.
- Prop model paths (`data/models/<name>.glb`) and texture paths
  (`data/textures/<name>.png`) follow a simple convention the editor can
  replicate without importing any game header.
- No changes to the engine or game needed; the editor is self-contained.
- Doesn't call `DrawArena` / game shaders; needs its own tiled UV emitter
  (copy of the technique, or promoted to engine as in Option B).

---

## 3. Recommendation

**Phase 1 (cheap, self-contained, high-value): Option C + Option B combined.**

1. Promote `DrawTexturedBox` and `DrawTexturedFloor` into the engine as:
   - `Eng_DrawTexturedBoxV(Vector3 centre, Vector3 size, Texture2D *tex,
     float tileSize, Color tint, Color fallback)` in `gfx.{c,h}`.
   - `Eng_DrawTexturedFloorV(Vector3 centre, float sx, float sz, Texture2D *tex,
     float tileSize, Color tint, Color fallback)` in `gfx.{c,h}`.
   - These replicate the `TexQuad` + `Eng_GfxBeginQuads` pattern from
     `render.c` in ~80 lines; they take a raw `Texture2D*` so neither the
     engine nor the editor ever includes `assets.h`.

2. In the editor, add an optional "material mode" toggle (button in the TOOLS
   panel; keyboard shortcut `M`).  When active, `EdScene_DrawViewport` draws:
   - **Sector floors**: `Eng_DrawTexturedFloorV` with the texture named in
     `doc.textures.floor`, loaded on demand via `Eng_LoadTextureByName`.
     Fallback to a flat colour when the texture is missing.
   - **Walls / obstacles**: `Eng_DrawTexturedBoxV` with the appropriate surface
     texture, sized from the proxy bounding box (already correct).
   - **Props**: `Eng_LoadModel(doc.props[i].name + ".glb")` → `Eng_GfxDrawModelEx`.
     Fallback to the existing proxy box.
   - Wire the world shader via `Eng_RenderBeginWorld()` / `Eng_RenderEndWorld()`
     with a fixed neutral-outdoor lighting preset so surfaces receive the same
     fog + sun shader the game uses (improves fidelity without needing game
     lighting state).

3. The game's `render.c` call sites are updated to call `Eng_DrawTexturedBoxV`
   instead of the local helper — one trivial wrapper that resolves `TextureId`
   and forwards.

**Phase 2: Option A (plugin bridge) for full parity.**

Once Phase 1 is proven, a `libgame_edbridge.so` can be written that registers
draw callbacks for entity kinds the game cares about (perk machines, wallbuys,
mystery box).  These callbacks call the game's actual draw functions.  The
editor binary never changes; only the optional plugin changes.

---

## 4. Phase 1 prototype assessment

Phase 1 is **low-risk and seam-clean**:

- The only engine change is adding two helper functions to `gfx.{c,h}` — no new
  concepts, just lifting existing immediate-mode code.
- The editor loads textures via `Eng_LoadTextureByName` (already available) and
  unloads them in `EdScene_Shutdown` via `Eng_ContentFlush`.
- The world shader call (`Eng_RenderBeginWorld`) is already tested by the game;
  the editor adds `Eng_RenderLoad()` at startup and `Eng_RenderUnloadPostFX()`
  at shutdown.
- The current proxy-box path is not removed; it remains the default and the
  textured mode is additive behind the `M` toggle.

## 5. Status — Phase 1 step 1 + 3 landed

The shared engine helpers now exist:

```c
void Eng_DrawTexturedBoxV(Vector3 centre, Vector3 size, Texture2D *tex,
                          float tileSize, Color tint, Color fallback);
void Eng_DrawTexturedFloorV(Vector3 centre, float sizeX, float sizeZ,
                            Texture2D *tex, float tileSize, Color tint, Color fallback);
```

They were **lifted out of the game's `render.c`** (the old static `DrawTexturedBoxTex` /
`DrawTexturedFloorTex` + `TexQuad`) into `src/engine/gfx.{c,h}`, taking an explicit
`tileSize` and a raw `Texture2D*` so the engine stays game-clean (no `assets.h`, no
`TextureId`). The game's `render.c` helpers are now thin shims that pass its own
`TILE_SIZE` (4.0) through to the engine functions — a pure refactor that renders
identically (verified via `--screenshot-map`). A `NULL` texture falls back to a flat
cube / plane.

**Remaining (Phase 1 step 2):** the editor-side `M`-toggle "material mode" in
`edscene.c` that loads map textures via `Eng_LoadTextureByName` and calls these helpers
instead of the proxy boxes. Phase 2 (the `libgame_edbridge.so` plugin bridge) is
unchanged and still future work.
