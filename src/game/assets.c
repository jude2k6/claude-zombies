#include "assets.h"
#include "content.h" // engine content registry: Eng_LoadModel/Texture/Shader
#include "gfx.h"     // Eng_GfxDefaultShaderId — low-level GL stays engine-side
#include <stdio.h>
#include <string.h>

// ---- world-shader model registry ----------------------------------------
// Game code enrolls any externally-owned Model* (e.g. weapon viewmodels) whose
// materials should track the world shader. Assets_ApplyWorldShader() walks this
// list, so assets.c never has to know about weaponModels[] or any game type.
static Model *s_wsModels[WORLD_SHADER_MODEL_MAX];
static int    s_wsModelCount = 0;

void Assets_RegisterWorldShaderModel(Model *m) {
    if (!m) return;
    for (int i = 0; i < s_wsModelCount; i++)
        if (s_wsModels[i] == m) return;        // already enrolled
    if (s_wsModelCount >= WORLD_SHADER_MODEL_MAX) {
        fprintf(stderr, "assets: world-shader registry full (cap=%d)\n",
                WORLD_SHADER_MODEL_MAX);
        return;
    }
    s_wsModels[s_wsModelCount++] = m;
}

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

// Texture filenames under data/textures/.
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
// faces facing away from the moon.
Vector3 sunDir       = { 0.30f, -1.0f, -0.40f };
Vector3 sunColor     = { 0.40f,  0.45f, 0.55f };
Vector3 ambientColor = { 0.55f,  0.58f, 0.65f };

void Assets_Load(void) {
    // Weapon viewmodels are loaded by Weapons_Load() (called separately
    // before this) from data/weapons/<name>/.

    // Generic props — route through the engine content registry.
    // Eng_LoadModel probes data/models/, ../data/models/, ./data/models/.
    for (int i = 0; i < PROP_COUNT; i++) {
        propModelLoaded[i] = false;
        const char *bn = PROP_FILES[i];
        if (!bn) continue;
        EngModel h = Eng_LoadModel(bn);
        Model *m = Eng_ModelGet(h);
        if (m) {
            propModels[i] = *m;
            propModelLoaded[i] = true;
            // Note: "model: loaded ..." line already printed by Eng_LoadModel.
        } else {
            fprintf(stderr, "model: %s not found, using primitive fallback\n", bn);
        }
    }

    // Textures — route through the engine content registry.
    // Eng_LoadTexture probes data/textures/ etc. and applies wrap/mipmap.
    for (int i = 0; i < TEX_COUNT; i++) {
        textureLoaded[i] = false;
        const char *bn = TEXTURE_FILES[i];
        if (!bn) continue;
        EngTexture h = Eng_LoadTexture(bn);
        Texture2D *t = Eng_TextureGet(h);
        if (t) {
            textures[i] = *t;
            textureLoaded[i] = true;
            // Note: "texture: loaded ..." line already printed by Eng_LoadTexture.
        } else {
            fprintf(stderr, "texture: %s not found, using flat colour\n", bn);
        }
    }

    // Shaders — route through the engine content registry.
    // Eng_LoadShader probes data/shaders/ etc.
    {
        EngShader h = Eng_LoadShader("world.vs", "world.fs");
        Shader *s = Eng_ShaderGet(h);
        worldShaderLoaded = (s != NULL);
        if (worldShaderLoaded) {
            worldShader                  = *s;
            worldShader_fogColorLoc      = GetShaderLocation(worldShader, "fogColor");
            worldShader_fogStartLoc      = GetShaderLocation(worldShader, "fogStart");
            worldShader_fogEndLoc        = GetShaderLocation(worldShader, "fogEnd");
            worldShader_sunDirLoc        = GetShaderLocation(worldShader, "sunDir");
            worldShader_sunColorLoc      = GetShaderLocation(worldShader, "sunColor");
            worldShader_ambientColorLoc  = GetShaderLocation(worldShader, "ambientColor");
            worldShader_tileVariationLoc = GetShaderLocation(worldShader, "tileVariation");
        } else {
            fprintf(stderr, "shader: world.{vs,fs} not found, fog disabled\n");
        }
    }

    {
        EngShader h = Eng_LoadShader("world_skinned.vs", "world.fs");
        Shader *s = Eng_ShaderGet(h);
        worldSkinnedShaderLoaded = (s != NULL);
        if (worldSkinnedShaderLoaded) {
            worldSkinnedShader                  = *s;
            worldSkinnedShader_fogColorLoc     = GetShaderLocation(worldSkinnedShader, "fogColor");
            worldSkinnedShader_fogStartLoc     = GetShaderLocation(worldSkinnedShader, "fogStart");
            worldSkinnedShader_fogEndLoc       = GetShaderLocation(worldSkinnedShader, "fogEnd");
            worldSkinnedShader_sunDirLoc       = GetShaderLocation(worldSkinnedShader, "sunDir");
            worldSkinnedShader_sunColorLoc     = GetShaderLocation(worldSkinnedShader, "sunColor");
            worldSkinnedShader_ambientColorLoc = GetShaderLocation(worldSkinnedShader, "ambientColor");
        } else {
            fprintf(stderr, "shader: world_skinned.vs not found, animated models unlit\n");
        }
    }

    {
        EngShader h = Eng_LoadShader("sky.vs", "sky.fs");
        Shader *s = Eng_ShaderGet(h);
        skyShaderLoaded = (s != NULL);
        if (skyShaderLoaded) {
            skyShader = *s;
            // Unit cube around the camera — vertex shader maps local pos
            // straight to view direction so the cube size doesn't matter.
            skyModel = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));
            skyModel.materials[0].shader = skyShader;
        } else {
            fprintf(stderr, "shader: sky.{vs,fs} not found, sky disabled\n");
        }
    }

    {
        // Post-FX fullscreen shader (postfx.fs — no vertex shader; use raylib's
        // default VS which emits gl_Position from vertexPosition directly).
        EngShader h = Eng_LoadShader(NULL, "postfx.fs");
        Shader *s = Eng_ShaderGet(h);
        postfxShaderLoaded = (s != NULL);
        if (postfxShaderLoaded) {
            postfxShader               = *s;
            postfxShader_resolutionLoc = GetShaderLocation(postfxShader, "resolution");
            postfxShader_timeLoc       = GetShaderLocation(postfxShader, "time");
            postfxShader_hitFlashLoc   = GetShaderLocation(postfxShader, "hitFlash");
            postfxShader_lowHpLoc      = GetShaderLocation(postfxShader, "lowHp");
        } else {
            fprintf(stderr, "shader: postfx.fs not found, post-FX disabled\n");
        }
    }

    Assets_ApplyWorldShader();
}

