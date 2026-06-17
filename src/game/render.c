#include "render.h"
#include "eng_render.h"  // engine render module: postFX RT + world shader pass
#include "viewmodel.h"
#include "level.h"
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "entities.h"
#include "interact.h"
#include "assets.h"
#include "mobs.h"
#include "decals.h"
#include "particles.h"
#include "anim.h"
#include "raymath.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>

// ---- shared rigged zombie (glTF skeletal animation) --------------------
// One shared skinned model + clips; each enemy slot carries a lightweight,
// render-local AnimState (clip + time). Driven entirely from DrawEnemy with
// GetFrameTime() — purely visual, so it isn't serialized (same precedent as
// the local viewmodel anim). Falls back to PROP_ZOMBIE / cubes when the
// .glb isn't present.
static AnimModel zombieAnim;
static AnimState zombieAnimState[MAX_ENEMIES];
static int zClipWalk = -1, zClipAttack = -1, zClipDeath = -1;

// ---- rigged third-person player model (glTF) ---------------------------
// One shared skinned soldier (player.glb) + a render-local AnimState per
// player slot. Only OTHER g_world.players are drawn with it (you never see your own
// body). State is render-local / not serialized — same precedent as the
// zombie anim: it's purely visual and reconstructed from the synced fields
// (pos delta → locomotion, reloadTimer, downed, reviveAsTarget, alive). Falls
// back to PROP_PLAYER_M / cubes when player.glb isn't present.
static AnimModel playerAnim;
static AnimState playerAnimState[NET_MAX_PLAYERS];
static Vector3   playerAnimPrevPos[NET_MAX_PLAYERS];
static float     playerAnimSpeed[NET_MAX_PLAYERS];   // smoothed horizontal m/s
static bool      playerAnimSeen[NET_MAX_PLAYERS];
static int pmIdle = -1, pmWalk = -1, pmRun = -1, pmFire = -1, pmReload = -1,
          pmRevive = -1, pmDowned = -1, pmDeath = -1;

static const Color PLAYER_COLORS[NET_MAX_PLAYERS] = {
    {220, 80, 80, 255}, {80, 120, 220, 255}, {90, 200, 90, 255}, {220, 200, 80, 255},
};

// Shorthand for the model-first / cube-fallback pattern that every
// prop draw uses below. `propModelLoaded[id]` is the only switch — every
// site simply tries the model first and falls back to a primitive draw
// only when the OBJ hasn't been authored yet.
static inline void DrawPropEx(PropId id, Vector3 pos, float yawDeg, Vector3 scale, Color tint) {
    Eng_GfxDrawModelEx(propModels[id], pos, (Vector3){0, 1, 0}, yawDeg, scale, tint);
}
static inline void DrawProp(PropId id, Vector3 pos, float yawDeg, Color tint) {
    DrawPropEx(id, pos, yawDeg, (Vector3){1, 1, 1}, tint);
}

// Load the shared rigged zombie and bind the skinned (lit + fogged) shader.
// Call once after Assets_Load (which loads worldSkinnedShader). No-op-safe:
// if the .glb is missing, DrawEnemy falls back to the OBJ / cube draw.
void Render_LoadZombieAnim(void) {
    // Model + animation clip names are data-driven from the zombie mob def when
    // the catalog is loaded; fall back to the historical hardcoded names so the
    // headless devtools (which may not call Mobs_Load) still render.
    const MobDef *md = Mob_Find("ZOMBIE");
    const char *model      = (md && md->model[0])      ? md->model      : "zombie.glb";
    const char *clipWalk   = (md && md->animWalk[0])   ? md->animWalk   : "walk";
    const char *clipAttack = (md && md->animAttack[0]) ? md->animAttack : "attack_a";
    const char *clipDeath  = (md && md->animDeath[0])  ? md->animDeath  : "death";
    if (!Anim_Load(&zombieAnim, model)) return;
    if (Eng_RenderWorldSkinnedShaderLoaded()) Anim_ApplyShader(&zombieAnim, Eng_RenderWorldSkinnedShader());
    zClipWalk   = Anim_FindClip(&zombieAnim, clipWalk);
    zClipAttack = Anim_FindClip(&zombieAnim, clipAttack);
    zClipDeath  = Anim_FindClip(&zombieAnim, clipDeath);
    for (int i = 0; i < MAX_ENEMIES; i++)
        Anim_Play(&zombieAnimState[i], zClipWalk, true, 1.0f);
}

void Render_UnloadZombieAnim(void) {
    Anim_Unload(&zombieAnim);
}

// Load the shared rigged player model; bind the skinned (lit + fogged) shader.
// Call once after Assets_Load. No-op-safe: missing .glb leaves DrawOtherPlayer
// on its PROP_PLAYER_M / cube path.
void Render_LoadPlayerAnim(void) {
    if (!Anim_Load(&playerAnim, "player.glb")) return;
    if (Eng_RenderWorldSkinnedShaderLoaded()) Anim_ApplyShader(&playerAnim, Eng_RenderWorldSkinnedShader());
    pmIdle   = Anim_FindClip(&playerAnim, "idle");
    pmWalk   = Anim_FindClip(&playerAnim, "walk");
    pmRun    = Anim_FindClip(&playerAnim, "run");
    pmFire   = Anim_FindClip(&playerAnim, "fire");
    pmReload = Anim_FindClip(&playerAnim, "reload");
    pmRevive = Anim_FindClip(&playerAnim, "revive");
    pmDowned = Anim_FindClip(&playerAnim, "downed");
    pmDeath  = Anim_FindClip(&playerAnim, "death");
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Anim_Play(&playerAnimState[i], pmIdle, true, 1.0f);
        playerAnimSpeed[i] = 0.0f;
        playerAnimSeen[i] = false;
    }
}

void Render_UnloadPlayerAnim(void) {
    Anim_Unload(&playerAnim);
}

// ---- textured surfaces -------------------------------------------------
// Walls and floors are procedural geometry with tiled textures.  Helpers
// below emit textured quads via rlgl immediate mode so each surface gets
// UVs scaled by `size / TILE_SIZE` and the texture repeats seamlessly
// across the box face.  Caller supplies a fallback colour for when the
// texture isn't loaded.

// Flush whatever rlgl immediate verts are pending under the *current*
// tileVariation value, switch the uniform on/off, and ensure the next
// flush picks up the new value. Called as a bookend around the textured
// box/floor emitters so the macro-variation overlay only colours tiled
// world geometry — not the grid lines, debug cubes, or anything else
// sharing the immediate-mode batch.
static void BeginTileVariation(void) {
    if (!Eng_RenderWorldShaderLoaded()) return;
    Eng_GfxFlushBatch();
    float v = 1.0f;
    SetShaderValue(Eng_RenderWorldShader(), Eng_RenderWorldShader_tileVariationLoc(), &v, SHADER_UNIFORM_FLOAT);
}
static void EndTileVariation(void) {
    if (!Eng_RenderWorldShaderLoaded()) return;
    Eng_GfxFlushBatch();
    float v = 0.0f;
    SetShaderValue(Eng_RenderWorldShader(), Eng_RenderWorldShader_tileVariationLoc(), &v, SHADER_UNIFORM_FLOAT);
}

// Draw a textured axis-aligned box.  Each face's UVs span the face's
// world-space size divided by TILE_SIZE, so the texture tiles
// continuously across walls of any length.
// `tex` — explicit Texture2D* to use; NULL triggers fallback.
// `fallback` — flat colour used when tex == NULL.
static void DrawTexturedBoxTex(Vector3 c, Vector3 s, Texture2D *tex,
                               Color tint, Color fallback) {
    if (!tex) { Eng_DrawTexturedBoxV(c, s, tex, TILE_SIZE, tint, fallback); return; }
    BeginTileVariation();
    Eng_DrawTexturedBoxV(c, s, tex, TILE_SIZE, tint, fallback);
    EndTileVariation();
}

