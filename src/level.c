#include "level.h"
#include "mapdoc.h"
#include "assets.h"
#include "decals.h"
#include "particles.h"
#include "raymath.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// All level state now lives in g_world (Phase 0). Collision-free names are
// reached via alias macros in world.h; collider names (obstacles, windows,
// pap, mbox, mapName, arenaHalfX/Z + the two counts) are accessed explicitly
// as g_world.X. Non-zero defaults (mapName, arena half-extents) are set in the
// g_world initializer in world.c.
//
// NOTE: interiorWallNoClip per-wall flag — header/lintel segments above door
// openings are drawn and block bullets (the 3D segment test is Y-aware) but
// must NOT block XZ movement, or the doorway is walled off whether the door is
// open or shut.

float Level_RandRange(float a, float b) {
    return a + ((float)rand() / (float)RAND_MAX) * (b - a);
}

BoundingBox Level_BoxToBB(Box b) {
    return (BoundingBox){
        (Vector3){ b.center.x - b.size.x*0.5f, b.center.y - b.size.y*0.5f, b.center.z - b.size.z*0.5f },
        (Vector3){ b.center.x + b.size.x*0.5f, b.center.y + b.size.y*0.5f, b.center.z + b.size.z*0.5f }
    };
}

bool Level_CircleHitsBoxXZ(Vector3 p, float r, Box b) {
    float minX = b.center.x - b.size.x*0.5f, maxX = b.center.x + b.size.x*0.5f;
    float minZ = b.center.z - b.size.z*0.5f, maxZ = b.center.z + b.size.z*0.5f;
    float cx = Clamp(p.x, minX, maxX);
    float cz = Clamp(p.z, minZ, maxZ);
    float dx = p.x - cx, dz = p.z - cz;
    return (dx*dx + dz*dz) < (r*r);
}

// If `p` is overlapping a box (penetrating), push it out along the shortest
// axis to the surface plus a tiny skin. Returns true if it moved.
static bool PushOutOfBoxXZ(Vector3 *p, float radius, Box b) {
    float minX = b.center.x - b.size.x*0.5f, maxX = b.center.x + b.size.x*0.5f;
    float minZ = b.center.z - b.size.z*0.5f, maxZ = b.center.z + b.size.z*0.5f;
    float cx = Clamp(p->x, minX, maxX);
    float cz = Clamp(p->z, minZ, maxZ);
    float dx = p->x - cx, dz = p->z - cz;
    float d2 = dx*dx + dz*dz;
    if (d2 >= radius*radius) return false; // not overlapping

    const float skin = 0.001f;
    if (d2 > 1e-6f) {
        float d = sqrtf(d2);
        float push = (radius - d) + skin;
        p->x += (dx / d) * push;
        p->z += (dz / d) * push;
    } else {
        // Player center is fully inside the box: push out toward the nearest
        // face. Compute penetration depth on each axis and pick the shallower.
        float penLeft   = p->x - minX;
        float penRight  = maxX - p->x;
        float penBack   = p->z - minZ;
        float penFront  = maxZ - p->z;
        float minPen = penLeft;
        char axis = 'L';
        if (penRight < minPen) { minPen = penRight; axis = 'R'; }
        if (penBack  < minPen) { minPen = penBack;  axis = 'B'; }
        if (penFront < minPen) { minPen = penFront; axis = 'F'; }
        switch (axis) {
            case 'L': p->x = minX - radius - skin; break;
            case 'R': p->x = maxX + radius + skin; break;
            case 'B': p->z = minZ - radius - skin; break;
            case 'F': p->z = maxZ + radius + skin; break;
        }
    }
    return true;
}

// Push `p` out of every collider it currently overlaps. A few passes lets
// corner cases (overlapping two boxes at once) settle.
static void UnstickXZ(Vector3 *p, float radius) {
    for (int pass = 0; pass < 4; pass++) {
        bool moved = false;
        for (int i = 0; i < g_world.obstacleCount; i++)
            if (PushOutOfBoxXZ(p, radius, g_world.obstacles[i])) moved = true;
        for (int i = 0; i < interiorWallCount; i++)
            if (!interiorWallNoClip[i] && PushOutOfBoxXZ(p, radius, interiorWalls[i])) moved = true;
        for (int i = 0; i < doorCount; i++)
            if (!doors[i].opened && PushOutOfBoxXZ(p, radius, doors[i].box)) moved = true;
        for (int i = 0; i < mapPropCount; i++)
            if (PushOutOfBoxXZ(p, radius, mapProps[i].collider)) moved = true;
        if (!moved) break;
    }
}

