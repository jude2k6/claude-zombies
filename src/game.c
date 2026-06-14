#include "game.h"
#include "player.h"
#include "weapons.h"
#include "perks.h"
#include "entities.h"
#include "interact.h"
#include "level.h"
#include "decals.h"

// roundNum + gamePhase now live in g_world (world.h); zero-init matches the old
// defaults (round 0, GS_PRE_GAME == 0).
float      roundBreakTimer = 0.0f;

void Game_StartRound(int r) {
    g_world.roundNum = r;
    enemiesToSpawn = Enemies_RoundSpawnCount(r);
    enemiesAlive = 0;
    spawnTimer = 1.0f;
    Enemies_ClearAll();
    Bullets_ClearAll();
    Throwables_ClearAll();
    for (int i = 0; i < g_world.windowCount; i++) {
        g_world.windows[i].boards = MAX_BOARDS_PER_WIN;
        g_world.windows[i].repairProgress = 0;
        g_world.windows[i].repairPlayer = -1;
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g_world.players[i].active) continue;
        // Anyone who was dead or downed comes back upright at full HP, on
        // their original spawn point (re-pick from the map's spawn list).
        bool wasOut = !g_world.players[i].alive || g_world.players[i].downed;
        if (wasOut) {
            g_world.players[i].alive = true;
            g_world.players[i].downed = false;
            g_world.players[i].bleedTimer = 0;
            g_world.players[i].reviveAsTarget = 0;
            g_world.players[i].reviverIdx = -1;
            g_world.players[i].hp = Perk_EffMaxHP(&g_world.players[i]);
            g_world.players[i].pos = Player_Spawn(i);
            WeaponSlot *s = &g_world.players[i].inventory[g_world.players[i].currentSlot];
            if (s->owned) {
                int need = Weapon_EffMagSize(s) - s->ammo;
                int take = (need < s->reserve) ? need : s->reserve;
                s->ammo += take; s->reserve -= take;
            }
        }
        if (r > g_world.players[i].highestRound) g_world.players[i].highestRound = r;
    }
    gamePhase = GS_PLAY;
}

void Game_Tick(float dt) {
    // Per-player weapon timers + fire-mode resolution
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &g_world.players[i];
        if (!p->active) continue;
        for (int s = 0; s < INV_SLOTS; s++) {
            WeaponSlot *ws = &p->inventory[s];
            if (!ws->owned) continue;
            if (ws->fireTimer   > 0) ws->fireTimer   -= dt;
            if (ws->burstTimer  > 0) ws->burstTimer  -= dt;
            if (ws->reloadTimer > 0) {
                ws->reloadTimer -= dt;
                if (ws->reloadTimer <= 0) {
                    ws->reloadTimer = 0;
                    Weapon_FinishReloadIfReady(p, ws);
                }
            }
        }

        if (p->alive) {
            WeaponSlot *cs = &p->inventory[p->currentSlot];
            const WeaponDef *cw = &WEAPONS[cs->weaponIdx];

            // Burst progression — runs even if the trigger was released after
            // the burst started (canonical burst behavior).
            if (cs->burstRemaining > 0 && cs->burstTimer <= 0 && cs->reloadTimer <= 0) {
                if (cs->ammo > 0) {
                    Weapon_Fire(p);
                    cs->burstRemaining--;
                    if (cs->burstRemaining > 0) {
                        // Mid-burst — override the inter-shot fireTimer (set
                        // by Weapon_Fire) so burstInterval gates next shot.
                        cs->fireTimer  = 0.0f;
                        cs->burstTimer = cw->burstInterval;
                    }
                    // Last shot leaves fireTimer at full cooldown (post-burst)
                } else {
                    cs->burstRemaining = 0;
                }
            }

            bool firePressed = p->fireHeld && !p->prevFireHeld;

            if (p->fireHeld && cs->burstRemaining == 0 && cs->reloadTimer <= 0) {
                // Auto-reload empty mag if we have reserve
                if (cs->ammo <= 0 && cs->reserve > 0 && cs->fireTimer <= 0) {
                    Weapon_StartReload(p);
                } else if (cs->fireTimer <= 0 && cs->ammo > 0) {
                    switch (cw->fireMode) {
                        case FM_AUTO:
                            Weapon_Fire(p);
                            break;
                        case FM_SEMI:
                            if (firePressed) Weapon_Fire(p);
                            break;
                        case FM_BURST:
                            if (firePressed) {
                                cs->burstRemaining = cw->burstCount;
                                cs->burstTimer = 0.0f;  // first shot immediate next tick
                            }
                            break;
                    }
                }
            }
        }
        p->prevFireHeld = p->fireHeld;

        if (p->damageFlash > 0) p->damageFlash -= dt * 1.5f;
        if (p->meleeTimer  > 0) p->meleeTimer  -= dt;

        // HP regeneration: heal back to max after REGEN_DELAY damage-free
        // seconds. Downed g_world.players don't regen (only revive brings them back).
        if (p->alive && !p->downed) {
            p->regenTimer += dt;
            if (p->regenTimer >= REGEN_DELAY) {
                int maxhp = Perk_EffMaxHP(p);
                if (p->hp < maxhp) {
                    p->hp += (int)(REGEN_RATE * dt + 0.5f);
                    if (p->hp > maxhp) p->hp = maxhp;
                }
            }
        }
    }

    Bullets_Update(dt);
    Throwables_Update(dt);
    Decals_Update(dt);
    Enemies_Update(dt);
    Enemies_Separate();
    Enemies_UpdateSpawns(g_world.roundNum, dt);
    Interact_UpdatePaP(dt);
    Interact_UpdateRepairs(dt);
    Interact_UpdateMBox(dt);
    PowerUps_Update(dt);
    PowerUps_Pickup();

    // Bleed-out: downed g_world.players lose their bleed timer. When it expires they
    // fully die. If there's nobody else upright who could revive them, the
    // timer drains 4x faster — keeps solo-style deaths snappy.
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &g_world.players[i];
        if (!p->active || !p->alive || !p->downed) continue;
        int rescuers = 0;
        for (int j = 0; j < NET_MAX_PLAYERS; j++) {
            if (j == i) continue;
            if (g_world.players[j].active && g_world.players[j].alive && !g_world.players[j].downed) rescuers++;
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
            int bonus = 50 + g_world.roundNum * 10;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (g_world.players[i].active) g_world.players[i].points += bonus;
            for (int i = 0; i < g_world.windowCount; i++) g_world.windows[i].boards = MAX_BOARDS_PER_WIN;
        }
    } else if (gamePhase == GS_ROUND_BREAK) {
        roundBreakTimer -= dt;
        if (roundBreakTimer <= 0) Game_StartRound(g_world.roundNum + 1);
    }
}
