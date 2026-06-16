# Using the Engine (`libengine.a`)

How to build a game — or a second one (the map editor) — on top of this
project's engine. The engine is the reusable runtime in **`src/engine/`**,
compiled into a static **`libengine.a`**; the game in `src/game/` is just one
*module* that links it. This guide is the contract for writing another.

For *why* the split exists and how it was reached, see
[engine-game-separation.md](engine-game-separation.md). This file is the
*how-to-use* reference.

---

## 1. The one rule

**Engine code never includes a game header.** `src/engine/` may include raylib,
rlgl (engine-only), and other engine headers — never `types.h`, `weapons.h`,
`game.h`, etc. This is enforced:

```bash
bash scripts/check-seam.sh        # CMake runs this as the `seam-check` target
```

The dependency arrow points one way: **the game calls into engine services; the
engine only calls *down* into the game through the `GameModule` callbacks.** If
you need the engine to know something game-specific, you've found a place to
invert (push an event / pass a parameter / register a callback), not to add an
include.

`rlgl.h` is included **only** inside `src/engine/`. Game code that needs
low-level GL goes through the `Eng_Gfx*` facade (§4).

---

## 2. Bootstrapping: `main()` + the `GameModule`

The engine owns `main()`'s body — window, GL/UI/audio init, the frame loop,
timing, and the `BeginDrawing`/`EndDrawing` bookends. Your `main()` is ~10
lines: hand the engine a config and a vtable of callbacks.

```c
#include "app.h"

int main(int argc, char **argv) {
    EngConfig cfg = { .w = 1280, .h = 720, .title = "My Game" };
    Eng_Run(&cfg, MyGame_Module());   // returns when the window closes
    return 0;
}
```

The vtable (`src/engine/app.h`):

```c
typedef struct {
    void (*init)    (void);                   // load content, build the world
    void (*frame)   (float dt, int w, int h); // per-frame: input, prediction, camera, visuals (no GL begin/end)
    void (*fixed)   (float dt);               // fixed-step authoritative sim (dt == ENG_FIXED_DT)
    void (*draw)    (int w, int h);           // issue draws; engine owns Begin/EndDrawing
    void (*shutdown)(void);                   // free content
} GameModule;
```

Any callback may be `NULL`. `MyGame_Module()` just returns a `GameModule` literal
with your function pointers.

### Timestep model (important)

Per rendered frame the engine runs this loop internally (inside `Eng_Run`) —
there's no game-facing call here; you just supply the callbacks it invokes:

```
frame(realDt)          // sample input, advance local prediction, build camera, visual state
accumulator += realDt  // clamped to 0.25 s to avoid catch-up spirals
while (accumulator >= ENG_FIXED_DT)   fixed(ENG_FIXED_DT)   // drain in constant steps
draw()                 // inside engine's BeginDrawing/EndDrawing
```

- Put **authoritative simulation** (AI, physics, rules, anything that must be
  deterministic / frame-rate independent / consistent across the network) in
  **`fixed`**. It always gets `ENG_FIXED_DT` (1/60 s).
- Put **per-frame** work (input sampling, local-player prediction so the camera
  stays smooth, camera assembly, bob/shake/post-FX ramps) in **`frame`**.
- `frame` runs *before* the fixed drain each rendered frame, so input you sample
  in `frame` is consumed by that same frame's `fixed` steps.
- `draw` must not call `BeginDrawing`/`EndDrawing` — the engine wraps it.

---

## 3. Subsystems & their APIs

All engine entry points are prefixed `Eng_*` (newer subsystems) or keep their
historical module prefix (`Audio_*`, `Net_*`, `Pad_*`, `Anim_*`, `Fx_*`,
`Particles_*`, `Decals_*`, `MapDoc_*`). None take or return a game type.

### app — `app.h`
`Eng_Run(cfg, module)`, `GameModule`, `ENG_FIXED_DT`. `EngConfig` is
`{w, h, title, vsync, msaa4x, resizable, fullscreen, fpsCap}` — the flags map to
raylib's `SetConfigFlags`/`SetTargetFPS` before window creation (a zero-init config
is windowed / no-vsync / no-MSAA / fixed-size / uncapped, so set what you want).

