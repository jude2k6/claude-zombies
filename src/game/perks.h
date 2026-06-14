#ifndef SHOOTER_PERKS_H
#define SHOOTER_PERKS_H

#include "types.h"

extern const PerkDef PERKS[PERK_COUNT];

int   Perk_EffMaxHP(Player *p);
float Perk_EffMoveSpeed(Player *p);

#endif
