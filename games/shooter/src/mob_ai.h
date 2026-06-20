#ifndef SHOOTER_MOB_AI_H
#define SHOOTER_MOB_AI_H

// ============================================================================
//  mob_ai.h — the mob behaviour registry (name -> AI archetype).
//
//  A .mob's `behaviour` field is NOT a hardcoded call — it is a name resolved
//  through this registry, populated at startup from two provider tiers:
//    Tier 1 — compiled-in C: Game_RegisterBehaviour("chaser", fn) from the
//             game (entities.c registers the zombie chaser).
//    Tier 2 — dynamic .so: drop a plugin in ./behaviours/ (or a mob folder),
//             dlopen'd at load — the same pattern as the editor plugin loader,
//             no game rebuild. (Tier 3 scripts are deferred.)
//
//  This lives game-side, never in the engine: the engine is behaviour-free.
//  See docs/editor-content-extensibility.md §4.
//
//  An archetype is an "update all my enemies" pass for one frame: it iterates
//  g_world.enemies itself and processes the ones whose mob names it. The
//  registry-driven dispatch (Mobs_RunBehaviours) calls each registered pass.
// ============================================================================

#include <stdbool.h>

typedef void (*MobBehaviourFn)(float dt);

// Register a compiled-in behaviour. A duplicate name overwrites (last wins).
void           Game_RegisterBehaviour(const char *name, MobBehaviourFn fn);
MobBehaviourFn Game_FindBehaviour(const char *name);
bool           Game_BehaviourRegistered(const char *name);

// Scan ./behaviours/ (and ./data/mobs/*/) for *.so plugins and dlopen each.
// Each exports `const GameBehaviourDesc *game_behaviour_main(void)`. Missing
// folder = no plugins, no error. Returns the number registered.
int            Game_LoadBehaviourPlugins(void);

// Run one frame: dispatch every registered behaviour pass in registration
// order. (Each pass filters to the enemies whose mob uses it.)
void           Mobs_RunBehaviours(float dt);

// ---- dynamic plugin ABI ----------------------------------------------------
#define GAME_BEHAVIOUR_ABI   1
#define GAME_BEHAVIOUR_ENTRY "game_behaviour_main"

typedef struct {
    const char     *name;        // behaviour name a .mob references
    int             abiVersion;  // must equal GAME_BEHAVIOUR_ABI
    MobBehaviourFn  fn;          // the per-frame update-all pass
} GameBehaviourDesc;

typedef const GameBehaviourDesc *(*GameBehaviourMainFn)(void);

#endif // SHOOTER_MOB_AI_H
