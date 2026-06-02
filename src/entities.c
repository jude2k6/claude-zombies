#include "entities.h"
#include "level.h"
#include "player.h"
#include "weapons.h"
#include "fx.h"
#include "decals.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

Enemy enemies[MAX_ENEMIES];
int   enemiesAlive = 0;
int   enemiesToSpawn = 0;
float spawnTimer = 0.0f;

Bullet bullets[MAX_BULLETS];

Throwable throwables[MAX_THROWABLES];

PowerUp powerUps[MAX_POWERUPS] = {0};
float   doublePointsTimer = 0.0f;
float   instaKillTimer    = 0.0f;

// Canonical CoD zombies health curve: linear +100/round through round 9,
// then a compounding +10% each round from round 10 on.
int Enemies_RoundHP(int r) {
    if (r < 1) r = 1;
    if (r <= 9) return 150 + (r - 1) * 100;   // R1=150 … R9=950
    float hp = 950.0f;
    for (int i = 10; i <= r; i++) hp *= 1.1f;
    return (int)hp;
}
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
                // Stagger runner first-lunge so they don't all dash on frame 1
                .specialTimer = (t == ZT_RUNNER) ? -Level_RandRange(0.5f, 2.5f) : 0.0f,
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
        if (e->stunTimer  > 0) e->stunTimer  -= dt;

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
            // Retarget cadence varies by type: runners switch fast, crawlers
            // commit, normals + bosses sit in the middle.
            int retargetMod = 60;
            if      (e->type == ZT_RUNNER)  retargetMod = 25;
            else if (e->type == ZT_CRAWLER) retargetMod = 100;
            if (e->targetPlayer < 0 || !players[e->targetPlayer].active || !players[e->targetPlayer].alive
                || (rand() % retargetMod) == 0) {
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

            // Per-type movement modulation
            e->specialTimer -= dt;
            float speedMul = 1.0f;
            Vector3 moveDir = dirGoal;

            switch (e->type) {
                case ZT_RUNNER: {
                    // Two-phase lunge: 0.20s wind-up tell (no speed boost,
                    // visual cue rendered in DrawZombie), then 0.55s
                    // actual lunge at ~1.9x speed. ~3s cooldown after.
                    // specialTimer encoding while active:
                    //   (0.55, 0.75]  -> winding up
                    //   (0,    0.55]  -> lunging
                    //   <= 0          -> cooldown counting down to -2.5
                    bool active = (e->specialTimer > 0);
                    if (!active && e->specialTimer < -2.5f && d > 1.2f && d < 9.0f &&
                        Level_PathClearXZ(e->pos, dirGoal, ENEMY_RADIUS, 3.0f)) {
                        e->specialTimer = 0.75f;  // wind-up first
                        active = true;
                    }
                    if (active && e->specialTimer <= 0.55f) {
                        // Past wind-up — kick the speed boost in
                        speedMul = 1.9f;
                    }
                    break;
                }
                case ZT_CRAWLER: {
                    // Serpentine weave — lateral sine around dirGoal makes
                    // them harder to track at range.
                    Vector3 perp = (Vector3){ -dirGoal.z, 0, dirGoal.x };
                    float wave = sinf(e->bobPhase * 1.6f) * 0.45f;
                    Vector3 w = Vector3Add(dirGoal, Vector3Scale(perp, wave));
                    float wl = Vector3Length(w);
                    if (wl > 1e-4f) moveDir = Vector3Scale(w, 1.0f / wl);
                    speedMul = 1.05f;
                    break;
                }
                case ZT_BOSS:
                    // Bosses grind straight forward; no weave, slight slow,
                    // but hit much harder on contact.
                    speedMul = 0.95f;
                    break;
                default: break;
            }

            // Proactive obstacle avoidance: if the immediate path is blocked
            // by anything solid, fan out angles and pick the smallest
            // deflection that gives us a clear lookahead. Commits to the
            // chosen direction briefly to avoid jitter at obstacle edges.
            // Skip while a commit is already in flight.
            if (e->escapeTimer <= 0 && d > 0.6f) {
                float lookahead = e->speed * speedMul * 0.45f;
                if (lookahead < 0.6f) lookahead = 0.6f;
                if (!Level_PathClearXZ(e->pos, moveDir, ENEMY_RADIUS + 0.05f, lookahead)) {
                    static const float baseAngles[] = { 30.0f, 60.0f, 90.0f, 125.0f };
                    int sign = e->stuckBias ? +1 : -1;
                    bool found = false;
                    for (int k = 0; k < 4 && !found; k++) {
                        for (int s = 0; s < 2 && !found; s++) {
                            float ang = baseAngles[k] * (s == 0 ? sign : -sign);
                            float rad = ang * DEG2RAD;
                            float c = cosf(rad), si = sinf(rad);
                            Vector3 cand = {
                                dirGoal.x * c - dirGoal.z * si, 0.0f,
                                dirGoal.x * si + dirGoal.z * c
                            };
                            if (Level_PathClearXZ(e->pos, cand, ENEMY_RADIUS + 0.05f, lookahead)) {
                                e->escapeDir = cand;
                                e->escapeTimer = 0.30f;
                                found = true;
                            }
                        }
                    }
                    if (!found) e->stuckBias = !e->stuckBias;
                }
            }

            // Stun grenades freeze the enemy in place and stop bites. Apply
            // after per-type speed mods so it overrides everything.
            if (e->stunTimer > 0) speedMul *= 0.20f;

            Vector3 oldPos = e->pos;
            if (d > 0.01f) {
                Vector3 useDir = moveDir;
                if (e->escapeTimer > 0) {
                    useDir = e->escapeDir;
                    e->escapeTimer -= dt;
                }
                Vector3 want = Vector3Add(e->pos, Vector3Scale(useDir, e->speed * speedMul * dt));
                e->pos = Level_ResolveXZ(e->pos, want, ENEMY_RADIUS, true);
            }

            // Reactive fallback: even after the probe + commit, if we barely
            // moved this frame, sidestep perpendicular for a beat. Catches
            // wedged-in-corner cases the probe couldn't resolve.
            float mdx = e->pos.x - oldPos.x;
            float mdz = e->pos.z - oldPos.z;
            float thresh = e->speed * speedMul * dt * 0.3f;
            if ((mdx*mdx + mdz*mdz) < (thresh*thresh) && d > 0.5f && e->escapeTimer <= 0) {
                e->escapeDir = e->stuckBias
                    ? (Vector3){ -dirGoal.z, 0,  dirGoal.x }
                    : (Vector3){  dirGoal.z, 0, -dirGoal.x };
                e->escapeTimer = 0.35f;
                e->stuckBias = !e->stuckBias;
            }

            Vector2 pXZ = { tp->pos.x, tp->pos.z };
            Vector2 eXZ = { e->pos.x, e->pos.z };
            if (Vector2Distance(pXZ, eXZ) < PLAYER_RADIUS + ENEMY_RADIUS + 0.1f && e->touchTimer <= 0 && e->stunTimer <= 0) {
                bool cheatProtected = godMode && (int)(tp - players) == localPlayerIdx;
                // Downed players are incapacitated — only the bleed timer kills them.
                if (tp->downed) cheatProtected = true;
                float ccd = ENEMY_TOUCH_COOLDOWN;
                if      (e->type == ZT_RUNNER) ccd *= 0.80f;
                else if (e->type == ZT_BOSS)   ccd *= 1.30f;
                if (!cheatProtected) {
                    int dmg = ENEMY_DAMAGE;
                    switch (e->type) {
                        case ZT_RUNNER:  dmg = (int)(ENEMY_DAMAGE * 0.80f); break;  // 40
                        case ZT_CRAWLER: dmg = ENEMY_DAMAGE;                break;  // 50
                        case ZT_BOSS:    dmg = ENEMY_DAMAGE * 2;            break;  // 100
                        default: break;
                    }
                    tp->hp -= dmg;
                    tp->damageFlash = 0.5f;
                    tp->regenTimer = 0.0f;
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
                        float kickAmt = tp->hp <= 0 ? 0.65f
                                      : (e->type == ZT_BOSS ? 0.50f : 0.30f);
                        Fx_PunchAndRumble(kickAmt, 0.55f, 0.55f, 0.15f);
                    }
                }
                e->touchTimer = ccd;
            }
        }
    }
}

