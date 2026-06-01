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
    Vector2 papXZ = { pap.pos.x, pap.pos.z };
    float dp = Vector2Distance(pXZ, papXZ);
    if (dp < best.dist) best = (Interact){ IK_PAP, 0, dp };

    for (int i = 0; i < windowCount; i++) {
        if (windows[i].boards >= MAX_BOARDS_PER_WIN) continue;
        Vector2 q = { windows[i].pos.x, windows[i].pos.z };
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
    int meIdx = (int)(p - players);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == meIdx) continue;
        if (!players[i].active || !players[i].downed) continue;
        Vector2 q = { players[i].pos.x, players[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < INTERACT_DIST && d < best.dist) best = (Interact){ IK_REVIVE, i, d };
    }
    // Mystery Box
    if (mbox.placed) {
        Vector2 q = { mbox.pos.x, mbox.pos.z };
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

void Interact_UsePackAPunch(Player *p) {
    if (pap.activeTimer > 0) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned || s->packed) return;
    if (p->points < PAP_COST) return;
    p->points -= PAP_COST;
    pap.activeTimer = PAP_DURATION;
    pap.slotInProgress = p->currentSlot;
    pap.ownerPlayer = (int)(p - players);
}

void Interact_UseMBox(Player *p) {
    if (!mbox.placed) return;
    int meIdx = (int)(p - players);
    if (mbox.state == MBOX_IDLE) {
        if (p->points < MBOX_COST) return;
        p->points -= MBOX_COST;
        mbox.state = MBOX_ROLLING;
        mbox.timer = MBOX_ROLL_TIME;
        mbox.ownerPlayer = meIdx;
        mbox.finalWeapon = rand() % W_COUNT;
        mbox.showingWeapon = 0;
    } else if (mbox.state == MBOX_WAITING && meIdx == mbox.ownerPlayer) {
        const WeaponDef *w = &WEAPONS[mbox.finalWeapon];
        int slot = Weapon_FirstEmptySlot(p);
        if (slot < 0) slot = p->currentSlot;
        p->inventory[slot] = (WeaponSlot){
            .weaponIdx = mbox.finalWeapon,
            .ammo = w->magSize,
            .reserve = w->reserveMax,
            .owned = true,
        };
        p->currentSlot = slot;
        mbox.state = MBOX_IDLE;
        mbox.ownerPlayer = -1;
    }
}

void Interact_UpdateMBox(float dt) {
    if (!mbox.placed) return;
    mbox.bob += dt * 3.0f;
    if (mbox.state == MBOX_ROLLING) {
        mbox.timer -= dt;
        // Slot-machine deceleration: rate(t) drops from ~12 Hz at the
        // start to ~1 Hz near the end. Integrated flip count is
        // f(u) = 12u - (11/8)u² where u = elapsed time. In the final
        // half-second we lock to the actual finalWeapon so the box
        // visibly "lands" on its pick before swapping to WAITING.
        if (mbox.timer < 0.5f) {
            mbox.showingWeapon = mbox.finalWeapon;
        } else {
            float u = MBOX_ROLL_TIME - mbox.timer;
            float flips = 12.0f * u - (11.0f / 8.0f) * u * u;
            int idx = ((int)flips) % W_COUNT;
            if (idx < 0) idx += W_COUNT;
            mbox.showingWeapon = idx;
        }
        if (mbox.timer <= 0) {
            mbox.state = MBOX_WAITING;
            mbox.timer = MBOX_WAIT_TIME;
            mbox.showingWeapon = mbox.finalWeapon;
        }
    } else if (mbox.state == MBOX_WAITING) {
        mbox.timer -= dt;
        if (mbox.timer <= 0) {
            mbox.state = MBOX_IDLE;
            mbox.ownerPlayer = -1;
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
    pap.bob += dt * 3.0f;
    if (pap.activeTimer > 0) {
        pap.activeTimer -= dt;
        if (pap.activeTimer <= 0) {
            pap.activeTimer = 0;
            if (pap.ownerPlayer >= 0 && pap.slotInProgress >= 0) {
                WeaponSlot *s = &players[pap.ownerPlayer].inventory[pap.slotInProgress];
                if (s->owned) {
                    s->packed = true;
                    s->ammo = Weapon_EffMagSize(s);
                    s->reserve = Weapon_EffReserveMax(s);
                    s->reloadTimer = 0;
                }
            }
            pap.slotInProgress = -1;
            pap.ownerPlayer = -1;
        }
    }
}

void Interact_UpdateRepairs(float dt) {
    bool winActive[MAX_WINDOWS] = { false };
    bool revActive[NET_MAX_PLAYERS] = { false };

    for (int pi = 0; pi < NET_MAX_PLAYERS; pi++) {
        Player *p = &players[pi];
        if (!p->active || !p->alive || !p->interactHeld) continue;
        Interact ix = Interact_FindFor(p);
        if (ix.kind == IK_WINDOW) {
            Window3D *w = &windows[ix.idx];
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
            Player *tgt = &players[ti];
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
    for (int i = 0; i < windowCount; i++) {
        if (!winActive[i]) {
            windows[i].repairProgress = 0;
            windows[i].repairPlayer = -1;
        }
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!revActive[i]) {
            // decay so an interrupted revive doesn't snap to 0 instantly
            players[i].reviveAsTarget *= (1.0f - dt * 2.0f);
            if (players[i].reviveAsTarget < 0) players[i].reviveAsTarget = 0;
            players[i].reviverIdx = -1;
        }
    }
}
