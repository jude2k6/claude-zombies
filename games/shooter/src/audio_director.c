#include "audio_director.h"
#include "audio.h"
#include "weapons.h"
#include "game.h"
#include "level.h"
#include "entities.h"
#include "player.h"
#include "content.h"   // Eng_ContentDirs — root-stack directory enumeration
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// The director translates game state changes into mixer events. It owns all
// the diffing/timing state that used to live inside audio.c's Audio_Tick.

// Ducking multiplier applied to non-heartbeat sounds while local player is downed.
#define DOWNED_DUCK   0.28f

// Footstep timbre variants (mixer slots SFX_STEP0..2).
#define STEP_VARIANTS 3

// ----- footstep state -----------------------------------------------------
// Per-player footstep cadence. Index 0 = local; 1..NET_MAX_PLAYERS-1 = others.
#define MAX_STEP_PLAYERS  NET_MAX_PLAYERS

typedef struct {
    float   timer;        // time until next step is allowed
    Vector3 lastPos;      // previous frame position (for remote g_world.players)
    bool    lastPosValid;
    int     variantIdx;   // rotates through STEP_VARIANTS
} StepState;

static StepState stepState[MAX_STEP_PLAYERS];

// ----- reload two-stage state ---------------------------------------------
typedef struct {
    float lastReloadTimer;
    float magInTimer;     // countdown; when it reaches 0 we play mag-in
    bool  magInPending;
} ReloadState;

static ReloadState reloadState[MAX_STEP_PLAYERS];

