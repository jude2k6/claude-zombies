#include "game.h"
#include "player.h"
#include "weapons.h"
#include "perks.h"
#include "entities.h"
#include "interact.h"
#include "level.h"

int        roundNum = 0;
GamePhase  gamePhase = GS_PRE_GAME;
float      roundBreakTimer = 0.0f;

void Game_StartRound(int r) {
    roundNum = r;
    enemiesToSpawn = Enemies_RoundSpawnCount(r);
    enemiesAlive = 0;
    spawnTimer = 1.0f;
    Enemies_ClearAll();
    Bullets_ClearAll();
    for (int i = 0; i < windowCount; i++) {
        windows[i].boards = MAX_BOARDS_PER_WIN;
        windows[i].repairProgress = 0;
        windows[i].repairPlayer = -1;
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!players[i].active) continue;
        // Anyone who was dead or downed comes back upright at full HP, on
        // their original spawn point (re-pick from the map's spawn list).
        bool wasOut = !players[i].alive || players[i].downed;
        if (wasOut) {
            players[i].alive = true;
            players[i].downed = false;
            players[i].bleedTimer = 0;
            players[i].reviveAsTarget = 0;
            players[i].reviverIdx = -1;
            players[i].hp = Perk_EffMaxHP(&players[i]);
            players[i].pos = Player_Spawn(i);
            WeaponSlot *s = &players[i].inventory[players[i].currentSlot];
            if (s->owned) {
                int need = Weapon_EffMagSize(s) - s->ammo;
                int take = (need < s->reserve) ? need : s->reserve;
                s->ammo += take; s->reserve -= take;
            }
        }
        if (r > players[i].highestRound) players[i].highestRound = r;
    }
    gamePhase = GS_PLAY;
}

void Game_Tick(float dt) {
    // Per-player weapon timers + fire-held resolution
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &players[i];
        if (!p->active) continue;
        for (int s = 0; s < INV_SLOTS; s++) {
            if (!p->inventory[s].owned) continue;
            if (p->inventory[s].fireTimer   > 0) p->inventory[s].fireTimer   -= dt;
            if (p->inventory[s].reloadTimer > 0) {
                p->inventory[s].reloadTimer -= dt;
                if (p->inventory[s].reloadTimer <= 0) {
                    p->inventory[s].reloadTimer = 0;
                    Weapon_FinishReloadIfReady(p, &p->inventory[s]);
                }
            }
        }
        if (p->alive && p->fireHeld) {
            WeaponSlot *cs = &p->inventory[p->currentSlot];
            const WeaponDef *cw = &WEAPONS[cs->weaponIdx];
            if (cw->automatic || cs->fireTimer <= 0) {
                if (cs->ammo <= 0 && cs->reserve > 0 && cs->reloadTimer <= 0) Weapon_StartReload(p);
                else Weapon_Fire(p);
            }
        }
        if (p->damageFlash > 0) p->damageFlash -= dt * 1.5f;
        if (p->meleeTimer  > 0) p->meleeTimer  -= dt;
    }

    Bullets_Update(dt);
    Enemies_Update(dt);
    Enemies_Separate();
    Enemies_UpdateSpawns(roundNum, dt);
    Interact_UpdatePaP(dt);
    Interact_UpdateRepairs(dt);
    Interact_UpdateMBox(dt);
    PowerUps_Update(dt);
    PowerUps_Pickup();

    // Bleed-out: downed players lose their bleed timer. When it expires they
    // fully die. If there's nobody else upright who could revive them, the
    // timer drains 4x faster — keeps solo-style deaths snappy.
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &players[i];
        if (!p->active || !p->alive || !p->downed) continue;
        int rescuers = 0;
        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
            if (j == i) continue;
            if (players[j].active && players[j].alive && !players[j].downed) rescuers++;
        }
        float rate = (rescuers > 0) ? 1.0f : 4.0f;
        p->bleedTimer -= dt * rate;
        if (p->bleedTimer <= 0) {
            p->bleedTimer = 0;
            p->downed = false;
            p->alive = false;
            p->reviveAsTarget = 0;
            p->reviverIdx = -1;
        }
    }

    if (gamePhase == GS_PLAY) {
        if (Player_AliveActiveCount() == 0 && Player_ActiveCount() > 0) {
            gamePhase = GS_GAME_OVER;
        } else if (enemiesAlive <= 0 && enemiesToSpawn <= 0) {
            gamePhase = GS_ROUND_BREAK;
            roundBreakTimer = 4.0f;
            int bonus = 50 + roundNum * 10;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (players[i].active) players[i].points += bonus;
            for (int i = 0; i < windowCount; i++) windows[i].boards = MAX_BOARDS_PER_WIN;
        }
    } else if (gamePhase == GS_ROUND_BREAK) {
        roundBreakTimer -= dt;
        if (roundBreakTimer <= 0) Game_StartRound(roundNum + 1);
    }
}
