#include "interact.h"
#include "weapons.h"
#include "perks.h"
#include "player.h"
#include "level.h"
#include "raymath.h"
#include <stdlib.h>

Interact Interact_FindFor(Player *p) {
    Interact best = { IK_NONE, -1, INTERACT_DIST };
    Vector2 pXZ = { p->pos.x, p->pos.z };

    for (int i = 0; i < wallBuyCount; i++) {
        Vector2 q = { wallBuys[i].pos.x, wallBuys[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < best.dist) best = (Interact){ IK_WALLBUY, i, d };
    }
    for (int i = 0; i < perkMachineCount; i++) {
        Vector2 q = { perkMachines[i].pos.x, perkMachines[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < best.dist) best = (Interact){ IK_PERK, i, d };
    }
    Vector2 papXZ = { g_world.pap.pos.x, g_world.pap.pos.z };
    float dp = Vector2Distance(pXZ, papXZ);
    if (dp < best.dist) best = (Interact){ IK_PAP, 0, dp };

    for (int i = 0; i < g_world.windowCount; i++) {
        if (g_world.windows[i].boards >= MAX_BOARDS_PER_WIN) continue;
        Vector2 q = { g_world.windows[i].pos.x, g_world.windows[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < best.dist) best = (Interact){ IK_WINDOW, i, d };
    }
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].opened) continue;
        Vector2 q = { doors[i].box.center.x, doors[i].box.center.z };
        float d = Vector2Distance(pXZ, q);
        if (d < DOOR_INTERACT_DIST && d < best.dist + 0.5f)
            best = (Interact){ IK_DOOR, i, d };
    }
    // Downed teammates within reach. Dead (bled out) teammates can't be
    // revived mid-round — they respawn at the next round break.
    int meIdx = (int)(p - g_world.players);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == meIdx) continue;
        if (!g_world.players[i].active || !g_world.players[i].downed) continue;
        Vector2 q = { g_world.players[i].pos.x, g_world.players[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < INTERACT_DIST && d < best.dist) best = (Interact){ IK_REVIVE, i, d };
    }
    // Mystery Box
    if (g_world.mbox.placed) {
        Vector2 q = { g_world.mbox.pos.x, g_world.mbox.pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < INTERACT_DIST && d < best.dist) best = (Interact){ IK_MBOX, 0, d };
    }
    return best;
}

void Interact_BuyAtWall(Player *p, int wbIdx) {
    if (wbIdx < 0) return;
    WallBuy *wb = &wallBuys[wbIdx];
    const WeaponDef *w = &WEAPONS[wb->weaponIdx];
    int ownedSlot = Weapon_FindOwnedSlot(p, wb->weaponIdx);
    if (ownedSlot >= 0) {
        WeaponSlot *s = &p->inventory[ownedSlot];
        int cap = Weapon_EffReserveMax(s);
        if (s->reserve >= cap) return;
        if (p->points < w->ammoPrice) return;
        p->points -= w->ammoPrice;
        s->reserve = cap;
    } else {
        if (p->points < w->buyPrice) return;
        p->points -= w->buyPrice;
        int slot = Weapon_FirstEmptySlot(p);
        if (slot < 0) slot = p->currentSlot;
        p->inventory[slot] = (WeaponSlot){
            .weaponIdx = wb->weaponIdx, .ammo = w->magSize,
            .reserve = w->reserveMax, .owned = true,
        };
        p->currentSlot = slot;
    }
}

void Interact_BuyPerk(Player *p, int pmIdx) {
    if (pmIdx < 0) return;
    int pIdx = perkMachines[pmIdx].perkIdx;
    if (p->hasPerk[pIdx]) return;
    if (p->points < PERKS[pIdx].cost) return;
    p->points -= PERKS[pIdx].cost;
    p->hasPerk[pIdx] = true;
    if (pIdx == PERK_JUG) p->hp = Perk_EffMaxHP(p);
}

void Interact_BuyDoor(Player *p, int doorIdx) {
    if (doorIdx < 0 || doorIdx >= doorCount) return;
    Door *d = &doors[doorIdx];
    if (d->opened) return;
    if (p->points < d->cost) return;
    p->points -= d->cost;
    d->opened = true;
}

// True while (playerIdx, slot) is the weapon sitting in the PaP machine — it's
// out of the player's hands, so the viewmodel hides and firing is blocked.
bool PaP_SlotLocked(int playerIdx, int slot) {
    return g_world.pap.phase != PAP_IDLE &&
           g_world.pap.ownerPlayer == playerIdx && g_world.pap.slotInProgress == slot;
}