// ----- zombie groan state -------------------------------------------------
typedef struct {
    float timer;            // seconds until next vocalization
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
static float duckMul = 1.0f;       // 1 = normal; DOWNED_DUCK while downed

// ----- map music resolution -----------------------------------------------
static char lastMapName[64] = {0};  // mapName snapshot for change detection

// Parse a map file to find the 'music' key inside the ATMOSPHERE block.
// Fills `outName` (max `nameMax` bytes including NUL). Returns true if found.
static bool ParseMapMusicName(const char *path, char *outName, int nameMax) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char raw[256];
    bool inAtmos = false;
    bool found   = false;
    while (!found && fgets(raw, sizeof raw, f)) {
        char *hash = strchr(raw, '#');
        if (hash) *hash = 0;
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
    // Enumerate all active map roots (game root first, then library, then data/
    // dev fallbacks) via the engine content resolver so game-over-library overlay
    // works transparently. This only runs on map change (once per load).
    char dirs[4][512];
    int nDirs = Eng_ContentDirs("maps", dirs, 4);
    const char *known[] = { "nacht", "factory", "default", "map" };
    for (int d = 0; d < nDirs; d++) {
        for (int k = 0; k < 4; k++) {
            // %.*s bounds the dir field so gcc can prove no truncation (the 2D
            // dirs[] array otherwise reads as a 2047-byte object to -Wformat).
            snprintf(outPath, (size_t)pathMax, "%.*s/%s.map", pathMax - 12, dirs[d], known[k]);
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
                    if (strcmp(assembled, g_world.mapName) == 0) matched = true;
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

// On map change: resolve the new map's music track and hand it to the mixer.
static void SwitchMapMusic(void) {
    char mapPath[256] = {0};
    char musicTrack[64] = {0};
    if (g_world.mapName[0] != 0 &&
        FindMapFile(mapPath, (int)sizeof mapPath) &&
        ParseMapMusicName(mapPath, musicTrack, (int)sizeof musicTrack)) {
        Audio_PlayMusicTrack(musicTrack);   // empty/missing track => silent
    } else {
        Audio_StopMusic();
    }
}

// ----- the director tick --------------------------------------------------

void AudioDirector_Tick(Player *me) {
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

    // Listener for all positional plays this frame (local player camera).
    Audio_SetListener(me->pos, me->yaw);

    // ---- first-frame seed -----------------------------------------------
    if (lastShotsFired < 0) {
        lastShotsFired = me->shotsFired; lastShotsHit = me->shotsHit;
        lastHeadshots = me->headshots;   lastKills = me->kills;
        lastHp = me->hp;                 lastPoints = me->points;
        lastPhase = gamePhase;           lastMBoxState = g_world.mbox.state;
        lastDoublePoints = g_world.doublePointsTimer; lastInstakill = g_world.instaKillTimer;
        int mask = 0;
        for (int k = 0; k < PERK_COUNT; k++) if (me->hasPerk[k]) mask |= 1 << k;
        lastPerks = mask;
        lastOnGround = me->onGround;

        // Seed footstep last positions for all g_world.players.
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            stepState[i].lastPos = g_world.players[i].pos;
            stepState[i].lastPosValid = g_world.players[i].active;
            stepState[i].timer = 0.0f;
            stepState[i].variantIdx = i % STEP_VARIANTS;
        }
        // Seed reload state for all g_world.players.
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            reloadState[i].lastReloadTimer = g_world.players[i].inventory[g_world.players[i].currentSlot].reloadTimer;
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
    if (strcmp(g_world.mapName, lastMapName) != 0) {
        strncpy(lastMapName, g_world.mapName, sizeof lastMapName - 1);
        lastMapName[sizeof lastMapName - 1] = 0;
        SwitchMapMusic();
    }
    Audio_Update();

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
            Audio_Play(SFX_HEARTBEAT, 0.9f, 1.0f);
            heartbeatTimer = interval;
        }
    }

    // ---- shooting ---------------------------------------------------------
    if (me->shotsFired > lastShotsFired) {
        WeaponSlot *cur = &me->inventory[me->currentSlot];
        SfxId id = SFX_SHOT;
        float vol = 0.6f, pitch = 1.0f;
        // Bank + volume + pitch are data-driven (`sfx` key in the .weapon file).
        if (cur->weaponIdx >= 0 && cur->weaponIdx < W_COUNT) {
            const WeaponDef *w = &WEAPONS[cur->weaponIdx];
            switch (w->sfxKind) {
                case WSFX_SHOTGUN: id = SFX_SHOTGUN; break;
                case WSFX_RAYGUN:  id = SFX_RAYGUN;  break;
                default:           id = SFX_SHOT;    break;
            }
            if (w->sfxVol   > 0.0f) vol   = w->sfxVol;
            if (w->sfxPitch > 0.0f) pitch = w->sfxPitch;
        }
        Audio_Play(id, vol * duckMul, pitch);
    }

    // ---- hit / headshot / kill --------------------------------------------
    if (me->shotsHit > lastShotsHit) {
        bool head = (me->headshots > lastHeadshots);
        Audio_Play(head ? SFX_HEAD : SFX_HIT,
                   (head ? 0.8f : 0.45f) * duckMul, 1.0f);
    }
    if (me->kills > lastKills) {
        killGrace = 0.04f;
        Audio_Play(SFX_KILL, 0.45f * duckMul, 1.0f);
    }
    if (killGrace > 0) killGrace -= dt;

    // ---- player damage ----------------------------------------------------
    if (me->hp < lastHp && me->alive) {
        Audio_Play(SFX_DMG, 0.9f * duckMul, 1.0f);
    }

    // ---- buy / perk / phase / mbox / powerup -----------------------------
    if (me->points < lastPoints) {
        int delta = lastPoints - me->points;
        if (delta >= 50 && delta < 20000)
            Audio_Play(SFX_BUY, 0.6f * duckMul, 1.0f);
    }

    int mask = 0;
    for (int k = 0; k < PERK_COUNT; k++) if (me->hasPerk[k]) mask |= 1 << k;
    if (mask & ~lastPerks) Audio_Play(SFX_PERK, 0.8f * duckMul, 1.0f);
    lastPerks = mask;

    if (gamePhase != lastPhase) {
        if (gamePhase == GS_ROUND_BREAK)
            Audio_Play(SFX_ROUND_END, 0.8f * duckMul, 1.0f);
        else if (gamePhase == GS_PLAY && lastPhase == GS_ROUND_BREAK)
            Audio_Play(SFX_ROUND_START, 0.8f * duckMul, 1.0f);
        lastPhase = gamePhase;
    }

    if (g_world.mbox.state != lastMBoxState) {
        if (g_world.mbox.state == MBOX_ROLLING)
            Audio_Play(SFX_MBOX_ROLL, 0.7f * duckMul, 1.0f);
        else if (g_world.mbox.state == MBOX_WAITING)
            Audio_Play(SFX_MBOX_STOP, 0.8f * duckMul, 1.0f);
        lastMBoxState = g_world.mbox.state;
    }

    if (g_world.doublePointsTimer > lastDoublePoints + 5.0f ||
        g_world.instaKillTimer    > lastInstakill    + 5.0f) {
        Audio_Play(SFX_POWERUP, 0.8f * duckMul, 1.0f);
    }
    lastDoublePoints = g_world.doublePointsTimer;
    lastInstakill    = g_world.instaKillTimer;

    // ---- jump foley -------------------------------------------------------
    if (lastOnGround && !me->onGround && me->alive) {
        Audio_Play(SFX_JUMP, 0.4f * duckMul, 1.0f);
    }
    lastOnGround = me->onGround;

    // ---- per-player footsteps + two-stage reload -------------------------
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &g_world.players[i];
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
            // Local player: moveBlend drives step speed; sprintBlend the cadence.
            speedH = p->moveBlend * BASE_MOVE_SPEED * (1.0f + 0.6f * p->sprintBlend);
        } else {
            // Remote g_world.players: derive from pos delta.
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
            int v = ss->variantIdx;
            ss->variantIdx = (ss->variantIdx + 1) % STEP_VARIANTS;
            ss->timer = stepInterval;

            // Pitch variation: slight random spread ±5%.
            float pitch = 0.95f + (float)rand() / (float)RAND_MAX * 0.10f;

            if (isLocal) {
                float vol = isCrouching ? 0.28f : 0.42f;
                Audio_Play(SFX_STEP0 + v, vol * duckMul, pitch);
            } else {
                // Positional for remote g_world.players.
                float vol, pan;
                if (Audio_Positional(p->pos, 18.0f, &vol, &pan)) {
                    float finalVol = vol * 0.50f * duckMul;
                    Audio_PlayPanned(SFX_STEP0 + v, finalVol, pitch, pan);
                }
            }
        }

        // ---- reload two-stage -------------------------------------------
        ReloadState *rs = &reloadState[i];
        WeaponSlot *wslot = &p->inventory[p->currentSlot];
        float rt = wslot->reloadTimer;
        float reloadTime = 1.0f;  // fallback; normally driven by weapon def
        if (wslot->weaponIdx >= 0 && wslot->weaponIdx < W_COUNT) {
            reloadTime = WEAPONS[wslot->weaponIdx].reloadTime;
            if (reloadTime < 0.1f) reloadTime = 0.1f;
        }

        // Detect reload start: timer rising edge (was 0, now > 0).
        if (rs->lastReloadTimer <= 0.0f && rt > 0.0f) {
            // Play mag-out immediately.
            if (isLocal) {
                Audio_Play(SFX_MAG_OUT, 0.70f * duckMul, 1.0f);
            } else {
                float vol, pan;
                if (Audio_Positional(p->pos, 12.0f, &vol, &pan)) {
                    Audio_PlayPanned(SFX_MAG_OUT, vol * 0.55f * duckMul, 1.0f, pan);
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
                        Audio_Play(SFX_MAG_IN, 0.75f * duckMul, 1.0f);
                    } else {
                        float vol, pan;
                        if (Audio_Positional(p->pos, 12.0f, &vol, &pan)) {
                            Audio_PlayPanned(SFX_MAG_IN, vol * 0.60f * duckMul, 1.0f, pan);
                        }
                    }
                }
            }
        }
    }

    // ---- zombie groans --------------------------------------------------
    // Budget: max MAX_GROAN_PER_TICK groans per frame (avoids an audio
    // explosion when a big horde all expire timers simultaneously).
    int groanBudget = MAX_GROAN_PER_TICK;

    for (int i = 0; i < MAX_ENEMIES && groanBudget > 0; i++) {
        Enemy *en = &enemies[i];
        if (!en->alive) {
            // Reset state when the slot is freed so stale data doesn't trigger
            // sounds for new enemies that occupy the slot.
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

        // Distance from enemy to the local player camera (horizontal).
        float dx = en->pos.x - me->pos.x;
        float dz = en->pos.z - me->pos.z;
        float dist = sqrtf(dx * dx + dz * dz);

        // ---- melee-range snarl ------------------------------------------
        bool inMelee = (dist < MELEE_SNARL_DIST);
        if (inMelee && !evs->lastInMeleeRange) {
            // Just entered melee range: play attack snarl.
            float vol, pan;
            if (Audio_Positional(en->pos, GROAN_MAX_DIST, &vol, &pan)) {
                float finalVol = (vol * 0.80f + 0.20f) * duckMul;  // floor so close ones are loud
                Audio_PlayPanned(SFX_SNARL, finalVol, 1.0f, pan);
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
        if (!Audio_Positional(en->pos, GROAN_MAX_DIST, &vol, &pan)) {
            evs->timer = 2.5f + (float)(rand() % 200) / 100.0f;
            continue;
        }

        // Select sound by zombie type.
        SfxId voxId   = SFX_GROAN;
        float voxPitch = 1.0f;
        float voxVol   = vol * 0.65f;
        float nextTimerLo = 3.0f, nextTimerHi = 6.0f;

        switch (en->type) {
            case ZT_NORMAL:
                voxId = SFX_GROAN;
                voxPitch = 0.90f + (float)(rand() % 200) / 1000.0f; // 0.90–1.10
                nextTimerLo = 3.5f; nextTimerHi = 7.0f;
                break;
            case ZT_RUNNER:
                voxId = SFX_GROWL;
                voxPitch = 1.05f + (float)(rand() % 200) / 1000.0f; // 1.05–1.25 (faster)
                nextTimerLo = 2.0f; nextTimerHi = 5.0f;
                break;
            case ZT_CRAWLER:
                voxId = SFX_HISS;
                voxPitch = 1.10f + (float)(rand() % 300) / 1000.0f; // higher sibilant
                nextTimerLo = 2.5f; nextTimerHi = 5.5f;
                break;
            case ZT_BOSS:
                voxId = SFX_ROAR;
                voxPitch = 0.80f + (float)(rand() % 150) / 1000.0f; // deep, 0.80–0.95
                voxVol   = vol * 0.90f;  // bosses are louder
                nextTimerLo = 4.0f; nextTimerHi = 8.0f;
                break;
            default:
                break;
        }

        Audio_PlayPanned(voxId, voxVol * duckMul, voxPitch, pan);
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
