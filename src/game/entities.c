#include "entities.h"
#include "level.h"
#include "player.h"
#include "weapons.h"
#include "mobs.h"
#include "mob_ai.h"
#include "fx.h"
#include "decals.h"
#include "particles.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// enemies[]/bullets[]/throwables[]/powerUps[] + doublePointsTimer/instaKillTimer
// now live in g_world (world.h).
int   enemiesAlive = 0;
int   enemiesToSpawn = 0;
float spawnTimer = 0.0f;

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
    // Pick a random accessible ZOMBIE-tagged spawn point (door-gated). Spawning
    // is now driven by generic mob spawn points, not the window list; a spawn
    // may be associated with a barricade (climbWindow) to climb on the way in.
    int accessible[MAX_MOB_SPAWNS]; int na = 0;
    for (int i = 0; i < g_world.mapMobSpawnCount; i++) {
        MobSpawn *m = &g_world.mapMobSpawns[i];
        if (strcmp(m->mob, "ZOMBIE") != 0) continue;
        int lk = m->lockedByDoor;
        if (lk >= 0 && lk < doorCount && !doors[lk].opened) continue;
        accessible[na++] = i;
    }
    if (na == 0) return;
    MobSpawn *ms = &g_world.mapMobSpawns[accessible[rand() % na]];
    int climbWin = ms->climbWindow;

    Vector3 spawn = ms->pos;
    spawn.x += Level_RandRange(-1.0f, 1.0f);
    spawn.z += Level_RandRange(-1.0f, 1.0f);
    spawn.y = ENEMY_HEIGHT * 0.5f;

    ZombieType t = PickType(round);

    // Stats are data-driven from the spawn's mob def: scale the round curve by
    // the mob's round-1 baselines (zombie.mob = 150 HP / 1.7 spd → ×1.0, i.e.
    // identical to the old hardcoded zombie). A missing catalog (e.g. a headless
    // devtool that never called Mobs_Load) falls back to the bare curve.
    int          mobIdx = Mob_FindIndex(ms->mob);
    const MobDef *md    = Mob_Get(mobIdx);
    float r1Hp  = (float)Enemies_RoundHP(1);     // 150 — the curve's round-1 base
    float r1Spd = Enemies_RoundSpeed(1);         // 1.7
    float hpScale  = (md && md->healthBase > 0) ? (float)md->healthBase / r1Hp : 1.0f;
    float spdScale = (md && md->moveSpeed  > 0) ? md->moveSpeed / r1Spd        : 1.0f;
    int   baseHp    = (int)(Enemies_RoundHP(round) * hpScale);
    float baseSpeed = Enemies_RoundSpeed(round) * spdScale;
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
                // With an associated barricade, walk to it and climb in; without
                // one, the mob is already inside and heads straight for a player.
                .state = (climbWin >= 0) ? ZS_OUTSIDE : ZS_INSIDE,
                .type = t,
                .mobIdx = mobIdx,
                .targetWindow = climbWin,
                .targetPlayer = (climbWin >= 0) ? -1 : Player_NearestAlive(spawn),
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
    float limX = g_world.arenaHalfX - ENEMY_RADIUS;
    float limZ = g_world.arenaHalfZ - ENEMY_RADIUS;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive || enemies[i].state != ZS_INSIDE) continue;
        enemies[i].pos.x = Clamp(enemies[i].pos.x, -limX, limX);
        enemies[i].pos.z = Clamp(enemies[i].pos.z, -limZ, limZ);
    }
}

// Floor levels within this much Y are treated as "the same floor" for AI.
#define FLOOR_LEVEL_EPS 0.75f

