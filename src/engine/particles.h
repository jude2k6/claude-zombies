#ifndef SHOOTER_PARTICLES_H
#define SHOOTER_PARTICLES_H

#include "raylib.h"

void Particles_Reset(void);
void Particles_Update(float dt);
void Particles_Draw(Camera camera);

// Typed spawn helpers — call from sim/weapons/entities side (same pattern as
// Decals_Spawn). Note: Particles are visual-only; spawning from sim code is
// fine on the host but will produce no effect on clients (no serialisation).
void Particles_MuzzleFlash(Vector3 pos, Vector3 dir);
void Particles_CasingEject(Vector3 pos, Vector3 right);
void Particles_BloodMist(Vector3 pos, Vector3 dir, bool headshot);
void Particles_Explosion(Vector3 pos);

#endif
