#include "gfx.h"
#include "rlgl.h"
#include "stats.h"   // draw-call tally

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

void Eng_GfxBeginMode3D(Camera3D cam)      { BeginMode3D(cam); }
void Eng_GfxEndMode3D(void)               { EndMode3D(); }
void Eng_GfxDrawModel(Model m, Vector3 pos, float scale, Color tint) { DrawModel(m, pos, scale, tint); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawModelEx(Model m, Vector3 pos, Vector3 axis, float angDeg, Vector3 scale, Color tint) { DrawModelEx(m, pos, axis, angDeg, scale, tint); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawCube(Vector3 pos, float w, float h, float l, Color c)   { DrawCube(pos, w, h, l, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawCubeV(Vector3 pos, Vector3 size, Color c)               { DrawCubeV(pos, size, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawCubeWires(Vector3 pos, float w, float h, float l, Color c) { DrawCubeWires(pos, w, h, l, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawCubeWiresV(Vector3 pos, Vector3 size, Color c)          { DrawCubeWiresV(pos, size, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawSphere(Vector3 center, float radius, Color c)           { DrawSphere(center, radius, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawPlane(Vector3 center, Vector2 size, Color c)            { DrawPlane(center, size, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawTriangle3D(Vector3 a, Vector3 b, Vector3 c, Color col)  { DrawTriangle3D(a, b, c, col); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawLine3D(Vector3 a, Vector3 b, Color c)                   { DrawLine3D(a, b, c); Eng_StatsAddDrawCalls(1); }
void Eng_GfxDrawGrid(int slices, float spacing)                         { DrawGrid(slices, spacing); Eng_StatsAddDrawCalls(1); }

// ---------------------------------------------------------------------------
//  Textured surface helpers
//  Game-agnostic: callers supply the tileSize (world units per texture repeat).
// ---------------------------------------------------------------------------

// Emit one quad (CCW from outside) spanning U×V texture repeats.
static inline void gfx_TexQuad(float ax, float ay, float az,
                                float bx, float by, float bz,
                                float cx, float cy, float cz,
                                float dx, float dy, float dz,
                                float u, float v) {
    rlTexCoord2f(0, v); rlVertex3f(ax, ay, az);
    rlTexCoord2f(u, v); rlVertex3f(bx, by, bz);
    rlTexCoord2f(u, 0); rlVertex3f(cx, cy, cz);
    rlTexCoord2f(0, 0); rlVertex3f(dx, dy, dz);
}

void Eng_DrawTexturedBoxV(Vector3 c, Vector3 s, Texture2D *tex,
                          float tileSize, Color tint, Color fallback) {
    if (!tex || tex->id == 0) {
        Eng_GfxDrawCubeV(c, s, fallback);
        Eng_GfxDrawCubeWiresV(c, s, BLACK);
        return;
    }
    float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    float ux = s.x / tileSize;
    float uy = s.y / tileSize;
    float uz = s.z / tileSize;

    Eng_GfxBeginQuads(tex->id);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);

    // +Z face
    rlNormal3f(0, 0, 1);
    gfx_TexQuad(c.x-hx, c.y-hy, c.z+hz,  c.x+hx, c.y-hy, c.z+hz,
                c.x+hx, c.y+hy, c.z+hz,  c.x-hx, c.y+hy, c.z+hz,  ux, uy);
    // -Z face
    rlNormal3f(0, 0, -1);
    gfx_TexQuad(c.x+hx, c.y-hy, c.z-hz,  c.x-hx, c.y-hy, c.z-hz,
                c.x-hx, c.y+hy, c.z-hz,  c.x+hx, c.y+hy, c.z-hz,  ux, uy);
    // +X face
    rlNormal3f(1, 0, 0);
    gfx_TexQuad(c.x+hx, c.y-hy, c.z+hz,  c.x+hx, c.y-hy, c.z-hz,
                c.x+hx, c.y+hy, c.z-hz,  c.x+hx, c.y+hy, c.z+hz,  uz, uy);
    // -X face
    rlNormal3f(-1, 0, 0);
    gfx_TexQuad(c.x-hx, c.y-hy, c.z-hz,  c.x-hx, c.y-hy, c.z+hz,
                c.x-hx, c.y+hy, c.z+hz,  c.x-hx, c.y+hy, c.z-hz,  uz, uy);
    // +Y face (top)
    rlNormal3f(0, 1, 0);
    gfx_TexQuad(c.x-hx, c.y+hy, c.z+hz,  c.x+hx, c.y+hy, c.z+hz,
                c.x+hx, c.y+hy, c.z-hz,  c.x-hx, c.y+hy, c.z-hz,  ux, uz);
    // -Y face (bottom)
    rlNormal3f(0, -1, 0);
    gfx_TexQuad(c.x-hx, c.y-hy, c.z-hz,  c.x+hx, c.y-hy, c.z-hz,
                c.x+hx, c.y-hy, c.z+hz,  c.x-hx, c.y-hy, c.z+hz,  ux, uz);

    Eng_GfxEndQuads();
    Eng_StatsAddDrawCalls(1);
}

void Eng_DrawTexturedFloorV(Vector3 centre, float sizeX, float sizeZ,
                            Texture2D *tex, float tileSize, Color tint, Color fallback) {
    if (!tex || tex->id == 0) {
        Eng_GfxDrawPlane(centre, (Vector2){sizeX, sizeZ}, fallback);
        return;
    }
    float hx = sizeX * 0.5f, hz = sizeZ * 0.5f;
    float u = sizeX / tileSize;
    float v = sizeZ / tileSize;

    Eng_GfxBeginQuads(tex->id);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0, 1, 0);
    gfx_TexQuad(centre.x-hx, centre.y, centre.z+hz,
                centre.x+hx, centre.y, centre.z+hz,
                centre.x+hx, centre.y, centre.z-hz,
                centre.x-hx, centre.y, centre.z-hz,  u, v);
    Eng_GfxEndQuads();
    Eng_StatsAddDrawCalls(1);
}
