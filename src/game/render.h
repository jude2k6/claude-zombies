#ifndef SHOOTER_RENDER_H
#define SHOOTER_RENDER_H

#include "types.h"

void Render_LoadZombieAnim(void);
void Render_UnloadZombieAnim(void);
void Render_LoadPlayerAnim(void);
void Render_UnloadPlayerAnim(void);

// Post-FX render target. When postfxShader loaded:
//   Render_BeginPostFX() — redirects rendering to the RT (call before
//                           Render_World3D and Render_WorldLabels).
//   Render_EndPostFX()   — stops rendering to the RT, draws the fullscreen
//                           quad with post-FX applied. Call after World3D +
//                           WorldLabels and before HUD/menus.
//   Render_UnloadPostFX() — release RT on shutdown.
// When postfxShader is NOT loaded these are no-ops (graceful fallback).
void Render_BeginPostFX(void);
void Render_EndPostFX(float hitFlash, float lowHp);
void Render_UnloadPostFX(void);

void Render_World3D(Camera camera);
void Render_WorldLabels(Camera camera, int sw, int sh, Player *me);

#endif
