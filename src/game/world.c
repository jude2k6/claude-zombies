#include "world.h"

// The single live game world. Zero-initialised except for the few level fields
// whose previous globals had non-zero defaults: mapName defaulted to "Default"
// and the arena half-extents to ARENA_HALF_DEFAULT (set per-map at load, but
// kept here so a world that never loads a map still has sane bounds). All other
// state (empty entity arrays, localPlayerIdx 0, gamePhase GS_PRE_GAME == 0,
// empty level geometry) matches a zeroed struct.
World g_world = {
    .mapName    = "Default",
    .arenaHalfX = ARENA_HALF_DEFAULT,
    .arenaHalfZ = ARENA_HALF_DEFAULT,
};