Vector3 Level_ResolveXZ(Vector3 from, Vector3 to, float radius, bool clampArena) {
    // Heal "stuck inside a wall" state by nudging `from` out of any collider
    // it's currently penetrating. This handles spawns that overlap geometry,
    // moving doors, and any edge case where sliding logic wedged the player
    // into a corner.
    Vector3 origin = from;
    UnstickXZ(&origin, radius);

    Vector3 candidate = to;
    if (clampArena) {
        float limX = g_world.arenaHalfX - radius;
        float limZ = g_world.arenaHalfZ - radius;
        if (candidate.x >  limX) candidate.x =  limX;
        if (candidate.x < -limX) candidate.x = -limX;
        if (candidate.z >  limZ) candidate.z =  limZ;
        if (candidate.z < -limZ) candidate.z = -limZ;
    }

    Vector3 stepX = (Vector3){ candidate.x, origin.y, origin.z };
    for (int i = 0; i < g_world.obstacleCount; i++)
        if (Level_CircleHitsBoxXZ(stepX, radius, g_world.obstacles[i])) { stepX.x = origin.x; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (!interiorWallNoClip[i] && Level_CircleHitsBoxXZ(stepX, radius, interiorWalls[i])) { stepX.x = origin.x; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && Level_CircleHitsBoxXZ(stepX, radius, doors[i].box)) { stepX.x = origin.x; break; }
    for (int i = 0; i < mapPropCount; i++)
        if (Level_CircleHitsBoxXZ(stepX, radius, mapProps[i].collider)) { stepX.x = origin.x; break; }

    Vector3 stepZ = (Vector3){ stepX.x, origin.y, candidate.z };
    for (int i = 0; i < g_world.obstacleCount; i++)
        if (Level_CircleHitsBoxXZ(stepZ, radius, g_world.obstacles[i])) { stepZ.z = origin.z; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (!interiorWallNoClip[i] && Level_CircleHitsBoxXZ(stepZ, radius, interiorWalls[i])) { stepZ.z = origin.z; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && Level_CircleHitsBoxXZ(stepZ, radius, doors[i].box)) { stepZ.z = origin.z; break; }
    for (int i = 0; i < mapPropCount; i++)
        if (Level_CircleHitsBoxXZ(stepZ, radius, mapProps[i].collider)) { stepZ.z = origin.z; break; }

    return stepZ;
}

bool Level_PointBlocked(Vector3 p, float pad) {
    for (int i = 0; i < g_world.obstacleCount; i++) {
        Box b = g_world.obstacles[i];
        b.size.x += pad*2; b.size.z += pad*2;
        if (Level_CircleHitsBoxXZ(p, 0.01f, b)) return true;
    }
    for (int i = 0; i < mapPropCount; i++) {
        Box b = mapProps[i].collider;
        b.size.x += pad*2; b.size.z += pad*2;
        if (Level_CircleHitsBoxXZ(p, 0.01f, b)) return true;
    }
    return false;
}

bool Level_PathClearXZ(Vector3 from, Vector3 dir, float radius, float dist) {
    const int steps = 4;
    for (int s = 1; s <= steps; s++) {
        float t = ((float)s / steps) * dist;
        Vector3 p = { from.x + dir.x * t, from.y, from.z + dir.z * t };
        for (int i = 0; i < g_world.obstacleCount; i++)
            if (Level_CircleHitsBoxXZ(p, radius, g_world.obstacles[i])) return false;
        for (int i = 0; i < interiorWallCount; i++)
            if (!interiorWallNoClip[i] && Level_CircleHitsBoxXZ(p, radius, interiorWalls[i])) return false;
        for (int i = 0; i < doorCount; i++)
            if (!doors[i].opened && Level_CircleHitsBoxXZ(p, radius, doors[i].box)) return false;
        for (int i = 0; i < mapPropCount; i++)
            if (Level_CircleHitsBoxXZ(p, radius, mapProps[i].collider)) return false;
    }
    return true;
}

float Level_RegionSurfaceY(const FloorRegion *f, float x, float z) {
    switch (f->rampAxis) {
        case RAMP_X: {
            float t = (x - (f->cx - f->halfX)) / (2.0f * f->halfX);
            return f->yLow + (f->yHigh - f->yLow) * Clamp(t, 0.0f, 1.0f);
        }
        case RAMP_Z: {
            float t = (z - (f->cz - f->halfZ)) / (2.0f * f->halfZ);
            return f->yLow + (f->yHigh - f->yLow) * Clamp(t, 0.0f, 1.0f);
        }
        default:
            return f->yLow;
    }
}

float Level_FloorHeightAt(float x, float z, float feetY) {
    // Implicit ground plane: always a candidate, so flat maps (no regions)
    // and falling-to-the-bottom both resolve to Y=0.
    float best = 0.0f;
    for (int i = 0; i < g_world.floorCount; i++) {
        const FloorRegion *f = &g_world.floors[i];
        if (x < f->cx - f->halfX || x > f->cx + f->halfX) continue;
        if (z < f->cz - f->halfZ || z > f->cz + f->halfZ) continue;
        float surf = Level_RegionSurfaceY(f, x, z);
        // Skip surfaces above what the player can step onto: anything more than
        // a step above the feet is "the floor above me", not the one I'm on.
        if (surf > feetY + STEP_UP_HEIGHT) continue;
        if (surf > best) best = surf;
    }
    return best;
}

// ============================================================================
//  Map instantiation (MapDoc -> game globals)
// ============================================================================

static int WeaponNameToIdx(const char *s) {
    if (!s) return -1;
    if (strcmp(s, "PISTOL")  == 0) return W_PISTOL;
    if (strcmp(s, "SMG")     == 0) return W_SMG;
    if (strcmp(s, "SHOTGUN") == 0) return W_SHOTGUN;
    if (strcmp(s, "RIFLE")   == 0) return W_RIFLE;
    if (strcmp(s, "RAYGUN")  == 0) return W_RAYGUN;
    return -1;
}

static int PerkNameToIdx(const char *s) {
    if (!s) return -1;
    if (strcmp(s, "JUG")    == 0) return PERK_JUG;
    if (strcmp(s, "SPEED")  == 0) return PERK_SPEED;
    if (strcmp(s, "DTAP")   == 0) return PERK_DTAP;
    if (strcmp(s, "STAMIN") == 0) return PERK_STAMIN;
    return -1;
}

static void ClearLevel(void) {
    g_world.obstacleCount = 0;
    for (int i = 0; i < MAX_OBSTACLES; i++) obstacleTexHandle[i] = -1;
    interiorWallCount = 0;
    memset(interiorWallNoClip, 0, sizeof interiorWallNoClip);
    for (int i = 0; i < MAX_INTERIOR_WALLS; i++) interiorWallTexHandle[i] = -1;
    doorCount = 0;
    wallBuyCount = 0;
    g_world.windowCount = 0;
    perkMachineCount = 0;
    mapSpawnCount = 0;
    mapPropCount = 0;
    g_world.mapName[0] = 0;
    g_world.arenaHalfX = ARENA_HALF_DEFAULT;
    g_world.arenaHalfZ = ARENA_HALF_DEFAULT;
    g_world.pap = (PackAPunch){ .pos = {0,0,0}, .phase = PAP_IDLE, .slotInProgress = -1, .ownerPlayer = -1, .weaponIdx = -1 };
    g_world.mbox = (MysteryBox){ .placed = false, .pos = {0,0,0}, .state = MBOX_IDLE,
                         .timer = 0, .showingWeapon = 0, .finalWeapon = 0, .ownerPlayer = -1, .bob = 0 };
    // Reset atmosphere globals to defaults so each map starts from the
    // same baseline; ATMOSPHERE blocks then override.
    fogStart = 10.0f;
    fogEnd   = 55.0f;
    fogColor = (Color){ 10, 12, 22, 255 };
}

// ---- prop registry -----------------------------------------------------
// Maps PROP names (used in map files) to engine PropId + a hand-tuned
// collider half-extent.  When a map adds `PROP <name> x z`, the
// instantiator looks up the name here.
typedef struct {
    const char *name;
    int         propId;
    Vector3     halfExtent;  // collision half-size (radius equivalent)
    float       footYOffset; // model origin to centre of collider Y
} PropDef;

static const PropDef PROP_DEFS[] = {
    { "sandbag_stack",   PROP_SANDBAG_STACK,   { 0.75f, 0.40f, 0.30f }, 0.40f },
    { "obstacle_crate",  PROP_OBSTACLE_CRATE,  { 0.50f, 0.50f, 0.50f }, 0.50f },
    { "obstacle_barrel", PROP_OBSTACLE_BARREL, { 0.35f, 0.55f, 0.35f }, 0.55f },
};
static const int PROP_DEF_COUNT = (int)(sizeof PROP_DEFS / sizeof PROP_DEFS[0]);

static const PropDef *PropDefByName(const char *name) {
    for (int i = 0; i < PROP_DEF_COUNT; i++)
        if (strcmp(PROP_DEFS[i].name, name) == 0) return &PROP_DEFS[i];
    return NULL;
}

// ---- dir string -> Vector3 normal ------------------------------------------
static Vector3 DirToNormal(const char dir[4]) {
    if (strcmp(dir, "+x") == 0) return (Vector3){  1, 0,  0 };
    if (strcmp(dir, "-x") == 0) return (Vector3){ -1, 0,  0 };
    if (strcmp(dir, "+z") == 0) return (Vector3){  0, 0,  1 };
    if (strcmp(dir, "-z") == 0) return (Vector3){  0, 0, -1 };
    return (Vector3){ 0, 0, 0 };
}

// ---- EmitWallSegment (shared by instantiator) ------------------------------

// Emit an interior wall segment between (x1, z1) and (x2, z2).  No-op
// if the two ends are equal (zero-length segment from a flush door).
// Uses a fake lineNo of 0 when called from the instantiator (no file context).
static bool EmitWallSegment(float x1, float z1, float x2, float z2) {
    if (fabsf(x1 - x2) < 0.001f && fabsf(z1 - z2) < 0.001f) return true;
    if (interiorWallCount >= MAX_INTERIOR_WALLS) {
        fprintf(stderr, "map: too many walls (MAX_INTERIOR_WALLS=%d)\n",
                MAX_INTERIOR_WALLS);
        return false;
    }
    Vector3 c = { (x1 + x2) * 0.5f, WALL_HEIGHT * 0.5f, (z1 + z2) * 0.5f };
    Vector3 s;
    if (fabsf(x1 - x2) < 0.001f) {
        s = (Vector3){ WALL_THICK, WALL_HEIGHT, fabsf(z2 - z1) };
    } else {
        s = (Vector3){ fabsf(x2 - x1), WALL_HEIGHT, WALL_THICK };
    }
    interiorWalls[interiorWallCount++] = (Box){ c, s };
    return true;
}

// ---- look up a door index by name in the current game state ----------------
static int DoorIndexByName(const char *name) {
    for (int i = 0; i < doorCount; i++)
        if (doors[i].name[0] && strcmp(doors[i].name, name) == 0) return i;
    return -1;
}

// ============================================================================
//  Level_InstantiateDoc — copy a MapDoc into the live game globals.
//  Warns to stderr if any map cap is exceeded and clamps rather than
//  corrupting memory.
// ============================================================================

void Level_InstantiateDoc(const MapDoc *doc) {
    // Flush the name-keyed texture cache and override table before rebuilding.
    // Must happen before ClearLevel (which also resets texture handle arrays)
    // so the GL unload happens while the GL context is valid.
    Assets_FlushNameCache();

    ClearLevel();

    // Name
    strncpy(g_world.mapName, doc->name, sizeof g_world.mapName - 1);
    g_world.mapName[sizeof g_world.mapName - 1] = '\0';

    // Arena half-extents
    g_world.arenaHalfX = doc->arenaHalfX;
    g_world.arenaHalfZ = doc->arenaHalfZ;

    // Per-map texture slot overrides from TEXTURES block.
    if (doc->textures.present) {
        const struct { const char *name; TextureId tid; } slots[] = {
            { doc->textures.floor,    TEX_FLOOR    },
            { doc->textures.ground,   TEX_GROUND   },
            { doc->textures.wall_ext, TEX_WALL_EXT },
            { doc->textures.wall_int, TEX_WALL_INT },
            { doc->textures.ceiling,  TEX_CEILING  },
        };
        for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) {
            if (slots[i].name[0])
                mapTexOverrides[slots[i].tid] = Assets_GetTextureByName(slots[i].name);
        }
    }

    // Atmosphere
    if (doc->atmosphere.present) {
        fogColor = (Color){
            (unsigned char)doc->atmosphere.fogR,
            (unsigned char)doc->atmosphere.fogG,
            (unsigned char)doc->atmosphere.fogB,
            255
        };
        fogStart = doc->atmosphere.fogStart;
        fogEnd   = doc->atmosphere.fogEnd;
        // sky_tint and music reserved — no game consumer yet
    }

    // Spawns — game array is sized NET_MAX_PLAYERS; warn if more in doc
    for (int i = 0; i < doc->spawnCount; i++) {
        if (mapSpawnCount >= NET_MAX_PLAYERS) {
            fprintf(stderr, "map: warning: %d spawns in map, game cap is %d — ignoring extras\n",
                    doc->spawnCount, NET_MAX_PLAYERS);
            break;
        }
        mapSpawns[mapSpawnCount++] = (Vector3){
            doc->spawns[i].x, PLAYER_EYE, doc->spawns[i].z
        };
    }
    if (mapSpawnCount == 0)
        mapSpawns[mapSpawnCount++] = (Vector3){ 0, PLAYER_EYE, 0 };

    // Walls & Doors — each MapDocWall may produce up to 3 interior wall
    // segments and 1 door entry.
    for (int wi = 0; wi < doc->wallCount; wi++) {
        const MapDocWall *w = &doc->walls[wi];
        bool xRun = fabsf(w->z1 - w->z2) < 0.001f;

        // Resolve per-wall TEX name to a cache handle (-1 if unset/missing).
        int wallTex = -1;
        if (w->texName[0])
            wallTex = Assets_GetTextureByName(w->texName);

        // Remember the first wall segment index so we can stamp all segments
        // produced by this doc wall with the same texture handle.
        int firstSeg = interiorWallCount;

        if (w->door.present) {
            if (doorCount >= MAX_DOORS) {
                fprintf(stderr, "map: warning: too many doors (MAX_DOORS=%d) — skipping\n",
                        MAX_DOORS);
                continue;
            }
            float center = w->door.center;
            float width  = w->door.width;
            float dl = center - width * 0.5f;
            float dr = center + width * 0.5f;
            float headerY  = DOOR_HEIGHT + (WALL_HEIGHT - DOOR_HEIGHT) * 0.5f;
            float headerSY = WALL_HEIGHT - DOOR_HEIGHT;

            if (xRun) {
                EmitWallSegment(w->x1, w->z1, dl, w->z1);
                EmitWallSegment(dr, w->z1, w->x2, w->z1);
                if (interiorWallCount < MAX_INTERIOR_WALLS) {
                    interiorWallNoClip[interiorWallCount] = true;
                    interiorWalls[interiorWallCount++] = (Box){
                        { center, headerY, w->z1 },
                        { width,  headerSY, WALL_THICK },
                    };
                }
                Vector3 c = { center, DOOR_HEIGHT * 0.5f, w->z1 };
                Vector3 s = { width,  DOOR_HEIGHT,        WALL_THICK };
                Door d = { .box = { c, s }, .cost = w->door.cost, .opened = false };
                if (w->door.name[0]) strncpy(d.name, w->door.name, sizeof d.name - 1);
                doors[doorCount++] = d;
            } else {
                EmitWallSegment(w->x1, w->z1, w->x1, dl);
                EmitWallSegment(w->x1, dr, w->x1, w->z2);
                if (interiorWallCount < MAX_INTERIOR_WALLS) {
                    interiorWallNoClip[interiorWallCount] = true;
                    interiorWalls[interiorWallCount++] = (Box){
                        { w->x1,      headerY,  center },
                        { WALL_THICK, headerSY, width  },
                    };
                }
                Vector3 c = { w->x1,      DOOR_HEIGHT * 0.5f, center };
                Vector3 s = { WALL_THICK, DOOR_HEIGHT,        width  };
                Door d = { .box = { c, s }, .cost = w->door.cost, .opened = false };
                if (w->door.name[0]) strncpy(d.name, w->door.name, sizeof d.name - 1);
                doors[doorCount++] = d;
            }
        } else {
            EmitWallSegment(w->x1, w->z1, w->x2, w->z2);
        }

        // Stamp all newly added segments with this wall's texture handle.
        for (int si = firstSeg; si < interiorWallCount; si++)
            interiorWallTexHandle[si] = wallTex;
    }

    // Windows — resolve LOCKED_BY after doors are built
    for (int i = 0; i < doc->windowCount; i++) {
        if (g_world.windowCount >= MAX_WINDOWS) {
            fprintf(stderr, "map: warning: too many g_world.windows (MAX_WINDOWS=%d) — ignoring extras\n",
                    MAX_WINDOWS);
            break;
        }
        const MapDocWindow *dw = &doc->windows[i];
        Vector3 normal  = DirToNormal(dw->dir);
        Vector3 tangent = { -normal.z, 0, normal.x };
        int lockedBy = -1;
        if (dw->lockedBy[0]) {
            lockedBy = DoorIndexByName(dw->lockedBy);
            if (lockedBy < 0)
                fprintf(stderr, "map: warning: window references unknown door '%s'\n",
                        dw->lockedBy);
        }
        g_world.windows[g_world.windowCount++] = (Window3D){
            .pos = (Vector3){ dw->x, WALL_HEIGHT * 0.5f, dw->z },
            .normal = normal, .tangent = tangent,
            .boards = MAX_BOARDS_PER_WIN,
            .repairProgress = 0, .repairPlayer = -1,
            .lockedByDoor = lockedBy,
        };
    }

    // Obstacles
    for (int i = 0; i < doc->obstacleCount; i++) {
        if (g_world.obstacleCount >= MAX_OBSTACLES) {
            fprintf(stderr, "map: warning: too many g_world.obstacles (MAX_OBSTACLES=%d) — ignoring extras\n",
                    MAX_OBSTACLES);
            break;
        }
        const MapDocObstacle *o = &doc->obstacles[i];
        int obIdx = g_world.obstacleCount++;
        g_world.obstacles[obIdx] = (Box){ { o->x, o->h * 0.5f, o->z }, { o->sx, o->h, o->sz } };
        obstacleTexHandle[obIdx] = (o->texName[0])
            ? Assets_GetTextureByName(o->texName) : -1;
    }

    // Wallbuys
    for (int i = 0; i < doc->wallbuyCount; i++) {
        if (wallBuyCount >= MAX_WALLBUYS) {
            fprintf(stderr, "map: warning: too many wallbuys (MAX_WALLBUYS=%d) — ignoring extras\n",
                    MAX_WALLBUYS);
            break;
        }
        const MapDocWallbuy *wb = &doc->wallbuys[i];
        int widx = WeaponNameToIdx(wb->weapon);
        if (widx < 0) continue;
        Vector3 normal = DirToNormal(wb->dir);
        wallBuys[wallBuyCount++] = (WallBuy){
            .pos = (Vector3){ wb->x, 2.0f, wb->z },
            .normal = normal,
            .weaponIdx = widx,
        };
    }

    // Perks
    for (int i = 0; i < doc->perkCount; i++) {
        if (perkMachineCount >= PERK_COUNT) {
            fprintf(stderr, "map: warning: too many perk machines (PERK_COUNT=%d) — ignoring extras\n",
                    PERK_COUNT);
            break;
        }
        const MapDocPerk *pk = &doc->perks[i];
        int pidx = PerkNameToIdx(pk->perk);
        if (pidx < 0) continue;
        perkMachines[perkMachineCount++] = (PerkMachine){ {pk->x, 0, pk->z}, pidx };
    }

    // Pack-a-Punch
    if (doc->hasPap) {
        g_world.pap = (PackAPunch){
            .pos = { doc->pap.x, 0, doc->pap.z },
            .phase = PAP_IDLE, .slotInProgress = -1,
            .ownerPlayer = -1, .weaponIdx = -1
        };
    }

    // Mystery Box
    if (doc->mbox.present) {
        g_world.mbox = (MysteryBox){
            .placed = true, .pos = { doc->mbox.x, 0.5f, doc->mbox.z },
            .state = MBOX_IDLE, .timer = 0,
            .showingWeapon = 0, .finalWeapon = 0,
            .ownerPlayer = -1, .bob = 0
        };
    }

    // Props
    for (int i = 0; i < doc->propCount; i++) {
        if (mapPropCount >= MAX_MAP_PROPS) {
            fprintf(stderr, "map: warning: too many props (MAX_MAP_PROPS=%d) — ignoring extras\n",
                    MAX_MAP_PROPS);
            break;
        }
        const MapDocProp *pr = &doc->props[i];
        const PropDef *def = PropDefByName(pr->name);
        if (!def) {
            fprintf(stderr, "map: warning: unknown prop '%s' — skipping\n", pr->name);
            continue;
        }
        Vector3 he = def->halfExtent;
        float   sc = pr->scale;
        mapProps[mapPropCount++] = (MapProp){
            .propId = def->propId,
            .pos    = (Vector3){ pr->x, 0, pr->z },
            .yawDeg = pr->yawDeg,
            .scale  = sc,
            .collider = (Box){
                { pr->x, def->footYOffset * sc, pr->z },
                { he.x * 2 * sc, he.y * 2 * sc, he.z * 2 * sc },
            },
        };
    }
}

bool Level_LoadFromFile(const char *path) {
    MapDoc doc;
    int errs = MapDoc_Parse(path, &doc, stderr);
    if (errs == 0 || doc.wallCount + doc.obstacleCount + doc.windowCount > 0) {
        Level_InstantiateDoc(&doc);
        fprintf(stderr, "map: loaded '%s' from %s (%d walls, %d doors, %d g_world.windows, %d props)\n",
                g_world.mapName, path, interiorWallCount, doorCount, g_world.windowCount, mapPropCount);
        return true;
    }
    return false;
}

int Level_Validate(const char *path) {
    MapDoc doc;
    int errs = MapDoc_Parse(path, &doc, stderr);
    if (errs == 0) fprintf(stderr, "map: %s — OK\n", path);
    else           fprintf(stderr, "map: %s — %d error(s)\n", path, errs);
    return errs;
}

void Level_LoadHardcodedFallback(void) {
    ClearLevel();
    strncpy(g_world.mapName, "Default (fallback)", sizeof g_world.mapName - 1);

    mapSpawns[mapSpawnCount++] = (Vector3){  0, PLAYER_EYE,  0 };
    mapSpawns[mapSpawnCount++] = (Vector3){  2, PLAYER_EYE,  0 };
    mapSpawns[mapSpawnCount++] = (Vector3){ -2, PLAYER_EYE,  0 };
    mapSpawns[mapSpawnCount++] = (Vector3){  0, PLAYER_EYE,  2 };

    Box layout[] = {
        { {  10, 1.5f,   5 }, { 4, 3, 4 } },
        { { -12, 1.0f,  -8 }, { 6, 2, 3 } },
        { { -10, 1.5f, -16 }, { 4, 3, 4 } },
        { {  18, 1.0f, -15 }, { 3, 2, 6 } },
        { { -20, 1.5f,  12 }, { 5, 3, 5 } },
        { {   8, 1.0f,  20 }, { 7, 2, 2 } },
        { {  -6, 1.5f,  26 }, { 3, 3, 3 } },
        { {  25, 1.0f,   2 }, { 2, 2, 8 } },
    };
    for (size_t i = 0; i < sizeof layout / sizeof layout[0]; i++) g_world.obstacles[g_world.obstacleCount++] = layout[i];

    interiorWalls[interiorWallCount++] = (Box){ { -21.5f, 2.5f, -20.0f }, { 37, 5, 1 } };
    interiorWalls[interiorWallCount++] = (Box){ {  21.5f, 2.5f, -20.0f }, { 37, 5, 1 } };

    doors[doorCount++] = (Door){ .box = { {0, 2.5f, -20}, {6, 5, 1} }, .cost = 1500, .opened = false };

    wallBuys[wallBuyCount++] = (WallBuy){ { 15.0f, 2.0f,  ARENA_HALF_DEFAULT - 0.55f }, { 0,0,-1}, W_SHOTGUN };
    wallBuys[wallBuyCount++] = (WallBuy){ { ARENA_HALF_DEFAULT - 0.55f, 2.0f, -10.0f }, {-1,0, 0}, W_SMG };
    wallBuys[wallBuyCount++] = (WallBuy){ {-15.0f, 2.0f, -ARENA_HALF_DEFAULT + 0.55f }, { 0,0, 1}, W_RIFLE };
    wallBuys[wallBuyCount++] = (WallBuy){ {-ARENA_HALF_DEFAULT + 0.55f, 2.0f, 18.0f }, { 1,0, 0}, W_RAYGUN };

    Window3D ws[] = {
        { .pos = { -18.0f, WALL_HEIGHT*0.5f,  ARENA_HALF_DEFAULT }, .normal = { 0, 0, -1 }, .tangent = { 1, 0, 0 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 },
        { .pos = { ARENA_HALF_DEFAULT, WALL_HEIGHT*0.5f, 18.0f }, .normal = { -1, 0, 0 }, .tangent = { 0, 0, 1 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 },
        { .pos = { 18.0f, WALL_HEIGHT*0.5f, -ARENA_HALF_DEFAULT }, .normal = { 0, 0, 1 }, .tangent = { 1, 0, 0 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = 0 },
        { .pos = { -ARENA_HALF_DEFAULT, WALL_HEIGHT*0.5f, -8.0f }, .normal = { 1, 0, 0 }, .tangent = { 0, 0, 1 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 },
    };
    for (size_t i = 0; i < sizeof ws / sizeof ws[0]; i++) g_world.windows[g_world.windowCount++] = ws[i];

    perkMachines[perkMachineCount++] = (PerkMachine){ { -8.0f, 0, 12.0f }, PERK_JUG };
    perkMachines[perkMachineCount++] = (PerkMachine){ {  4.0f, 0, -8.0f }, PERK_SPEED };
    perkMachines[perkMachineCount++] = (PerkMachine){ { 22.0f, 0, 25.0f }, PERK_DTAP };
    perkMachines[perkMachineCount++] = (PerkMachine){ {-25.0f, 0,-22.0f }, PERK_STAMIN };

    g_world.pap = (PackAPunch){ .pos = { 0, 0, -28.0f }, .phase = PAP_IDLE, .slotInProgress = -1, .ownerPlayer = -1, .weaponIdx = -1 };
    g_world.mbox = (MysteryBox){ .placed = true, .pos = { -22.0f, 0.5f, 15.0f },
                         .state = MBOX_IDLE, .timer = 0,
                         .showingWeapon = 0, .finalWeapon = 0,
                         .ownerPlayer = -1, .bob = 0 };
}

void Level_Build(void) {
    // Try a few likely paths so it works whether you run from the project
    // root or from the build/ directory.
    const char *paths[] = {
        "data/maps/default.map",
        "../data/maps/default.map",
        "./data/maps/default.map",
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        if (Level_LoadFromFile(paths[i])) return;
    }
    fprintf(stderr, "map: no .map file found, using hardcoded fallback\n");
    Level_LoadHardcodedFallback();
}

void Level_Reset(void) {
    for (int i = 0; i < doorCount; i++) doors[i].opened = false;
    for (int i = 0; i < g_world.windowCount; i++) {
        g_world.windows[i].boards = MAX_BOARDS_PER_WIN;
        g_world.windows[i].repairProgress = 0;
        g_world.windows[i].repairPlayer = -1;
    }
    g_world.pap.phase = PAP_IDLE;
    g_world.pap.timer = 0;
    g_world.pap.slotInProgress = -1;
    g_world.pap.ownerPlayer = -1;
    g_world.pap.weaponIdx = -1;
    g_world.mbox.state = MBOX_IDLE;
    g_world.mbox.timer = 0;
    g_world.mbox.ownerPlayer = -1;
    Decals_ClearAll();
    Particles_Reset();
}
