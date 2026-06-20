#ifndef SHOOTER_WEAPONS_H
#define SHOOTER_WEAPONS_H

#include "types.h"
#include "raylib.h"

// WEAPONS[] is mutable so .weapon files can populate / override entries at
// startup via Weapons_Load. Compiled-in defaults serve as fallbacks for any
// slot whose file is missing or malformed.
extern WeaponDef WEAPONS[W_COUNT];

// ---- weapon models + viewmodel tuning (was in assets.{c,h}) -------------
// Now owned by the weapons module since they're authored alongside the
// .weapon spec under data/weapons/<name>/.
extern Model weaponModels[W_COUNT];
extern bool  weaponModelLoaded[W_COUNT];

typedef struct WeaponModelTune {
    float scale;       // multiplier on top of base draw scale
    float yawDeg;      // extra Y rotation in degrees
    Vector3 offset;    // local-space offset before rotation
} WeaponModelTune;

extern WeaponModelTune weaponTune[W_COUNT];

// Per-weapon grip: where/how the gun OBJ sits in the arms viewmodel's
// hand.R bone (viewmodel.c arms path). `pos` is in bone-local metres AFTER
// the base +90° X rotation (+x = right, +y = toward muzzle, +z = up),
// `rotDeg` is extra fine rotation, `scale` sizes the gun against the arms.
// Data-driven via the .weapon keys vm_grip_pos / vm_grip_rot / vm_grip_scale;
// tuned with --screenshot-viewmodels.
typedef struct WeaponGrip {
    Vector3 pos;
    Vector3 rotDeg;
    float   scale;
} WeaponGrip;

extern WeaponGrip weaponGrip[W_COUNT];

// ---- loader -------------------------------------------------------------
// Scans data/weapons/ for *.weapon files, parses each, populates WEAPONS[]
// + weaponTune[] and loads the model into weaponModels[]. Call BEFORE
// Assets_Load: it enrols the loaded models via Assets_RegisterWorldShaderModel
// so the world (fog) shader is stamped on them by Assets_ApplyWorldShader.
// Safe to call after InitWindow only — uses LoadModel from raylib.
void  Weapons_Load(void);
void  Weapons_Unload(void);

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
