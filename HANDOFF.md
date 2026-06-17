# Handoff — Claude Zombies

For the next Claude agent picking up this project. Read this first, then
`README.md` for player-facing context, then `TODO.md` for what's next.

## What this is

A 3D round-based zombies shooter in C using **raylib 5.5**, **raygui 4.0**,
and **enet 1.3.18** (all via CMake `FetchContent`). Host-authoritative
4-player coop over UDP. Cross-platform: Linux (X11), macOS, Windows
(MSVC + MinGW).

Repo: `git@github.com:jude2k6/claude-zombies.git` · branch: `main`.

## Current state

- **Engine/game split complete (§15).** Source tree is `src/engine/` (→ `libengine.a`, zero game knowledge) + `src/game/`. `src/game/main.c` is ~10 lines: `Eng_Run(&cfg, Game_Module())`. Headless sim: `./build/shooter --sim-tick <map>`. CI runs seam-check + build + `--validate` + `--sim-tick` on every map.
- **MP5 is the only gun with a combined viewmodel rig** (`data/weapons/smg/smg_vm.glb` — arms + gun + mechanism in one skinned glTF). Pistol/shotgun/rifle/raygun fall back to the shared `arms_vm.glb` + gun bolted to `hand.R` (`vm_grip_*` seating). **Next: author combined rigs for those 4 guns** using the MP5 recipe in the `blender-game-asset` skill, then retire the bolt-on path.
- **`NET_PROTO_VERSION` is 11** (`src/engine/net.h`). `SerPlayer` carries `fireHeld` (uint8_t) and `reviverIdx` (int8_t).
- **Zombie + player third-person rigs** (`zombie.glb`, `player.glb`) are wired and animating. Death/attack clips are sim-driven via `dyingTimer`/`simAttackTimer`. Region-BFS zombie nav over the sector graph is live (`CrossFloorGoalFull` in `entities.c`, driven by `RAMP … LINK` edges).
- **Post-FX render target** with bloom/vignette/hit-flash/low-HP pulse is live (`data/shaders/postfx.fs`). Particles (`src/engine/particles.c`), decals (`src/engine/decals.c`), full audio pass, equipment system, and weapons data-driven via `.weapon` files are all in.
- **Map editor** (`src/editor/editor_main.c`) is a second `GameModule` on the engine (links `libengine.a`, zero game deps). Fly/orbit/top views, place/select/move entities via the `pick`/`gizmo`/`mapedit` toolkit, undo/redo, grid snapping, a persisted settings overlay (`editor.cfg` via `cfg.h`), and a `MapDoc_Validate` geometry-check overlay. Headless `./build/editor --check <map>` validates + exits non-zero on error. **No map *save* (`MapDoc_Save`) UI yet.** See `docs/scene-builder.md`.

