# Engine / Game Separation — Design & Plan

> ## ▶ Resume here (state as of commit `714cd20`)
> Everything below "Progress" is current. Tree is clean and green; build runs
> `seam-check` and a pre-commit hook enforces the §2 rule
> (`git config core.hooksPath scripts/hooks` if cloning fresh).
>
> **Build + verify loop** (no in-game input/audio path is headless, so these
> plus a manual playtest are the regression check):
> ```
> cmake --build build -j                 # seam-check runs first, then compiles
> ./build/shooter --screenshot-zombies   # exercises g_world.enemies
> ./build/shooter --screenshot-coop      # players[] + round state
> timeout 5 ./build/shooter              # exit 124 = ran clean
> ```
>
> **Done since a590afb:** all level state relocated into `g_world` — collision-free
> names via macro alias (commit `ef90e9e`), colliders (`obstacles`, `obstacleCount`,
> `windows`, `windowCount`, `pap`, `mbox`, `mapName`, `arenaHalfX/Z`) via explicit
> `g_world.X` (commit `714cd20`). `level.{h,c}` now hold no state. The collider
> rewrite was a lookbehind-guarded perl substitution over `src/*.c`
> (`s/(?<![.>\w])NAME\b/g_world.NAME/g`) — it correctly skipped `doc->X`/`hdr->X`
> field accesses, so protocol.c needed no hand-editing after all. Non-zero defaults
> (`mapName "Default"`, arena halves) preserved via a designated initializer on
> `g_world` in `world.c`.
>
> **Next slice — finish Phase 0 state relocation into `g_world`** (`src/world.{h,c}`).
> Two proven techniques:
> 1. *collision-free name* → add a `World` member + an object-like alias macro in
>    `world.h` (`#define enemies (g_world.enemies)`). Safe ONLY if the name is
>    never a struct field or local — check with
>    `grep -rhoE '[.>]NAME\b' src/*.c src/*.h`.
> 2. *collider* (name also a struct field, e.g. in `PktSnapshotHeader`/`MapDoc`)
>    → add a `World` member with **no** macro; rewrite every bare use to
>    `g_world.NAME` (sed `s/\bNAME\b/g_world.NAME/g` on the simple files; hand-edit
>    files where a `hdr->NAME`/`doc->NAME` field shares a line — currently only
>    `protocol.c`). The linker catches any miss (undefined `NAME`).
>
> Order of attack for the rest: **`players[]`** (collider, 277 uses/13 files — do
> it alone, playtest after) and `netMode`. (`mbox`, `mapName`, and all level state
> are now done — see "Done since a590afb" above.) Each global: remove its `extern`
> from the canonical header
> (which already `#include "world.h"`) and its definition from the `.c`. After
> all state is in `g_world`, start threading `World *` through the sim toward the
> §15 litmus (headless `fixed()` tick). Then resume Phase 2 (gamepad fold-in,
> movement/look), Phase 4 (content registry), Phase 5 (render seam), Phase 6
> (`main.c` flip), Phase 7 (full dir split).
>
> **Subagent note:** worktree agents keep forking from stale bases — only spawn
> them for additive, independent, green-building units, tell them to rebase onto
> main first, and always `git diff <merge-base> <branch>` before merging.

Status: **in progress**. This is the target architecture and the incremental
path to it. The goal is a clean, reusable **engine** that owns