// Centre of a ramp's low / high edge — the point a zombie aims for to get on
// or off it. (RAMP_X slopes along +X, RAMP_Z along +Z.)
static Vector3 RampLowEntrance(const FloorRegion *f) {
    if (f->rampAxis == RAMP_X) return (Vector3){ f->cx - f->halfX, f->yLow, f->cz };
    return (Vector3){ f->cx, f->yLow, f->cz - f->halfZ };
}
static Vector3 RampHighEntrance(const FloorRegion *f) {
    if (f->rampAxis == RAMP_X) return (Vector3){ f->cx + f->halfX, f->yHigh, f->cz };
    return (Vector3){ f->cx, f->yHigh, f->cz + f->halfZ };
}

// Multi-floor pathing via region BFS over the nav graph (NavNode/NavEdge).
// Replaces the old greedy Y-level heuristic that dead-ended on maps requiring
// down-then-up routing (two decks at the same Y connected only via ground).
//
// Nav_NodeAt: returns the doc-sector index (NavNode id) of the FLAT region
// whose XZ footprint contains (x,z) and whose surface Y ≈ feetY (within
// FLOOR_LEVEL_EPS). Ground fallback: if no node matches and feetY≈0, returns
// the Y0 node (the ground sector). Returns -1 if truly no matching node.
static int Nav_NodeAt(float x, float z, float feetY) {
    int groundNode = -1;
    for (int i = 0; i < g_world.navNodeCount; i++) {
        const NavNode *n = &g_world.navNodes[i];
        if (x < n->cx - n->halfX || x > n->cx + n->halfX) continue;
        if (z < n->cz - n->halfZ || z > n->cz + n->halfZ) continue;
        if (fabsf(n->y - feetY) <= FLOOR_LEVEL_EPS) return n->docSector;
        // Track the ground node (Y0) as a fallback
        if (fabsf(n->y) < 0.001f) groundNode = n->docSector;
    }
    // Ground fallback: entities near ground level (feetY ≤ 2.0f, covering the
    // lower half of typical ramps) that aren't in any elevated flat-sector
    // footprint are treated as on-ground for nav purposes. The threshold is
    // intentionally larger than FLOOR_LEVEL_EPS to cover the low end of ramps
    // and prevent oscillation at the ramp-to-ground boundary.
    if (feetY <= 2.0f && groundNode >= 0) return groundNode;
    return -1;
}

// Hop distance between two nav nodes over the ramp-edge graph; -1 if
// unreachable. Node ids are doc sector indices (all < MAX_NAV_SECTORS).
static int Nav_HopDist(int from, int to) {
    if (from < 0 || to < 0) return -1;
    if (from == to) return 0;
    bool vis[MAX_NAV_SECTORS]; for (int i = 0; i < MAX_NAV_SECTORS; i++) vis[i] = false;
    int q[MAX_NAV_SECTORS], dist[MAX_NAV_SECTORS], qh = 0, qt = 0;
    vis[from] = true; dist[from] = 0; q[qt++] = from;
    while (qh < qt) {
        int cur = q[qh++];
        for (int i = 0; i < g_world.navEdgeCount; i++) {
            const NavEdge *ed = &g_world.navEdges[i];
            int nb = -1;
            if (ed->a == cur && !vis[ed->b]) nb = ed->b;
            else if (ed->b == cur && !vis[ed->a]) nb = ed->a;
            if (nb < 0) continue;
            vis[nb] = true; dist[nb] = dist[cur] + 1;
            if (nb == to) return dist[nb];
            if (qt < MAX_NAV_SECTORS) q[qt++] = nb;
        }
    }
    return -1;
}

// Surface Y of a nav node (flat sector); 0 if the id isn't a node.
static float Nav_NodeY(int node) {
    for (int i = 0; i < g_world.navNodeCount; i++)
        if (g_world.navNodes[i].docSector == node) return g_world.navNodes[i].y;
    return 0.0f;
}

// Split a ramp edge into its low (yLow side) and high (yHigh side) nodes. Link
// order (a=linkA, b=linkB) doesn't encode which end is higher, so compare the
// two linked sectors' surface Y.
static void Nav_EdgeEnds(const NavEdge *ed, int *loNode, int *hiNode) {
    if (Nav_NodeY(ed->a) <= Nav_NodeY(ed->b)) { *loNode = ed->a; *hiNode = ed->b; }
    else                                       { *loNode = ed->b; *hiNode = ed->a; }
}

