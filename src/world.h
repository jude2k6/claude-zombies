#ifndef SHOOTER_WORLD_H
#define SHOOTER_WORLD_H

#include "types.h"

// ---------------------------------------------------------------------------
// World — the game's mutable state (Phase 0 of the engine/game split).
//
// The end goal is a single owner of all simulation state so the game can be
// ticked headless through a `World *` instead of reaching for file-scope
// externs (see docs/engine-game-separation.md §5). Migration is incremental:
// collision-free globals are relocated here first and the old bare names are
// kept working via the transitional macros below. Names that clash with
// protocol/mapdoc struct fields — `players`, `mbox`, `mapName`, `roundNum`,
// and the power-up timers — can't use the macro shim (an object-like macro
// would corrupt `hdr->players` / `doc->mbox`), so they stay as externs until
// real `World *` threading lands.
// ---------------------------------------------------------------------------
typedef struct {
    Enemy     enemies[MAX_ENEMIES];
    Bullet    bullets[MAX_BULLETS];
    Throwable throwables[MAX_THROWABLES];
    PowerUp   powerUps[MAX_POWERUPS];
    int       localPlayerIdx;
    GamePhase gamePhase;
    // TODO (Phase 0, real threading): players[], level state, mbox, mapName,
    // roundNum, doublePointsTimer, instaKillTimer, netMode.
} World;

// The single live world instance. Defined in world.c.
extern World g_world;

// ---- transitional aliases (Phase 0 shim) ----------------------------------
// Keep existing `enemies[i]` / `gamePhase` call sites compiling unchanged while
// the storage lives in g_world. Safe only because none of these names are used
// as struct fields or locals anywhere (verified). Remove as call sites are
// threaded to take a `World *`.
#define enemies        (g_world.enemies)
#define bullets        (g_world.bullets)
#define throwables     (g_world.throwables)
#define powerUps       (g_world.powerUps)
#define localPlayerIdx (g_world.localPlayerIdx)
#define gamePhase      (g_world.gamePhase)

#endif