> **Progress (2026-06-14)**
> - ✅ **Phase 3 (clean-module relocation)** — `mapdoc, net, anim, particles,
>   decals, pad, fx` moved to `src/engine/` (commit `cefdae3`). `src/engine`
>   is on the include path so includers were untouched. Cardinal-rule grep
>   over `src/engine/` is clean. `src/game/` scaffolded.
> - ✅ **Phase 1, leak #2 (assets→weapons)** — `assets.c` no longer includes
>   `weapons.h` or iterates `weaponModels[]`; the game enrols viewmodels via
>   `Assets_RegisterWorldShaderModel` from `Weapons_Load` (commit `c2111ee`).
> - ✅ **Phase 1, leak #1 (audio→game)** — `audio.c` is now a pure mixer
>   (`audio.h` + libc only): `SfxId` sound bank, `Audio_Play`/`Audio_PlayPanned`,
>   `Audio_SetListener`/`Audio_Positional`, `Audio_PlayMusicTrack`. The new
>   game-side `audio_director.c` owns the diff/timer state and fires events
>   (`AudioDirector_Tick`, commit `cd6989f`). **Phase 1 complete — both §2
>   leaks closed, dependency direction is now correct.**
> - ✅ **Phase 7a (seam relocation + enforcement)** — the now-pure `audio.c`
>   moved to `src/engine/`; `scripts/check-seam.sh` enforces the §2 cardinal
>   rule, wired as a CMake `seam-check` target the `shooter` build depends on,
>   plus a tracked `scripts/hooks/pre-commit` (activate with
>   `git config core.hooksPath scripts/hooks`).
> - 🔶 **Phase 2 (input action map) — infra + gameplay edges migrated** — engine
>   action map in `src/engine/pad.{c,h}`: `Eng_InputBind` (key + mouse + pad) /
>   `Eng_InputPressed` / `Eng_InputDown` / `Eng_InputMoveAxis` /
>   `Eng_InputLookDelta` (commits `b3247ab`, `eb55002`). `HandleLocalActions` +
>   fire/ADS/interact-held now read the action map (keyed by the existing `BA_*`
>   ids) instead of hardcoded `KEY_*`/mouse literals; keyboard+mouse bound once
>   at startup via `BindGameInputDefaults`. **Behaviour-preserving by
>   construction** (each action bound to exactly its old key/mouse), but in-game
>   input is not headlessly verifiable — wants a manual playtest. **TODO:** fold
>   the configurable gamepad layer (`Bind_*`) into the engine map so each site is
>   a single call; migrate movement/look onto `Eng_InputMoveAxis/LookDelta`; the
>   menu/HUD/debug key reads (player.c, menu.c, hud.c, settings.c) are untouched.
> - ✅ **Phase 0 (state ownership) — all level state relocated** — collision-free
>   level globals (`interiorWalls`, `doors`, `wallBuys`, `perkMachines`,
>   `mapSpawns`, `mapProps` + counts/handles) via macro alias (commit `ef90e9e`);
>   colliders (`obstacles`, `obstacleCount`, `windows`, `windowCount`, `pap`,
>   `mbox`, `mapName`, `arenaHalfX/Z`) via explicit `g_world.X` (commit `714cd20`).
>   `level.{h,c}` now hold no game state. Only `players[]` and `netMode` remain
>   before the `World *` threading toward the §15 headless-tick litmus.
> - 🔶 **Phase 0 (earlier) — World struct + first clusters** — `src/world.{h,c}`
>   add the `World` struct and the live `g_world`. Migrated so far:
>   `enemies, bullets, throwables, powerUps, localPlayerIdx, gamePhase` via
>   transitional macros (collision-free names, commit `059b638`); and
>   `roundNum, doublePointsTimer, instaKillTimer` via **explicit `g_world.X`**
>   (these clash with `PktSnapshotHeader` fields so can't be macro-aliased —
>   commit `e02f621`). Two techniques now established: object-like macro for
>   collision-free names, explicit relocation for colliders (leave `hdr->X`
>   snapshot fields alone). Pure storage relocation — linker-proven,
>   `--screenshot-{zombies,coop}` + 5s run clean.
> - Remaining Phase 0: relocate the rest (`players[]` — 277 uses/13 files,
>   `mbox`, `mapName`, level state) the same way, then thread `World *` through
>   the sim so it can tick headless (the §15 litmus). Macros are removed as call
>   sites take a `World *`.
> - Remaining: Phase 0 (players[]/level/mbox/mapName + headless tick), Phase 2
>   gamepad fold-in + movement/look, Phase 4 content registry, Phase 5 render
>   seam, Phase 6 `main.c` flip, Phase 7 full dir split.
>
> **Note on Phase 0:** the doc's §14 macro-shim (`#define players …`) is unsafe
> as-is — `players`, `mbox`, `mapName`, `roundNum` and the power-up timers are
> also field names in the protocol/mapdoc structs, so an object-like macro would
> corrupt `hdr->players` / `doc->mbox` / `s->mapName`. Phase 0 therefore needs
> real `World *` threading for those names (or a rename), not a blanket macro.

