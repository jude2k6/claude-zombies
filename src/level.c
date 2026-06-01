#include "level.h"
#include "assets.h"
#include "decals.h"
#include "raymath.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

Box         obstacles[MAX_OBSTACLES];
int         obstacleCount = 0;
Box         interiorWalls[MAX_INTERIOR_WALLS];
int         interiorWallCount = 0;
// Per-wall flag: header/lintel segments above door openings are drawn and
// block bullets (the 3D segment test is Y-aware) but must NOT block XZ
// movement, or the doorway is walled off whether the door is open or shut.
bool        interiorWallNoClip[MAX_INTERIOR_WALLS];
Door        doors[MAX_DOORS];
int         doorCount = 0;
WallBuy     wallBuys[8];
int         wallBuyCount = 0;
Window3D    windows[MAX_WINDOWS];
int         windowCount = 0;
PerkMachine perkMachines[PERK_COUNT];
int         perkMachineCount = 0;
PackAPunch  pap;
MysteryBox  mbox;
Vector3     mapSpawns[NET_MAX_PLAYERS];
int         mapSpawnCount = 0;
char        mapName[64] = "Default";
MapProp     mapProps[MAX_MAP_PROPS];
int         mapPropCount = 0;

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
        for (int i = 0; i < obstacleCount; i++)
            if (PushOutOfBoxXZ(p, radius, obstacles[i])) moved = true;
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
        float lim = ARENA_HALF - radius;
        if (candidate.x >  lim) candidate.x =  lim;
        if (candidate.x < -lim) candidate.x = -lim;
        if (candidate.z >  lim) candidate.z =  lim;
        if (candidate.z < -lim) candidate.z = -lim;
    }

    Vector3 stepX = (Vector3){ candidate.x, origin.y, origin.z };
    for (int i = 0; i < obstacleCount; i++)
        if (Level_CircleHitsBoxXZ(stepX, radius, obstacles[i])) { stepX.x = origin.x; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (!interiorWallNoClip[i] && Level_CircleHitsBoxXZ(stepX, radius, interiorWalls[i])) { stepX.x = origin.x; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && Level_CircleHitsBoxXZ(stepX, radius, doors[i].box)) { stepX.x = origin.x; break; }
    for (int i = 0; i < mapPropCount; i++)
        if (Level_CircleHitsBoxXZ(stepX, radius, mapProps[i].collider)) { stepX.x = origin.x; break; }

    Vector3 stepZ = (Vector3){ stepX.x, origin.y, candidate.z };
    for (int i = 0; i < obstacleCount; i++)
        if (Level_CircleHitsBoxXZ(stepZ, radius, obstacles[i])) { stepZ.z = origin.z; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (!interiorWallNoClip[i] && Level_CircleHitsBoxXZ(stepZ, radius, interiorWalls[i])) { stepZ.z = origin.z; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && Level_CircleHitsBoxXZ(stepZ, radius, doors[i].box)) { stepZ.z = origin.z; break; }
    for (int i = 0; i < mapPropCount; i++)
        if (Level_CircleHitsBoxXZ(stepZ, radius, mapProps[i].collider)) { stepZ.z = origin.z; break; }

    return stepZ;
}

bool Level_PointBlocked(Vector3 p, float pad) {
    for (int i = 0; i < obstacleCount; i++) {
        Box b = obstacles[i];
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
        for (int i = 0; i < obstacleCount; i++)
            if (Level_CircleHitsBoxXZ(p, radius, obstacles[i])) return false;
        for (int i = 0; i < interiorWallCount; i++)
            if (!interiorWallNoClip[i] && Level_CircleHitsBoxXZ(p, radius, interiorWalls[i])) return false;
        for (int i = 0; i < doorCount; i++)
            if (!doors[i].opened && Level_CircleHitsBoxXZ(p, radius, doors[i].box)) return false;
        for (int i = 0; i < mapPropCount; i++)
            if (Level_CircleHitsBoxXZ(p, radius, mapProps[i].collider)) return false;
    }
    return true;
}

// ============================================================================
//  Map parsing
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
    obstacleCount = 0;
    interiorWallCount = 0;
    memset(interiorWallNoClip, 0, sizeof interiorWallNoClip);
    doorCount = 0;
    wallBuyCount = 0;
    windowCount = 0;
    perkMachineCount = 0;
    mapSpawnCount = 0;
    mapPropCount = 0;
    mapName[0] = 0;
    pap = (PackAPunch){ .pos = {0,0,0}, .activeTimer = 0, .slotInProgress = -1, .ownerPlayer = -1 };
    mbox = (MysteryBox){ .placed = false, .pos = {0,0,0}, .state = MBOX_IDLE,
                         .timer = 0, .showingWeapon = 0, .finalWeapon = 0, .ownerPlayer = -1, .bob = 0 };
    // Reset atmosphere globals to defaults so each map starts from the
    // same baseline; ATMOSPHERE blocks then override.
    fogStart = 10.0f;
    fogEnd   = 55.0f;
    fogColor = (Color){ 10, 12, 22, 255 };
}

