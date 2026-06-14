#include "particles.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

// ---- pool ------------------------------------------------------------------

#define MAX_PARTICLES 256

typedef struct {
    bool             active;
    EngParticleBlend blend;
    Vector3          pos;
    Vector3          vel;
    float            size;
    float            life;
    float            maxLife;
    Color            colorStart;
    Color            colorEnd;
    float            gravity;   // m/s^2 downward acceleration (0 = none)
} Particle;

static Particle particles[MAX_PARTICLES];
static int      nextSlot = 0;   // ring-buffer write head when pool is full

void Particles_Reset(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
    nextSlot = 0;
}

// ---- internal helpers ------------------------------------------------------

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

// ---- generic emitter API ---------------------------------------------------

void Eng_FxEmit(EngEmitterDesc desc) {
    for (int i = 0; i < desc.count; i++) {
        Particle *p = AllocParticle();

        Vector3 d = (desc.halfAngle > 0.0f)
            ? RandCone(desc.dir, desc.halfAngle)
            : desc.dir;
        float speed = RandfRange(desc.speedMin, desc.speedMax);
        float life  = RandfRange(desc.ttlMin,   desc.ttlMax);
        float sz    = RandfRange(desc.sizeMin,  desc.sizeMax);

        Vector3 jitter = {
            RandfRange(-desc.posJitterX, desc.posJitterX),
            RandfRange(-desc.posJitterY, desc.posJitterY),
            RandfRange(-desc.posJitterZ, desc.posJitterZ),
        };

        *p = (Particle){
            .active     = true,
            .blend      = desc.blend,
            .pos        = Vector3Add(desc.pos, jitter),
            .vel        = Vector3Scale(d, speed),
            .size       = sz,
            .life       = life,
            .maxLife    = life,
            .colorStart = desc.colorStart,
            .colorEnd   = desc.colorEnd,
            .gravity    = desc.gravity,
        };
    }
}

// ---- typed spawners (thin wrappers over Eng_FxEmit) -----------------------

void Particles_MuzzleFlash(Vector3 pos, Vector3 dir) {
    // 2-4 short-lived additive flash/spark particles along fire dir.
    Eng_FxEmit((EngEmitterDesc){
        .pos        = pos,
        .dir        = dir,
        .count      = 2 + (rand() % 3),
        .halfAngle  = 0.35f,
        .speedMin   = 3.5f,
        .speedMax   = 9.0f,
        .sizeMin    = 0.06f,
        .sizeMax    = 0.14f,
        .ttlMin     = 0.04f,
        .ttlMax     = 0.10f,
        .colorStart = (Color){ 255, 210, 110, 255 },
        .colorEnd   = (Color){ 255, 130,  30,   0 },
        .gravity    = 0.0f,
        .blend      = ENG_PBLEND_ADDITIVE,
    });
}

void Particles_CasingEject(Vector3 pos, Vector3 right) {
    // One warm-brass alpha particle: compound right+up velocity, falls under gravity.
    // Build the velocity manually (it's not a simple cone) then normalise to get a
    // direction + speed to pass into the generic desc.
    Vector3 up  = { 0, 1, 0 };
    Vector3 vel = Vector3Add(
        Vector3Scale(right, RandfRange(1.8f, 3.0f)),
        Vector3Scale(up,    RandfRange(2.0f, 3.5f)));
    vel.x += RandfRange(-0.4f, 0.4f);
    vel.z += RandfRange(-0.4f, 0.4f);

    float   speed = Vector3Length(vel);
    Vector3 dir   = (speed > 1e-4f) ? Vector3Scale(vel, 1.0f / speed) : up;
    float   life  = RandfRange(0.5f, 0.9f);
    float   sz    = RandfRange(0.025f, 0.040f);

    // halfAngle=0: Eng_FxEmit uses desc.dir exactly (no cone randomisation),
    // so speedMin == speedMax == speed gives us our pre-built velocity vector.
    Eng_FxEmit((EngEmitterDesc){
        .pos        = pos,
        .dir        = dir,
        .count      = 1,
        .halfAngle  = 0.0f,
        .speedMin   = speed,
        .speedMax   = speed,
        .sizeMin    = sz,
        .sizeMax    = sz,
        .ttlMin     = life,
        .ttlMax     = life,
        .colorStart = (Color){ 210, 160,  60, 220 },
        .colorEnd   = (Color){ 120,  80,  20,   0 },
        .gravity    = 9.8f,
        .blend      = ENG_PBLEND_ALPHA,
    });
}

