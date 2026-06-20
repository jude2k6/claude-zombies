# Engine / Game Separation — Design Rationale & Final State

> ## ▶ DONE — §15 definition of done is met (commit `6ae27ce`)
> The engine/game split is complete. The tree is `src/engine/` (a reusable
> static `libengine.a`) + `games/shooter/src/` (rules + content linking it). Build runs
> `seam-check`, a pre-commit hook enforces the §2 rule
> (`git config core.hooksPath scripts/hooks` if cloning fresh), and CI
> (`.github/workflows/ci.yml`) runs the seam check + build + `--validate` +
> `--sim-tick` on every map.
>
> **§15 done-criteria — all met:**
> 1. ✅ Seam grep returns nothing (`scripts/check-seam.sh`); no `src/engine/`
>    file includes a game header.
> 2. ✅ `src/engine/` builds into `libengine.a` with **zero game sources**
>    (`audio net mapdoc pad fx decals particles anim gfx app`); the `shooter`
>    executable links it.
> 3. ✅ A `World` is instantiated from a `MapDoc` and `Game_Tick`'d with **no
>    window** — `./build/shooter --sim-tick <map> [frames]` (devtools.c).
> 4. ✅ `main()` is 10 lines and lives in `games/shooter/src/main.c`.
> 5. ✅ No file outside `src/engine/` includes `rlgl.h`; no engine file includes
>    a game header.
>
> **Build + verify loop:**
> ```
> cmake --build build -j                            # seam-check → libengine.a → shooter
> ./build/shooter --sim-tick data/maps/nacht.map    # §15 litmus: headless tick, NO window
> ./build/shooter --screenshot-zombies              # g_world.enemies render path
> ./build/shooter --screenshot-coop                 # g_world.players[] + round state
> timeout 5 ./build/shooter                         # exit 124 = ran clean
> ```

For how to build on the engine (GameModule contract, `Eng_*` APIs, fixed-timestep
callback model), see **[`docs/engine-usage.md`](engine-usage.md)**.

---

## 2. The cardinal rule

**Engine code never includes a game header. Ever.**

That single constraint *is* the separation. Everything else is consequence.

Enforcement is a one-line grep that should return nothing — wired into CI and
a pre-commit hook:

```bash
# Engine must not know about game concepts.
grep -rEl 'weapons\.h|perks\.h|entities\.h|game\.h|player\.h|interact\.h|hud\.h|menu\.h' src/engine/ \
  && echo "SEAM VIOLATION" && exit 1
```

The two founding violations that drove the whole effort:

| File | Bad include | Why it's wrong | Fix | Status |
|------|-------------|----------------|-----|--------|
| `audio.c` | `game.h`, `level.h`, `entities.h`, `player.h` | the mixer pulled game state to decide what to play (`Audio_Tick(me)` diffed HP, round phase, box state) | invert to **push**: game fires `Eng_AudioPlay(clip, pos)` events; mixer just plays | ✅ done (mixer + game-side `audio_director.c`, commit `cd6989f`) |
| `assets.c` | `weapons.h` | the asset loader iterated `weaponModels[]` in `Assets_ApplyWorldShader` | invert: game **registers** which model handles want the world shader | ✅ done (`Assets_RegisterWorldShaderModel`, commit `c2111ee`) |

---

## 3. Directory layout (final)

