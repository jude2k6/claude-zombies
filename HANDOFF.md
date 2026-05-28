# Handoff — Claude Zombies

For the next Claude agent picking up this project. Read this first, then
`README.md` for player-facing context, then `TODO.md` for what's next.

## What this is

A 3D round-based zombies shooter in C using **raylib 5.5**, **raygui 4.0**,
and **enet 1.3.18** (all via CMake `FetchContent`). Host-authoritative
4-player coop over UDP. Cross-platform: Linux (X11), macOS, Windows
(MSVC + MinGW).

Repo: `git@github.com:jude2k6/claude-zombies.git` · branch: `main`.

## Current state (2026-05-28)

**There are uncommitted changes** for a batch of features the user
asked for ("add 1 2 3 4 12 9"). Build is clean; smoke-tested. The
commit + push step was never executed before context compaction.

Run `git status` and `git diff` to see exactly what's pending. The
batch covers:

- Per-player stats (kills, headshots, melee, accuracy, revives) +
  game-over stats table.
- Stamina bar for sprint (local-only `Player.stamina`).
- New `ZombieType`: Runner (R4+, fast/low HP), Crawler (R7+, no head,
  can't be headshot), Boss (every 5th round, big HP).
- Door waypoint pathfinding (`ZS_INSIDE` case in `Enemies_Update`).
- Hold-`E` revive of downed teammates (new `IK_REVIVE`).
- Mystery Box (`MBOX x z` in map, `$950`, 4s spin, 8s claim window).
- **Network protocol bumped to v4** (`NET_PROTO_VERSION` in
  `src/net.h`). Old clients won't connect — that's intentional.

Ask the user before committing/pushing — they may want to test first.

## Build / run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shooter
```

First Linux build needs X11 dev headers — see `README.md`. The build
copies `data/` next to the executable, so maps load from
`data/maps/default.map`. If that file isn't found the game falls
back to a hardcoded layout in `Level_LoadHardcodedFallback`.

## Architecture

`src/main.c` is just the game loop (~250 lines). Everything else is
one translation unit per responsibility, sharing `types.h`:

| File              | Owns                                                   |
|-------------------|--------------------------------------------------------|
| `types.h`         | All shared structs/enums/constants                     |
| `level.{c,h}`     | Map parser, obstacles/walls/doors/windows, `Level_Reset` |
| `player.{c,h}`    | `players[]`, look/move, sprint/crouch, stamina, push-out |
| `weapons.{c,h}`   | `WEAPONS[]` table, fire/reload/melee                   |
| `entities.{c,h}`  | `enemies[]`, `bullets[]`, `powerUps[]`, zombie AI + spawns |
| `interact.{c,h}`  | Wall-buys, perks, PaP, doors, repairs, revive, Mystery Box |
| `perks.{c,h}`     | Perk effects (jug/speed/dtap/stamin)                   |
| `game.{c,h}`      | `Game_Tick` — orchestrates a server tick               |
| `protocol.{c,h}`  | Serialization (`SerPlayer`, `SerEnemy`, `PktSnapshotHeader`) |
| `net.{c,h}`       | enet wrapper, `Net_GetLocalIPs` for the host's join prompt |
| `render.{c,h}`    | 3D world draw + sprites/labels                         |
| `hud.{c,h}`       | 2D HUD overlay                                         |
| `menu.{c,h}`      | Menu screens incl. game-over stats table               |

## Conventions & gotchas

- **Host-authoritative.** Clients send `PktInput`; host runs sim and
  broadcasts `PktSnapshot` at 20 Hz. Local-only fields (stamina,
  godMode) live on `Player` but aren't serialized.
- **Bump `NET_PROTO_VERSION`** in `net.h` any time you change a
  serialized struct in `protocol.{c,h}`. The handshake rejects
  mismatched versions.
- **Map file path resolution** tries several relative locations and
  falls back to hardcoded geometry. Don't assume CWD.
- **Door persistence bug** previously caused doors to stay open
  across games. Fixed by `Level_Reset` being called from
  `Menu_StartSoloGame` / `Menu_StartHostedGame`. Don't regress.
- **Repair-progress bug** previously zeroed all windows each tick.
  Fixed by tracking `winActive[]` and only zeroing inactive ones in
  `Interact_UpdateRepairs`. Same pattern used for revives.
- **rand()** — `interact.c` uses it for Mystery Box rolls; needs
  `#include <stdlib.h>`.
- **rlgl** — if you use `rlPushMatrix`/`rlTranslatef`/`rlPopMatrix`,
  include `rlgl.h` explicitly. raylib doesn't pull it in.
- **Spectator mode** — when `!me->alive`, `main.c` runs a free-fly
  camera (WASD + mouse, Space up, Shift down) instead of the player.

## Working style the user has shown

- Likes terse commits with a concise subject + bulleted body. Recent
  example: `dae71fb Bug fixes + sprint/crouch/melee/headshot/...`.
- Skips planning docs — asked for a design doc once, then said
  "forget about design doc delete it just refactor it".
- Bundles multiple features in a single request ("add 1 2 3 4 12 9").
  Implement them all in one batch; one commit is fine.
- Doesn't proofread feature names; "melay" = melee, "baricade" =
  barricade, etc. Just infer.

## Suggested next things (from `TODO.md`)

- Audio (procedural SFX via raylib)
- Map selector UI (engine already supports multi-map loading)
- Mule Kick (3rd weapon slot)
- Pack-a-Punch Tier 2
- Floating damage numbers
- Settings persistence
- Prone

## If something breaks

1. `cmake --build build -j 2>&1 | tail -40` — most issues are
   missing-include or implicit-decl warnings.
2. Run `./build/shooter` and check stdout for the `map: loaded ...`
   line — confirms the map file is being found.
3. For multiplayer issues, check `NET_PROTO_VERSION` matches between
   host and client builds.