all infrastructure — content loading, rendering, audio, animation, particles,
input, networking transport — and a **game** that is pure rules + content
sitting on top of it.

The litmus test for "done": you could build a *second* game (a different
shooter, a walking sim, the map editor) against the same engine without
touching a single engine file.

---

## 1. Vision: the engine is a runtime, the game is a module it hosts

Today `main.c` *is* the game: it opens the window, owns the frame loop, and
also knows about spectator cameras, weapon swap edges, and post-FX HP ramps.
There is no engine — raylib is the substrate and everything above it is one
undifferentiated blob of globals.

Target model:

- The **engine** owns `main()`, the window, the frame loop, and every
  subsystem (render/audio/anim/content/input/net/fx). It knows nothing about
  zombies, perks, rounds, or weapons.
- The **game** is a *module* the engine hosts. It registers itself once
  (`Game_Module()`), hands the engine a set of callbacks, and from then on the
  engine calls *down* into the game each frame. The game calls *into* engine
  services, never the reverse.

```
            ┌───────────────────────────────────────────┐
            │                  ENGINE                    │
            │  main loop · window · time                 │
            │  ┌───────┐ ┌────────┐ ┌──────┐ ┌────────┐  │
            │  │render │ │ audio  │ │ anim │ │content │  │
            │  └───────┘ └────────┘ └──────┘ └────────┘  │
            │  ┌───────┐ ┌────────┐ ┌──────┐ ┌────────┐  │
            │  │ input │ │  net   │ │  fx  │ │ math   │  │
            │  └───────┘ └────────┘ └──────┘ └────────┘  │
            └───────────────▲───────────────┬───────────┘
              game calls services   engine calls game callbacks
                            │               ▼
            ┌───────────────┴───────────────────────────┐
            │                   GAME                      │
            │  World state · simulation · rules           │
            │  content defs (weapons/perks/rounds)        │
            │  GameModule callbacks (update/render/net)   │
            └─────────────────────────────────────────────┘
```

---

## 2. The cardinal rule

**Engine code never includes a game header. Ever.**

That single constraint *is* the separation. Everything else is consequence.

Enforcement is a one-line grep that should return nothing — wire it into CI or
a pre-commit hook:

```bash
# Engine must not know about game concepts.
grep -rEl 'weapons\.h|perks\.h|entities\.h|game\.h|player\.h|interact\.h|hud\.h|menu\.h' src/engine/ \
  && echo "SEAM VIOLATION" && exit 1
```

Two current violations to fix first, because they are the whole problem in
miniature:

| File | Bad include | Why it's wrong | Fix | Status |
|------|-------------|----------------|-----|--------|
| `audio.c` | `game.h`, `level.h`, `entities.h`, `player.h` | the mixer pulls game state to decide what to play (`Audio_Tick(me)` diffs HP, round phase, box state) | invert to **push**: game fires `Eng_AudioPlay(clip, pos)` events; mixer just plays | ✅ done (mixer + game-side `audio_director.c`, commit `cd6989f`) |
| `assets.c` | `weapons.h` | the asset loader iterates `weaponModels[]` in `Assets_ApplyWorldShader` | invert: game **registers** which model handles want the world shader | ✅ done (`Assets_RegisterWorldShaderModel`, commit `c2111ee`) |

If those two inversions land, the seam exists in principle and the rest is
mechanical.

---

## 3. Directory layout

