#include "hud.h"
#include "render.h"     // muzzleFlashLocal
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "level.h"
#include "game.h"
#include "entities.h"
#include "pad.h"
#include "raygui.h"
#include "raymath.h"
#include <math.h>
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

// Hit marker / damage-direction / round-bonus toast state. All inferred from
// values the snapshot already provides, so this works for both host and clients.
static int       hudLastShotsHit = -1;
static int       hudLastHeadshots = -1;
static float     hudHitMarkerTimer = 0.0f;
static bool      hudHitMarkerHead = false;

static int       hudLastHp = -1;
static float     hudDmgDirTimer = 0.0f;
static Vector3   hudDmgDirVec = { 0, 0, 1 };

static GamePhase hudLastPhase = GS_PRE_GAME;
static int       hudLastRound = 0;
static float     hudBonusToastTimer = 0.0f;
static int       hudBonusToastAmount = 0;

static void HudUpdateFeedback(Player *me, float dt) {
    if (hudLastShotsHit < 0) { hudLastShotsHit = me->shotsHit; hudLastHeadshots = me->headshots; }
    if (me->shotsHit > hudLastShotsHit) {
        hudHitMarkerTimer = 0.20f;
        hudHitMarkerHead  = (me->headshots > hudLastHeadshots);
    }
    hudLastShotsHit = me->shotsHit;
    hudLastHeadshots = me->headshots;

    if (hudLastHp < 0) hudLastHp = me->hp;
    if (me->alive && me->hp < hudLastHp) {
        int bestE = -1; float bestD = 1e9f;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;
            if (enemies[i].state != ZS_INSIDE) continue;
            float dx = enemies[i].pos.x - me->pos.x;
            float dz = enemies[i].pos.z - me->pos.z;
            float d2 = dx*dx + dz*dz;
            if (d2 < bestD) { bestD = d2; bestE = i; }
        }
        if (bestE >= 0) {
            hudDmgDirVec = (Vector3){ enemies[bestE].pos.x - me->pos.x, 0,
                                       enemies[bestE].pos.z - me->pos.z };
            hudDmgDirTimer = 1.2f;
        }
    }
    hudLastHp = me->hp;

    if (gamePhase == GS_ROUND_BREAK && hudLastPhase == GS_PLAY) {
        hudBonusToastAmount = 50 + roundNum * 10;
        hudBonusToastTimer  = 2.5f;
    }
    hudLastPhase = gamePhase;
    hudLastRound = roundNum;

    if (hudHitMarkerTimer  > 0) hudHitMarkerTimer  -= dt;
    if (hudDmgDirTimer     > 0) hudDmgDirTimer     -= dt;
    if (hudBonusToastTimer > 0) hudBonusToastTimer -= dt;
}

static void HudDrawCrosshair(int cx, int cy, Player *me, Color cross) {
    if (me->adsHeld) {
        // Tighter dot crosshair while aiming.
        DrawCircleLines(cx, cy, 3, cross);
        DrawPixel(cx, cy, cross);
        return;
    }
    DrawLine(cx - 10, cy, cx + 10, cy, cross);
    DrawLine(cx, cy - 10, cx, cy + 10, cross);
    DrawCircleLines(cx, cy, 2, cross);
}

static void HudDrawHitMarker(int cx, int cy) {
    if (hudHitMarkerTimer <= 0) return;
    float a = hudHitMarkerTimer / 0.20f;
    if (a > 1) a = 1;
    Color c = hudHitMarkerHead ? (Color){255, 200, 80, (unsigned char)(255 * a)}
                               : (Color){255, 255, 255, (unsigned char)(220 * a)};
    int r = hudHitMarkerHead ? 11 : 8;
    DrawLine(cx - r, cy - r, cx - r + 4, cy - r + 4, c);
    DrawLine(cx + r, cy - r, cx + r - 4, cy - r + 4, c);
    DrawLine(cx - r, cy + r, cx - r + 4, cy + r - 4, c);
    DrawLine(cx + r, cy + r, cx + r - 4, cy + r - 4, c);
}

static void HudDrawDamageDir(int cx, int cy, Player *me) {
    if (hudDmgDirTimer <= 0) return;
    // World-space attacker dir → screen-space arc angle relative to look.
    Vector3 fwd  = { sinf(me->yaw), 0, -cosf(me->yaw) };
    Vector3 dir  = Vector3Normalize(hudDmgDirVec);
    float dot   = fwd.x * dir.x + fwd.z * dir.z;
    float cross = fwd.x * dir.z - fwd.z * dir.x; // sign tells left vs right
    float angle = atan2f(cross, dot);
    float radius = 96.0f;
    float ax = cx + sinf(angle) * radius;
    float ay = cy - cosf(angle) * radius;
    float a = hudDmgDirTimer / 1.2f;
    if (a > 1) a = 1;
    Color c = (Color){ 240, 60, 60, (unsigned char)(220 * a) };

    Vector2 tip   = { ax, ay };
    Vector2 dirV  = { sinf(angle), -cosf(angle) };
    Vector2 perp  = { -dirV.y, dirV.x };
    Vector2 base  = { tip.x - dirV.x * 22, tip.y - dirV.y * 22 };
    Vector2 left  = { base.x + perp.x * 10, base.y + perp.y * 10 };
    Vector2 right = { base.x - perp.x * 10, base.y - perp.y * 10 };
    DrawTriangle(tip, right, left, c);
}