### stats — `stats.h` (per-frame instrumentation)
Frame timing, the fixed-step count drained this frame, and a draw tally — driven by
the engine each frame; read the getters anywhere (debug HUD, devtools).

```c
float ft = Eng_StatsFrameTimeMs();   float fps = Eng_StatsFps();
int   fs = Eng_StatsFixedSteps();    // fixed sim steps last frame (spot sim spikes)
int   dc = Eng_StatsDrawCalls();     // engine-facade 3D draws last frame
uint64_t n = Eng_StatsFrameCount();
```
The draw tally counts `Eng_Gfx*` 3D draw *submissions* (not raw GL draw calls — rlgl
batches primitives), so it's the signal for per-entity draw growth, not 2D HUD draws.

### gfx — `gfx.h` (the GL facade)
The only place outside `src/engine/` is forbidden from touching rlgl, so all
drawing goes through here. Immediate-mode + state + high-level 3D draws:

```c
Eng_GfxBeginMode3D(cam); … Eng_GfxEndMode3D();
Eng_GfxDrawModel/ModelEx/Cube/CubeV/CubeWires(V)/Sphere/Plane/Triangle3D/Line3D/Grid(...);
Eng_GfxBeginQuads(texId); Eng_GfxColor/Normal/TexCoord/Vertex(...); Eng_GfxEndQuads();  // textured immediate quads
Eng_GfxDepthMask/DepthTest/BackfaceCull(bool);  Eng_GfxClearDepth();
Eng_GfxUseShader(s) / Eng_GfxUseDefaultShader();  Eng_GfxFlushBatch();
```

The game draws its scene with these between the engine's render bookends (below).
This is deliberately a *facade*, not a retained scene graph — see the
engine-separation doc for why a `DrawItem[]` submission model was rejected for
this renderer.

### render — `eng_render.h` (frame structure: passes, shaders, post-FX)
The engine owns the world shaders, lighting uniforms, and the post-FX render
target. The game sets lighting once per frame and brackets its world draws:

```c
Eng_RenderLoad();                       // load world/skinned/postfx shaders (in init)
Eng_RenderSetLighting((EngLighting){ .sunDir=…, .sunColor=…, .ambientColor=…,
                                     .fogStart=…, .fogEnd=…, .fogColor=… });
Eng_RenderBeginPostFX();                // redirect to the screen RT (no-op if postfx.fs missing)
  Eng_RenderBeginWorld();               // bind world shader + push lighting
    … draw the scene via Eng_Gfx* / Anim_Draw …
  Eng_RenderEndWorld();
Eng_RenderEndPostFX((EngPostFxParams){ .hitFlash=…, .lowHp=…, .time=… });  // composite
// HUD/UI draw AFTER EndPostFX so they aren't post-processed (see §3a).
Eng_RenderClearDepth();                 // e.g. before a first-person viewmodel pass
Eng_RenderWorldShader() / WorldSkinnedShader()    // handles, e.g. to stamp onto models
```

### content — `content.h` (asset loading)
Handle-based loaders with path-probing (`data/X → ../data/X → ./data/X`) and
dedup by resolved path. The engine reads bytes; the game interprets its own
formats via registered parsers.

```c
EngModel   m  = Eng_LoadModel("models/foo.obj");
EngTexture t  = Eng_LoadTexture("textures/bar.png");
EngTexture t2 = Eng_LoadTextureByName("bar");      // name-keyed, deduped
EngShader  s  = Eng_LoadShader("shaders/x.vs", "shaders/x.fs");
EngClipSet c  = Eng_LoadAnimModel("models/rig.glb");
Model     *rm = Eng_ModelGet(m);                   // underlying raylib value (until Eng_ContentFlush)
Eng_ResolveAssetPath("maps/a.map", buf, sizeof buf);
// Game-registered parser: engine loads bytes, hands them to fn(path,bytes,n,user).
Eng_RegisterContentType(".weapon", parse_weapon, userptr);
void *def = Eng_LoadContent("weapons/smg/smg.weapon");
Eng_UnloadModel(m);  Eng_UnloadTexture(t);         // per-handle release (refcounted)
Eng_ReloadTexture(t2);                             // re-read same path from disk (hot-reload)
Eng_ContentFlush();                                // release everything (e.g. on map unload)
```