// Multi-floor pathing via region BFS over the nav graph. Two phases:
//   1. If the enemy is physically ON a ramp slope, walk toward whichever end is
//      on the shorter graph path to the target (handles up, down, down-then-up).
//   2. Otherwise the enemy is on a flat sector: BFS to the target's sector and
//      head for the NEAR entrance (on the enemy's own level) of the first ramp
//      on that path. Phase 1 takes over once they step onto the slope, walking
//      them across to the far end. Aiming at the FAR end from a flat sector was
//      the bug that made zombies walk *under* a ramp instead of up it.
// Replaces the old greedy Y-level heuristic that dead-ended on down-then-up
// routes (two decks at the same Y joined only via the ground). On flat maps
// navNodeCount==0, so this never fires and AI is byte-identical.
// climbRampOut (optional) receives the NavEdge index of the ramp the enemy
// intends to mount/traverse this tick, so the caller's obstacle-avoidance can
// steer the enemy AROUND every other ramp instead of auto-climbing it.
static bool CrossFloorGoalFull(const Enemy *e, float eY, float tY,
                                float tX, float tZ, Vector3 *goal, int *climbRampOut) {
    if (g_world.navNodeCount == 0) return false;
    int targetNode = Nav_NodeAt(tX, tZ, tY);
    if (targetNode < 0) return false;  // target in unknown region

    // Phase 1 — physically on a ramp slope: aim for the end nearer the target.
    for (int i = 0; i < g_world.navEdgeCount; i++) {
        const NavEdge *ed = &g_world.navEdges[i];
        const FloorRegion *f = &ed->ramp;
        if (e->pos.x < f->cx - f->halfX || e->pos.x > f->cx + f->halfX) continue;
        if (e->pos.z < f->cz - f->halfZ || e->pos.z > f->cz + f->halfZ) continue;
        float rampSurf = Level_RegionSurfaceY(f, e->pos.x, e->pos.z);
        if (fabsf(rampSurf - eY) >= 0.4f) continue;
        int loNode, hiNode; Nav_EdgeEnds(ed, &loNode, &hiNode);
        int dLo = Nav_HopDist(loNode, targetNode);
        int dHi = Nav_HopDist(hiNode, targetNode);
        bool goHigh;
        if (dHi < 0)      goHigh = false;   // target only reachable via the low end
        else if (dLo < 0) goHigh = true;    // ... or only via the high end
        else              goHigh = (dHi <= dLo);
        // If the chosen end is the one we're already standing at (we've reached
        // the foot/top of this ramp), defer to flat-sector routing below — it
        // steps us onto the adjacent flat and on toward the next ramp. Without
        // this, a zombie that must continue past this ramp's near end pins
        // itself to its own current spot (the down-then-up dead-end).
        const float ENDZONE = 0.6f;   // < FLOOR_LEVEL_EPS so Nav_NodeAt resolves the flat
        if (goHigh && f->yHigh - rampSurf < ENDZONE) continue;
        if (!goHigh && rampSurf - f->yLow < ENDZONE) continue;
        *goal = goHigh ? RampHighEntrance(f) : RampLowEntrance(f);
        if (climbRampOut) *climbRampOut = i;   // we're on this ramp — don't avoid it
        return true;
    }

    // Phase 2 — on a flat sector.
    int enemyNode = Nav_NodeAt(e->pos.x, e->pos.z, eY);
    if (enemyNode < 0 || enemyNode == targetNode) return false;

    // BFS enemyNode -> targetNode; record the edge that first reached each node.
    int  queue[MAX_NAV_SECTORS], firstEdge[MAX_NAV_SECTORS];
    bool visited[MAX_NAV_SECTORS];
    for (int i = 0; i < MAX_NAV_SECTORS; i++) { firstEdge[i] = -1; visited[i] = false; }
    int head = 0, tail = 0;
    visited[enemyNode] = true; queue[tail++] = enemyNode;
    bool found = false;
    while (head < tail && !found) {
        int cur = queue[head++];
        for (int ei = 0; ei < g_world.navEdgeCount; ei++) {
            const NavEdge *ed = &g_world.navEdges[ei];
            int nb = -1;
            if (ed->a == cur && !visited[ed->b]) nb = ed->b;
            else if (ed->b == cur && !visited[ed->a]) nb = ed->a;
            if (nb < 0) continue;
            visited[nb] = true; firstEdge[nb] = ei;
            if (nb == targetNode) { found = true; break; }
            if (tail < MAX_NAV_SECTORS) queue[tail++] = nb;
        }
    }
    if (!found) return false;  // no path — fall back to direct homing

    // Walk the parent chain back to the hop that leaves enemyNode.
    int cur = targetNode, hopEdge = firstEdge[targetNode];
    while (hopEdge >= 0) {
        const NavEdge *ed = &g_world.navEdges[hopEdge];
        int parent = (ed->a == cur) ? ed->b : ed->a;
        if (parent == enemyNode) break;
        cur = parent; hopEdge = firstEdge[cur];
    }
    if (hopEdge < 0) return false;

    // Head for the NEAR entrance of that ramp — the end on the enemy's own
    // level — so they reach the foot of the ramp; phase 1 then walks them across.
    const NavEdge *hop = &g_world.navEdges[hopEdge];
    int loNode, hiNode; Nav_EdgeEnds(hop, &loNode, &hiNode);
    *goal = (enemyNode == loNode) ? RampLowEntrance(&hop->ramp)
                                  : RampHighEntrance(&hop->ramp);
    if (climbRampOut) *climbRampOut = hopEdge;   // mounting this ramp — avoid others
    return true;
}

