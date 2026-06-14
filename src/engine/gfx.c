#include "gfx.h"
#include "rlgl.h"

void Eng_GfxFlushBatch(void)               { rlDrawRenderBatchActive(); }
void Eng_GfxDepthMask(bool on)             { if (on) rlEnableDepthMask();        else rlDisableDepthMask(); }
void Eng_GfxDepthTest(bool on)             { if (on) rlEnableDepthTest();        else rlDisableDepthTest(); }
void Eng_GfxBackfaceCull(bool on)          { if (on) rlEnableBackfaceCulling();  else rlDisableBackfaceCulling(); }
void Eng_GfxUseShader(Shader s)            { rlSetShader(s.id, s.locs); }
void Eng_GfxUseDefaultShader(void)         { rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault()); }
void Eng_GfxBeginQuads(unsigned int texId) { rlSetTexture(texId); rlBegin(RL_QUADS); }
void Eng_GfxColor(Color c)                 { rlColor4ub(c.r, c.g, c.b, c.a); }
void Eng_GfxNormal(float x, float y, float z)  { rlNormal3f(x, y, z); }
void Eng_GfxTexCoord(float u, float v)     { rlTexCoord2f(u, v); }
void Eng_GfxVertex(float x, float y, float z)  { rlVertex3f(x, y, z); }
void Eng_GfxEndQuads(void)                 { rlEnd(); rlSetTexture(0); }
