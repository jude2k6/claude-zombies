#include "decals.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

typedef struct {
    bool      active;
    DecalKind kind;
    Vector3   pos;
    Vector3   normal;     // outward surface normal (unit)
    Vector3   tangentU;   // in-plane basis
    Vector3   tangentV;
    float     size;       // disc radius (m)
    float     life;
    float     maxLife;
    float     yaw;        // random tilt so adjacent splats don't look identical
} Decal;

static Decal decals[MAX_DECALS];
static int   nextSlot = 0;   // ring-buffer write head when no free slot

void Decals_Init(void) {
    for (int i = 0; i < MAX_DECALS; i++) decals[i].active = false;
    nextSlot = 0;
}

void Decals_ClearAll(void) { Decals_Init(); }

static void BuildTangents(Vector3 n, Vector3 *u, Vector3 *v) {
    Vector3 ref = (fabsf(n.y) < 0.9f) ? (Vector3){0,1,0} : (Vector3){1,0,0};
    *u = Vector3Normalize(Vector3CrossProduct(ref, n));
    *v = Vector3CrossProduct(n, *u);
}

void Decals_Spawn(DecalKind kind, Vector3 pos, Vector3 normal, float size) {
    float nLen = sqrtf(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
    Vector3 n = (nLen > 1e-4f) ? (Vector3){ normal.x/nLen, normal.y/nLen, normal.z/nLen }
                               : (Vector3){ 0, 1, 0 };

    int slot = -1;
    for (int i = 0; i < MAX_DECALS; i++) {
        if (!decals[i].active) { slot = i; break; }
    }
    if (slot < 0) {                 // none free: overwrite oldest via ring head
        slot = nextSlot;
        nextSlot = (nextSlot + 1) % MAX_DECALS;
    }

    Vector3 u, v;
    BuildTangents(n, &u, &v);

    float life = (kind == DECAL_BLOOD) ? 8.0f : 14.0f;
    decals[slot] = (Decal){
        .active   = true,
        .kind     = kind,
        .pos      = pos,
        .normal   = n,
        .tangentU = u,
        .tangentV = v,
        .size     = size,
        .life     = life,
        .maxLife  = life,
        .yaw      = (float)(rand() % 360) * (3.14159265f / 180.0f),
    };
}

void Decals_Update(float dt) {
    for (int i = 0; i < MAX_DECALS; i++) {
        if (!decals[i].active) continue;
        decals[i].life -= dt;
        if (decals[i].life <= 0) decals[i].active = false;
    }
}

void Decals_Draw(void) {
    // Don't write depth: keeps faraway transparency layered with bullets/sky
    // and prevents flickering against the surface we're glued to.
    rlDisableDepthMask();
    rlSetTexture(0);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < MAX_DECALS; i++) {
        if (!decals[i].active) continue;
        Decal *d = &decals[i];

        float ratio = d->life / d->maxLife;
        float fade  = (ratio > 0.25f) ? 1.0f : (ratio / 0.25f);

        Color cCenter, cEdge;
        if (d->kind == DECAL_BLOOD) {
            cCenter = (Color){ 160, 10, 10, (unsigned char)(220 * fade) };
            cEdge   = (Color){  60,  0,  0, 0 };
        } else {
            cCenter = (Color){  20,  18, 16, (unsigned char)(210 * fade) };
            cEdge   = (Color){  35,  32, 28, 0 };
        }

        Vector3 c = Vector3Add(d->pos, Vector3Scale(d->normal, 0.012f));
        Vector3 cu = d->tangentU, cv = d->tangentV;
        float r = d->size;

        const int N = 10;
        for (int k = 0; k < N; k++) {
            float a0 = d->yaw + (k    ) * (6.28318530f / N);
            float a1 = d->yaw + (k + 1) * (6.28318530f / N);
            Vector3 p0 = Vector3Add(c, Vector3Add(Vector3Scale(cu, cosf(a0)*r),
                                                  Vector3Scale(cv, sinf(a0)*r)));
            Vector3 p1 = Vector3Add(c, Vector3Add(Vector3Scale(cu, cosf(a1)*r),
                                                  Vector3Scale(cv, sinf(a1)*r)));
            rlColor4ub(cCenter.r, cCenter.g, cCenter.b, cCenter.a);
            rlVertex3f(c.x, c.y, c.z);
            rlColor4ub(cEdge.r, cEdge.g, cEdge.b, cEdge.a);
            rlVertex3f(p0.x, p0.y, p0.z);
            rlColor4ub(cEdge.r, cEdge.g, cEdge.b, cEdge.a);
            rlVertex3f(p1.x, p1.y, p1.z);
        }
    }
    rlEnd();
    rlEnableDepthMask();
}