void Particles_BloodMist(Vector3 pos, Vector3 dir, bool headshot) {
    // A few dark-red alpha puffs; a couple extra on headshot.
    Eng_FxEmit((EngEmitterDesc){
        .pos        = pos,
        .dir        = dir,
        .count      = headshot ? (4 + (rand() % 3)) : (2 + (rand() % 2)),
        .halfAngle  = 0.80f,
        .speedMin   = 0.8f,
        .speedMax   = 2.8f,
        .sizeMin    = 0.07f,
        .sizeMax    = headshot ? 0.20f : 0.14f,
        .ttlMin     = 0.20f,
        .ttlMax     = 0.45f,
        .colorStart = (Color){ 150, 10, 10, 200 },
        .colorEnd   = (Color){  70,  0,  0,   0 },
        .gravity    = 1.5f,
        .blend      = ENG_PBLEND_ALPHA,
    });
}

void Particles_Explosion(Vector3 pos) {
    // Additive orange/yellow fire sparks — two hot/warm variants interleaved.
    int sparks = 18 + (rand() % 8);
    // Alternate hot/warm per spark to get the mixed look without a conditional
    // inside Eng_FxEmit. Emit hot and warm halves separately.
    int hot_n  = sparks / 2;
    int warm_n = sparks - hot_n;

    // Spark origin is slightly above ground (+0.1 Y); smoke at +0.2 Y.
    Vector3 posLow  = { pos.x, pos.y + 0.1f, pos.z };
    Vector3 posHigh = { pos.x, pos.y + 0.2f, pos.z };

    Eng_FxEmit((EngEmitterDesc){
        .pos        = posLow,
        .dir        = (Vector3){ 0, 1, 0 },
        .count      = hot_n,
        .halfAngle  = 1.40f,
        .speedMin   = 3.0f,
        .speedMax   = 10.0f,
        .sizeMin    = 0.05f,
        .sizeMax    = 0.16f,
        .ttlMin     = 0.25f,
        .ttlMax     = 0.70f,
        .colorStart = (Color){ 255, 200, 80, 255 },
        .colorEnd   = (Color){ 255,  60,  0,  0 },
        .gravity    = 4.0f,
        .blend      = ENG_PBLEND_ADDITIVE,
        .posJitterX = 0.15f,
        .posJitterY = 0.0f,
        .posJitterZ = 0.15f,
    });
    Eng_FxEmit((EngEmitterDesc){
        .pos        = posLow,
        .dir        = (Vector3){ 0, 1, 0 },
        .count      = warm_n,
        .halfAngle  = 1.40f,
        .speedMin   = 3.0f,
        .speedMax   = 10.0f,
        .sizeMin    = 0.05f,
        .sizeMax    = 0.16f,
        .ttlMin     = 0.25f,
        .ttlMax     = 0.70f,
        .colorStart = (Color){ 255, 120, 30, 255 },
        .colorEnd   = (Color){ 255,  60,  0,  0 },
        .gravity    = 4.0f,
        .blend      = ENG_PBLEND_ADDITIVE,
        .posJitterX = 0.15f,
        .posJitterY = 0.0f,
        .posJitterZ = 0.15f,
    });

    // Alpha smoke/grey puffs rising upward.
    Eng_FxEmit((EngEmitterDesc){
        .pos        = posHigh,
        .dir        = (Vector3){ 0, 1, 0 },
        .count      = 5 + (rand() % 4),
        .halfAngle  = 0.60f,
        .speedMin   = 1.5f,
        .speedMax   = 4.0f,
        .sizeMin    = 0.12f,
        .sizeMax    = 0.28f,
        .ttlMin     = 0.35f,
        .ttlMax     = 0.80f,
        .colorStart = (Color){ 90, 80, 70, 180 },
        .colorEnd   = (Color){ 50, 45, 40,   0 },
        .gravity    = -1.2f,  // smoke rises
        .blend      = ENG_PBLEND_ALPHA,
        .posJitterX = 0.2f,
        .posJitterY = 0.0f,
        .posJitterZ = 0.2f,
    });
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
        EngParticleBlend wantBlend = (pass == 0) ? ENG_PBLEND_ADDITIVE : ENG_PBLEND_ALPHA;
        bool hasAny = false;
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].active && particles[i].blend == wantBlend) { hasAny = true; break; }
        }
        if (!hasAny) continue;

        if (wantBlend == ENG_PBLEND_ADDITIVE) BeginBlendMode(BLEND_ADDITIVE);
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
        if (wantBlend == ENG_PBLEND_ADDITIVE) EndBlendMode();
    }

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
