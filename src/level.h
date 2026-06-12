#ifndef SHOOTER_LEVEL_H
#define SHOOTER_LEVEL_H

#include "types.h"
#include "mapdoc.h"

extern Box         obstacles[MAX_OBSTACLES];
extern int         obstacleCount;
extern Box         interiorWalls[MAX_INTERIOR_WALLS];
extern int         interiorWallCount;
extern bool        interiorWallNoClip[MAX_INTERIOR_WALLS];
extern Door        doors[MAX_DOORS];
extern int         doorCount;
extern WallBuy     wallBuys[MAX_WALLBUYS];
extern int         wallBuyCount;
extern Window3D    windows[MAX_WINDOWS];
extern int         windowCount;
extern PerkMachine perkMachines[PERK_COUNT];
extern int         perkMachineCount;
extern PackAPunch  pap;
extern MysteryBox  mbox;
extern Vector3     mapSpawns[NET_MAX_PLAYERS];
extern int         mapSpawnCount;
extern char        mapName[64];
extern MapProp     mapProps[MAX_MAP_PROPS];
extern int         mapPropCount;

/* Per-map arena half-extents (runtime; default 40 x 40). */
extern float arenaHalfX;
extern float arenaHalfZ;

void  Level_Build(void);
bool  Level_LoadFromFile(const char *path);
void  Level_InstantiateDoc(const MapDoc *doc);  // doc -> game globals
void  Level_LoadHardcodedFallback(void);
void  Level_Reset(void);     // close doors, refill boards, clear PaP

// Validate a map file without spawning the game.  Returns 0 on clean
// parse, >0 on errors (number of errors reported).  Errors print to
// stderr with line numbers.
int   Level_Validate(const char *path);

// Collision helpers
BoundingBox Level_BoxToBB(Box b);
bool        Level_CircleHitsBoxXZ(Vector3 p, float r, Box b);
Vector3     Level_ResolveXZ(Vector3 from, Vector3 to, float radius, bool clampArena);
bool        Level_PointBlocked(Vector3 p, float pad);

// Returns true if a circle of `radius` swept from `from` along `dir` for
// `dist` meters in the XZ plane doesn't intersect any solid geometry
// (obstacles, interior walls, closed doors, map props). Open doors and
// windows are NOT considered solid. Used for AI obstacle avoidance.
bool        Level_PathClearXZ(Vector3 from, Vector3 dir, float radius, float dist);

float       Level_RandRange(float a, float b);

#endif
