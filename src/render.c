#include "render.h"
#include "level.h"
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "entities.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>

float muzzleFlashLocal = 0.0f;

static const Color PLAYER_COLORS[NET_MAX_PLAYERS] = {
    {220, 80, 80, 255}, {80, 120, 220, 255}, {90, 200, 90, 255}, {220, 200, 80, 255},
};

static void DrawWallXSeg(float x0, float x1, float fixedZ, Color c) {
    if (x1 <= x0) return;
    float cx = (x0 + x1) * 0.5f;
    DrawCube((Vector3){cx, WALL_HEIGHT*0.5f, fixedZ}, (x1 - x0), WALL_HEIGHT, WALL_THICK, c);
}
static void DrawWallZSeg(float z0, float z1, float fixedX, Color c) {
    if (z1 <= z0) return;
    float cz = (z0 + z1) * 0.5f;
    DrawCube((Vector3){fixedX, WALL_HEIGHT*0.5f, cz}, WALL_THICK, WALL_HEIGHT, (z1 - z0), c);
}

static void DrawArena(void) {
    DrawPlane((Vector3){0,0,0}, (Vector2){ARENA_HALF*2, ARENA_HALF*2}, (Color){55,65,75,255});
    rlPushMatrix(); rlTranslatef(0, 0.01f, 0); DrawGrid(40, 2.0f); rlPopMatrix();
    DrawPlane((Vector3){0, -0.01f, 0}, (Vector2){ARENA_HALF*2 + 16, ARENA_HALF*2 + 16}, (Color){30,35,40,255});

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
        DrawCubeV(obstacles[i].center, obstacles[i].size, (Color){120,90,70,255});
        DrawCubeWiresV(obstacles[i].center, obstacles[i].size, (Color){40,30,20,255});
    }
}

static void DrawInteriorWalls(void) {
    for (int i = 0; i < interiorWallCount; i++) {
        DrawCubeV(interiorWalls[i].center, interiorWalls[i].size, (Color){80, 90, 100, 255});
        DrawCubeWiresV(interiorWalls[i].center, interiorWalls[i].size, (Color){40, 50, 60, 255});
    }
}

static void DrawDoors(void) {
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].opened) continue;
        Box b = doors[i].box;
        DrawCubeV(b.center, b.size, (Color){130, 80, 50, 255});
        DrawCubeWiresV(b.center, b.size, (Color){40, 25, 15, 255});
        for (int k = 0; k < 3; k++) {
            float y = b.center.y - b.size.y*0.3f + k * (b.size.y*0.3f);
            Vector3 c2 = { b.center.x, y, b.center.z };
            Vector3 sz = { b.size.x * 0.96f, 0.08f, b.size.z * 1.05f };
            DrawCubeV(c2, sz, (Color){90, 55, 30, 255});
        }
    }
}

static void DrawWindows(void) {
    for (int i = 0; i < windowCount; i++) {
        Window3D *w = &windows[i];
        for (int b = 0; b < w->boards; b++) {
            float t = (b + 1.0f) / (MAX_BOARDS_PER_WIN + 1.0f);
            float by = t * WALL_HEIGHT;
            Vector3 planeCenter = { w->pos.x, by, w->pos.z };
            Vector3 planeSize = (fabsf(w->normal.x) > 0.5f)
                ? (Vector3){ WALL_THICK + 0.2f, 0.25f, WINDOW_WIDTH }
                : (Vector3){ WINDOW_WIDTH, 0.25f, WALL_THICK + 0.2f };
            DrawCubeV(planeCenter, planeSize, (Color){150, 100, 50, 255});
            DrawCubeWiresV(planeCenter, planeSize, (Color){60, 40, 20, 255});
        }
    }
}

static void DrawWallBuys(void) {
    for (int i = 0; i < wallBuyCount; i++) {
        WallBuy *wb = &wallBuys[i];
        const WeaponDef *w = &WEAPONS[wb->weaponIdx];
        Vector3 size = (fabsf(wb->normal.x) > 0.5f)
            ? (Vector3){ 0.2f, 1.0f, 1.6f }
            : (Vector3){ 1.6f, 1.0f, 0.2f };
        DrawCubeV(wb->pos, size, w->tint);
        DrawCubeWiresV(wb->pos, size, BLACK);
    }
}