**Failure contract:** handles wrap a `uint32_t` where `0 == invalid`. A failed
load returns an invalid handle (`id==0`) and logs one stderr line — it never
crashes; `Eng_ModelGet`/`Eng_TextureGet` hand back a safe placeholder for an
invalid handle, so you can defer the check. `Eng_LoadContent` (and any registered
parser) returns `NULL` on failure.

**Unload / reload:** `Eng_Unload{Model,Texture,Shader,ClipSet}(handle)` release a
single resource. Slots are **refcounted** — because loads dedup by resolved path,
N loads of one path share a slot and take N unloads to actually free; the freed
slot is then recycled by later loads. Unloading an invalid handle is a no-op, and
any stale handle to a freed slot reads back as the placeholder (never crashes).
`Eng_Reload{Model,Texture}(handle)` re-read the slot's path from disk in place (for
live editing); a failed reload keeps the old data and returns `false`. Bulk teardown
is still `Eng_ContentFlush()` (frees everything regardless of refcounts).

### anim — `anim.h` (skeletal animation, GPU skinning)
`AnimModel` is the shared rig+clips; `AnimState` is a lightweight per-instance
clip/time (keep one per entity, render-local). Apply the engine skinned shader.

```c
AnimModel am; Anim_Load(&am, "zombie.glb"); Anim_ApplyShader(&am, Eng_RenderWorldSkinnedShader());
int clip = Anim_FindClip(&am, "walk");
Anim_Play(&st, clip, /*loop*/true, /*speed*/1.0f);
Anim_Update(&am, &st, dt);
Anim_Draw(&am, &st, feetPos, yawDeg, scale, tint);
// Attach a separate object to a bone (e.g. a gun on hand.R):
Matrix bone = Anim_BoneMatrix(&am, &st, Anim_FindBone(&am, "hand.R"));   // after Anim_Pose
```

### audio — `audio.h` (mixer; event-driven)
Dumb mixer: the game decides *when*, the engine plays *how*. `Audio_Init`/
`Shutdown`/`Update` are already called by `app.c`.

```c
Audio_SetListener(camPos, camYaw);                    // each frame
Audio_Play(SFX_SHOT, vol, pitch);                     // 2D
Audio_PlayPanned(SFX_GROAN, vol, pitch, pan);         // explicit pan
if (Audio_Positional(srcPos, maxDist, &vol, &pan)) Audio_PlayPanned(id, vol, 1, pan);
Audio_PlayMusicTrack("nacht_loop"); Audio_StopMusic();
```
(This game's `audio_director.c` is the *game-side* glue that diffs world state
into these calls — a model worth copying, not part of the engine.)

### input — `pad.h` (action map + raw gamepad)
The engine owns a named action map; bind each game action id to a key + mouse
button + pad button, then query by action, never by raw key.

```c
Eng_InputBind(ACT_FIRE, KEY_NULL, MOUSE_BUTTON_LEFT, ENG_BIND_TRIG_R);
Eng_InputBind(ACT_RELOAD, KEY_R, -1, GAMEPAD_BUTTON_RIGHT_FACE_UP);
bool firing  = Eng_InputDown(ACT_FIRE);
bool reload  = Eng_InputPressed(ACT_RELOAD);          // rising edge
Vector2 look = Eng_InputLookDelta();                  // {yaw, pitch} this frame — mouse delta or right stick, sensitivity-scaled
Eng_InputSetLookSensitivity(mouse, pad);
Eng_InputTickTriggerEdges();                          // once per frame, end of frame
Vector2 mp = Eng_InputMousePos();                     // absolute cursor px (editor: drag-select, gizmos)
Vector2 md = Eng_InputMouseDelta();                   // raw unscaled mouse px this frame
float   wz = Eng_InputMouseWheel();                   // scroll this frame (editor: zoom)
// Raw readers still available: Pad_Connected/StickX/StickY/Down/Pressed/TriggerL/R.
```
`Eng_InputLookDelta()` is the mouselook primitive: call it in `frame`, add
`look.x`/`look.y` to your camera yaw/pitch. It already unifies mouse + right-stick
and applies the sensitivities set above. **Cursor capture is game-side** — the
engine doesn't grab the mouse for you; toggle raylib's `DisableCursor()` (locked
mouselook) / `EnableCursor()` (menus) yourself (see `src/game/main.c`).

