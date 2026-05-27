#ifndef SHOOTER_ENTITIES_H
#define SHOOTER_ENTITIES_H

#include "types.h"

// Enemies
extern Enemy enemies[MAX_ENEMIES];
extern int   enemiesAlive;
extern int   enemiesToSpawn;
extern float spawnTimer;

int   Enemies_CountAlive(void);
int   Enemies_RoundHP(int round);
float Enemies_RoundSpeed(int round);
int   Enemies_RoundSpawnCount(int round);

void  Enemies_TrySpawn(int round);
void  Enemies_UpdateSpawns(int round, float dt);
void  Enemies_Update(float dt);
void  Enemies_Separate(void);
void  Enemies_ClearAll(void);

// Bullets
extern Bullet bullets[MAX_BULLETS];

void  Bullets_Spawn(Vector3 origin, Vector3 dir, int damage, int ownerPlayer);
void  Bullets_Update(float dt);
void  Bullets_ClearAll(void);

// Power-ups
extern PowerUp powerUps[MAX_POWERUPS];
extern float   doublePointsTimer;
extern float   instaKillTimer;

void PowerUps_ClearAll(void);
void PowerUps_TryDrop(Vector3 pos);
void PowerUps_Update(float dt);
void PowerUps_Pickup(void);              // checks all players for pickups
void PowerUps_Apply(PowerUpType type);   // server-side apply

#endif
