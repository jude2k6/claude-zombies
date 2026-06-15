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

// ---------------------------------------------------------------------------
// Engine input action map — implementation
// ---------------------------------------------------------------------------

// Sane defaults: mouse sensitivity in pixels-to-radians, pad in radians/sec.
#define ENG_DEFAULT_MOUSE_SENS  0.0015f
#define ENG_DEFAULT_PAD_SENS    1.8f

typedef struct {
    int key;          // raylib KEY_* (0/KEY_NULL = none)
    int mouseButton;  // raylib MOUSE_BUTTON_* (-1 = none; note LEFT == 0)
    int padButton;    // GAMEPAD_BUTTON_* (0/UNKNOWN = none), or ENG_BIND_TRIG_L/R
} ActionBind;

// Default every slot to fully-unbound: mouseButton must start at -1 because
// MOUSE_BUTTON_LEFT == 0 is a real button (zero-init would bind every action
// to left-click). key/padButton use 0 as their natural "none" sentinel.
static ActionBind s_binds[ENG_MAX_ACTIONS];
static bool       s_bindsInit = false;
static float      s_mouseSens = ENG_DEFAULT_MOUSE_SENS;
static float      s_padSens   = ENG_DEFAULT_PAD_SENS;

// Per-action trigger-edge state for ENG_BIND_TRIG_L/R press detection.
static bool s_trigPrev[ENG_MAX_ACTIONS];

static void EnsureBindsInit(void) {
    if (s_bindsInit) return;
    for (int i = 0; i < ENG_MAX_ACTIONS; i++) {
        s_binds[i].key = 0; s_binds[i].mouseButton = -1; s_binds[i].padButton = 0;
        s_trigPrev[i] = false;
    }
    s_bindsInit = true;
}

void Eng_InputBind(Action a, int key, int mouseButton, int padButton) {
    if (a < 0 || a >= ENG_MAX_ACTIONS) return;
    EnsureBindsInit();
    // key: -1 → 0 (KEY_NULL). mouseButton: keep -1 as "none" sentinel because
    // MOUSE_BUTTON_LEFT == 0 is a real button. padButton: ENG_BIND_TRIG_L/R are
    // stored verbatim (negative); other negative values → 0 (GAMEPAD_BUTTON_UNKNOWN).
    s_binds[a].key         = (key < 0) ? 0 : key;
    s_binds[a].mouseButton = mouseButton;
    if (padButton == ENG_BIND_TRIG_L || padButton == ENG_BIND_TRIG_R) {
        s_binds[a].padButton = padButton;
    } else {
        s_binds[a].padButton = (padButton < 0) ? 0 : padButton;
    }
}

bool Eng_InputPressed(Action a) {
    if (a < 0 || a >= ENG_MAX_ACTIONS) return false;
    EnsureBindsInit();
    int k = s_binds[a].key;
    // KEY_NULL == 0 and GAMEPAD_BUTTON_UNKNOWN == 0 are the unbound sentinels.
    if (k > 0 && IsKeyPressed(k)) return true;
    int m = s_binds[a].mouseButton;
    if (m >= 0 && IsMouseButtonPressed(m)) return true;
    int b = s_binds[a].padButton;
    // Trigger axes: rising-edge detection via per-action s_trigPrev[] state that
    // Eng_InputTickTriggerEdges() advances once at end-of-frame.
    if (b == ENG_BIND_TRIG_L) return Pad_TriggerL() && !s_trigPrev[a];
    if (b == ENG_BIND_TRIG_R) return Pad_TriggerR() && !s_trigPrev[a];
    if (b > 0 && IsGamepadAvailable(PAD_ID) && IsGamepadButtonPressed(PAD_ID, b)) return true;
    return false;
}

bool Eng_InputDown(Action a) {
    if (a < 0 || a >= ENG_MAX_ACTIONS) return false;
    EnsureBindsInit();
    int k = s_binds[a].key;
    if (k > 0 && IsKeyDown(k)) return true;
    int m = s_binds[a].mouseButton;
    if (m >= 0 && IsMouseButtonDown(m)) return true;
    int b = s_binds[a].padButton;
    if (b == ENG_BIND_TRIG_L) return Pad_TriggerL();
    if (b == ENG_BIND_TRIG_R) return Pad_TriggerR();
    if (b > 0 && IsGamepadAvailable(PAD_ID) && IsGamepadButtonDown(PAD_ID, b)) return true;
    return false;
}

void Eng_InputTickTriggerEdges(void) {
    // Advance per-action trigger-prev state so the next frame's Eng_InputPressed
    // can detect rising edges.  Call once per frame, after all Eng_InputPressed
    // queries, before the next frame begins.
    EnsureBindsInit();
    bool tl = Pad_TriggerL();
    bool tr = Pad_TriggerR();
    for (int i = 0; i < ENG_MAX_ACTIONS; i++) {
        int b = s_binds[i].padButton;
        if (b == ENG_BIND_TRIG_L) s_trigPrev[i] = tl;
        else if (b == ENG_BIND_TRIG_R) s_trigPrev[i] = tr;
    }
}

Vector2 Eng_InputMoveAxis(void) {
    Vector2 v = { 0.0f, 0.0f };
    if (Pad_Connected()) {
        // Left stick: x = strafe, y = forward (stick Y is inverted: up = -1).
        v.x = Pad_StickX(0);
        v.y = -Pad_StickY(0);
    } else {
        if (IsKeyDown(KEY_D)) v.x += 1.0f;
        if (IsKeyDown(KEY_A)) v.x -= 1.0f;
        if (IsKeyDown(KEY_W)) v.y += 1.0f;
        if (IsKeyDown(KEY_S)) v.y -= 1.0f;
        // Clamp diagonal to unit magnitude.
        float len = sqrtf(v.x * v.x + v.y * v.y);
        if (len > 1.0f) { v.x /= len; v.y /= len; }
    }
    return v;
}

Vector2 Eng_InputLookDelta(void) {
    Vector2 d = { 0.0f, 0.0f };
    if (Pad_Connected()) {
        // Right stick gives a rate; scale by a fixed dt proxy of GetFrameTime().
        float dt = GetFrameTime();
        d.x = Pad_StickX(1) * s_padSens * dt;
        d.y = Pad_StickY(1) * s_padSens * dt;
    } else {
        Vector2 md = GetMouseDelta();
        d.x = md.x * s_mouseSens;
        d.y = md.y * s_mouseSens;
    }
    return d;
}

void Eng_InputSetLookSensitivity(float mouse, float pad) {
    s_mouseSens = mouse;
    s_padSens   = pad;
}