// Locomotion guard for cross-floor routing: true if a short step along `dir`
// would put the enemy onto a ramp surface meaningfully above its current floor
// — i.e. auto-climb a ramp it isn't trying to take. `allowRamp` is the NavEdge
// index of the ramp the enemy intends to mount this tick (-1 = none); that one
// is never blocked. Lets the existing fan-probe steer around stray ramps the
// same way it steers around walls (e.g. detour in Z past an X-sloped ramp that
// lies across a ground path between two other ramps).
static bool RampAscentAhead(const Enemy *e, Vector3 dir, float floorY, int allowRamp) {
    const float LOOK = 1.6f;   // far enough to see a gentle ramp rising ahead
    const float RISE = 0.6f;   // ignore tangential brushes along a ramp's foot
    Vector3 p = { e->pos.x + dir.x * LOOK, 0.0f, e->pos.z + dir.z * LOOK };
    for (int i = 0; i < g_world.navEdgeCount; i++) {
        if (i == allowRamp) continue;
        const FloorRegion *f = &g_world.navEdges[i].ramp;
        if (p.x < f->cx - f->halfX || p.x > f->cx + f->halfX) continue;
        if (p.z < f->cz - f->halfZ || p.z > f->cz + f->halfZ) continue;
        if (Level_RegionSurfaceY(f, p.x, p.z) > floorY + RISE) return true;
    }
    return false;
}