static void HudDrawScoreboard(int sw, int sh) {
    int rows = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (players[i].active) rows++;
    if (rows == 0) return;
    int rowH = 32;
    int w = 720, h = 80 + rows * rowH;
    int x = sw/2 - w/2, y = sh/2 - h/2;
    DrawRectangle(x, y, w, h, (Color){0, 0, 0, 200});
    DrawRectangleLines(x, y, w, h, (Color){200, 200, 200, 200});
    DrawText("SCOREBOARD", x + 20, y + 14, 26, RAYWHITE);
    int hy = y + 56;
    DrawText("PLAYER",    x + 20,  hy, 18, GRAY);
    DrawText("POINTS",    x + 220, hy, 18, GRAY);
    DrawText("KILLS",     x + 340, hy, 18, GRAY);
    DrawText("HEADSHOTS", x + 440, hy, 18, GRAY);
    DrawText("ACC %",     x + 580, hy, 18, GRAY);
    int row = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!players[i].active) continue;
        Player *p = &players[i];
        int yr = hy + 24 + row * rowH;
        Color tc = p->alive ? RAYWHITE : (Color){180, 80, 80, 255};
        DrawText(p->name[0] ? p->name : "Player", x + 20, yr, 20, tc);
        char buf[32];
        snprintf(buf, sizeof buf, "%d", p->points);     DrawText(buf, x + 220, yr, 20, YELLOW);
        snprintf(buf, sizeof buf, "%d", p->kills);      DrawText(buf, x + 340, yr, 20, tc);
        snprintf(buf, sizeof buf, "%d", p->headshots);  DrawText(buf, x + 440, yr, 20, tc);
        float acc = (p->shotsFired > 0) ? 100.0f * p->shotsHit / p->shotsFired : 0.0f;
        snprintf(buf, sizeof buf, "%.1f", acc);         DrawText(buf, x + 580, yr, 20, tc);
        row++;
    }
}

static void HudDrawBonusToast(int sw, int sh) {
    if (hudBonusToastTimer <= 0) return;
    float a = hudBonusToastTimer / 2.5f;
    if (a > 1) a = 1;
    char buf[64];
    snprintf(buf, sizeof buf, "+%d  ROUND BONUS", hudBonusToastAmount);
    int fs = 32;
    int tw = MeasureText(buf, fs);
    int x = sw/2 - tw/2, y = sh/3;
    unsigned char alpha = (unsigned char)(220 * a);
    DrawRectangle(x - 16, y - 6, tw + 32, fs + 12, (Color){0, 0, 0, (unsigned char)(160 * a)});
    DrawText(buf, x, y, fs, (Color){240, 220, 60, alpha});
}

void Hud_Draw(int sw, int sh, Player *me, Interact ix) {
    HudUpdateFeedback(me, GetFrameTime());

    int cx = sw / 2, cy = sh / 2;
    Color cross = (muzzleFlashLocal > 0) ? YELLOW : RAYWHITE;
    HudDrawCrosshair(cx, cy, me, cross);
    HudDrawHitMarker(cx, cy);
    HudDrawDamageDir(cx, cy, me);

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
    // Stamina sliver under HP
    {
        float s = me->stamina;
        GuiProgressBar((Rectangle){62, 42, 215, 6}, NULL, NULL, &s, 0.0f, 100.0f);
    }
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
    if (noclipMode) {
        const char *nm = "NOCLIP  (F4)";
        int gw = MeasureText(nm, 22);
        DrawRectangle(sw - gw - 30, 175, gw + 20, 30, (Color){0,0,0,160});
        DrawRectangleLines(sw - gw - 30, 175, gw + 20, 30, (Color){120,220,255,255});
        DrawText(nm, sw - gw - 20, 180, 22, (Color){120,220,255,255});
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
        } else if (ix.kind == IK_REVIVE) {
            Player *t = &players[ix.idx];
            border = (Color){80, 200, 80, 255};
            snprintf(prompt, sizeof prompt, "[Hold E]  REVIVE %s",
                     t->name[0] ? t->name : "teammate");
        } else if (ix.kind == IK_MBOX) {
            border = (Color){200, 100, 200, 255};
            int meIdx = localPlayerIdx;
            if (mbox.state == MBOX_IDLE) {
                snprintf(prompt, sizeof prompt, "[F]  MYSTERY BOX  -  %d", MBOX_COST);
                if (me->points < MBOX_COST) promptColor = (Color){200,80,80,255};
            } else if (mbox.state == MBOX_ROLLING) {
                snprintf(prompt, sizeof prompt, "Rolling... %.1fs", mbox.timer);
                promptColor = (Color){220,180,220,255};
            } else if (mbox.state == MBOX_WAITING) {
                const WeaponDef *w = &WEAPONS[mbox.showingWeapon];
                if (meIdx == mbox.ownerPlayer) {
                    snprintf(prompt, sizeof prompt, "[F]  TAKE %s  (%.1fs)", w->name, mbox.timer);
                } else {
                    snprintf(prompt, sizeof prompt, "%s  -  waiting for %s",
                             w->name,
                             players[mbox.ownerPlayer].name[0] ? players[mbox.ownerPlayer].name : "owner");
                    promptColor = GRAY;
                }
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
            } else if (ix.kind == IK_REVIVE) {
                float p = players[ix.idx].reviveAsTarget;
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

    HudDrawBonusToast(sw, sh);
    if (IsKeyDown(KEY_TAB) || Pad_Down(PAD_BACK)) HudDrawScoreboard(sw, sh);
}