```
src/
  engine/                 ← reusable, zero game knowledge
    eng.h                 master include (pulls the subsystem headers)
    app.c/.h              window, frame loop, time, lifecycle, GameModule host
    content.c/.h          asset registry: models/textures/shaders/audio/anim
                          + registration of game content parsers
    render.c/.h           scene submission, camera, passes, shaders, postFX, lighting
    anim.c/.h             skeletal animation runtime (generalised from today's anim.c)
    audio.c/.h            mixer: 3D positional, event-driven
    fx.c/.h               particles + decals as generic systems
    input.c/.h            polling + named action map
    net.c/.h              transport (enet wrapper) + replication channel
    math.h                thin re-export of raymath + helpers
    io.c/.h               file probing, config (.cfg) read/write
    mapdoc.c/.h           generic map document (already engine-clean — moves as-is)

  game/                   ← rules + content, depends on engine only
    main.c                tiny: Eng_Run(Game_Module())
    module.c              the GameModule vtable wiring
    world.h               World struct (all game state, was the extern globals)
    sim/                  level, player, weapons, perks, entities, interact, rounds
    content/              weapon/perk/round definitions + parser registration
    view/                 builds the render frame from World (was render.c/viewmodel.c)
    ui/                   hud, menu (game-specific; uses engine UI primitives)
    net/                  protocol: snapshot (de)serialisation (the wire schema)

  shared/
    types.h               split: engine-types vs game-types (see §13)
```

`mapdoc` already includes only `mapdoc.h` — it moves to `engine/` unchanged and
becomes the first proof the layout works.

---

## 4. Ownership: where every current file lands

| Current | Lands in | Notes |
|---------|----------|-------|
| `main.c` | split: `engine/app.c` (loop) + `game/main.c` + `game/module.c` | the ~250-line gameplay body moves into game callbacks |
| `mapdoc.*` | `engine/` | already clean |
| `assets.*` | `engine/content.*` | weapon-model coupling inverted (§2) |
| `render.*` | split: `engine/render.*` (device/passes) + `game/view/` (scene build) | the hard one — see §8 |
| `viewmodel.*` | `game/view/` | uses engine anim + render submission |
| `anim.*` | `engine/anim.*` | generalise: engine owns instance pool (§9) |
| `particles.*`, `decals.*` | `engine/fx.*` | generic emitters; game picks effect params |
| `audio.*` | `engine/audio.*` | invert to event push (§2, §10) |
| `net.*` | `engine/net.*` | already game-clean |
| `protocol.*` | `game/net/` | knows the World schema — stays game-side (§12) |
| `pad.*`, `settings.*` (bindings) | `engine/input.*` | action map is engine; *which* actions exist is game-registered |
| `settings.*` (cfg I/O) | `engine/io.*` | |
| `fx.*` (camera shake) | `engine/render.*` (camera) or `engine/fx.*` | trauma model is generic |
| `level.*` | `game/sim/` | instantiates a MapDoc into World; collision rules are game |
| `player, weapons, perks, entities, interact, game(round)` | `game/sim/` | pure rules |
| `hud.*`, `menu.*` | `game/ui/` | draw via engine UI primitives |
| `devtools.*` | split | CLI harness is engine; per-mode scene setup is game |

---

## 5. State ownership: kill the global externs

The defining problem today is ~60 `extern` globals spread across headers
(`players[]`, `enemies[]`, `bullets[]`, `worldShader`, `textures[]`,
`fogStart`, …). They are *the* coupling: any file can touch any state.

Collapse into two owners.

```c
// engine/app.h — engine-owned, passed to every game callback
typedef struct Engine Engine;   // opaque; holds subsystem state below

// Subsystem handles the game uses (all opaque, accessed via Eng_* calls):
//   content registry, render device, audio mixer, anim pool,
//   input state, net transport.
```

```c
// game/world.h — game-owned
typedef struct {
    Player    players[NET_MAX_PLAYERS];
    Enemy     enemies[MAX_ENEMIES];
    Bullet    bullets[MAX_BULLETS];
    Throwable throwables[MAX_THROWABLES];
    PowerUp   powerUps[MAX_POWERUPS];
    Level     level;        // obstacles/doors/windows/spawns (was level.h externs)
    RoundState round;       // roundNum, gamePhase, timers
    int       localPlayerIdx;
    NetMode   netMode;
} World;
```

