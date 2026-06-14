#ifndef SHOOTER_DECALS_H
#define SHOOTER_DECALS_H

#include "raylib.h"   // Vector3 — decals is engine-side, owns no game types

#define MAX_DECALS 96   // engine-internal cap (mirrors the game's old types.h value)

// ---- decal kind ------------------------------------------------------------
// Names kept stable so existing call sites (entities.c etc.) compile unchanged.

typedef enum {
    DECAL_IMPACT,   // grey/black mark on a hard surface
    DECAL_BLOOD,    // dark-red wet splat
} DecalKind;

// ---- generic engine API ----------------------------------------------------

// Engine-facing kind aliases: game-neutral names that map 1:1 to DecalKind.
// Call sites that want to stay game-agnostic should use these names.
typedef enum {
    ENG_DECAL_IMPACT = DECAL_IMPACT,
    ENG_DECAL_SPLAT  = DECAL_BLOOD,
} EngDecalKind;

// Generic decal emit: places a decal at `pos` oriented to `normal`.
// This is the engine-API entry point; Decals_Spawn is the concrete implementation.
void Eng_FxDecal(EngDecalKind kind, Vector3 pos, Vector3 normal, float size);

// ---- lifecycle + render ----------------------------------------------------

void Decals_Init(void);
void Decals_ClearAll(void);
void Decals_Spawn(DecalKind kind, Vector3 pos, Vector3 normal, float size);
void Decals_Update(float dt);
void Decals_Draw(void);

#endif
