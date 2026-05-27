#ifndef SHOOTER_WEAPONS_H
#define SHOOTER_WEAPONS_H

#include "types.h"

extern const WeaponDef WEAPONS[W_COUNT];

// Effective stats (factor in perks and Pack-a-Punch)
int   Weapon_EffDamage(Player *p, WeaponSlot *s);
float Weapon_EffFireCD(Player *p, WeaponSlot *s);
float Weapon_EffReload(Player *p, WeaponSlot *s);
int   Weapon_EffMagSize(WeaponSlot *s);
int   Weapon_EffReserveMax(WeaponSlot *s);

// Inventory helpers
int   Weapon_FindOwnedSlot(Player *p, int weaponIdx);
int   Weapon_FirstEmptySlot(Player *p);

// Server-side actions
void  Weapon_Fire(Player *p);            // fire current weapon if able
void  Weapon_StartReload(Player *p);
void  Weapon_FinishReloadIfReady(Player *p, WeaponSlot *s);
void  Weapon_SwapSlot(Player *p, int target);

// Misc
Vector3 Weapon_SpreadDir(Vector3 base, float degrees);

// Melee
void  Weapon_Melee(Player *p);

#endif