static void DrawPerkMachines(void) {
    for (int i = 0; i < perkMachineCount; i++) {
        PerkMachine *pm = &perkMachines[i];
        const PerkDef *pd = &PERKS[pm->perkIdx];
        DrawCube((Vector3){ pm->pos.x, 0.6f, pm->pos.z }, 1.0f, 1.2f, 1.0f, (Color){30,30,30,255});
        DrawCube((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, pd->tint);
        DrawCubeWires((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, BLACK);
        DrawCube((Vector3){ pm->pos.x, 2.5f, pm->pos.z }, 0.6f, 0.4f, 0.6f, (Color){40,40,40,255});
        bool myOwn = players[localPlayerIdx].hasPerk[pm->perkIdx];
        DrawSphere((Vector3){ pm->pos.x, 2.95f, pm->pos.z }, 0.18f,
                   myOwn ? (Color){240,240,240,255} : pd->tint);
    }
}

static void DrawPaP(void) {
    DrawCube(pap.pos, 2.5f, 0.6f, 2.5f, (Color){25,25,30,255});
    DrawCubeWires(pap.pos, 2.5f, 0.6f, 2.5f, BLACK);
    DrawCube((Vector3){pap.pos.x, 1.0f, pap.pos.z}, 2.0f, 0.8f, 2.0f, (Color){50,50,60,255});
    Vector3 top = { pap.pos.x, 1.7f + sinf(pap.bob) * 0.05f, pap.pos.z };
    DrawCube(top, 1.6f, 0.5f, 1.6f, (Color){80, 50, 130, 255});
    DrawCubeWires(top, 1.6f, 0.5f, 1.6f, (Color){200,150,255,255});
    DrawSphere((Vector3){pap.pos.x, 2.2f, pap.pos.z}, 0.15f, (Color){220, 180, 255, 255});

    if (pap.activeTimer > 0 && pap.ownerPlayer >= 0 && pap.slotInProgress >= 0) {
        float spin = pap.bob * 4.0f;
        Vector3 weaponPos = { pap.pos.x, 2.6f + sinf(pap.bob*2) * 0.15f, pap.pos.z };
        rlPushMatrix();
        rlTranslatef(weaponPos.x, weaponPos.y, weaponPos.z);
        rlRotatef(spin * 60.0f, 0, 1, 0);
        int w = players[pap.ownerPlayer].inventory[pap.slotInProgress].weaponIdx;
        DrawCube((Vector3){0,0,0}, 0.8f, 0.3f, 0.2f, WEAPONS[w].tint);
        rlPopMatrix();
    }
}

static void DrawEnemy(Enemy *e) {
    float bob = sinf(e->bobPhase) * 0.06f;
    Vector3 body = { e->pos.x, e->pos.y + bob, e->pos.z };
    float t = (float)e->hp / (float)(e->maxHp > 0 ? e->maxHp : 1);
    Color hpTint;
    if      (t > 0.66f) hpTint = (Color){80, 140, 60, 255};
    else if (t > 0.33f) hpTint = (Color){180,160, 40, 255};
    else                hpTint = (Color){200, 60, 50, 255};

    float w = ENEMY_RADIUS * 2.0f, h = ENEMY_HEIGHT;
    bool drawHead = true;
    Color stripe = (Color){0, 0, 0, 0};
    switch (e->type) {
        case ZT_RUNNER:
            w *= 0.85f; h *= 0.95f;
            stripe = (Color){255, 220, 50, 255};
            break;
        case ZT_CRAWLER:
            w *= 1.0f;  h *= 0.45f;
            body.y -= h * 0.5f * 0.4f;  // lower
            drawHead = false;
            hpTint.r = (unsigned char)(hpTint.r * 0.7f);
            hpTint.g = (unsigned char)(hpTint.g * 0.5f);
            hpTint.b = (unsigned char)(hpTint.b * 0.5f);
            break;
        case ZT_BOSS:
            w *= 1.7f; h *= 1.5f;
            body.y += (h - ENEMY_HEIGHT) * 0.5f;
            stripe = (Color){200, 40, 200, 255};
            break;
        default: break;
    }

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
    if (!p->alive) c = (Color){ 100,100,100, 200 };
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
    Color crate = (mbox.state == MBOX_IDLE) ? (Color){120, 80, 50, 255} : (Color){160, 100, 60, 255};
    DrawCube(b, 1.8f, 1.0f, 1.2f, crate);
    DrawCubeWires(b, 1.8f, 1.0f, 1.2f, BLACK);
    // Yellow lid for visibility
    DrawCube((Vector3){ b.x, b.y + 0.55f, b.z }, 1.8f, 0.1f, 1.2f, (Color){240, 200, 80, 255});

    if (mbox.state == MBOX_ROLLING || mbox.state == MBOX_WAITING) {
        Vector3 wp = { b.x, b.y + 1.4f + sinf(mbox.bob * 2.0f) * 0.1f, b.z };
        rlPushMatrix();
        rlTranslatef(wp.x, wp.y, wp.z);
        rlRotatef(mbox.bob * 60.0f, 0, 1, 0);
        Color wc = WEAPONS[mbox.showingWeapon].tint;
        DrawCube((Vector3){0,0,0}, 0.9f, 0.25f, 0.2f, wc);
        DrawCubeWires((Vector3){0,0,0}, 0.9f, 0.25f, 0.2f, BLACK);
        rlPopMatrix();
    }
}

static void DrawPowerUps(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        Vector3 p = powerUps[i].pos;
        p.y += sinf(powerUps[i].bob) * 0.15f;
        Color c = POWERUP_COLORS[powerUps[i].type];
        DrawCube(p, 0.6f, 0.6f, 0.6f, c);
        DrawCubeWires(p, 0.6f, 0.6f, 0.6f, BLACK);
        DrawSphere((Vector3){p.x, p.y + 0.55f, p.z}, 0.08f, WHITE);
    }
}

void Render_World3D(Camera camera) {
    BeginMode3D(camera);
        DrawArena();
        DrawInteriorWalls();
        DrawDoors();
        DrawWindows();
        DrawWallBuys();
        DrawPerkMachines();
        DrawPaP();
        DrawMysteryBox();
        DrawPowerUps();
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (i == localPlayerIdx) continue;
            DrawOtherPlayer(i);
        }
        for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) DrawEnemy(&enemies[i]);
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].alive) continue;
            DrawSphere(bullets[i].pos, 0.08f, YELLOW);
            DrawSphere(bullets[i].pos, 0.14f, (Color){255,200,0,90});
        }
    EndMode3D();
}

