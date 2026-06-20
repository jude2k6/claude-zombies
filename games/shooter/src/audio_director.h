#ifndef SHOOTER_AUDIO_DIRECTOR_H
#define SHOOTER_AUDIO_DIRECTOR_H

#include "types.h"   // Player

// Game-side audio "director": the *when*. It reads game state (the local
// player, the world, weapons, rounds, the map) and fires engine mixer events
// through audio.h's Audio_Play* API. The mixer (audio.c) is the *how* and
// knows nothing about the game. This replaces the old Audio_Tick.
//
// Call once per frame with the local player while in active play.
void AudioDirector_Tick(Player *me);

#endif