`Eng_InputMousePos`/`MouseDelta`/`MouseWheel` route raw cursor state through the
same facade for an unlocked-cursor tool UI (the editor). Note `MouseDelta` is *raw
pixels, mouse-only* — distinct from `LookDelta` (sensitivity-scaled, mouse-or-stick).

Re-call `Eng_InputBind` whenever the player rebinds (see `Settings_SyncEngineBindings`
in the game for the pattern).

### net — `net.h` (UDP transport via enet)
Game-agnostic connect/poll/send. The wire *schema* (what a snapshot contains)
stays game-side in `protocol.c` — the engine never learns what a player is.

```c
Net_InitHost(7777); / Net_InitClient("127.0.0.1", 7777); / Net_Shutdown();
NetEvent evs[32]; int n = Net_Poll(evs, 32);          // NEV_CONNECT/DISCONNECT/RECEIVE
Net_SendTo(peerIdx, data, len, /*reliable*/true); Net_Broadcast(data, len, true);
Net_GetLocalIPs(ips, max);                            // for the host's join prompt
```

### mapdoc — `mapdoc.h` (neutral map document)
A pure C-stdlib document mirroring the `.map` grammar — no raylib, no game
types. The engine parses/serialises it; the game (or the editor) instantiates it
into its own world. **This is the seam the map editor is built on.**

```c
MapDoc doc; MapDoc_Parse("maps/nacht.map", &doc, stderr);   // returns error count
MapDoc_Save("maps/out.map", &doc);
MapDoc_Equal(&a, &b);                                       // round-trip check
int id = MapDoc_AllocId(&doc);                              // stable id for a newly added entity
```
**Stable entity IDs:** every placed-entity struct carries an `int id`; `MapDoc_Parse`
assigns fresh monotonic ids and sets `doc.nextId`. IDs are **runtime-only** — not
serialised by `MapDoc_Save` and ignored by `MapDoc_Equal` (so the round-trip stays
intact). The editor uses them as persistent handles for selection / undo / drag,
minting new ones with `MapDoc_AllocId` — array indices shift on insert/delete and
can't serve as identity. The id-addressed editing layer (mutators + undo/redo) lives
in `mapedit.h` (§3 `mapedit`).

### fx — `fx.h`, `particles.h`, `decals.h`
Generic camera-shake/trauma, particle emitters, and decals.

```c
Fx_Punch(trauma); Fx_Rumble(lo, hi, secs); Vector3 off = Fx_CameraOffset(&yawJ, &pitchJ);
Eng_FxEmit((EngEmitterDesc){ … });   Particles_Update(dt); Particles_Draw(cam);
Eng_FxDecal(ENG_DECAL_SPLAT, pos, normal, size);   Decals_Update(dt); Decals_Draw();
```

### pick — `pick.h` (ray construction + intersection)
Pure, stateless helpers for turning a screen pixel into a world ray and testing it
— the core primitive the map editor selects and places with.

