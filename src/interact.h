#ifndef SHOOTER_INTERACT_H
#define SHOOTER_INTERACT_H

#include "types.h"

Interact Interact_FindFor(Player *p);

void Interact_BuyAtWall(Player *p, int wbIdx);
void Interact_BuyPerk(Player *p, int pmIdx);
void Interact_BuyDoor(Player *p, int doorIdx);
void Interact_UsePackAPunch(Player *p);
void Interact_UseMBox(Player *p);
void Interact_Do(Player *p);                  // single-press F dispatch

void Interact_UpdatePaP(float dt);
bool PaP_SlotLocked(int playerIdx, int slot);  // weapon is in the PaP machine
void Interact_UpdateRepairs(float dt);
void Interact_UpdateMBox(float dt);

#endif