// ----- Bullets -----

void Bullets_ClearAll(void) {
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].alive = false;
}

void Bullets_Spawn(Vector3 origin, Vector3 dir, float speed, float life,
                   int damage, int weaponIdx, int ownerPlayer) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) {
            bullets[i].pos    = origin;
            bullets[i].origin = origin;
            bullets[i].vel    = Vector3Scale(dir, speed);
            bullets[i].damage = damage;
            bullets[i].weaponIdx = weaponIdx;
            bullets[i].life   = life;
            bullets[i].alive  = true;
            bullets[i].ownerPlayer = ownerPlayer;
            return;
        }
    }
}

// Swept segment vs axis-aligned box (slab method). Returns true if the
// segment a→b crosses the box and writes the entry time t in [0,1], the
// hit point, and the outward face normal.
static bool SegmentBoxHit(Vector3 a, Vector3 b, Box box,
                          float *outT, Vector3 *outHit, Vector3 *outNormal) {
    Vector3 d = Vector3Subtract(b, a);
    float halfX = box.size.x * 0.5f;
    float halfY = box.size.y * 0.5f;
    float halfZ = box.size.z * 0.5f;
    float bounds[3][2] = {
        { box.center.x - halfX, box.center.x + halfX },
        { box.center.y - halfY, box.center.y + halfY },
        { box.center.z - halfZ, box.center.z + halfZ },
    };
    float pa[3] = { a.x, a.y, a.z };
    float pd[3] = { d.x, d.y, d.z };
    float tNear = 0.0f, tFar = 1.0f;
    int axis = -1;
    for (int k = 0; k < 3; k++) {
        if (fabsf(pd[k]) < 1e-6f) {
            if (pa[k] < bounds[k][0] || pa[k] > bounds[k][1]) return false;
            continue;
        }
        float inv = 1.0f / pd[k];
        float t1 = (bounds[k][0] - pa[k]) * inv;
        float t2 = (bounds[k][1] - pa[k]) * inv;
        if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
        if (t1 > tNear) { tNear = t1; axis = k; }
        if (t2 < tFar)  tFar = t2;
        if (tNear > tFar) return false;
    }
    if (axis < 0 || tNear < 0 || tNear > 1) return false;
    *outT   = tNear;
    *outHit = Vector3Add(a, Vector3Scale(d, tNear));
    Vector3 n = { 0, 0, 0 };
    if (axis == 0) n.x = (pd[0] > 0) ? -1.0f : 1.0f;
    if (axis == 1) n.y = (pd[1] > 0) ? -1.0f : 1.0f;
    if (axis == 2) n.z = (pd[2] > 0) ? -1.0f : 1.0f;
    *outNormal = n;
    return true;
}

