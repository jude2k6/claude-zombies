#include "world.h"

// The single live game world. Zero-initialised: that matches the previous
// globals' defaults (empty entity arrays, localPlayerIdx 0, gamePhase
// GS_PRE_GAME which is enum value 0).
World g_world;
