#ifndef SHOOTER_FX_H
#define SHOOTER_FX_H

#include "raylib.h"

// Screen-shake amplitude (positional offset, world units) and trauma (decays
// each frame). Reads are clamped/zero when no shake is active.
extern float fxShakeTrauma;     // 0..1
extern float fxFlashAmount;     // 0..1, white screen flash (e.g. nuke)

// Tick decays of trauma/flash. Call once per frame in the main loop.
void Fx_Tick(float dt);

// Add a shake impulse. magnitude is roughly "trauma" added (0.1 small, 0.5 big).
// Optional rumble: matching low/high motor + duration on gamepad 0.
void Fx_Punch(float trauma);
void Fx_Rumble(float low, float high, float seconds);
void Fx_PunchAndRumble(float trauma, float low, float high, float seconds);

// Compute the camera offset for this frame. Returns a small Vec3 to add to
// the camera position; also produces a yaw/pitch jitter via out params.
Vector3 Fx_CameraOffset(float *yawJitter, float *pitchJitter);

#endif
