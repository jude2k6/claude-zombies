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
void Eng_GfxClearDepth(void);               // flush batch then glClear(GL_DEPTH_BUFFER_BIT)
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

void Eng_GfxBeginMode3D(Camera3D cam);                                                          // BeginMode3D
void Eng_GfxEndMode3D(void);                                                                    // EndMode3D
void Eng_GfxDrawModel(Model m, Vector3 pos, float scale, Color tint);                          // DrawModel
void Eng_GfxDrawModelEx(Model m, Vector3 pos, Vector3 axis, float angDeg, Vector3 scale, Color tint); // DrawModelEx
void Eng_GfxDrawCube(Vector3 pos, float w, float h, float l, Color c);                         // DrawCube
void Eng_GfxDrawCubeV(Vector3 pos, Vector3 size, Color c);                                     // DrawCubeV
void Eng_GfxDrawCubeWires(Vector3 pos, float w, float h, float l, Color c);                    // DrawCubeWires
void Eng_GfxDrawCubeWiresV(Vector3 pos, Vector3 size, Color c);                                // DrawCubeWiresV
void Eng_GfxDrawSphere(Vector3 center, float radius, Color c);                                  // DrawSphere
void Eng_GfxDrawPlane(Vector3 center, Vector2 size, Color c);                                   // DrawPlane
void Eng_GfxDrawTriangle3D(Vector3 a, Vector3 b, Vector3 c, Color col);                        // DrawTriangle3D
void Eng_GfxDrawLine3D(Vector3 a, Vector3 b, Color c);                                         // DrawLine3D
void Eng_GfxDrawGrid(int slices, float spacing);                                                // DrawGrid

#endif // SHOOTER_GFX_H
