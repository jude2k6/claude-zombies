#ifndef SHOOTER_DECALS_H
#define SHOOTER_DECALS_H

#include "types.h"

typedef enum {
    DECAL_IMPACT,   // grey/black puff on hard surface
    DECAL_BLOOD,    // red splat on a zombie hit
} DecalKind;

void Decals_Init(void);
void Decals_ClearAll(void);
void Decals_Spawn(DecalKind kind, Vector3 pos, Vector3 normal, float size);
void Decals_Update(float dt);
void Decals_Draw(void);

#endif