// Convenience: resolve TextureId through the per-map override chain, then draw.
static void DrawTexturedBox(Vector3 c, Vector3 s, TextureId tid,
                            Color tint, Color fallback) {
    DrawTexturedBoxTex(c, s, Assets_ResolveTexture(tid), tint, fallback);
}

// Textured XZ plane centred at `center` of size `sizeX × sizeZ`, top
// face pointing +Y.  Used for the arena floor and outside ground.
// `tex` — explicit Texture2D*; NULL triggers flat colour fallback.
static void DrawTexturedFloorTex(Vector3 center, float sizeX, float sizeZ,
                                 Texture2D *tex, Color tint, Color fallback) {
    if (!tex) { Eng_DrawTexturedFloorV(center, sizeX, sizeZ, tex, TILE_SIZE, tint, fallback); return; }
    BeginTileVariation();
    Eng_DrawTexturedFloorV(center, sizeX, sizeZ, tex, TILE_SIZE, tint, fallback);
    EndTileVariation();
}

// Convenience: resolve TextureId through the per-map override chain, then draw.
static void DrawTexturedFloor(Vector3 center, float sizeX, float sizeZ,
                              TextureId tid, Color tint, Color fallback) {
    DrawTexturedFloorTex(center, sizeX, sizeZ, Assets_ResolveTexture(tid), tint, fallback);
}

static void DrawWallXSeg(float x0, float x1, float fixedZ, Color c) {
    if (x1 <= x0) return;
    float cx = (x0 + x1) * 0.5f;
    DrawTexturedBox((Vector3){cx, WALL_HEIGHT*0.5f, fixedZ},
                    (Vector3){x1 - x0, WALL_HEIGHT, WALL_THICK},
                    TEX_WALL_EXT, WHITE, c);
}
static void DrawWallZSeg(float z0, float z1, float fixedX, Color c) {
    if (z1 <= z0) return;
    float cz = (z0 + z1) * 0.5f;
    DrawTexturedBox((Vector3){fixedX, WALL_HEIGHT*0.5f, cz},
                    (Vector3){WALL_THICK, WALL_HEIGHT, z1 - z0},
                    TEX_WALL_EXT, WHITE, c);
}

// Walkable floor regions for multi-floor maps. Flat slabs draw as a textured
// top plane plus a thin body for a depth cue; ramps draw as a sloped quad
// (both windings so the surface shows from either side). The implicit ground
// plane at Y=0 is the arena floor below, drawn by DrawArena itself.
static void DrawFloors(void) {
    for (int i = 0; i < g_world.floorCount; i++) {
        const FloorRegion *f = &g_world.floors[i];
        float x0 = f->cx - f->halfX, x1 = f->cx + f->halfX;
        float z0 = f->cz - f->halfZ, z1 = f->cz + f->halfZ;
        if (f->rampAxis == RAMP_FLAT) {
            DrawTexturedFloor((Vector3){ f->cx, f->yLow, f->cz },
                              f->halfX * 2, f->halfZ * 2,
                              TEX_FLOOR, WHITE, (Color){ 70, 80, 95, 255 });
            Eng_GfxDrawCubeV((Vector3){ f->cx, f->yLow - 0.1f, f->cz },
                      (Vector3){ f->halfX * 2, 0.2f, f->halfZ * 2 },
                      (Color){ 45, 50, 60, 255 });
            Eng_GfxDrawCubeWiresV((Vector3){ f->cx, f->yLow - 0.1f, f->cz },
                           (Vector3){ f->halfX * 2, 0.2f, f->halfZ * 2 },
                           (Color){ 25, 28, 34, 255 });
        } else {
            Vector3 a = { x0, Level_RegionSurfaceY(f, x0, z0), z0 };
            Vector3 b = { x1, Level_RegionSurfaceY(f, x1, z0), z0 };
            Vector3 c = { x1, Level_RegionSurfaceY(f, x1, z1), z1 };
            Vector3 d = { x0, Level_RegionSurfaceY(f, x0, z1), z1 };
            Color rc = (Color){ 90, 95, 110, 255 };
            Eng_GfxDrawTriangle3D(a, b, c, rc); Eng_GfxDrawTriangle3D(a, c, d, rc);
            Eng_GfxDrawTriangle3D(a, c, b, rc); Eng_GfxDrawTriangle3D(a, d, c, rc);
        }
    }
}

static void DrawArena(void) {
    DrawTexturedFloor((Vector3){0,0,0}, g_world.arenaHalfX*2, g_world.arenaHalfZ*2,
                      TEX_FLOOR, WHITE, (Color){55,65,75,255});
    float groundW = (g_world.arenaHalfX > g_world.arenaHalfZ ? g_world.arenaHalfX : g_world.arenaHalfZ) * 2 + 16;
    DrawTexturedFloor((Vector3){0, -0.01f, 0}, groundW, groundW,
                      TEX_GROUND, WHITE, (Color){30,35,40,255});

    Color wc = (Color){90,100,110,255};
    /* Perimeter walls — sides 0/1 are north/south (fixed z, vary x).
       Sides 2/3 are east/west (fixed x, vary z). */
    float aX = g_world.arenaHalfX;  /* half-extent along X */
    float aZ = g_world.arenaHalfZ;  /* half-extent along Z */
    for (int side = 0; side < 4; side++) {
        float gapPos = 0; bool hasGap = false;
        for (int i = 0; i < g_world.windowCount; i++) {
            if (side == 0 && g_world.windows[i].pos.z >  aZ - 0.5f) { gapPos = g_world.windows[i].pos.x; hasGap = true; }
            if (side == 1 && g_world.windows[i].pos.z < -aZ + 0.5f) { gapPos = g_world.windows[i].pos.x; hasGap = true; }
            if (side == 2 && g_world.windows[i].pos.x >  aX - 0.5f) { gapPos = g_world.windows[i].pos.z; hasGap = true; }
            if (side == 3 && g_world.windows[i].pos.x < -aX + 0.5f) { gapPos = g_world.windows[i].pos.z; hasGap = true; }
        }
        float gMin = gapPos - WINDOW_WIDTH*0.5f, gMax = gapPos + WINDOW_WIDTH*0.5f;
        /* sides 0/1: X-running wall at fixed z=+aZ or z=-aZ; runs from -aX to +aX */
        if (side == 0) { if (hasGap) { DrawWallXSeg(-aX, gMin,  aZ, wc); DrawWallXSeg(gMax, aX,  aZ, wc); } else DrawWallXSeg(-aX, aX,  aZ, wc); }
        else if (side == 1) { if (hasGap) { DrawWallXSeg(-aX, gMin, -aZ, wc); DrawWallXSeg(gMax, aX, -aZ, wc); } else DrawWallXSeg(-aX, aX, -aZ, wc); }
        /* sides 2/3: Z-running wall at fixed x=+aX or x=-aX; runs from -aZ to +aZ */
        else if (side == 2) { if (hasGap) { DrawWallZSeg(-aZ, gMin,  aX, wc); DrawWallZSeg(gMax, aZ,  aX, wc); } else DrawWallZSeg(-aZ, aZ,  aX, wc); }
        else                 { if (hasGap) { DrawWallZSeg(-aZ, gMin, -aX, wc); DrawWallZSeg(gMax, aZ, -aX, wc); } else DrawWallZSeg(-aZ, aZ, -aX, wc); }
    }

    for (int i = 0; i < g_world.obstacleCount; i++) {
        // Per-surface TEX override: if set, draw the obstacle as a textured box
        // regardless of whether the crate model is loaded.  This lets map authors
        // give each obstacle a distinct look without a dedicated OBJ.
        int h = obstacleTexHandle[i];
        if (h >= 0) {
            Texture2D *tex = Assets_CachedTexture(h);
            if (tex) {
                DrawTexturedBoxTex(g_world.obstacles[i].center, g_world.obstacles[i].size,
                                   tex, WHITE, (Color){120,90,70,255});
                continue;
            }
        }
        if (propModelLoaded[PROP_OBSTACLE_CRATE]) {
            // crate.obj is unit-cube; scale to the obstacle's box size.
            DrawPropEx(PROP_OBSTACLE_CRATE,
                       g_world.obstacles[i].center, 0.0f,
                       g_world.obstacles[i].size, WHITE);
        } else {
            Eng_GfxDrawCubeV(g_world.obstacles[i].center, g_world.obstacles[i].size, (Color){120,90,70,255});
            Eng_GfxDrawCubeWiresV(g_world.obstacles[i].center, g_world.obstacles[i].size, (Color){40,30,20,255});
        }
    }

    DrawFloors();
}

