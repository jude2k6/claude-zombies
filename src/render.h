#ifndef SHOOTER_RENDER_H
#define SHOOTER_RENDER_H

#include "types.h"

void Render_LoadZombieAnim(void);
void Render_UnloadZombieAnim(void);
void Render_LoadPlayerAnim(void);
void Render_UnloadPlayerAnim(void);

void Render_World3D(Camera camera);
void Render_WorldLabels(Camera camera, int sw, int sh, Player *me);

#endif