void Assets_ApplyWorldShader(void) {
    if (!worldShaderLoaded) return;
    // Replace the shader on every material of every loaded model so
    // DrawModelEx draws through the fog program. Models owned outside assets.c
    // (weapon viewmodels) are enrolled via Assets_RegisterWorldShaderModel.
    for (int i = 0; i < s_wsModelCount; i++) {
        Model *mdl = s_wsModels[i];
        for (int m = 0; m < mdl->materialCount; m++) {
            mdl->materials[m].shader = worldShader;
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
    // The engine content registry (Eng_ContentFlush) handles the underlying
    // GL resources for all models/textures/shaders loaded via Eng_Load*.
    // The game arrays (propModels[], textures[], worldShader, etc.) just hold
    // copies of the handles' values; we clear the loaded flags here.
    for (int i = 0; i < PROP_COUNT; i++) {
        propModelLoaded[i] = false;
    }
    for (int i = 0; i < TEX_COUNT; i++) {
        textureLoaded[i] = false;
    }
    if (skyShaderLoaded) {
        // skyModel was created with GenMesh + LoadModelFromMesh, not via the
        // engine registry, so we still own it here.
        UnloadModel(skyModel);
        skyShaderLoaded = false;
    }
    worldShaderLoaded        = false;
    worldSkinnedShaderLoaded = false;
    postfxShaderLoaded       = false;

    // Flush engine registry (releases all GL objects) and the name cache.
    Eng_ContentFlush();
    Assets_FlushNameCache();
}

// ---- name-keyed texture cache -------------------------------------------
// This cache is now a thin wrapper over Eng_LoadTextureByName.  We keep the
// integer handle / Assets_CachedTexture interface for callers (level.c uses it
// for per-map texture slot overrides).

typedef struct {
    char  name[64];
    EngTexture engHandle;
    bool  used;
} TexCacheEntry;

static TexCacheEntry texCache[TEX_CACHE_SIZE];
static int           texCacheCount = 0;

int mapTexOverrides[TEX_COUNT];  // -1 = use boot slot

static void InitMapTexOverrides(void) {
    for (int i = 0; i < TEX_COUNT; i++) mapTexOverrides[i] = -1;
}

__attribute__((constructor))
static void TexCacheInit(void) {
    texCacheCount = 0;
    InitMapTexOverrides();
}

void Assets_FlushNameCache(void) {
    // The underlying Texture2D objects are owned by the engine registry;
    // we just drop our index entries here.  The caller must also call
    // Eng_ContentFlush() (or Assets_Unload) to free the GL objects.
    for (int i = 0; i < texCacheCount; i++) {
        texCache[i].used = false;
        texCache[i].name[0] = '\0';
        texCache[i].engHandle = (EngTexture){0};
    }
    texCacheCount = 0;
    InitMapTexOverrides();
}

int Assets_GetTextureByName(const char *name) {
    if (!name || name[0] == '\0') return -1;

    // Dedup: return existing handle if already loaded.
    for (int i = 0; i < texCacheCount; i++) {
        if (texCache[i].used && strcmp(texCache[i].name, name) == 0) {
            return (texCache[i].engHandle.id != 0) ? i : -1;
        }
    }

    // Capacity check.
    if (texCacheCount >= TEX_CACHE_SIZE) {
        fprintf(stderr, "texture cache: full (%d slots), cannot load '%s'\n",
                TEX_CACHE_SIZE, name);
        return -1;
    }

    // Load via engine registry (probes data/textures/<name>.png).
    EngTexture h = Eng_LoadTextureByName(name);
    int slot = texCacheCount++;
    strncpy(texCache[slot].name, name, sizeof texCache[slot].name - 1);
    texCache[slot].name[sizeof texCache[slot].name - 1] = '\0';
    texCache[slot].engHandle = h;
    texCache[slot].used = true;

    if (h.id == 0) {
        // Eng_LoadTextureByName already printed a "not found" line.
        return -1;
    }
    // Success: "texture: loaded ..." line already printed by engine.
    return slot;
}

Texture2D *Assets_CachedTexture(int handle) {
    if (handle < 0 || handle >= texCacheCount) return NULL;
    if (!texCache[handle].used) return NULL;
    return Eng_TextureGet(texCache[handle].engHandle);
}

Texture2D *Assets_ResolveTexture(TextureId tid) {
    if (tid < 0 || tid >= TEX_COUNT) return NULL;
    int ovr = mapTexOverrides[tid];
    if (ovr >= 0) {
        Texture2D *t = Assets_CachedTexture(ovr);
        if (t) return t;
    }
    if (textureLoaded[tid]) return &textures[tid];
    return NULL;
}
