#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

float audioMasterVol = 0.7f;

#define SAMPLE_RATE   22050
#define MAX_ALIASES   6   // simultaneous variants per slot (rapid fire reuse)

typedef struct {
    Sound base;
    Sound aliases[MAX_ALIASES];
    int   next;
    bool  loaded;
} SoundSlot;

// ---- sound bank ----------------------------------------------------------
// One slot per SfxId. Filled by Audio_Init, played by Audio_Play* via id.
static SoundSlot sfx[SFX_COUNT];

// Footsteps come in STEP_VARIANTS timbres (SFX_STEP0..2) to avoid a
// machine-gun effect when the same step fires repeatedly.
#define STEP_VARIANTS 3

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

// Listener (local player camera), set once per frame by Audio_SetListener.
static Vector3 listenerPos = { 0 };
static float   listenerYaw = 0.0f;

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

// ----- public one-shot + listener API -------------------------------------

void Audio_Play(SfxId id, float vol, float pitch) {
    if (id < 0 || id >= SFX_COUNT) return;
    PlaySlot(&sfx[id], vol, pitch);
}

void Audio_PlayPanned(SfxId id, float vol, float pitch, float pan) {
    if (id < 0 || id >= SFX_COUNT) return;
    PlaySlotPanned(&sfx[id], vol, pitch, pan);
}

void Audio_SetListener(Vector3 pos, float yaw) {
    listenerPos = pos;
    listenerYaw = yaw;
}

bool Audio_Positional(Vector3 srcPos, float maxDist, float *outVol, float *outPan) {
    return PositionalAudio(listenerPos, listenerYaw, srcPos, maxDist, outVol, outPan);
}

// ----- map music state ----------------------------------------------------

static Music  bgMusic;
static bool   bgMusicLoaded  = false;
static char   bgMusicName[64] = {0};  // currently-loaded music name (track key)

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

void Audio_StopMusic(void) {
    if (bgMusicLoaded) {
        StopMusicStream(bgMusic);
        UnloadMusicStream(bgMusic);
        bgMusicLoaded = false;
        bgMusicName[0] = 0;
    }
}

void Audio_PlayMusicTrack(const char *trackName) {
    // Unload any current track first.
    Audio_StopMusic();
    if (!trackName || trackName[0] == 0) return;
    if (!TryLoadMusic(trackName)) return;   // file not present — silent skip

    strncpy(bgMusicName, trackName, sizeof bgMusicName - 1);
    bgMusicName[sizeof bgMusicName - 1] = 0;
    SetMusicVolume(bgMusic, 0.25f);
    PlayMusicStream(bgMusic);
    bgMusicLoaded = true;
}

void Audio_Update(void) {
    if (bgMusicLoaded) UpdateMusicStream(bgMusic);
}

// ----- public API ---------------------------------------------------------

void Audio_Init(void) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    LoadSlot(&sfx[SFX_SHOT],        GenGunshot(0.13f, 2200.0f, 0.020f));
    LoadSlot(&sfx[SFX_SHOTGUN],     GenGunshot(0.20f, 1400.0f, 0.030f));
    LoadSlot(&sfx[SFX_RAYGUN],      GenGunshot(0.18f, 6000.0f, 0.040f));
    LoadSlot(&sfx[SFX_HIT],         GenHit());
    LoadSlot(&sfx[SFX_HEAD],        GenHeadshot());
    LoadSlot(&sfx[SFX_KILL],        GenKill());
    LoadSlot(&sfx[SFX_DMG],         GenDamage());
    LoadSlot(&sfx[SFX_RELOAD],      GenReload());
    LoadSlot(&sfx[SFX_ROUND_START], GenChime(440.0f, 554.0f, 659.0f, 0.18f));
    LoadSlot(&sfx[SFX_ROUND_END],   GenChime(659.0f, 554.0f, 440.0f, 0.20f));
    LoadSlot(&sfx[SFX_BUY],         GenBuy());
    LoadSlot(&sfx[SFX_POWERUP],     GenPowerup());
    LoadSlot(&sfx[SFX_MBOX_ROLL],   GenMBoxRoll());
    LoadSlot(&sfx[SFX_MBOX_STOP],   GenMBoxStop());
    LoadSlot(&sfx[SFX_JUMP],        GenJump());
    LoadSlot(&sfx[SFX_PERK],        GenPerk());

    // Footstep variants (SFX_STEP0..2).
    for (int v = 0; v < STEP_VARIANTS; v++) {
        LoadSlot(&sfx[SFX_STEP0 + v], GenFootstep(v));
    }

    // Reload two-stage.
    LoadSlot(&sfx[SFX_MAG_OUT], GenMagOut());
    LoadSlot(&sfx[SFX_MAG_IN],  GenMagIn());

    // Zombie vocalisation.
    LoadSlot(&sfx[SFX_GROAN], GenGroan());
    LoadSlot(&sfx[SFX_GROWL], GenGrowl());
    LoadSlot(&sfx[SFX_HISS],  GenHiss());
    LoadSlot(&sfx[SFX_ROAR],  GenRoar());
    LoadSlot(&sfx[SFX_SNARL], GenSnarl());

    // Heartbeat.
    LoadSlot(&sfx[SFX_HEARTBEAT], GenHeartbeat());

    bgMusicLoaded  = false;
    bgMusicName[0] = 0;
}

void Audio_Shutdown(void) {
    if (!IsAudioDeviceReady()) return;
    for (int i = 0; i < SFX_COUNT; i++) FreeSlot(&sfx[i]);
    Audio_StopMusic();
    CloseAudioDevice();
}