// Map-authored props placed via the `PROP` line in .map files. Each entry
// already has a PropId resolved and a collider sized for it; we just need
// to draw the model (or a placeholder cube if the OBJ isn't loaded yet).
static void DrawMapProps(void) {
    for (int i = 0; i < mapPropCount; i++) {
        MapProp *mp = &mapProps[i];
        if (propModelLoaded[mp->propId]) {
            DrawPropEx(mp->propId, mp->pos, mp->yawDeg,
                       (Vector3){mp->scale, mp->scale, mp->scale}, WHITE);
        } else {
            // Show the collider so authors can position even with no OBJ.
            Eng_GfxDrawCubeV(mp->collider.center, mp->collider.size, (Color){140,110,80,255});
            Eng_GfxDrawCubeWiresV(mp->collider.center, mp->collider.size, BLACK);
        }
    }
}

static void DrawInteriorWalls(void) {
    for (int i = 0; i < interiorWallCount; i++) {
        // Per-surface handle takes priority, then map-slot override, then boot slot.
        Texture2D *tex = NULL;
        int h = interiorWallTexHandle[i];
        if (h >= 0)
            tex = Assets_CachedTexture(h);
        if (!tex)
            tex = Assets_ResolveTexture(TEX_WALL_INT);
        DrawTexturedBoxTex(interiorWalls[i].center, interiorWalls[i].size,
                           tex, WHITE, (Color){80, 90, 100, 255});
    }
}

static void DrawDoors(void) {
    // Door OBJ is authored 1.6 m wide × 2.5 m tall, frame at 1.8 m wide.
    // The opening in the wall is sized to the frame, so we scale both the
    // frame AND the door by openingW / FRAME_OBJ_WIDTH — that keeps the
    // door slightly narrower than the cutout, leaving the jamb visible.
    const float FRAME_OBJ_WIDTH = 1.8f;
    for (int i = 0; i < doorCount; i++) {
        Box b = doors[i].box;
        bool xRun = (b.size.x > b.size.z);
        float yaw = xRun ? 0.0f : 90.0f;
        float openingW = xRun ? b.size.x : b.size.z;
        float scaleX = openingW / FRAME_OBJ_WIDTH;
        Vector3 s = { scaleX, 1.0f, 1.0f };

        // Frame is always drawn (even when the door is opened) so the
        // opening reads as trimmed instead of a clean hole.
        Vector3 framePos = { b.center.x, 0.0f, b.center.z };
        if (propModelLoaded[PROP_DOOR_FRAME]) {
            DrawPropEx(PROP_DOOR_FRAME, framePos, yaw, s, WHITE);
        }

        if (doors[i].opened) continue;

        Vector3 doorPos = { b.center.x, 0.0f, b.center.z };
        if (propModelLoaded[PROP_DOOR]) {
            DrawPropEx(PROP_DOOR, doorPos, yaw, s, WHITE);
        } else {
            Eng_GfxDrawCubeV(b.center, b.size, (Color){130, 80, 50, 255});
            Eng_GfxDrawCubeWiresV(b.center, b.size, (Color){40, 25, 15, 255});
        }
    }
}

static void DrawWindows(void) {
    for (int i = 0; i < g_world.windowCount; i++) {
        Window3D *w = &g_world.windows[i];
        for (int b = 0; b < w->boards; b++) {
            float t = (b + 1.0f) / (MAX_BOARDS_PER_WIN + 1.0f);
            float by = t * WALL_HEIGHT;
            // Nail the board to the arena's inside face of the wall (toward
            // the player) so it overlaps the wall's edge trim instead of
            // floating in the middle of the wall thickness. w->normal
            // points outward; -w->normal is the inside face direction.
            float push = WALL_THICK * 0.5f - 0.05f;
            Vector3 planeCenter = {
                w->pos.x - w->normal.x * push,
                by,
                w->pos.z - w->normal.z * push,
            };
            if (propModelLoaded[PROP_BOARD]) {
                // board.obj is authored 4.0 long along +X. Walls facing ±X
                // need the plank rotated 90° so it lies across the opening.
                float yaw = (fabsf(w->normal.x) > 0.5f) ? 90.0f : 0.0f;
                DrawProp(PROP_BOARD, planeCenter, yaw, WHITE);
            } else {
                Vector3 planeSize = (fabsf(w->normal.x) > 0.5f)
                    ? (Vector3){ WALL_THICK + 0.2f, 0.25f, WINDOW_WIDTH }
                    : (Vector3){ WINDOW_WIDTH, 0.25f, WALL_THICK + 0.2f };
                Eng_GfxDrawCubeV(planeCenter, planeSize, (Color){150, 100, 50, 255});
                Eng_GfxDrawCubeWiresV(planeCenter, planeSize, (Color){60, 40, 20, 255});
            }
        }
    }
}

// Draw a weapon at pos with given yaw (degrees). If a Model is loaded for the
// weapon, use it; otherwise fall back to the old colored cube. The weapon OBJs
// are authored at wildly different unit scales (model_scale 10–35 in the
// .weapon files), so a raw scale of 1.0 makes each gun a different size — the
// M1911 (scale 35) came out ~3.5× smaller than the rifle/smg (scale 10). Use
// the same life-size base the first-person viewmodel uses, 0.05 * tune.scale,
// so every gun shows at true world size; `displayScale` is then an extra
// caller multiplier (1.0 = life-size). weaponTune.yawDeg is deliberately NOT
// added here — it is an in-hand framing tilt (28° on the pistol) that would
// skew the gun off a flat wall/box mount.
static void DrawWeaponDisplay(int weaponIdx, Vector3 pos, float yawDeg, float displayScale) {
    if (weaponIdx >= 0 && weaponIdx < W_COUNT && weaponModelLoaded[weaponIdx]) {
        float s = displayScale * 0.05f * weaponTune[weaponIdx].scale;
        Eng_GfxDrawModelEx(weaponModels[weaponIdx], pos,
                    (Vector3){0, 1, 0},
                    yawDeg,
                    (Vector3){s, s, s},
                    WHITE);
    } else {
        Vector3 size = { 0.8f * displayScale, 0.3f * displayScale, 0.2f * displayScale };
        Eng_GfxDrawCubeV(pos, size, WEAPONS[weaponIdx].tint);
        Eng_GfxDrawCubeWiresV(pos, size, BLACK);
    }
}

static void DrawWallBuys(void) {
    for (int i = 0; i < wallBuyCount; i++) {
        WallBuy *wb = &wallBuys[i];
        const WeaponDef *w = &WEAPONS[wb->weaponIdx];
        if (propModelLoaded[PROP_WALLBUY_PANEL]) {
            // Panel is authored facing -Z (its mounting face).  Rotate 90°
            // when the wall normal is along ±X so the panel faces outward.
            float yaw = (fabsf(wb->normal.x) > 0.5f) ? 90.0f : 0.0f;
            // Tint with the weapon colour so each buy still reads as the
            // right category at a glance.
            DrawProp(PROP_WALLBUY_PANEL, wb->pos, yaw, w->tint);
        } else {
            Vector3 size = (fabsf(wb->normal.x) > 0.5f)
                ? (Vector3){ 0.2f, 1.0f, 1.6f }
                : (Vector3){ 1.6f, 1.0f, 0.2f };
            Eng_GfxDrawCubeV(wb->pos, size, w->tint);
            Eng_GfxDrawCubeWiresV(wb->pos, size, BLACK);
        }

        if (weaponModelLoaded[wb->weaponIdx]) {
            // The gun lies flat on the panel with its barrel parallel to the
            // wall. That is 90° off the panel's outward-facing yaw above —
            // reusing the panel yaw pointed the barrel straight out of the
            // wall. Models are authored barrel-along-(-Z): yaw 0 runs the
            // barrel along Z (for a wall whose normal is ±X), yaw 90 along X
            // (for a wall whose normal is ±Z).
            float yaw = (fabsf(wb->normal.x) > 0.5f) ? 0.0f : 90.0f;
            Vector3 p = {
                wb->pos.x + wb->normal.x * 0.18f,
                wb->pos.y,
                wb->pos.z + wb->normal.z * 0.18f,
            };
            DrawWeaponDisplay(wb->weaponIdx, p, yaw, 1.0f);  // life-size on the mount
        }
    }
}

