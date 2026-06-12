#include "audio.h"
#include "weapons.h"
#include "game.h"
#include "level.h"
#include "entities.h"
#include "player.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

float audioMasterVol = 0.7f;

#define SAMPLE_RATE   22050
#define MAX_ALIASES   6   // simultaneous variants per slot (rapid fire reuse)

// Ducking multiplier applied to non-heartbeat sounds while local player is downed.
#define DOWNED_DUCK   0.28f

typedef struct {
    Sound base;
    Sound aliases[MAX_ALIASES];
    int   next;
    bool  loaded;
} SoundSlot;

// ---- original sound bank -------------------------------------------------
static SoundSlot sfx_shot;
static SoundSlot sfx_shotgun;
static SoundSlot sfx_raygun;
static SoundSlot sfx_hit;
static SoundSlot sfx_head;
static SoundSlot sfx_kill;
static SoundSlot sfx_dmg;
static SoundSlot sfx_reload;
static SoundSlot sfx_roundStart;
static SoundSlot sfx_roundEnd;
static SoundSlot sfx_buy;
static SoundSlot sfx_powerup;
static SoundSlot sfx_mboxRoll;
static SoundSlot sfx_mboxStop;
static SoundSlot sfx_jump;
static SoundSlot sfx_perk;

// ---- new sound slots ------------------------------------------------------
// Footstep variants (3 timbre variants to avoid machine-gun effect)
#define STEP_VARIANTS 3
static SoundSlot sfx_step[STEP_VARIANTS];

// Reload two-stage: mag-out click, mag-in clack
static SoundSlot sfx_magOut;
static SoundSlot sfx_magIn;

// Zombie vocalisation per type
static SoundSlot sfx_groan;    // ZT_NORMAL
static SoundSlot sfx_growl;    // ZT_RUNNER
static SoundSlot sfx_hiss;     // ZT_CRAWLER
static SoundSlot sfx_roar;     // ZT_BOSS
static SoundSlot sfx_snarl;    // melee-range attack snarl (any type)

// Downed heartbeat
static SoundSlot sfx_heartbeat;

// ----- procedural wave generators -----------------------------------------

static Wave WaveAlloc(int frameCount) {
    Wave w = { 0 };
    w.frameCount = frameCount;
    w.sampleRate = SAMPLE_RATE;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data = calloc(frameCount, sizeof(short));
    return w;
}

static void WritePCM(Wave *w, int i, float sample) {
    if (sample >  1.0f) sample =  1.0f;
    if (sample < -1.0f) sample = -1.0f;
    ((short *)w->data)[i] = (short)(sample * 32000.0f);
}

static float frand(void) { return (float)rand() / (float)RAND_MAX * 2.0f - 1.0f; }

// Lowpass IIR. y[n] = y[n-1] + a * (x[n] - y[n-1])
static void Lowpass(Wave *w, float cutoffHz) {
    float a = cutoffHz / (cutoffHz + SAMPLE_RATE);
    float y = 0;
    for (unsigned int i = 0; i < w->frameCount; i++) {
        float x = ((short *)w->data)[i] / 32000.0f;
        y = y + a * (x - y);
        WritePCM(w, i, y);
    }
}

// Sharp attack, exponential decay envelope. attack/decay in seconds.
static float Env(float t, float dur, float attack, float decayTau) {
    if (t < attack) return t / attack;
    float rt = t - attack;
    float e = expf(-rt / decayTau);
    if (t > dur) e *= 0.0f;
    return e;
}

static Wave GenGunshot(float dur, float lpHz, float pitchDownTau) {
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.002f, 0.06f);
        // Sub-bass thump via a damped low sine, mixed with noise.
        float subFreq = 80.0f * expf(-t / pitchDownTau);
        float sub = sinf(2.0f * 3.14159265f * subFreq * t) * 0.6f;
        float ns  = frand() * 0.9f;
        WritePCM(&w, i, (sub + ns) * env);
    }
    Lowpass(&w, lpHz);
    return w;
}

static Wave GenHit(void) {
    int n = (int)(0.06f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.06f, 0.001f, 0.02f);
        float tone = sinf(2.0f * 3.14159265f * 1200.0f * t);
        WritePCM(&w, i, tone * env * 0.6f);
    }
    return w;
}

static Wave GenHeadshot(void) {
    int n = (int)(0.18f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.18f, 0.003f, 0.05f);
        // Up-chirp from 900 → 1600 Hz.
        float f = 900.0f + 700.0f * (t / 0.18f);
        float tone = sinf(2.0f * 3.14159265f * f * t);
        WritePCM(&w, i, tone * env * 0.7f);
    }
    return w;
}

static Wave GenKill(void) {
    int n = (int)(0.18f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.18f, 0.003f, 0.06f);
        // Down-chirp.
        float f = 600.0f - 300.0f * (t / 0.18f);
        float tone = sinf(2.0f * 3.14159265f * f * t);
        float ns = frand() * 0.2f;
        WritePCM(&w, i, (tone + ns) * env * 0.5f);
    }
    Lowpass(&w, 4000.0f);
    return w;
}

static Wave GenDamage(void) {
    int n = (int)(0.25f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.25f, 0.005f, 0.10f);
        float f = 110.0f * expf(-t / 0.18f);
        float tone = sinf(2.0f * 3.14159265f * f * t);
        WritePCM(&w, i, tone * env * 0.85f);
    }
    Lowpass(&w, 700.0f);
    return w;
}

static Wave GenReload(void) {
    int n = (int)(0.30f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    // Two distinct clicks at 0.0 and ~0.18s.
    float clickAt[2] = { 0.00f, 0.16f };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float s = 0;
        for (int c = 0; c < 2; c++) {
            float ct = t - clickAt[c];
            if (ct >= 0 && ct < 0.04f) {
                float env = expf(-ct / 0.008f);
                s += frand() * env * 0.7f + sinf(2.0f * 3.14159265f * 350.0f * ct) * env * 0.5f;
            }
        }
        WritePCM(&w, i, s);
    }
    Lowpass(&w, 2500.0f);
    return w;
}