## Build / run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shooter
./build/shooter --validate data/maps/nacht.map  # game-side map check
./build/editor data/maps/nacht.map              # map editor (GUI)
./build/editor --check data/maps/nacht.map      # headless parse + validate
```

First Linux build needs X11 dev headers — see `README.md`. The CMake
target has a `POST_BUILD` step that copies `data/` next to the
executable. **It only fires when the `shooter` target rebuilds** — if
you only changed a `.map`, a texture, a shader, or a model with no code
changes, the copy doesn't run; manually `cp -r data/ build/` or touch a
source.

`settings.cfg` lives next to the binary (i.e. inside `build/` when run
from the build dir). On first run with no file, defaults are written.

## Architecture

The tree is split into a reusable **`src/engine/`** (→ `libengine.a`) and the
game in **`src/game/`** (which links it). `src/game/main.c` holds `main()` + the
per-frame gameplay body; `src/engine/app.c` owns the window / frame-loop / time
and hosts the `GameModule` vtable. Engine modules (zero game knowledge):
`app gfx anim audio decals fx mapdoc net pad particles`. Game TUs share
`src/game/types.h` and are one translation unit per responsibility:

| File              | Owns                                                   |
|-------------------|--------------------------------------------------------|
| `types.h`         | All shared structs/enums/constants (incl. `MapProp`, bumped `MAX_*`) |
| `world.{c,h}`     | `g_world` — the `World` struct holding all game state (players/enemies/bullets/level/round/timers); the single state owner introduced by the engine split |
| `assets.{c,h}`    | `propModels[]`, `textures[]`, `worldShader`, `skyShader`, fog globals, `TILE_SIZE`. (Weapon storage moved out — see `weapons.{c,h}`.) |
| `level.{c,h}`     | Map tokeniser + parser, `Level_Validate`, `obstacles/walls/doors/windows/mapProps`, `Level_Reset`, `UnstickXZ`, `Level_PathClearXZ` (AI lookahead), `PROP_DEFS[]` |
| `player.{c,h}`    | `players[]`, look/move, sprint/crouch, stamina, sprintBlend, push-out |
| `weapons.{c,h}`   | `WEAPONS[]`/`weaponTune[]`/`weaponGrip[]` (plain storage — `.weapon` files are the single source of truth, loaded by `Weapons_Load`), `weaponModels[]`, fire/reload/melee, fire-mode + recoil + dropoff + sfx/haptic/mbox-weight routing |
| `entities.{c,h}`  | `enemies[]`, `bullets[]`, `powerUps[]`, per-type zombie AI (lunge/weave/steamroll), proactive obstacle probing, damage→downed→dead transition |
| `interact.{c,h}`  | Wall-buys, perks, PaP, doors, repairs, revive, Mystery Box |
| `perks.{c,h}`     | Perk effects (jug/speed/dtap/stamin)                   |
| `game.{c,h}`      | `Game_Tick` — orchestrates a server tick, round breaks, respawn-on-round-start |
| `protocol.{c,h}`  | Serialization (`SerPlayer`, `SerEnemy`, `PktSnapshotHeader`) |
| `net.{c,h}`       | enet wrapper, `Net_GetLocalIPs` for the host's join prompt |
| `render.{c,h}`    | 3D world draw (model-first with cube fallbacks), textured walls/floor via rlgl, fog/sky/lit shader swap, tracer billboards |
| `viewmodel.{c,h}` | First-person viewmodel. Primary path: per-weapon **combined rigs** (`<id>/<id>_vm.glb` = arms+gun+mechanism in one skinned glTF) via `Viewmodel_LoadCombinedRigs` + `DrawCombinedRigViewmodel`, framed by shared `CRIG_*` constants. Fallback order: combined rig → shared `arms_vm.glb` + gun bolted to `hand.R` (`weaponGrip[]`/`vm_grip_*`) → gun-only OBJ. Only the MP5 has a combined rig so far; the other 4 guns use the arms fallback. `Viewmodel_DrawFirstPerson(Camera)` |
| `devtools.{c,h}`  | The 5 CLI debug/validation modes (`--validate`, `--screenshot-viewmodels`, `--screenshot-coop`, `--screenshot-pap`, `--anim-test`) behind `Devtools_HandleCLI(argc, argv, &exitCode)` |
| `decals.{c,h}`    | 96-slot ring buffer for blood + impact decals, drawn from `Render_World3D` |
| `anim.{c,h}`      | Skeletal animation pipeline. `AnimModel` (shared skinned glTF + clips) + per-instance `AnimState`; load/find-clip/update/draw via raylib 5.5 GPU skinning. `Anim_Pose` (pose without draw, for custom transforms), `Anim_FindBone`/`Anim_BoneMatrix` (bolt a separate object — e.g. a gun — onto a skeleton bone) |
| `hud.{c,h}`       | 2D HUD overlay, round splash, downed/spectate overlays |
| `menu.{c,h}`      | Menu screens incl. game-over stats table, controller bindings UI, map picker |
| `pad.{c,h}`       | Raw gamepad axis/button readers                        |
| `settings.{c,h}`  | `bindButton[]` runtime bindings, `Bind_Pressed/Down/PollAny`, `Settings_Load/Save` |
| `audio_director.{c,h}` | Game-side audio event detection (snapshot-delta diff + timers); fires `Audio_*` calls. The mixer itself is `src/engine/audio.{c,h}` (pure, positional). |
| `fx.{c,h}`        | Camera shake (no real particle system yet)             |

Data layout under `data/`:

```
data/
├── audio/                       # (nothing here yet; ATMOSPHERE music: parses but isn't loaded)
├── maps/*.map                   # new format; --validate to check
├── models/                      # props/decor (OBJ) + rigged characters (glTF)
│   ├── ASSETS.md                # spec for every prop + texture + shader
│   ├── *.obj/*.mtl              # static props (mystery box, doors, perks, …)
│   ├── zombie.glb               # rigged enemy (walk/attack_a/death)
│   ├── player.glb               # rigged co-op third-person soldier
│   └── arms_vm.glb              # shared first-person arms — fallback for guns lacking a combined rig
├── weapons/                     # per-folder weapon defs + assets
│   ├── pistol/{pistol.weapon,pistol.obj,pistol.mtl,pistol_vm.legacy.glb}
│   ├── smg/{smg.weapon,smg.obj,smg.mtl,smg_vm.glb,smg_vm.blend}   # only combined rig so far
│   ├── shotgun/...
│   ├── rifle/...
│   └── raygun/...               # guns 2–5 still need <id>_vm.glb combined rigs
├── shaders/{world,sky}.{vs,fs}
└── textures/*.png               # 5 seamless 1024² PNGs (floor/ground/wall_ext/wall_int/ceiling)
```

## Conventions & gotchas

- **Host-authoritative.** Clients send `PktInput`; host runs sim and
  broadcasts `PktSnapshot` at 20 Hz. Local-only fields (stamina,
  sprintBlend, godMode, noclipMode) live on/around `Player` but aren't
  serialized.
- **Bump `NET_PROTO_VERSION`** in `net.h` any time you change a
  serialized struct in `protocol.{c,h}`. The handshake rejects
  mismatched versions.
- **Map file path resolution** tries several relative locations and
  falls back to hardcoded geometry. Don't assume CWD. Same pattern for
  models (`data/models/`, `../data/models/`, `./data/models/`),
  textures, and shaders.
- **rlgl shader swap** — `EndWorldShader` **must** pass
  `rlGetShaderLocsDefault()` as the second arg to `rlSetShader`, not
  `NULL`. Passing `NULL` leaves `currentShaderLocs` dangling and
  `EndDrawing`'s render-batch flush SEGVs (it dereferences
  `currentShaderLocs[RL_SHADER_LOC_MATRIX_MVP]`). This was the
  menu-launch crash.
- **Blender OBJ axis quirks** (documented in ASSETS.md but easy to
  re-hit):
  - Author with Blender Z up, model facing **-Y**. Export with
    `forward_axis='Z'` (NOT `'NEGATIVE_Z'`, despite what the name
    suggests) so Blender -Y maps to OBJ -Z. Blender +X gets mirrored
    to OBJ -X; bake asymmetric details accordingly.
  - `primitive_cube_add(size=1)` already gives a unit cube. Setting
    `o.scale = (sx, sy, sz)` gives final size `sx × sy × sz` — do
    **NOT** multiply by 0.5. (That was the "exploded zombie" bug.)
  - For MTL `Kd` to carry, the material must `use_nodes = True` and
    the colour set on `Principled BSDF → Base Color`. Plain
    `material.diffuse_color` is viewport-only.
- **`-Wall -Wextra` are ON** (GCC/Clang, CMakeLists.txt). `SHOOTER_ASAN=ON`
  adds `-fsanitize=address,undefined`. Remaining: a handful of
  `-Wstringop-truncation` warnings from `strncpy` in
  net.c/level.c/settings.c/main.c — harmless, not yet addressed;
  everything else is clean.
- **raylib 5.5 OBJ loader — both known bugs are now PATCHED**
  via `cmake/patch_raylib_obj.cmake`, applied as a `PATCH_COMMAND` on the
  raylib `FetchContent_Declare`. Historic context (the patch makes both moot):
  - *No-UV read:* `LoadOBJ` read `texcoords[vt_idx*2]` for OBJs with no `vt`
    (all of ours) → wild OOB read. Now guarded + zero-filled. **You may add
    sanitizers now** (old blocker is gone), and don't bother adding UVs to
    "fix" anything.
  - *N-gon overflow:* tinyobj's `parseLine` read an `f` line into a fixed
    `f[16]` before triangulating; our cap n-gons (pistol 20-vert, shotgun
    24-vert) overflowed it. Limit raised to 256. `export_triangulated_mesh=True`
    is still good hygiene but **no longer required for crash-safety** — un-
    triangulated n-gons load fine.
  - If you bump raylib's `GIT_TAG`, re-verify the patch matches (the script
    aborts loudly with a clear message if the upstream source moved).
- **Skeletal animation pipeline.** Engine support for animated models is in.
  Key facts:
  - Animations need **glTF (`.glb`)**, NOT OBJ — OBJ can't carry a skeleton.
    raylib 5.5 GPU skinning is used (`UpdateModelAnimationBones` writes only
    per-instance bone matrices; `DrawMesh` uploads them to the `boneMatrices`
    uniform — cheap enough to draw all 48 zombies at different poses).
  - `anim.{c,h}`: `Anim_Load/Unload/ApplyShader/FindClip/Play/Update/Draw`.
    `AnimModel` is the shared asset (model + clips); `AnimState` is per
    instance (clip/time/loop/speed). glTF clips are baked at ~60 fps
    (`ANIM_FPS = 1000/17`), so playback is driven in seconds.
  - Animated models must use **`worldSkinnedShader`** (`world_skinned.vs` +
    the shared `world.fs`) via `Anim_ApplyShader`, so they get the same fog +
    lighting. `BeginWorldShader` pushes the fog/sun uniforms to it each frame.
  - Validate any `.glb` with **`./build/shooter --anim-test <file.glb> [clip]`**
    (writes `anim_test_0..3.png` across the clip) or add `--live` for an
    interactive spinning viewer.
  - Engine work per new animated entity: load AnimModel, `Anim_ApplyShader`,
    store an AnimState, `Anim_Update` each tick, `Anim_Draw` instead of
    `DrawProp`.
- **Rigged zombie (`zombie.glb`).** Template for every future rigged model.
  - **Body = one skin-modifier island; head = separate modeled parts.** The
    torso/limbs are a skin-modifier stick figure (single watertight island,
    auto-weighted, deforms cleanly). The **head is modeled geometry** (skull
    box + brow/cheekbones/eye sockets/maw/teeth/chin) — each a separate
    beveled box, overlapping the skull so the audit's floating-part check
    passes, each rigidly bound to the `head` bone. Pure skin-modifier heads
    come out as featureless blobby wedges — model the face as real geometry.
    Each separate part must stay ONE island (don't `join` boxes — that makes
    multi-island objects = audit FAIL) and must touch/overlap a neighbour.
  - **Facing — author +Y in Blender.** glTF `export_yup=True` maps Blender
    **+Y → raylib -Z** (the forward/look direction the enemy yaw expects).
    This is the OPPOSITE of the OBJ `forward_axis='Z'` convention — don't
    cross them.
  - **AnimState is render-local, not on `Enemy`.** `render.c` keeps
    `zombieAnimState[MAX_ENEMIES]` indexed by `e - enemies` and ticks it with
    `GetFrameTime()` in `DrawEnemy`. Intentional: it's purely visual, so it
    stays out of `SerEnemy` (no protocol bump). If you ever need teammates to
    see synchronised attack/death frames, that's when it moves into serialized
    state.
  - **Uniform scale only** (`Anim_Draw` takes one float). Boss uses 1.7×,
    crawler 0.7× — they can't be Y-squished like the old cube path. The
    yellow runner stripe + magenta boss stripe + runner red-eye overlays are
    still drawn on top of the rigged model.
  - **Death/attack are sim-driven.** Kills set `Enemy.dyingTimer` (1.34 s;
    corpse plays `death` clip then slot frees); bites set
    `Enemy.simAttackTimer` (0.61 s) driving `attack_a`. Both serialized in
    `SerEnemy`. Dying corpses are skipped by AI/bullets/collision/round-end/
    audio — **if you add a new kill site, set `dyingTimer` before flipping
    `alive=false`** (see the 6 existing sites).
- **Knee direction.** Both `player.glb` and `zombie.glb` require a NEGATIVE
  shin-X rotation for anatomically-correct knees (foot tucks backward+up,
  toward the back of the thigh). A positive shin-X swings the foot forward
  (knee juts back). Verify empirically per rig (rotate `shin.L ±55°`, render
  a side view) — the sign depends on bone roll. The `blender-game-asset` skill
  has an "anatomically/mechanically correct" non-negotiable + a joint-direction
  calibration step. Also: delete any stray objects (e.g. `Icosphere`) from the
  Blender scene before exporting — `use_selection=False` bakes the whole scene.
- **Rigged player third-person model (`player.glb`).** Wiring notes for
  `render.c:DrawOtherPlayer`:
  - **Only OTHER players use it** — the draw loop skips `localPlayerIdx`. The
    `AnimState[]` is render-local (not in `SerPlayer`); no protocol bump.
  - **Clip is reconstructed from synced fields.** Locomotion uses a smoothed
    horizontal speed from per-frame `pos` delta (snapshots are 20 Hz, raw
    delta spikes — an EMA of `dist/dt` recovers the true average). `reload`
    reads `inventory[currentSlot].reloadTimer`. `revive` reads the serialized
    `reviverIdx` (set on the DOWNED player = "who is reviving me"): the
    reviver is whoever a downed teammate points back at — authoritative, no
    proximity guess. `fire` keys off `fireHeld`, serialized in `SerPlayer`
    (proto 11).
  - **Ground clips own the lay-down.** `downed`/`death` lower the `pelvis` to
    the floor in the animation itself. The draw passes `feet.y = pos.y -
    PLAYER_EYE` (the player's floor; 0 on flat maps, the deck height on
    multi-floor maps) with NO tilt hack.
  - **Team colour is a *lightened* wash** (`128 + c/2` toward white), not the
    raw team colour — a full-saturation multiply would flatten the soldier's
    materials. Dead/downed keep the existing grey/red `c`.
  - **Verify without MP:** `./build/shooter --screenshot-coop` → `coop_*.png`.
- **Combined viewmodel rig (MP5 — `smg_vm.glb`).** The proven recipe for all
  future guns. One rigged glTF: arms + hands + gun + mechanism bones (e.g.
  `bolt`, `magazine`), hands authored ON the gun in the idle/rest pose so
  there's no runtime seating, no `vm_grip_*`, no IK. Engine plays clips on
  the skinned model via `DrawCombinedRigViewmodel`. Framing constants at the
  top of that function: `CRIG_FWD_OFFSET 0.35`, `CRIG_DOWN_OFFSET −0.08`,
  `CRIG_RIGHT_OFFSET 0.10`, scale 0.9. Author origin-at-eye, +Y-forward
  (`export_yup=True`), real metric.
  - **Framing debug:** a combined rig authored origin-at-eye is *invisible*
    if anchored exactly at the camera (you're embedded in it / clips the
    lens). This is NOT a shader/skinning bug — isolate by drawing the model
    via `Anim_Draw` a few metres ahead. If it shows there, it's purely the
    framing transform. Push off the lens with `CRIG_FWD_OFFSET`.
  - **Worktree agent gotcha:** a sandboxed agent's worktree may branch from a
    stale base — its branch diff can be useless. Extract the real change by
    diffing the worktree file against `main`.
- **Shared arms viewmodel + bone-bolted gun (`arms_vm.glb`) — now the
  FALLBACK path.** Runs for guns lacking a `<id>_vm.glb`. Implementation
  notes for `viewmodel.c:DrawArmsViewmodel`:
  - **The attach math.** `Anim_BoneMatrix(am, st, boneIdx)` returns the
    bone's model-space transform at the current frame; the gun's world matrix
    is `gunLocal * bone * root` (raylib `MatrixMultiply` applies the LEFT
    operand first, so the chain reads inner→outer). `root` is the camera-space
    matrix the arms are drawn with. `gunLocal` first rotates the gun OBJ +90°
    about X (OBJ `-Z` muzzle / `+Y` up → `+Y` muzzle / `+Z` up to match the
    arms-forward frame), then applies the per-gun `gunGrip[wi]`
    `scale`/`rotDeg`/`pos` nudge in hand-local metres.
  - **The hands never sit cleanly on the guns on this path.** The gun and
    debug markers both ride `hand.R` via `Anim_BoneMatrix` (self-consistent),
    but the *visible mesh* isn't tracking them. **Do NOT keep tuning
    `vm_grip_*` to chase the gap; the fix is in the asset.** The proper fix
    is authoring a combined rig (then this path is retired for that gun).
    Re-check with `--screenshot-viewmodels` (grip markers: red = hand.R +
    axes, blue = hand.L; stderr `vmdbg` lines dump hand-bone positions); no
    recompile needed.
  - **Two shaders, two flat-light overrides.** Arms use `worldSkinnedShader`;
    the gun keeps its OBJ `worldShader`. Each gets its own flat sun/ambient
    override (restored after) so neither colour-swings.
  - **`idle_pistol` / `inspect` clips are NOT in `arms_vm.glb`.** Asset ships
    7 clips: `fire idle lower raise reload reload_empty sprint`. The
    `vm_pose PISTOL` key in `pistol.weapon` resolves to -1 and is a no-op
    (pistols fall back to the two-handed `idle`).
  - **Falls back gracefully.** If `arms_vm.glb` is missing or has no `hand.R`
    bone, drops through to the legacy gun-only OBJ viewmodel — nothing crashes.
- **Blender viewmodel rig rotation mode.** Keep ONE rotation mode per rig
  (current rigs are XYZ euler). Flipping a bone to euler for a calibration
  test then keyframing `rotation_quaternion` = silently-ignored keys (finger
  open/close was a no-op until caught this way).
- **CoD-style balance pass.** Numbers are tuned to classic Treyarch zombies —
  don't "simplify" them back:
  - **Zombie HP** (`entities.c::Enemies_RoundHP`): `150 + 100*(r-1)` through
    round 9 (R1=150 … R9=950), then `*1.1` compounding each round from 10.
  - **Player damage** (`types.h::ENEMY_DAMAGE=50`): ~2 hits down at 100 HP,
    ~5 with Juggernog (250). Per-type mult: runner ×0.8, crawler ×1.0,
    boss ×2.0.
  - **HP regen**: `REGEN_DELAY` 4 s damage-free, then `REGEN_RATE` 110 HP/s
    back to max. Driven by `Player.regenTimer` (local, not serialized — reset
    at every damage site; runs host-side in `Game_Tick`). Downed players
    don't regen.
  - **Points**: 10 / hitmarker, 60 body kill, 100 headshot kill, 130 melee.
    Don't re-add a non-kill headshot bonus.
  - **Weapon stats** live ONLY in the `.weapon` files (no compiled-in stats).
    Base damages: M1911 40, MP5 35, Olympia 60/pellet, M14 100 (SEMI),
    Ray Gun 1000 + 500 splash. **Pack-a-Punch** = damage ×2.5, mag ×2,
    reserve ×2 (`weapons.c::Weapon_Eff*`).
- **Two independent weapon scales.** `weaponTune.scale` (`model_scale` in
  `.weapon`) is the **first-person viewmodel framing knob only** (viewmodel
  base × `weaponTune.scale`, base currently 0.05). World draws — wall-buy,
  Mystery Box, PaP floater — go through `DrawWeaponDisplay` at literal world
  scale (1.0 = life-size), ignoring `weaponTune.scale`. Don't reintroduce the
  `displayScale * weaponTune.scale` multiply. The viewmodel is drawn under
  flattened lighting (low sun / high ambient, restored after) so its facets
  don't swing dark↔bright as the camera turns.
- **Weapon model sizing.** Anything authored in metric metres needs
  `model_scale` in the 10–15 range to display at the same on-screen size as
  the old Quaternius set (which was ~10 units/inch). All current `.weapon`
  files have `model_scale ≈ 10–12` for this reason.
- **Map engine limits** (current): `MAX_INTERIOR_WALLS=32`, `MAX_DOORS=8`,
  `MAX_WINDOWS=4`, `MAX_OBSTACLES=24`, `MAX_MAP_PROPS=32`. Windows must be on
  arena perimeter walls (engine renders gaps in those for windows).
- **Spawns must not overlap an OBSTACLE or PROP's XZ projection** — collision
  is XZ-only, ignores box Y extent. `UnstickXZ` will rescue the player but
  the spawn flash looks bad.
- **Door lintel walls are no-clip for movement.** Each parsed `DOOR` emits a
  header/lintel wall above the opening into `interiorWalls[]`.
  `interiorWallNoClip[]` (parallel, set on the two lintel emit sites) is
  skipped by the three XZ collision paths in `level.c` but still rendered +
  bullet-collided (the bullet test is Y-aware). Don't add new full-height
  geometry above a walkable gap without flagging it no-clip.
- **Settings persistence**: `Settings_Save` is called on graceful exit, on
  closing the Settings screen, and after every binding change. SIGTERM/SIGKILL
  won't save.
- **rand()** — `interact.c` uses it for Mystery Box rolls; needs
  `#include <stdlib.h>`.
- **rlgl** — if you use `rlPushMatrix`/`rlTranslatef`/`rlSetShader`, include
  `rlgl.h` explicitly. raylib doesn't pull it in.
- **Spectator mode** — when `!me->alive`, `main.c` runs a 3rd-person spectate
  cam over a live teammate's shoulder. F / A / LMB / Jump bind cycles to the
  next teammate. Auto-respawns at next round start.
- **Noclip = out-of-body fly cam** (F4 / R3). The body is left behind FROZEN
  at the spot/facing where you toggled noclip on. While noclipping, the local
  player's own third-person body IS drawn (`DrawOtherPlayer` skips
  `localPlayerIdx` only when `!noclipMode`) and the first-person viewmodel is
  suppressed (`Viewmodel_DrawFirstPerson` bails on `noclipMode`). Toggling off
  snaps the camera back to the body.
- **Round 1 skip** — was a real bug, fixed by initializing
  `roundNum=0; gamePhase=GS_ROUND_BREAK` so `Game_Tick` rolls it into
  `Game_StartRound(1)`. Don't regress.
- **Commit policy**: always commit completed work to `main`; never `git push`
  — the user pushes themselves. Don't ask before committing. Terse subject +
  bulleted body.
- **Bob phase sync.** `ViewmodelMotion` must not run its own oscillator — pass
  the phase from `GameMod_Frame` via `Viewmodel_SetBobPhase`. Separate
  oscillators drift apart visually.
- **Viewmodel swap edge detection.** The combined-rig path and the shared-arms
  path must share ONE `Viewmodel_Edges()` edge-detect state. Separate
  function-`static` prev-state per path causes the raise clip to be skipped
  when swapping between a combined-rig gun and an arms-path gun and back.
- **Third-person body floor.** `DrawOtherPlayer` must use
  `feet.y = pos.y - PLAYER_EYE` (the player's floor), NOT a hardcoded
  `feet.y = 0`. Hardcoding floats bodies at the ground floor on multi-floor
  maps.
- **`fireHeld` and `reviverIdx` are now serialized** (`SerPlayer`, proto 11).
  `fireHeld` is input-only on the local player (live input); remote avatars
  adopt the host's value. `reviverIdx` is set on the DOWNED player (= "who is
  reviving me", in `interact.c`) — the reviver test is "does any downed player
  point back at me", not proximity.

## Controller bindings model

`BindAction` enum in `settings.h` has **16** entries; UI is
`Menu_DrawBindings`. See `settings.{h,c}` for the live mapping. Default
mapping mirrors the old hardcoded one (RT fire, LT ADS, Y reload, X
interact, RB melee, LB swap, A jump, B crouch, L3 sprint, Start pause,
Back hold scoreboard, R3 noclip) plus DPad-Up throw lethal, DPad-Down
throw tactical. Keyboard equivalents (hardcoded alongside binds in
`main.c`): G lethal, H tactical, F3 god, F4 noclip.

## Working style the user has shown

- Likes terse commits with a concise subject + bulleted body.
- Bundles multiple features in a single request ("fix it all", "add
  1 2 3 4 12 9"). Implement all in one batch; one commit per natural
  unit is fine.
- Will reject feel-changes if they go too far. Gets specific on layout
  iteration and asset detail level.
- Skips planning docs — when asked to spec, asks for a punch list
  instead.
- Replies with "fix it all" / "do those three" / "yes" — commits to
  full scopes when given options.
- Doesn't proofread; "explod" = exploded, "raygun" lower-case, "to do"
  = TODO. Just infer.
- Asks "what else needs to be added?" mid-stream — wants a survey,
  not a sales pitch. Be honest about gaps; rank by impact.

## Suggested next things

See `TODO.md` for the full live list. Impact-ordered short version:

1. **Combined viewmodel rigs for guns 2–5** — only the MP5 (`smg_vm.glb`)
   has one. Author `pistol/shotgun/rifle/raygun` `<id>_vm.glb` via the
   `blender-game-asset` skill's proven MP5 recipe, then retire the bolt-on
   fallback (`DrawArmsViewmodel`/`weaponGrip[]`/`vm_grip_*`/`arms_vm.glb`/
   gun-only OBJ). Decision: `docs/arms-rig-generalisation.md` §0.
2. **Author music + ambience .oggs** — the per-map music engine path is in
   (streams `data/audio/<name>.ogg` if present); no audio ships yet.
   nacht.map references `nacht_loop`.
3. **Zombie clip set + per-type variants** — `spawn`/`run`/`attack_b`,
   runner `lunge`, crawler `crawl`, boss `steamroll` (`data/ANIMATIONS.md`).
4. **`LIGHTS x y z r g b range`** in `.map` + `sky_tint` plumbing (both parse
   but aren't fed to the shaders yet).
5. **Parser tests** — `tests/map_parser_test.c` / `weapon_parser_test.c`.
   CI already runs `--validate` + `--sim-tick`; add fixture-based tests.

Tune-on-playtest flags: boss melee (100 dmg = 1-shot a no-Jug player from
full), HP regen rate/delay (110/s after 4 s), PaP damage ×2.5, and the
viewmodel base scale (0.05) — all single-number tweaks if they feel off.

## If something breaks

1. `cmake --build build -j 2>&1 | tail -40` — most issues are
   missing-include or implicit-decl warnings.
2. `./build/shooter --validate data/maps/<name>.map` — if a map won't
   load, this prints line-numbered errors and exits 1.
3. Run `./build/shooter` and check stdout for the
   `model: loaded …` / `texture: …` / `shader: …` /
   `map: loaded '<name>' from … (W walls, D doors, …)` block — confirms
   the asset pipeline + map parser are seeing what you expect.
4. For multiplayer issues, check `NET_PROTO_VERSION` matches between
   host and client builds.
5. For "stuck in geometry" reports, look for SPAWN points overlapping
   OBSTACLE / PROP XZ projections in the relevant `.map`. `UnstickXZ`
   will rescue but it's a smell.
6. **For a menu-launch SEGV**: check `EndWorldShader` in `render.c` —
   `rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault())`,
   not `NULL`.
7. If bindings UI doesn't capture: confirm `Pad_Connected()`. Without
   a pad, the screen shows the "(no controller detected)" hint.
