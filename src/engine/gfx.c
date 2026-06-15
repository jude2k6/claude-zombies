#include "gfx.h"
#include "rlgl.h"

// Expose the underlying GL clear function. We pull it through rlgl's guard
// so we get the same glad context that raylib initialised. rlgl.h pulls in
// glad.h (or the platform GL header) only when compiled as the implementation
// unit, so we declare glClear ourselves here — it's always available after
// InitWindow because raylib's rlglInit() loads all extensions via glad.
// Forward-declare to avoid including a second GL header that might conflict.
extern void glClear(unsigned int mask);
#define GL_DEPTH_BUFFER_BIT 0x00000100

void Eng_GfxFlushBatch(void)               { rlDrawRenderBatchActive(); }
void Eng_GfxClearDepth(void) {
    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT);
}
void Eng_GfxDepthMask(bool on)             { if (on) rlEnableDepthMask();        else rlDisableDepthMask(); }
void Eng_GfxDepthTest(bool on)             { if (on) rlEnableDepthTest();        else rlDisableDepthTest(); }
void Eng_GfxBackfaceCull(bool on)          { if (on) rlEnableBackfaceCulling();  else rlDisableBackfaceCulling(); }
void Eng_GfxUseShader(Shader s)            { rlSetShader(s.id, s.locs); }
void Eng_GfxUseDefaultShader(void)         { rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault()); }
unsigned int Eng_GfxDefaultShaderId(void)  { return rlGetShaderIdDefault(); }
void Eng_GfxBeginQuads(unsigned int texId) { rlSetTexture(texId); rlBegin(RL_QUADS); }
void Eng_GfxColor(Color c)                 { rlColor4ub(c.r, c.g, c.b, c.a); }
void Eng_GfxNormal(float x, float y, float z)  { rlNormal3f(x, y, z); }
void Eng_GfxTexCoord(float u, float v)     { rlTexCoord2f(u, v); }
void Eng_GfxVertex(float x, float y, float z)  { rlVertex3f(x, y, z); }
void Eng_GfxEndQuads(void)                 { rlEnd(); rlSetTexture(0); }