// ---- prop registry -----------------------------------------------------
// Maps PROP names (used in map files) to engine PropId + a hand-tuned
// collider half-extent.  When map adds `PROP <name> x z`, the parser
// looks up the name here.  Add new props by extending both this table
// and PropId.
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

// ---- parse helpers -----------------------------------------------------

// Tokenise an in-place buffer into NUL-terminated whitespace-delimited
// tokens.  Modifies `s` (replaces whitespace with NUL).  Returns the
// number of tokens written into `out`, capped at `max`.
static int Tokenise(char *s, char **out, int max) {
    int n = 0;
    while (*s) {
        while (*s && isspace((unsigned char)*s)) *s++ = 0;
        if (!*s) break;
        if (n < max) out[n] = s;
        n++;
        while (*s && !isspace((unsigned char)*s)) s++;
    }
    return n;
}

static bool ParseFloat(const char *s, float *out) {
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s || *end != 0) return false;
    *out = v;
    return true;
}

static bool ParseInt(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != 0) return false;
    *out = (int)v;
    return true;
}

// `+x`, `-x`, `+z`, `-z` -> unit Vector3 (y = 0).  Returns false on
// anything else.
static bool ParseDir(const char *s, Vector3 *out) {
    if (strcmp(s, "+x") == 0) { *out = (Vector3){  1, 0,  0 }; return true; }
    if (strcmp(s, "-x") == 0) { *out = (Vector3){ -1, 0,  0 }; return true; }
    if (strcmp(s, "+z") == 0) { *out = (Vector3){  0, 0,  1 }; return true; }
    if (strcmp(s, "-z") == 0) { *out = (Vector3){  0, 0, -1 }; return true; }
    return false;
}

// Per-window deferred door reference: window N wants door named `name`,
// resolve to index after parse.  Cleared per parse.
static struct {
    int  windowIdx;
    char name[24];
} g_pendingDoorRef[MAX_WINDOWS];
static int g_pendingDoorRefCount = 0;

static int DoorIndexByName(const char *name) {
    for (int i = 0; i < doorCount; i++)
        if (doors[i].name[0] && strcmp(doors[i].name, name) == 0) return i;
    return -1;
}

// Emit an interior wall segment between (x1, z1) and (x2, z2).  No-op
// if the two ends are equal (zero-length segment from a flush door).
static bool EmitWallSegment(float x1, float z1, float x2, float z2,
                            int lineNo, int *errCount, FILE *errs) {
    if (fabsf(x1 - x2) < 0.001f && fabsf(z1 - z2) < 0.001f) return true;
    if (interiorWallCount >= MAX_INTERIOR_WALLS) {
        fprintf(errs, "map: line %d: too many walls (MAX_INTERIOR_WALLS=%d)\n",
                lineNo, MAX_INTERIOR_WALLS);
        if (errCount) (*errCount)++;
        return false;
    }
    Vector3 c = { (x1 + x2) * 0.5f, WALL_HEIGHT * 0.5f, (z1 + z2) * 0.5f };
    Vector3 s;
    if (fabsf(x1 - x2) < 0.001f) {
        // Z-running wall
        s = (Vector3){ WALL_THICK, WALL_HEIGHT, fabsf(z2 - z1) };
    } else {
        s = (Vector3){ fabsf(x2 - x1), WALL_HEIGHT, WALL_THICK };
    }
    interiorWalls[interiorWallCount++] = (Box){ c, s };
    return true;
}

// ---- the parser --------------------------------------------------------

