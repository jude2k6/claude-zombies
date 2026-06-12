#include "audio.h"
#include "weapons.h"
#include "game.h"
#include "level.h"
#include "entities.h"
#include "player.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float audioMasterVol = 0.7f;

#define SAMPLE_RATE   22050
#define MAX_ALIASES   6   // simultaneous variants per slot (rapid fire reuse)

typedef struct {
    Sound base;
    Sound aliases[MAX_ALIASES];
    int   next;
    bool  loaded;
} SoundSlot;

// Sound bank. Each is a separate procedural waveform.
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

// ----- procedural wave generators ----------------------------------------

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

// ----- slot management ---------------------------------------------------

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

static void FreeSlot(SoundSlot *slot) {
    if (!slot->loaded) return;
    for (int i = 0; i < MAX_ALIASES; i++) UnloadSoundAlias(slot->aliases[i]);
    UnloadSound(slot->base);
    slot->loaded = false;
}

// ----- public API --------------------------------------------------------

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
    CloseAudioDevice();
}

void Audio_Buy(void)  { PlaySlot(&sfx_buy, 0.7f, 1.0f); }

void Audio_Tick(Player *me) {
    // Per-local-player diff state. Static so it lives across frames.
    static int       lastShotsFired = -1, lastShotsHit = -1, lastHeadshots = -1, lastKills = -1;
    static int       lastHp = -1, lastPoints = -1, lastPerks = -1;
    static float     lastReloadTimer = 0.0f;
    static GamePhase lastPhase = GS_PRE_GAME;
    static int       lastMBoxState = -1;
    static float     lastDoublePoints = 0.0f, lastInstakill = 0.0f;
    static bool      lastOnGround = true;
    static float     killGrace = 0.0f;

    if (lastShotsFired < 0) {
        lastShotsFired = me->shotsFired; lastShotsHit = me->shotsHit;
        lastHeadshots = me->headshots;   lastKills = me->kills;
        lastHp = me->hp;                 lastPoints = me->points;
        lastReloadTimer = me->inventory[me->currentSlot].reloadTimer;
        lastPhase = gamePhase;           lastMBoxState = mbox.state;
        lastDoublePoints = doublePointsTimer; lastInstakill = instaKillTimer;
        int mask = 0;
        for (int k = 0; k < PERK_COUNT; k++) if (me->hasPerk[k]) mask |= 1 << k;
        lastPerks = mask;
        lastOnGround = me->onGround;
        return;
    }

    if (me->shotsFired > lastShotsFired) {
        WeaponSlot *cur = &me->inventory[me->currentSlot];
        SoundSlot *slot = &sfx_shot;
        float vol = 0.6f, pitch = 1.0f;
        switch (cur->weaponIdx) {
            case W_PISTOL:  slot = &sfx_shot;    vol = 0.55f; pitch = 1.10f; break;
            case W_SMG:     slot = &sfx_shot;    vol = 0.55f; pitch = 1.25f; break;
            case W_SHOTGUN: slot = &sfx_shotgun; vol = 0.85f; pitch = 0.95f; break;
            case W_RIFLE:   slot = &sfx_shot;    vol = 0.75f; pitch = 0.85f; break;
            case W_RAYGUN:  slot = &sfx_raygun;  vol = 0.75f; pitch = 1.00f; break;
        }
        PlaySlot(slot, vol, pitch);
    }

    if (me->shotsHit > lastShotsHit) {
        bool head = (me->headshots > lastHeadshots);
        PlaySlot(head ? &sfx_head : &sfx_hit, head ? 0.8f : 0.45f, 1.0f);
    }
    if (me->kills > lastKills) {
        killGrace = 0.04f;  // small window — covers shotgun/bullet kill stacking
        PlaySlot(&sfx_kill, 0.45f, 1.0f);
    }
    if (killGrace > 0) killGrace -= GetFrameTime();

    if (me->hp < lastHp && me->alive) {
        PlaySlot(&sfx_dmg, 0.9f, 1.0f);
    }

    // Reload start: detect timer 0 → >0.
    float rt = me->inventory[me->currentSlot].reloadTimer;
    if (lastReloadTimer <= 0.0f && rt > 0.0f) {
        PlaySlot(&sfx_reload, 0.7f, 1.0f);
    }
    lastReloadTimer = rt;

    // Buy click on points decrease (purchases reduce points). Filter big drops
    // (probably PaP); play a single click either way.
    if (me->points < lastPoints) {
        int delta = lastPoints - me->points;
        if (delta >= 50 && delta < 20000) PlaySlot(&sfx_buy, 0.6f, 1.0f);
    }

    // New perk bought: perk-jingle.
    int mask = 0;
    for (int k = 0; k < PERK_COUNT; k++) if (me->hasPerk[k]) mask |= 1 << k;
    if (mask & ~lastPerks) PlaySlot(&sfx_perk, 0.8f, 1.0f);
    lastPerks = mask;

    // Phase transitions.
    if (gamePhase != lastPhase) {
        if (gamePhase == GS_ROUND_BREAK)      PlaySlot(&sfx_roundEnd, 0.8f, 1.0f);
        else if (gamePhase == GS_PLAY && lastPhase == GS_ROUND_BREAK)
                                              PlaySlot(&sfx_roundStart, 0.8f, 1.0f);
        lastPhase = gamePhase;
    }

    // Mystery box state changes.
    if (mbox.state != lastMBoxState) {
        if (mbox.state == MBOX_ROLLING)      PlaySlot(&sfx_mboxRoll, 0.7f, 1.0f);
        else if (mbox.state == MBOX_WAITING) PlaySlot(&sfx_mboxStop, 0.8f, 1.0f);
        lastMBoxState = mbox.state;
    }

    // Power-up applied (timer freshly set).
    if (doublePointsTimer > lastDoublePoints + 5.0f || instaKillTimer > lastInstakill + 5.0f) {
        PlaySlot(&sfx_powerup, 0.8f, 1.0f);
    }
    lastDoublePoints = doublePointsTimer;
    lastInstakill    = instaKillTimer;

    // Jump foley: transitioned from grounded to airborne.
    if (lastOnGround && !me->onGround && me->alive) {
        PlaySlot(&sfx_jump, 0.4f, 1.0f);
    }
    lastOnGround = me->onGround;

    lastShotsFired  = me->shotsFired;
    lastShotsHit    = me->shotsHit;
    lastHeadshots   = me->headshots;
    lastKills       = me->kills;
    lastHp          = me->hp;
    lastPoints      = me->points;
}
