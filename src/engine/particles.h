#ifndef SHOOTER_PARTICLES_H
#define SHOOTER_PARTICLES_H

#include "raylib.h"
#include <stdbool.h>

// ---- generic emitter API ---------------------------------------------------

// Blend mode for emitted particles.
typedef enum {
    ENG_PBLEND_ADDITIVE = 0,  // fire, sparks, flash — no depth-sort needed
    ENG_PBLEND_ALPHA    = 1,  // blood puffs, smoke, casings — alpha compositing
} EngParticleBlend;

// Descriptor passed to Eng_FxEmit. All fields are engine-generic; no game
// concepts appear here. Ranges are sampled uniformly per particle.
typedef struct {
    Vector3          pos;           // emit origin
    Vector3          dir;           // central direction for the cone
    int              count;         // number of particles to spawn
    float            halfAngle;     // cone half-angle in radians (0 = exact dir)
    float            speedMin;      // initial speed range (m/s)
    float            speedMax;
    float            sizeMin;       // particle billboard radius range (m)
    float            sizeMax;
    float            ttlMin;        // time-to-live range (seconds)
    float            ttlMax;
    Color            colorStart;    // colour at birth
    Color            colorEnd;      // colour at death (alpha→0 for fade-out)
    float            gravity;       // downward acceleration (m/s²); negative = upward
    EngParticleBlend blend;
    // Optional position jitter applied to each particle's starting pos.
    float            posJitterX;
    float            posJitterY;
    float            posJitterZ;
} EngEmitterDesc;

// Emit `desc.count` particles according to the descriptor.
void Eng_FxEmit(EngEmitterDesc desc);

// ---- lifecycle + render ----------------------------------------------------

void Particles_Reset(void);
void Particles_Update(float dt);
void Particles_Draw(Camera camera);

// ---- game-flavoured spawn helpers ------------------------------------------
// These fill an EngEmitterDesc and call Eng_FxEmit. Their signatures are
// stable so call sites outside engine/ compile unchanged.
void Particles_MuzzleFlash(Vector3 pos, Vector3 dir);
void Particles_CasingEject(Vector3 pos, Vector3 right);
void Particles_BloodMist(Vector3 pos, Vector3 dir, bool headshot);
void Particles_Explosion(Vector3 pos);

#endif