// Map perk slots to their dedicated cabinet model.  Each variant has a
// distinct faceplate colour / logo baked into the OBJ.
static PropId PerkCabinetProp(int perkIdx) {
    switch (perkIdx) {
        case PERK_JUG:    return PROP_PERK_JUG;
        case PERK_SPEED:  return PROP_PERK_SPEED;
        case PERK_DTAP:   return PROP_PERK_DTAP;
        case PERK_STAMIN: return PROP_PERK_STAMIN;
        default:          return PROP_PERK_JUG;
    }
}

static void DrawPerkMachines(void) {
    for (int i = 0; i < perkMachineCount; i++) {
        PerkMachine *pm = &perkMachines[i];
        const PerkDef *pd = &PERKS[pm->perkIdx];
        PropId pid = PerkCabinetProp(pm->perkIdx);
        if (propModelLoaded[pid]) {
            // Cabinet origin sits on the floor at pm->pos.
            Vector3 base = { pm->pos.x, 0.0f, pm->pos.z };
            DrawProp(pid, base, 0.0f, WHITE);
        } else {
            Eng_GfxDrawCube((Vector3){ pm->pos.x, 0.6f, pm->pos.z }, 1.0f, 1.2f, 1.0f, (Color){30,30,30,255});
            Eng_GfxDrawCube((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, pd->tint);
            Eng_GfxDrawCubeWires((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, BLACK);
            Eng_GfxDrawCube((Vector3){ pm->pos.x, 2.5f, pm->pos.z }, 0.6f, 0.4f, 0.6f, (Color){40,40,40,255});
            bool myOwn = g_world.players[localPlayerIdx].hasPerk[pm->perkIdx];
            Eng_GfxDrawSphere((Vector3){ pm->pos.x, 2.95f, pm->pos.z }, 0.18f,
                       myOwn ? (Color){240,240,240,255} : pd->tint);
        }
    }
}

// CoD-style Pack-a-Punch. The cabinet + chamber housing + emitter posts are
// the authored pap_machine.obj; only the chamber *shutter*, the spark FX, and
// the weapon animation are drawn here, driven by the phase machine in
// interact.c. Coordinates match the model's chamber mouth (top rim ~y=1.99,
// opening ~0.58 x 0.46). See docs/pack-a-punch-spec.md.
//
// Mouth geometry the animation aligns to:
#define PAP_MOUTH_Y    1.99f   // top rim of the chamber (world Y)
#define PAP_INSIDE_Y   1.70f   // weapon's resting depth inside the chamber
static void DrawPaP(void) {
    float px = g_world.pap.pos.x, pz = g_world.pap.pos.z;

    // --- Cabinet (authored model, or primitive fallback) --------------
    if (propModelLoaded[PROP_PAP]) {
        DrawProp(PROP_PAP, (Vector3){ px, 0.0f, pz }, 0.0f, WHITE);
    } else {
        Eng_GfxDrawCube(g_world.pap.pos, 2.5f, 0.6f, 2.5f, (Color){25,25,30,255});
        Eng_GfxDrawCubeWires(g_world.pap.pos, 2.5f, 0.6f, 2.5f, BLACK);
        Eng_GfxDrawCube((Vector3){ px, 1.0f, pz }, 2.0f, 0.8f, 2.0f, (Color){50,50,60,255});
        Eng_GfxDrawCube((Vector3){ px, 1.7f, pz }, 0.9f, 0.45f, 0.7f, (Color){80,50,130,255});
    }

    float workPulse = 0.5f + 0.5f * sinf(g_world.pap.bob * 9.0f);
    Color trimHot = (Color){ 200, 150, 255, 255 };

    // --- Chamber shutter ----------------------------------------------
    // A panel that rises from inside the chamber to seal the mouth while the
    // machine works. Timed so it is OPEN while the gun is dropping in (INSERT)
    // and while the gun rises back out (end of WORK / READY), and SHUT in the
    // middle of the work cycle — it never collides with the weapon.
    float lidClose = 0.0f;
    if (g_world.pap.phase == PAP_WORK) {
        float elapsed = PAP_WORK_TIME - g_world.pap.timer;
        float closeUp = elapsed / 0.5f;       if (closeUp  > 1) closeUp  = 1;
        float openDn  = g_world.pap.timer / 0.5f;      if (openDn   > 1) openDn   = 1;
        lidClose = (closeUp < openDn) ? closeUp : openDn;  // min: seal then open
    }
    if (lidClose > 0.02f) {
        float y = PAP_INSIDE_Y + lidClose * (PAP_MOUTH_Y - PAP_INSIDE_Y);
        Eng_GfxDrawCubeV((Vector3){ px, y, pz }, (Vector3){0.58f, 0.07f, 0.46f}, (Color){60,42,90,255});
        Eng_GfxDrawCubeWiresV((Vector3){ px, y, pz }, (Vector3){0.58f, 0.07f, 0.46f}, trimHot);
    }

    // --- Phase-driven weapon animation --------------------------------
    Vector3 chamberIn = { px, PAP_INSIDE_Y, pz };
    if (g_world.pap.phase == PAP_INSERT && g_world.pap.weaponIdx >= 0) {
        // Gun descends from a hand-height point in front into the chamber
        // mouth, spinning as it drops.
        float frac = 1.0f - g_world.pap.timer / PAP_INSERT_TIME;   // 0 -> 1
        Vector3 handPt = { px, 2.35f, pz + 0.95f };
        Vector3 gp = Vector3Lerp(handPt, chamberIn, frac);
        DrawWeaponDisplay(g_world.pap.weaponIdx, gp, 90.0f + frac * 180.0f, 1.0f);
    } else if (g_world.pap.phase == PAP_WORK) {
        // Sealed: sparks spit from the mouth seam, brightest at the seal.
        for (int i = 0; i < 6; i++) {
            float a = g_world.pap.bob * 6.0f + i * 1.7f;
            float r = 0.18f + 0.16f * sinf(a * 1.3f);
            Vector3 sp = { px + cosf(a) * r, PAP_MOUTH_Y + sinf(a*2.0f) * 0.1f, pz + sinf(a) * r };
            Eng_GfxDrawCubeV(sp, (Vector3){0.045f,0.045f,0.045f},
                      (Color){255, (unsigned char)(170 + 70*workPulse), 110, 255});
        }
    } else if (g_world.pap.phase == PAP_READY && g_world.pap.weaponIdx >= 0) {
        // Upgraded weapon rises out of the mouth, glowing + slowly spinning,
        // until the owner manually takes it (Use).
        Vector3 gp = { px, 2.28f + sinf(g_world.pap.bob) * 0.06f, pz };
        DrawWeaponDisplay(g_world.pap.weaponIdx, gp, g_world.pap.bob * 50.0f, 1.15f);
    }
}

static void DrawEnemy(Enemy *e) {
    float bob = sinf(e->bobPhase) * 0.06f;
    Vector3 body = { e->pos.x, e->pos.y + bob, e->pos.z };
    Color hpTint = WHITE;

    // Runner pre-lunge tell: specialTimer in (0.55, 0.75] is the 0.2s
    // wind-up window before the speed burst kicks in. Render an angry
    // red tint + glowing eye spheres so the lunge is anticipated.
    bool runnerWindup = (e->type == ZT_RUNNER &&
                         e->specialTimer > 0.55f && e->specialTimer <= 0.75f);
    if (runnerWindup) hpTint = (Color){255, 90, 70, 255};
    bool stunned = (e->stunTimer > 0);
    if (stunned && !runnerWindup) hpTint = (Color){120, 180, 230, 255};

    // Per-type silhouette scale. Runner is leaner & shorter, crawler is
    // squished low to the ground (no head), boss is hulking and accented.
    float w = ENEMY_RADIUS * 2.0f, h = ENEMY_HEIGHT;
    float modelScaleXZ = 1.0f, modelScaleY = 1.0f;
    bool drawHead = true;
    Color stripe = (Color){0, 0, 0, 0};
    switch (e->type) {
        case ZT_RUNNER:
            w *= 0.85f; h *= 0.95f;
            modelScaleXZ = 0.85f; modelScaleY = 0.95f;
            stripe = (Color){255, 220, 50, 255};
            break;
        case ZT_CRAWLER:
            w *= 1.0f;  h *= 0.45f;
            body.y -= h * 0.5f * 0.4f;  // lower
            modelScaleY = 0.45f;
            drawHead = false;
            break;
        case ZT_BOSS:
            w *= 1.7f; h *= 1.5f;
            body.y += (h - ENEMY_HEIGHT) * 0.5f;
            modelScaleXZ = 1.7f; modelScaleY = 1.5f;
            stripe = (Color){200, 40, 200, 255};
            break;
        default: break;
    }

    // Face the targeted player; fall back to 0 yaw if no target.
    float yawDeg = 0.0f;
    if (e->targetPlayer >= 0 && e->targetPlayer < NET_MAX_PLAYERS &&
        g_world.players[e->targetPlayer].active) {
        Vector3 t3 = g_world.players[e->targetPlayer].pos;
        float dx = t3.x - e->pos.x;
        float dz = t3.z - e->pos.z;
        // raylib draws model with rotation around (0,1,0); 0 deg = -Z fwd.
        yawDeg = atan2f(-dx, -dz) * RAD2DEG;
    }

    // Feet sit on the ground; the model origin (feet) is the scale pivot,
    // so a uniform scale keeps the soles planted for every type.
    Vector3 feet = { e->pos.x, e->pos.y - ENEMY_HEIGHT * 0.5f, e->pos.z };

    // Rigged, animated zombie (preferred): one shared skinned glTF posed
    // per-instance via a render-local AnimState.
    //   - dying (dyingTimer > 0): play 'death' one-shot, freeze on last frame.
    //   - sim attack (simAttackTimer > 0): play 'attack_a' one-shot.
    //   - otherwise: loop 'walk' at speed-scaled playback rate.
    if (zombieAnim.loaded && zClipWalk >= 0) {
        int idx = (int)(e - enemies);
        AnimState *st = &zombieAnimState[idx];
        bool dying = (e->dyingTimer > 0 && !e->alive);

        if (dying && zClipDeath >= 0) {
            // Start the death clip once; let it run to completion and hold.
            if (st->clip != zClipDeath)
                Anim_Play(st, zClipDeath, false, 1.0f);
            // Once finished, keep time pinned at the last frame (corpse pose).
        } else if (!dying && e->simAttackTimer > 0 && zClipAttack >= 0) {
            // Sim-driven attack: start the one-shot if not already playing.
            if (st->clip != zClipAttack || st->finished)
                Anim_Play(st, zClipAttack, false, 1.2f);
        } else if (!dying) {
            // Playback rate tracks how fast this zombie is moving so the
            // shamble reads faster for runners / slower while stunned.
            float pb = e->speed > 0.05f ? Clamp(e->speed / 1.3f, 0.45f, 2.4f)
                                        : 0.55f;
            if (st->clip != zClipWalk) Anim_Play(st, zClipWalk, true, pb);
            else st->speed = pb;
        }
        // Uniform scale (the rigged mesh has one shared skeleton; per-type
        // non-uniform squish awaits dedicated crawler/boss meshes). Crawler
        // is kept small so it still reads as a distinct, low type.
        float animScale = (e->type == ZT_CRAWLER) ? 0.7f : modelScaleXZ;
        Anim_Update(&zombieAnim, st, GetFrameTime());
        Anim_Draw(&zombieAnim, st, feet, yawDeg, animScale, hpTint);

        if (stripe.a) {
            Eng_GfxDrawCube((Vector3){body.x, body.y, body.z},
                     w * 1.02f, h * 0.10f, w * 1.02f, stripe);
        }
        if (runnerWindup) {
            float headY = body.y + h * 0.5f + 0.20f;
            float fwdDx = sinf(yawDeg * DEG2RAD);
            float fwdDz = -cosf(yawDeg * DEG2RAD);
            float perpX = -fwdDz, perpZ = fwdDx;
            float pulse = 0.6f + 0.4f * sinf((0.75f - e->specialTimer) * 30.0f);
            unsigned char r = (unsigned char)(255.0f * pulse);
            Color eye = (Color){ r, 30, 30, 255 };
            Eng_GfxDrawSphere((Vector3){ body.x + fwdDx*0.18f + perpX*0.08f, headY, body.z + fwdDz*0.18f + perpZ*0.08f }, 0.045f, eye);
            Eng_GfxDrawSphere((Vector3){ body.x + fwdDx*0.18f - perpX*0.08f, headY, body.z + fwdDz*0.18f - perpZ*0.08f }, 0.045f, eye);
        }
        return;
    }

    if (propModelLoaded[PROP_ZOMBIE]) {
        // OBJ origin is at the feet, model -Z is forward (matches raylib).
        Vector3 scale = { modelScaleXZ, modelScaleY, modelScaleXZ };
        DrawPropEx(PROP_ZOMBIE, feet, yawDeg, scale, hpTint);

        if (stripe.a) {
            // Type stripe ring around the chest, drawn as a thin cube band.
            Eng_GfxDrawCube((Vector3){body.x, body.y, body.z},
                     w * 1.02f, h * 0.10f, w * 1.02f, stripe);
        }
        if (runnerWindup) {
            // Glowing red eyes at head height — pulse over the 0.2s window.
            float headY = body.y + h * 0.5f + 0.20f;
            float fwdDx = sinf(yawDeg * DEG2RAD);
            float fwdDz = -cosf(yawDeg * DEG2RAD);
            float perpX = -fwdDz, perpZ = fwdDx;
            float pulse = 0.6f + 0.4f * sinf((0.75f - e->specialTimer) * 30.0f);
            unsigned char r = (unsigned char)(255.0f * pulse);
            Color eye = (Color){ r, 30, 30, 255 };
            float eyeR = 0.045f;
            Eng_GfxDrawSphere((Vector3){ body.x + fwdDx*0.18f + perpX*0.08f, headY, body.z + fwdDz*0.18f + perpZ*0.08f }, eyeR, eye);
            Eng_GfxDrawSphere((Vector3){ body.x + fwdDx*0.18f - perpX*0.08f, headY, body.z + fwdDz*0.18f - perpZ*0.08f }, eyeR, eye);
        }
        return;
    }

    // Procedural fallback (no model loaded)
    Eng_GfxDrawCube(body, w, h, w, hpTint);
    Eng_GfxDrawCubeWires(body, w, h, w, BLACK);
    if (stripe.a) {
        Eng_GfxDrawCube((Vector3){body.x, body.y, body.z}, w * 1.02f, h * 0.10f, w * 1.02f, stripe);
    }
    if (drawHead) {
        Eng_GfxDrawSphere((Vector3){body.x, body.y + h*0.5f + 0.3f, body.z},
                   (e->type == ZT_BOSS ? 0.45f : 0.28f), hpTint);
    }
}

static void DrawOtherPlayer(int idx) {
    Player *p = &g_world.players[idx];
    if (!p->active) return;
    // p->pos.y is eye height (groundY + PLAYER_EYE); the model origin is at the
    // feet, so the floor under this player is pos.y - PLAYER_EYE. On flat maps
    // that's 0 (unchanged); on multi-floor maps it puts the body on the right
    // deck instead of the ground plane.
    float floorY = p->pos.y - PLAYER_EYE;
    Color c = PLAYER_COLORS[idx];
    if (!p->alive)      c = (Color){ 100,100,100, 200 };
    else if (p->downed) c = (Color){ 180, 60, 60, 230 };

    // Rigged, animated soldier (preferred): one shared skinned glTF posed
    // per-player via a render-local AnimState, with the clip reconstructed
    // from the synced fields. Falls back to the OBJ / cube draws below.
    if (playerAnim.loaded && pmIdle >= 0) {
        AnimState *st = &playerAnimState[idx];

        // Smoothed horizontal speed from the synced position. Snapshots land
        // at 20 Hz but we render at ~60 fps, so the per-frame delta spikes on
        // snapshot frames; an EMA of dist/dt recovers the true average speed.
        float dt = GetFrameTime();
        if (dt < 1e-4f) dt = 1e-4f;
        if (playerAnimSeen[idx]) {
            float dx = p->pos.x - playerAnimPrevPos[idx].x;
            float dz = p->pos.z - playerAnimPrevPos[idx].z;
            float inst = sqrtf(dx*dx + dz*dz) / dt;
            if (inst > 14.0f) inst = 14.0f;   // clamp snapshot jumps / teleports
            playerAnimSpeed[idx] += (inst - playerAnimSpeed[idx]) * 0.25f;
        }
        playerAnimPrevPos[idx] = p->pos;
        playerAnimSeen[idx] = true;
        float spd = playerAnimSpeed[idx];

        // Reviving a teammate? reviverIdx is set on the DOWNED player ("who is
        // reviving me") and is now serialized, so this is authoritative — a
        // downed teammate pointing back at us means we're the reviver. No more
        // proximity guessing (which false-fired when two players stood close).
        bool reviving = false;
        if (p->alive && !p->downed) {
            for (int j = 0; j < NET_MAX_PLAYERS; j++) {
                Player *d = &g_world.players[j];
                if (j == idx || !d->active || !d->downed) continue;
                if (d->reviverIdx == idx) { reviving = true; break; }
            }
        }

        int cs = p->currentSlot;
        bool reloading = (cs >= 0 && cs < INV_SLOTS &&
                          p->inventory[cs].reloadTimer > 0.0f);

        // Clip selection, highest priority first.
        if (!p->alive) {
            if (st->clip != pmDeath && pmDeath >= 0) Anim_Play(st, pmDeath, false, 1.0f);
        } else if (p->downed) {
            if (st->clip != pmDowned && pmDowned >= 0) Anim_Play(st, pmDowned, true, 1.0f);
        } else if (reviving && pmRevive >= 0) {
            if (st->clip != pmRevive) Anim_Play(st, pmRevive, true, 1.0f);
        } else if (reloading && pmReload >= 0) {
            // Scale playback so the clip fits the (possibly Speed-Cola'd) timer.
            float rt = p->inventory[cs].reloadTimer;
            float cd = Anim_ClipDuration(&playerAnim, pmReload);
            float ps = (rt > 0.05f && cd > 0.0f) ? Clamp(cd / rt, 0.4f, 3.0f) : 1.0f;
            if (st->clip != pmReload) Anim_Play(st, pmReload, false, ps);
            else st->speed = ps;
        } else if (p->fireHeld && pmFire >= 0) {
            // fireHeld is serialized in the snapshot now, so teammates' models
            // actually play the fire clip while they're shooting.
            if (st->clip != pmFire || st->finished) Anim_Play(st, pmFire, false, 1.0f);
        } else if (spd > 8.5f && pmRun >= 0) {
            float ps = Clamp(spd / 9.5f, 0.7f, 1.6f);
            if (st->clip != pmRun) Anim_Play(st, pmRun, true, ps); else st->speed = ps;
        } else if (spd > 0.8f && pmWalk >= 0) {
            float ps = Clamp(spd / 5.0f, 0.6f, 2.2f);
            if (st->clip != pmWalk) Anim_Play(st, pmWalk, true, ps); else st->speed = ps;
        } else {
            if (st->clip != pmIdle) Anim_Play(st, pmIdle, true, 1.0f);
        }

        Anim_Update(&playerAnim, st, dt);

        // Model origin is at the feet; authored +Y-in-Blender → -Z forward in
        // raylib, so the camera-yaw mapping matches the OBJ path below. The
        // downed/death clips lay the body down themselves (no tilt hack).
        Vector3 feet = { p->pos.x, floorY, p->pos.z };
        float yawDeg = -p->yaw * RAD2DEG;
        // Team-colour wash: lighten toward white so the soldier keeps its
        // material detail while still reading as the player's team colour.
        // Dead/downed keep the existing grey/red `c`.
        Color tint = c;
        if (p->alive && !p->downed)
            tint = (Color){ (unsigned char)(128 + c.r/2),
                            (unsigned char)(128 + c.g/2),
                            (unsigned char)(128 + c.b/2), 255 };
        Anim_Draw(&playerAnim, st, feet, yawDeg, 1.0f, tint);
        return;
    }

    if (propModelLoaded[PROP_PLAYER_M]) {
        // Model origin is at the feet — same convention as the zombie.
        Vector3 feet = { p->pos.x, floorY, p->pos.z };
        float yawDeg = -p->yaw * RAD2DEG; // model -Z forward matches camera yaw
        if (p->alive && p->downed) {
            // Tilt forward 90° around X to read as prone.  raylib's
            // Eng_GfxDrawModelEx only takes a yaw, so we do this with a custom
            // matrix by leaning the feet pos forward and using a +X-axis
            // rotation via a quick model-transform poke.
            Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
            Vector3 lieAt = Vector3Add(feet, Vector3Scale(fwd, 0.4f));
            lieAt.y = floorY + 0.05f;
            // Quarter-turn the model on its face by rotating around its
            // forward axis (yaw spec at base + 90 pitch via the existing
            // transform stack).  Since Eng_GfxDrawModelEx rotates around Y only,
            // approximate by drawing slightly squashed.
            DrawPropEx(PROP_PLAYER_M, lieAt, yawDeg,
                       (Vector3){1.0f, 0.45f, 1.0f}, c);
        } else {
            DrawProp(PROP_PLAYER_M, feet, yawDeg, c);
        }
        return;
    }

    // Fallback: cube + sphere silhouette.
    if (p->alive && p->downed) {
        Vector3 body = { p->pos.x, floorY + 0.30f, p->pos.z };
        Eng_GfxDrawCube(body, 1.6f, 0.45f, 0.55f, c);
        Eng_GfxDrawCubeWires(body, 1.6f, 0.45f, 0.55f, BLACK);
        Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
        Vector3 head = Vector3Add(body, Vector3Scale(fwd, 0.7f));
        head.y += 0.1f;
        Eng_GfxDrawSphere(head, 0.25f, c);
        return;
    }

    Vector3 body = { p->pos.x, floorY + ENEMY_HEIGHT*0.5f, p->pos.z };
    Eng_GfxDrawCube(body, 0.55f, 1.6f, 0.55f, c);
    Eng_GfxDrawCubeWires(body, 0.55f, 1.6f, 0.55f, BLACK);
    Eng_GfxDrawSphere((Vector3){ body.x, body.y + 0.95f, body.z }, 0.28f, c);
    Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
    Vector3 nose = Vector3Add((Vector3){body.x, body.y + 0.5f, body.z}, Vector3Scale(fwd, 0.45f));
    Eng_GfxDrawSphere(nose, 0.10f, BLACK);
}

static const Color POWERUP_COLORS[PU_COUNT] = {
    {120, 200, 240, 255},   // MAX_AMMO  - cyan
    {120, 120, 240, 255},   // NUKE      - blue
    {240, 220,  60, 255},   // DOUBLE_PT - yellow
    {240,  80,  80, 255},   // INSTAKILL - red
    { 80, 200, 120, 255},   // CARPENTER - green
};

static const char *POWERUP_LETTERS[PU_COUNT] = { "A", "N", "2x", "X", "C" };

static void DrawMysteryBox(void) {
    if (!g_world.mbox.placed) return;
    Vector3 b = g_world.mbox.pos;
    if (propModelLoaded[PROP_MYSTERY_BOX]) {
        // mystery_box.obj has its origin at the centre of the base, but
        // mbox.pos is the centre of the 1.0 m tall crate (Eng_GfxDrawCube's
        // anchor) — offset down by half a height so the new model lines
        // up with where the cube used to sit.
        Vector3 modelPos = { b.x, b.y - 0.5f, b.z };
        Color tint = (g_world.mbox.state == MBOX_IDLE) ? WHITE : (Color){255, 220, 180, 255};
        DrawProp(PROP_MYSTERY_BOX, modelPos, 0.0f, tint);
    } else {
        Color crate = (g_world.mbox.state == MBOX_IDLE) ? (Color){120, 80, 50, 255} : (Color){160, 100, 60, 255};
        Eng_GfxDrawCube(b, 1.8f, 1.0f, 1.2f, crate);
        Eng_GfxDrawCubeWires(b, 1.8f, 1.0f, 1.2f, BLACK);
        // Yellow lid for visibility
        Eng_GfxDrawCube((Vector3){ b.x, b.y + 0.55f, b.z }, 1.8f, 0.1f, 1.2f, (Color){240, 200, 80, 255});
    }

    if (g_world.mbox.state == MBOX_ROLLING || g_world.mbox.state == MBOX_WAITING) {
        Vector3 wp = { b.x, b.y + 1.4f + sinf(g_world.mbox.bob * 2.0f) * 0.1f, b.z };
        DrawWeaponDisplay(g_world.mbox.showingWeapon, wp, g_world.mbox.bob * 60.0f, 1.0f);
    }
}

static void DrawThrowables(void) {
    for (int i = 0; i < MAX_THROWABLES; i++) {
        Throwable *t = &throwables[i];
        if (!t->alive) continue;
        // Frag: dark green metal ball with a glowing fuse cap; faster pulse
        // as the fuse runs down so the throw cooks the player's eyes.
        // Stun: matte black puck with cyan band.
        if (t->kind == TH_FRAG) {
            float urge = (FRAG_FUSE - t->fuse) / FRAG_FUSE;   // 0..1
            if (urge < 0.0f) urge = 0.0f;
            if (urge > 1.0f) urge = 1.0f;
            float pulse = 0.5f + 0.5f * sinf((float)GetTime() * (6.0f + 18.0f * urge));
            unsigned char gr = (unsigned char)(160 + (unsigned)(95 * pulse * urge));
            Eng_GfxDrawSphere(t->pos, THROWABLE_RADIUS * 1.2f, (Color){40, 60, 40, 255});
            Eng_GfxDrawSphere((Vector3){t->pos.x, t->pos.y + 0.10f, t->pos.z},
                       THROWABLE_RADIUS * 0.55f, (Color){gr, 80, 30, 255});
        } else {
            Eng_GfxDrawSphere(t->pos, THROWABLE_RADIUS * 1.2f, (Color){25, 25, 30, 255});
            Eng_GfxDrawSphere((Vector3){t->pos.x, t->pos.y + 0.08f, t->pos.z},
                       THROWABLE_RADIUS * 0.5f, (Color){80, 200, 240, 255});
        }
    }
}

static void DrawPowerUps(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        Vector3 p = powerUps[i].pos;
        p.y += sinf(powerUps[i].bob) * 0.15f;
        Color c = POWERUP_COLORS[powerUps[i].type];
        if (propModelLoaded[PROP_POWERUP_DROP]) {
            // Slowly spin the drop so each face catches the light, and
            // multiply-tint with the per-type colour so each category
            // still reads at a glance.
            float yaw = powerUps[i].bob * 30.0f;
            DrawProp(PROP_POWERUP_DROP, p, yaw, c);
        } else {
            Eng_GfxDrawCube(p, 0.6f, 0.6f, 0.6f, c);
            Eng_GfxDrawCubeWires(p, 0.6f, 0.6f, 0.6f, BLACK);
            Eng_GfxDrawSphere((Vector3){p.x, p.y + 0.55f, p.z}, 0.08f, WHITE);
        }
    }
}

// Draw the procedural night sky: a cube centred on the camera with depth
// writes disabled so it always sits behind every world-space object.
static void DrawSkybox(Camera camera) {
    if (!skyShaderLoaded) return;
    Eng_GfxBackfaceCull(false);
    Eng_GfxDepthMask(false);
    Eng_GfxDrawModel(skyModel, camera.position, 1.0f, WHITE);
    Eng_GfxDepthMask(true);
    Eng_GfxBackfaceCull(true);
}

// Push the current lighting globals into the engine render module, then bind
// the world shader so textured walls/floor and models go through the fog
// program.  EndWorldShader restores the default.
static void BeginWorldShader(void) {
    Eng_RenderSetLighting((EngLighting){
        .fogStart    = fogStart,
        .fogEnd      = fogEnd,
        .fogColor    = fogColor,
        .sunDir      = sunDir,
        .sunColor    = sunColor,
        .ambientColor= ambientColor,
    });
    Eng_RenderBeginWorld();
}
static void EndWorldShader(void) {
    Eng_RenderEndWorld();
}

// Speed → tracer colour. Avoids serialising the firing weapon on every
// bullet; values here mirror the WEAPONS[] bulletSpeed entries.
static Color TracerColor(float speed) {
    if (speed < 120.0f) return (Color){ 130, 255, 170, 255 };  // raygun (green)
    if (speed < 260.0f) return (Color){ 255, 170,  70, 255 };  // shotgun (orange)
    if (speed < 380.0f) return (Color){ 255, 215, 130, 255 };  // pistol / smg (yellow)
    return                       (Color){ 255, 230, 170, 255 };  // rifle (warm white)
}

// Billboard-aligned streak between (head - vel * tail) and head. Bright at
// the head, transparent at the tail.
static void DrawTracers(Camera camera) {
    Eng_GfxBackfaceCull(false);
    Eng_GfxBeginQuads(0);
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) continue;
        Vector3 head = bullets[i].pos;
        float speed = Vector3Length(bullets[i].vel);
        if (speed < 1e-3f) continue;
        Vector3 dirN  = Vector3Scale(bullets[i].vel, 1.0f / speed);
        float tailLen = speed * 0.018f;
        if (tailLen > BULLET_TAIL_MAX) tailLen = BULLET_TAIL_MAX;
        if (tailLen < 0.20f)           tailLen = 0.20f;
        Vector3 tail = Vector3Subtract(head, Vector3Scale(dirN, tailLen));

        Vector3 mid   = Vector3Scale(Vector3Add(head, tail), 0.5f);
        Vector3 toCam = Vector3Subtract(camera.position, mid);
        Vector3 side  = Vector3CrossProduct(dirN, toCam);
        float sl = Vector3Length(side);
        if (sl < 1e-4f) continue;
        side = Vector3Scale(side, 1.0f / sl);

        float w = (speed < 100.0f) ? 0.10f : 0.055f;  // raygun fatter than ballistic
        Color cHead = TracerColor(speed);
        Color cTail = (Color){ cHead.r, cHead.g, cHead.b, 0 };
        Color cCore = (Color){ 255, 255, 255, cHead.a };

        Vector3 hr = Vector3Add(head, Vector3Scale(side,  w));
        Vector3 hl = Vector3Add(head, Vector3Scale(side, -w));
        Vector3 tr = Vector3Add(tail, Vector3Scale(side,  w));
        Vector3 tl = Vector3Add(tail, Vector3Scale(side, -w));

        Eng_GfxColor(cCore); Eng_GfxVertex(hr.x, hr.y, hr.z);
        Eng_GfxColor(cCore); Eng_GfxVertex(hl.x, hl.y, hl.z);
        Eng_GfxColor(cTail); Eng_GfxVertex(tl.x, tl.y, tl.z);
        Eng_GfxColor(cTail); Eng_GfxVertex(tr.x, tr.y, tr.z);
    }
    Eng_GfxEndQuads();
    Eng_GfxBackfaceCull(true);
}

