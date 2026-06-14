#ifndef SHOOTER_ENTITIES_H
#define SHOOTER_ENTITIES_H

#include "types.h"
#include "world.h"   // enemies/bullets/throwables/powerUps now live in g_world

// Enemies (enemies[] is g_world.enemies via world.h)
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

// Bullets (bullets[] is g_world.bullets via world.h)
void  Bullets_Spawn(Vector3 origin, Vector3 dir, float speed, float life,
                    int damage, int weaponIdx, int ownerPlayer);
void  Bullets_Update(float dt);
void  Bullets_ClearAll(void);

// Throwables (frag / stun grenades; throwables[] is g_world.throwables via world.h)
void  Throwables_ClearAll(void);
void  Throwables_Throw(Player *p, ThrowableKind kind);
void  Throwables_Update(float dt);
void  Throwables_Detonate(Throwable *t);

// Power-ups (powerUps[]/doublePointsTimer/instaKillTimer all live in g_world)

void PowerUps_ClearAll(void);
void PowerUps_TryDrop(Vector3 pos);
void PowerUps_Update(float dt);
void PowerUps_Pickup(void);              // checks all players for pickups
void PowerUps_Apply(PowerUpType type);   // server-side apply

#endif
