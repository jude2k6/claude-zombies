#ifndef SHOOTER_AUDIO_H
#define SHOOTER_AUDIO_H

#include "types.h"

// Master gain (0..1). Tweakable; tied to a future settings slider.
extern float audioMasterVol;

void Audio_Init(void);
void Audio_Shutdown(void);

// Diff-driven hook: call once per frame with the local player. Emits sounds
// when relevant snapshot-state values change (shots fired, hit count, hp,
// round phase, mystery-box state, power-up timers).
void Audio_Tick(Player *me);

// Direct hooks (host-side convenience for events not in the snapshot).
void Audio_Buy(void);

#endif