// Three-note chime, equal durations. freqs in Hz.
static Wave GenChime(float f0, float f1, float f2, float noteDur) {
    float dur = noteDur * 3.0f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    float freqs[3] = { f0, f1, f2 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        int note = (int)(t / noteDur);
        if (note > 2) note = 2;
        float nt = t - note * noteDur;
        float env = Env(nt, noteDur, 0.005f, 0.20f);
        float a = sinf(2.0f * 3.14159265f * freqs[note]         * nt) * 0.5f;
        float b = sinf(2.0f * 3.14159265f * freqs[note] * 2.0f  * nt) * 0.25f;
        WritePCM(&w, i, (a + b) * env);
    }
    return w;
}

static Wave GenBuy(void) {
    int n = (int)(0.10f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.10f, 0.002f, 0.04f);
        float tone = sinf(2.0f * 3.14159265f * 900.0f * t)
                   + sinf(2.0f * 3.14159265f * 1350.0f * t) * 0.5f;
        WritePCM(&w, i, tone * env * 0.35f);
    }
    return w;
}

static Wave GenPowerup(void) {
    int n = (int)(0.45f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    // Up-arpeggio sweep.
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.45f, 0.003f, 0.18f);
        float f = 440.0f + 600.0f * (t / 0.45f) + sinf(t * 40.0f) * 80.0f;
        float tone = sinf(2.0f * 3.14159265f * f * t);
        float over = sinf(2.0f * 3.14159265f * f * 2.0f * t) * 0.3f;
        WritePCM(&w, i, (tone + over) * env * 0.4f);
    }
    return w;
}

static Wave GenMBoxRoll(void) {
    int n = (int)(0.7f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    // Series of short clicks accelerating then steady.
    int ti = 0;
    while (ti < n) {
        int gap = 1000 + (int)(2000.0f * (1.0f - (float)ti / n));
        if (gap < 600) gap = 600;
        int len = 200;
        for (int k = 0; k < len && ti + k < n; k++) {
            float lt = (float)k / SAMPLE_RATE;
            float env = expf(-lt / 0.005f);
            float s = frand() * env * 0.7f + sinf(2.0f * 3.14159265f * 800.0f * lt) * env * 0.4f;
            WritePCM(&w, ti + k, s);
        }
        ti += gap;
    }
    return w;
}

static Wave GenMBoxStop(void) {
    int n = (int)(0.25f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.25f, 0.003f, 0.10f);
        float tone = sinf(2.0f * 3.14159265f * 660.0f * t)
                   + sinf(2.0f * 3.14159265f * 990.0f * t) * 0.6f;
        WritePCM(&w, i, tone * env * 0.5f);
    }
    return w;
}

static Wave GenJump(void) {
    int n = (int)(0.10f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = Env(t, 0.10f, 0.005f, 0.05f);
        float ns = frand() * 0.5f;
        WritePCM(&w, i, ns * env * 0.3f);
    }
    Lowpass(&w, 600.0f);
    return w;
}

static Wave GenPerk(void) {
    int n = (int)(0.55f * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    float freqs[4] = { 392.0f, 494.0f, 587.0f, 784.0f };  // G B D G (G major)
    float noteDur = 0.55f / 4.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        int note = (int)(t / noteDur);
        if (note > 3) note = 3;
        float nt = t - note * noteDur;
        float env = Env(nt, noteDur, 0.005f, 0.10f);
        float a = sinf(2.0f * 3.14159265f * freqs[note] * nt) * 0.4f;
        float b = sinf(2.0f * 3.14159265f * freqs[note] * 2.0f * nt) * 0.2f;
        WritePCM(&w, i, (a + b) * env);
    }
    return w;
}

// ---- new generator functions ----------------------------------------------

// Footstep: short filtered noise thud.
// variant 0 = normal pitch, 1 = slightly higher, 2 = slightly duller/lower.
static Wave GenFootstep(int variant) {
    float dur = 0.055f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    // Sub-thump: damped sine at low freq + noise burst.
    float baseFreq  = (variant == 1) ? 120.0f : (variant == 2) ? 70.0f : 95.0f;
    float decayTau  = (variant == 2) ? 0.025f : 0.018f;
    float lpCutoff  = (variant == 1) ? 900.0f : (variant == 2) ? 550.0f : 700.0f;
    float noiseMix  = (variant == 1) ? 0.55f  : 0.65f;
    float toneMix   = 1.0f - noiseMix;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.001f, decayTau);
        float tone = sinf(2.0f * 3.14159265f * baseFreq * t) * toneMix;
        float ns   = frand() * noiseMix;
        WritePCM(&w, i, (tone + ns) * env * 0.75f);
    }
    Lowpass(&w, lpCutoff);
    return w;
}

// Mag-out: short metallic scrape/click — higher-pitched noise with a
// downward-chirped tone.
static Wave GenMagOut(void) {
    float dur = 0.12f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.002f, 0.035f);
        float f   = 2200.0f - 800.0f * (t / dur);  // downward chirp
        float tone = sinf(2.0f * 3.14159265f * f * t) * 0.45f;
        float ns   = frand() * 0.6f;
        WritePCM(&w, i, (tone + ns) * env * 0.8f);
    }
    Lowpass(&w, 3500.0f);
    return w;
}

// Mag-in: solid mechanical clack — heavier, with a low resonant click.
static Wave GenMagIn(void) {
    float dur = 0.14f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.001f, 0.030f);
        // Two-tone clack: body impact + brief high ping.
        float body = sinf(2.0f * 3.14159265f * 380.0f * t) * 0.5f;
        float ping = sinf(2.0f * 3.14159265f * 2800.0f * t) * expf(-t / 0.010f) * 0.35f;
        float ns   = frand() * 0.5f;
        WritePCM(&w, i, (body + ping + ns) * env * 0.85f);
    }
    Lowpass(&w, 4000.0f);
    return w;
}

