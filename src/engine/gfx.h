#ifndef SHOOTER_GFX_H
#define SHOOTER_GFX_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Engine GL facade — thin wrappers over rlgl immediate-mode calls.
//
//  Consumers include this header only; rlgl.h is hidden in gfx.c.
//  The cardinal rule (§15): no file outside src/engine/ may include rlgl.h.
// ============================================================================

void Eng_GfxFlushBatch(void);               // rlDrawRenderBatchActive()
void Eng_GfxDepthMask(bool on);             // on -> rlEnableDepthMask(), else rlDisableDepthMask()
void Eng_GfxDepthTest(bool on);             // on -> rlEnableDepthTest(),  else rlDisableDepthTest()
void Eng_GfxBackfaceCull(bool on);          // on -> rlEnableBackfaceCulling(), else rlDisableBackfaceCulling()
void Eng_GfxUseShader(Shader s);            // rlSetShader(s.id, s.locs)
void Eng_GfxUseDefaultShader(void);         // rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault())
unsigned int Eng_GfxDefaultShaderId(void);  // rlGetShaderIdDefault() — for load-failure checks
void Eng_GfxBeginQuads(unsigned int texId); // rlSetTexture(texId); rlBegin(RL_QUADS);
void Eng_GfxColor(Color c);                 // rlColor4ub(c.r,c.g,c.b,c.a)
void Eng_GfxNormal(float x, float y, float z);  // rlNormal3f
void Eng_GfxTexCoord(float u, float v);     // rlTexCoord2f
void Eng_GfxVertex(float x, float y, float z);  // rlVertex3f
void Eng_GfxEndQuads(void);                 // rlEnd(); rlSetTexture(0);

#endif // SHOOTER_GFX_H
