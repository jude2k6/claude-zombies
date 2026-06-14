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
    Player    players[NET_MAX_PLAYERS];
    Enemy     enemies[MAX_ENEMIES];
    Bullet    bullets[MAX_BULLETS];
    Throwable throwables[MAX_THROWABLES];
    PowerUp   powerUps[MAX_POWERUPS];
    int       localPlayerIdx;
    GamePhase gamePhase;
    // Scalar game-progress state. These names also appear as fields in the
    // network snapshot struct, so they are accessed explicitly as g_world.X
    // (no aliasing macro below) — see the note above.
    int       roundNum;
    float     doublePointsTimer;
    float     instaKillTimer;

    // ---- level geometry / state (relocated from level.c) ------------------
    // Collision-free names below are aliased by macros at the bottom of this
    // header; collider names (`obstacles`, `obstacleCount`, `windows`,
    // `windowCount`, `pap`, `mbox`, `mapName`, `arenaHalfX`, `arenaHalfZ`)
    // also appear as MapDoc/Pkt struct fields so they are accessed explicitly
    // as g_world.X with no macro (see note above).
    Box         obstacles[MAX_OBSTACLES];
    int         obstacleCount;
    int         obstacleTexHandle[MAX_OBSTACLES];     /* -1 = no override */
    Box         interiorWalls[MAX_INTERIOR_WALLS];
    int         interiorWallCount;
    bool        interiorWallNoClip[MAX_INTERIOR_WALLS];
    int         interiorWallTexHandle[MAX_INTERIOR_WALLS]; /* -1 = no override */
    Door        doors[MAX_DOORS];
    int         doorCount;
    WallBuy     wallBuys[MAX_WALLBUYS];
    int         wallBuyCount;
    Window3D    windows[MAX_WINDOWS];
    int         windowCount;
    PerkMachine perkMachines[PERK_COUNT];
    int         perkMachineCount;
    PackAPunch  pap;
    MysteryBox  mbox;
    Vector3     mapSpawns[NET_MAX_PLAYERS];
    int         mapSpawnCount;
    char        mapName[64];
    MapProp     mapProps[MAX_MAP_PROPS];
    int         mapPropCount;
    float       arenaHalfX;   // per-map arena half-extents (default 40 x 40)
    float       arenaHalfZ;

    // Walkable floor regions for multi-floor / vertical maps. Empty by default
    // (an implicit ground plane at Y=0 covers the flat case). Accessed
    // explicitly as g_world.floors — no alias macro (new state).
    FloorRegion floors[MAX_FLOORS];
    int         floorCount;

    // ---- session / networking role ---------------------------------------
    // netMode is collision-free (never a struct field), so it is aliased by a
    // macro below. players[] clashes with the SerPlayer players[] field in the
    // network snapshot header, so it is accessed explicitly as g_world.players
    // (no macro) — protocol.c keeps its hdr->players / SerPlayer players[] uses.
    NetMode   netMode;
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

// Collision-free level globals (verified: never struct fields or locals).
#define obstacleTexHandle     (g_world.obstacleTexHandle)
#define interiorWalls         (g_world.interiorWalls)
#define interiorWallCount     (g_world.interiorWallCount)
#define interiorWallNoClip    (g_world.interiorWallNoClip)
#define interiorWallTexHandle (g_world.interiorWallTexHandle)
#define doors                 (g_world.doors)
#define doorCount             (g_world.doorCount)
#define wallBuys              (g_world.wallBuys)
#define wallBuyCount          (g_world.wallBuyCount)
#define perkMachines          (g_world.perkMachines)
#define perkMachineCount      (g_world.perkMachineCount)
#define mapSpawns             (g_world.mapSpawns)
#define mapSpawnCount         (g_world.mapSpawnCount)
#define mapProps              (g_world.mapProps)
#define mapPropCount          (g_world.mapPropCount)
#define netMode               (g_world.netMode)

#endif