// Normal zombie: low, slow groan — modulated sine descending.
static Wave GenGroan(void) {
    float dur = 0.60f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.06f, 0.22f);
        // Fundamental glides from ~120 Hz down to ~80 Hz via phase accumulation.
        float phase = 2.0f * 3.14159265f * (120.0f * t - 20.0f * t * t / dur);
        float tone  = sinf(phase) * 0.6f;
        float ns    = frand() * 0.15f;
        // Vibrato
        float vib   = sinf(2.0f * 3.14159265f * 5.5f * t) * 0.08f;
        WritePCM(&w, i, (tone * (1.0f + vib) + ns) * env);
    }
    Lowpass(&w, 800.0f);
    return w;
}

// Runner zombie: raspier, faster growl.
static Wave GenGrowl(void) {
    float dur = 0.35f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.015f, 0.12f);
        // Higher, guttural — more noise in the mix.
        float f    = 160.0f + 40.0f * sinf(t * 22.0f);
        float tone = sinf(2.0f * 3.14159265f * f * t) * 0.45f;
        // Raspy noise overlay.
        float ns   = frand() * 0.65f;
        WritePCM(&w, i, (tone + ns) * env * 0.9f);
    }
    Lowpass(&w, 1400.0f);
    return w;
}

// Crawler zombie: sibilant hiss.
static Wave GenHiss(void) {
    float dur = 0.45f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.03f, 0.18f);
        // Pure high-frequency noise, envelope shaped.
        float ns  = frand();
        WritePCM(&w, i, ns * env * 0.55f);
    }
    // High-pass effect: bandpass by running lowpass then subtracting from original.
    // Approximate: just apply a highish-frequency lowpass.
    Lowpass(&w, 6000.0f);
    // Second pass: notch the very lows by running at a high cutoff,
    // then scale. Simpler — just leave the first lowpass; it's filtered enough.
    return w;
}

// Boss zombie: deep, resonant roar.
static Wave GenRoar(void) {
    float dur = 0.85f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t    = (float)i / SAMPLE_RATE;
        float env  = Env(t, dur, 0.04f, 0.30f);
        // Sub-bass with slow pitch wobble via phase accumulation.
        float phase = 2.0f * 3.14159265f * (55.0f * t + (15.0f / 7.0f) * (1.0f - cosf(7.0f * t)));
        float sub   = sinf(phase) * 0.7f;
        float over  = sinf(phase * 3.0f) * 0.25f;       // 3rd harmonic grit
        float ns    = frand() * 0.2f;
        WritePCM(&w, i, (sub + over + ns) * env);
    }
    Lowpass(&w, 600.0f);
    return w;
}

// Attack snarl: short, aggressive burst.
static Wave GenSnarl(void) {
    float dur = 0.22f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    for (int i = 0; i < n; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float env = Env(t, dur, 0.004f, 0.07f);
        float f   = 200.0f + 100.0f * expf(-t / 0.04f);
        float tone = sinf(2.0f * 3.14159265f * f * t) * 0.5f;
        float ns   = frand() * 0.7f;
        WritePCM(&w, i, (tone + ns) * env);
    }
    Lowpass(&w, 2200.0f);
    return w;
}

// Heartbeat: two-thump pattern (lub-dub).
// Returns a single lub+dub wave ~0.30s long. Looped with a variable-period
// timer to get rate control.
static Wave GenHeartbeat(void) {
    float dur = 0.30f;
    int n = (int)(dur * SAMPLE_RATE);
    Wave w = WaveAlloc(n);
    // Lub at t=0.0s, dub at t=0.10s. Both are short sub-bass thumps.
    float beats[2] = { 0.0f, 0.10f };
    float lubGain[2] = { 0.95f, 0.65f };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float s = 0;
        for (int b = 0; b < 2; b++) {
            float bt = t - beats[b];
            if (bt >= 0 && bt < 0.08f) {
                float env = Env(bt, 0.08f, 0.003f, 0.025f);
                float f   = 70.0f * expf(-bt / 0.04f);
                s += sinf(2.0f * 3.14159265f * f * bt) * env * lubGain[b];
            }
        }
        WritePCM(&w, i, s);
    }
    Lowpass(&w, 500.0f);
    return w;
}

// ----- slot management ----------------------------------------------------

static void LoadSlot(SoundSlot *slot, Wave wave) {
    slot->base = LoadSoundFromWave(wave);
    for (int i = 0; i < MAX_ALIASES; i++) slot->aliases[i] = LoadSoundAlias(slot->base);
    UnloadWave(wave);
    slot->next = 0;
    slot->loaded = true;
}

static void PlaySlot(SoundSlot *slot, float vol, float pitch) {
    if (!slot->loaded) return;
    Sound s = slot->aliases[slot->next];
    slot->next = (slot->next + 1) % MAX_ALIASES;
    SetSoundVolume(s, vol * audioMasterVol);
    SetSoundPitch(s, pitch);
    PlaySound(s);
}

// Play with explicit stereo pan (0=full left, 0.5=centre, 1=full right).
static void PlaySlotPanned(SoundSlot *slot, float vol, float pitch, float pan) {
    if (!slot->loaded) return;
    Sound s = slot->aliases[slot->next];
    slot->next = (slot->next + 1) % MAX_ALIASES;
    SetSoundVolume(s, vol * audioMasterVol);
    SetSoundPitch(s, pitch);
    SetSoundPan(s, pan);
    PlaySound(s);
}

static void FreeSlot(SoundSlot *slot) {
    if (!slot->loaded) return;
    for (int i = 0; i < MAX_ALIASES; i++) UnloadSoundAlias(slot->aliases[i]);
    UnloadSound(slot->base);
    slot->loaded = false;
}

// ----- positional audio helpers -------------------------------------------

