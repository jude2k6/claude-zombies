#include "hud.h"
#include "render.h"     // muzzleFlashLocal
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "level.h"
#include "game.h"
#include "entities.h"
#include "pad.h"
#include "settings.h"
#include "fx.h"
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
static float     hudRoundSplashTimer = 0.0f;
static int       hudRoundSplashRound = 0;

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
    // Transition into a fresh round (PRE_GAME or ROUND_BREAK → PLAY): big splash.
    if (gamePhase == GS_PLAY && hudLastPhase != GS_PLAY && roundNum > 0) {
        hudRoundSplashTimer = 2.6f;
        hudRoundSplashRound = roundNum;
    }
    hudLastPhase = gamePhase;
    hudLastRound = roundNum;

    if (hudHitMarkerTimer    > 0) hudHitMarkerTimer    -= dt;
    if (hudDmgDirTimer       > 0) hudDmgDirTimer       -= dt;
    if (hudBonusToastTimer   > 0) hudBonusToastTimer   -= dt;
    if (hudRoundSplashTimer  > 0) hudRoundSplashTimer  -= dt;
}

static void HudDrawRoundSplash(int sw, int sh) {
    if (hudRoundSplashTimer <= 0) return;
    float t = hudRoundSplashTimer / 2.6f;          // 1 → 0
    float fadeIn  = 1.0f - (t - 0.6f) / 0.4f;      // first 0.4s fade-in
    if (fadeIn > 1) fadeIn = 1; if (fadeIn < 0) fadeIn = 0;
    float fadeOut = (t < 0.4f) ? (t / 0.4f) : 1.0f; // last 0.4s fade-out
    float a = fadeIn * fadeOut;
    // Letters drop in from above; settle by mid-life.
    float settleP = 1.0f - t;
    if (settleP > 1) settleP = 1; if (settleP < 0) settleP = 0;
    float ease = 1.0f - (1.0f - settleP) * (1.0f - settleP);
    int dropY = (int)((1.0f - ease) * -160.0f);

    char rd[24]; snprintf(rd, sizeof rd, "ROUND  %d", hudRoundSplashRound);
    int fs = 96;
    int rw = MeasureText(rd, fs);
    int x = sw/2 - rw/2;
    int y = sh/3 + dropY;
    unsigned char A   = (unsigned char)(220 * a);
    unsigned char Aof = (unsigned char)(180 * a);
    // Black panel behind so it pops over the world.
    DrawRectangle(0, y - 14, sw, fs + 28, (Color){0,0,0,(unsigned char)(120 * a)});
    DrawText(rd, x + 4, y + 4, fs, (Color){0, 0, 0, Aof});
    DrawText(rd, x, y, fs, (Color){220, 50, 50, A});

    // Subtitle: zombie count for the round.
    int totalZ = enemiesAlive + enemiesToSpawn;
    char sub[64];
    if (totalZ > 0) snprintf(sub, sizeof sub, "%d  ZOMBIES INCOMING", totalZ);
    else            snprintf(sub, sizeof sub, "BRACE  YOURSELF");
    int subFs = 22;
    int sw2 = MeasureText(sub, subFs);
    DrawText(sub, sw/2 - sw2/2, y + fs + 8, subFs, (Color){220, 220, 220, A});
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

    // Hit-flash from being struck.
    if (me->damageFlash > 0) {
        unsigned char a = (unsigned char)(me->damageFlash * 180);
        DrawRectangle(0, 0, sw, sh, (Color){180, 0, 0, a});
    }
    // Nuke / big-flash effect (FX-driven white wash).
    if (fxFlashAmount > 0) {
        unsigned char a = (unsigned char)(fxFlashAmount * 220);
        DrawRectangle(0, 0, sw, sh, (Color){255, 255, 255, a});
    }
    // Low-HP red vignette (radial-ish: 4 edge bands).
    int hpMax = Perk_EffMaxHP(me);
    float hpFrac = (hpMax > 0) ? ((float)me->hp / (float)hpMax) : 0.0f;
    if (hpFrac < 0.4f && me->alive) {
        float t = 1.0f - (hpFrac / 0.4f);   // 0..1 intensity
        float pulse = 0.65f + 0.35f * sinf((float)GetTime() * 6.0f);
        unsigned char a = (unsigned char)(t * pulse * 180);
        int band = (int)(t * 140) + 40;
        DrawRectangle(0,         0,         sw,   band, (Color){180, 20, 20, a});
        DrawRectangle(0,         sh - band, sw,   band, (Color){180, 20, 20, a});
        DrawRectangle(0,         0,         band, sh,   (Color){180, 20, 20, a});
        DrawRectangle(sw - band, 0,         band, sh,   (Color){180, 20, 20, a});
    }

    char buf[96];

    // Top-center: round number with zombies-remaining underneath. Big and
    // unframed, COD-style.
    {
        char rd[24]; snprintf(rd, sizeof rd, "%d", roundNum);
        int fs = 56;
        int rw = MeasureText(rd, fs);
        DrawText(rd, sw/2 - rw/2 + 2, 22, fs, (Color){0,0,0,200});
        DrawText(rd, sw/2 - rw/2,     20, fs, (Color){220, 60, 60, 255});
        char zsub[40]; snprintf(zsub, sizeof zsub, "Zombies  %d", enemiesAlive + enemiesToSpawn);
        int zw = MeasureText(zsub, 18);
        DrawText(zsub, sw/2 - zw/2, 20 + fs + 4, 18, (Color){220,220,220,220});
    }

    // Bottom-left: compact HP bar (no panel chrome) + stamina sliver below.
    // Sits above the perk icons.
    {
        int barW = 280, barH = 18;
        int bx = 24, by = sh - 100;
        DrawRectangle(bx - 2, by - 2, barW + 4, barH + 4, (Color){0,0,0,160});
        Color hpCol = (hpFrac > 0.66f) ? (Color){80,200,80,230}
                    : (hpFrac > 0.33f) ? (Color){220,200,60,230}
                                       : (Color){220,60,60,230};
        DrawRectangle(bx, by, (int)(barW * hpFrac), barH, hpCol);
        DrawRectangleLines(bx - 2, by - 2, barW + 4, barH + 4, (Color){0,0,0,200});
        snprintf(buf, sizeof buf, "%d / %d", me->hp, hpMax);
        DrawText(buf, bx + 6, by - 1, 16, RAYWHITE);
        // Stamina
        int sy = by + barH + 4;
        DrawRectangle(bx - 2, sy - 1, barW + 4, 7, (Color){0,0,0,160});
        DrawRectangle(bx, sy, (int)(barW * me->stamina / 100.0f), 5, (Color){200,200,255,200});
    }

    // Bottom-center: points (small but always visible).
    {
        snprintf(buf, sizeof buf, "%d", me->points);
        int fs = 28;
        int tw = MeasureText(buf, fs);
        DrawText(buf, sw/2 - tw/2 + 2, sh - 44 + 2, fs, (Color){0,0,0,200});
        DrawText(buf, sw/2 - tw/2,     sh - 44,     fs, (Color){240, 220, 60, 255});
        const char *plbl = "POINTS";
        int plw = MeasureText(plbl, 12);
        DrawText(plbl, sw/2 - plw/2, sh - 16, 12, (Color){200,200,200,180});
    }

    // Bottom-right: weapon + ammo, large, no boxed frame. Big ammo number.
    WeaponSlot *cur = &me->inventory[me->currentSlot];
    const WeaponDef *cw = &WEAPONS[cur->weaponIdx];
    const char *displayName = cur->packed ? cw->packedName : cw->name;
    {
        int rx = sw - 24, ry = sh - 90;
        Color nameCol = cur->packed ? (Color){220,180,255,255} : RAYWHITE;

        if (cur->reloadTimer > 0) {
            const char *rl = "RELOADING";
            int rlw = MeasureText(rl, 22);
            DrawText(rl, rx - rlw, ry, 22, ORANGE);
            float t = 1.0f - (cur->reloadTimer / Weapon_EffReload(me, cur));
            DrawRectangle(rx - 220, ry + 30, 220, 6, (Color){80,40,0,180});
            DrawRectangle(rx - 220, ry + 30, (int)(220 * t), 6, ORANGE);
        } else if (pap.activeTimer > 0 && pap.ownerPlayer == localPlayerIdx && pap.slotInProgress == me->currentSlot) {
            const char *pl = "PACK-A-PUNCH";
            int plw = MeasureText(pl, 22);
            DrawText(pl, rx - plw, ry, 22, (Color){200,150,255,255});
            float t = 1.0f - (pap.activeTimer / PAP_DURATION);
            DrawRectangle(rx - 220, ry + 30, 220, 6, (Color){40,20,60,180});
            DrawRectangle(rx - 220, ry + 30, (int)(220 * t), 6, (Color){200,150,255,255});
        } else {
            int nameW = MeasureText(displayName, 22);
            DrawText(displayName, rx - nameW, ry, 22, nameCol);
            // Big ammo / reserve
            char ammo[16]; snprintf(ammo, sizeof ammo, "%d", cur->ammo);
            char rsv[24];  snprintf(rsv, sizeof rsv, "/ %d", cur->reserve);
            int af = 44, rf = 22;
            int aw = MeasureText(ammo, af);
            int rw = MeasureText(rsv, rf);
            Color ammoCol = (cur->ammo == 0) ? RED : (Color){240,220,60,255};
            DrawText(rsv,  rx - rw,         ry + 36, rf, (Color){200,200,200,200});
            DrawText(ammo, rx - rw - aw - 6, ry + 24, af, ammoCol);
            // Small tint sliver under the gun name to identify weapon at a glance.
            DrawRectangle(rx - nameW, ry + 24, nameW, 3, cw->tint);
        }
    }

    // Tiny alt-slot indicator above the weapon block.
    WeaponSlot *other = &me->inventory[(me->currentSlot + 1) % INV_SLOTS];
    {
        int rx = sw - 24, ry = sh - 110;
        if (other->owned) {
            const WeaponDef *ow = &WEAPONS[other->weaponIdx];
            const char *on = other->packed ? ow->packedName : ow->name;
            snprintf(buf, sizeof buf, "[Q]  %s   %d / %d", on, other->ammo, other->reserve);
        } else {
            snprintf(buf, sizeof buf, "[Q]  (empty)");
        }
        int tw = MeasureText(buf, 14);
        DrawText(buf, rx - tw, ry, 14, (Color){180,180,180,180});
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
            const char *suffix = players[i].downed ? "  (DOWN)" : (!players[i].alive ? "  (DEAD)" : "");
            snprintf(ln, sizeof ln, "%s%s", players[i].name[0] ? players[i].name : "Player", suffix);
            Color tc = (players[i].alive && !players[i].downed) ? RAYWHITE : (Color){220,90,90,255};
            DrawText(ln, rx + 26, ry + 4 + row*rh + 2, 16, tc);
            char hpStr[32];
            if (players[i].downed) snprintf(hpStr, sizeof hpStr, "%.0fs", players[i].bleedTimer);
            else                   snprintf(hpStr, sizeof hpStr, "%d", players[i].hp);
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

    if (me->alive && me->downed) {
        DrawRectangle(0, sh/2 - 50, sw, 100, (Color){120, 0, 0, 170});
        const char *title = "YOU ARE DOWN";
        int tw1 = MeasureText(title, 36);
        DrawText(title, sw/2 - tw1/2, sh/2 - 30, 36, RAYWHITE);
        char sub[96];
        int rescuers = 0;
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            if (i != localPlayerIdx && players[i].active && players[i].alive && !players[i].downed) rescuers++;
        if (rescuers > 0)
            snprintf(sub, sizeof sub, "Hold on — bleeding out in %.0fs", me->bleedTimer);
        else
            snprintf(sub, sizeof sub, "No one to revive you — bleeding out in %.0fs", me->bleedTimer);
        int sw3 = MeasureText(sub, 20);
        DrawText(sub, sw/2 - sw3/2, sh/2 + 14, 20, (Color){240, 220, 220, 230});
    } else if (!me->alive && gamePhase != GS_GAME_OVER) {
        DrawRectangle(0, sh/2 - 50, sw, 100, (Color){0, 0, 0, 170});
        const char *title = "SPECTATING";
        int tw1 = MeasureText(title, 36);
        DrawText(title, sw/2 - tw1/2, sh/2 - 30, 36, RAYWHITE);
        const char *sub = "Respawn at next round  -  [F / A] cycle teammate";
        int sw3 = MeasureText(sub, 20);
        DrawText(sub, sw/2 - sw3/2, sh/2 + 14, 20, (Color){220, 220, 220, 220});
    }

    HudDrawBonusToast(sw, sh);
    HudDrawRoundSplash(sw, sh);
    if (IsKeyDown(KEY_TAB) || Bind_Down(BA_SCORE)) HudDrawScoreboard(sw, sh);
}