```
src/
  engine/                 ← reusable, zero game knowledge → libengine.a
    eng.h                 master include (pulls subsystem headers)
    app.c/.h              window, frame loop, time, lifecycle, GameModule host
    content.c/.h          asset registry: models/textures/shaders/audio/anim
                          + registration of game content parsers
    eng_render.c/.h       postFX RT lifecycle, worldShader handles + uniform locs,
                          lighting bookend (Eng_RenderSetLighting/BeginWorld/EndWorld)
    gfx.c/.h              thin facade over rlgl: all 3D draw calls (Eng_GfxDrawModel,
                          BeginMode3D/EndMode3D, Cube, Sphere, Grid, …)
    anim.c/.h             skeletal animation runtime
    audio.c/.h            mixer: 3D positional, event-driven
    fx.c/.h / particles / decals   generic emitter + decal systems
    pad.c/.h              engine action map (keyboard + mouse + gamepad)
    net.c/.h              transport (enet wrapper)
    mapdoc.c/.h           generic map document
    math.h                thin re-export of raymath + helpers
    io.c/.h               file probing, config read/write

  game/                   ← rules + content, depends on engine only
    main.c                tiny: Eng_Run(Game_Module())
    world.{h,c}           World struct (all game state) + g_world singleton
    audio_director.c      game-side state diff → fires Eng_Audio* events
    render.c / viewmodel.c  build scene draws via Eng_Gfx* + Eng_Render* calls
    assets.c              PropId list + world-shader registration (game-side)
    sim/                  level, player, weapons, perks, entities, interact, rounds
    ui/                   hud, menu
    net/                  protocol: snapshot (de)serialisation

  (no shared/ — types.h split was assessed and skipped; see §17 #2)
```

---

## 4. Ownership: where every file landed

| Was | Now in | Notes |
|-----|--------|-------|
| `main.c` | split: `engine/app.c` (loop) + `game/main.c` + GameModule wiring | gameplay body in game callbacks |
| `mapdoc.*` | `engine/` | already clean |
| `assets.*` | `engine/content.*` (registry) + `game/assets.c` (PropId list) | weapon-model coupling inverted (§2) |
| `render.*`, `viewmodel.*` | `game/` (scene build via Eng_Gfx* calls) + `engine/eng_render.*` (passes/RT) | see §17 #3 |
| `anim.*` | `engine/anim.*` | |
| `particles.*`, `decals.*` | `engine/fx.*` | generic emitters; game fills `EngEmitterDesc` |
| `audio.*` | `engine/audio.*` (mixer) + `game/audio_director.c` (diff/events) | inversion done |
| `net.*` | `engine/net.*` | already game-clean |
| `protocol.*` | `game/net/` | knows the World schema — inherently game-side |
| `pad.*`, settings bindings | `engine/pad.*` | action map is engine; `BA_*` ids are game-registered |
| `fx.*` (camera shake) | `engine/` | trauma model is generic |
| `level.*` | `game/sim/` | instantiates MapDoc into World; collision rules are game |
| `player, weapons, perks, entities, interact, game(round)` | `game/sim/` | pure rules |
| `hud.*`, `menu.*` | `game/ui/` | |
| `devtools.*` | split: CLI harness (engine) + per-mode scene setup (game) | |

---

## 17. Decisions made and why (resolved + open)

### ✅ Phase 2 — gamepad fold-in (commit `985efe3`)
Engine action map (`src/engine/pad.{c,h}`) is the single source of truth for
keyboard + mouse + gamepad. `Eng_InputBind` carries trigger-axis bindings
(`ENG_BIND_TRIG_L/R`); `Settings_SyncEngineBindings` pushes every `BA_*`'s
current binding into the map and is called from `Settings_Load`/`Save`. All
`|| Bind_Pressed/Down` parallel queries at gameplay sites are gone — each is
one `Eng_Input*` call. `Bind_PollAny` remains only for the rebind-capture UI.

### ✅ Phase 4 — content registry (commit `8b0f153`)
Handle-based `Eng_LoadModel/Texture/Shader/AnimModel` with path-probing
(`Eng_ResolveAssetPath`) + dedup by resolved path, and
`Eng_RegisterContentType`/`Eng_LoadContent` so the engine loads `.weapon` bytes
and hands them to a game-registered parser without knowing their meaning.
`assets.c` + `weapons.c` route through it (deleted their private probing loops);
`assets.c` stays game-side for the `PropId` list + world-shader application.
`.map` stays game-side (instantiation into `g_world` is game logic). The full
handle-pool/AnimInstance generalisation (§9) was NOT done — draw sites still pull
the underlying raylib `Model`/`Texture` from the handle.

### ✅ Phase 5 — render seam via facade (commits `6df74ec`, `dc9db3e`); §8 DrawItem submission intentionally NOT done