// Apply the upgrade to the owner's locked slot and free the machine.
static void PaP_Finalize(void) {
    if (g_world.pap.ownerPlayer >= 0 && g_world.pap.slotInProgress >= 0) {
        WeaponSlot *s = &g_world.players[g_world.pap.ownerPlayer].inventory[g_world.pap.slotInProgress];
        if (s->owned && s->weaponIdx == g_world.pap.weaponIdx) {
            s->packed = true;
            s->ammo = Weapon_EffMagSize(s);
            s->reserve = Weapon_EffReserveMax(s);
            s->reloadTimer = 0;
        }
    }
    g_world.pap.phase = PAP_IDLE;
    g_world.pap.timer = 0;
    g_world.pap.slotInProgress = -1;
    g_world.pap.ownerPlayer = -1;
    g_world.pap.weaponIdx = -1;
}

void Interact_UsePackAPunch(Player *p) {
    int meIdx = (int)(p - g_world.players);
    // READY: only the owner can take the finished weapon out.
    if (g_world.pap.phase == PAP_READY) {
        if (g_world.pap.ownerPlayer == meIdx) PaP_Finalize();
        return;
    }
    // Any other non-idle phase: machine is busy.
    if (g_world.pap.phase != PAP_IDLE) return;
    // IDLE: start a new job with the currently-held weapon.
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned || s->packed) return;
    if (p->points < PAP_COST) return;
    p->points -= PAP_COST;
    g_world.pap.phase = PAP_INSERT;
    g_world.pap.timer = PAP_INSERT_TIME;
    g_world.pap.slotInProgress = p->currentSlot;
    g_world.pap.ownerPlayer = meIdx;
    g_world.pap.weaponIdx = s->weaponIdx;
}

void Interact_UseMBox(Player *p) {
    if (!g_world.mbox.placed) return;
    int meIdx = (int)(p - g_world.players);
    if (g_world.mbox.state == MBOX_IDLE) {
        if (p->points < MBOX_COST) return;
        p->points -= MBOX_COST;
        g_world.mbox.state = MBOX_ROLLING;
        g_world.mbox.timer = MBOX_ROLL_TIME;
        g_world.mbox.ownerPlayer = meIdx;
        // Weighted roll — per-weapon `mbox_weight` from the .weapon files
        // (0 = never rolls; uniform fallback if every weight is zero).
        float totalW = 0.0f;
        for (int w = 0; w < W_COUNT; w++)
            if (WEAPONS[w].mboxWeight > 0.0f) totalW += WEAPONS[w].mboxWeight;
        if (totalW > 0.0f) {
            float r = ((float)rand() / (float)RAND_MAX) * totalW;
            int pick = 0;
            for (int w = 0; w < W_COUNT; w++) {
                if (WEAPONS[w].mboxWeight <= 0.0f) continue;
                pick = w;
                r -= WEAPONS[w].mboxWeight;
                if (r <= 0.0f) break;
            }
            g_world.mbox.finalWeapon = pick;
        } else {
            g_world.mbox.finalWeapon = rand() % W_COUNT;
        }
        g_world.mbox.showingWeapon = 0;
    } else if (g_world.mbox.state == MBOX_WAITING && meIdx == g_world.mbox.ownerPlayer) {
        const WeaponDef *w = &WEAPONS[g_world.mbox.finalWeapon];
        int slot = Weapon_FirstEmptySlot(p);
        if (slot < 0) slot = p->currentSlot;
        p->inventory[slot] = (WeaponSlot){
            .weaponIdx = g_world.mbox.finalWeapon,
            .ammo = w->magSize,
            .reserve = w->reserveMax,
            .owned = true,
        };
        p->currentSlot = slot;
        g_world.mbox.state = MBOX_IDLE;
        g_world.mbox.ownerPlayer = -1;
    }
}

void Interact_UpdateMBox(float dt) {
    if (!g_world.mbox.placed) return;
    g_world.mbox.bob += dt * 3.0f;
    if (g_world.mbox.state == MBOX_ROLLING) {
        g_world.mbox.timer -= dt;
        // Slot-machine deceleration: rate(t) drops from ~12 Hz at the
        // start to ~1 Hz near the end. Integrated flip count is
        // f(u) = 12u - (11/8)u² where u = elapsed time. In the final
        // half-second we lock to the actual finalWeapon so the box
        // visibly "lands" on its pick before swapping to WAITING.
        if (g_world.mbox.timer < 0.5f) {
            g_world.mbox.showingWeapon = g_world.mbox.finalWeapon;
        } else {
            float u = MBOX_ROLL_TIME - g_world.mbox.timer;
            float flips = 12.0f * u - (11.0f / 8.0f) * u * u;
            int idx = ((int)flips) % W_COUNT;
            if (idx < 0) idx += W_COUNT;
            g_world.mbox.showingWeapon = idx;
        }
        if (g_world.mbox.timer <= 0) {
            g_world.mbox.state = MBOX_WAITING;
            g_world.mbox.timer = MBOX_WAIT_TIME;
            g_world.mbox.showingWeapon = g_world.mbox.finalWeapon;
        }
    } else if (g_world.mbox.state == MBOX_WAITING) {
        g_world.mbox.timer -= dt;
        if (g_world.mbox.timer <= 0) {
            g_world.mbox.state = MBOX_IDLE;
            g_world.mbox.ownerPlayer = -1;
        }
    }
}

