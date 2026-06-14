#include "hud.h"
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "level.h"
#include "interact.h"
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

// ---- shared HUD styling -------------------------------------------------
#define HUD_GOLD  (Color){ 255, 206, 84,  255 }
#define HUD_TEXT  (Color){ 236, 238, 245, 255 }
#define HUD_DIM   (Color){ 152, 158, 172, 255 }

// Text with a soft drop shadow so it stays legible over bright geometry.
static void HudText(const char *s, int x, int y, int fs, Color c) {
    DrawText(s, x + 1, y + 2, fs, (Color){ 0, 0, 0, 170 });
    DrawText(s, x, y, fs, c);
}
// Right-aligned variant (x is the right edge).
static void HudTextR(const char *s, int xr, int y, int fs, Color c) {
    HudText(s, xr - MeasureText(s, fs), y, fs, c);
}
static Color WithAlpha(Color c, unsigned char a) { c.a = a; return c; }

// Draw a small white-ish vector glyph identifying a perk, centred at (cx,cy)
// and sized to radius r. Kept primitive so it reads at HUD scale.
static void DrawPerkGlyph(int perkIdx, float cx, float cy, float r, Color col) {
    switch (perkIdx) {
        case PERK_JUG: {              // medical cross — survivability
            float t = r * 0.24f, h = r * 0.62f;
            DrawRectangle((int)(cx - t), (int)(cy - h), (int)(2*t), (int)(2*h), col);
            DrawRectangle((int)(cx - h), (int)(cy - t), (int)(2*h), (int)(2*t), col);
        } break;
        case PERK_SPEED: {            // double fast-forward — reload speed
            for (int k = 0; k < 2; k++) {
                float ox = cx + (k ? 0.0f : -r*0.42f) + r*0.05f;
                DrawTriangle((Vector2){ox - r*0.32f, cy - r*0.5f},
                             (Vector2){ox - r*0.32f, cy + r*0.5f},
                             (Vector2){ox + r*0.18f, cy}, col);
            }
        } break;
        case PERK_DTAP: {             // two rounds — extra bullets
            for (int k = 0; k < 2; k++) {
                float bx = cx + (k ? r*0.30f : -r*0.30f);
                DrawCircle((int)bx, (int)(cy - r*0.30f), r*0.20f, col);
                DrawRectangle((int)(bx - r*0.20f), (int)(cy - r*0.30f), (int)(r*0.40f), (int)(r*0.62f), col);
            }
        } break;
        case PERK_STAMIN: {           // double up-chevron — sprint/stamina
            for (int k = 0; k < 2; k++) {
                float oy = cy + r*0.28f + k * r*0.42f - r*0.42f;
                DrawTriangle((Vector2){cx,          oy - r*0.30f},
                             (Vector2){cx - r*0.5f, oy + r*0.18f},
                             (Vector2){cx + r*0.5f, oy + r*0.18f}, col);
            }
        } break;
        default: break;
    }
}

// A circular perk badge: soft glow, dark disc, coloured ring, centred glyph.
static void DrawPerkBadge(float cx, float cy, float r, int perkIdx) {
    Color tint = PERKS[perkIdx].tint;
    DrawCircle((int)cx, (int)(cy + 2), r + 1, (Color){ 0, 0, 0, 120 });        // shadow
    DrawRing((Vector2){cx, cy}, r, r + 4, 0, 360, 40, WithAlpha(tint, 70));    // glow
    DrawCircle((int)cx, (int)cy, r, (Color){ 18, 20, 26, 245 });              // disc
    DrawRing((Vector2){cx, cy}, r - 3.5f, r, 0, 360, 40, tint);                // ring
    DrawPerkGlyph(perkIdx, cx, cy, r * 0.72f, WithAlpha(tint, 255));
}

#define HUD_BADGE_R    19.0f
#define HUD_BADGE_STEP 48          // r*2 + spacing; shared by perks + equipment

static void DrawPerkIcons(int sh, Player *me) {
    int cx = 28 + (int)HUD_BADGE_R, cy = sh - 30;
    for (int i = 0; i < PERK_COUNT; i++) {
        if (!me->hasPerk[i]) continue;
        DrawPerkBadge((float)cx, (float)cy, HUD_BADGE_R, i);
        cx += HUD_BADGE_STEP;
    }
}

