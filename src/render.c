#include "render.h"
#include "level.h"
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "entities.h"
#include "assets.h"
#include "decals.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>

float muzzleFlashLocal = 0.0f;

static const Color PLAYER_COLORS[NET_MAX_PLAYERS] = {
    {220, 80, 80, 255}, {80, 120, 220, 255}, {90, 200, 90, 255}, {220, 200, 80, 255},
};

// Shorthand for the model-first / cube-fallback pattern that every
// prop draw uses below. `propModelLoaded[id]` is the only switch — every
// site simply tries the model first and falls back to a primitive draw
// only when the OBJ hasn't been authored yet.
static inline void DrawPropEx(PropId id, Vector3 pos, float yawDeg, Vector3 scale, Color tint) {
    DrawModelEx(propModels[id], pos, (Vector3){0, 1, 0}, yawDeg, scale, tint);
}
static inline void DrawProp(PropId id, Vector3 pos, float yawDeg, Color tint) {
    DrawPropEx(id, pos, yawDeg, (Vector3){1, 1, 1}, tint);
}

// ---- textured surfaces -------------------------------------------------
// Walls and floors are procedural geometry with tiled textures.  Helpers
// below emit textured quads via rlgl immediate mode so each surface gets
// UVs scaled by `size / TILE_SIZE` and the texture repeats seamlessly
// across the box face.  Caller supplies a fallback colour for when the
// texture isn't loaded.

static inline void TexQuad(float ax, float ay, float az,
                           float bx, float by, float bz,
                           float cx, float cy, float cz,
                           float dx, float dy, float dz,
                           float u, float v) {
    // Four-vertex quad (CCW from outside).  Texture spans U×V repeats.
    rlTexCoord2f(0, v); rlVertex3f(ax, ay, az);
    rlTexCoord2f(u, v); rlVertex3f(bx, by, bz);
    rlTexCoord2f(u, 0); rlVertex3f(cx, cy, cz);
    rlTexCoord2f(0, 0); rlVertex3f(dx, dy, dz);
}

// Flush whatever rlgl immediate verts are pending under the *current*
// tileVariation value, switch the uniform on/off, and ensure the next
// flush picks up the new value. Called as a bookend around the textured
// box/floor emitters so the macro-variation overlay only colours tiled
// world geometry — not the grid lines, debug cubes, or anything else
// sharing the immediate-mode batch.
static void BeginTileVariation(void) {
    if (!worldShaderLoaded) return;
    rlDrawRenderBatchActive();
    float v = 1.0f;
    SetShaderValue(worldShader, worldShader_tileVariationLoc, &v, SHADER_UNIFORM_FLOAT);
}
static void EndTileVariation(void) {
    if (!worldShaderLoaded) return;
    rlDrawRenderBatchActive();
    float v = 0.0f;
    SetShaderValue(worldShader, worldShader_tileVariationLoc, &v, SHADER_UNIFORM_FLOAT);
}