Every function gains a `World *w` (and `Engine *e` if it touches services)
instead of reaching for a global. This is verbose but it is exactly what makes
the engine reusable and the game headless-testable. The render tuning globals
(`fogStart`, `sunDir`, shader uniform `*_Loc` ints) move *inside* the engine
render device — the game sets them through a small `Eng_RenderSetLighting(...)`
call, not by writing a global.

---

## 6. The engine API, subsystem by subsystem

All engine headers are prefixed `Eng_` and take an `Engine *` (or an opaque
subsystem handle). None mention a game type.

### app

```c
Engine *Eng_Init(const EngConfig *cfg);   // window, GL, subsystems
void    Eng_Run(Engine *e, GameModule game);  // owns the loop; calls game callbacks
void    Eng_Shutdown(Engine *e);
double  Eng_FrameTime(Engine *e);
void    Eng_RequestQuit(Engine *e);
```

### content (was assets.c)

Handle-based registry. Generic loaders for engine-native formats; the game
registers **parsers** for its own content types so the engine can load
`.weapon`/`.map` files without knowing their meaning.

```c
typedef struct { uint32_t id; } ModelHandle;   // 0 = invalid
typedef struct { uint32_t id; } TexHandle;
typedef struct { uint32_t id; } ShaderHandle;
typedef struct { uint32_t id; } ClipSetHandle; // a skinned model + its anim clips

ModelHandle  Eng_LoadModel (Engine*, const char *path);
TexHandle    Eng_LoadTexture(Engine*, const char *path);   // name-cached, dedup
ClipSetHandle Eng_LoadAnimModel(Engine*, const char *path);
ShaderHandle Eng_LoadShader (Engine*, const char *vs, const char *fs);

// Game registers a parser for its content extension. Engine reads bytes +
// resolves asset paths; game turns bytes into its own def struct.
typedef void *(*ContentParseFn)(const char *path, const uint8_t *bytes, size_t n, void *user);
void Eng_RegisterContentType(Engine*, const char *ext, ContentParseFn fn, void *user);
```

The current `Assets_ApplyWorldShader` coupling dies here: instead of the engine
walking `weaponModels[]`, models loaded through `Eng_LoadModel` are *already*
bound to the active world shader by the registry. The game never asks for it.

### render — see §8 (the important one).

### anim — see §9.

### audio (was audio.c)

```c
typedef struct { uint32_t id; } SoundHandle;
SoundHandle Eng_LoadSound(Engine*, const char *path);

void Eng_AudioListener(Engine*, Vector3 pos, Vector3 forward);  // set each frame
void Eng_AudioPlay (Engine*, SoundHandle s, Vector3 pos, float gain);  // 3D one-shot
void Eng_AudioPlay2D(Engine*, SoundHandle s, float gain);             // UI/non-spatial
void Eng_AudioMusic(Engine*, SoundHandle s, float gain, bool loop);
void Eng_AudioMasterVol(Engine*, float v);
```

No `Audio_Tick(me)`. The game diffs its own state and *fires* events. The mixer
is dumb and reusable.

### fx (was particles.c + decals.c)

```c
void Eng_FxEmit(Engine*, EngEmitterDesc desc);  // pos, dir, count, kind, color, ttl
void Eng_FxDecal(Engine*, Vector3 pos, Vector3 normal, float size, TexHandle tex);
```

Generic. Today's `Particles_BloodMist` / `Particles_MuzzleFlash` become game-side
helpers that fill an `EngEmitterDesc` and call `Eng_FxEmit`.

### input (was pad.c + settings bindings)

The engine owns an **action map**; the game registers the action *names* it
cares about, then queries by action — never by raw key.

```c
typedef int Action;   // game-defined ids
void Eng_InputBind(Engine*, Action a, int key, int padButton);
bool Eng_InputPressed(Engine*, Action a);   // edge
bool Eng_InputDown   (Engine*, Action a);   // held
Vector2 Eng_InputMoveAxis(Engine*);         // resolved WASD/stick
Vector2 Eng_InputLookDelta(Engine*);        // mouse/stick, sens applied
```