// One throwable badge: dark disc + category ring, a glyph, key hint and count.
// Greyed when the player holds none (so the slot is still discoverable).
static void DrawEquipBadge(float cx, float cy, float r, int kind, int count, const char *key) {
    bool any = count > 0;
    Color tint = (kind == 0) ? (Color){ 150, 190, 90, 255 }    // frag — olive
                             : (Color){ 110, 200, 240, 255 };  // stun — cyan
    if (!any) tint = (Color){ 90, 94, 104, 255 };
    Color glyph = any ? WithAlpha(tint, 255) : (Color){ 110, 114, 124, 255 };

    DrawCircle((int)cx, (int)(cy + 2), r + 1, (Color){ 0, 0, 0, 120 });
    DrawCircle((int)cx, (int)cy, r, (Color){ 18, 20, 26, 245 });
    DrawRing((Vector2){cx, cy}, r - 3.5f, r, 0, 360, 36, tint);

    if (kind == 0) {                  // frag — round body + spoon nub
        DrawCircle((int)cx, (int)(cy + r*0.12f), r*0.42f, glyph);
        DrawRectangle((int)(cx - r*0.10f), (int)(cy - r*0.55f), (int)(r*0.20f), (int)(r*0.42f), glyph);
    } else {                          // stun — burst lines from a core
        DrawCircle((int)cx, (int)cy, r*0.22f, glyph);
        for (int a = 0; a < 8; a++) {
            float ang = a * (PI / 4.0f);
            DrawLineEx((Vector2){cx + cosf(ang)*r*0.30f, cy + sinf(ang)*r*0.30f},
                       (Vector2){cx + cosf(ang)*r*0.62f, cy + sinf(ang)*r*0.62f}, 2.0f, glyph);
        }
    }
    char b[8]; snprintf(b, sizeof b, "%d", count);
    HudText(b, (int)(cx + r - 2), (int)(cy + r - 14), 18, any ? HUD_TEXT : HUD_DIM);
    HudText(key, (int)(cx - r + 2), (int)(cy - r - 1), 13, any ? WithAlpha(tint,255) : HUD_DIM);
}

// Equipment continues the bottom-left "loadout" row, just past the owned
// perks, so consumables and perks read as one group (and never collide with
// the bottom-right ammo block).
static void DrawEquipmentIcons(int sw, int sh, Player *me) {
    (void)sw;
    int owned = 0;
    for (int i = 0; i < PERK_COUNT; i++) if (me->hasPerk[i]) owned++;
    int cx = 28 + (int)HUD_BADGE_R + owned * HUD_BADGE_STEP;
    int cy = sh - 30;
    DrawEquipBadge((float)cx, (float)cy, HUD_BADGE_R, 0, me->lethals, "G");
    cx += HUD_BADGE_STEP;
    DrawEquipBadge((float)cx, (float)cy, HUD_BADGE_R, 1, me->tacticals, "H");
}

