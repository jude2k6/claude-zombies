#ifndef SHOOTER_GAME_H
#define SHOOTER_GAME_H

#include "types.h"
#include "world.h"   // gamePhase now lives in g_world

extern int        roundNum;
extern float      roundBreakTimer;

void Game_StartRound(int r);
void Game_Tick(float dt);       // host/solo simulation

#endif
