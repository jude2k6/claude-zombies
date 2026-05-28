#include "weapons.h"
#include "player.h"
#include "level.h"
#include "raymath.h"
#include <math.h>

const WeaponDef WEAPONS[W_COUNT] = {
    [W_PISTOL]  = { "M1911",   "Mustang",     12, 12, 120, 0.20f, 1.3f, 1, 0.0f, false,     0,   100, (Color){200,200,200,255} },
    [W_SMG]     = { "MP5",     "MP5-K+",       9, 30, 180, 0.07f, 1.8f, 1, 1.5f, true,   1000,   600, (Color){220,170, 70,255} },
    [W_SHOTGUN] = { "Olympia", "Hades",       22,  2,  24, 0.55f, 2.0f, 6, 6.0f, false,   500,   300, (Color){170, 90, 50,255} },
    [W_RIFLE]   = { "M14",     "Mnesia",      34, 10,  90, 0.28f, 1.5f, 1, 0.4f, false,  1200,   600, (Color){100,140,100,255} },
    [W_RAYGUN]  = { "Ray Gun", "Porter's X2", 95, 20,  80, 0.22f, 2.6f, 1, 0.0f, true,  10000,  4500, (Color){120,255,160,255} },
};

int Weapon_EffDamage(Player *p, WeaponSlot *s) {
    int d = WEAPONS[s->weaponIdx].damage;
    if (s->packed) d = (int)(d * 2.5f);
    if (p->hasPerk[PERK_DTAP]) d = (int)(d * 1.33f);
    return d;
}
float Weapon_EffFireCD(Player *p, WeaponSlot *s) {
    float c = WEAPONS[s->weaponIdx].fireCooldown;
    if (p->hasPerk[PERK_DTAP]) c *= 0.72f;
    return c;
}
float Weapon_EffReload(Player *p, WeaponSlot *s) {
    float r = WEAPONS[s->weaponIdx].reloadTime;
    if (p->hasPerk[PERK_SPEED]) r *= 0.5f;
    return r;
}
int Weapon_EffMagSize(WeaponSlot *s) {
    int m = WEAPONS[s->weaponIdx].magSize;
    if (s->packed) m *= 2;
    return m;
}
int Weapon_EffReserveMax(WeaponSlot *s) {
    int r = WEAPONS[s->weaponIdx].reserveMax;
    if (s->packed) r += 60;
    return r;
}

int Weapon_FindOwnedSlot(Player *p, int weaponIdx) {
    for (int i = 0; i < INV_SLOTS; i++)
        if (p->inventory[i].owned && p->inventory[i].weaponIdx == weaponIdx) return i;
    return -1;
}
int Weapon_FirstEmptySlot(Player *p) {
    for (int i = 0; i < INV_SLOTS; i++) if (!p->inventory[i].owned) return i;
    return -1;
}

Vector3 Weapon_SpreadDir(Vector3 base, float degrees) {
    if (degrees <= 0) return base;
    float rad = degrees * DEG2RAD;
    float yaw   = Level_RandRange(-rad, rad);
    float pitch = Level_RandRange(-rad, rad);
    Vector3 up    = { 0, 1, 0 };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(base, up));
    Vector3 trueUp = Vector3CrossProduct(right, base);
    Vector3 d = base;
    d = Vector3Add(d, Vector3Scale(right,  tanf(yaw)));
    d = Vector3Add(d, Vector3Scale(trueUp, tanf(pitch)));
    return Vector3Normalize(d);
}

// Forward decl for bullet spawn
extern void Bullets_Spawn(Vector3 origin, Vector3 dir, int damage, int ownerPlayer);
extern float muzzleFlashLocal;  // defined in render.c, ticked by HUD/main

void Weapon_Fire(Player *p) {
    if (!p->alive) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned) return;
    if (s->reloadTimer > 0 || s->fireTimer > 0 || s->ammo <= 0) return;
    if (pap.activeTimer > 0 && pap.slotInProgress == p->currentSlot && pap.ownerPlayer == (int)(p - players)) return;

    const WeaponDef *w = &WEAPONS[s->weaponIdx];
    Vector3 origin = (Vector3){ p->pos.x, PLAYER_EYE, p->pos.z };
    Vector3 dir = Player_LookDir(p->yaw, p->pitch);
    int dmg = Weapon_EffDamage(p, s);
    int ownerIdx = (int)(p - players);
    for (int k = 0; k < w->pellets; k++) {
        Vector3 d = Weapon_SpreadDir(dir, w->spreadDeg);
        Bullets_Spawn(origin, d, dmg, ownerIdx);
    }
    s->ammo--;
    s->fireTimer = Weapon_EffFireCD(p, s);
    p->shotsFired += w->pellets;
    if (ownerIdx == localPlayerIdx) muzzleFlashLocal = 0.05f;
}

void Weapon_StartReload(Player *p) {
    if (!p->alive) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned) return;
    if (s->reloadTimer > 0 || s->ammo >= Weapon_EffMagSize(s) || s->reserve <= 0) return;
    s->reloadTimer = Weapon_EffReload(p, s);
}

void Weapon_FinishReloadIfReady(Player *p, WeaponSlot *s) {
    (void)p;
    if (s->reloadTimer > 0) return;
    int mag = Weapon_EffMagSize(s);
    int need = mag - s->ammo;
    if (need <= 0) return;
    int take = (need < s->reserve) ? need : s->reserve;
    s->ammo += take; s->reserve -= take;
}

void Weapon_SwapSlot(Player *p, int target) {
    if (target < 0 || target >= INV_SLOTS) return;
    if (p->inventory[target].owned) p->currentSlot = target;
}

// Forward decls into entities
extern Enemy enemies[MAX_ENEMIES];
extern int   enemiesAlive;

#define MELEE_RANGE       1.8f
#define MELEE_DAMAGE      150
#define MELEE_KILL_POINTS 130
#define MELEE_COOLDOWN    0.6f

void Weapon_Melee(Player *p) {
    if (!p->alive) return;
    if (p->meleeTimer > 0) return;
    p->meleeTimer = MELEE_COOLDOWN;

    Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
    int ownerIdx = (int)(p - players);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        float dx = enemies[i].pos.x - p->pos.x;
        float dz = enemies[i].pos.z - p->pos.z;
        float d2 = dx*dx + dz*dz;
        if (d2 > MELEE_RANGE * MELEE_RANGE) continue;
        float d = sqrtf(d2);
        if (d < 0.0001f) d = 0.0001f;
        float dot = (dx * fwd.x + dz * fwd.z) / d;
        if (dot < 0.3f) continue;   // ~70deg forward arc
        enemies[i].hp -= MELEE_DAMAGE;
        if (enemies[i].hp <= 0) {
            enemies[i].alive = false;
            enemiesAlive--;
            if (ownerIdx >= 0 && ownerIdx < NET_MAX_PLAYERS && players[ownerIdx].active) {
                players[ownerIdx].points += MELEE_KILL_POINTS;
                players[ownerIdx].kills++;
                players[ownerIdx].meleeKills++;
            }
        } else if (ownerIdx >= 0 && ownerIdx < NET_MAX_PLAYERS && players[ownerIdx].active) {
            players[ownerIdx].points += HIT_POINTS;
        }
    }
}