// Swept segment vs vertical cylinder (body) + sphere on top (head). On hit
// returns the t in [0,1] of the entry, plus whether the head zone was hit.
static bool SegmentEnemyHit(Vector3 a, Vector3 b, const Enemy *e,
                            float *outT, bool *outHead, Vector3 *outHitPos) {
    const float bodyR = ENEMY_RADIUS + 0.15f;
    const float headR = 0.40f;
    const float bodyYmin = e->pos.y - ENEMY_HEIGHT * 0.5f;
    const float bodyYmax = e->pos.y + ENEMY_HEIGHT * 0.5f;
    const float headY    = e->pos.y + ENEMY_HEIGHT * 0.5f + 0.30f;

    Vector3 d = Vector3Subtract(b, a);
    float bestT = 2.0f;
    bool  bestHead = false;

    // Body cylinder (XZ disk extruded along Y)
    {
        float fx = a.x - e->pos.x, fz = a.z - e->pos.z;
        float A = d.x*d.x + d.z*d.z;
        float B = 2.0f * (fx*d.x + fz*d.z);
        float C = fx*fx + fz*fz - bodyR*bodyR;
        if (A > 1e-8f) {
            float disc = B*B - 4*A*C;
            if (disc >= 0) {
                float sq = sqrtf(disc);
                float t0 = (-B - sq) / (2*A);
                float t1 = (-B + sq) / (2*A);
                float t = (t0 >= 0) ? t0 : t1;
                if (t >= 0 && t <= 1) {
                    float hy = a.y + d.y * t;
                    if (hy >= bodyYmin && hy <= bodyYmax && t < bestT) {
                        bestT = t; bestHead = false;
                    }
                }
            }
        } else if (C <= 0) {
            // Segment is purely vertical and inside the cylinder XZ
            if (a.y <= bodyYmax && a.y >= bodyYmin) { bestT = 0; bestHead = false; }
        }
    }

    // Head sphere
    {
        Vector3 f = { a.x - e->pos.x, a.y - headY, a.z - e->pos.z };
        float A = d.x*d.x + d.y*d.y + d.z*d.z;
        float B = 2.0f * (f.x*d.x + f.y*d.y + f.z*d.z);
        float C = f.x*f.x + f.y*f.y + f.z*f.z - headR*headR;
        if (A > 1e-8f) {
            float disc = B*B - 4*A*C;
            if (disc >= 0) {
                float sq = sqrtf(disc);
                float t0 = (-B - sq) / (2*A);
                float t1 = (-B + sq) / (2*A);
                float t = (t0 >= 0) ? t0 : t1;
                if (t >= 0 && t <= 1 && t < bestT) {
                    bestT = t; bestHead = true;
                }
            }
        }
    }

    if (bestT > 1.0f) return false;
    *outT = bestT;
    *outHead = bestHead;
    *outHitPos = Vector3Add(a, Vector3Scale(d, bestT));
    return true;
}