static const char *CategoryLabel(WeaponCategory c) {
    switch (c) {
        case WC_PRIMARY:  return "PRIMARY";
        case WC_SPECIAL:  return "SPECIAL";
        case WC_MELEE:    return "MELEE";
        case WC_LETHAL:   return "LETHAL";
        case WC_TACTICAL: return "TACTICAL";
        default:          return "";
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

// Dynamic-crosshair smoothing state. `hudCrossGap` is the current pixel
// distance from screen center to each tick; lerps toward the target spread
// each frame. `hudCrossKick` accumulates a per-shot bloom that decays
// regardless of weapon — bigger weapons spike it harder.
static float     hudCrossGap = 6.0f;
static float     hudCrossKick = 0.0f;
static int       hudCrossLastShots = -1;

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
        hudBonusToastAmount = 50 + g_world.roundNum * 10;
        hudBonusToastTimer  = 2.5f;
    }
    // Transition into a fresh round (PRE_GAME or ROUND_BREAK → PLAY): big splash.
    if (gamePhase == GS_PLAY && hudLastPhase != GS_PLAY && g_world.roundNum > 0) {
        hudRoundSplashTimer = 2.6f;
        hudRoundSplashRound = g_world.roundNum;
    }
    hudLastPhase = gamePhase;
    hudLastRound = g_world.roundNum;

    if (hudHitMarkerTimer    > 0) hudHitMarkerTimer    -= dt;
    if (hudDmgDirTimer       > 0) hudDmgDirTimer       -= dt;
    if (hudBonusToastTimer   > 0) hudBonusToastTimer   -= dt;
    if (hudRoundSplashTimer  > 0) hudRoundSplashTimer  -= dt;

    // Crosshair kick: edge-detect new shots and bump bloom; decay every
    // frame so it eases back down between shots.
    if (hudCrossLastShots < 0) hudCrossLastShots = me->shotsFired;
    if (me->shotsFired > hudCrossLastShots) {
        int delta = me->shotsFired - hudCrossLastShots;
        WeaponSlot *s = &me->inventory[me->currentSlot];
        const WeaponDef *cw = &WEAPONS[s->weaponIdx];
        float kickPer = 5.0f + cw->spreadDeg * 1.6f + cw->recoilPitch * 4.0f;
        hudCrossKick += kickPer * (float)delta;
        if (hudCrossKick > 36.0f) hudCrossKick = 36.0f;
    }
    hudCrossLastShots = me->shotsFired;
    hudCrossKick -= dt * 36.0f;
    if (hudCrossKick < 0) hudCrossKick = 0;

    // Target gap = base + weapon spread + sprint + movement bloom + kick,
    // minus an ADS pull-in. Smooth toward target with a framerate-
    // independent exponential blend.
    WeaponSlot *cs = &me->inventory[me->currentSlot];
    const WeaponDef *cwd = &WEAPONS[cs->weaponIdx];
    float target = 6.0f;
    target += cwd->spreadDeg * 0.9f;
    target += me->moveBlend * 6.0f;          // walking opens the reticle
    target += me->sprintBlend * 14.0f;       // sprinting blows it wide
    target += hudCrossKick;
    if (me->adsHeld) target = 1.0f;          // collapse to a dot while ADS
    if (target < 1.0f) target = 1.0f;
    hudCrossGap += (target - hudCrossGap) * (1.0f - expf(-dt * 18.0f));
}

