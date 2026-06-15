#include "eng_render.h"
#include "content.h"
#include "gfx.h"
#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Engine render module — Phase 5 pragmatic render seam.
//  Owns: postFX RT + composite, world-shader lighting pass, depth-clear prim.
//  Game still issues its own draw calls between the begin/end pairs.
// ============================================================================

// ---- World shader state -----------------------------------------------------

static Shader s_worldShader;
static bool   s_worldShaderLoaded = false;
static int    s_ws_fogColorLoc;
static int    s_ws_fogStartLoc;
static int    s_ws_fogEndLoc;
static int    s_ws_sunDirLoc;
static int    s_ws_sunColorLoc;
static int    s_ws_ambientColorLoc;
static int    s_ws_tileVariationLoc;

static Shader s_skinnedShader;
static bool   s_skinnedShaderLoaded = false;
static int    s_sk_fogColorLoc;
static int    s_sk_fogStartLoc;
static int    s_sk_fogEndLoc;
static int    s_sk_sunDirLoc;
static int    s_sk_sunColorLoc;
static int    s_sk_ambientColorLoc;

// ---- Post-FX state ----------------------------------------------------------

static Shader s_postfxShader;
static bool   s_postfxShaderLoaded = false;
static int    s_pf_resolutionLoc;
static int    s_pf_timeLoc;
static int    s_pf_hitFlashLoc;
static int    s_pf_lowHpLoc;

static RenderTexture2D s_postfxRT;
static int             s_postfxRT_w = 0;
static int             s_postfxRT_h = 0;
static bool            s_postfxRT_active = false;

// ---- Cached lighting state --------------------------------------------------

static EngLighting s_lighting = {
    .sunDir      = { 0.30f, -1.0f, -0.40f },
    .sunColor    = { 0.40f,  0.45f,  0.55f },
    .ambientColor= { 0.55f,  0.58f,  0.65f },
    .fogStart    = 10.0f,
    .fogEnd      = 55.0f,
    .fogColor    = { 10, 12, 22, 255 },
};

// ============================================================================
//  Lifecycle
// ============================================================================

void Eng_RenderLoad(void) {
    // World OBJ shader (fog + directional light + ambient).
    {
        EngShader h = Eng_LoadShader("world.vs", "world.fs");
        Shader *s = Eng_ShaderGet(h);
        s_worldShaderLoaded = (s != NULL);
        if (s_worldShaderLoaded) {
            s_worldShader          = *s;
            s_ws_fogColorLoc       = GetShaderLocation(s_worldShader, "fogColor");
            s_ws_fogStartLoc       = GetShaderLocation(s_worldShader, "fogStart");
            s_ws_fogEndLoc         = GetShaderLocation(s_worldShader, "fogEnd");
            s_ws_sunDirLoc         = GetShaderLocation(s_worldShader, "sunDir");
            s_ws_sunColorLoc       = GetShaderLocation(s_worldShader, "sunColor");
            s_ws_ambientColorLoc   = GetShaderLocation(s_worldShader, "ambientColor");
            s_ws_tileVariationLoc  = GetShaderLocation(s_worldShader, "tileVariation");
        } else {
            TraceLog(LOG_WARNING, "ENGRENDER: world.{vs,fs} not found, fog disabled");
        }
    }

    // Skinned variant (GPU skeletal, same lighting + fog).
    {
        EngShader h = Eng_LoadShader("world_skinned.vs", "world.fs");
        Shader *s = Eng_ShaderGet(h);
        s_skinnedShaderLoaded = (s != NULL);
        if (s_skinnedShaderLoaded) {
            s_skinnedShader        = *s;
            s_sk_fogColorLoc      = GetShaderLocation(s_skinnedShader, "fogColor");
            s_sk_fogStartLoc      = GetShaderLocation(s_skinnedShader, "fogStart");
            s_sk_fogEndLoc        = GetShaderLocation(s_skinnedShader, "fogEnd");
            s_sk_sunDirLoc        = GetShaderLocation(s_skinnedShader, "sunDir");
            s_sk_sunColorLoc      = GetShaderLocation(s_skinnedShader, "sunColor");
            s_sk_ambientColorLoc  = GetShaderLocation(s_skinnedShader, "ambientColor");
        } else {
            TraceLog(LOG_WARNING, "ENGRENDER: world_skinned.vs not found, animated models unlit");
        }
    }

    // Post-FX fullscreen shader (no VS — uses raylib's default passthrough).
    {
        EngShader h = Eng_LoadShader(NULL, "postfx.fs");
        Shader *s = Eng_ShaderGet(h);
        s_postfxShaderLoaded = (s != NULL);
        if (s_postfxShaderLoaded) {
            s_postfxShader       = *s;
            s_pf_resolutionLoc   = GetShaderLocation(s_postfxShader, "resolution");
            s_pf_timeLoc         = GetShaderLocation(s_postfxShader, "time");
            s_pf_hitFlashLoc     = GetShaderLocation(s_postfxShader, "hitFlash");
            s_pf_lowHpLoc        = GetShaderLocation(s_postfxShader, "lowHp");
        } else {
            TraceLog(LOG_WARNING, "ENGRENDER: postfx.fs not found, post-FX disabled");
        }
    }
}