void Render_WorldLabels(Camera camera, int sw, int sh, Player *me) {
    for (int i = 0; i < wallBuyCount; i++) {
        WallBuy *wb = &wallBuys[i];
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
        DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
        DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, w->tint);
    }
    for (int i = 0; i < perkMachineCount; i++) {
        PerkMachine *pm = &perkMachines[i];
        const PerkDef *pd = &PERKS[pm->perkIdx];
        Vector3 above = { pm->pos.x, 3.3f, pm->pos.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -200 || sp.y < -100 || sp.x > sw + 200 || sp.y > sh + 100) continue;
        char label[64];
        if (me->hasPerk[pm->perkIdx]) snprintf(label, sizeof label, "%s", pd->name);
        else                          snprintf(label, sizeof label, "%s  %d", pd->name, pd->cost);
        int lw = MeasureText(label, 18);
        DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
        DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18,
                 me->hasPerk[pm->perkIdx] ? GRAY : pd->tint);
    }
    {
        Vector3 above = { pap.pos.x, 3.4f, pap.pos.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x > -200 && sp.x < sw + 200 && sp.y > -200 && sp.y < sh + 200) {
            char label[64];
            snprintf(label, sizeof label, "PACK-A-PUNCH  %d", PAP_COST);
            int lw = MeasureText(label, 18);
            DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
            DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, (Color){200,150,255,255});
        }
    }
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].opened) continue;
        Vector3 above = { doors[i].box.center.x,
                          doors[i].box.center.y + doors[i].box.size.y*0.5f + 0.8f,
                          doors[i].box.center.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -200 || sp.y < -100 || sp.x > sw + 200 || sp.y > sh + 100) continue;
        char label[64];
        snprintf(label, sizeof label, "DOOR  %d", doors[i].cost);
        int lw = MeasureText(label, 18);
        DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
        DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, (Color){220,170,110,255});
    }
    // Power-up labels
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerUps[i].active) continue;
        Vector3 above = { powerUps[i].pos.x, powerUps[i].pos.y + 1.0f, powerUps[i].pos.z };
        Vector2 sp = GetWorldToScreen(above, camera);
        if (sp.x < -100 || sp.y < -100 || sp.x > sw + 100 || sp.y > sh + 100) continue;
        const char *lbl = POWERUP_LETTERS[powerUps[i].type];
        int lw = MeasureText(lbl, 22);
        DrawText(lbl, (int)sp.x - lw/2, (int)sp.y - 10, 22, POWERUP_COLORS[powerUps[i].type]);
    }
}
