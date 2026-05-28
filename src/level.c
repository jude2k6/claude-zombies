#include "level.h"
#include "raymath.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

Box         obstacles[MAX_OBSTACLES];
int         obstacleCount = 0;
Box         interiorWalls[MAX_INTERIOR_WALLS];
int         interiorWallCount = 0;
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

Vector3 Level_ResolveXZ(Vector3 from, Vector3 to, float radius, bool clampArena) {
    Vector3 candidate = to;
    if (clampArena) {
        float lim = ARENA_HALF - radius;
        if (candidate.x >  lim) candidate.x =  lim;
        if (candidate.x < -lim) candidate.x = -lim;
        if (candidate.z >  lim) candidate.z =  lim;
        if (candidate.z < -lim) candidate.z = -lim;
    }

    Vector3 stepX = (Vector3){ candidate.x, from.y, from.z };
    for (int i = 0; i < obstacleCount; i++)
        if (Level_CircleHitsBoxXZ(stepX, radius, obstacles[i])) { stepX.x = from.x; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (Level_CircleHitsBoxXZ(stepX, radius, interiorWalls[i])) { stepX.x = from.x; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && Level_CircleHitsBoxXZ(stepX, radius, doors[i].box)) { stepX.x = from.x; break; }

    Vector3 stepZ = (Vector3){ stepX.x, from.y, candidate.z };
    for (int i = 0; i < obstacleCount; i++)
        if (Level_CircleHitsBoxXZ(stepZ, radius, obstacles[i])) { stepZ.z = from.z; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (Level_CircleHitsBoxXZ(stepZ, radius, interiorWalls[i])) { stepZ.z = from.z; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && Level_CircleHitsBoxXZ(stepZ, radius, doors[i].box)) { stepZ.z = from.z; break; }

    return stepZ;
}

bool Level_PointBlocked(Vector3 p, float pad) {
    for (int i = 0; i < obstacleCount; i++) {
        Box b = obstacles[i];
        b.size.x += pad*2; b.size.z += pad*2;
        if (Level_CircleHitsBoxXZ(p, 0.01f, b)) return true;
    }
    return false;
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
    doorCount = 0;
    wallBuyCount = 0;
    windowCount = 0;
    perkMachineCount = 0;
    mapSpawnCount = 0;
    mapName[0] = 0;
    pap = (PackAPunch){ .pos = {0,0,0}, .activeTimer = 0, .slotInProgress = -1, .ownerPlayer = -1 };
    mbox = (MysteryBox){ .placed = false, .pos = {0,0,0}, .state = MBOX_IDLE,
                         .timer = 0, .showingWeapon = 0, .finalWeapon = 0, .ownerPlayer = -1, .bob = 0 };
}

bool Level_LoadFromFile(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    ClearLevel();

    char raw[256];
    int lineNo = 0;
    while (fgets(raw, sizeof raw, f)) {
        lineNo++;
        char *hash = strchr(raw, '#');
        if (hash) *hash = 0;

        char *p = raw;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) continue;

        char key[32];
        if (sscanf(p, "%31s", key) != 1) continue;

        if (strcmp(key, "NAME") == 0) {
            // Take everything after "NAME " (preserve spaces)
            char *rest = p + 4;
            while (*rest && isspace((unsigned char)*rest)) rest++;
            char *nl = strchr(rest, '\n'); if (nl) *nl = 0;
            char *cr = strchr(rest, '\r'); if (cr) *cr = 0;
            strncpy(mapName, rest, sizeof mapName - 1);
            mapName[sizeof mapName - 1] = 0;
        }
        else if (strcmp(key, "SPAWN") == 0) {
            float x, z;
            if (sscanf(p, "%*s %f %f", &x, &z) == 2 && mapSpawnCount < NET_MAX_PLAYERS) {
                mapSpawns[mapSpawnCount++] = (Vector3){ x, PLAYER_EYE, z };
            }
        }
        else if (strcmp(key, "OBSTACLE") == 0) {
            float cx, cy, cz, sx, sy, sz;
            if (sscanf(p, "%*s %f %f %f %f %f %f", &cx, &cy, &cz, &sx, &sy, &sz) == 6
                && obstacleCount < MAX_OBSTACLES) {
                obstacles[obstacleCount++] = (Box){ {cx, cy, cz}, {sx, sy, sz} };
            }
        }
        else if (strcmp(key, "WALL") == 0) {
            float cx, cy, cz, sx, sy, sz;
            if (sscanf(p, "%*s %f %f %f %f %f %f", &cx, &cy, &cz, &sx, &sy, &sz) == 6
                && interiorWallCount < MAX_INTERIOR_WALLS) {
                interiorWalls[interiorWallCount++] = (Box){ {cx, cy, cz}, {sx, sy, sz} };
            }
        }
        else if (strcmp(key, "DOOR") == 0) {
            float cx, cy, cz, sx, sy, sz; int cost;
            if (sscanf(p, "%*s %f %f %f %f %f %f %d", &cx, &cy, &cz, &sx, &sy, &sz, &cost) == 7
                && doorCount < MAX_DOORS) {
                doors[doorCount++] = (Door){
                    .box = (Box){ {cx, cy, cz}, {sx, sy, sz} },
                    .cost = cost, .opened = false,
                };
            }
        }
        else if (strcmp(key, "WALLBUY") == 0) {
            float x, z, nx, nz; char wname[16];
            if (sscanf(p, "%*s %f %f %f %f %15s", &x, &z, &nx, &nz, wname) == 5
                && wallBuyCount < 8) {
                int widx = WeaponNameToIdx(wname);
                if (widx >= 0) {
                    wallBuys[wallBuyCount++] = (WallBuy){
                        .pos = (Vector3){ x, 2.0f, z },
                        .normal = (Vector3){ nx, 0, nz },
                        .weaponIdx = widx,
                    };
                } else {
                    fprintf(stderr, "map: unknown weapon '%s' on line %d\n", wname, lineNo);
                }
            }
        }
        else if (strcmp(key, "WINDOW") == 0) {
            float x, z, nx, nz; int lockedBy;
            if (sscanf(p, "%*s %f %f %f %f %d", &x, &z, &nx, &nz, &lockedBy) == 5
                && windowCount < MAX_WINDOWS) {
                Vector3 normal = { nx, 0, nz };
                Vector3 tangent = { -nz, 0, nx };
                windows[windowCount++] = (Window3D){
                    .pos = (Vector3){ x, WALL_HEIGHT*0.5f, z },
                    .normal = normal, .tangent = tangent,
                    .boards = MAX_BOARDS_PER_WIN,
                    .repairProgress = 0, .repairPlayer = -1,
                    .lockedByDoor = lockedBy,
                };
            }
        }
        else if (strcmp(key, "PERK") == 0) {
            float x, z; char pname[16];
            if (sscanf(p, "%*s %f %f %15s", &x, &z, pname) == 3
                && perkMachineCount < PERK_COUNT) {
                int pidx = PerkNameToIdx(pname);
                if (pidx >= 0) {
                    perkMachines[perkMachineCount++] = (PerkMachine){ {x, 0, z}, pidx };
                } else {
                    fprintf(stderr, "map: unknown perk '%s' on line %d\n", pname, lineNo);
                }
            }
        }
        else if (strcmp(key, "PAP") == 0) {
            float x, z;
            if (sscanf(p, "%*s %f %f", &x, &z) == 2) {
                pap = (PackAPunch){ .pos = {x, 0, z}, .activeTimer = 0, .slotInProgress = -1, .ownerPlayer = -1 };
            }
        }
        else if (strcmp(key, "MBOX") == 0) {
            float x, z;
            if (sscanf(p, "%*s %f %f", &x, &z) == 2) {
                mbox = (MysteryBox){ .placed = true, .pos = {x, 0.5f, z},
                                     .state = MBOX_IDLE, .timer = 0,
                                     .showingWeapon = 0, .finalWeapon = 0,
                                     .ownerPlayer = -1, .bob = 0 };
            }
        }
        else {
            fprintf(stderr, "map: unknown key '%s' on line %d\n", key, lineNo);
        }
    }
    fclose(f);

    if (mapSpawnCount == 0) {
        mapSpawns[mapSpawnCount++] = (Vector3){ 0, PLAYER_EYE, 0 };
    }
    fprintf(stderr, "map: loaded '%s' from %s\n", mapName, path);
    return true;
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
}
