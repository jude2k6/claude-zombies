#ifndef SHOOTER_ASSETS_H
#define SHOOTER_ASSETS_H

#include "raylib.h"
#include "types.h"

// ---- weapon models -------------------------------------------------------
// Weapon model storage + per-weapon tune are owned by weapons.{c,h} (loaded
// alongside the .weapon spec under data/weapons/<name>/). Include weapons.h
// to access weaponModels[] / weaponModelLoaded[] / weaponTune[].

// ---- generic prop registry ----------------------------------------------
// Every non-weapon model the renderer can use is registered here. The order
// must match PROP_FILES[] in assets.c. Each render site calls
// `propModelLoaded[id]` to choose between the model and a primitive
// fallback, so it's safe to ship the binary with any subset of these
// missing — see data/models/ASSETS.md for the spec of every entry.
typedef enum {
    PROP_ZOMBIE = 0,
    PROP_MYSTERY_BOX,
    PROP_BOARD,
    PROP_SANDBAG_STACK,
    PROP_DOOR,
    PROP_DOOR_FRAME,
    PROP_OBSTACLE_CRATE,
    PROP_OBSTACLE_BARREL,
    PROP_PERK_JUG,
    PROP_PERK_SPEED,
    PROP_PERK_DTAP,
    PROP_PERK_STAMIN,
    PROP_PAP,
    PROP_WALLBUY_PANEL,
    PROP_POWERUP_DROP,
    PROP_PLAYER_M,
    PROP_COUNT
} PropId;

extern Model propModels[PROP_COUNT];
extern bool  propModelLoaded[PROP_COUNT];

// ---- texture registry --------------------------------------------------
// Walls and floors use textured surfaces instead of OBJ tiles. Renderer
// looks up `textures[id]` and falls back to a flat-colour cube draw when
// the texture isn't loaded — see data/models/ASSETS.md "Textures".
typedef enum {
    TEX_FLOOR = 0,    // arena floor
    TEX_GROUND,       // outside-the-walls plane
    TEX_WALL_EXT,     // exterior arena walls
    TEX_WALL_INT,     // interior dividing walls
    TEX_CEILING,      // reserved (no ceiling on current maps)
    TEX_COUNT
} TextureId;

extern Texture2D textures[TEX_COUNT];
extern bool      textureLoaded[TEX_COUNT];

// Repeat distance — the texture tiles once every TILE_SIZE world metres.
// At 4 m a 40 m floor still tiles 10× per axis, but the visible grid is
// soft enough that the `tileVariation` shader overlay (see world.fs) hides
// the remaining repetition.
#define TILE_SIZE 4.0f

// ---- shaders ------------------------------------------------------------
// `worldShader` replaces raylib's default shader for the 3D pass so every
// model and every rlgl immediate draw goes through one program that does
// texture * tint * fog. `skyShader` paints the procedural night sky.
// Both share data/shaders/*.{vs,fs}; falling back to the default shader
// if the files don't load means missing-asset state still runs.
extern Shader worldShader;
extern bool   worldShaderLoaded;
extern int    worldShader_fogColorLoc;
extern int    worldShader_fogStartLoc;
extern int    worldShader_fogEndLoc;
extern int    worldShader_sunDirLoc;
extern int    worldShader_sunColorLoc;
extern int    worldShader_ambientColorLoc;
extern int    worldShader_tileVariationLoc;

// Skinned variant of the world shader (world_skinned.vs + world.fs): same
// lighting + fog, but with GPU skeletal skinning. Assigned to animated
// (glTF) models' materials. Its fog/sun/ambient uniforms are pushed every
// frame alongside worldShader's (see render BeginWorldShader).
extern Shader worldSkinnedShader;
extern bool   worldSkinnedShaderLoaded;
extern int    worldSkinnedShader_fogColorLoc;
extern int    worldSkinnedShader_fogStartLoc;
extern int    worldSkinnedShader_fogEndLoc;
extern int    worldSkinnedShader_sunDirLoc;
extern int    worldSkinnedShader_sunColorLoc;
extern int    worldSkinnedShader_ambientColorLoc;

extern Shader skyShader;
extern bool   skyShaderLoaded;
extern Model  skyModel;             // unit cube, materials[0].shader = skyShader

// Post-process fullscreen-quad shader (postfx.fs). Loaded alongside the world
// shaders; postfxShaderLoaded == false triggers the "no RT, draw direct" path.
extern Shader postfxShader;
extern bool   postfxShaderLoaded;
extern int    postfxShader_resolutionLoc;
extern int    postfxShader_timeLoc;
extern int    postfxShader_hitFlashLoc;
extern int    postfxShader_lowHpLoc;

// Tuning the renderer applies to worldShader every frame.
extern float   fogStart;
extern float   fogEnd;
extern Color   fogColor;
extern Vector3 sunDir;          // direction the moon's light travels (unit)
extern Vector3 sunColor;        // RGB 0..1
extern Vector3 ambientColor;    // RGB 0..1

void Assets_Load(void);    // call after InitWindow
void Assets_Unload(void);  // call before CloseWindow

// Wire `worldShader` onto every loaded prop / weapon model and to rlgl's
// default. Called after Assets_Load and after any later LoadModel.
void Assets_ApplyWorldShader(void);

// ---- name-keyed texture cache -------------------------------------------
// Look up (or load on first request) a texture by base name.  The file
// data/textures/<name>.png is probed via the same prefix list as the slot
// textures.  Returns a non-negative handle on success; -1 if the file is
// missing or fails to load (one stderr line is printed).  Handles deduplicate:
// calling with the same name twice returns the same handle.
// Cache capacity: TEX_CACHE_SIZE entries.  Flush with Assets_FlushNameCache
// (call before loading a new map so stale GL textures are released on the
// main thread before GL context teardown).
#define TEX_CACHE_SIZE  32
int       Assets_GetTextureByName(const char *name);
Texture2D *Assets_CachedTexture(int handle);  // NULL if handle invalid
void      Assets_FlushNameCache(void);        // unload all cached textures

// ---- per-map texture slot overrides -------------------------------------
// Indexed by TextureId (0..TEX_COUNT-1).  -1 = use the boot slot texture.
// Set by Level_InstantiateDoc from the map's TEXTURES block; cleared by
// Assets_FlushNameCache (called at the start of Level_InstantiateDoc).
extern int mapTexOverrides[TEX_COUNT];

// Resolve a TextureId to the Texture2D* to use, applying override then
// boot-slot fallback.  Returns NULL only when neither is loaded (flat
// colour fallback stays in the caller).
Texture2D *Assets_ResolveTexture(TextureId tid);

#endif