void Bullets_Update(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) continue;
        bullets[i].life -= dt;
        if (bullets[i].life <= 0) { bullets[i].alive = false; continue; }

        Vector3 a = bullets[i].pos;
        Vector3 b = Vector3Add(a, Vector3Scale(bullets[i].vel, dt));

        float    bestT = 2.0f;
        bool     hitIsEnemy = false;
        int      enemyIdx   = -1;
        bool     headHit    = false;
        Vector3  hitPos     = b;
        Vector3  hitNormal  = { 0, 1, 0 };

        // Enemies (target the bullet, take priority on tie)
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].alive) continue;
            float t; bool head; Vector3 hp;
            if (!SegmentEnemyHit(a, b, &enemies[e], &t, &head, &hp)) continue;
            if (t < bestT) {
                bestT = t; hitIsEnemy = true; enemyIdx = e; headHit = head;
                hitPos = hp;
            }
        }

        // Static geometry: obstacles, interior walls, closed doors
        for (int j = 0; j < obstacleCount; j++) {
            float t; Vector3 hp, hn;
            if (SegmentBoxHit(a, b, obstacles[j], &t, &hp, &hn) && t < bestT) {
                bestT = t; hitIsEnemy = false; hitPos = hp; hitNormal = hn;
            }
        }
        for (int j = 0; j < interiorWallCount; j++) {
            float t; Vector3 hp, hn;
            if (SegmentBoxHit(a, b, interiorWalls[j], &t, &hp, &hn) && t < bestT) {
                bestT = t; hitIsEnemy = false; hitPos = hp; hitNormal = hn;
            }
        }
        for (int j = 0; j < doorCount; j++) {
            if (doors[j].opened) continue;
            float t; Vector3 hp, hn;
            if (SegmentBoxHit(a, b, doors[j].box, &t, &hp, &hn) && t < bestT) {
                bestT = t; hitIsEnemy = false; hitPos = hp; hitNormal = hn;
            }
        }

        // Floor (y = 0 plane)
        if (a.y > 0.0f && b.y <= 0.0f) {
            float t = a.y / (a.y - b.y);
            if (t < bestT) {
                bestT = t; hitIsEnemy = false;
                hitPos = Vector3Add(a, Vector3Scale(Vector3Subtract(b, a), t));
                hitPos.y = 0.0f;
                hitNormal = (Vector3){ 0, 1, 0 };
            }
        }

        if (bestT > 1.0f) {
            // No hit this step — advance and life-out on arena escape.
            bullets[i].pos = b;
            float ax = fabsf(b.x), az = fabsf(b.z);
            if (ax > ARENA_HALF + 4.0f || az > ARENA_HALF + 4.0f)
                bullets[i].alive = false;
            continue;
        }

        bullets[i].pos   = hitPos;
        bullets[i].alive = false;

        // Splash AoE on impact (raygun-style). Applies regardless of
        // whether the bullet hit a zombie or static geometry; never
        // damages the directly-hit enemy (already handled below).
        int splashOwner = bullets[i].ownerPlayer;
        int splashWi    = bullets[i].weaponIdx;
        bool doSplash = false;
        float splashR  = 0.0f;
        int   splashDmg = 0;
        if (splashWi >= 0 && splashWi < W_COUNT) {
            const WeaponDef *swd = &WEAPONS[splashWi];
            if (swd->splashRadius > 0.001f && swd->splashDamage > 0) {
                doSplash  = true;
                splashR   = swd->splashRadius;
                splashDmg = swd->splashDamage;
            }
        }

        if (hitIsEnemy) {
            Enemy *en = &enemies[enemyIdx];
            // Damage dropoff: linear from dropoffStart..dropoffEnd, clamped
            // to dropoffMinMul. Distance is hit point vs spawn origin.
            float dropMul = 1.0f;
            int wi = bullets[i].weaponIdx;
            if (wi >= 0 && wi < W_COUNT) {
                const WeaponDef *wd = &WEAPONS[wi];
                if (wd->dropoffEnd > wd->dropoffStart) {
                    float dist = Vector3Distance(hitPos, bullets[i].origin);
                    if (dist > wd->dropoffStart) {
                        float t = (dist - wd->dropoffStart) / (wd->dropoffEnd - wd->dropoffStart);
                        if (t > 1.0f) t = 1.0f;
                        dropMul = 1.0f - t * (1.0f - wd->dropoffMinMul);
                    }
                }
            }
            int dmg = (int)(bullets[i].damage * dropMul) * (headHit ? 2 : 1);
            if (dmg < 1) dmg = 1;
            if (instaKillTimer > 0) dmg = 99999;
            en->hp -= dmg;
            int op = bullets[i].ownerPlayer;
            // CoD scoring: 10 per hitmarker, 60 for a body kill (10+50),
            // 100 for a headshot kill (10+90). Non-killing headshots still
            // only score the base 10.
            int hitPts  = HIT_POINTS;
            int killPts = KILL_POINTS + (headHit ? 40 : 0);
            if (doublePointsTimer > 0) { hitPts *= 2; killPts *= 2; }
            if (op >= 0 && op < NET_MAX_PLAYERS && players[op].active) {
                players[op].points += hitPts;
                players[op].shotsHit++;
                if (headHit) players[op].headshots++;
                if (en->hp <= 0) {
                    en->alive = false;
                    enemiesAlive--;
                    players[op].points += killPts;
                    players[op].kills++;
                    PowerUps_TryDrop(en->pos);
                }
            } else if (en->hp <= 0) {
                en->alive = false;
                enemiesAlive--;
                PowerUps_TryDrop(en->pos);
            }
            // Blood faces back toward the shooter
            Vector3 vn = Vector3Normalize(Vector3Negate(bullets[i].vel));
            float sz = 0.18f + (rand() % 100) / 100.0f * 0.10f;
            Decals_Spawn(DECAL_BLOOD, hitPos, vn, sz);
        } else {
            float sz = 0.10f + (rand() % 100) / 100.0f * 0.05f;
            Decals_Spawn(DECAL_IMPACT, hitPos, hitNormal, sz);
        }

        if (doSplash) {
            int hitKillPts = KILL_POINTS;
            int hitHitPts  = HIT_POINTS;
            if (doublePointsTimer > 0) { hitKillPts *= 2; hitHitPts *= 2; }
            for (int e = 0; e < MAX_ENEMIES; e++) {
                if (!enemies[e].alive) continue;
                if (hitIsEnemy && e == enemyIdx) continue;  // direct-hit already took damage
                float dxs = enemies[e].pos.x - hitPos.x;
                float dys = enemies[e].pos.y - hitPos.y;
                float dzs = enemies[e].pos.z - hitPos.z;
                float dd  = sqrtf(dxs*dxs + dys*dys + dzs*dzs);
                if (dd > splashR) continue;
                float mul = 1.0f - (dd / splashR);
                int dmg = (int)(splashDmg * mul);
                if (dmg < 1) continue;
                if (instaKillTimer > 0) dmg = 99999;
                enemies[e].hp -= dmg;
                if (splashOwner >= 0 && splashOwner < NET_MAX_PLAYERS && players[splashOwner].active) {
                    players[splashOwner].points += hitHitPts;
                }
                if (enemies[e].hp <= 0) {
                    enemies[e].alive = false;
                    enemiesAlive--;
                    if (splashOwner >= 0 && splashOwner < NET_MAX_PLAYERS && players[splashOwner].active) {
                        players[splashOwner].points += hitKillPts;
                        players[splashOwner].kills++;
                    }
                    PowerUps_TryDrop(enemies[e].pos);
                }
                // Blood mist on each splashed enemy
                Vector3 vn2 = (Vector3){ -dxs/(dd+1e-4f), -dys/(dd+1e-4f), -dzs/(dd+1e-4f) };
                Decals_Spawn(DECAL_BLOOD, enemies[e].pos, vn2, 0.18f);
            }
        }
    }
}

