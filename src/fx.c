#include "fx.h"
#include "pad.h"
#include <math.h>
#include <stdlib.h>

float fxShakeTrauma = 0.0f;
float fxFlashAmount = 0.0f;

// Small noise helper: deterministic-ish, fed by a slowly advancing seed so the
// shake feels continuous rather than per-frame snap.
static float fxNoiseT = 0.0f;
static float Noise1(float t) {
    // Hash via sin trick. Cheap and good enough for camera jitter.
    float s = sinf(t * 12.9898f) * 43758.5453f;
    return (s - floorf(s)) * 2.0f - 1.0f;
}

void Fx_Tick(float dt) {
    // Trauma decays linearly. 1.0 → 0 in ~0.8s.
    fxShakeTrauma -= 1.25f * dt;
    if (fxShakeTrauma < 0) fxShakeTrauma = 0;
    if (fxShakeTrauma > 1) fxShakeTrauma = 1;
    fxFlashAmount -= 2.5f * dt;
    if (fxFlashAmount < 0) fxFlashAmount = 0;
    fxNoiseT += dt * 28.0f;
}

void Fx_Punch(float trauma) {
    fxShakeTrauma += trauma;
    if (fxShakeTrauma > 1.0f) fxShakeTrauma = 1.0f;
}

void Fx_Rumble(float low, float high, float seconds) {
    if (!Pad_Connected()) return;
    if (low  < 0) low  = 0; if (low  > 1) low  = 1;
    if (high < 0) high = 0; if (high > 1) high = 1;
    SetGamepadVibration(PAD_ID, low, high, seconds);
}

void Fx_PunchAndRumble(float trauma, float low, float high, float seconds) {
    Fx_Punch(trauma);
    Fx_Rumble(low, high, seconds);
}

Vector3 Fx_CameraOffset(float *yawJitter, float *pitchJitter) {
    // Square the trauma so small shakes don't move the camera much. Big shakes
    // feel much heavier than the linear scale would suggest.
    float t = fxShakeTrauma * fxShakeTrauma;
    float maxOffset = 0.18f;
    float maxAngle  = 0.06f;
    Vector3 off = {
        Noise1(fxNoiseT)         * maxOffset * t,
        Noise1(fxNoiseT + 17.3f) * maxOffset * t * 0.6f,
        Noise1(fxNoiseT + 33.1f) * maxOffset * t,
    };
    if (yawJitter)   *yawJitter   = Noise1(fxNoiseT + 51.7f) * maxAngle * t;
    if (pitchJitter) *pitchJitter = Noise1(fxNoiseT + 71.2f) * maxAngle * t;
    return off;
}
