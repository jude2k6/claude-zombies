#ifndef SHOOTER_AUDIO_H
#define SHOOTER_AUDIO_H

// Engine-side mixer. Knows *how* to play sounds, never *when*: it has no
// knowledge of players, weapons, zombies, rounds or maps. The game decides
// what to play and fires events through this API (see audio_director.c).
#include "raylib.h"
#include <stdbool.h>

// Master gain (0..1). Tweakable; tied to a future settings slider.
extern float audioMasterVol;

// The mixer's procedurally-generated sound bank. These are *clip* ids, not
// game concepts — a different game hosting this mixer would pick its own
// subset. Footsteps come in three timbre variants (SFX_STEP0..2).
typedef enum {
    SFX_SHOT, SFX_SHOTGUN, SFX_RAYGUN,
    SFX_HIT, SFX_HEAD, SFX_KILL, SFX_DMG, SFX_RELOAD,
    SFX_ROUND_START, SFX_ROUND_END,
    SFX_BUY, SFX_POWERUP, SFX_MBOX_ROLL, SFX_MBOX_STOP,
    SFX_JUMP, SFX_PERK,
    SFX_STEP0, SFX_STEP1, SFX_STEP2,
    SFX_MAG_OUT, SFX_MAG_IN,
    SFX_GROAN, SFX_GROWL, SFX_HISS, SFX_ROAR, SFX_SNARL,
    SFX_HEARTBEAT,
    SFX_COUNT
} SfxId;

void Audio_Init(void);
void Audio_Shutdown(void);

// Pump streaming music. Call once per frame (the director does this).
void Audio_Update(void);

// ---- one-shots -----------------------------------------------------------
// vol is the pre-master gain (0..1); the mixer applies audioMasterVol.
void Audio_Play(SfxId id, float vol, float pitch);                 // 2D / non-spatial
void Audio_PlayPanned(SfxId id, float vol, float pitch, float pan); // explicit pan 0..1

// ---- 3D listener + positional resolve ------------------------------------
// Set the listener (local player camera) once per frame before positional
// plays. Audio_Positional rolls off volume and computes stereo pan for a
// world-space source; returns false when the source is inaudible.
void Audio_SetListener(Vector3 pos, float yaw);
bool Audio_Positional(Vector3 srcPos, float maxDist, float *outVol, float *outPan);

// ---- music ---------------------------------------------------------------
// Stream data/audio/<trackName>.ogg, replacing any current track. A missing
// file is a silent no-op. Audio_StopMusic unloads the current stream.
void Audio_PlayMusicTrack(const char *trackName);
void Audio_StopMusic(void);

#endif