// ============================================================================
//  Throwables (frag / stun grenades)
// ============================================================================

void Throwables_ClearAll(void) {
    for (int i = 0; i < MAX_THROWABLES; i++) throwables[i].alive = false;
}

void Throwables_Throw(Player *p, ThrowableKind kind) {
    if (!p || !p->alive || p->downed) return;
    if (kind == TH_FRAG && p->lethals   <= 0) return;
    if (kind == TH_STUN && p->tacticals <= 0) return;

    Vector3 look = Player_LookDir(p->yaw, p->pitch);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(look, (Vector3){0,1,0}));
    Vector3 origin = p->pos;
    origin = Vector3Add(origin, Vector3Scale(look,  0.45f));
    origin = Vector3Add(origin, Vector3Scale(right, 0.20f));
    origin.y -= 0.15f;

    Vector3 vel = Vector3Scale(look, THROW_SPEED);
    vel.y += THROW_UPKICK;

    for (int i = 0; i < MAX_THROWABLES; i++) {
        if (!throwables[i].alive) {
            throwables[i] = (Throwable){
                .alive       = true,
                .kind        = kind,
                .pos         = origin,
                .vel         = vel,
                .fuse        = FRAG_FUSE,
                .ownerPlayer = (int)(p - players),
                .spinPhase   = 0.0f,
            };
            if (kind == TH_FRAG) p->lethals--;
            else                 p->tacticals--;
            return;
        }
    }
}

