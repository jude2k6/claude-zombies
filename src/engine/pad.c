#include "pad.h"
#include <math.h>

float padLookYawRate    = 3.4f;
float padLookPitchRate  = 2.6f;
float padStickDeadzone  = 0.18f;
float padTriggerThresh  = 0.10f;

bool Pad_Connected(void) {
    return IsGamepadAvailable(PAD_ID);
}

static float Deadzone(float v) {
    float dz = padStickDeadzone;
    float a = fabsf(v);
    if (a < dz) return 0.0f;
    float scaled = (a - dz) / (1.0f - dz);
    // Mild aim-curve so small deflections are gentler.
    scaled = scaled * scaled * (3.0f - 2.0f * scaled);
    return (v < 0) ? -scaled : scaled;
}

float Pad_StickX(int stick) {
    if (!IsGamepadAvailable(PAD_ID)) return 0.0f;
    int axis = (stick == 0) ? GAMEPAD_AXIS_LEFT_X : GAMEPAD_AXIS_RIGHT_X;
    return Deadzone(GetGamepadAxisMovement(PAD_ID, axis));
}

float Pad_StickY(int stick) {
    if (!IsGamepadAvailable(PAD_ID)) return 0.0f;
    int axis = (stick == 0) ? GAMEPAD_AXIS_LEFT_Y : GAMEPAD_AXIS_RIGHT_Y;
    return Deadzone(GetGamepadAxisMovement(PAD_ID, axis));
}

bool Pad_Down(int btn) {
    if (!IsGamepadAvailable(PAD_ID)) return false;
    return IsGamepadButtonDown(PAD_ID, btn);
}

bool Pad_Pressed(int btn) {
    if (!IsGamepadAvailable(PAD_ID)) return false;
    return IsGamepadButtonPressed(PAD_ID, btn);
}

bool Pad_TriggerL(void) {
    if (!IsGamepadAvailable(PAD_ID)) return false;
    return GetGamepadAxisMovement(PAD_ID, GAMEPAD_AXIS_LEFT_TRIGGER) > padTriggerThresh;
}

bool Pad_TriggerR(void) {
    if (!IsGamepadAvailable(PAD_ID)) return false;
    return GetGamepadAxisMovement(PAD_ID, GAMEPAD_AXIS_RIGHT_TRIGGER) > padTriggerThresh;
}