// Implementation shared by Level_LoadFromFile and Level_Validate. In
// validate mode we don't open a window or set globals; we just count
// errors.
static int ParseMapFile(const char *path, bool validateOnly) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "map: cannot open %s\n", path);
        return 1;
    }

    ClearLevel();
    g_pendingDoorRefCount = 0;
    int errors = 0;
    FILE *errs = stderr;

    enum { BLK_NONE, BLK_ATMOS, BLK_ROOM } block = BLK_NONE;
    char roomName[32] = {0};

    char raw[256];
    int lineNo = 0;
    while (fgets(raw, sizeof raw, f)) {
        lineNo++;
        // Strip comment.
        char *hash = strchr(raw, '#');
        if (hash) *hash = 0;

        char *toks[16];
        int n = Tokenise(raw, toks, 16);
        if (n == 0) continue;

        const char *key = toks[0];

        // --- block management ---
        if (strcmp(key, "ATMOSPHERE") == 0) {
            if (block != BLK_NONE) {
                fprintf(errs, "map: line %d: ATMOSPHERE inside another block\n", lineNo);
                errors++; continue;
            }
            block = BLK_ATMOS; continue;
        }
        if (strcmp(key, "ROOM") == 0) {
            if (block != BLK_NONE) {
                fprintf(errs, "map: line %d: ROOM inside another block\n", lineNo);
                errors++; continue;
            }
            if (n >= 2) {
                strncpy(roomName, toks[1], sizeof roomName - 1);
                roomName[sizeof roomName - 1] = 0;
            } else roomName[0] = 0;
            block = BLK_ROOM; continue;
        }
        if (strcmp(key, "END") == 0) {
            if (block == BLK_NONE) {
                fprintf(errs, "map: line %d: END outside a block\n", lineNo);
                errors++;
            }
            block = BLK_NONE;
            roomName[0] = 0;
            continue;
        }

        // --- atmosphere sub-keys ---
        if (block == BLK_ATMOS) {
            if (strcmp(key, "fog") == 0 && n == 6) {
                float r, g, b, s, e;
                if (!ParseFloat(toks[1],&r) || !ParseFloat(toks[2],&g) ||
                    !ParseFloat(toks[3],&b) || !ParseFloat(toks[4],&s) ||
                    !ParseFloat(toks[5],&e)) {
                    fprintf(errs, "map: line %d: fog expects r g b start end\n", lineNo);
                    errors++; continue;
                }
                if (!validateOnly) {
                    fogColor = (Color){ (unsigned char)r, (unsigned char)g,
                                        (unsigned char)b, 255 };
                    fogStart = s;
                    fogEnd   = e;
                }
            } else if (strcmp(key, "sky_tint") == 0 && n == 4) {
                // Reserved; logged but no engine consumer yet.
            } else if (strcmp(key, "music") == 0 && n == 2) {
                // Reserved; logged but no audio impl yet.
            } else {
                fprintf(errs, "map: line %d: unknown atmosphere key '%s'\n", lineNo, key);
                errors++;
            }
            continue;
        }

        // --- top-level / room-level entries ---
        if (strcmp(key, "NAME") == 0) {
            // Re-join tokens 1..N with single spaces.
            mapName[0] = 0;
            for (int i = 1; i < n; i++) {
                if (i > 1) strncat(mapName, " ", sizeof mapName - strlen(mapName) - 1);
                strncat(mapName, toks[i], sizeof mapName - strlen(mapName) - 1);
            }
            continue;
        }

        if (strcmp(key, "SPAWN") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: SPAWN expects x z\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: SPAWN bad number\n", lineNo);
                errors++; continue;
            }
            if (mapSpawnCount >= NET_MAX_PLAYERS) {
                fprintf(errs, "map: line %d: too many spawns\n", lineNo);
                errors++; continue;
            }
            mapSpawns[mapSpawnCount++] = (Vector3){ x, PLAYER_EYE, z };
            continue;
        }

        if (strcmp(key, "WALL") == 0) {
            // WALL x1 z1 x2 z2 [DOOR center width cost [AS name]]
            if (n < 5) {
                fprintf(errs, "map: line %d: WALL expects x1 z1 x2 z2\n", lineNo);
                errors++; continue;
            }
            float x1, z1, x2, z2;
            if (!ParseFloat(toks[1],&x1) || !ParseFloat(toks[2],&z1) ||
                !ParseFloat(toks[3],&x2) || !ParseFloat(toks[4],&z2)) {
                fprintf(errs, "map: line %d: WALL bad number\n", lineNo);
                errors++; continue;
            }
            bool xRun = fabsf(z1 - z2) < 0.001f;
            bool zRun = fabsf(x1 - x2) < 0.001f;
            if (!xRun && !zRun) {
                fprintf(errs, "map: line %d: WALL must be axis-aligned\n", lineNo);
                errors++; continue;
            }
            // Normalise ordering so x1<=x2 (or z1<=z2).
            if (xRun && x2 < x1) { float t=x1; x1=x2; x2=t; }
            if (zRun && z2 < z1) { float t=z1; z1=z2; z2=t; }

            // Look for DOOR clause.
            int tIdx = 5;
            if (tIdx < n && strcmp(toks[tIdx], "DOOR") == 0) {
                if (n < tIdx + 4) {
                    fprintf(errs, "map: line %d: DOOR expects center width cost\n", lineNo);
                    errors++; continue;
                }
                float center, width;
                int cost;
                if (!ParseFloat(toks[tIdx+1], &center) ||
                    !ParseFloat(toks[tIdx+2], &width) ||
                    !ParseInt  (toks[tIdx+3], &cost)) {
                    fprintf(errs, "map: line %d: DOOR bad number\n", lineNo);
                    errors++; continue;
                }
                char doorName[24] = {0};
                if (n >= tIdx + 6 && strcmp(toks[tIdx+4], "AS") == 0) {
                    strncpy(doorName, toks[tIdx+5], sizeof doorName - 1);
                }
                if (doorCount >= MAX_DOORS) {
                    fprintf(errs, "map: line %d: too many doors (MAX_DOORS=%d)\n",
                            lineNo, MAX_DOORS);
                    errors++; continue;
                }
                // Emit left wall, door, right wall, plus a "header" wall
                // segment above the door so the opening above the 2.5m door
                // is closed off instead of leaving a 2.5m gap to the ceiling.
                float headerY  = DOOR_HEIGHT + (WALL_HEIGHT - DOOR_HEIGHT) * 0.5f;
                float headerSY = WALL_HEIGHT - DOOR_HEIGHT;
                if (xRun) {
                    float dl = center - width * 0.5f;
                    float dr = center + width * 0.5f;
                    if (dl < x1 || dr > x2) {
                        fprintf(errs, "map: line %d: DOOR extends past wall ends\n", lineNo);
                        errors++; continue;
                    }
                    if (!EmitWallSegment(x1, z1, dl, z1, lineNo, &errors, errs)) continue;
                    if (!EmitWallSegment(dr, z1, x2, z1, lineNo, &errors, errs)) continue;
                    if (interiorWallCount < MAX_INTERIOR_WALLS) {
                        interiorWallNoClip[interiorWallCount] = true;
                        interiorWalls[interiorWallCount++] = (Box){
                            { center, headerY, z1 },
                            { width,  headerSY, WALL_THICK },
                        };
                    }
                    Vector3 c = { center, DOOR_HEIGHT * 0.5f, z1 };
                    Vector3 s = { width,  DOOR_HEIGHT,        WALL_THICK };
                    Door d = { .box = { c, s }, .cost = cost, .opened = false };
                    if (doorName[0]) strncpy(d.name, doorName, sizeof d.name - 1);
                    doors[doorCount++] = d;
                } else {
                    float dl = center - width * 0.5f;
                    float dr = center + width * 0.5f;
                    if (dl < z1 || dr > z2) {
                        fprintf(errs, "map: line %d: DOOR extends past wall ends\n", lineNo);
                        errors++; continue;
                    }
                    if (!EmitWallSegment(x1, z1, x1, dl, lineNo, &errors, errs)) continue;
                    if (!EmitWallSegment(x1, dr, x1, z2, lineNo, &errors, errs)) continue;
                    if (interiorWallCount < MAX_INTERIOR_WALLS) {
                        interiorWallNoClip[interiorWallCount] = true;
                        interiorWalls[interiorWallCount++] = (Box){
                            { x1,         headerY,  center },
                            { WALL_THICK, headerSY, width  },
                        };
                    }
                    Vector3 c = { x1,         DOOR_HEIGHT * 0.5f, center };
                    Vector3 s = { WALL_THICK, DOOR_HEIGHT,        width  };
                    Door d = { .box = { c, s }, .cost = cost, .opened = false };
                    if (doorName[0]) strncpy(d.name, doorName, sizeof d.name - 1);
                    doors[doorCount++] = d;
                }
            } else {
                EmitWallSegment(x1, z1, x2, z2, lineNo, &errors, errs);
            }
            continue;
        }

        if (strcmp(key, "DOOR") == 0) {
            fprintf(errs, "map: line %d: DOOR is only valid as part of a WALL line\n", lineNo);
            errors++; continue;
        }

        if (strcmp(key, "OBSTACLE") == 0) {
            // OBSTACLE x z sx sz [h]
            if (n < 5) {
                fprintf(errs, "map: line %d: OBSTACLE expects x z sx sz [h]\n", lineNo);
                errors++; continue;
            }
            float x, z, sx, sz, h = 2.0f;
            if (!ParseFloat(toks[1], &x)  || !ParseFloat(toks[2], &z) ||
                !ParseFloat(toks[3], &sx) || !ParseFloat(toks[4], &sz)) {
                fprintf(errs, "map: line %d: OBSTACLE bad number\n", lineNo);
                errors++; continue;
            }
            if (n >= 6 && !ParseFloat(toks[5], &h)) {
                fprintf(errs, "map: line %d: OBSTACLE bad height\n", lineNo);
                errors++; continue;
            }
            if (obstacleCount >= MAX_OBSTACLES) {
                fprintf(errs, "map: line %d: too many obstacles\n", lineNo);
                errors++; continue;
            }
            obstacles[obstacleCount++] = (Box){
                { x, h * 0.5f, z }, { sx, h, sz }
            };
            continue;
        }

        if (strcmp(key, "WALLBUY") == 0) {
            // WALLBUY x z <dir> WEAPON
            if (n != 5) {
                fprintf(errs, "map: line %d: WALLBUY expects x z <dir> WEAPON\n", lineNo);
                errors++; continue;
            }
            float x, z;
            Vector3 normal;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z) ||
                !ParseDir  (toks[3], &normal)) {
                fprintf(errs, "map: line %d: WALLBUY bad arg\n", lineNo);
                errors++; continue;
            }
            int widx = WeaponNameToIdx(toks[4]);
            if (widx < 0) {
                fprintf(errs, "map: line %d: unknown weapon '%s'\n", lineNo, toks[4]);
                errors++; continue;
            }
            if (wallBuyCount >= 8) {
                fprintf(errs, "map: line %d: too many wall buys\n", lineNo);
                errors++; continue;
            }
            wallBuys[wallBuyCount++] = (WallBuy){
                .pos = (Vector3){ x, 2.0f, z },
                .normal = normal,
                .weaponIdx = widx,
            };
            continue;
        }

        if (strcmp(key, "PERK") == 0) {
            if (n != 4) {
                fprintf(errs, "map: line %d: PERK expects x z NAME\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: PERK bad coord\n", lineNo);
                errors++; continue;
            }
            int pidx = PerkNameToIdx(toks[3]);
            if (pidx < 0) {
                fprintf(errs, "map: line %d: unknown perk '%s'\n", lineNo, toks[3]);
                errors++; continue;
            }
            if (perkMachineCount >= PERK_COUNT) {
                fprintf(errs, "map: line %d: too many perk machines\n", lineNo);
                errors++; continue;
            }
            perkMachines[perkMachineCount++] = (PerkMachine){ {x, 0, z}, pidx };
            continue;
        }

        if (strcmp(key, "PAP") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: PAP expects x z\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: PAP bad coord\n", lineNo);
                errors++; continue;
            }
            pap = (PackAPunch){ .pos = {x, 0, z}, .activeTimer = 0,
                                .slotInProgress = -1, .ownerPlayer = -1 };
            continue;
        }

        if (strcmp(key, "MBOX") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: MBOX expects x z\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: MBOX bad coord\n", lineNo);
                errors++; continue;
            }
            mbox = (MysteryBox){ .placed = true, .pos = {x, 0.5f, z},
                                 .state = MBOX_IDLE, .timer = 0,
                                 .showingWeapon = 0, .finalWeapon = 0,
                                 .ownerPlayer = -1, .bob = 0 };
            continue;
        }

        if (strcmp(key, "WINDOW") == 0) {
            // WINDOW x z <dir> [LOCKED_BY door_name]
            if (n < 4) {
                fprintf(errs, "map: line %d: WINDOW expects x z <dir>\n", lineNo);
                errors++; continue;
            }
            float x, z;
            Vector3 normal;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z) ||
                !ParseDir  (toks[3], &normal)) {
                fprintf(errs, "map: line %d: WINDOW bad arg\n", lineNo);
                errors++; continue;
            }
            int lockedBy = -1;
            char pendingName[24] = {0};
            if (n >= 6 && strcmp(toks[4], "LOCKED_BY") == 0) {
                strncpy(pendingName, toks[5], sizeof pendingName - 1);
            } else if (n != 4) {
                fprintf(errs, "map: line %d: WINDOW trailing tokens\n", lineNo);
                errors++; continue;
            }
            if (windowCount >= MAX_WINDOWS) {
                fprintf(errs, "map: line %d: too many windows\n", lineNo);
                errors++; continue;
            }
            Vector3 tangent = { -normal.z, 0, normal.x };
            int widx = windowCount++;
            windows[widx] = (Window3D){
                .pos = (Vector3){ x, WALL_HEIGHT*0.5f, z },
                .normal = normal, .tangent = tangent,
                .boards = MAX_BOARDS_PER_WIN,
                .repairProgress = 0, .repairPlayer = -1,
                .lockedByDoor = lockedBy,
            };
            if (pendingName[0] && g_pendingDoorRefCount < MAX_WINDOWS) {
                g_pendingDoorRef[g_pendingDoorRefCount].windowIdx = widx;
                strncpy(g_pendingDoorRef[g_pendingDoorRefCount].name,
                        pendingName, sizeof g_pendingDoorRef[0].name - 1);
                g_pendingDoorRefCount++;
            }
            continue;
        }

        if (strcmp(key, "PROP") == 0) {
            // PROP <name> x z [yaw <deg>] [scale <s>]
            if (n < 4) {
                fprintf(errs, "map: line %d: PROP expects <name> x z [yaw d] [scale s]\n", lineNo);
                errors++; continue;
            }
            const PropDef *def = PropDefByName(toks[1]);
            if (!def) {
                fprintf(errs, "map: line %d: unknown PROP '%s'\n", lineNo, toks[1]);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[2], &x) || !ParseFloat(toks[3], &z)) {
                fprintf(errs, "map: line %d: PROP bad coord\n", lineNo);
                errors++; continue;
            }
            float yaw = 0.0f, scale = 1.0f;
            int i = 4;
            while (i + 1 < n) {
                if (strcmp(toks[i], "yaw") == 0) {
                    if (!ParseFloat(toks[i+1], &yaw)) {
                        fprintf(errs, "map: line %d: PROP bad yaw\n", lineNo);
                        errors++; break;
                    }
                } else if (strcmp(toks[i], "scale") == 0) {
                    if (!ParseFloat(toks[i+1], &scale)) {
                        fprintf(errs, "map: line %d: PROP bad scale\n", lineNo);
                        errors++; break;
                    }
                } else {
                    fprintf(errs, "map: line %d: PROP unknown option '%s'\n",
                            lineNo, toks[i]);
                    errors++; break;
                }
                i += 2;
            }
            if (mapPropCount >= MAX_MAP_PROPS) {
                fprintf(errs, "map: line %d: too many PROPs\n", lineNo);
                errors++; continue;
            }
            Vector3 he = def->halfExtent;
            mapProps[mapPropCount++] = (MapProp){
                .propId = def->propId,
                .pos    = (Vector3){ x, 0, z },
                .yawDeg = yaw,
                .scale  = scale,
                .collider = (Box){
                    { x, def->footYOffset * scale, z },
                    { he.x * 2 * scale, he.y * 2 * scale, he.z * 2 * scale },
                },
            };
            continue;
        }

        fprintf(errs, "map: line %d: unknown key '%s'\n", lineNo, key);
        errors++;
    }
    fclose(f);

    if (block != BLK_NONE) {
        fprintf(errs, "map: end of file inside an open block\n");
        errors++;
    }

    // Resolve window LOCKED_BY refs now that all doors are known.
    for (int i = 0; i < g_pendingDoorRefCount; i++) {
        int dIdx = DoorIndexByName(g_pendingDoorRef[i].name);
        if (dIdx < 0) {
            fprintf(errs, "map: window references unknown door '%s'\n",
                    g_pendingDoorRef[i].name);
            errors++;
        } else {
            windows[g_pendingDoorRef[i].windowIdx].lockedByDoor = dIdx;
        }
    }

    if (mapSpawnCount == 0) {
        mapSpawns[mapSpawnCount++] = (Vector3){ 0, PLAYER_EYE, 0 };
    }

    if (!validateOnly) {
        fprintf(stderr, "map: loaded '%s' from %s (%d walls, %d doors, %d windows, %d props)\n",
                mapName, path, interiorWallCount, doorCount, windowCount, mapPropCount);
    }
    return errors;
}

