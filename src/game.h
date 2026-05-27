#ifndef SHOOTER_GAME_H
#define SHOOTER_GAME_H

#include "types.h"

extern int        roundNum;
extern GamePhase  gamePhase;
extern float      roundBreakTimer;

void Game_StartRound(int r);
void Game_Tick(float dt);       // host/solo simulation

#endif