```c
Ray r = Eng_PickRayFromScreen(cam, Eng_InputMousePos(), w, h);  // cursor → world ray
float t; if (Eng_PickRayAABB(r, box, &t))    … ;                // ray vs AABB (nearest hit dist)
if (Eng_PickRaySphere(r, c, radius, &t))     … ;
Vector3 hit; if (Eng_PickRayPlane(r, pPt, pN, &t, &hit)) … ;    // ray vs infinite plane
if (Eng_PickRayGroundY(r, 0.0f, &hit))       … ;                // where on the floor was clicked
```
(Hit-testing against *game* world geometry is still game-side — see §3a; these are
the math/picking primitives that test was missing.)

### collide — `collide.h` (sweep / overlap / penetration math)
Pure collision primitives — the movement substrate. The engine answers *do these
volumes touch, when, and how deep*; it does **not** decide the response. The
character controller (slide, bhop, step-up, friction) is **game-side and pluggable**
— read these results and apply your own movement, or ignore the module entirely.

```c
Eng_CollideAABBOverlap(a, b);                              // quick reject
Vector3 mtv;  Eng_CollideAABBPenetration(a, b, &mtv);      // depenetration (move a by mtv)
Vector3 push; Eng_CollideSphereAABB(c, r, box, &push);     // sphere/capsule push-out…
Eng_CollideCapsuleAABB(p0, p1, r, box, &push);
float t; Vector3 n;
Eng_CollideSweptAABB(moverBox, vel, box, &t, &n);          // time-of-impact + contact normal
```
`Eng_CollideSweptAABB` is the anti-tunnelling workhorse: `vel` is the full-frame
displacement, `t` is the TOI in [0,1], `n` is the static box's surface normal. A
mover advances to `t`, kills the into-surface velocity, and re-sweeps the remainder
— that slide loop is the game's to write. (Returned normals are the static box's
outward normal; push vectors move the mover out. The engine has no broadphase — you
iterate your own candidate boxes.)

### debugdraw — `debugdraw.h` (deferred overlay)
Toggleable, queued line/box/sphere/3D-text overlay. Submit from anywhere in the
frame; the queue is drawn once and cleared. Off by default, so calls can live
permanently in code at ~zero cost.

```c
Eng_DebugSetEnabled(true);                              // dev toggle (hotkey/console)
Eng_DebugLine(a, b, RED);  Eng_DebugBox(bbox, GREEN);  Eng_DebugSphere(c, r, BLUE);
Eng_DebugText3D(worldPos, "spawn", YELLOW);
// at frame end:
Eng_GfxBeginMode3D(cam);  …world…  Eng_DebugDraw3D(cam);  Eng_GfxEndMode3D();
Eng_DebugDrawLabels(cam);                               // 2D text, AFTER EndMode3D
```
Order matters: shapes (`Eng_DebugDraw3D`) inside `Mode3D`, labels
(`Eng_DebugDrawLabels`) after. Both clear their queues even when disabled.

### gizmo — `gizmo.h` (transform-handle interaction math)
Pure, stateless manipulation math for a translate/rotate/scale gizmo — the editor
analogue of `pick.h`. It answers *which handle is under the cursor* and *given mouse
motion, what constrained delta results*; it owns **no** selection, MapDoc, or camera.
Modes `ENG_GIZMO_TRANSLATE/ROTATE/SCALE`; handles `ENG_GIZMO_AXIS_X/Y/Z` plus the
planar `_XY/_YZ/_XZ`.

```c
EngGizmoAxis hot = Eng_GizmoHitTest(cam, mouse, w, h, origin, mode, handleSize);
EngGizmoDrag d   = Eng_GizmoBeginDrag(cam, mouse, w, h, origin, mode, hot);  // on mouse-down
EngGizmoDelta mv = Eng_GizmoUpdateDrag(d, cam, mouse, w, h);                 // each frame
// mv is accumulated SINCE grab — recompute newTransform = original + mv (drift-free).
Eng_GizmoDebugDraw(origin, mode, handleSize, hot);   // optional: draw handles via debugdraw
```
The delta is **absolute since grab**, not per-frame: the caller rebuilds the
transform from the entity's pre-drag value each frame, so there's no accumulated
float drift. Translate returns a signed axis distance / plane offset, rotate a signed
angle (radians) about the axis, scale a factor. The pure helpers
`Eng_GizmoClosestPointOnAxis` / `Eng_GizmoAngleInPlane` are camera-free (headless-testable);
the screen→ray step reuses `Eng_PickRayFromScreen` (§3 `pick.h`).

