#include "level.h"
#include "raymath.h"
#include <stdlib.h>

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

void Level_Build(void) {
    obstacleCount = 0;
    Box layout[] = {
        { (Vector3){  10,  1.5f,   5 }, (Vector3){ 4, 3, 4 } },
        { (Vector3){ -12,  1.0f,  -8 }, (Vector3){ 6, 2, 3 } },
        { (Vector3){ -10,  1.5f, -16 }, (Vector3){ 4, 3, 4 } },
        { (Vector3){  18,  1.0f, -15 }, (Vector3){ 3, 2, 6 } },
        { (Vector3){ -20,  1.5f,  12 }, (Vector3){ 5, 3, 5 } },
        { (Vector3){   8,  1.0f,  20 }, (Vector3){ 7, 2, 2 } },
        { (Vector3){  -6,  1.5f,  26 }, (Vector3){ 3, 3, 3 } },
        { (Vector3){  25,  1.0f,   2 }, (Vector3){ 2, 2, 8 } },
    };
    int n = (int)(sizeof(layout) / sizeof(layout[0]));
    for (int i = 0; i < n && obstacleCount < MAX_OBSTACLES; i++) obstacles[obstacleCount++] = layout[i];

    wallBuyCount = 0;
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){ 15.0f, 2.0f,  ARENA_HALF - 0.55f }, (Vector3){ 0,0,-1}, W_SHOTGUN };
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){ ARENA_HALF - 0.55f, 2.0f, -10.0f }, (Vector3){-1,0, 0}, W_SMG };
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){-15.0f, 2.0f, -ARENA_HALF + 0.55f }, (Vector3){ 0,0, 1}, W_RIFLE };
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){-ARENA_HALF + 0.55f, 2.0f, 18.0f }, (Vector3){ 1,0, 0}, W_RAYGUN };

    windowCount = 0;
    windows[windowCount++] = (Window3D){
        .pos = { -18.0f, WALL_HEIGHT*0.5f,  ARENA_HALF }, .normal = { 0, 0, -1 }, .tangent = { 1, 0, 0 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 };
    windows[windowCount++] = (Window3D){
        .pos = { ARENA_HALF, WALL_HEIGHT*0.5f, 18.0f }, .normal = { -1, 0, 0 }, .tangent = { 0, 0, 1 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 };
    windows[windowCount++] = (Window3D){
        .pos = { 18.0f, WALL_HEIGHT*0.5f, -ARENA_HALF }, .normal = { 0, 0, 1 }, .tangent = { 1, 0, 0 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = 0 };
    windows[windowCount++] = (Window3D){
        .pos = { -ARENA_HALF, WALL_HEIGHT*0.5f, -8.0f }, .normal = { 1, 0, 0 }, .tangent = { 0, 0, 1 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 };

    interiorWallCount = 0;
    interiorWalls[interiorWallCount++] = (Box){ (Vector3){ -21.5f, WALL_HEIGHT*0.5f, -20.0f }, (Vector3){ 37.0f, WALL_HEIGHT, 1.0f } };
    interiorWalls[interiorWallCount++] = (Box){ (Vector3){  21.5f, WALL_HEIGHT*0.5f, -20.0f }, (Vector3){ 37.0f, WALL_HEIGHT, 1.0f } };

    doorCount = 0;
    doors[doorCount++] = (Door){
        .box = { (Vector3){ 0, WALL_HEIGHT*0.5f, -20.0f }, (Vector3){ 6.0f, WALL_HEIGHT, 1.0f } },
        .cost = 1500, .opened = false,
    };

    perkMachineCount = 0;
    perkMachines[perkMachineCount++] = (PerkMachine){ { -8.0f, 0, 12.0f }, PERK_JUG };
    perkMachines[perkMachineCount++] = (PerkMachine){ {  4.0f, 0, -8.0f }, PERK_SPEED };
    perkMachines[perkMachineCount++] = (PerkMachine){ { 22.0f, 0, 25.0f }, PERK_DTAP };
    perkMachines[perkMachineCount++] = (PerkMachine){ {-25.0f, 0,-22.0f }, PERK_STAMIN };

    pap = (PackAPunch){ .pos = { 0, 0, -28.0f }, .activeTimer = 0, .slotInProgress = -1, .ownerPlayer = -1 };
}
