#include "particles.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

// ---- pool ------------------------------------------------------------------

#define MAX_PARTICLES 256

typedef enum {
    PBLEND_ADDITIVE,  // muzzle flash, sparks, explosion fire
    PBLEND_ALPHA,     // blood mist, casings, smoke puffs
} ParticleBlend;

typedef struct {
    bool          active;
    ParticleBlend blend;
    Vector3       pos;
    Vector3       vel;
    float         size;
    float         life;
    float         maxLife;
    Color         colorStart;
    Color         colorEnd;
    float         gravity;   // m/s^2 downward acceleration (0 = none)
} Particle;

static Particle particles[MAX_PARTICLES];
static int      nextSlot = 0;   // ring-buffer write head when pool is full

void Particles_Reset(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
    nextSlot = 0;
}

// ---- internal spawn --------------------------------------------------------

static Particle *AllocParticle(void) {
    // Prefer a free slot; fall back to overwriting the ring head.
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) return &particles[i];
    }
    Particle *p = &particles[nextSlot];
    nextSlot = (nextSlot + 1) % MAX_PARTICLES;
    return p;
}

static float Randf(void) { return (float)(rand() % 10000) / 10000.0f; }
static float RandfRange(float lo, float hi) { return lo + Randf() * (hi - lo); }

// Uniformly-distributed random direction vector in the hemisphere centred on
// `axis`, up to `halfAngle` radians off-axis.
static Vector3 RandCone(Vector3 axis, float halfAngle) {
    // Pick a random rotation axis perpendicular to `axis`.
    Vector3 perp;
    if (fabsf(axis.y) < 0.9f)
        perp = Vector3Normalize(Vector3CrossProduct(axis, (Vector3){0,1,0}));
    else
        perp = Vector3Normalize(Vector3CrossProduct(axis, (Vector3){1,0,0}));

    // Random azimuth + polar angle within the cone.
    float az    = Randf() * 6.28318f;
    float polar = Randf() * halfAngle;

    // Rodrigues rotation of `axis` by `polar` around a random azimuthal axis.
    Vector3 az_perp = Vector3Normalize(
        Vector3Add(Vector3Scale(perp, cosf(az)),
                   Vector3Scale(Vector3CrossProduct(axis, perp), sinf(az))));

    Vector3 v = Vector3Add(
        Vector3Scale(axis,    cosf(polar)),
        Vector3Scale(az_perp, sinf(polar)));
    return Vector3Normalize(v);
}

// ---- typed spawners --------------------------------------------------------

void Particles_MuzzleFlash(Vector3 pos, Vector3 dir) {
    // 2-4 short-lived additive flash/spark particles along fire dir.
    int n = 2 + (rand() % 3);
    for (int i = 0; i < n; i++) {
        Particle *p = AllocParticle();
        Vector3 d    = RandCone(dir, 0.35f);
        float speed  = RandfRange(3.5f, 9.0f);
        float life   = RandfRange(0.04f, 0.10f);
        *p = (Particle){
            .active     = true,
            .blend      = PBLEND_ADDITIVE,
            .pos        = pos,
            .vel        = Vector3Scale(d, speed),
            .size       = RandfRange(0.06f, 0.14f),
            .life       = life,
            .maxLife    = life,
            .colorStart = (Color){ 255, 210, 110, 255 },
            .colorEnd   = (Color){ 255, 130,  30,   0 },
            .gravity    = 0.0f,
        };
    }
}

void Particles_CasingEject(Vector3 pos, Vector3 right) {
    // One warm-brass alpha particle popped up and to the right, falls under gravity.
    Particle *p = AllocParticle();
    Vector3 up   = { 0, 1, 0 };
    Vector3 vel  = Vector3Add(
        Vector3Scale(right, RandfRange(1.8f, 3.0f)),
        Vector3Scale(up,    RandfRange(2.0f, 3.5f)));
    vel = Vector3Add(vel, Vector3Scale((Vector3){
        RandfRange(-0.4f, 0.4f), 0, RandfRange(-0.4f, 0.4f)}, 1.0f));
    float life = RandfRange(0.5f, 0.9f);
    *p = (Particle){
        .active     = true,
        .blend      = PBLEND_ALPHA,
        .pos        = pos,
        .vel        = vel,
        .size       = RandfRange(0.025f, 0.040f),
        .life       = life,
        .maxLife    = life,
        .colorStart = (Color){ 210, 160,  60, 220 },
        .colorEnd   = (Color){ 120,  80,  20,   0 },
        .gravity    = 9.8f,
    };
}

void Particles_BloodMist(Vector3 pos, Vector3 dir, bool headshot) {
    // A few dark-red alpha puffs; a couple extra on headshot.
    int n = headshot ? (4 + (rand() % 3)) : (2 + (rand() % 2));
    for (int i = 0; i < n; i++) {
        Particle *p = AllocParticle();
        Vector3 d    = RandCone(dir, 0.80f);
        float speed  = RandfRange(0.8f, 2.8f);
        float life   = RandfRange(0.20f, 0.45f);
        *p = (Particle){
            .active     = true,
            .blend      = PBLEND_ALPHA,
            .pos        = pos,
            .vel        = Vector3Scale(d, speed),
            .size       = RandfRange(0.07f, headshot ? 0.20f : 0.14f),
            .life       = life,
            .maxLife    = life,
            .colorStart = (Color){ 150, 10, 10, 200 },
            .colorEnd   = (Color){  70,  0,  0,   0 },
            .gravity    = 1.5f,
        };
    }
}

