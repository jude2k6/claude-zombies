#ifndef SHOOTER_LEVEL_H
#define SHOOTER_LEVEL_H

#include "types.h"
#include "mapdoc.h"
#include "world.h"   // all level state now lives in g_world (Phase 0)

// Level state has been fully relocated into g_world (src/world.h). Collision-
// free names are reached via alias macros there; collider names (obstacles,
// obstacleCount, windows, windowCount, pap, mbox, mapName, arenaHalfX/Z) are
// accessed explicitly as g_world.X because they clash with MapDoc/Pkt fields.

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

// ---- multi-floor spatial queries ------------------------------------------
// Surface Y of a single floor region at (x,z) — caller must ensure (x,z) is
// inside the region's XZ rect. Handles flat slabs and ramps.
float       Level_RegionSurfaceY(const FloorRegion *f, float x, float z);

// "Where is the floor here?" — the highest walkable surface at (x,z) that sits
// at or just above `feetY` (within STEP_UP_HEIGHT, so stairs/curbs auto-climb).
// Returns the implicit ground plane (Y=0) when no region qualifies, so maps
// with no FloorRegions behave exactly like the old flat world. This is the one
// query that turns ground height from a constant into data (see
// docs/multi-floor-maps.md §2).
float       Level_FloorHeightAt(float x, float z, float feetY);

#endif
