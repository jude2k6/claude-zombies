#include "entities.h"
#include "level.h"
#include "player.h"
#include "weapons.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

Enemy enemies[MAX_ENEMIES];
int   enemiesAlive = 0;
int   enemiesToSpawn = 0;
float spawnTimer = 0.0f;

Bullet bullets[MAX_BULLETS];

PowerUp powerUps[MAX_POWERUPS] = {0};
float   doublePointsTimer = 0.0f;
float   instaKillTimer    = 0.0f;

int Enemies_RoundHP(int r)        { return 30 + r * 22; }
float Enemies_RoundSpeed(int r)   { float s = 1.6f + r * 0.10f; return s > 4.5f ? 4.5f : s; }
int Enemies_RoundSpawnCount(int r){
    int base = 6 + r * 2;
    int active = Player_ActiveCount();
    if (active < 1) active = 1;
    return base + (active - 1) * 4;
}

int Enemies_CountAlive(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) n++;
    return n;
}

void Enemies_ClearAll(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = false;
    enemiesAlive = 0;
}

void Enemies_TrySpawn(int round) {
    if (windowCount == 0) return;
    int accessible[MAX_WINDOWS]; int na = 0;
    for (int i = 0; i < windowCount; i++) {
        int lk = windows[i].lockedByDoor;
        if (lk >= 0 && lk < doorCount && !doors[lk].opened) continue;
        accessible[na++] = i;
    }
    if (na == 0) return;
    int wi = accessible[rand() % na];
    Window3D *w = &windows[wi];

    Vector3 spawn = Vector3Subtract(w->pos, Vector3Scale(w->normal, Level_RandRange(4.0f, 6.0f)));
    spawn = Vector3Add(spawn, Vector3Scale(w->tangent, Level_RandRange(-1.2f, 1.2f)));
    spawn.y = ENEMY_HEIGHT * 0.5f;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) {
            enemies[i] = (Enemy){
                .pos = spawn,
                .hp = Enemies_RoundHP(round), .maxHp = Enemies_RoundHP(round),
                .alive = true,
                .bobPhase = Level_RandRange(0, 6.28f),
                .speed = Enemies_RoundSpeed(round),
                .state = ZS_OUTSIDE,
                .targetWindow = wi,
                .targetPlayer = -1,
            };
            enemiesAlive++;
            enemiesToSpawn--;
            return;
        }
    }
}

void Enemies_UpdateSpawns(int round, float dt) {
    if (enemiesToSpawn <= 0) return;
    if (Enemies_CountAlive() >= 24 + (Player_ActiveCount() - 1) * 4) return;
    spawnTimer -= dt;
    if (spawnTimer <= 0) {
        Enemies_TrySpawn(round);
        float gap = 1.2f - round * 0.05f;
        if (gap < 0.35f) gap = 0.35f;
        spawnTimer = gap;
    }
}

void Enemies_Separate(void) {
    float minD = ENEMY_RADIUS * 1.9f;
    float minD2 = minD * minD;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        if (enemies[i].state == ZS_AT_WINDOW) continue;
        for (int j = i + 1; j < MAX_ENEMIES; j++) {
            if (!enemies[j].alive) continue;
            if (enemies[j].state == ZS_AT_WINDOW) continue;
            float dx = enemies[i].pos.x - enemies[j].pos.x;
            float dz = enemies[i].pos.z - enemies[j].pos.z;
            float d2 = dx*dx + dz*dz;
            if (d2 < minD2 && d2 > 1e-6f) {
                float d = sqrtf(d2);
                float push = (minD - d) * 0.5f;
                float nx = dx / d, nz = dz / d;
                enemies[i].pos.x += nx * push;
                enemies[i].pos.z += nz * push;
                enemies[j].pos.x -= nx * push;
                enemies[j].pos.z -= nz * push;
            }
        }
    }
    float lim = ARENA_HALF - ENEMY_RADIUS;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive || enemies[i].state != ZS_INSIDE) continue;
        enemies[i].pos.x = Clamp(enemies[i].pos.x, -lim, lim);
        enemies[i].pos.z = Clamp(enemies[i].pos.z, -lim, lim);
    }
}