void Particles_Explosion(Vector3 pos) {
    // Additive orange/yellow fire sparks.
    int sparks = 18 + (rand() % 8);
    for (int i = 0; i < sparks; i++) {
        Particle *p = AllocParticle();
        Vector3 d    = RandCone((Vector3){0,1,0}, 1.40f);
        float speed  = RandfRange(3.0f, 10.0f);
        float life   = RandfRange(0.25f, 0.70f);
        bool hot     = (rand() % 2);
        *p = (Particle){
            .active     = true,
            .blend      = PBLEND_ADDITIVE,
            .pos        = Vector3Add(pos, (Vector3){ RandfRange(-0.15f,0.15f), 0.1f, RandfRange(-0.15f,0.15f) }),
            .vel        = Vector3Scale(d, speed),
            .size       = RandfRange(0.05f, 0.16f),
            .life       = life,
            .maxLife    = life,
            .colorStart = hot ? (Color){ 255, 200, 80, 255 } : (Color){ 255, 120, 30, 255 },
            .colorEnd   = (Color){ 255,  60,  0,   0 },
            .gravity    = 4.0f,
        };
    }
    // Alpha smoke/grey puffs rising upward.
    int puffs = 5 + (rand() % 4);
    for (int i = 0; i < puffs; i++) {
        Particle *p = AllocParticle();
        Vector3 d    = RandCone((Vector3){0,1,0}, 0.60f);
        float speed  = RandfRange(1.5f, 4.0f);
        float life   = RandfRange(0.35f, 0.80f);
        *p = (Particle){
            .active     = true,
            .blend      = PBLEND_ALPHA,
            .pos        = Vector3Add(pos, (Vector3){ RandfRange(-0.2f,0.2f), 0.2f, RandfRange(-0.2f,0.2f) }),
            .vel        = Vector3Scale(d, speed),
            .size       = RandfRange(0.12f, 0.28f),
            .life       = life,
            .maxLife    = life,
            .colorStart = (Color){ 90, 80, 70, 180 },
            .colorEnd   = (Color){ 50, 45, 40,   0 },
            .gravity    = -1.2f,   // smoke rises
        };
    }
}

// ---- update ----------------------------------------------------------------

void Particles_Update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0.0f) { p->active = false; continue; }
        p->vel.y   -= p->gravity * dt;
        p->pos      = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
    }
}

// ---- draw ------------------------------------------------------------------

// Camera-facing billboard for a particle: emit two triangles aligned to the
// camera right/up axes so the quad always faces the viewer. Colour is lerped
// between colorStart and colorEnd over the particle's life.
static inline Color LerpColor(Color a, Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        (unsigned char)(a.a + (b.a - a.a) * t),
    };
}

void Particles_Draw(Camera camera) {
    // Precompute camera axes for billboard alignment.
    Vector3 fwd  = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 rght = Vector3Normalize(Vector3CrossProduct(fwd, camera.up));
    Vector3 up   = Vector3CrossProduct(rght, fwd);

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlSetTexture(0);

    // Draw additive pass first, then alpha — avoids sorting.
    for (int pass = 0; pass < 2; pass++) {
        ParticleBlend wantBlend = (pass == 0) ? PBLEND_ADDITIVE : PBLEND_ALPHA;
        bool hasAny = false;
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].active && particles[i].blend == wantBlend) { hasAny = true; break; }
        }
        if (!hasAny) continue;

        if (wantBlend == PBLEND_ADDITIVE) BeginBlendMode(PBLEND_ADDITIVE);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < MAX_PARTICLES; i++) {
            Particle *p = &particles[i];
            if (!p->active || p->blend != wantBlend) continue;

            float t   = 1.0f - (p->life / p->maxLife);   // 0=new, 1=end
            Color col = LerpColor(p->colorStart, p->colorEnd, t);
            float r   = p->size * 0.5f;

            Vector3 c  = p->pos;
            Vector3 hr = Vector3Scale(rght, r);
            Vector3 hu = Vector3Scale(up,   r);

            // Four corners of the billboard quad.
            Vector3 tl = Vector3Add(Vector3Subtract(c, hr), hu);
            Vector3 tr = Vector3Add(Vector3Add(c, hr), hu);
            Vector3 br = Vector3Subtract(Vector3Add(c, hr), hu);
            Vector3 bl = Vector3Subtract(Vector3Subtract(c, hr), hu);

            rlColor4ub(col.r, col.g, col.b, col.a);
            // Triangle 1: tl, bl, br
            rlVertex3f(tl.x, tl.y, tl.z);
            rlVertex3f(bl.x, bl.y, bl.z);
            rlVertex3f(br.x, br.y, br.z);
            // Triangle 2: tl, br, tr
            rlVertex3f(tl.x, tl.y, tl.z);
            rlVertex3f(br.x, br.y, br.z);
            rlVertex3f(tr.x, tr.y, tr.z);
        }
        rlEnd();
        if (wantBlend == PBLEND_ADDITIVE) EndBlendMode();
    }

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
