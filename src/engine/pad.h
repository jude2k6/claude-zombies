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

// ---------------------------------------------------------------------------
// Engine input action map (Phase 2 of the engine/game split).
//
// The engine owns the binding table; the game assigns Action ids and meaning.
// An Action is just an int chosen by the game — the engine assigns no names.
// The map supports up to ENG_MAX_ACTIONS distinct actions.
//
// Typical usage:
//   Eng_InputBind(ACT_FIRE, KEY_NULL, MOUSE_BUTTON_LEFT, PAD_RT_BTN);
//   if (Eng_InputDown(ACT_FIRE)) { ... }
// ---------------------------------------------------------------------------

#define ENG_MAX_ACTIONS 64

// Opaque integer id chosen by the game.  The engine does not enumerate names.
typedef int Action;

// Special pad-slot sentinels for trigger axes (analog, not digital buttons).
// Pass these as the padButton argument to route through Pad_TriggerL/R.
#define ENG_BIND_TRIG_L  -10
#define ENG_BIND_TRIG_R  -11

// Bind a raylib KEY_*, MOUSE_BUTTON_*, and/or GAMEPAD_BUTTON_* to an action.
// Pass -1 for "none" on any slot. (MOUSE_BUTTON_LEFT == 0 is a real button, so
// only -1 — not 0 — disables the mouse slot.)
// ENG_BIND_TRIG_L / ENG_BIND_TRIG_R may be passed as padButton to bind to the
// left / right trigger axis (threshold-based, via Pad_TriggerL/R).
void    Eng_InputBind(Action a, int key, int mouseButton, int padButton);

// True on the first frame the action becomes active (edge trigger).
bool    Eng_InputPressed(Action a);

// True for every frame the action is held.
bool    Eng_InputDown(Action a);

// Resolved movement axis: x = strafe, y = forward, each in [-1, 1].
// Uses WASD when no pad is connected; left stick otherwise.
Vector2 Eng_InputMoveAxis(void);

// Look delta this frame: x = yaw, y = pitch.
// Uses mouse delta (scaled by mouse sensitivity) or right stick (scaled by
// pad sensitivity), whichever source has non-zero input.
Vector2 Eng_InputLookDelta(void);

// Override the default look sensitivities.
void    Eng_InputSetLookSensitivity(float mouse, float pad);

// Advance trigger-axis edge state for actions bound to ENG_BIND_TRIG_L/R.
// Must be called once per frame after all Eng_InputPressed queries.
// (Mirrors Settings_TickTriggerEdges — replace it or call both; the engine
// version covers gameplay actions while settings covers the rebind-UI poll.)
void    Eng_InputTickTriggerEdges(void);

#endif
