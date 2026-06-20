#ifndef SHOOTER_VIEWMODEL_H
#define SHOOTER_VIEWMODEL_H

#include "types.h"
#include "raylib.h"

// Load / unload the shared first-person arms model (arms_vm.glb).
// Call once after Assets_Load / before CloseWindow.
void Viewmodel_LoadArms(void);
void Viewmodel_UnloadArms(void);

// Load / unload per-weapon combined viewmodel rigs.
// Each weapon may have data/weapons/<id>/<id>_vm.glb (arms + gun + mechanism
// in one rigged glTF). Missing files are silently skipped — that weapon falls
// through to the shared-arms path or gun-only OBJ. Call after Assets_Load so
// worldSkinnedShader is available.
void Viewmodel_LoadCombinedRigs(void);
void Viewmodel_UnloadCombinedRigs(void);

// Draw the first-person viewmodel for the local player.
// Must be called inside an active BeginMode3D scope.
void Viewmodel_DrawFirstPerson(Camera camera);

// Set the walk-bob phase for this frame (from the camera-bob computation in the
// game's per-frame step), so the first-person gun bob stays phase-locked to the
// camera bob instead of free-running on its own oscillator.
void Viewmodel_SetBobPhase(float phase);

// Grip-tuning aid: when true, the arms path draws a red sphere + axis ticks
// at the hand.R bone origin and a blue sphere at hand.L. Set by
// --screenshot-viewmodels so vm_grip_* keys can be dialled geometrically.
extern bool vmDebugMarkers;

#endif