This dissolves the per-key spaghetti in `HandleLocalActions` — the game asks
`Eng_InputPressed(ACT_RELOAD)` and doesn't care if it came from `R` or a pad.

### net (was net.c)

Stays as-is (already game-clean), plus an optional replication channel the game
drives with serialize callbacks (§12).

---

## 7. The Game module interface

The game hands the engine a vtable. The engine calls these; they are the *only*
entry points from engine into game.

```c
// game/module.h
typedef struct {
    void  (*init)    (Engine *e, void **gameState);   // build World, register content/actions/parsers
    void  (*fixed)   (Engine *e, void *g, double dt);  // sim tick (host/solo authoritative)
    void  (*frame)   (Engine *e, void *g, double dt);  // input, camera, per-frame visual state
    void  (*render)  (Engine *e, void *g, RenderFrame *out); // fill the frame's draw list
    void  (*ui)      (Engine *e, void *g, int w, int h);     // HUD/menus via engine UI prims
    void  (*net_send)(Engine *e, void *g);                   // serialise + Eng_NetSend
    void  (*net_recv)(Engine *e, void *g, const uint8_t *p, size_t n);
    void  (*shutdown)(Engine *e, void *g);
} GameModule;

GameModule Game_Module(void);   // the one symbol game/main.c hands to the engine
```

`game/main.c` in full:

```c
int main(int argc, char **argv) {
    EngConfig cfg = { .w = 1280, .h = 720, .title = "Claude Zombies", .argc = argc, .argv = argv };
    Engine *e = Eng_Init(&cfg);
    Eng_Run(e, Game_Module());
    Eng_Shutdown(e);
    return 0;
}
```

Everything currently inline in `main.c`'s loop — spectator camera, walk bob,
ADS FOV lerp, post-FX HP ramps, action edges — moves into `frame`/`render`,
because all of it is *game* policy.

---

## 8. The render seam (the hard part — do it properly)

You said the engine should fully own rendering. That means the game stops
calling `rlgl`/`DrawModel` directly (today `render.c`, `viewmodel.c`,
`particles.c`, `decals.c` all do). Instead the game **describes** the frame and
the engine **renders** it.

```c
typedef enum { LAYER_WORLD, LAYER_SKINNED, LAYER_VIEWMODEL, LAYER_TRANSPARENT } RenderLayer;

typedef struct {
    ModelHandle model;        // OR animInstance for skinned
    AnimInstance anim;        // 0 for static models
    Matrix transform;
    Color  tint;
    RenderLayer layer;
} DrawItem;

typedef struct {
    Camera camera;
    LightingParams lighting;  // fog start/end/color, sun dir/color, ambient
    DrawItem items[MAX_DRAW_ITEMS];
    int      count;
    PostFxParams postfx;      // hitFlash, lowHp, time
} RenderFrame;
```

Engine render flow (game never sees a `BeginMode3D`):

```
Eng_Run each frame:
    game.render(e, g, &frame)            // game fills items + camera + lighting + postfx
    Eng_RenderFrame(e, &frame)           // engine: postFX RT → sky → world → skinned
                                         //   → viewmodel pass → composite → present
    game.ui(e, g, w, h)                  // HUD on top, outside postFX
```

`Eng_RenderFrame` absorbs everything in today's `Render_World3D` +
`Render_BeginPostFX`/`EndPostFX` + the shader-uniform pushes + the
viewmodel-on-top depth-clear trick. The game's `render` callback is pure
translation from `World` → `DrawItem[]` (this is what `render.c`/`viewmodel.c`
become — but emitting data, not GL calls).

**Honest cost:** this is the most invasive part of the whole plan. Every draw
site moves behind the submission API, and a few raylib idioms (immediate-mode
`rlPushMatrix` tricks, the viewmodel depth clear) need an engine-side
equivalent. Do this layer **last** (§14) once the cheaper wins are banked.