void Throwables_Detonate(Throwable *t) {
    if (!t->alive) return;
    if (t->kind == TH_FRAG) {
        int killPts = KILL_POINTS;
        int hitPts  = HIT_POINTS;
        if (doublePointsTimer > 0) { killPts *= 2; hitPts *= 2; }
        int op = t->ownerPlayer;
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].alive) continue;
            float dx = enemies[e].pos.x - t->pos.x;
            float dy = enemies[e].pos.y - t->pos.y;
            float dz = enemies[e].pos.z - t->pos.z;
            float d  = sqrtf(dx*dx + dy*dy + dz*dz);
            if (d > FRAG_RADIUS) continue;
            float mul = 1.0f - (d / FRAG_RADIUS);
            int dmg = (int)(FRAG_DAMAGE * mul);
            if (dmg < 1) continue;
            if (instaKillTimer > 0) dmg = 99999;
            enemies[e].hp -= dmg;
            if (op >= 0 && op < NET_MAX_PLAYERS && players[op].active) {
                players[op].points += hitPts;
            }
            if (enemies[e].hp <= 0) {
                enemies[e].alive = false;
                enemiesAlive--;
                if (op >= 0 && op < NET_MAX_PLAYERS && players[op].active) {
                    players[op].points += killPts;
                    players[op].kills++;
                }
                PowerUps_TryDrop(enemies[e].pos);
            }
            Vector3 vn = (Vector3){ -dx/(d+1e-4f), -dy/(d+1e-4f), -dz/(d+1e-4f) };
            Decals_Spawn(DECAL_BLOOD, enemies[e].pos, vn, 0.22f);
        }
        // Camera kick for any nearby local player
        if (localPlayerIdx >= 0 && players[localPlayerIdx].active) {
            float dx = players[localPlayerIdx].pos.x - t->pos.x;
            float dy = players[localPlayerIdx].pos.y - t->pos.y;
            float dz = players[localPlayerIdx].pos.z - t->pos.z;
            float d  = sqrtf(dx*dx + dy*dy + dz*dz);
            if (d < FRAG_RADIUS * 1.5f) {
                float mul = 1.0f - (d / (FRAG_RADIUS * 1.5f));
                Fx_PunchAndRumble(0.55f * mul, 0.7f * mul, 0.6f * mul, 0.20f);
                // Frag damages the local player too (friendly fire by proximity).
                if (d < FRAG_RADIUS && !godMode && !players[localPlayerIdx].downed) {
                    int self = (int)(FRAG_DAMAGE * 0.40f * (1.0f - d / FRAG_RADIUS));
                    if (self > 0) {
                        players[localPlayerIdx].hp -= self;
                        players[localPlayerIdx].damageFlash = 0.6f;
                        players[localPlayerIdx].regenTimer = 0.0f;
                        if (players[localPlayerIdx].hp < 0) players[localPlayerIdx].hp = 0;
                    }
                }
            }
        }
    } else {
        // Stun: freeze every zombie in range for STUN_DURATION
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].alive) continue;
            float dx = enemies[e].pos.x - t->pos.x;
            float dy = enemies[e].pos.y - t->pos.y;
            float dz = enemies[e].pos.z - t->pos.z;
            float d  = sqrtf(dx*dx + dy*dy + dz*dz);
            if (d > STUN_RADIUS) continue;
            enemies[e].stunTimer = STUN_DURATION;
        }
        if (localPlayerIdx >= 0 && players[localPlayerIdx].active) {
            float dx = players[localPlayerIdx].pos.x - t->pos.x;
            float dz = players[localPlayerIdx].pos.z - t->pos.z;
            if (dx*dx + dz*dz < STUN_RADIUS * STUN_RADIUS * 4.0f) {
                Fx_PunchAndRumble(0.10f, 0.2f, 0.2f, 0.05f);
            }
        }
    }
    t->alive = false;
}