Two parts completed:

- **Frame structure** (`6df74ec`): `src/engine/eng_render.{c,h}` owns the postFX
  RT lifecycle + composite, the `worldShader`/`worldSkinnedShader`/`postfxShader`
  handles + uniform locs (moved out of assets.c), and the lighting bookend
  (`Eng_RenderSetLighting`/`Eng_RenderBeginWorld/EndWorld`).
- **3D draw facade** (`dc9db3e`): `gfx.{c,h}` gained all 3D-scene draw wrappers
  (`Eng_GfxBeginMode3D/EndMode3D`, `Eng_GfxDrawModel/Ex`, `Cube/V/Wires/WiresV`,
  `Sphere`, `Plane`, `Triangle3D`, `Line3D`, `Grid`); `render.c`/`viewmodel.c`/
  `devtools.c` route every 3D draw through them. The game makes **zero direct
  raylib rendering calls for the 3D scene** — the render backend lives entirely
  behind `engine/{gfx,eng_render}`.

**Why NOT the full `RenderFrame`/`DrawItem[]` submission model (§8):** this
renderer is irreducibly *procedural* — cubes/spheres/triangles, per-draw
shader-uniform toggles (`tileVariation`), and animation state machines woven into
the draw calls. A flat `DrawItem[]` data list would be a large, lossy
command-recording layer (the engine would just replay raylib calls) with no real
separation gain, and there is no second-backend need driving it (the map editor
reuses the *same* backend — it needs the facade + frame structure, which it now
has). The facade is the correct boundary for this engine; the DrawItem submission
model is the wrong abstraction here and is intentionally not pursued.

### ✅ Fixed timestep (commit `2fe930c`)
`GameModule` has a `fixed(dt)` callback; `engine/app.c` drives it from a clamped
accumulator at `ENG_FIXED_DT` (1/60 s). The host/solo authoritative sim
(`Game_Tick` + snapshot broadcast) runs there; input/local-prediction/camera stay
in `frame`. Sim is frame-rate independent and matches the headless `--sim-tick`
rate. See `docs/engine-usage.md` §2.

### Open #1 — 2D UI facade (raygui / `DrawText` / `DrawRectangle`)
The remaining open render item: wrap raygui calls as `Eng_Ui*` primitives, or let
the game keep calling raygui directly as an allowed engine-adjacent dependency?
Deferred because it is **low value for the current goals**: the map editor needs
the same raygui anyway, and there is no second UI backend in view. Tracked but not
a blocker.

**Update (partially resolved):** the editor becoming real flipped this from low-value to
worth doing — two consumers (game menus + editor panels) now share the widgets. A
*partial* `Eng_Ui*` facade exists at `src/engine/ui.{c,h}` (theme + scaled text +
accent-bar tool button); both `menu.c` and the editor are ported onto it. Still open: a
full widget facade so apps stop calling raygui `Gui*` directly. See
[engine-layers.md](engine-layers.md) (step 1) and [engine-usage.md](engine-usage.md) §3 `ui`.

### Open #2 — `types.h` split (§13) — assessed 2026-06-15, NOT worth doing
No engine file includes `types.h` (verified: `grep '#include.*types.h' src/engine/`
is empty — the only hits are comment mentions). Since the engine is already fully
independent of it, the split would be a purely cosmetic game-side reorg of one
494-line header that touches every game file's includes for zero
seam/architectural value. Skipped on impact-per-effort grounds. If `types.h` ever
becomes a genuine merge-conflict hotspot, revisit as pure hygiene — but it is no
longer an engine-separation concern.

### Open #3 — `g_world` singleton vs threaded `World *`
`g_world` stays the singleton. The headless litmus (`--sim-tick`) is met without
threading a `World *` through ~290 call sites, so that churn was skipped. The
`players[]` name collides with `SerPlayer players[]` in `PktSnapshotHeader`, so
those are explicit `g_world.players` (not macro-aliased); `netMode` is
macro-aliased. No functional consequence — revisit only if a second simultaneous
`World` is needed (e.g. replays or multi-instance hosting).