void Enemies_Update(float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        Enemy *e = &enemies[i];
        e->bobPhase += dt * 4.0f;
        if (e->touchTimer > 0) e->touchTimer -= dt;

        if (e->state == ZS_OUTSIDE) {
            Window3D *w = &windows[e->targetWindow];
            Vector3 to = Vector3Subtract(w->pos, e->pos);
            to.y = 0;
            float d = Vector3Length(to);
            if (d < 1.2f) {
                e->state = ZS_AT_WINDOW;
                e->attackTimer = ENEMY_BOARD_ATK * 0.4f;
                e->pos = (Vector3){ w->pos.x - w->normal.x * 0.5f, ENEMY_HEIGHT*0.5f, w->pos.z - w->normal.z * 0.5f };
            } else {
                Vector3 dir = Vector3Scale(to, 1.0f / d);
                e->pos = Vector3Add(e->pos, Vector3Scale(dir, e->speed * 0.7f * dt));
            }
        }
        else if (e->state == ZS_AT_WINDOW) {
            Window3D *w = &windows[e->targetWindow];
            if (w->boards <= 0) {
                e->pos = Vector3Add(w->pos, Vector3Scale(w->normal, 1.6f));
                e->pos.y = ENEMY_HEIGHT*0.5f;
                e->state = ZS_INSIDE;
                e->targetPlayer = Player_NearestAlive(e->pos);
            } else {
                e->attackTimer -= dt;
                if (e->attackTimer <= 0) {
                    w->boards--;
                    e->attackTimer = ENEMY_BOARD_ATK;
                }
            }
        }
        else {
            if (e->targetPlayer < 0 || !players[e->targetPlayer].active || !players[e->targetPlayer].alive
                || (rand() % 60) == 0) {
                e->targetPlayer = Player_NearestAlive(e->pos);
            }
            if (e->targetPlayer < 0) continue;
            Player *tp = &players[e->targetPlayer];
            Vector3 to = Vector3Subtract(tp->pos, e->pos);
            to.y = 0;
            float d = Vector3Length(to);
            if (d > 0.01f) {
                Vector3 dir = Vector3Scale(to, 1.0f / d);
                Vector3 want = Vector3Add(e->pos, Vector3Scale(dir, e->speed * dt));
                e->pos = Level_ResolveXZ(e->pos, want, ENEMY_RADIUS, true);
            }

            Vector2 pXZ = { tp->pos.x, tp->pos.z };
            Vector2 eXZ = { e->pos.x, e->pos.z };
            if (Vector2Distance(pXZ, eXZ) < PLAYER_RADIUS + ENEMY_RADIUS + 0.1f && e->touchTimer <= 0) {
                bool cheatProtected = godMode && (int)(tp - players) == localPlayerIdx;
                if (!cheatProtected) {
                    tp->hp -= ENEMY_DAMAGE;
                    tp->damageFlash = 0.5f;
                    if (tp->hp <= 0) { tp->hp = 0; tp->alive = false; }
                }
                e->touchTimer = ENEMY_TOUCH_COOLDOWN;
            }
        }
    }
}

// ----- Bullets -----

void Bullets_ClearAll(void) {
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].alive = false;
}

void Bullets_Spawn(Vector3 origin, Vector3 dir, int damage, int ownerPlayer) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) {
            bullets[i].pos    = Vector3Add(origin, Vector3Scale(dir, 0.4f));
            bullets[i].vel    = Vector3Scale(dir, BULLET_SPEED);
            bullets[i].damage = damage;
            bullets[i].life   = BULLET_LIFE;
            bullets[i].alive  = true;
            bullets[i].ownerPlayer = ownerPlayer;
            return;
        }
    }
}

