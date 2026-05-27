#include "hud.h"
#include "render.h"     // muzzleFlashLocal
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "level.h"
#include "game.h"
#include "entities.h"
#include "raygui.h"
#include <stdio.h>

static const Color PLAYER_COLORS[NET_MAX_PLAYERS] = {
    {220, 80, 80, 255}, {80, 120, 220, 255}, {90, 200, 90, 255}, {220, 200, 80, 255},
};

static void DrawPerkIcons(int sh, Player *me) {
    int sz = 36, gap = 6, x = 14, y = sh - sz - 14;
    for (int i = 0; i < PERK_COUNT; i++) {
        if (!me->hasPerk[i]) continue;
        DrawRectangle(x, y, sz, sz, PERKS[i].tint);
        DrawRectangleLines(x, y, sz, sz, BLACK);
        DrawText(PERKS[i].name, x, y - 16, 12, RAYWHITE);
        x += sz + gap;
    }
}

void Hud_Draw(int sw, int sh, Player *me, Interact ix) {
    int cx = sw / 2, cy = sh / 2;
    Color cross = (muzzleFlashLocal > 0) ? YELLOW : RAYWHITE;
    DrawLine(cx - 10, cy, cx + 10, cy, cross);
    DrawLine(cx, cy - 10, cx, cy + 10, cross);
    DrawCircleLines(cx, cy, 2, cross);

    if (me->damageFlash > 0) {
        unsigned char a = (unsigned char)(me->damageFlash * 180);
        DrawRectangle(0, 0, sw, sh, (Color){180, 0, 0, a});
    }

    DrawRectangle(10, 10, 280, 120, (Color){0,0,0,140});
    DrawRectangleLines(10, 10, 280, 120, (Color){200,200,200,180});

    char buf[96];
    DrawText("HP", 22, 22, 18, RAYWHITE);
    float hpf = (float)me->hp, hpMax = (float)Perk_EffMaxHP(me);
    GuiProgressBar((Rectangle){62, 22, 215, 18}, NULL, NULL, &hpf, 0.0f, hpMax);
    snprintf(buf, sizeof buf, "%d / %d", me->hp, Perk_EffMaxHP(me));
    DrawText(buf, 145, 22, 16, RAYWHITE);
    snprintf(buf, sizeof buf, "POINTS  %d", me->points);
    DrawText(buf, 22, 52, 22, YELLOW);
    snprintf(buf, sizeof buf, "ROUND  %d", roundNum);
    DrawText(buf, 22, 80, 22, SKYBLUE);
    snprintf(buf, sizeof buf, "ZOMBIES LEFT  %d", enemiesAlive + enemiesToSpawn);
    DrawText(buf, 22, 106, 16, RAYWHITE);

    WeaponSlot *cur = &me->inventory[me->currentSlot];
    const WeaponDef *cw = &WEAPONS[cur->weaponIdx];
    const char *displayName = cur->packed ? cw->packedName : cw->name;

    int panelW = 340, panelH = 110;
    int px = sw - panelW - 10, py = sh - panelH - 10;
    DrawRectangle(px, py, panelW, panelH, (Color){0,0,0,160});
    DrawRectangleLines(px, py, panelW, panelH,
                       cur->packed ? (Color){200,150,255,255} : (Color){200,200,200,180});
    DrawRectangle(px + 8, py + 8, 14, panelH - 16, cw->tint);

    if (cur->reloadTimer > 0) {
        DrawText("RELOADING...", px + 32, py + 14, 22, ORANGE);
        float t = 1.0f - (cur->reloadTimer / Weapon_EffReload(me, cur));
        GuiProgressBar((Rectangle){px + 32, py + 46, panelW - 50, 14}, NULL, NULL, &t, 0.0f, 1.0f);
    } else if (pap.activeTimer > 0 && pap.ownerPlayer == localPlayerIdx && pap.slotInProgress == me->currentSlot) {
        DrawText("IN PACK-A-PUNCH...", px + 32, py + 14, 20, (Color){200,150,255,255});
        float t = 1.0f - (pap.activeTimer / PAP_DURATION);
        GuiProgressBar((Rectangle){px + 32, py + 46, panelW - 50, 14}, NULL, NULL, &t, 0.0f, 1.0f);
    } else {
        DrawText(displayName, px + 32, py + 14, 22, cur->packed ? (Color){220,180,255,255} : RAYWHITE);
        snprintf(buf, sizeof buf, "%d / %d", cur->ammo, cur->reserve);
        DrawText(buf, px + 32, py + 42, 28, (cur->ammo == 0) ? RED : YELLOW);
    }

    WeaponSlot *other = &me->inventory[(me->currentSlot + 1) % INV_SLOTS];
    if (other->owned) {
        const WeaponDef *ow = &WEAPONS[other->weaponIdx];
        const char *on = other->packed ? ow->packedName : ow->name;
        snprintf(buf, sizeof buf, "[Q] %s  %d/%d", on, other->ammo, other->reserve);
        DrawText(buf, px + 32, py + 80, 16, GRAY);
    } else {
        DrawText("[Q] (empty slot)", px + 32, py + 80, 16, (Color){90,90,90,255});
    }

    DrawPerkIcons(sh, me);

    if (godMode) {
        const char *gm = "GOD MODE";
        int gw = MeasureText(gm, 22);
        DrawRectangle(sw - gw - 30, 140, gw + 20, 30, (Color){0,0,0,160});
        DrawRectangleLines(sw - gw - 30, 140, gw + 20, 30, (Color){255,220,100,255});
        DrawText(gm, sw - gw - 20, 145, 22, (Color){255,220,100,255});
    }

    // Power-up status (centered under crosshair)
    int puY = sh / 2 + 30;
    if (doublePointsTimer > 0) {
        char b[32]; snprintf(b, sizeof b, "DOUBLE POINTS  %.1fs", doublePointsTimer);
        int tw = MeasureText(b, 20);
        DrawText(b, sw/2 - tw/2, puY, 20, (Color){240,220,60,255});
        puY += 24;
    }
    if (instaKillTimer > 0) {
        char b[32]; snprintf(b, sizeof b, "INSTA-KILL  %.1fs", instaKillTimer);
        int tw = MeasureText(b, 20);
        DrawText(b, sw/2 - tw/2, puY, 20, (Color){240,80,80,255});
    }

    int rx = sw - 220, ry = 10, rh = 28;
    int actCount = Player_ActiveCount();
    if (actCount > 1) {
        DrawRectangle(rx, ry, 210, 8 + actCount * rh, (Color){0,0,0,140});
        DrawRectangleLines(rx, ry, 210, 8 + actCount * rh, (Color){200,200,200,180});
        int row = 0;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (!players[i].active) continue;
            DrawRectangle(rx + 6, ry + 4 + row*rh + 4, 14, 14, PLAYER_COLORS[i]);
            char ln[64];
            snprintf(ln, sizeof ln, "%s", players[i].name[0] ? players[i].name : "Player");
            Color tc = players[i].alive ? RAYWHITE : (Color){180,80,80,255};
            DrawText(ln, rx + 26, ry + 4 + row*rh + 2, 16, tc);
            char hpStr[32]; snprintf(hpStr, sizeof hpStr, "%d", players[i].hp);
            DrawText(hpStr, rx + 150, ry + 4 + row*rh + 2, 16, tc);
            row++;
        }
    }

    if (ix.kind != IK_NONE) {
        char prompt[128] = {0};
        Color promptColor = RAYWHITE;
        Color border = (Color){200,200,200,200};

        if (ix.kind == IK_WALLBUY) {
            WallBuy *wb = &wallBuys[ix.idx];
            const WeaponDef *w = &WEAPONS[wb->weaponIdx];
            int owned = Weapon_FindOwnedSlot(me, wb->weaponIdx);
            border = w->tint;
            if (owned >= 0) {
                WeaponSlot *s = &me->inventory[owned];
                int cap = Weapon_EffReserveMax(s);
                if (s->reserve >= cap) { snprintf(prompt, sizeof prompt, "%s ammo: FULL", w->name); promptColor = GRAY; }
                else { snprintf(prompt, sizeof prompt, "[F]  %s AMMO  -  %d", w->name, w->ammoPrice);
                       if (me->points < w->ammoPrice) promptColor = (Color){200,80,80,255}; }
            } else if (Weapon_FirstEmptySlot(me) < 0) {
                // Both slots full — buy will replace the currently-held weapon
                const WeaponDef *cur = &WEAPONS[me->inventory[me->currentSlot].weaponIdx];
                snprintf(prompt, sizeof prompt, "[F]  BUY %s (replaces %s)  -  %d",
                         w->name, cur->name, w->buyPrice);
                if (me->points < w->buyPrice) promptColor = (Color){200,80,80,255};
            } else {
                snprintf(prompt, sizeof prompt, "[F]  BUY %s  -  %d", w->name, w->buyPrice);
                if (me->points < w->buyPrice) promptColor = (Color){200,80,80,255};
            }
        } else if (ix.kind == IK_PERK) {
            PerkMachine *pm = &perkMachines[ix.idx];
            const PerkDef *pd = &PERKS[pm->perkIdx];
            border = pd->tint;
            if (me->hasPerk[pm->perkIdx]) { snprintf(prompt, sizeof prompt, "%s : owned", pd->name); promptColor = GRAY; }
            else { snprintf(prompt, sizeof prompt, "[F]  BUY %s  -  %d", pd->name, pd->cost);
                   if (me->points < pd->cost) promptColor = (Color){200,80,80,255}; }
        } else if (ix.kind == IK_PAP) {
            border = (Color){200,150,255,255};
            WeaponSlot *s = &me->inventory[me->currentSlot];
            if (pap.activeTimer > 0) { snprintf(prompt, sizeof prompt, "Upgrading... %.1fs", pap.activeTimer); promptColor = (Color){200,150,255,255}; }
            else if (s->packed) { snprintf(prompt, sizeof prompt, "Already Pack-a-Punched"); promptColor = GRAY; }
            else { snprintf(prompt, sizeof prompt, "[F]  PACK-A-PUNCH  -  %d", PAP_COST);
                   if (me->points < PAP_COST) promptColor = (Color){200,80,80,255}; }
        } else if (ix.kind == IK_WINDOW) {
            Window3D *w = &windows[ix.idx];
            border = (Color){200, 160, 80, 255};
            if (w->boards >= MAX_BOARDS_PER_WIN) { snprintf(prompt, sizeof prompt, "Window sealed"); promptColor = GRAY; }
            else snprintf(prompt, sizeof prompt, "[Hold E]  REPAIR BOARD");
        } else if (ix.kind == IK_DOOR) {
            Door *d = &doors[ix.idx];
            border = (Color){200, 150, 100, 255};
            if (d->opened) { snprintf(prompt, sizeof prompt, "Door open"); promptColor = GRAY; }
            else {
                snprintf(prompt, sizeof prompt, "[F]  OPEN DOOR  -  %d", d->cost);
                if (me->points < d->cost) promptColor = (Color){200,80,80,255};
            }
        }

        if (prompt[0]) {
            int tw = MeasureText(prompt, 26);
            int by = sh / 2 + 80;
            DrawRectangle(cx - tw/2 - 16, by - 8, tw + 32, 40, (Color){0,0,0,180});
            DrawRectangleLines(cx - tw/2 - 16, by - 8, tw + 32, 40, border);
            DrawText(prompt, cx - tw/2, by, 26, promptColor);
            if (ix.kind == IK_WINDOW) {
                float p = windows[ix.idx].repairProgress;
                if (p > 0) GuiProgressBar((Rectangle){cx - tw/2, by + 36, tw, 8}, NULL, NULL, &p, 0.0f, 1.0f);
            }
        }
    }

    if (gamePhase == GS_ROUND_BREAK) {
        char rb[64]; snprintf(rb, sizeof rb, "ROUND  %d", roundNum + 1);
        int rs = 70;
        int rw = MeasureText(rb, rs);
        DrawRectangle(0, sh/2 - 70, sw, 140, (Color){0,0,0,160});
        DrawText(rb, cx - rw/2, sh/2 - 50, rs, (Color){220, 50, 50, 255});
        const char *sub = "Get ready...";
        int sw2 = MeasureText(sub, 24);
        DrawText(sub, cx - sw2/2, sh/2 + 30, 24, RAYWHITE);
    }

    if (!me->alive && gamePhase != GS_GAME_OVER) {
        DrawRectangle(0, sh/2 - 40, sw, 80, (Color){80, 0, 0, 160});
        const char *down = "YOU ARE DOWN  —  revive at next round";
        int dw = MeasureText(down, 28);
        DrawText(down, sw/2 - dw/2, sh/2 - 14, 28, RAYWHITE);
    }
}