// The "chaser" AI archetype: one update-all pass over the enemies whose mob
// uses it. Registered with the behaviour registry as "chaser" (see
// Enemies_InitBehaviours) and dispatched via Mobs_RunBehaviours from
// Enemies_Update — a real name->fn lookup, not a hardcoded call.
static void Chaser_UpdateAll(float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        // Tick dying corpses: decrement the window and free the slot when done.
        if (!enemies[i].alive) {
            if (enemies[i].dyingTimer > 0) {
                enemies[i].dyingTimer -= dt;
                if (enemies[i].dyingTimer < 0) enemies[i].dyingTimer = 0;
            }
            continue;
        }
        Enemy *e = &enemies[i];
        // Skip enemies whose mob names a different archetype (none today — all
        // mobs use chaser — but this keeps dispatch correct once more land).
        const MobDef *emd = Mob_Get(e->mobIdx);
        if (emd && emd->behaviour[0] && strcmp(emd->behaviour, "chaser") != 0) continue;
        e->bobPhase += dt * 4.0f;
        if (e->touchTimer     > 0) e->touchTimer     -= dt;
        if (e->stunTimer      > 0) e->stunTimer      -= dt;
        if (e->simAttackTimer > 0) e->simAttackTimer -= dt;

        if (e->state == ZS_OUTSIDE) {
            Window3D *w = &g_world.windows[e->targetWindow];
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
            Window3D *w = &g_world.windows[e->targetWindow];
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
            if (e->targetPlayer < 0 || !g_world.players[e->targetPlayer].active || !g_world.players[e->targetPlayer].alive
                || (rand() % retargetMod) == 0) {
                e->targetPlayer = Player_NearestAlive(e->pos);
            }
            if (e->targetPlayer < 0) continue;
            Player *tp = &g_world.players[e->targetPlayer];

            // Route via an open door if one lies between us and the target.
            // Use a two-stage waypoint: first line up perpendicular to the
            // door on our side, then aim past it. Otherwise a steep approach
            // angle pins zombies against the wall instead of the doorway.
            Vector3 goal = tp->pos;
            e->hasWaypoint = false;

            // Multi-floor: if the player is on a different floor, route toward a
            // ramp instead of straight at them. Takes priority over door routing
            // (ramps/doors rarely interact); never fires on flat maps.
            float eFloorY = Level_FloorHeightAt(e->pos.x, e->pos.z, e->pos.y - ENEMY_HEIGHT*0.5f);
            float tFloorY = Level_FloorHeightAt(tp->pos.x, tp->pos.z, tp->pos.y - PLAYER_EYE);
            int climbRamp = -1;
            bool crossFloor = CrossFloorGoalFull(e, eFloorY, tFloorY,
                                                  tp->pos.x, tp->pos.z, &goal, &climbRamp);
            if (crossFloor) { e->waypoint = goal; e->hasWaypoint = true; }

            for (int di = 0; di < doorCount && !crossFloor; di++) {
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
                bool rampBlock = crossFloor && RampAscentAhead(e, moveDir, eFloorY, climbRamp);
                bool blocked = rampBlock
                            || !Level_PathClearXZ(e->pos, moveDir, ENEMY_RADIUS + 0.05f, lookahead);
                if (blocked) {
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
                            if (Level_PathClearXZ(e->pos, cand, ENEMY_RADIUS + 0.05f, lookahead)
                                && !(crossFloor && RampAscentAhead(e, cand, eFloorY, climbRamp))) {
                                e->escapeDir = cand;
                                // Ramp detours need to clear the whole footprint
                                // (several metres in Z) before re-evaluating, or
                                // phase-1 re-grabs the enemy onto the ramp and it
                                // oscillates. Commit long enough to circumnavigate;
                                // wall detours keep the short anti-jitter commit.
                                e->escapeTimer = rampBlock ? 1.0f : 0.30f;
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

            // Stand on the floor surface here (multi-floor): walk up ramps and
            // onto decks. Flat maps -> surface 0, identical to the old fixed Y.
            float surfY = Level_FloorHeightAt(e->pos.x, e->pos.z, e->pos.y - ENEMY_HEIGHT*0.5f);
            e->pos.y = surfY + ENEMY_HEIGHT*0.5f;

            Vector2 pXZ = { tp->pos.x, tp->pos.z };
            Vector2 eXZ = { e->pos.x, e->pos.z };
            // Bite only when on the same floor (don't claw through a deck at a
            // player standing right above/below). On flat maps the Y delta is
            // the fixed eye-vs-centre gap, well within the threshold.
            if (Vector2Distance(pXZ, eXZ) < PLAYER_RADIUS + ENEMY_RADIUS + 0.1f
                && fabsf(e->pos.y - tp->pos.y) < 2.0f
                && e->touchTimer <= 0 && e->stunTimer <= 0) {
                bool cheatProtected = godMode && (int)(tp - g_world.players) == localPlayerIdx;
                // Downed g_world.players are incapacitated — only the bleed timer kills them.
                if (tp->downed) cheatProtected = true;
                float ccd = ENEMY_TOUCH_COOLDOWN;
                if      (e->type == ZT_RUNNER) ccd *= 0.80f;
                else if (e->type == ZT_BOSS)   ccd *= 1.30f;
                if (!cheatProtected) {
                    // Base contact damage from the mob def (zombie.mob = 50 =
                    // the old ENEMY_DAMAGE); ZombieType variants scale it.
                    const MobDef *bmd = Mob_Get(e->mobIdx);
                    int baseDmg = (bmd && bmd->damage > 0) ? bmd->damage : ENEMY_DAMAGE;
                    int dmg = baseDmg;
                    switch (e->type) {
                        case ZT_RUNNER:  dmg = (int)(baseDmg * 0.80f); break;
                        case ZT_CRAWLER: dmg = baseDmg;                break;
                        case ZT_BOSS:    dmg = baseDmg * 2;            break;
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
                        int meIdx = (int)(tp - g_world.players);
                        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                            if (j == meIdx) continue;
                            if (g_world.players[j].active && g_world.players[j].alive && !g_world.players[j].downed) otherUp++;
                        }
                        if (otherUp > 0) {
                            tp->downed = true;
                            tp->bleedTimer = BLEED_TIME;
                        } else {
                            tp->alive = false;
                        }
                    }
                    if ((int)(tp - g_world.players) == localPlayerIdx) {
                        float kickAmt = tp->hp <= 0 ? 0.65f
                                      : (e->type == ZT_BOSS ? 0.50f : 0.30f);
                        Fx_PunchAndRumble(kickAmt, 0.55f, 0.55f, 0.15f);
                    }
                }
                // Flag the attack animation for the renderer (and clients).
                e->simAttackTimer = ENEMY_ATTACK_TIMER;
                e->touchTimer = ccd;
            }
        }
    }
}

// One-time: register the compiled-in (Tier-1) archetypes, then dlopen any
// Tier-2 .so behaviours, then warn about any loaded mob whose `behaviour` names
// an archetype nobody registered (the "missing archetype" signal from
// editor-content-extensibility.md §4).
static bool g_behInited = false;
void Enemies_InitBehaviours(void) {
    if (g_behInited) return;
    Game_RegisterBehaviour("chaser", Chaser_UpdateAll);
    Game_LoadBehaviourPlugins();
    for (int i = 0; i < Mob_Count(); i++) {
        const MobDef *m = Mob_Get(i);
        if (m && m->behaviour[0] && !Game_BehaviourRegistered(m->behaviour))
            fprintf(stderr, "mob: %s references unregistered behaviour '%s'\n",
                    m->id, m->behaviour);
    }
    g_behInited = true;
}

void Enemies_Update(float dt) {
    if (!g_behInited) Enemies_InitBehaviours();
    Mobs_RunBehaviours(dt);
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

        // Static geometry: g_world.obstacles, interior walls, closed doors
        for (int j = 0; j < g_world.obstacleCount; j++) {
            float t; Vector3 hp, hn;
            if (SegmentBoxHit(a, b, g_world.obstacles[j], &t, &hp, &hn) && t < bestT) {
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
        // Floor-region slabs (multi-floor): shots can't pass through a deck/ramp.
        for (int j = 0; j < g_world.floorCount; j++) {
            Box fb = Level_FloorRegionBox(&g_world.floors[j]);
            float t; Vector3 hp, hn;
            if (SegmentBoxHit(a, b, fb, &t, &hp, &hn) && t < bestT) {
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
            if (ax > g_world.arenaHalfX + 4.0f || az > g_world.arenaHalfZ + 4.0f)
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
            if (g_world.instaKillTimer > 0) dmg = 99999;
            en->hp -= dmg;
            int op = bullets[i].ownerPlayer;
            // CoD scoring: 10 per hitmarker, 60 for a body kill (10+50),
            // 100 for a headshot kill (10+90). Non-killing headshots still
            // only score the base 10.
            int hitPts  = HIT_POINTS;
            int killPts = KILL_POINTS + (headHit ? 40 : 0);
            if (g_world.doublePointsTimer > 0) { hitPts *= 2; killPts *= 2; }
            if (op >= 0 && op < NET_MAX_PLAYERS && g_world.players[op].active) {
                g_world.players[op].points += hitPts;
                g_world.players[op].shotsHit++;
                if (headHit) g_world.players[op].headshots++;
                if (en->hp <= 0) {
                    en->dyingTimer = ENEMY_DEATH_WINDOW;
                    en->alive = false;
                    enemiesAlive--;
                    g_world.players[op].points += killPts;
                    g_world.players[op].kills++;
                    PowerUps_TryDrop(en->pos);
                }
            } else if (en->hp <= 0) {
                en->dyingTimer = ENEMY_DEATH_WINDOW;
                en->alive = false;
                enemiesAlive--;
                PowerUps_TryDrop(en->pos);
            }
            // Blood faces back toward the shooter
            Vector3 vn = Vector3Normalize(Vector3Negate(bullets[i].vel));
            float sz = 0.18f + (rand() % 100) / 100.0f * 0.10f;
            Decals_Spawn(DECAL_BLOOD, hitPos, vn, sz);
            Particles_BloodMist(hitPos, vn, headHit);
        } else {
            float sz = 0.10f + (rand() % 100) / 100.0f * 0.05f;
            Decals_Spawn(DECAL_IMPACT, hitPos, hitNormal, sz);
        }

        if (doSplash) {
            int hitKillPts = KILL_POINTS;
            int hitHitPts  = HIT_POINTS;
            if (g_world.doublePointsTimer > 0) { hitKillPts *= 2; hitHitPts *= 2; }
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
                if (g_world.instaKillTimer > 0) dmg = 99999;
                enemies[e].hp -= dmg;
                if (splashOwner >= 0 && splashOwner < NET_MAX_PLAYERS && g_world.players[splashOwner].active) {
                    g_world.players[splashOwner].points += hitHitPts;
                }
                if (enemies[e].hp <= 0) {
                    enemies[e].dyingTimer = ENEMY_DEATH_WINDOW;
                    enemies[e].alive = false;
                    enemiesAlive--;
                    if (splashOwner >= 0 && splashOwner < NET_MAX_PLAYERS && g_world.players[splashOwner].active) {
                        g_world.players[splashOwner].points += hitKillPts;
                        g_world.players[splashOwner].kills++;
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
                .ownerPlayer = (int)(p - g_world.players),
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
        if (g_world.doublePointsTimer > 0) { killPts *= 2; hitPts *= 2; }
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
            if (g_world.instaKillTimer > 0) dmg = 99999;
            enemies[e].hp -= dmg;
            if (op >= 0 && op < NET_MAX_PLAYERS && g_world.players[op].active) {
                g_world.players[op].points += hitPts;
            }
            if (enemies[e].hp <= 0) {
                enemies[e].dyingTimer = ENEMY_DEATH_WINDOW;
                enemies[e].alive = false;
                enemiesAlive--;
                if (op >= 0 && op < NET_MAX_PLAYERS && g_world.players[op].active) {
                    g_world.players[op].points += killPts;
                    g_world.players[op].kills++;
                }
                PowerUps_TryDrop(enemies[e].pos);
            }
            Vector3 vn = (Vector3){ -dx/(d+1e-4f), -dy/(d+1e-4f), -dz/(d+1e-4f) };
            Decals_Spawn(DECAL_BLOOD, enemies[e].pos, vn, 0.22f);
        }
        // Explosion particle burst at the detonation point.
        Particles_Explosion(t->pos);

        // Camera kick for any nearby local player
        if (localPlayerIdx >= 0 && g_world.players[localPlayerIdx].active) {
            float dx = g_world.players[localPlayerIdx].pos.x - t->pos.x;
            float dy = g_world.players[localPlayerIdx].pos.y - t->pos.y;
            float dz = g_world.players[localPlayerIdx].pos.z - t->pos.z;
            float d  = sqrtf(dx*dx + dy*dy + dz*dz);
            if (d < FRAG_RADIUS * 1.5f) {
                float mul = 1.0f - (d / (FRAG_RADIUS * 1.5f));
                Fx_PunchAndRumble(0.55f * mul, 0.7f * mul, 0.6f * mul, 0.20f);
                // Frag damages the local player too (friendly fire by proximity).
                if (d < FRAG_RADIUS && !godMode && !g_world.players[localPlayerIdx].downed) {
                    int self = (int)(FRAG_DAMAGE * 0.40f * (1.0f - d / FRAG_RADIUS));
                    if (self > 0) {
                        g_world.players[localPlayerIdx].hp -= self;
                        g_world.players[localPlayerIdx].damageFlash = 0.6f;
                        g_world.players[localPlayerIdx].regenTimer = 0.0f;
                        if (g_world.players[localPlayerIdx].hp < 0) g_world.players[localPlayerIdx].hp = 0;
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
        if (localPlayerIdx >= 0 && g_world.players[localPlayerIdx].active) {
            float dx = g_world.players[localPlayerIdx].pos.x - t->pos.x;
            float dz = g_world.players[localPlayerIdx].pos.z - t->pos.z;
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
    g_world.doublePointsTimer = 0;
    g_world.instaKillTimer = 0;
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
    if (g_world.doublePointsTimer > 0) g_world.doublePointsTimer -= dt;
    if (g_world.instaKillTimer    > 0) g_world.instaKillTimer    -= dt;
}

void PowerUps_Apply(PowerUpType type) {
    switch (type) {
        case PU_MAX_AMMO:
            for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                if (!g_world.players[i].active) continue;
                for (int s = 0; s < INV_SLOTS; s++) {
                    if (!g_world.players[i].inventory[s].owned) continue;
                    int cap = WEAPONS[g_world.players[i].inventory[s].weaponIdx].reserveMax;
                    if (g_world.players[i].inventory[s].packed) cap += 60;
                    g_world.players[i].inventory[s].reserve = cap;
                }
            }
            break;
        case PU_NUKE:
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (!enemies[i].alive) continue;
                enemies[i].dyingTimer = ENEMY_DEATH_WINDOW;
                enemies[i].alive = false;
                enemiesAlive--;
            }
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (g_world.players[i].active) g_world.players[i].points += 400;
            fxFlashAmount = 1.0f;
            Fx_PunchAndRumble(0.9f, 1.0f, 1.0f, 0.5f);
            break;
        case PU_DOUBLE_POINTS:
            g_world.doublePointsTimer = 20.0f;
            break;
        case PU_INSTAKILL:
            g_world.instaKillTimer = 20.0f;
            break;
        case PU_CARPENTER:
            for (int i = 0; i < g_world.windowCount; i++) g_world.windows[i].boards = MAX_BOARDS_PER_WIN;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (g_world.players[i].active) g_world.players[i].points += 200;
            break;
        default: break;
    }
}

void PowerUps_Pickup(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        for (int pi = 0; pi < NET_MAX_PLAYERS; pi++) {
            if (!g_world.players[pi].active || !g_world.players[pi].alive) continue;
            float dx = g_world.players[pi].pos.x - powerUps[i].pos.x;
            float dz = g_world.players[pi].pos.z - powerUps[i].pos.z;
            if (dx*dx + dz*dz < POWERUP_PICKUP_R * POWERUP_PICKUP_R) {
                PowerUps_Apply(powerUps[i].type);
                powerUps[i].active = false;
                break;
            }
        }
    }
}
