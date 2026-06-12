#include "assets.h"
#include "weapons.h"     // weaponModels[] for Assets_ApplyWorldShader
#include "rlgl.h"
#include <stdio.h>
#include <string.h>

// ---- prop storage -------------------------------------------------------
Model propModels[PROP_COUNT];
bool  propModelLoaded[PROP_COUNT];

// Index into PropId. Designating ones with `NULL` skips the load.
// See data/models/ASSETS.md for the spec of every entry.
static const char *PROP_FILES[PROP_COUNT] = {
    [PROP_ZOMBIE]          = "zombie.obj",
    [PROP_MYSTERY_BOX]     = "mystery_box.obj",
    [PROP_BOARD]           = "board.obj",
    [PROP_SANDBAG_STACK]   = "sandbag_stack.obj",
    [PROP_DOOR]            = "door.obj",
    [PROP_DOOR_FRAME]      = "door_frame.obj",
    [PROP_OBSTACLE_CRATE]  = "obstacle_crate.obj",
    [PROP_OBSTACLE_BARREL] = "obstacle_barrel.obj",
    [PROP_PERK_JUG]        = "perk_juggernog.obj",
    [PROP_PERK_SPEED]      = "perk_speed_cola.obj",
    [PROP_PERK_DTAP]       = "perk_double_tap.obj",
    [PROP_PERK_STAMIN]     = "perk_staminup.obj",
    [PROP_PAP]             = "pap_machine.obj",
    [PROP_WALLBUY_PANEL]   = "wallbuy_panel.obj",
    [PROP_POWERUP_DROP]    = "powerup_drop.obj",
    [PROP_PLAYER_M]        = "player_m.obj",
};

// ---- texture storage ----------------------------------------------------
Texture2D textures[TEX_COUNT];
bool      textureLoaded[TEX_COUNT];

static const char *TEXTURE_FILES[TEX_COUNT] = {
    [TEX_FLOOR]    = "floor_concrete.png",
    [TEX_GROUND]   = "ground_dirt.png",
    [TEX_WALL_EXT] = "wall_brick.png",
    [TEX_WALL_INT] = "wall_plaster.png",
    [TEX_CEILING]  = "ceiling_wood.png",
};

// ---- shader storage -----------------------------------------------------
Shader worldShader;
bool   worldShaderLoaded;
int    worldShader_fogColorLoc;
int    worldShader_fogStartLoc;
int    worldShader_fogEndLoc;
int    worldShader_sunDirLoc;
int    worldShader_sunColorLoc;
int    worldShader_ambientColorLoc;
int    worldShader_tileVariationLoc;

Shader worldSkinnedShader;
bool   worldSkinnedShaderLoaded;
int    worldSkinnedShader_fogColorLoc;
int    worldSkinnedShader_fogStartLoc;
int    worldSkinnedShader_fogEndLoc;
int    worldSkinnedShader_sunDirLoc;
int    worldSkinnedShader_sunColorLoc;
int    worldSkinnedShader_ambientColorLoc;

Shader skyShader;
bool   skyShaderLoaded;
Model  skyModel;

Shader postfxShader;
bool   postfxShaderLoaded;
int    postfxShader_resolutionLoc;
int    postfxShader_timeLoc;
int    postfxShader_hitFlashLoc;
int    postfxShader_lowHpLoc;

// Defaults match the night-sky aesthetic and Nacht's interior scale.
// Tweak per-map by editing these globals before Render_World3D.
float fogStart = 10.0f;
float fogEnd   = 55.0f;
Color fogColor = (Color){ 10, 12, 22, 255 };

// Moonlight from the upper +X, into -Z. Cool blue-white tone, with the
// ambient lifted high enough that the diffuse texture actually reads on
// faces facing away from the moon — Poly Haven photo textures are
// authored in sRGB space and the sampler returns those bytes verbatim,
// so multiplying by a low ambient crushes them to near-black.
Vector3 sunDir       = { 0.30f, -1.0f, -0.40f };
Vector3 sunColor     = { 0.40f,  0.45f, 0.55f };
Vector3 ambientColor = { 0.55f,  0.58f, 0.65f };

// Try the same prefix list as models/textures.
static bool TryLoadShader(const char *prefix, const char *vsName,
                          const char *fsName, Shader *out) {
    char vsPath[512], fsPath[512];
    snprintf(vsPath, sizeof vsPath, "%s%s", prefix, vsName);
    snprintf(fsPath, sizeof fsPath, "%s%s", prefix, fsName);
    if (!FileExists(vsPath) || !FileExists(fsPath)) return false;
    *out = LoadShader(vsPath, fsPath);
    return out->id != 0 && out->id != rlGetShaderIdDefault();
}

