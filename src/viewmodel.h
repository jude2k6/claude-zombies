#ifndef SHOOTER_VIEWMODEL_H
#define SHOOTER_VIEWMODEL_H

#include "types.h"
#include "raylib.h"

// Load / unload the shared first-person arms model (arms_vm.glb).
// Call once after Assets_Load / before CloseWindow.
void Viewmodel_LoadArms(void);
void Viewmodel_UnloadArms(void);

// Draw the first-person viewmodel for the local player.
// Must be called inside an active BeginMode3D scope.
void Viewmodel_DrawFirstPerson(Camera camera);

#endif