static void HudDrawRoundSplash(int sw, int sh) {
    if (hudRoundSplashTimer <= 0) return;
    float t = hudRoundSplashTimer / 2.6f;          // 1 → 0
    float fadeIn  = 1.0f - (t - 0.6f) / 0.4f;      // first 0.4s fade-in
    if (fadeIn > 1) fadeIn = 1;
    if (fadeIn < 0) fadeIn = 0;
    float fadeOut = (t < 0.4f) ? (t / 0.4f) : 1.0f; // last 0.4s fade-out
    float a = fadeIn * fadeOut;
    // Letters drop in from above; settle by mid-life.
    float settleP = 1.0f - t;
    if (settleP > 1) settleP = 1;
    if (settleP < 0) settleP = 0;
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
    // ADS collapses the reticle to a small dot — the iron sights are the
    // actual aiming reference now.
    if (me->adsHeld && hudCrossGap < 2.5f) {
        DrawCircleLines(cx, cy, 2, cross);
        DrawRectangle(cx - 1, cy - 1, 2, 2, cross);
        return;
    }
    int gap = (int)(hudCrossGap + 0.5f);
    int len = 6;
    int t   = 2;
    Color outline = (Color){ 0, 0, 0, cross.a };

    // Semi-auto weapons drop the top tick (COD marksman-rifle convention).
    const WeaponDef *cwd = &WEAPONS[me->inventory[me->currentSlot].weaponIdx];
    bool drawTop = (cwd->fireMode != FM_SEMI);

    // Outline drawn 1px bigger underneath each tick for legibility against
    // bright surfaces.
    DrawRectangle(cx - gap - len - 1, cy - t/2 - 1, len + 2, t + 2, outline);
    DrawRectangle(cx + gap     - 1,   cy - t/2 - 1, len + 2, t + 2, outline);
    if (drawTop)
        DrawRectangle(cx - t/2 - 1, cy - gap - len - 1, t + 2, len + 2, outline);
    DrawRectangle(cx - t/2 - 1, cy + gap     - 1,   t + 2, len + 2, outline);

    DrawRectangle(cx - gap - len, cy - t/2, len, t, cross);
    DrawRectangle(cx + gap,       cy - t/2, len, t, cross);
    if (drawTop)
        DrawRectangle(cx - t/2, cy - gap - len, t, len, cross);
    DrawRectangle(cx - t/2, cy + gap,       t, len, cross);

    // Centre pip
    DrawRectangle(cx - 1, cy - 1, 2, 2, cross);
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
    HudDrawCrosshair(cx, cy, me, RAYWHITE);
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
    // unframed, COD-style, sitting over a soft dark gradient so it reads
    // against the bright sky.
    {
        char rd[24]; snprintf(rd, sizeof rd, "%d", g_world.roundNum);
        int fs = 58;
        int rw = MeasureText(rd, fs);
        DrawRectangle(sw/2 - 130, 0, 260, 96, (Color){0,0,0,70});
        // "ROUND" caption above the number.
        const char *cap = "ROUND";
        HudText(cap, sw/2 - MeasureText(cap, 14)/2, 12, 14, WithAlpha((Color){220,70,70,255}, 220));
        HudText(rd, sw/2 - rw/2, 26, fs, (Color){224, 64, 64, 255});
        int zleft = enemiesAlive + enemiesToSpawn;
        char zsub[40]; snprintf(zsub, sizeof zsub, "%d  ZOMBIES", zleft);
        HudText(zsub, sw/2 - MeasureText(zsub, 16)/2, 26 + fs + 2, 16, WithAlpha(HUD_TEXT, 220));
    }

    // Bottom-left: HP bar + stamina sliver, sitting above the perk badges.
    {
        float barW = 248, barH = 16;
        float bx = 28, by = sh - 100;
        Rectangle track = { bx, by, barW, barH };
        DrawRectangleRounded(track, 0.5f, 8, (Color){ 0, 0, 0, 150 });
        Color hpCol = (hpFrac > 0.66f) ? (Color){ 92, 200, 96, 240 }
                    : (hpFrac > 0.33f) ? (Color){ 232, 196, 64, 240 }
                                       : (Color){ 226, 64, 64, 240 };
        if (hpFrac > 0.001f) {
            float fw = barW * hpFrac; if (fw < barH) fw = barH;
            DrawRectangleRounded((Rectangle){ bx, by, fw, barH }, 0.5f, 8, hpCol);
            // glossy top highlight
            DrawRectangle((int)(bx + 3), (int)(by + 2), (int)(fw - 6), 2, WithAlpha(WHITE, 60));
        }
        snprintf(buf, sizeof buf, "%d", me->hp);
        HudText(buf, (int)(bx + 8), (int)(by - 1), 14, HUD_TEXT);
        // Stamina sliver under the HP bar.
        float sy = by + barH + 5;
        DrawRectangleRounded((Rectangle){ bx, sy, barW, 5 }, 0.5f, 6, (Color){ 0, 0, 0, 150 });
        float stf = me->stamina / 100.0f;
        if (stf > 0.001f)
            DrawRectangleRounded((Rectangle){ bx, sy, barW * stf, 5 }, 0.5f, 6, (Color){ 150, 180, 240, 220 });
    }

    // Bottom-center: points — the CoD economy anchor, large gold.
    {
        snprintf(buf, sizeof buf, "%d", me->points);
        int fs = 32;
        int tw = MeasureText(buf, fs);
        HudText(buf, sw/2 - tw/2, sh - 50, fs, HUD_GOLD);
        const char *plbl = "POINTS";
        HudText(plbl, sw/2 - MeasureText(plbl, 12)/2, sh - 16, 12, WithAlpha(HUD_DIM, 200));
    }

    // Bottom-right: weapon + ammo, large, no boxed frame. Big ammo number.
    WeaponSlot *cur = &me->inventory[me->currentSlot];
    const WeaponDef *cw = &WEAPONS[cur->weaponIdx];
    const char *displayName = cur->packed ? cw->packedName : cw->name;
    {
        int rx = sw - 28, ry = sh - 92;
        Color nameCol = cur->packed ? (Color){ 214, 170, 255, 255 } : HUD_TEXT;

        if (cur->reloadTimer > 0) {
            HudTextR("RELOADING", rx, ry, 22, (Color){ 240, 150, 40, 255 });
            float t = 1.0f - (cur->reloadTimer / Weapon_EffReload(me, cur));
            DrawRectangleRounded((Rectangle){ rx - 220, ry + 30, 220, 6 }, 0.5f, 6, (Color){ 70, 40, 10, 200 });
            DrawRectangleRounded((Rectangle){ rx - 220, ry + 30, 220 * t, 6 }, 0.5f, 6, (Color){ 240, 150, 40, 255 });
        } else if (PaP_SlotLocked(localPlayerIdx, me->currentSlot)) {
            if (pap.phase == PAP_READY) {
                HudTextR("READY - TAKE IT", rx, ry, 22, (Color){ 200, 150, 255, 255 });
            } else {
                HudTextR("PACK-A-PUNCH", rx, ry, 22, (Color){ 200, 150, 255, 255 });
                // Progress across both timed phases (insert + work).
                float total  = PAP_INSERT_TIME + PAP_WORK_TIME;
                float remain = pap.timer + (pap.phase == PAP_INSERT ? PAP_WORK_TIME : 0.0f);
                float t = 1.0f - remain / total;
                DrawRectangleRounded((Rectangle){ rx - 220, ry + 30, 220, 6 }, 0.5f, 6, (Color){ 40, 20, 60, 200 });
                DrawRectangleRounded((Rectangle){ rx - 220, ry + 30, 220 * t, 6 }, 0.5f, 6, (Color){ 200, 150, 255, 255 });
            }
        } else {
            int nameW = MeasureText(displayName, 22);
            HudTextR(displayName, rx, ry, 22, nameCol);
            // Category tag above the name — "PRIMARY" / "SPECIAL" / etc.
            const char *cat = CategoryLabel(cw->category);
            if (cat[0]) HudTextR(cat, rx, ry - 12, 11, WithAlpha(HUD_DIM, 220));
            // Weapon-tint accent line under the name.
            DrawRectangle(rx - nameW, ry + 25, nameW, 3, cw->tint);
            // Big ammo / reserve, right-aligned.
            char ammo[16]; snprintf(ammo, sizeof ammo, "%d", cur->ammo);
            char rsv[24];  snprintf(rsv, sizeof rsv, "/ %d", cur->reserve);
            int af = 46, rf = 22;
            int rw = MeasureText(rsv, rf);
            Color ammoCol = (cur->ammo == 0) ? (Color){ 232, 64, 64, 255 } : HUD_GOLD;
            HudTextR(rsv,  rx,          ry + 36, rf, WithAlpha(HUD_DIM, 220));
            HudTextR(ammo, rx - rw - 8, ry + 24, af, ammoCol);
        }
    }

    // Tiny alt-slot indicator above the weapon block.
    WeaponSlot *other = &me->inventory[(me->currentSlot + 1) % INV_SLOTS];
    {
        int rx = sw - 28, ry = sh - 112;
        if (other->owned) {
            const WeaponDef *ow = &WEAPONS[other->weaponIdx];
            const char *on = other->packed ? ow->packedName : ow->name;
            snprintf(buf, sizeof buf, "[Q]  %s   %d / %d", on, other->ammo, other->reserve);
        } else {
            snprintf(buf, sizeof buf, "[Q]  (empty)");
        }
        HudTextR(buf, rx, ry, 14, WithAlpha(HUD_DIM, 200));
    }

    DrawPerkIcons(sh, me);
    DrawEquipmentIcons(sw, sh, me);

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
    if (g_world.doublePointsTimer > 0) {
        char b[32]; snprintf(b, sizeof b, "DOUBLE POINTS  %.1fs", g_world.doublePointsTimer);
        int tw = MeasureText(b, 20);
        DrawText(b, sw/2 - tw/2, puY, 20, (Color){240,220,60,255});
        puY += 24;
    }
    if (g_world.instaKillTimer > 0) {
        char b[32]; snprintf(b, sizeof b, "INSTA-KILL  %.1fs", g_world.instaKillTimer);
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
            if (pap.phase == PAP_READY) {
                if (pap.ownerPlayer == localPlayerIdx) snprintf(prompt, sizeof prompt, "[F]  TAKE WEAPON");
                else { snprintf(prompt, sizeof prompt, "In use"); promptColor = GRAY; }
            }
            else if (pap.phase != PAP_IDLE) {
                if (pap.ownerPlayer == localPlayerIdx) snprintf(prompt, sizeof prompt, "Upgrading...");
                else snprintf(prompt, sizeof prompt, "In use");
                promptColor = (Color){200,150,255,255};
            }
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
        char rb[64]; snprintf(rb, sizeof rb, "ROUND  %d", g_world.roundNum + 1);
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
