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

Per rendered frame the engine does:

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
`Eng_Run(cfg, module)`, `EngConfig{w,h,title}`, `GameModule`, `ENG_FIXED_DT`.

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
Eng_ContentFlush();                                // release everything (e.g. on map unload)
```

**Failure contract:** handles wrap a `uint32_t` where `0 == invalid`. A failed
load returns an invalid handle (`id==0`) and logs one stderr line — it never
crashes; `Eng_ModelGet`/`Eng_TextureGet` hand back a safe placeholder for an
invalid handle, so you can defer the check. `Eng_LoadContent` (and any registered
parser) returns `NULL` on failure. **Unload is all-or-nothing** — `Eng_ContentFlush`
only; there is no per-handle unload or hot-reload (see §3b).

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
// Raw readers still available: Pad_Connected/StickX/StickY/Down/Pressed/TriggerL/R.
```
`Eng_InputLookDelta()` is the mouselook primitive: call it in `frame`, add
`look.x`/`look.y` to your camera yaw/pitch. It already unifies mouse + right-stick
and applies the sensitivities set above. **Cursor capture is game-side** — the
engine doesn't grab the mouse for you; toggle raylib's `DisableCursor()` (locked
mouselook) / `EnableCursor()` (menus) yourself (see `src/game/main.c`).

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
```

### fx — `fx.h`, `particles.h`, `decals.h`
Generic camera-shake/trauma, particle emitters, and decals.

```c
Fx_Punch(trauma); Fx_Rumble(lo, hi, secs); Vector3 off = Fx_CameraOffset(&yawJ, &pitchJ);
Eng_FxEmit((EngEmitterDesc){ … });   Particles_Update(dt); Particles_Draw(cam);
Eng_FxDecal(ENG_DECAL_SPLAT, pos, normal, size);   Decals_Update(dt); Decals_Draw();
```

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
The engine ships **no raycast, sweep, or collision-query primitive** — *all* spatial
tests run game-side against the game's own world representation, not an engine
structure. This covers three things, not just weapons:

- **Movement collision:** the game push-resolves the player out of solid colliders
  each tick — `PushOutOfBoxXZ` vs obstacles / interior walls / closed doors
  (`level.c`, `player.c`), plus `PushOutOfEnemies`. There is no character-controller
  or capsule-sweep in the engine; you write the resolver.
- **Hitscan:** the game derives a fire direction (camera forward + per-pellet
  `Weapon_SpreadDir`) and tests targets with a cheap forward-arc dot + distance
  (`weapons.c`). Bullets-vs-walls use a **Y-aware wall-segment** test (`level.c`);
  door headers/lintels block shots, open doorways don't.
- **AI / line-of-sight & nav:** also game-side, against the same colliders.

If a second module needs picking (the editor's ray-vs-scene select), it builds the
ray from the camera and tests against `MapDoc` geometry itself — reuse the shared
floor-height / region-nav spatial helpers in [multi-floor-maps.md](multi-floor-maps.md) §7.

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

Genuinely absent — not a deliberate game-side punt, just unbuilt. Know these before
you start the map editor; some are on its critical path.

- **Per-handle content unload / hot-reload** — only `Eng_ContentFlush` (all-or-nothing).
  An editor's load→edit→reload loop currently means re-flush + re-load everything.
  The most likely first thing the editor will need from the engine.
- **Debug-draw channel** — no toggleable, draw-after-the-fact line/box/3D-text overlay.
  `Eng_GfxCubeWires`/`Line3D`/`Grid` work for ad-hoc viz, but there's no persistent
  debug layer for collision volumes, nav, or gizmo state. (§4 names "gizmos" — the
  editor draws them itself with `Eng_Gfx*`; the engine provides no gizmo API.)
- **Profiling / timing query** — no frame-time / fixed-step-count / draw-call API.
- **Generic (de)serialization** — `MapDoc` covers the `.map` format only; save-games and
  net payloads are hand-packed.

If you build any of these into `src/engine/`, add it to §3 and delete it here.

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