**World-space text labels** (`Render_WorldLabels`) need an engine primitive too:
add `Eng_RenderBillboardText(frame, worldPos, str)` so the game doesn't reach
for `GetWorldToScreen` itself.

---

## 9. The animation system (engine-owned)

Good news: `anim.c` is *already* the right design — a shared `AnimModel`
(model + clips) plus a lightweight per-instance `AnimState`. To make the engine
own animation fully, move the instance pool inside the engine and hand the game
opaque handles. The game says "play walk on this instance"; the engine does all
posing, bone matrices, and GPU skinning.

```c
typedef struct { uint32_t id; } AnimInstance;  // 0 = invalid

AnimInstance Eng_AnimSpawn(Engine*, ClipSetHandle set);
void Eng_AnimPlay (Engine*, AnimInstance, const char *clip, bool loop, float speed);
void Eng_AnimBlend(Engine*, AnimInstance, const char *clip, float weight);  // future: locomotion blends
bool Eng_AnimFinished(Engine*, AnimInstance);
void Eng_AnimDespawn(Engine*, AnimInstance);

// Bone attach (the gun-on-hand.R trick) — engine resolves the matrix:
Matrix Eng_AnimBoneMatrix(Engine*, AnimInstance, const char *bone);
```

`Eng_AnimUpdate` is called by the engine each frame for all live instances;
the game never calls `UpdateModelAnimationBones` or touches `model.transform`.
Skinned draws go through the render frame (`LAYER_SKINNED`, with the
`anim` field set). The current `Anim_Pose` + manual-transform escape hatch for
the first-person viewmodel becomes a `LAYER_VIEWMODEL` draw item with a
caller-supplied transform — same capability, no leaked internals.

This is the subsystem closest to done; it mostly needs the pool relocation and
the `AnimModel`/uniform globals pulled inside the engine.

---

## 10. Audio model

Already covered in §2/§6. The principle: **the game knows *when*, the engine
knows *how*.** Replace `Audio_Tick(me)`'s state-diffing with explicit game-side
events at the moment the rule fires:

```c
// in weapons.c when a shot actually goes out:
Eng_AudioPlay(e, sfx_fire[weaponId], muzzlePos, 1.0f);
```

The host already knows these moments. Clients reconstruct them from the
snapshot diff *on the game side* (in `net_recv`/`protocol`), then fire engine
events — so the diff logic stays game-side where it belongs, and the mixer is
clean.

---

## 11. Content / asset pipeline

The data files (`data/maps`, `data/weapons`, `data/models`, `data/shaders`)
already make *content* well-isolated from code — that half is done. The change
is on the loading side:

- Engine provides byte-loading + path probing + handle dedup + the name-keyed
  texture cache (today's `Assets_GetTextureByName`).
- Game registers parsers (`Eng_RegisterContentType(".weapon", parse_weapon, …)`)
  so weapon/map files load through the engine but are *interpreted* by the game.
- `MapDoc` is the model: a pure document the engine can load and the game (or
  the editor) instantiates into a `World`.

---

## 12. Networking split

- **Transport** (`net.c`) → engine. Connect/poll/send/broadcast. Game-agnostic.
- **Protocol** (`protocol.c`) → game. It encodes the `World` schema (`PktInput`,
  `PktSnapshot`, …) — that is inherently game knowledge and must stay game-side.

The engine may offer a thin replication helper (game provides
`serialize(World*)` / `apply(World*, bytes)` callbacks via the `net_send` /
`net_recv` module hooks), but it never learns what a player or a zombie is.
This is the one place that legitimately straddles — keep it on the game side
and don't try to purify it.

---

## 13. The types.h problem

`types.h` is currently a god-header mixing engine-ish types (`Camera` usage,
constants) with pure game types (`Player`, `Enemy`, `WeaponDef`, `Perk`).
Split it:

- `engine/` headers define engine types (handles, `RenderFrame`, `DrawItem`,
  `EngConfig`, `NetEvent`, math).
- `game/world.h` + `game/content/*.h` define `Player`, `Enemy`, `WeaponDef`,
  `PerkDef`, `RoundState`, the enums for `UiState`/`GamePhase`, etc.

A struct is a *game* type if the engine would have no opinion about it when
hosting a different game. `Player` is game. `Camera` is engine. `MapDoc` is
engine (it's a generic document). When unsure, ask: "would a walking-sim built
on this engine need it?" If no, it's game.

---

## 14. Migration plan (the game keeps running the whole way)

Do **not** big-bang this. Each phase compiles and plays.

**Phase 0 — bundle, don't move.** Introduce `Engine` and `World` structs and
put the existing globals *inside* them, but keep the old extern names alive as
macros (`#define players (g_world.players)`) so nothing breaks. This is pure
plumbing and instantly lets you pass `&g_world` around incrementally.

**Phase 1 — invert the two leaks** (§2). Make `audio` event-driven and
`content` stop iterating weapon models. After this the *dependency direction*
is correct even if files haven't moved. This is the highest-value phase.

**Phase 2 — input action map.** Replace raw `IsKeyPressed`/`Bind_*` scattering
with `Eng_Input*`. Collapses `HandleLocalActions`.

**Phase 3 — move the clean modules** to `engine/`: `mapdoc`, `net`, `anim`
(relocate instance pool), `fx` (particles+decals). These have few or no game
deps already.

**Phase 4 — content registry.** Handle-based assets + game-registered parsers.

**Phase 5 — the render seam** (§8). Introduce `RenderFrame`/`DrawItem`, move all
GL/`rlgl` calls behind `Eng_RenderFrame`, convert `render.c`/`viewmodel.c` into
frame-builders. Largest phase; do it alone.

**Phase 6 — flip `main.c`.** Extract `engine/app.c` loop + `GameModule` vtable;
move the gameplay body into game callbacks; delete the Phase-0 macros once no
bare globals remain.

**Phase 7 — directory split + enforce.** Physically move files into
`engine/`/`game/`, add the §2 grep to CI.

After each phase: `--screenshot-*` dev modes and a quick playtest are the
regression check (they already exercise render/anim/audio paths headlessly-ish).

---

## 15. Definition of done

- `grep` in §2 returns nothing.
- `src/engine/` builds into a static lib (`libengine.a`) with **no** game
  sources in its `add_library`. The game links against it.
- You can instantiate a `World` from a `MapDoc` and tick `fixed()` with **no
  window open** (headless sim test).
- `main()` is < 20 lines and lives in `game/`.
- No file outside `engine/` includes `rlgl.h`; no file in `engine/` includes a
  game header.

---

## 16. What this unlocks

- **The 3D map editor** (your stated goal): it's just a *second* `GameModule`
  hosted by the same engine — reuse render/content/input/anim, swap the
  simulation for editing tools. `MapDoc` + headless `World` instantiation is
  the foundation that makes this cheap.
- **Headless tests**: tick the sim, assert on `World`, no GPU.
- **Faster iteration**: subsystems become independently testable and swappable
  (e.g. a new renderer backend touches only `engine/render`).
- **Reuse**: a future second game starts from `libengine.a`.

---

## 17. Open decisions

1. **UI**: keep raygui (wrap it as `Eng_Ui*` primitives the game draws with),
   or let the game keep calling raygui directly as an allowed engine-adjacent
   dependency? Recommend wrapping, so a non-raygui backend stays possible.
2. **Render purity**: full `DrawItem` submission (§8, recommended, reusable) vs
   the cheaper "engine owns passes, game still issues draws between begin/end".
   You asked for full engine ownership → go with submission, accept the cost,
   do it in Phase 5 only.
3. **Fixed vs frame timestep**: the host sim (`Game_Tick`) currently runs at
   frame rate. Worth deciding if `fixed()` should run on a fixed accumulator
   while migrating, since the loop is being rewritten anyway.
4. **Static lib vs same-target dirs**: `libengine.a` enforces the seam at link
   time (engine literally cannot see game symbols). Stronger than the grep —
   recommended for the end state.
```