void Eng_RenderUnloadPostFX(void) {
    if (s_postfxRT_w > 0) {
        UnloadRenderTexture(s_postfxRT);
        s_postfxRT_w = 0;
        s_postfxRT_h = 0;
    }
    s_worldShaderLoaded   = false;
    s_skinnedShaderLoaded = false;
    s_postfxShaderLoaded  = false;
}

// ============================================================================
//  World-shader lighting pass
// ============================================================================

void Eng_RenderSetLighting(EngLighting l) {
    s_lighting = l;
}

void Eng_RenderBeginWorld(void) {
    if (!s_worldShaderLoaded) return;

    float fc[4] = {
        s_lighting.fogColor.r / 255.0f,
        s_lighting.fogColor.g / 255.0f,
        s_lighting.fogColor.b / 255.0f,
        s_lighting.fogColor.a / 255.0f,
    };
    float sd[3] = { s_lighting.sunDir.x,       s_lighting.sunDir.y,       s_lighting.sunDir.z       };
    float sc[3] = { s_lighting.sunColor.x,     s_lighting.sunColor.y,     s_lighting.sunColor.z     };
    float ac[3] = { s_lighting.ambientColor.x, s_lighting.ambientColor.y, s_lighting.ambientColor.z };

    SetShaderValue(s_worldShader, s_ws_fogColorLoc,     fc,                   SHADER_UNIFORM_VEC4);
    SetShaderValue(s_worldShader, s_ws_fogStartLoc,     &s_lighting.fogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_worldShader, s_ws_fogEndLoc,       &s_lighting.fogEnd,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_worldShader, s_ws_sunDirLoc,       sd,                   SHADER_UNIFORM_VEC3);
    SetShaderValue(s_worldShader, s_ws_sunColorLoc,     sc,                   SHADER_UNIFORM_VEC3);
    SetShaderValue(s_worldShader, s_ws_ambientColorLoc, ac,                   SHADER_UNIFORM_VEC3);

    // Push the same uniforms to the skinned variant so animated models get
    // identical fog + lighting.
    if (s_skinnedShaderLoaded) {
        SetShaderValue(s_skinnedShader, s_sk_fogColorLoc,     fc,                   SHADER_UNIFORM_VEC4);
        SetShaderValue(s_skinnedShader, s_sk_fogStartLoc,     &s_lighting.fogStart, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_skinnedShader, s_sk_fogEndLoc,       &s_lighting.fogEnd,   SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_skinnedShader, s_sk_sunDirLoc,       sd,                   SHADER_UNIFORM_VEC3);
        SetShaderValue(s_skinnedShader, s_sk_sunColorLoc,     sc,                   SHADER_UNIFORM_VEC3);
        SetShaderValue(s_skinnedShader, s_sk_ambientColorLoc, ac,                   SHADER_UNIFORM_VEC3);
    }

    Eng_GfxUseShader(s_worldShader);
}

