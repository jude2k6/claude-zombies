#ifndef SHOOTER_PERKS_H
#define SHOOTER_PERKS_H

#include "types.h"

extern PerkDef PERKS[PERK_COUNT];

// Overlay name/cost/tint onto PERKS[] from the perks/*.perk catalog (matched to
// PerkId by id). The static initialiser in perks.c is the fallback, so a missing
// catalog keeps the historical values. Called from Assets_Load. The perk EFFECT
// stays in code (Perk_EffMaxHP etc.) — only the data is file-driven.
void Perks_Load(void);

int   Perk_EffMaxHP(Player *p);
float Perk_EffMoveSpeed(Player *p);

#endif