// Draw a textured axis-aligned box.  Each face's UVs span the face's
// world-space size divided by TILE_SIZE, so the texture tiles
// continuously across walls of any length.
static void DrawTexturedBox(Vector3 c, Vector3 s, TextureId tid,
                            Color tint, Color fallback) {
    if (!textureLoaded[tid]) {
        DrawCubeV(c, s, fallback);
        DrawCubeWiresV(c, s, BLACK);
        return;
    }
    float hx = s.x*0.5f, hy = s.y*0.5f, hz = s.z*0.5f;
    float ux = s.x / TILE_SIZE;
    float uy = s.y / TILE_SIZE;
    float uz = s.z / TILE_SIZE;

    BeginTileVariation();
    rlSetTexture(textures[tid].id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);

    // +Z face
    rlNormal3f(0, 0, 1);
    TexQuad(c.x-hx, c.y-hy, c.z+hz,  c.x+hx, c.y-hy, c.z+hz,
            c.x+hx, c.y+hy, c.z+hz,  c.x-hx, c.y+hy, c.z+hz,  ux, uy);
    // -Z face
    rlNormal3f(0, 0, -1);
    TexQuad(c.x+hx, c.y-hy, c.z-hz,  c.x-hx, c.y-hy, c.z-hz,
            c.x-hx, c.y+hy, c.z-hz,  c.x+hx, c.y+hy, c.z-hz,  ux, uy);
    // +X face
    rlNormal3f(1, 0, 0);
    TexQuad(c.x+hx, c.y-hy, c.z+hz,  c.x+hx, c.y-hy, c.z-hz,
            c.x+hx, c.y+hy, c.z-hz,  c.x+hx, c.y+hy, c.z+hz,  uz, uy);
    // -X face
    rlNormal3f(-1, 0, 0);
    TexQuad(c.x-hx, c.y-hy, c.z-hz,  c.x-hx, c.y-hy, c.z+hz,
            c.x-hx, c.y+hy, c.z+hz,  c.x-hx, c.y+hy, c.z-hz,  uz, uy);
    // +Y face (top)
    rlNormal3f(0, 1, 0);
    TexQuad(c.x-hx, c.y+hy, c.z+hz,  c.x+hx, c.y+hy, c.z+hz,
            c.x+hx, c.y+hy, c.z-hz,  c.x-hx, c.y+hy, c.z-hz,  ux, uz);
    // -Y face (bottom)
    rlNormal3f(0, -1, 0);
    TexQuad(c.x-hx, c.y-hy, c.z-hz,  c.x+hx, c.y-hy, c.z-hz,
            c.x+hx, c.y-hy, c.z+hz,  c.x-hx, c.y-hy, c.z+hz,  ux, uz);

    rlEnd();
    rlSetTexture(0);
    EndTileVariation();
}

// Textured XZ plane centred at `center` of size `sizeX × sizeZ`, top
// face pointing +Y.  Used for the arena floor and outside ground.
static void DrawTexturedFloor(Vector3 center, float sizeX, float sizeZ,
                              TextureId tid, Color tint, Color fallback) {
    if (!textureLoaded[tid]) {
        DrawPlane(center, (Vector2){sizeX, sizeZ}, fallback);
        return;
    }
    float hx = sizeX*0.5f, hz = sizeZ*0.5f;
    float u = sizeX / TILE_SIZE;
    float v = sizeZ / TILE_SIZE;

    BeginTileVariation();
    rlSetTexture(textures[tid].id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0, 1, 0);
    TexQuad(center.x-hx, center.y, center.z+hz,
            center.x+hx, center.y, center.z+hz,
            center.x+hx, center.y, center.z-hz,
            center.x-hx, center.y, center.z-hz,  u, v);
    rlEnd();
    rlSetTexture(0);
    EndTileVariation();
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

static void DrawArena(void) {
    DrawTexturedFloor((Vector3){0,0,0}, ARENA_HALF*2, ARENA_HALF*2,
                      TEX_FLOOR, WHITE, (Color){55,65,75,255});
    DrawTexturedFloor((Vector3){0, -0.01f, 0}, ARENA_HALF*2 + 16, ARENA_HALF*2 + 16,
                      TEX_GROUND, WHITE, (Color){30,35,40,255});

    Color wc = (Color){90,100,110,255};
    float a = ARENA_HALF;
    for (int side = 0; side < 4; side++) {
        float gapPos = 0; bool hasGap = false;
        for (int i = 0; i < windowCount; i++) {
            if (side == 0 && windows[i].pos.z >  a - 0.5f) { gapPos = windows[i].pos.x; hasGap = true; }
            if (side == 1 && windows[i].pos.z < -a + 0.5f) { gapPos = windows[i].pos.x; hasGap = true; }
            if (side == 2 && windows[i].pos.x >  a - 0.5f) { gapPos = windows[i].pos.z; hasGap = true; }
            if (side == 3 && windows[i].pos.x < -a + 0.5f) { gapPos = windows[i].pos.z; hasGap = true; }
        }
        float gMin = gapPos - WINDOW_WIDTH*0.5f, gMax = gapPos + WINDOW_WIDTH*0.5f;
        if (side == 0) { if (hasGap) { DrawWallXSeg(-a, gMin,  a, wc); DrawWallXSeg(gMax, a,  a, wc); } else DrawWallXSeg(-a, a,  a, wc); }
        else if (side == 1) { if (hasGap) { DrawWallXSeg(-a, gMin, -a, wc); DrawWallXSeg(gMax, a, -a, wc); } else DrawWallXSeg(-a, a, -a, wc); }
        else if (side == 2) { if (hasGap) { DrawWallZSeg(-a, gMin,  a, wc); DrawWallZSeg(gMax, a,  a, wc); } else DrawWallZSeg(-a, a,  a, wc); }
        else                 { if (hasGap) { DrawWallZSeg(-a, gMin, -a, wc); DrawWallZSeg(gMax, a, -a, wc); } else DrawWallZSeg(-a, a, -a, wc); }
    }

    for (int i = 0; i < obstacleCount; i++) {
        if (propModelLoaded[PROP_OBSTACLE_CRATE]) {
            // crate.obj is unit-cube; scale to the obstacle's box size.
            DrawPropEx(PROP_OBSTACLE_CRATE,
                       obstacles[i].center, 0.0f,
                       obstacles[i].size, WHITE);
        } else {
            DrawCubeV(obstacles[i].center, obstacles[i].size, (Color){120,90,70,255});
            DrawCubeWiresV(obstacles[i].center, obstacles[i].size, (Color){40,30,20,255});
        }
    }
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
            DrawCubeV(mp->collider.center, mp->collider.size, (Color){140,110,80,255});
            DrawCubeWiresV(mp->collider.center, mp->collider.size, BLACK);
        }
    }
}

