#ifndef SHOOTER_PAD_H
#define SHOOTER_PAD_H

#include "raylib.h"
#include <stdbool.h>

// Use gamepad 0.
#define PAD_ID 0

// Xbox-style button aliases (raylib's enum maps to "right face" buttons).
#define PAD_A       GAMEPAD_BUTTON_RIGHT_FACE_DOWN
#define PAD_B       GAMEPAD_BUTTON_RIGHT_FACE_RIGHT
#define PAD_X       GAMEPAD_BUTTON_RIGHT_FACE_LEFT
#define PAD_Y       GAMEPAD_BUTTON_RIGHT_FACE_UP
#define PAD_DP_UP    GAMEPAD_BUTTON_LEFT_FACE_UP
#define PAD_DP_DOWN  GAMEPAD_BUTTON_LEFT_FACE_DOWN
#define PAD_DP_LEFT  GAMEPAD_BUTTON_LEFT_FACE_LEFT
#define PAD_DP_RIGHT GAMEPAD_BUTTON_LEFT_FACE_RIGHT
#define PAD_LB       GAMEPAD_BUTTON_LEFT_TRIGGER_1
#define PAD_RB       GAMEPAD_BUTTON_RIGHT_TRIGGER_1
#define PAD_LT_BTN   GAMEPAD_BUTTON_LEFT_TRIGGER_2   // press-style (off/on)
#define PAD_RT_BTN   GAMEPAD_BUTTON_RIGHT_TRIGGER_2
#define PAD_BACK     GAMEPAD_BUTTON_MIDDLE_LEFT
#define PAD_START    GAMEPAD_BUTTON_MIDDLE_RIGHT
#define PAD_L3       GAMEPAD_BUTTON_LEFT_THUMB
#define PAD_R3       GAMEPAD_BUTTON_RIGHT_THUMB

// Tunables.
extern float padLookYawRate;    // rad/sec at full deflection
extern float padLookPitchRate;
extern float padStickDeadzone;
extern float padTriggerThresh;  // axis value at which a trigger counts as held

bool  Pad_Connected(void);
float Pad_StickX(int stick);   // 0 = left, 1 = right. Deadzoned -1..1
float Pad_StickY(int stick);
bool  Pad_Down(int btn);
bool  Pad_Pressed(int btn);
bool  Pad_TriggerL(void);
bool  Pad_TriggerR(void);

#endif
