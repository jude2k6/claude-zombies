#ifndef SHOOTER_RENDER_H
#define SHOOTER_RENDER_H

#include "types.h"

extern float muzzleFlashLocal;     // ticked by main loop

void Render_World3D(Camera camera);
void Render_WorldLabels(Camera camera, int sw, int sh, Player *me);

#endif