// ---- common loader ------------------------------------------------------
static bool TryLoad(const char *prefix, const char *basename, Model *out) {
    char path[512];
    snprintf(path, sizeof path, "%s%s", prefix, basename);
    if (!FileExists(path)) return false;
    *out = LoadModel(path);
    return out->meshCount > 0;
}

void Assets_Load(void) {
    // Weapon viewmodels are loaded by Weapons_Load() (called separately
    // before this) from data/weapons/<name>/.

    // Generic props live under data/models/.
    const char *propPrefixes[] = {
        "data/models/",
        "../data/models/",
        "./data/models/",
    };
    for (int i = 0; i < PROP_COUNT; i++) {
        propModelLoaded[i] = false;
        const char *bn = PROP_FILES[i];
        if (!bn) continue;
        for (size_t p = 0; p < sizeof propPrefixes / sizeof propPrefixes[0]; p++) {
            if (TryLoad(propPrefixes[p], bn, &propModels[i])) {
                propModelLoaded[i] = true;
                fprintf(stderr, "model: loaded %s%s (meshes=%d)\n",
                        propPrefixes[p], bn, propModels[i].meshCount);
                break;
            }
        }
        if (!propModelLoaded[i]) {
            fprintf(stderr, "model: %s not found, using primitive fallback\n", bn);
        }
    }

    // Textures live under data/textures/.
    const char *texPrefixes[] = {
        "data/textures/",
        "../data/textures/",
        "./data/textures/",
    };
    for (int i = 0; i < TEX_COUNT; i++) {
        textureLoaded[i] = false;
        const char *bn = TEXTURE_FILES[i];
        if (!bn) continue;
        for (size_t p = 0; p < sizeof texPrefixes / sizeof texPrefixes[0]; p++) {
            char path[512];
            snprintf(path, sizeof path, "%s%s", texPrefixes[p], bn);
            if (!FileExists(path)) continue;
            textures[i] = LoadTexture(path);
            if (textures[i].id == 0) continue;
            // Tile seamlessly when UVs exceed 1.0.  Mipmaps stop distant
            // tiles from shimmering — TRILINEAR samples between mip
            // levels so the transition is smooth.
            SetTextureWrap(textures[i], TEXTURE_WRAP_REPEAT);
            GenTextureMipmaps(&textures[i]);
            SetTextureFilter(textures[i], TEXTURE_FILTER_TRILINEAR);
            textureLoaded[i] = true;
            fprintf(stderr, "texture: loaded %s%s (%dx%d)\n",
                    texPrefixes[p], bn,
                    textures[i].width, textures[i].height);
            break;
        }
        if (!textureLoaded[i]) {
            fprintf(stderr, "texture: %s not found, using flat colour\n", bn);
        }
    }

    // Shaders live under data/shaders/.
    const char *shaderPrefixes[] = {
        "data/shaders/",
        "../data/shaders/",
        "./data/shaders/",
    };
    worldShaderLoaded = false;
    for (size_t p = 0; p < sizeof shaderPrefixes / sizeof shaderPrefixes[0]; p++) {
        if (TryLoadShader(shaderPrefixes[p], "world.vs", "world.fs", &worldShader)) {
            worldShaderLoaded = true;
            worldShader_fogColorLoc      = GetShaderLocation(worldShader, "fogColor");
            worldShader_fogStartLoc      = GetShaderLocation(worldShader, "fogStart");
            worldShader_fogEndLoc        = GetShaderLocation(worldShader, "fogEnd");
            worldShader_sunDirLoc        = GetShaderLocation(worldShader, "sunDir");
            worldShader_sunColorLoc      = GetShaderLocation(worldShader, "sunColor");
            worldShader_ambientColorLoc  = GetShaderLocation(worldShader, "ambientColor");
            worldShader_tileVariationLoc = GetShaderLocation(worldShader, "tileVariation");
            fprintf(stderr, "shader: loaded %sworld.{vs,fs}\n", shaderPrefixes[p]);
            break;
        }
    }
    if (!worldShaderLoaded) {
        fprintf(stderr, "shader: world.{vs,fs} not found, fog disabled\n");
    }

    // Skinned world shader: world_skinned.vs paired with the same world.fs.
    worldSkinnedShaderLoaded = false;
    for (size_t p = 0; p < sizeof shaderPrefixes / sizeof shaderPrefixes[0]; p++) {
        if (TryLoadShader(shaderPrefixes[p], "world_skinned.vs", "world.fs", &worldSkinnedShader)) {
            worldSkinnedShaderLoaded = true;
            worldSkinnedShader_fogColorLoc     = GetShaderLocation(worldSkinnedShader, "fogColor");
            worldSkinnedShader_fogStartLoc     = GetShaderLocation(worldSkinnedShader, "fogStart");
            worldSkinnedShader_fogEndLoc       = GetShaderLocation(worldSkinnedShader, "fogEnd");
            worldSkinnedShader_sunDirLoc       = GetShaderLocation(worldSkinnedShader, "sunDir");
            worldSkinnedShader_sunColorLoc     = GetShaderLocation(worldSkinnedShader, "sunColor");
            worldSkinnedShader_ambientColorLoc = GetShaderLocation(worldSkinnedShader, "ambientColor");
            fprintf(stderr, "shader: loaded %sworld_skinned.{vs,fs}\n", shaderPrefixes[p]);
            break;
        }
    }
    if (!worldSkinnedShaderLoaded) {
        fprintf(stderr, "shader: world_skinned.vs not found, animated models unlit\n");
    }

    skyShaderLoaded = false;
    for (size_t p = 0; p < sizeof shaderPrefixes / sizeof shaderPrefixes[0]; p++) {
        if (TryLoadShader(shaderPrefixes[p], "sky.vs", "sky.fs", &skyShader)) {
            skyShaderLoaded = true;
            // Unit cube around the camera — vertex shader maps local pos
            // straight to view direction so the cube size doesn't matter.
            skyModel = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));
            skyModel.materials[0].shader = skyShader;
            fprintf(stderr, "shader: loaded %ssky.{vs,fs}\n", shaderPrefixes[p]);
            break;
        }
    }
    if (!skyShaderLoaded) {
        fprintf(stderr, "shader: sky.{vs,fs} not found, sky disabled\n");
    }

    // Post-FX fullscreen shader (postfx.fs — no vertex shader; use raylib's
    // default VS which emits gl_Position from vertexPosition directly).
    postfxShaderLoaded = false;
    for (size_t p = 0; p < sizeof shaderPrefixes / sizeof shaderPrefixes[0]; p++) {
        char fsPath[512];
        snprintf(fsPath, sizeof fsPath, "%s%s", shaderPrefixes[p], "postfx.fs");
        if (!FileExists(fsPath)) continue;
        // NULL vertex shader = raylib's built-in passthrough VS
        postfxShader = LoadShader(NULL, fsPath);
        if (postfxShader.id == 0 || postfxShader.id == rlGetShaderIdDefault()) continue;
        postfxShaderLoaded         = true;
        postfxShader_resolutionLoc = GetShaderLocation(postfxShader, "resolution");
        postfxShader_timeLoc       = GetShaderLocation(postfxShader, "time");
        postfxShader_hitFlashLoc   = GetShaderLocation(postfxShader, "hitFlash");
        postfxShader_lowHpLoc      = GetShaderLocation(postfxShader, "lowHp");
        fprintf(stderr, "shader: loaded %spostfx.fs\n", shaderPrefixes[p]);
        break;
    }
    if (!postfxShaderLoaded) {
        fprintf(stderr, "shader: postfx.fs not found, post-FX disabled\n");
    }

    Assets_ApplyWorldShader();
}

