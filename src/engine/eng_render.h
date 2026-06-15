#ifndef SHOOTER_ENG_RENDER_H
#define SHOOTER_ENG_RENDER_H

// ============================================================================
//  Engine render module — Phase 5 (pragmatic) of the engine/game split.
//
//  The engine owns the frame structure (postFX RT, world-shader lighting pass)
//  while the game still issues its own draw calls between the begin/end pairs.
//  No DrawItem submission — draw sites stay as direct Eng_Gfx*/DrawModel calls.
//
//  Three responsibilities:
//
//  1. Post-FX render target lifecycle + fullscreen composite.
//     Eng_RenderBeginPostFX() / Eng_RenderEndPostFX(EngPostFxParams) /
//     Eng_RenderUnloadPostFX().
//     Behaves identically to the old Render_BeginPostFX / Render_EndPostFX
//     when postfx shader is missing (graceful no-op fallback).
//
//  2. World-shader lighting bookend.
//     Eng_RenderSetLighting(EngLighting) — called once per frame by the game.
//     Eng_RenderBeginWorld() / Eng_RenderEndWorld() — push fog+sun+ambient
//     uniforms to both world + skinned shaders and bind/unbind via the gfx
//     facade.  Game still issues all world/prop/enemy draws between them.
//
//     Shader OWNERSHIP lives here; callers that need the raw Shader (e.g.
//     assets.c for ApplyWorldShader, anim.c for Anim_ApplyShader) use:
//       Eng_RenderWorldShader()        — static world OBJ shader
//       Eng_RenderWorldSkinnedShader() — skinned glTF shader
//       Eng_RenderWorldShaderLoaded()  — guard before calling either accessor
//       Eng_RenderWorldSkinnedShaderLoaded()
//
//  3. Depth-clear primitive — exposes the "flush batch + clear depth so the
//     viewmodel layer draws on top" trick as Eng_RenderClearDepth().
//
//  All shaders are loaded here via the engine content registry
//  (Eng_LoadShader); the caller does NOT need to manage shader lifetime.
//  Call Eng_RenderLoad() after InitWindow (before drawing), and
//  Eng_RenderUnload() before CloseWindow.
//
//  Cardinal rule (§2): this header includes ONLY raylib + engine headers.
// ============================================================================

#include "raylib.h"
#include <stdbool.h>

// ---- Post-FX parameters (per-frame, passed to Eng_RenderEndPostFX) ----------

typedef struct {
    float hitFlash;   // 0..1 — red-tinted edge damage flash
    float lowHp;      // 0..1 — desaturated heartbeat vignette
    float time;       // GetTime() — shader time for animated effects
} EngPostFxParams;

// ---- Lighting parameters (set once per frame via Eng_RenderSetLighting) ----

typedef struct {
    Vector3 sunDir;        // light travel direction (unit)
    Vector3 sunColor;      // RGB 0..1
    Vector3 ambientColor;  // RGB 0..1
    float   fogStart;      // world units
    float   fogEnd;        // world units
    Color   fogColor;      // RGBA 0..255 each
} EngLighting;

// ---- Lifecycle --------------------------------------------------------------

// Load the world / skinned / postfx shaders (probed via the engine content
// registry).  Call after InitWindow.  Missing shaders degrade gracefully.
void Eng_RenderLoad(void);

// Release the postFX render texture.  Call before CloseWindow.
// (Shaders are released by Eng_ContentFlush in Assets_Unload.)
void Eng_RenderUnloadPostFX(void);

// ---- World-shader pass ------------------------------------------------------

// Push the lighting state; call once per frame before Eng_RenderBeginWorld().
void Eng_RenderSetLighting(EngLighting l);

// Bind the world shader + push fog/sun/ambient uniforms to both world and
// skinned variants.  Must be called inside BeginMode3D.
void Eng_RenderBeginWorld(void);

// Unbind the world shader (restore raylib default).
void Eng_RenderEndWorld(void);

// Raw shader accessors — for callers that need to stamp models or push
// per-draw temporary uniforms (viewmodel flat-lighting trick).
Shader Eng_RenderWorldShader(void);
Shader Eng_RenderWorldSkinnedShader(void);
bool   Eng_RenderWorldShaderLoaded(void);
bool   Eng_RenderWorldSkinnedShaderLoaded(void);

// Per-uniform location accessors — for viewmodel's flat-lighting trick and
// tileVariation toggle (render.c).
int Eng_RenderWorldShader_sunColorLoc(void);
int Eng_RenderWorldShader_ambientColorLoc(void);
int Eng_RenderWorldShader_tileVariationLoc(void);
int Eng_RenderWorldSkinnedShader_sunColorLoc(void);
int Eng_RenderWorldSkinnedShader_ambientColorLoc(void);

// ---- Post-FX pass -----------------------------------------------------------

// Redirect rendering to the fullscreen RT.  No-op when postfx shader missing.
// Call between BeginDrawing and EndDrawing, before the world draw.
void Eng_RenderBeginPostFX(void);

// Stop rendering to the RT, push per-frame uniforms, and draw the fullscreen
// composite quad.  No-op when postfx shader missing.
void Eng_RenderEndPostFX(EngPostFxParams p);

// ---- Depth-clear primitive --------------------------------------------------

// Flush the current render batch and clear the depth buffer so subsequent
// draws (e.g. the viewmodel) always appear in front of the world geometry.
void Eng_RenderClearDepth(void);

#endif // SHOOTER_ENG_RENDER_H
