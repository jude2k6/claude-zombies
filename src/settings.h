#ifndef SHOOTER_SETTINGS_H
#define SHOOTER_SETTINGS_H

#include <stdbool.h>

// ---------------------------------------------------------------------------
//  Controller bindings
// ---------------------------------------------------------------------------
//
// Every gameplay action that can be triggered on a gamepad routes through
// Bind_Pressed / Bind_Down with one of these action ids. The button each
// action maps to is configurable at runtime via the Settings -> Bindings UI
// and persisted to settings.cfg next to the binary.

typedef enum {
    BA_FIRE = 0,
    BA_ADS,
    BA_RELOAD,
    BA_INTERACT,
    BA_MELEE,
    BA_SWAP,
    BA_SLOT1,
    BA_SLOT2,
    BA_JUMP,
    BA_CROUCH,
    BA_SPRINT,
    BA_PAUSE,
    BA_SCORE,
    BA_NOCLIP,
    BA_COUNT
} BindAction;

// Sentinel button values. Real raylib button ids are >= 0 (small ints), so we
// use negative numbers for trigger axes and "unbound".
#define BIND_NONE     -1
#define BIND_TRIG_L   -10
#define BIND_TRIG_R   -11

// Current bindings, indexed by BindAction. Live, can be edited via UI.
extern int bindButton[BA_COUNT];

const char *Bind_ActionName(BindAction a);
const char *Bind_ButtonLabel(int btn);    // human-readable label for the bind
bool        Bind_Pressed(BindAction a);   // edge
bool        Bind_Down(BindAction a);      // sustained

// Tries to read any gamepad input this frame and returns the corresponding
// bind value (raylib button id, BIND_TRIG_L, or BIND_TRIG_R). Returns
// BIND_NONE if nothing was pressed. Used by the bindings UI to capture a
// rebind.
int  Bind_PollAny(void);

// ---------------------------------------------------------------------------
//  Persistence
// ---------------------------------------------------------------------------
//
// settings.cfg is searched in the working dir and a couple of relatives so it
// works whether you run from the project root or from build/. On save we
// write to whichever location we loaded from (or "settings.cfg" if there was
// no prior file).

void Settings_Load(void);
void Settings_Save(void);

// Call once per frame at end-of-input so BIND_TRIG_L/R edge detection works.
void Settings_TickTriggerEdges(void);

#endif