// Compute volume attenuation and stereo pan for a world-space position
// relative to the local player camera. `camPos` is camera world pos,
// `camYaw` is the camera yaw angle (rad). `maxDist` is the audible radius.
// Returns false if outside audible range (vol <= 0.01).
static bool PositionalAudio(Vector3 camPos, float camYaw,
                             Vector3 srcPos, float maxDist,
                             float *outVol, float *outPan) {
    float dx = srcPos.x - camPos.x;
    float dz = srcPos.z - camPos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist >= maxDist) return false;
    // Linear rolloff from maxDist → 0.
    float atten = 1.0f - (dist / maxDist);
    *outVol = atten * atten;   // squared for perceptual roll-off feel
    if (*outVol < 0.01f) return false;
    // Stereo pan: compute camera-right dot product with direction to source.
    // camYaw: player faces -Z when yaw=0, so right = (cos(yaw), 0, sin(yaw)).
    float rightX = cosf(camYaw);
    float rightZ = sinf(camYaw);
    float dot = 0.0f;
    if (dist > 0.001f) {
        dot = (dx / dist) * rightX + (dz / dist) * rightZ;
    }
    // dot: -1 = full left, 0 = centre, +1 = full right → remap to [0,1].
    *outPan = (dot + 1.0f) * 0.5f;
    return true;
}

// ----- map music state ----------------------------------------------------

static Music  bgMusic;
static bool   bgMusicLoaded  = false;
static char   bgMusicName[64] = {0};  // currently-loaded music name (track key)
static char   lastMapName[64] = {0};  // mapName snapshot for change detection

// Try to load music stream from a few candidate paths.
// Returns true on success. Writes the loaded stream into bgMusic.
static bool TryLoadMusic(const char *name) {
    const char *prefixes[] = {
        "data/audio/",
        "../data/audio/",
        "./data/audio/",
    };
    const int nPrefixes = 3;
    char path[256];
    for (int i = 0; i < nPrefixes; i++) {
        snprintf(path, sizeof path, "%s%s.ogg", prefixes[i], name);
        // Check if file is accessible before loading.
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fclose(f);
        bgMusic = LoadMusicStream(path);
        if (bgMusic.stream.buffer != NULL) {
            return true;
        }
        // LoadMusicStream failed even though file existed.
        UnloadMusicStream(bgMusic);
    }
    return false;
}

// Parse a map file to find the 'music' key inside ATMOSPHERE block.
// Fills `outName` (max `nameMax` bytes including NUL). Returns true if found.
static bool ParseMapMusicName(const char *path, char *outName, int nameMax) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char raw[256];
    bool inAtmos = false;
    bool found   = false;
    while (!found && fgets(raw, sizeof raw, f)) {
        // Strip comments.
        char *hash = strchr(raw, '#');
        if (hash) *hash = 0;
        // Tokenise in place.
        char *toks[8];
        int n = 0;
        char *s = raw;
        while (*s) {
            while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
            if (!*s) break;
            if (n < 8) toks[n] = s;
            n++;
            while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') s++;
            if (*s) *s++ = 0;
        }
        if (n == 0) continue;
        if (strcmp(toks[0], "ATMOSPHERE") == 0) { inAtmos = true; continue; }
        if (strcmp(toks[0], "END") == 0)         { inAtmos = false; continue; }
        if (inAtmos && strcmp(toks[0], "music") == 0 && n >= 2) {
            strncpy(outName, toks[1], (size_t)(nameMax - 1));
            outName[nameMax - 1] = 0;
            found = true;
        }
    }
    fclose(f);
    return found;
}