### mapedit — `mapedit.h` (id-addressed edits + undo/redo)
Stdlib-only companion to `mapdoc.h`: edit a `MapDoc` by **stable id** (not fragile
array index) and a **snapshot** undo/redo history. This is what a gizmo drag writes
into, and what an editor's Ctrl-Z drives.

```c
int id = EngMapEnt_Add(&doc, ENGMAPENT_PROP);     // mints id, respects MAPDOC_MAX_*; -1 on cap
Eng_SetPos(&doc, id, x, z);                        // id-addressed mutators (pos: all kinds)
Eng_SetYaw(&doc, id, deg); Eng_SetScale(&doc, id, s);   // prop; obstacle/sector have size/height setters
EngMapEnt_Delete(&doc, id);                        // compacts the array; fixes sector index refs

EngMapHistory h; EngMapHistory_Init(&h, ENGMAPHISTORY_DEFAULT_DEPTH);
EngMapHistory_Commit(&h, &doc, 0);                 // checkpoint (tag 0 = distinct step)
if (EngMapHistory_Undo(&h, &doc)) …                // writes prior snapshot into doc
EngMapHistory_Free(&h);
```
History stores full `MapDoc` snapshots (type-agnostic, always correct since MapDoc is
flat POD) at a fixed depth ring. A no-op commit (equal by `MapDoc_Equal`) is dropped.
**Coalescing:** commit a drag every frame under a non-zero `tag` (e.g. `"drag:42"`'s
hash) and the consecutive same-tag commits overwrite one checkpoint instead of
stacking — so a whole drag is a single undo step; `tag 0` and any Undo/Redo reset the
coalesce state. `EngMapEnt_Find` / `EngMapEnt_Ptr` resolve an id to a typed handle for
custom edits.

---

## 3a. What the engine deliberately leaves to the game

Three things a game needs but the engine has **no `Eng_*` API for** — on purpose.
They're not gaps; they're the game's job. A second module handles them the same way.

### 2D / HUD / text — raw raylib, after post-FX
There is no `Eng_Gfx2D`/text facade. The `Eng_Gfx*` facade exists only to keep
**rlgl** out of game code (§1); raylib's own 2D immediate calls are *not* engine-only,
so the game uses them directly:

```c
// In draw(), AFTER Eng_RenderEndPostFX() so the HUD isn't post-processed:
DrawText("AMMO 30", x, y, 20, WHITE);                 // raylib, called from game code
DrawRectangle(cx - 1, cy - 8, 2, 16, crosshairCol);   // crosshair, bars, panels
DrawTextureEx(icon, pos, 0, scale, tint);
```

The game's `hud.c` and `menu.c` are the reference: crosshair, health/ammo, hitmarkers,
and menus are all hand-drawn with these. Load fonts with raylib's `LoadFont*` if you
outgrow the built-in font. The editor draws its property panels / labels the same way.

### Collision, raycasts & hit detection — game-side geometry
The engine now ships collision *math* — ray tests (§3 `pick.h`) and sweep/overlap/
penetration (§3 `collide.h`) — but it has **no broadphase, no world, and no
response/controller**. *What* is solid, *which* candidates to test, and *how* a mover
or bullet reacts are all game-side. This covers three things, not just weapons:

- **Movement collision:** the game push-resolves the player out of solid colliders
  each tick — `PushOutOfBoxXZ` vs obstacles / interior walls / closed doors
  (`level.c`, `player.c`), plus `PushOutOfEnemies`. (This game predates `collide.h`
  and rolls its own; new movers can lean on `Eng_CollideSweptAABB` /
  `Eng_CollideCapsuleAABB` instead — but the *controller* is still yours to write.)