void Interact_Do(Player *p) {
    Interact ix = Interact_FindFor(p);
    if      (ix.kind == IK_WALLBUY) Interact_BuyAtWall(p, ix.idx);
    else if (ix.kind == IK_PERK)    Interact_BuyPerk(p, ix.idx);
    else if (ix.kind == IK_PAP)     Interact_UsePackAPunch(p);
    else if (ix.kind == IK_DOOR)    Interact_BuyDoor(p, ix.idx);
    else if (ix.kind == IK_MBOX)    Interact_UseMBox(p);
}

void Interact_UpdatePaP(float dt) {
    g_world.pap.bob += dt * 3.0f;
    if (g_world.pap.phase == PAP_IDLE) return;

    // Safety: never strand a weapon. If the owner vanished or died, finalize
    // (apply the upgrade if the slot is still valid) and free the machine.
    if (g_world.pap.ownerPlayer < 0 || g_world.pap.ownerPlayer >= NET_MAX_PLAYERS ||
        !g_world.players[g_world.pap.ownerPlayer].active || !g_world.players[g_world.pap.ownerPlayer].alive) {
        PaP_Finalize();
        return;
    }

    switch (g_world.pap.phase) {
        case PAP_INSERT:
            g_world.pap.timer -= dt;
            if (g_world.pap.timer <= 0) { g_world.pap.phase = PAP_WORK; g_world.pap.timer = PAP_WORK_TIME; }
            break;
        case PAP_WORK:
            g_world.pap.timer -= dt;
            if (g_world.pap.timer <= 0) { g_world.pap.phase = PAP_READY; g_world.pap.timer = 0; }
            break;
        case PAP_READY:
            // Waits indefinitely for the owner to manually retrieve (Use).
            break;
        default: break;
    }
}

void Interact_UpdateRepairs(float dt) {
    bool winActive[MAX_WINDOWS] = { false };
    bool revActive[NET_MAX_PLAYERS] = { false };

    for (int pi = 0; pi < NET_MAX_PLAYERS; pi++) {
        Player *p = &g_world.players[pi];
        if (!p->active || !p->alive || !p->interactHeld) continue;
        Interact ix = Interact_FindFor(p);
        if (ix.kind == IK_WINDOW) {
            Window3D *w = &g_world.windows[ix.idx];
            if (w->boards >= MAX_BOARDS_PER_WIN) continue;
            if (w->repairPlayer < 0 || w->repairPlayer == pi) {
                w->repairPlayer = pi;
                w->repairProgress += dt / BOARD_REPAIR_TIME;
                winActive[ix.idx] = true;
                if (w->repairProgress >= 1.0f) {
                    w->boards++;
                    w->repairProgress = 0;
                    p->points += BOARD_REPAIR_PTS;
                    w->repairPlayer = -1;
                }
            }
        }
        else if (ix.kind == IK_REVIVE) {
            int ti = ix.idx;
            if (ti < 0 || ti >= NET_MAX_PLAYERS) continue;
            Player *tgt = &g_world.players[ti];
            if (!tgt->active || !tgt->downed) continue;
            tgt->reviverIdx = pi;
            tgt->reviveAsTarget += dt / REVIVE_TIME;
            revActive[ti] = true;
            if (tgt->reviveAsTarget >= 1.0f) {
                tgt->downed = false;
                tgt->bleedTimer = 0;
                tgt->hp = Perk_EffMaxHP(tgt);
                tgt->reviveAsTarget = 0;
                tgt->reviverIdx = -1;
                p->revives++;
            }
        }
    }
    // Clear inactive repair / revive accumulators
    for (int i = 0; i < g_world.windowCount; i++) {
        if (!winActive[i]) {
            g_world.windows[i].repairProgress = 0;
            g_world.windows[i].repairPlayer = -1;
        }
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!revActive[i]) {
            // decay so an interrupted revive doesn't snap to 0 instantly
            g_world.players[i].reviveAsTarget *= (1.0f - dt * 2.0f);
            if (g_world.players[i].reviveAsTarget < 0) g_world.players[i].reviveAsTarget = 0;
            g_world.players[i].reviverIdx = -1;
        }
    }
}