// Resolve the map file path from mapName and the known map search dirs.
// Writes path into `outPath` (max `pathMax` bytes). Returns true if found.
static bool FindMapFile(char *outPath, int pathMax) {
    // Maps are stored under data/maps/ (relative to the working dir).
    // We scan known filenames in each search dir, parsing NAME to match mapName.
    // This only runs on map change (once per load), so the cost is acceptable.
    const char *dirs[] = { "data/maps", "../data/maps", "./data/maps" };
    const char *known[] = { "nacht", "factory", "default", "map" };
    for (int d = 0; d < 3; d++) {
        for (int k = 0; k < 4; k++) {
            snprintf(outPath, (size_t)pathMax, "%s/%s.map", dirs[d], known[k]);
            FILE *f = fopen(outPath, "r");
            if (!f) continue;
            char raw[256];
            bool matched = false;
            while (!matched && fgets(raw, sizeof raw, f)) {
                char *hash = strchr(raw, '#');
                if (hash) *hash = 0;
                char *toks[16];
                int n = 0;
                char *p = raw;
                while (*p) {
                    while (*p && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) p++;
                    if (!*p) break;
                    if (n < 16) toks[n] = p;
                    n++;
                    while (*p && *p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n') p++;
                    if (*p) *p++ = 0;
                }
                if (n == 0) continue;
                if (strcmp(toks[0], "NAME") == 0 && n >= 2) {
                    char assembled[64] = {0};
                    for (int i = 1; i < n && i < 8; i++) {
                        if (i > 1) strncat(assembled, " ", sizeof assembled - strlen(assembled) - 1);
                        strncat(assembled, toks[i], sizeof assembled - strlen(assembled) - 1);
                    }
                    if (strcmp(assembled, mapName) == 0) matched = true;
                    // Either way, NAME found — no need to keep scanning this file.
                    break;
                }
            }
            fclose(f);
            if (matched) return true;
        }
    }
    outPath[0] = 0;
    return false;
}

// Called when mapName changes. Look up the music track for the new map,
// unload any previous stream, try to load the new one.
static void Audio_LoadMapMusic(void) {
    // Unload previous music if any.
    if (bgMusicLoaded) {
        StopMusicStream(bgMusic);
        UnloadMusicStream(bgMusic);
        bgMusicLoaded = false;
        bgMusicName[0] = 0;
    }

    if (mapName[0] == 0) return;  // no map loaded

    // Find the map file to parse for the music name.
    char mapPath[256] = {0};
    if (!FindMapFile(mapPath, (int)sizeof mapPath)) return;

    // Extract the music name from the ATMOSPHERE block.
    char musicTrack[64] = {0};
    if (!ParseMapMusicName(mapPath, musicTrack, (int)sizeof musicTrack)) return;
    if (musicTrack[0] == 0) return;

    // Attempt to stream the audio file.
    if (!TryLoadMusic(musicTrack)) return;  // file not present — silent skip

    strncpy(bgMusicName, musicTrack, sizeof bgMusicName - 1);
    SetMusicVolume(bgMusic, 0.25f);
    PlayMusicStream(bgMusic);
    bgMusicLoaded = true;
}

// ----- footstep state -----------------------------------------------------

// Per-player footstep cadence state.
// Index 0 = local player; 1..NET_MAX_PLAYERS-1 = others (by player slot).
#define MAX_STEP_PLAYERS  NET_MAX_PLAYERS

typedef struct {
    float timer;          // time until next step is allowed
    Vector3 lastPos;      // previous frame position (for remote players)
    bool  lastPosValid;
    int   variantIdx;     // rotates through STEP_VARIANTS
} StepState;

static StepState stepState[MAX_STEP_PLAYERS];

// ----- reload two-stage state ---------------------------------------------

// Per-player: track the reload timer and schedule the mag-in click.
typedef struct {
    float lastReloadTimer;
    float magInTimer;     // countdown; when it reaches 0 we play mag-in
    bool  magInPending;
} ReloadState;

static ReloadState reloadState[MAX_STEP_PLAYERS];

// ----- zombie groan state -------------------------------------------------

// Per-enemy: countdown timer until next vocalization.
typedef struct {
    float timer;           // seconds until next vocalization
    bool  initialized;
    bool  lastInMeleeRange; // for snarl edge detection
} EnemyVoxState;

static EnemyVoxState enemyVox[MAX_ENEMIES];

// Maximum concurrent zombie sounds we'll play per tick to avoid blasting.
#define MAX_GROAN_PER_TICK   2

// Distance beyond which zombie groans are inaudible.
#define GROAN_MAX_DIST       20.0f
// Distance at which a zombie is considered in melee range for the snarl.
#define MELEE_SNARL_DIST      2.5f

// ----- downed heartbeat state ---------------------------------------------

static bool  heartbeatActive  = false;
static float heartbeatTimer   = 0.0f;  // countdown to next thump
static bool  lastDowned       = false;

// Interval range for heartbeat in seconds (at full health → fast bleed).
#define HB_INTERVAL_SLOW   1.30f   // start of bleed (bleedTimer near BLEED_TIME)
#define HB_INTERVAL_FAST   0.55f   // end of bleed (bleedTimer near 0)

// Duck multiplier: applied to all non-heartbeat volumes while downed.
static float duckMul = 1.0f;   // 1 = normal; DOWNED_DUCK while downed

// ----- public API ---------------------------------------------------------

void Audio_Init(void) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    LoadSlot(&sfx_shot,       GenGunshot(0.13f, 2200.0f, 0.020f));
    LoadSlot(&sfx_shotgun,    GenGunshot(0.20f, 1400.0f, 0.030f));
    LoadSlot(&sfx_raygun,     GenGunshot(0.18f, 6000.0f, 0.040f));
    LoadSlot(&sfx_hit,        GenHit());
    LoadSlot(&sfx_head,       GenHeadshot());
    LoadSlot(&sfx_kill,       GenKill());
    LoadSlot(&sfx_dmg,        GenDamage());
    LoadSlot(&sfx_reload,     GenReload());
    LoadSlot(&sfx_roundStart, GenChime(440.0f, 554.0f, 659.0f, 0.18f));
    LoadSlot(&sfx_roundEnd,   GenChime(659.0f, 554.0f, 440.0f, 0.20f));
    LoadSlot(&sfx_buy,        GenBuy());
    LoadSlot(&sfx_powerup,    GenPowerup());
    LoadSlot(&sfx_mboxRoll,   GenMBoxRoll());
    LoadSlot(&sfx_mboxStop,   GenMBoxStop());
    LoadSlot(&sfx_jump,       GenJump());
    LoadSlot(&sfx_perk,       GenPerk());

    // Footstep variants.
    for (int v = 0; v < STEP_VARIANTS; v++) {
        LoadSlot(&sfx_step[v], GenFootstep(v));
    }

    // Reload two-stage.
    LoadSlot(&sfx_magOut, GenMagOut());
    LoadSlot(&sfx_magIn,  GenMagIn());

    // Zombie vocalisation.
    LoadSlot(&sfx_groan,  GenGroan());
    LoadSlot(&sfx_growl,  GenGrowl());
    LoadSlot(&sfx_hiss,   GenHiss());
    LoadSlot(&sfx_roar,   GenRoar());
    LoadSlot(&sfx_snarl,  GenSnarl());

    // Heartbeat.
    LoadSlot(&sfx_heartbeat, GenHeartbeat());

    // Zero all runtime state.
    memset(stepState,   0, sizeof stepState);
    memset(reloadState, 0, sizeof reloadState);
    memset(enemyVox,    0, sizeof enemyVox);
    heartbeatActive = false;
    heartbeatTimer  = 0.0f;
    lastDowned      = false;
    duckMul         = 1.0f;
    bgMusicLoaded   = false;
    bgMusicName[0]  = 0;
    lastMapName[0]  = 0;
}

void Audio_Shutdown(void) {
    if (!IsAudioDeviceReady()) return;
    FreeSlot(&sfx_shot);    FreeSlot(&sfx_shotgun); FreeSlot(&sfx_raygun);
    FreeSlot(&sfx_hit);     FreeSlot(&sfx_head);    FreeSlot(&sfx_kill);
    FreeSlot(&sfx_dmg);     FreeSlot(&sfx_reload);
    FreeSlot(&sfx_roundStart); FreeSlot(&sfx_roundEnd);
    FreeSlot(&sfx_buy);     FreeSlot(&sfx_powerup);
    FreeSlot(&sfx_mboxRoll); FreeSlot(&sfx_mboxStop);
    FreeSlot(&sfx_jump);    FreeSlot(&sfx_perk);

    for (int v = 0; v < STEP_VARIANTS; v++) FreeSlot(&sfx_step[v]);
    FreeSlot(&sfx_magOut);  FreeSlot(&sfx_magIn);
    FreeSlot(&sfx_groan);   FreeSlot(&sfx_growl);
    FreeSlot(&sfx_hiss);    FreeSlot(&sfx_roar);
    FreeSlot(&sfx_snarl);   FreeSlot(&sfx_heartbeat);

    if (bgMusicLoaded) {
        StopMusicStream(bgMusic);
        UnloadMusicStream(bgMusic);
        bgMusicLoaded = false;
    }

    CloseAudioDevice();
}