void Throwables_Update(float dt) {
    for (int i = 0; i < MAX_THROWABLES; i++) {
        Throwable *t = &throwables[i];
        if (!t->alive) continue;

        t->spinPhase += dt * 14.0f;

        // Gravity
        t->vel.y -= 18.0f * dt;
        Vector3 newPos = Vector3Add(t->pos, Vector3Scale(t->vel, dt));

        // XZ wall / obstacle bounce — Level_ResolveXZ clamps the position to
        // an unblocked spot; if either axis got clamped, reverse + damp the
        // corresponding velocity component.
        Vector3 horiz = Level_ResolveXZ(t->pos, newPos, THROWABLE_RADIUS, true);
        if (fabsf(horiz.x - newPos.x) > 1e-4f) { t->vel.x = -t->vel.x * 0.35f; newPos.x = horiz.x; }
        if (fabsf(horiz.z - newPos.z) > 1e-4f) { t->vel.z = -t->vel.z * 0.35f; newPos.z = horiz.z; }

        // Floor bounce — damp Y and bleed XZ to settle
        if (newPos.y < THROWABLE_RADIUS) {
            newPos.y = THROWABLE_RADIUS;
            if (t->vel.y < 0) {
                t->vel.y = -t->vel.y * 0.35f;
                t->vel.x *= 0.70f;
                t->vel.z *= 0.70f;
                if (fabsf(t->vel.y) < 0.3f) t->vel.y = 0.0f;
            }
        }
        t->pos = newPos;

        t->fuse -= dt;
        if (t->fuse <= 0) Throwables_Detonate(t);
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