// Post-FX pass — thin wrappers over the engine render module.
// The RT lifecycle, composite quad, and shader uniform push all live in
// src/engine/eng_render.c now.  The game just passes its per-frame params.

void Render_BeginPostFX(void) {
    Eng_RenderBeginPostFX();
}

void Render_EndPostFX(float hitFlash, float lowHp) {
    Eng_RenderEndPostFX((EngPostFxParams){
        .hitFlash = hitFlash,
        .lowHp    = lowHp,
        .time     = (float)GetTime(),
    });
}

void Render_UnloadPostFX(void) {
    // Engine-owned RT is released inside Assets_Unload → Eng_RenderUnloadPostFX.
    // This function is kept for callers that explicitly want to release the RT
    // (e.g. devtools) and is now a no-op — the engine manages the lifetime.
    (void)0;
}

void Render_World3D(Camera camera) {
    Eng_GfxBeginMode3D(camera);
        DrawSkybox(camera);
        BeginWorldShader();
        DrawArena();
        DrawMapProps();
        DrawInteriorWalls();
        DrawDoors();
        DrawWindows();
        DrawWallBuys();
        DrawPerkMachines();
        DrawPaP();
        DrawMysteryBox();
        DrawPowerUps();
        DrawThrowables();
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            // Your own body is normally hidden (you're the camera). While
            // noclipping it's drawn too, so the detached fly camera can see the
            // body left standing where you entered noclip.
            if (i == localPlayerIdx && !noclipMode) continue;
            DrawOtherPlayer(i);
        }
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].alive || enemies[i].dyingTimer > 0) DrawEnemy(&enemies[i]);
        }
        Decals_Draw();
        DrawTracers(camera);
        // Particles: drawn inside Eng_GfxBeginMode3D but outside the world shader so
        // their blend modes (additive / alpha) aren't affected by the fog program.
        // Same discipline as decals/tracers: rlDisableDepthMask during draw,
        // restored after. Particles_Update is called here so it drives off the
        // render-frame dt, consistent with every other visual-only effect.
        EndWorldShader();
        Particles_Update(GetFrameTime());
        Particles_Draw(camera);
        Viewmodel_DrawFirstPerson(camera);
    Eng_GfxEndMode3D();
}