static void DrawInteriorWalls(void) {
    for (int i = 0; i < interiorWallCount; i++) {
        DrawTexturedBox(interiorWalls[i].center, interiorWalls[i].size,
                        TEX_WALL_INT, WHITE, (Color){80, 90, 100, 255});
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
            DrawCubeV(b.center, b.size, (Color){130, 80, 50, 255});
            DrawCubeWiresV(b.center, b.size, (Color){40, 25, 15, 255});
        }
    }
}

static void DrawWindows(void) {
    for (int i = 0; i < windowCount; i++) {
        Window3D *w = &windows[i];
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
                DrawCubeV(planeCenter, planeSize, (Color){150, 100, 50, 255});
                DrawCubeWiresV(planeCenter, planeSize, (Color){60, 40, 20, 255});
            }
        }
    }
}

// Draw a weapon at pos with given yaw (degrees). If a Model is loaded for the
// weapon, use it; otherwise fall back to the old colored cube. The weapon OBJs
// are authored at true real-world size, so `displayScale` is the literal world
// scale: 1.0 = life-size. It is deliberately NOT multiplied by
// `weaponTune.scale` — that knob is the first-person viewmodel framing scale
// (tuned for the gun held close to the camera) and has no business sizing the
// gun in the world, where it was making wall-buy/box guns ~10× too big.
static void DrawWeaponDisplay(int weaponIdx, Vector3 pos, float yawDeg, float displayScale) {
    if (weaponIdx >= 0 && weaponIdx < W_COUNT && weaponModelLoaded[weaponIdx]) {
        float s = displayScale;
        DrawModelEx(weaponModels[weaponIdx], pos,
                    (Vector3){0, 1, 0},
                    yawDeg + weaponTune[weaponIdx].yawDeg,
                    (Vector3){s, s, s},
                    WHITE);
    } else {
        Vector3 size = { 0.8f * displayScale, 0.3f * displayScale, 0.2f * displayScale };
        DrawCubeV(pos, size, WEAPONS[weaponIdx].tint);
        DrawCubeWiresV(pos, size, BLACK);
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
            DrawCubeV(wb->pos, size, w->tint);
            DrawCubeWiresV(wb->pos, size, BLACK);
        }

        if (weaponModelLoaded[wb->weaponIdx]) {
            // Pop the gun off the mount plate, aligned along the wall.
            Vector3 along = (fabsf(wb->normal.x) > 0.5f)
                ? (Vector3){ 0, 0, 1 } : (Vector3){ 1, 0, 0 };
            (void)along;
            float yaw = (fabsf(wb->normal.x) > 0.5f) ? 90.0f : 0.0f;
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
            DrawCube((Vector3){ pm->pos.x, 0.6f, pm->pos.z }, 1.0f, 1.2f, 1.0f, (Color){30,30,30,255});
            DrawCube((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, pd->tint);
            DrawCubeWires((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, BLACK);
            DrawCube((Vector3){ pm->pos.x, 2.5f, pm->pos.z }, 0.6f, 0.4f, 0.6f, (Color){40,40,40,255});
            bool myOwn = players[localPlayerIdx].hasPerk[pm->perkIdx];
            DrawSphere((Vector3){ pm->pos.x, 2.95f, pm->pos.z }, 0.18f,
                       myOwn ? (Color){240,240,240,255} : pd->tint);
        }
    }
}

static void DrawPaP(void) {
    if (propModelLoaded[PROP_PAP]) {
        // Cabinet body sits on the floor at pap.pos.x/z.
        Vector3 base = { pap.pos.x, 0.0f, pap.pos.z };
        DrawProp(PROP_PAP, base, 0.0f, WHITE);
        // Bobbing upgrade chamber stays as the existing primitive draw so
        // the animation reads the same as before — the model spec leaves
        // this on top to be replaced by `pap_chamber.obj` later.
        Vector3 top = { pap.pos.x, 1.7f + sinf(pap.bob) * 0.05f, pap.pos.z };
        DrawCube(top, 1.6f, 0.5f, 1.6f, (Color){80, 50, 130, 255});
        DrawCubeWires(top, 1.6f, 0.5f, 1.6f, (Color){200,150,255,255});
        DrawSphere((Vector3){pap.pos.x, 2.2f, pap.pos.z}, 0.15f, (Color){220, 180, 255, 255});
    } else {
        DrawCube(pap.pos, 2.5f, 0.6f, 2.5f, (Color){25,25,30,255});
        DrawCubeWires(pap.pos, 2.5f, 0.6f, 2.5f, BLACK);
        DrawCube((Vector3){pap.pos.x, 1.0f, pap.pos.z}, 2.0f, 0.8f, 2.0f, (Color){50,50,60,255});
        Vector3 top = { pap.pos.x, 1.7f + sinf(pap.bob) * 0.05f, pap.pos.z };
        DrawCube(top, 1.6f, 0.5f, 1.6f, (Color){80, 50, 130, 255});
        DrawCubeWires(top, 1.6f, 0.5f, 1.6f, (Color){200,150,255,255});
        DrawSphere((Vector3){pap.pos.x, 2.2f, pap.pos.z}, 0.15f, (Color){220, 180, 255, 255});
    }

    if (pap.activeTimer > 0 && pap.ownerPlayer >= 0 && pap.slotInProgress >= 0) {
        float spin = pap.bob * 4.0f;
        Vector3 weaponPos = { pap.pos.x, 2.6f + sinf(pap.bob*2) * 0.15f, pap.pos.z };
        int w = players[pap.ownerPlayer].inventory[pap.slotInProgress].weaponIdx;
        DrawWeaponDisplay(w, weaponPos, spin * 60.0f, 1.0f);
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

    if (propModelLoaded[PROP_ZOMBIE]) {
        // OBJ origin is at the feet, model -Z is forward (matches raylib).
        // e->pos.y is the body centre, so subtract half-height to land feet.
        Vector3 feet = { body.x, body.y - ENEMY_HEIGHT * 0.5f, body.z };

        // Face the targeted player; fall back to 0 yaw if no target.
        float yawDeg = 0.0f;
        if (e->targetPlayer >= 0 && e->targetPlayer < NET_MAX_PLAYERS &&
            players[e->targetPlayer].active) {
            Vector3 t3 = players[e->targetPlayer].pos;
            float dx = t3.x - e->pos.x;
            float dz = t3.z - e->pos.z;
            // raylib draws model with rotation around (0,1,0); 0 deg = -Z fwd.
            yawDeg = atan2f(-dx, -dz) * RAD2DEG;
        }

        Vector3 scale = { modelScaleXZ, modelScaleY, modelScaleXZ };
        DrawPropEx(PROP_ZOMBIE, feet, yawDeg, scale, hpTint);

        if (stripe.a) {
            // Type stripe ring around the chest, drawn as a thin cube band.
            DrawCube((Vector3){body.x, body.y, body.z},
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
            DrawSphere((Vector3){ body.x + fwdDx*0.18f + perpX*0.08f, headY, body.z + fwdDz*0.18f + perpZ*0.08f }, eyeR, eye);
            DrawSphere((Vector3){ body.x + fwdDx*0.18f - perpX*0.08f, headY, body.z + fwdDz*0.18f - perpZ*0.08f }, eyeR, eye);
        }
        return;
    }

    // Procedural fallback (no model loaded)
    DrawCube(body, w, h, w, hpTint);
    DrawCubeWires(body, w, h, w, BLACK);
    if (stripe.a) {
        DrawCube((Vector3){body.x, body.y, body.z}, w * 1.02f, h * 0.10f, w * 1.02f, stripe);
    }
    if (drawHead) {
        DrawSphere((Vector3){body.x, body.y + h*0.5f + 0.3f, body.z},
                   (e->type == ZT_BOSS ? 0.45f : 0.28f), hpTint);
    }
}

static void DrawOtherPlayer(int idx) {
    Player *p = &players[idx];
    if (!p->active) return;
    Color c = PLAYER_COLORS[idx];
    if (!p->alive)      c = (Color){ 100,100,100, 200 };
    else if (p->downed) c = (Color){ 180, 60, 60, 230 };

    if (propModelLoaded[PROP_PLAYER_M]) {
        // Model origin is at the feet — same convention as the zombie.
        Vector3 feet = { p->pos.x, 0.0f, p->pos.z };
        float yawDeg = -p->yaw * RAD2DEG; // model -Z forward matches camera yaw
        if (p->alive && p->downed) {
            // Tilt forward 90° around X to read as prone.  raylib's
            // DrawModelEx only takes a yaw, so we do this with a custom
            // matrix by leaning the feet pos forward and using a +X-axis
            // rotation via a quick model-transform poke.
            Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
            Vector3 lieAt = Vector3Add(feet, Vector3Scale(fwd, 0.4f));
            lieAt.y = 0.05f;
            // Quarter-turn the model on its face by rotating around its
            // forward axis (yaw spec at base + 90 pitch via the existing
            // transform stack).  Since DrawModelEx rotates around Y only,
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
        Vector3 body = { p->pos.x, 0.30f, p->pos.z };
        DrawCube(body, 1.6f, 0.45f, 0.55f, c);
        DrawCubeWires(body, 1.6f, 0.45f, 0.55f, BLACK);
        Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
        Vector3 head = Vector3Add(body, Vector3Scale(fwd, 0.7f));
        head.y += 0.1f;
        DrawSphere(head, 0.25f, c);
        return;
    }

    Vector3 body = { p->pos.x, ENEMY_HEIGHT*0.5f, p->pos.z };
    DrawCube(body, 0.55f, 1.6f, 0.55f, c);
    DrawCubeWires(body, 0.55f, 1.6f, 0.55f, BLACK);
    DrawSphere((Vector3){ body.x, body.y + 0.95f, body.z }, 0.28f, c);
    Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
    Vector3 nose = Vector3Add((Vector3){body.x, body.y + 0.5f, body.z}, Vector3Scale(fwd, 0.45f));
    DrawSphere(nose, 0.10f, BLACK);
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
    if (!mbox.placed) return;
    Vector3 b = mbox.pos;
    if (propModelLoaded[PROP_MYSTERY_BOX]) {
        // mystery_box.obj has its origin at the centre of the base, but
        // mbox.pos is the centre of the 1.0 m tall crate (DrawCube's
        // anchor) — offset down by half a height so the new model lines
        // up with where the cube used to sit.
        Vector3 modelPos = { b.x, b.y - 0.5f, b.z };
        Color tint = (mbox.state == MBOX_IDLE) ? WHITE : (Color){255, 220, 180, 255};
        DrawProp(PROP_MYSTERY_BOX, modelPos, 0.0f, tint);
    } else {
        Color crate = (mbox.state == MBOX_IDLE) ? (Color){120, 80, 50, 255} : (Color){160, 100, 60, 255};
        DrawCube(b, 1.8f, 1.0f, 1.2f, crate);
        DrawCubeWires(b, 1.8f, 1.0f, 1.2f, BLACK);
        // Yellow lid for visibility
        DrawCube((Vector3){ b.x, b.y + 0.55f, b.z }, 1.8f, 0.1f, 1.2f, (Color){240, 200, 80, 255});
    }

    if (mbox.state == MBOX_ROLLING || mbox.state == MBOX_WAITING) {
        Vector3 wp = { b.x, b.y + 1.4f + sinf(mbox.bob * 2.0f) * 0.1f, b.z };
        DrawWeaponDisplay(mbox.showingWeapon, wp, mbox.bob * 60.0f, 1.0f);
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
            if (urge < 0.0f) urge = 0.0f; if (urge > 1.0f) urge = 1.0f;
            float pulse = 0.5f + 0.5f * sinf((float)GetTime() * (6.0f + 18.0f * urge));
            unsigned char gr = (unsigned char)(160 + (unsigned)(95 * pulse * urge));
            DrawSphere(t->pos, THROWABLE_RADIUS * 1.2f, (Color){40, 60, 40, 255});
            DrawSphere((Vector3){t->pos.x, t->pos.y + 0.10f, t->pos.z},
                       THROWABLE_RADIUS * 0.55f, (Color){gr, 80, 30, 255});
        } else {
            DrawSphere(t->pos, THROWABLE_RADIUS * 1.2f, (Color){25, 25, 30, 255});
            DrawSphere((Vector3){t->pos.x, t->pos.y + 0.08f, t->pos.z},
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
            DrawCube(p, 0.6f, 0.6f, 0.6f, c);
            DrawCubeWires(p, 0.6f, 0.6f, 0.6f, BLACK);
            DrawSphere((Vector3){p.x, p.y + 0.55f, p.z}, 0.08f, WHITE);
        }
    }
}

static void DrawFirstPersonViewmodel(Camera camera);

// Draw the procedural night sky: a cube centred on the camera with depth
// writes disabled so it always sits behind every world-space object.
static void DrawSkybox(Camera camera) {
    if (!skyShaderLoaded) return;
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawModel(skyModel, camera.position, 1.0f, WHITE);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

// Push fog uniforms + swap rlgl's default shader so textured walls / floor
// go through the same fog program as the loaded Models.  Returns the
// previously-active default shader id so the caller can restore it.
static void BeginWorldShader(void) {
    if (!worldShaderLoaded) return;
    float fc[4] = {
        fogColor.r / 255.0f, fogColor.g / 255.0f,
        fogColor.b / 255.0f, fogColor.a / 255.0f,
    };
    SetShaderValue(worldShader, worldShader_fogColorLoc, fc, SHADER_UNIFORM_VEC4);
    SetShaderValue(worldShader, worldShader_fogStartLoc, &fogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, worldShader_fogEndLoc,   &fogEnd,   SHADER_UNIFORM_FLOAT);
    float sd[3] = { sunDir.x, sunDir.y, sunDir.z };
    float sc[3] = { sunColor.x, sunColor.y, sunColor.z };
    float ac[3] = { ambientColor.x, ambientColor.y, ambientColor.z };
    SetShaderValue(worldShader, worldShader_sunDirLoc,       sd, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, worldShader_sunColorLoc,     sc, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, worldShader_ambientColorLoc, ac, SHADER_UNIFORM_VEC3);

    // Animated models draw through the skinned variant (their material shader
    // is worldSkinnedShader, used by DrawMesh regardless of rlSetShader), so
    // it needs the same fog/sun/ambient values pushed each frame.
    if (worldSkinnedShaderLoaded) {
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_fogColorLoc, fc, SHADER_UNIFORM_VEC4);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_fogStartLoc, &fogStart, SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_fogEndLoc,   &fogEnd,   SHADER_UNIFORM_FLOAT);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunDirLoc,       sd, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunColorLoc,     sc, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_ambientColorLoc, ac, SHADER_UNIFORM_VEC3);
    }
    rlSetShader(worldShader.id, worldShader.locs);
}
static void EndWorldShader(void) {
    if (!worldShaderLoaded) return;
    // CRITICAL: rlSetShader's second arg must point at the default shader's
    // own locs array; passing NULL leaves currentShaderLocs as NULL and
    // EndDrawing's render-batch flush dereferences it (SEGV).
    rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
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
    rlSetTexture(0);
    rlDisableBackfaceCulling();
    rlBegin(RL_QUADS);
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

        rlColor4ub(cCore.r, cCore.g, cCore.b, cCore.a); rlVertex3f(hr.x, hr.y, hr.z);
        rlColor4ub(cCore.r, cCore.g, cCore.b, cCore.a); rlVertex3f(hl.x, hl.y, hl.z);
        rlColor4ub(cTail.r, cTail.g, cTail.b, cTail.a); rlVertex3f(tl.x, tl.y, tl.z);
        rlColor4ub(cTail.r, cTail.g, cTail.b, cTail.a); rlVertex3f(tr.x, tr.y, tr.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();
}

void Render_World3D(Camera camera) {
    BeginMode3D(camera);
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
            if (i == localPlayerIdx) continue;
            DrawOtherPlayer(i);
        }
        for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) DrawEnemy(&enemies[i]);
        Decals_Draw();
        DrawTracers(camera);
        DrawFirstPersonViewmodel(camera);
        EndWorldShader();
    EndMode3D();
}

// Draws the held weapon attached to the camera so the player sees their gun
// in the corner of the view. Must be called inside an active BeginMode3D
// scope (Render_World3D handles that).
//
// Transform stack: vertex → (per-weapon yaw around model Y) → camera basis
// (model +X → world right, +Y → up, -Z → forward) → translate to anchor.
// weaponTune.yawDeg lets each weapon's authored "forward" axis be aligned
// to the camera's forward without modifying the OBJ.
static void DrawFirstPersonViewmodel(Camera camera) {
    Player *me = &players[localPlayerIdx];
    if (!me->alive || me->downed) return;
    int wi = me->inventory[me->currentSlot].weaponIdx;
    if (wi < 0 || wi >= W_COUNT) return;
    if (!weaponModelLoaded[wi]) return;

    // ---- viewmodel anim state (local to the player's render) ----
    // Swap: when the displayed weapon changes (currentSlot flip OR a
    // weapon swap into the same slot), start a 0.22s "raise from below"
    // animation. State is render-local since the viewmodel is local-only.
    static int   prev_slot = -1;
    static int   prev_wi   = -1;
    static float swap_t    = 0.0f;
    int cur_slot = me->currentSlot;
    if (cur_slot != prev_slot || wi != prev_wi) {
        swap_t = 1.0f;
        prev_slot = cur_slot;
        prev_wi   = wi;
    }
    if (swap_t > 0.0f) {
        swap_t -= GetFrameTime() / 0.22f;
        if (swap_t < 0.0f) swap_t = 0.0f;
    }
    // Reload: parabolic dip + lateral tilt over the weapon's reload time.
    float reload_dip = 0.0f;
    {
        WeaponSlot *cs = &me->inventory[cur_slot];
        const WeaponDef *cw = &WEAPONS[cs->weaponIdx];
        if (cs->reloadTimer > 0.0f && cw->reloadTime > 0.0f) {
            float t = 1.0f - cs->reloadTimer / cw->reloadTime;  // 0 -> 1
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            reload_dip = sinf(t * PI);   // 0 -> 1 -> 0
        }
    }
    // Swap raise: gun starts 0.32m below + 0.12m back, eases in
    float swap_down = swap_t * 0.32f;
    float swap_back = swap_t * 0.12f;
    // Reload dip: drops 0.18m, pushes 0.10m forward (away from screen)
    float reload_down = reload_dip * 0.18f;
    float reload_fwd  = reload_dip * 0.10f;
    // Tilt the gun (right-axis rotation) during reload — muzzle drops
    float tilt = reload_dip * 0.55f;  // radians (~31 deg peak)

    Vector3 fwd   = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, camera.up));
    Vector3 up    = Vector3CrossProduct(right, fwd);

    // Viewmodel framing scale. The per-weapon `weaponTune.scale` knob is the
    // relative size between guns; this base sets the overall in-hand size.
    // (World pickups/wall-buys are sized separately in DrawWeaponDisplay.)
    float s = 0.05f * weaponTune[wi].scale;
    Vector3 anchor = camera.position;
    anchor = Vector3Add(anchor, Vector3Scale(fwd,    0.55f - swap_back + reload_fwd));
    anchor = Vector3Add(anchor, Vector3Scale(right,  0.35f));
    anchor = Vector3Add(anchor, Vector3Scale(up,    -0.28f - reload_down - swap_down));
    anchor = Vector3Add(anchor, Vector3Scale(right,  weaponTune[wi].offset.x));
    anchor = Vector3Add(anchor, Vector3Scale(up,     weaponTune[wi].offset.y));
    anchor = Vector3Add(anchor, Vector3Scale(fwd,    weaponTune[wi].offset.z));

    // R_y(yaw) applied in model space, then camera basis applied: the column
    // for model +X = cos(y)*right + sin(y)*fwd, for model +Z = sin(y)*right
    // - cos(y)*fwd. yaw=0 leaves model -Z pointing world-forward.
    float yawRad = weaponTune[wi].yawDeg * DEG2RAD;
    float cy = cosf(yawRad), sy = sinf(yawRad);
    Vector3 colX = { cy*right.x + sy*fwd.x, cy*right.y + sy*fwd.y, cy*right.z + sy*fwd.z };
    Vector3 colY = up;
    Vector3 colZ = { sy*right.x - cy*fwd.x, sy*right.y - cy*fwd.y, sy*right.z - cy*fwd.z };

    // Pitch tilt around the model's right axis (colX) for the reload dip:
    // rotates colY -> colY*cos(tilt) + colZ*sin(tilt) and colZ similarly.
    // Positive `tilt` drops the muzzle (model -Z) downward.
    if (tilt > 0.0f) {
        float ct = cosf(tilt), st = sinf(tilt);
        Vector3 newY = {
            colY.x * ct + colZ.x * st,
            colY.y * ct + colZ.y * st,
            colY.z * ct + colZ.z * st,
        };
        Vector3 newZ = {
            colZ.x * ct - colY.x * st,
            colZ.y * ct - colY.y * st,
            colZ.z * ct - colY.z * st,
        };
        colY = newY;
        colZ = newZ;
    }

    Matrix tx;
    tx.m0 = colX.x * s; tx.m4 = colY.x * s; tx.m8  = colZ.x * s; tx.m12 = anchor.x;
    tx.m1 = colX.y * s; tx.m5 = colY.y * s; tx.m9  = colZ.y * s; tx.m13 = anchor.y;
    tx.m2 = colX.z * s; tx.m6 = colY.z * s; tx.m10 = colZ.z * s; tx.m14 = anchor.z;
    tx.m3 = 0;          tx.m7 = 0;          tx.m11 = 0;          tx.m15 = 1;

    Model m = weaponModels[wi];
    m.transform = tx;

    // The world shader lights every fragment from a fixed *world* direction
    // using a screen-space-derivative normal. The viewmodel rotates with the
    // camera, so that makes its facets swing between lit and near-black as the
    // player looks around — "colour all over the place". Draw it under near-
    // flat lighting (tiny directional term for form, high ambient) so the gun
    // shows its authored material colours consistently, then restore the world
    // lighting for everything drawn afterwards.
    if (worldShaderLoaded) {
        Vector3 flatSun = { 0.12f, 0.13f, 0.16f };
        Vector3 flatAmb = { 0.90f, 0.91f, 0.96f };
        rlDrawRenderBatchActive();
        SetShaderValue(worldShader, worldShader_sunColorLoc,     &flatSun, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, worldShader_ambientColorLoc, &flatAmb, SHADER_UNIFORM_VEC3);
        DrawModel(m, (Vector3){0,0,0}, 1.0f, WHITE);
        rlDrawRenderBatchActive();
        SetShaderValue(worldShader, worldShader_sunColorLoc,     &sunColor,     SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, worldShader_ambientColorLoc, &ambientColor, SHADER_UNIFORM_VEC3);
    } else {
        DrawModel(m, (Vector3){0,0,0}, 1.0f, WHITE);
    }
}

void Render_FirstPersonViewmodel(Camera camera, Player *me) {
    (void)me; // currently uses globals
    DrawFirstPersonViewmodel(camera);
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
        float a = LabelAlpha(mePos, pap.pos);
        if (a > 0) {
            Vector3 above = { pap.pos.x, 3.4f, pap.pos.z };
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
