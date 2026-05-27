#include "player.h"
#include "level.h"
#include "perks.h"
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
    static const Vector3 SP[NET_MAX_PLAYERS] = {
        { 0, PLAYER_EYE,  0 },
        { 2, PLAYER_EYE,  0 },
        {-2, PLAYER_EYE,  0 },
        { 0, PLAYER_EYE,  2 },
    };
    return SP[idx];
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

void Player_ApplyLocalMove(Player *p, float dt) {
    Vector3 fwd   = { sinf(p->yaw),  0, -cosf(p->yaw) };
    Vector3 right = { cosf(p->yaw),  0,  sinf(p->yaw) };

    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (Vector3LengthSqr(move) > 0.0001f) {
        move = Vector3Scale(Vector3Normalize(move), Perk_EffMoveSpeed(p) * dt);
    }
    Vector3 newPos = Vector3Add(p->pos, move);
    newPos.y = PLAYER_EYE;
    p->pos = Level_ResolveXZ(p->pos, newPos, PLAYER_RADIUS, true);
}