- **Hitscan:** the game derives a fire direction (camera forward + per-pellet
  `Weapon_SpreadDir`) and tests targets with a cheap forward-arc dot + distance
  (`weapons.c`). Bullets-vs-walls use a **Y-aware wall-segment** test (`level.c`);
  door headers/lintels block shots, open doorways don't.
- **AI / line-of-sight & nav:** also game-side, against the same colliders.

If a second module needs picking (the editor's ray-vs-scene select), the engine now
provides the ray math — `Eng_PickRayFromScreen` + `Eng_PickRay*` (§3 `pick.h`) — but
*what* the ray tests against is still the module's own geometry: walk the `MapDoc` and
intersect, reusing the shared floor-height / region-nav helpers in
[multi-floor-maps.md](multi-floor-maps.md) §7.

### Screenshots & runtime window control — raylib direct
Like the HUD, these aren't wrapped: the window opens **resizable + vsync + MSAA-4x**
(set in `app.c`), and the game calls raylib directly for the rest — `TakeScreenshot(path)`
(see the `--screenshot-*` dev modes in `devtools.c`), `ToggleFullscreen()`,
`GetScreenWidth/Height()`. The engine owns `main()` and window creation, but the live
window is raylib's and game code may drive it.

### Settings persistence — game-side
Key bindings, sensitivities, and video/audio options are loaded/saved by the game and
pushed into the engine via `Eng_InputBind` / `Eng_InputSetLookSensitivity` on startup
and on every rebind (see `Settings_SyncEngineBindings`). The engine holds the live
action map but never touches disk for you.

---

## 3b. Not yet in the engine

Genuinely absent — not a deliberate game-side punt, just unbuilt. If you reach for
one of these, it isn't there yet:

- **Generic (de)serialization** — `MapDoc` covers the `.map` format only; save-games and
  net payloads are hand-packed.
- **Async / background asset loading** — `content.h` loaders are main-thread, synchronous.
- **gfx convenience draws** — no billboard or bounding-box helper in the `Eng_Gfx*` facade.
- **Multiplayer netcode depth** — `net.h` is raw transport (`NET_MAX_PLAYERS == 4`); no
  interpolation / delta-compression / server-tick loop yet.

This is just the current limits list. For the *plan* — phases, priorities, and the
Krunker-like end goal driving them — see **[engine-roadmap.md](engine-roadmap.md)**.
When an item ships, document it in §3 and delete it here.

---

## 4. Building a second module (e.g. the map editor)

The litmus for the whole split: *a second game is a second `GameModule`,
touching zero engine files.*

1. New `editor_main.c`: `Eng_Run(&cfg, Editor_Module())`.
2. `init`: `Eng_RenderLoad()`, `Eng_LoadModel/Texture` your assets,
   `Eng_InputBind` your editor actions, `MapDoc_Parse` the map you're editing.
3. `frame`: read input via `Eng_Input*`, move the editor camera, hit-test/select.
4. `fixed`: usually empty (no sim) — or a light preview tick.
5. `draw`: `Eng_RenderBeginPostFX`/`BeginWorld` → walk the `MapDoc` and draw
   sectors/props with `Eng_Gfx*` + gizmos → `EndWorld`/`EndPostFX` → 2D UI.
6. `shutdown`: `MapDoc_Save`, `Eng_ContentFlush`.

You reuse the window/loop/timestep, rendering passes + post-FX, content loading,
input map, audio, and `MapDoc` — and write only editing logic. (Spatial queries
the editor and game share — floor height, region nav — are described in
[multi-floor-maps.md](multi-floor-maps.md) §7.)

---

## 5. Headless / CI

The sim is window-free: `./build/shooter --sim-tick <map> [frames]` instantiates
the world from a `MapDoc` and ticks the full simulation with no GL. CI
(`.github/workflows/ci.yml`) runs the seam check, build, `--validate`, and
`--sim-tick` on every map. When adding engine code, keep all of these green.

## 6. Build

CMake builds `libengine.a` (`add_library(engine …)` — engine sources only) and
links the game executable against it. A new module adds its own target that
links `engine`. The link step itself enforces the seam: the engine archive
contains no game symbols to call.
