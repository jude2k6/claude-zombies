#ifndef SHOOTER_LEVEL_H
#define SHOOTER_LEVEL_H

#include "types.h"

extern Box         obstacles[MAX_OBSTACLES];
extern int         obstacleCount;
extern Box         interiorWalls[MAX_INTERIOR_WALLS];
extern int         interiorWallCount;
extern Door        doors[MAX_DOORS];
extern int         doorCount;
extern WallBuy     wallBuys[8];
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

void  Level_Build(void);
bool  Level_LoadFromFile(const char *path);
void  Level_LoadHardcodedFallback(void);
void  Level_Reset(void);     // close doors, refill boards, clear PaP

// Collision helpers
BoundingBox Level_BoxToBB(Box b);
bool        Level_CircleHitsBoxXZ(Vector3 p, float r, Box b);
Vector3     Level_ResolveXZ(Vector3 from, Vector3 to, float radius, bool clampArena);
bool        Level_PointBlocked(Vector3 p, float pad);

float       Level_RandRange(float a, float b);

#endif