void Audio_Buy(void)  { PlaySlot(&sfx_buy, 0.7f, 1.0f); }

void Audio_Tick(Player *me) {
    // Per-local-player diff state. Static so it lives across frames.
    static int       lastShotsFired = -1, lastShotsHit = -1, lastHeadshots = -1, lastKills = -1;
    static int       lastHp = -1, lastPoints = -1, lastPerks = -1;
    static GamePhase lastPhase = GS_PRE_GAME;
    static int       lastMBoxState = -1;
    static float     lastDoublePoints = 0.0f, lastInstakill = 0.0f;
    static bool      lastOnGround = true;
    static float     killGrace = 0.0f;

    float dt = GetFrameTime();
    if (dt <= 0.0f) dt = 0.016f;

    // ---- first-frame seed -----------------------------------------------
    if (lastShotsFired < 0) {
        lastShotsFired = me->shotsFired; lastShotsHit = me->shotsHit;
        lastHeadshots = me->headshots;   lastKills = me->kills;
        lastHp = me->hp;                 lastPoints = me->points;
        lastPhase = gamePhase;           lastMBoxState = mbox.state;
        lastDoublePoints = doublePointsTimer; lastInstakill = instaKillTimer;
        int mask = 0;
        for (int k = 0; k < PERK_COUNT; k++) if (me->hasPerk[k]) mask |= 1 << k;
        lastPerks = mask;
        lastOnGround = me->onGround;

        // Seed footstep last positions for all players.
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            stepState[i].lastPos = players[i].pos;
            stepState[i].lastPosValid = players[i].active;
            stepState[i].timer = 0.0f;
            stepState[i].variantIdx = i % STEP_VARIANTS;
        }
        // Seed reload state for all players.
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            reloadState[i].lastReloadTimer = players[i].inventory[players[i].currentSlot].reloadTimer;
            reloadState[i].magInPending    = false;
            reloadState[i].magInTimer      = 0.0f;
        }
        // Stagger enemy vocalization timers so they don't all fire at once.
        for (int i = 0; i < MAX_ENEMIES; i++) {
            enemyVox[i].timer = 1.0f + (float)(i % 7) * 0.55f;
            enemyVox[i].initialized = true;
            enemyVox[i].lastInMeleeRange = false;
        }
        lastMapName[0] = 0;
        return;
    }

    // ---- map music: detect mapName change --------------------------------
    if (strcmp(mapName, lastMapName) != 0) {
        strncpy(lastMapName, mapName, sizeof lastMapName - 1);
        lastMapName[sizeof lastMapName - 1] = 0;
        Audio_LoadMapMusic();
    }
    if (bgMusicLoaded) {
        UpdateMusicStream(bgMusic);
    }

    // ---- downed heartbeat / duck setup ----------------------------------
    bool meDowned = me->alive && me->downed;
    if (meDowned && !lastDowned) {
        // Just went down.
        heartbeatActive = true;
        heartbeatTimer  = 0.0f;
        duckMul         = DOWNED_DUCK;
    } else if (!meDowned && lastDowned) {
        // Came back up (revived or round ended).
        heartbeatActive = false;
        duckMul         = 1.0f;
    }
    lastDowned = meDowned;

    if (heartbeatActive) {
        heartbeatTimer -= dt;
        if (heartbeatTimer <= 0.0f) {
            // Bleed progress 0..1 (0 = just downed, 1 = nearly dead).
            float bleedProg = 0.0f;
            if (me->bleedTimer > 0.0f) {
                bleedProg = 1.0f - (me->bleedTimer / BLEED_TIME);
                if (bleedProg < 0.0f) bleedProg = 0.0f;
                if (bleedProg > 1.0f) bleedProg = 1.0f;
            }
            float interval = HB_INTERVAL_SLOW + (HB_INTERVAL_FAST - HB_INTERVAL_SLOW) * bleedProg;
            // Heartbeat is NOT ducked — it needs to cut through.
            if (sfx_heartbeat.loaded) {
                Sound s = sfx_heartbeat.aliases[sfx_heartbeat.next];
                sfx_heartbeat.next = (sfx_heartbeat.next + 1) % MAX_ALIASES;
                SetSoundVolume(s, 0.9f * audioMasterVol);
                SetSoundPitch(s, 1.0f);
                PlaySound(s);
            }
            heartbeatTimer = interval;
        }
    }

    // ---- shooting ---------------------------------------------------------
    if (me->shotsFired > lastShotsFired) {
        WeaponSlot *cur = &me->inventory[me->currentSlot];
        SoundSlot *slot = &sfx_shot;
        float vol = 0.6f, pitch = 1.0f;
        // Bank + volume + pitch are data-driven (`sfx` key in the .weapon file).
        if (cur->weaponIdx >= 0 && cur->weaponIdx < W_COUNT) {
            const WeaponDef *w = &WEAPONS[cur->weaponIdx];
            switch (w->sfxKind) {
                case WSFX_SHOTGUN: slot = &sfx_shotgun; break;
                case WSFX_RAYGUN:  slot = &sfx_raygun;  break;
                default:           slot = &sfx_shot;    break;
            }
            if (w->sfxVol   > 0.0f) vol   = w->sfxVol;
            if (w->sfxPitch > 0.0f) pitch = w->sfxPitch;
        }
        PlaySlot(slot, vol * duckMul, pitch);
    }

    // ---- hit / headshot / kill --------------------------------------------
    if (me->shotsHit > lastShotsHit) {
        bool head = (me->headshots > lastHeadshots);
        PlaySlot(head ? &sfx_head : &sfx_hit,
                 (head ? 0.8f : 0.45f) * duckMul, 1.0f);
    }
    if (me->kills > lastKills) {
        killGrace = 0.04f;
        PlaySlot(&sfx_kill, 0.45f * duckMul, 1.0f);
    }
    if (killGrace > 0) killGrace -= dt;

    // ---- player damage ----------------------------------------------------
    if (me->hp < lastHp && me->alive) {
        PlaySlot(&sfx_dmg, 0.9f * duckMul, 1.0f);
    }

    // ---- buy / perk / phase / mbox / powerup (unchanged logic) -----------
    if (me->points < lastPoints) {
        int delta = lastPoints - me->points;
        if (delta >= 50 && delta < 20000)
            PlaySlot(&sfx_buy, 0.6f * duckMul, 1.0f);
    }

    int mask = 0;
    for (int k = 0; k < PERK_COUNT; k++) if (me->hasPerk[k]) mask |= 1 << k;
    if (mask & ~lastPerks) PlaySlot(&sfx_perk, 0.8f * duckMul, 1.0f);
    lastPerks = mask;

    if (gamePhase != lastPhase) {
        if (gamePhase == GS_ROUND_BREAK)
            PlaySlot(&sfx_roundEnd, 0.8f * duckMul, 1.0f);
        else if (gamePhase == GS_PLAY && lastPhase == GS_ROUND_BREAK)
            PlaySlot(&sfx_roundStart, 0.8f * duckMul, 1.0f);
        lastPhase = gamePhase;
    }

    if (mbox.state != lastMBoxState) {
        if (mbox.state == MBOX_ROLLING)
            PlaySlot(&sfx_mboxRoll, 0.7f * duckMul, 1.0f);
        else if (mbox.state == MBOX_WAITING)
            PlaySlot(&sfx_mboxStop, 0.8f * duckMul, 1.0f);
        lastMBoxState = mbox.state;
    }

    if (doublePointsTimer > lastDoublePoints + 5.0f ||
        instaKillTimer    > lastInstakill    + 5.0f) {
        PlaySlot(&sfx_powerup, 0.8f * duckMul, 1.0f);
    }
    lastDoublePoints = doublePointsTimer;
    lastInstakill    = instaKillTimer;

    // ---- jump foley -------------------------------------------------------
    if (lastOnGround && !me->onGround && me->alive) {
        PlaySlot(&sfx_jump, 0.4f * duckMul, 1.0f);
    }
    lastOnGround = me->onGround;

    // ==== NEW FEATURES ====================================================

    // ---- reload two-stage (local player) ---------------------------------
    // The existing code plays the old combined sfx_reload. We now replace that
    // with a two-stage mag-out / mag-in pair. We keep sfx_reload for
    // backward-compat but don't play it for the local player here; the per-
    // player loop below handles the local player too.
    // (sfx_reload is still loaded and available; it's just not played below.)

    // ---- per-player footsteps + two-stage reload -------------------------
    // Camera position = local player eye position.
    Vector3 camPos = me->pos;  // close enough; y used only for world-to-cam
    float   camYaw = me->yaw;

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &players[i];
        if (!p->active || !p->alive || p->downed) {
            // Keep last pos valid so there's no phantom step when they re-activate.
            stepState[i].lastPos = p->pos;
            stepState[i].timer = 0.0f;
            continue;
        }

        bool isLocal = (i == localPlayerIdx);

        // ---- footsteps --------------------------------------------------
        StepState *ss = &stepState[i];
        ss->timer -= dt;

        float speedH;
        if (isLocal) {
            // Local player: use moveBlend + sprintBlend directly.
            // moveBlend drives how fast we step; sprintBlend determines cadence.
            speedH = p->moveBlend * BASE_MOVE_SPEED * (1.0f + 0.6f * p->sprintBlend);
        } else {
            // Remote players: derive from pos delta.
            float dx = p->pos.x - ss->lastPos.x;
            float dz = p->pos.z - ss->lastPos.z;
            speedH = sqrtf(dx * dx + dz * dz) / dt;
        }
        ss->lastPos = p->pos;

        // Detect crouch: pos.y is the eye height; crouching lowers it ~0.6m.
        bool isCrouching = (p->pos.y < (PLAYER_EYE - 0.3f));
        bool isAirborne  = !p->onGround;

        // Step cadence: faster when sprinting (~0.28s), walk ~0.42s,
        // crouch walk ~0.55s. No steps while airborne or standing still.
        float stepInterval = 0.0f;
        if (!isAirborne && speedH > 0.8f) {
            float sprint = (isLocal ? p->sprintBlend : 0.0f);
            float baseCadence = isCrouching ? 0.55f : (0.42f - 0.14f * sprint);
            stepInterval = baseCadence;
        }

        if (stepInterval > 0.0f && ss->timer <= 0.0f) {
            // Time to play a step.
            int v = ss->variantIdx;
            ss->variantIdx = (ss->variantIdx + 1) % STEP_VARIANTS;
            ss->timer = stepInterval;

            // Pitch variation: slight random spread ±5%.
            float pitch = 0.95f + (float)rand() / (float)RAND_MAX * 0.10f;

            if (isLocal) {
                float vol = isCrouching ? 0.28f : 0.42f;
                PlaySlot(&sfx_step[v], vol * duckMul, pitch);
            } else {
                // Positional for remote players.
                float vol, pan;
                if (PositionalAudio(camPos, camYaw, p->pos, 18.0f, &vol, &pan)) {
                    float finalVol = vol * 0.50f * duckMul;
                    PlaySlotPanned(&sfx_step[v], finalVol, pitch, pan);
                }
            }
        }

        // ---- reload two-stage -------------------------------------------
        ReloadState *rs = &reloadState[i];
        WeaponSlot *wslot = &p->inventory[p->currentSlot];
        float rt = wslot->reloadTimer;
        float reloadTime = 1.0f;  // fallback; normally driven by weapon def
        // Use the weapon definition's reloadTime (WEAPONS[] declared in weapons.h).
        if (wslot->weaponIdx >= 0 && wslot->weaponIdx < W_COUNT) {
            reloadTime = WEAPONS[wslot->weaponIdx].reloadTime;
            if (reloadTime < 0.1f) reloadTime = 0.1f;
        }

        // Detect reload start: timer rising edge (was 0, now > 0).
        if (rs->lastReloadTimer <= 0.0f && rt > 0.0f) {
            // Play mag-out immediately.
            if (isLocal) {
                PlaySlot(&sfx_magOut, 0.70f * duckMul, 1.0f);
            } else {
                float vol, pan;
                if (PositionalAudio(camPos, camYaw, p->pos, 12.0f, &vol, &pan)) {
                    PlaySlotPanned(&sfx_magOut, vol * 0.55f * duckMul, 1.0f, pan);
                }
            }
            // Schedule mag-in at 55% through the reload duration.
            rs->magInPending = true;
            rs->magInTimer   = reloadTime * 0.55f;
        }
        rs->lastReloadTimer = rt;

        // Count down mag-in timer.
        if (rs->magInPending) {
            rs->magInTimer -= dt;
            if (rs->magInTimer <= 0.0f) {
                rs->magInPending = false;
                // Only play if reload is still in progress.
                if (rt > 0.0f) {
                    if (isLocal) {
                        PlaySlot(&sfx_magIn, 0.75f * duckMul, 1.0f);
                    } else {
                        float vol, pan;
                        if (PositionalAudio(camPos, camYaw, p->pos, 12.0f, &vol, &pan)) {
                            PlaySlotPanned(&sfx_magIn, vol * 0.60f * duckMul, 1.0f, pan);
                        }
                    }
                }
            }
        }
    }

    // ---- zombie groans --------------------------------------------------
    // Pick a groan slot for a zombie type.
    // Budget: max MAX_GROAN_PER_TICK groans per frame (avoids audio explosion
    // when a big horde all expire timers simultaneously).
    int groanBudget = MAX_GROAN_PER_TICK;

    for (int i = 0; i < MAX_ENEMIES && groanBudget > 0; i++) {
        Enemy *en = &enemies[i];
        if (!en->alive) {
            // Reset state when enemy slot is freed so stale data doesn't
            // trigger sounds for new enemies that occupy the slot.
            enemyVox[i].initialized = false;
            enemyVox[i].lastInMeleeRange = false;
            continue;
        }

        EnemyVoxState *evs = &enemyVox[i];
        if (!evs->initialized) {
            // Stagger new enemy timers by slot index for natural spread.
            evs->timer = 1.5f + (float)(i % 9) * 0.40f;
            evs->initialized = true;
            evs->lastInMeleeRange = false;
        }

        // Compute distance from enemy to local player camera.
        float dx = en->pos.x - camPos.x;
        float dz = en->pos.z - camPos.z;
        float dist = sqrtf(dx * dx + dz * dz);

        // ---- melee-range snarl ------------------------------------------
        bool inMelee = (dist < MELEE_SNARL_DIST);
        if (inMelee && !evs->lastInMeleeRange) {
            // Just entered melee range: play attack snarl.
            float vol, pan;
            if (PositionalAudio(camPos, camYaw, en->pos, GROAN_MAX_DIST, &vol, &pan)) {
                float finalVol = (vol * 0.80f + 0.20f) * duckMul;  // min floor so close ones are loud
                PlaySlotPanned(&sfx_snarl, finalVol, 1.0f, pan);
            }
        }
        evs->lastInMeleeRange = inMelee;

        // ---- periodic vocalization --------------------------------------
        evs->timer -= dt;
        if (evs->timer > 0.0f) continue;

        // Timer expired. If out of earshot, just reset and skip.
        if (dist >= GROAN_MAX_DIST) {
            // Reset with a shorter wait — they might walk into range soon.
            evs->timer = 2.0f + (float)(rand() % 300) / 100.0f;
            continue;
        }

        // Compute positional.
        float vol, pan;
        if (!PositionalAudio(camPos, camYaw, en->pos, GROAN_MAX_DIST, &vol, &pan)) {
            evs->timer = 2.5f + (float)(rand() % 200) / 100.0f;
            continue;
        }

        // Select sound by zombie type.
        SoundSlot *voxSlot = &sfx_groan;
        float voxPitch = 1.0f;
        float voxVol   = vol * 0.65f;
        float nextTimerLo = 3.0f, nextTimerHi = 6.0f;

        switch (en->type) {
            case ZT_NORMAL:
                voxSlot = &sfx_groan;
                voxPitch = 0.90f + (float)(rand() % 200) / 1000.0f; // 0.90–1.10
                nextTimerLo = 3.5f; nextTimerHi = 7.0f;
                break;
            case ZT_RUNNER:
                voxSlot = &sfx_growl;
                voxPitch = 1.05f + (float)(rand() % 200) / 1000.0f; // 1.05–1.25 (faster)
                nextTimerLo = 2.0f; nextTimerHi = 5.0f;
                break;
            case ZT_CRAWLER:
                voxSlot = &sfx_hiss;
                voxPitch = 1.10f + (float)(rand() % 300) / 1000.0f; // higher sibilant
                nextTimerLo = 2.5f; nextTimerHi = 5.5f;
                break;
            case ZT_BOSS:
                voxSlot = &sfx_roar;
                voxPitch = 0.80f + (float)(rand() % 150) / 1000.0f; // deep, 0.80–0.95
                voxVol   = vol * 0.90f;  // bosses are louder
                nextTimerLo = 4.0f; nextTimerHi = 8.0f;
                break;
            default:
                break;
        }

        PlaySlotPanned(voxSlot, voxVol * duckMul, voxPitch, pan);
        groanBudget--;

        // Randomise next timer.
        float range = nextTimerHi - nextTimerLo;
        evs->timer = nextTimerLo + (float)(rand() % (int)(range * 100 + 1)) / 100.0f;
    }

    // ---- update tail state -----------------------------------------------
    lastShotsFired  = me->shotsFired;
    lastShotsHit    = me->shotsHit;
    lastHeadshots   = me->headshots;
    lastKills       = me->kills;
    lastHp          = me->hp;
    lastPoints      = me->points;
}
