#include "perks.h"

const PerkDef PERKS[PERK_COUNT] = {
    [PERK_JUG]    = { "Juggernog",   2500, (Color){220, 40, 40, 255} },
    [PERK_SPEED]  = { "Speed Cola",  3000, (Color){ 40,180, 80, 255} },
    [PERK_DTAP]   = { "Double Tap",  2000, (Color){240,180, 40, 255} },
    [PERK_STAMIN] = { "Stamin-Up",   2000, (Color){ 60,140,220, 255} },
};

int Perk_EffMaxHP(Player *p) {
    return p->hasPerk[PERK_JUG] ? 250 : 100;
}

float Perk_EffMoveSpeed(Player *p) {
    return BASE_MOVE_SPEED * (p->hasPerk[PERK_STAMIN] ? 1.4f : 1.0f);
}
