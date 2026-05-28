#include "entities.h"
#include "level.h"
#include "player.h"
#include "weapons.h"
#include "fx.h"
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

static ZombieType PickType(int round) {
    // Boss every 5 rounds: 1 boss per spawn, chance scaled small so most are normals
    if (round >= 5 && (round % 5) == 0 && Level_RandRange(0, 1) < 0.04f) return ZT_BOSS;
    if (round >= 7 && Level_RandRange(0, 1) < 0.18f) return ZT_CRAWLER;
    if (round >= 4 && Level_RandRange(0, 1) < 0.30f) return ZT_RUNNER;
    return ZT_NORMAL;
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

    ZombieType t = PickType(round);
    int   baseHp    = Enemies_RoundHP(round);
    float baseSpeed = Enemies_RoundSpeed(round);
    int   hp = baseHp; float spd = baseSpeed;
    switch (t) {
        case ZT_RUNNER:  hp = (int)(baseHp * 0.55f); spd = baseSpeed * 1.6f; break;
        case ZT_CRAWLER: hp = (int)(baseHp * 0.80f); spd = baseSpeed * 0.55f; break;
        case ZT_BOSS:    hp = baseHp * 6;            spd = baseSpeed * 0.85f; break;
        default: break;
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) {
            enemies[i] = (Enemy){
                .pos = spawn,
                .hp = hp, .maxHp = hp,
                .alive = true,
                .bobPhase = Level_RandRange(0, 6.28f),
                .speed = spd,
                .state = ZS_OUTSIDE,
                .type = t,
                .targetWindow = wi,
                .targetPlayer = -1,
                .hasWaypoint = false,
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

            // Route via an open door if one lies between us and the target.
            // Use a two-stage waypoint: first line up perpendicular to the
            // door on our side, then aim past it. Otherwise a steep approach
            // angle pins zombies against the wall instead of the doorway.
            Vector3 goal = tp->pos;
            e->hasWaypoint = false;
            for (int di = 0; di < doorCount; di++) {
                if (!doors[di].opened) continue;
                Box b = doors[di].box;
                bool wallAlongX = b.size.x > b.size.z;
                if (wallAlongX) {
                    float wz = b.center.z;
                    if ((e->pos.z - wz) * (tp->pos.z - wz) >= 0) continue;
                    float zombieSide = (e->pos.z > wz) ? 1.0f : -1.0f;
                    float halfX = b.size.x * 0.5f;
                    float off   = b.size.z * 0.5f + 1.5f;
                    if (fabsf(e->pos.x - b.center.x) > halfX - ENEMY_RADIUS) {
                        goal = (Vector3){ b.center.x, 0, wz + zombieSide * off };
                    } else {
                        goal = (Vector3){ b.center.x, 0, wz - zombieSide * off };
                    }
                    e->waypoint = goal; e->hasWaypoint = true;
                    break;
                } else {
                    float wx = b.center.x;
                    if ((e->pos.x - wx) * (tp->pos.x - wx) >= 0) continue;
                    float zombieSide = (e->pos.x > wx) ? 1.0f : -1.0f;
                    float halfZ = b.size.z * 0.5f;
                    float off   = b.size.x * 0.5f + 1.5f;
                    if (fabsf(e->pos.z - b.center.z) > halfZ - ENEMY_RADIUS) {
                        goal = (Vector3){ wx + zombieSide * off, 0, b.center.z };
                    } else {
                        goal = (Vector3){ wx - zombieSide * off, 0, b.center.z };
                    }
                    e->waypoint = goal; e->hasWaypoint = true;
                    break;
                }
            }

            Vector3 to = Vector3Subtract(goal, e->pos);
            to.y = 0;
            float d = Vector3Length(to);
            Vector3 dirGoal = (d > 0.01f) ? Vector3Scale(to, 1.0f / d) : (Vector3){0,0,0};

            Vector3 oldPos = e->pos;
            if (d > 0.01f) {
                Vector3 moveDir = dirGoal;
                if (e->escapeTimer > 0) {
                    moveDir = e->escapeDir;
                    e->escapeTimer -= dt;
                }
                Vector3 want = Vector3Add(e->pos, Vector3Scale(moveDir, e->speed * dt));
                e->pos = Level_ResolveXZ(e->pos, want, ENEMY_RADIUS, true);
            }

            // Stuck detection: if we wanted to move but barely did, commit to
            // a perpendicular sidestep for a beat. Alternate sides so we
            // probe both ways around an obstacle/wall.
            float mdx = e->pos.x - oldPos.x;
            float mdz = e->pos.z - oldPos.z;
            float thresh = e->speed * dt * 0.3f;
            if ((mdx*mdx + mdz*mdz) < (thresh*thresh) && d > 0.5f && e->escapeTimer <= 0) {
                e->escapeDir = e->stuckBias
                    ? (Vector3){ -dirGoal.z, 0,  dirGoal.x }
                    : (Vector3){  dirGoal.z, 0, -dirGoal.x };
                e->escapeTimer = 0.35f;
                e->stuckBias = e->stuckBias ? 0 : 1;
            }

            Vector2 pXZ = { tp->pos.x, tp->pos.z };
            Vector2 eXZ = { e->pos.x, e->pos.z };
            if (Vector2Distance(pXZ, eXZ) < PLAYER_RADIUS + ENEMY_RADIUS + 0.1f && e->touchTimer <= 0) {
                bool cheatProtected = godMode && (int)(tp - players) == localPlayerIdx;
                // Downed players are incapacitated — only the bleed timer kills them.
                if (tp->downed) cheatProtected = true;
                if (!cheatProtected) {
                    tp->hp -= ENEMY_DAMAGE;
                    tp->damageFlash = 0.5f;
                    if (tp->hp <= 0) {
                        tp->hp = 0;
                        // Drop into downed state if a teammate can possibly
                        // revive us, otherwise die outright (solo flow).
                        int otherUp = 0;
                        int meIdx = (int)(tp - players);
                        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                            if (j == meIdx) continue;
                            if (players[j].active && players[j].alive && !players[j].downed) otherUp++;
                        }
                        if (otherUp > 0) {
                            tp->downed = true;
                            tp->bleedTimer = BLEED_TIME;
                        } else {
                            tp->alive = false;
                        }
                    }
                    if ((int)(tp - players) == localPlayerIdx) {
                        Fx_PunchAndRumble(tp->hp <= 0 ? 0.65f : 0.30f,
                                          0.55f, 0.55f, 0.15f);
                    }
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
                    players[op].shotsHit++;
                    if (headHit) players[op].headshots++;
                    if (enemies[e].hp <= 0) {
                        enemies[e].alive = false;
                        enemiesAlive--;
                        players[op].points += killPts;
                        players[op].kills++;
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
            fxFlashAmount = 1.0f;
            Fx_PunchAndRumble(0.9f, 1.0f, 1.0f, 0.5f);
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