void Eng_RenderEndWorld(void) {
    if (!s_worldShaderLoaded) return;
    // CRITICAL: restore rlgl's default shader — passing NULL to rlSetShader
    // leaves currentShaderLocs NULL and EndDrawing's batch flush derefs it (SEGV).
    Eng_GfxUseDefaultShader();
}

// ---- Shader accessors -------------------------------------------------------

Shader Eng_RenderWorldShader(void)        { return s_worldShader;   }
Shader Eng_RenderWorldSkinnedShader(void) { return s_skinnedShader; }
bool   Eng_RenderWorldShaderLoaded(void)        { return s_worldShaderLoaded;   }
bool   Eng_RenderWorldSkinnedShaderLoaded(void) { return s_skinnedShaderLoaded; }

int Eng_RenderWorldShader_sunColorLoc(void)            { return s_ws_sunColorLoc;       }
int Eng_RenderWorldShader_ambientColorLoc(void)        { return s_ws_ambientColorLoc;   }
int Eng_RenderWorldShader_tileVariationLoc(void)       { return s_ws_tileVariationLoc;  }
int Eng_RenderWorldSkinnedShader_sunColorLoc(void)     { return s_sk_sunColorLoc;       }
int Eng_RenderWorldSkinnedShader_ambientColorLoc(void) { return s_sk_ambientColorLoc;   }

// ============================================================================
//  Post-FX pass
// ============================================================================

static void PostFX_EnsureRT(int w, int h) {
    if (s_postfxRT_w == w && s_postfxRT_h == h) return;
    if (s_postfxRT_w > 0) UnloadRenderTexture(s_postfxRT);
    s_postfxRT   = LoadRenderTexture(w, h);
    s_postfxRT_w = w;
    s_postfxRT_h = h;
}

void Eng_RenderBeginPostFX(void) {
    if (!s_postfxShaderLoaded) return;
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    PostFX_EnsureRT(w, h);
    BeginTextureMode(s_postfxRT);
    s_postfxRT_active = true;
}

void Eng_RenderEndPostFX(EngPostFxParams p) {
    if (!s_postfxShaderLoaded) return;
    if (s_postfxRT_active) {
        EndTextureMode();
        s_postfxRT_active = false;
    }
    int w = s_postfxRT_w;
    int h = s_postfxRT_h;

    float res[2] = { (float)w, (float)h };
    SetShaderValue(s_postfxShader, s_pf_resolutionLoc, res,         SHADER_UNIFORM_VEC2);
    SetShaderValue(s_postfxShader, s_pf_timeLoc,       &p.time,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_postfxShader, s_pf_hitFlashLoc,   &p.hitFlash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_postfxShader, s_pf_lowHpLoc,      &p.lowHp,    SHADER_UNIFORM_FLOAT);

    // Fullscreen quad: negative height un-flips the RT texture (raylib RTs flip V).
    BeginShaderMode(s_postfxShader);
    DrawTextureRec(s_postfxRT.texture,
                   (Rectangle){ 0, 0, (float)w, -(float)h },
                   (Vector2){ 0, 0 },
                   WHITE);
    EndShaderMode();
}

// ============================================================================
//  Depth-clear primitive
// ============================================================================

void Eng_RenderClearDepth(void) {
    // Flush any pending rlgl batch draws so they land in the depth buffer
    // before we clear it, then wipe depth only — this lets a subsequent pass
    // (e.g. the viewmodel) always draw on top of the world geometry.
    Eng_GfxClearDepth();
}