void Bullets_Update(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) continue;
        bullets[i].life -= dt;
        if (bullets[i].life <= 0) { bullets[i].alive = false; continue; }
        bullets[i].pos = Vector3Add(bullets[i].pos, Vector3Scale(bullets[i].vel, dt));

        for (int j = 0; j < obstacleCount; j++) {
            if (CheckCollisionBoxSphere(Level_BoxToBB(obstacles[j]), bullets[i].pos, 0.1f)) {
                bullets[i].alive = false; break;
            }
        }
        if (!bullets[i].alive) continue;

        float ax = fabsf(bullets[i].pos.x), az = fabsf(bullets[i].pos.z);
        if (ax > ARENA_HALF + 4.0f || az > ARENA_HALF + 4.0f) { bullets[i].alive = false; continue; }

        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].alive) continue;
            float dx = bullets[i].pos.x - enemies[e].pos.x;
            float dz = bullets[i].pos.z - enemies[e].pos.z;
            float dy = bullets[i].pos.y - enemies[e].pos.y;
            // Hit envelope: body cylinder of radius r, plus a slightly wider
            // head sphere on top.
            float r  = ENEMY_RADIUS + 0.15f;
            bool bodyHit = (dx*dx + dz*dz < r*r) && fabsf(dy) < ENEMY_HEIGHT*0.6f;
            float headDy = dy - (ENEMY_HEIGHT * 0.5f + 0.3f);
            float headR  = 0.35f;
            bool headHit = (dx*dx + dz*dz < headR*headR) && fabsf(headDy) < headR;
            if (bodyHit || headHit) {
                int dmg = bullets[i].damage * (headHit ? 2 : 1);
                if (instaKillTimer > 0) dmg = 99999;
                enemies[e].hp -= dmg;
                bullets[i].alive = false;
                int op = bullets[i].ownerPlayer;
                int hitPts  = HIT_POINTS + (headHit ? 30 : 0);
                int killPts = KILL_POINTS + (headHit ? 50 : 0);
                if (doublePointsTimer > 0) { hitPts *= 2; killPts *= 2; }
                if (op >= 0 && op < NET_MAX_PLAYERS && players[op].active) {
                    players[op].points += hitPts;
                    if (enemies[e].hp <= 0) {
                        enemies[e].alive = false;
                        enemiesAlive--;
                        players[op].points += killPts;
                        PowerUps_TryDrop(enemies[e].pos);
                    }
                } else if (enemies[e].hp <= 0) {
                    enemies[e].alive = false;
                    enemiesAlive--;
                    PowerUps_TryDrop(enemies[e].pos);
                }
                break;
            }
        }
    }
}

// ============================================================================
//  Power-ups
// ============================================================================

void PowerUps_ClearAll(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) powerUps[i].active = false;
    doublePointsTimer = 0;
    instaKillTimer = 0;
}

void PowerUps_TryDrop(Vector3 pos) {
    if (Level_RandRange(0, 1) > POWERUP_DROP_CHANCE) return;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) {
            powerUps[i].active = true;
            powerUps[i].type = (PowerUpType)(rand() % PU_COUNT);
            powerUps[i].pos = (Vector3){ pos.x, 0.6f, pos.z };
            powerUps[i].bob = Level_RandRange(0, 6.28f);
            powerUps[i].lifetime = POWERUP_LIFETIME;
            return;
        }
    }
}

void PowerUps_Update(float dt) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        powerUps[i].bob += dt * 3.0f;
        powerUps[i].lifetime -= dt;
        if (powerUps[i].lifetime <= 0) powerUps[i].active = false;
    }
    if (doublePointsTimer > 0) doublePointsTimer -= dt;
    if (instaKillTimer    > 0) instaKillTimer    -= dt;
}

void PowerUps_Apply(PowerUpType type) {
    switch (type) {
        case PU_MAX_AMMO:
            for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                if (!players[i].active) continue;
                for (int s = 0; s < INV_SLOTS; s++) {
                    if (!players[i].inventory[s].owned) continue;
                    int cap = WEAPONS[players[i].inventory[s].weaponIdx].reserveMax;
                    if (players[i].inventory[s].packed) cap += 60;
                    players[i].inventory[s].reserve = cap;
                }
            }
            break;
        case PU_NUKE:
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (!enemies[i].alive) continue;
                enemies[i].alive = false;
                enemiesAlive--;
            }
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (players[i].active) players[i].points += 400;
            break;
        case PU_DOUBLE_POINTS:
            doublePointsTimer = 20.0f;
            break;
        case PU_INSTAKILL:
            instaKillTimer = 20.0f;
            break;
        case PU_CARPENTER:
            for (int i = 0; i < windowCount; i++) windows[i].boards = MAX_BOARDS_PER_WIN;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (players[i].active) players[i].points += 200;
            break;
        default: break;
    }
}

void PowerUps_Pickup(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        for (int pi = 0; pi < NET_MAX_PLAYERS; pi++) {
            if (!players[pi].active || !players[pi].alive) continue;
            float dx = players[pi].pos.x - powerUps[i].pos.x;
            float dz = players[pi].pos.z - powerUps[i].pos.z;
            if (dx*dx + dz*dz < POWERUP_PICKUP_R * POWERUP_PICKUP_R) {
                PowerUps_Apply(powerUps[i].type);
                powerUps[i].active = false;
                break;
            }
        }
    }
}
