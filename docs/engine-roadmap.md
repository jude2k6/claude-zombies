# Engine Roadmap

Where the engine (`libengine.a`) is headed. This is the *plan*; for how to use
what already exists, see [engine-usage.md](engine-usage.md) (§3 = present, §3a =
deliberately game-side, §3b = not yet built).

## North star

The long-term goal is a **Krunker-like engine**: a fast, low-poly, **multiplayer
movement-FPS** runtime — many players per match, fast TTK, movement tech
(slide / bhop / parkour), hitscan + projectile weapons, multiple modes,
cosmetics — with a **first-class in-engine map editor** and community custom maps.

The engine today is a capable single-player/co-op runtime + an editor *foundation*
(MapDoc + picking + debug-draw). It is not yet a multiplayer-movement-shooter
platform. This doc tracks the gap.

## Design principle: substrate, not policy

The engine ships **primitives**, the game brings **policy**. The clearest live
example is **movement**:

- The engine provides the **collision/sweep math** (`collide.h`, planned): capsule/
  AABB sweep, swept-cast, time-of-impact, penetration depth — the same kind of
  reusable math as `pick.h`'s ray helpers.
- The engine does **not** impose a character controller. Movement *feel* — slide,
  bhop, air-strafe, wall-run, accel/friction curves — is **game-side and
  pluggable**: an engine user drops in their own movement on top of the sweep
  primitive, or replaces it wholesale. (An optional reference controller may ship
  as a *convenience*, never a requirement.)

Same rule applies elsewhere: the engine owns transport, the game owns wire schema;
the engine owns ray math, the game owns what's solid; the engine owns the action
map, the game owns what the actions mean.

## Gaps to the north star

Three **Critical** engine-level gaps (confirmed by source audit), then supporting
work. None block finishing the current game + a basic editor — they block the
multiplayer-movement phase.

### Critical
- **Netcode depth & scale** — `net.h` is raw enet transport with `NET_MAX_PLAYERS == 4`
  hardcoded. Needs: the cap raised + a many-client peer model; **entity interpolation**,
  **snapshot delta-compression**, and a **server-tick/broadcast loop** as reusable
  engine services. (Client prediction/reconciliation and the wire *schema* stay
  game-side — that split is correct.)
- **Collision/sweep primitive** — `collide.h`: capsule/AABB sweep + TOI, so movement
  tech has a shared substrate. Per the design principle above, this is *math only*;
  the controller stays game-side.
- **Render at scale** — frustum culling + instanced/batched draws + LOD selection in
  `gfx.h`/`eng_render.h`. Many players/projectiles/casings go draw-call-bound fast
  without it.

### Important
- ~~**Profiling/stats** — `stats.h`: frame time, draw-call count, fixed-steps-per-frame.~~
  **Done** (Phase A) — `stats.h`, `Eng_Stats*`.
- ~~**`EngConfig` launch options** — vsync / MSAA / FPS-cap / fullscreen.~~ **Done**
  (Phase A) — `EngConfig{vsync,msaa4x,resizable,fullscreen,fpsCap}`.
- **Editor: gizmo + undo/redo** — axis-constrained translate/rotate/scale drag handles
  and a command-stack on MapDoc's stable ids. Both engine-agnostic enough to be shared
  utilities (`gizmo.h`), like `pick.h`.
- **Async asset loading** — for streamed community maps/cosmetics without frame hitches.

### Nice-to-have
- Audio voice-budget / priority / distance culling (16+ players = many simultaneous SFX).
- `Eng_Gfx*` billboard + bounding-box helpers.
- Generic (de)serialization / bit-packing (snapshot delta-compression would share it).

## Phases

| Phase | Work | Why first |
|-------|------|-----------|
| ~~**A**~~ ✅ | `EngConfig` options + `stats.h` profiling | Small, low-risk; unblocks tuning everything after — **shipped** |
| **B** | `collide.h` sweep/cast primitive | Movement substrate (controller stays game-side/pluggable) |
| **C** | Editor gizmo + undo/redo command-stack | Completes the in-engine editor |
| **D** | Netcode depth (raise cap, interpolation, delta-compression, server tick) | The big one; the core of "Krunker-like" |
| **E** | Render at scale (culling, instancing, LOD) | Revisit once Phase D player counts make it load-bearing |
| **F** | Async loading, audio voice-budget, serialization, gfx helpers | Polish |

When a roadmap item ships, document it in [engine-usage.md](engine-usage.md) §3 and
drop it from §3b.
