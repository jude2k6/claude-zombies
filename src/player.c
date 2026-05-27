#include "player.h"
#include "level.h"
#include "perks.h"
#include "entities.h"
#include "raymath.h"
#include <string.h>
#include <math.h>

Player players[NET_MAX_PLAYERS];
int    localPlayerIdx = 0;
bool   godMode = false;

int Player_ActiveCount(void) {
    int n = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (players[i].active) n++;
    return n;
}

int Player_AliveActiveCount(void) {
    int n = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (players[i].active && players[i].alive) n++;
    return n;
}

int Player_NearestAlive(Vector3 pos) {
    int best = -1; float bestD = 1e9f;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!players[i].active || !players[i].alive) continue;
        float dx = players[i].pos.x - pos.x, dz = players[i].pos.z - pos.z;
        float d = dx*dx + dz*dz;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

Vector3 Player_Spawn(int idx) {
    if (idx >= 0 && idx < mapSpawnCount) return mapSpawns[idx];
    if (mapSpawnCount > 0) return mapSpawns[0];
    return (Vector3){ 0, PLAYER_EYE, 0 };
}

Vector3 Player_LookDir(float yaw, float pitch) {
    float cp = cosf(pitch);
    return (Vector3){ sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp };
}

void Player_GiveStartingPistol(Player *p) {
    extern const WeaponDef WEAPONS[W_COUNT];
    p->inventory[0] = (WeaponSlot){
        .weaponIdx = W_PISTOL,
        .ammo = WEAPONS[W_PISTOL].magSize,
        .reserve = WEAPONS[W_PISTOL].reserveMax,
        .owned = true,
    };
    p->inventory[1] = (WeaponSlot){ .owned = false };
    p->currentSlot = 0;
    for (int k = 0; k < PERK_COUNT; k++) p->hasPerk[k] = false;
    p->hp = 100;
    p->points = 500;
    p->alive = true;
}

void Player_ResetForGame(int idx, const char *name) {
    Player *p = &players[idx];
    memset(p, 0, sizeof *p);
    p->active = true;
    p->alive = true;
    if (name) strncpy(p->name, name, sizeof p->name - 1);
    p->pos = Player_Spawn(idx);
    p->yaw = 0.0f; p->pitch = 0.0f;
    Player_GiveStartingPistol(p);
}

void Player_ApplyLocalLook(Player *p, float mouseSens) {
    Vector2 md = GetMouseDelta();
    p->yaw   += md.x * mouseSens;
    p->pitch -= md.y * mouseSens;
    if (p->pitch >  1.55f) p->pitch =  1.55f;
    if (p->pitch < -1.55f) p->pitch = -1.55f;
}

static void PushOutOfEnemies(Player *p) {
    float minD = PLAYER_RADIUS + ENEMY_RADIUS;
    float minD2 = minD * minD;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        if (enemies[i].state != ZS_INSIDE) continue;
        float dx = p->pos.x - enemies[i].pos.x;
        float dz = p->pos.z - enemies[i].pos.z;
        float d2 = dx*dx + dz*dz;
        if (d2 < minD2 && d2 > 1e-6f) {
            float d = sqrtf(d2);
            float push = (minD - d);
            float nx = dx / d, nz = dz / d;
            // Move both: player most of the way, zombie a small nudge
            p->pos.x += nx * push * 0.7f;
            p->pos.z += nz * push * 0.7f;
            enemies[i].pos.x -= nx * push * 0.3f;
            enemies[i].pos.z -= nz * push * 0.3f;
        }
    }
}

void Player_ApplyLocalMove(Player *p, float dt) {
    Vector3 fwd   = { sinf(p->yaw),  0, -cosf(p->yaw) };
    Vector3 right = { cosf(p->yaw),  0,  sinf(p->yaw) };

    float speed = Perk_EffMoveSpeed(p);
    if (IsKeyDown(KEY_LEFT_SHIFT)) speed *= 1.6f;          // sprint
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C)) speed *= 0.55f; // crouch

    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (Vector3LengthSqr(move) > 0.0001f) {
        move = Vector3Scale(Vector3Normalize(move), speed * dt);
    }
    Vector3 newPos = Vector3Add(p->pos, move);
    newPos.y = PLAYER_EYE;
    p->pos = Level_ResolveXZ(p->pos, newPos, PLAYER_RADIUS, true);
    PushOutOfEnemies(p);
    // Final clamp against level after enemy push
    p->pos = Level_ResolveXZ(p->pos, p->pos, PLAYER_RADIUS, true);
}
