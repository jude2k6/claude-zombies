#ifndef SHOOTER_PLAYER_H
#define SHOOTER_PLAYER_H

#include "types.h"

extern Player players[NET_MAX_PLAYERS];
extern int    localPlayerIdx;
extern bool   godMode;

int     Player_ActiveCount(void);
int     Player_AliveActiveCount(void);
int     Player_NearestAlive(Vector3 pos);

Vector3 Player_Spawn(int idx);
Vector3 Player_LookDir(float yaw, float pitch);

void    Player_GiveStartingPistol(Player *p);
void    Player_ResetForGame(int idx, const char *name);

void    Player_ApplyLocalLook(Player *p, float mouseSens);
void    Player_ApplyLocalMove(Player *p, float dt);

#endif