bool Level_LoadFromFile(const char *path) {
    int errs = ParseMapFile(path, false);
    return errs == 0 || (errs > 0 && interiorWallCount + doorCount + windowCount > 0);
}

int Level_Validate(const char *path) {
    int errs = ParseMapFile(path, true);
    if (errs == 0) fprintf(stderr, "map: %s — OK\n", path);
    else           fprintf(stderr, "map: %s — %d error(s)\n", path, errs);
    return errs;
}

void Level_LoadHardcodedFallback(void) {
    ClearLevel();
    strncpy(mapName, "Default (fallback)", sizeof mapName - 1);

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
    for (size_t i = 0; i < sizeof layout / sizeof layout[0]; i++) obstacles[obstacleCount++] = layout[i];

    interiorWalls[interiorWallCount++] = (Box){ { -21.5f, 2.5f, -20.0f }, { 37, 5, 1 } };
    interiorWalls[interiorWallCount++] = (Box){ {  21.5f, 2.5f, -20.0f }, { 37, 5, 1 } };

    doors[doorCount++] = (Door){ .box = { {0, 2.5f, -20}, {6, 5, 1} }, .cost = 1500, .opened = false };

    wallBuys[wallBuyCount++] = (WallBuy){ { 15.0f, 2.0f,  ARENA_HALF - 0.55f }, { 0,0,-1}, W_SHOTGUN };
    wallBuys[wallBuyCount++] = (WallBuy){ { ARENA_HALF - 0.55f, 2.0f, -10.0f }, {-1,0, 0}, W_SMG };
    wallBuys[wallBuyCount++] = (WallBuy){ {-15.0f, 2.0f, -ARENA_HALF + 0.55f }, { 0,0, 1}, W_RIFLE };
    wallBuys[wallBuyCount++] = (WallBuy){ {-ARENA_HALF + 0.55f, 2.0f, 18.0f }, { 1,0, 0}, W_RAYGUN };

    Window3D ws[] = {
        { .pos = { -18.0f, WALL_HEIGHT*0.5f,  ARENA_HALF }, .normal = { 0, 0, -1 }, .tangent = { 1, 0, 0 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 },
        { .pos = { ARENA_HALF, WALL_HEIGHT*0.5f, 18.0f }, .normal = { -1, 0, 0 }, .tangent = { 0, 0, 1 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 },
        { .pos = { 18.0f, WALL_HEIGHT*0.5f, -ARENA_HALF }, .normal = { 0, 0, 1 }, .tangent = { 1, 0, 0 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = 0 },
        { .pos = { -ARENA_HALF, WALL_HEIGHT*0.5f, -8.0f }, .normal = { 1, 0, 0 }, .tangent = { 0, 0, 1 },
          .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 },
    };
    for (size_t i = 0; i < sizeof ws / sizeof ws[0]; i++) windows[windowCount++] = ws[i];

    perkMachines[perkMachineCount++] = (PerkMachine){ { -8.0f, 0, 12.0f }, PERK_JUG };
    perkMachines[perkMachineCount++] = (PerkMachine){ {  4.0f, 0, -8.0f }, PERK_SPEED };
    perkMachines[perkMachineCount++] = (PerkMachine){ { 22.0f, 0, 25.0f }, PERK_DTAP };
    perkMachines[perkMachineCount++] = (PerkMachine){ {-25.0f, 0,-22.0f }, PERK_STAMIN };

    pap = (PackAPunch){ .pos = { 0, 0, -28.0f }, .activeTimer = 0, .slotInProgress = -1, .ownerPlayer = -1 };
    mbox = (MysteryBox){ .placed = true, .pos = { -22.0f, 0.5f, 15.0f },
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
    for (int i = 0; i < windowCount; i++) {
        windows[i].boards = MAX_BOARDS_PER_WIN;
        windows[i].repairProgress = 0;
        windows[i].repairPlayer = -1;
    }
    pap.activeTimer = 0;
    pap.slotInProgress = -1;
    pap.ownerPlayer = -1;
    mbox.state = MBOX_IDLE;
    mbox.timer = 0;
    mbox.ownerPlayer = -1;
    Decals_ClearAll();
}