void Assets_ApplyWorldShader(void) {
    if (!worldShaderLoaded) return;
    // Replace the shader on every material of every loaded model so
    // DrawModelEx draws through the fog program. Weapons keep their own
    // separate array but go through the same path.
    for (int i = 0; i < W_COUNT; i++) {
        if (!weaponModelLoaded[i]) continue;
        for (int m = 0; m < weaponModels[i].materialCount; m++) {
            weaponModels[i].materials[m].shader = worldShader;
        }
    }
    for (int i = 0; i < PROP_COUNT; i++) {
        if (!propModelLoaded[i]) continue;
        for (int m = 0; m < propModels[i].materialCount; m++) {
            propModels[i].materials[m].shader = worldShader;
        }
    }
}

void Assets_Unload(void) {
    // Weapons are owned by weapons.{c,h}; Weapons_Unload handles their models.
    for (int i = 0; i < PROP_COUNT; i++) {
        if (propModelLoaded[i]) {
            UnloadModel(propModels[i]);
            propModelLoaded[i] = false;
        }
    }
    for (int i = 0; i < TEX_COUNT; i++) {
        if (textureLoaded[i]) {
            UnloadTexture(textures[i]);
            textureLoaded[i] = false;
        }
    }
    if (skyShaderLoaded) {
        UnloadModel(skyModel);     // also frees the cube mesh
        UnloadShader(skyShader);
        skyShaderLoaded = false;
    }
    if (worldShaderLoaded) {
        UnloadShader(worldShader);
        worldShaderLoaded = false;
    }
    if (worldSkinnedShaderLoaded) {
        UnloadShader(worldSkinnedShader);
        worldSkinnedShaderLoaded = false;
    }
    if (postfxShaderLoaded) {
        UnloadShader(postfxShader);
        postfxShaderLoaded = false;
    }
}