// Labels are only drawn when the player is close enough, and fade smoothly
// over the last few metres so they don't pop on/off.
#define LABEL_FULL_DIST  6.0f
#define LABEL_FADE_DIST 12.0f

static float LabelAlpha(Vector3 from, Vector3 target) {
    float dx = from.x - target.x, dz = from.z - target.z;
    float d = sqrtf(dx*dx + dz*dz);
    if (d <= LABEL_FULL_DIST) return 1.0f;
    if (d >= LABEL_FADE_DIST) return 0.0f;
    return 1.0f - (d - LABEL_FULL_DIST) / (LABEL_FADE_DIST - LABEL_FULL_DIST);
}

static Color FadeColor(Color c, float a) {
    c.a = (unsigned char)(c.a * a);
    return c;
}

void Render_WorldLabels(Camera camera, int sw, int sh, Player *me) {
    Vector3 mePos = me->pos;

    for (int i = 0; i < wallBuyCount; i++) {
        WallBuy *wb = &wallBuys[i];
        float a = LabelAlpha(mePos, wb->pos);
        if (a <= 0) continue;
        const WeaponDef *w = &WEAPONS[wb->weaponIdx];
        Vector3 above = { wb->pos.x, wb->pos.y + 1.2f, wb->pos.z };
        Vector3 toCam = Vector3Subtract(camera.position, wb->pos);
        if (Vector3DotProduct(toCam, wb->normal) <= 0) continue;
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -100 || sp.y < -100 || sp.x > sw + 100 || sp.y > sh + 100) continue;
        int owned = Weapon_FindOwnedSlot(me, wb->weaponIdx);
        char label[64];
        snprintf(label, sizeof label, "%s  %d",
                 w->name, (owned >= 0) ? w->ammoPrice : w->buyPrice);
        int lw = MeasureText(label, 18);
        DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24,
                      FadeColor((Color){0,0,0,160}, a));
        DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, FadeColor(w->tint, a));
    }
    for (int i = 0; i < perkMachineCount; i++) {
        PerkMachine *pm = &perkMachines[i];
        float a = LabelAlpha(mePos, pm->pos);
        if (a <= 0) continue;
        const PerkDef *pd = &PERKS[pm->perkIdx];
        Vector3 above = { pm->pos.x, 3.3f, pm->pos.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -200 || sp.y < -100 || sp.x > sw + 200 || sp.y > sh + 100) continue;
        char label[64];
        if (me->hasPerk[pm->perkIdx]) snprintf(label, sizeof label, "%s", pd->name);
        else                          snprintf(label, sizeof label, "%s  %d", pd->name, pd->cost);
        int lw = MeasureText(label, 18);
        DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24,
                      FadeColor((Color){0,0,0,160}, a));
        DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18,
                 FadeColor(me->hasPerk[pm->perkIdx] ? GRAY : pd->tint, a));
    }
    {
        float a = LabelAlpha(mePos, g_world.pap.pos);
        if (a > 0) {
            Vector3 above = { g_world.pap.pos.x, 3.4f, g_world.pap.pos.z };
            Vector2 sp = GetWorldToScreen(above, camera);
            if (sp.x > -200 && sp.x < sw + 200 && sp.y > -200 && sp.y < sh + 200) {
                char label[64];
                snprintf(label, sizeof label, "PACK-A-PUNCH  %d", PAP_COST);
                int lw = MeasureText(label, 18);
                DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24,
                              FadeColor((Color){0,0,0,160}, a));
                DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18,
                         FadeColor((Color){200,150,255,255}, a));
            }
        }
    }
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].opened) continue;
        float a = LabelAlpha(mePos, doors[i].box.center);
        if (a <= 0) continue;
        Vector3 above = { doors[i].box.center.x,
                          doors[i].box.center.y + doors[i].box.size.y*0.5f + 0.8f,
                          doors[i].box.center.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -200 || sp.y < -100 || sp.x > sw + 200 || sp.y > sh + 100) continue;
        char label[64];
        snprintf(label, sizeof label, "DOOR  %d", doors[i].cost);
        int lw = MeasureText(label, 18);
        DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24,
                      FadeColor((Color){0,0,0,160}, a));
        DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18,
                 FadeColor((Color){220,170,110,255}, a));
    }
    // Power-up labels (single letter — keep visible a bit further than the
    // full prompts since they communicate type at a glance).
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        float dx = mePos.x - powerUps[i].pos.x;
        float dz = mePos.z - powerUps[i].pos.z;
        float d = sqrtf(dx*dx + dz*dz);
        if (d > 18.0f) continue;
        float a = (d < 10.0f) ? 1.0f : 1.0f - (d - 10.0f) / 8.0f;
        Vector3 above = { powerUps[i].pos.x, powerUps[i].pos.y + 1.0f, powerUps[i].pos.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -100 || sp.y < -100 || sp.x > sw + 100 || sp.y > sh + 100) continue;
        const char *lbl = POWERUP_LETTERS[powerUps[i].type];
        int lw = MeasureText(lbl, 22);
        DrawText(lbl, (int)sp.x - lw/2, (int)sp.y - 10, 22,
                 FadeColor(POWERUP_COLORS[powerUps[i].type], a));
    }
}
