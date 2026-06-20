#ifndef SHOOTER_MOBS_H
#define SHOOTER_MOBS_H

// ============================================================================
//  mobs.h — the data-driven mob catalog (data/mobs/<id>/<id>.mob).
//
//  A mob is content, authored exactly like a weapon: a key=value .mob file
//  (parsed through the engine's shared deffile reader) describing the mob's
//  identity, render model/animations, AI behaviour archetype, and stats. The
//  game loads the catalog at startup; a SPAWN's `mob` tag selects which MobDef
//  to instantiate. See docs/editor-content-extensibility.md §3.
//
//  Split along the engine/game seam: everything in a .mob is DATA the editor
//  can read too — except `behaviour`, which NAMES game-side AI code resolved
//  through the behaviour registry (see ai/behaviour.h).
// ============================================================================

#include "types.h"     // MOB_TAG_LEN
#include "raylib.h"    // Color
#include <stdbool.h>

#define MOB_NAME_LEN       48
#define MOB_MODEL_LEN      64
#define MOB_ANIM_LEN       32
#define MOB_BEHAVIOUR_LEN  32
#define MAX_MOB_DEFS       16

typedef struct {
    char  id[MOB_TAG_LEN];          // "ZOMBIE" — what a SPAWN MOB tag stores
    char  name[MOB_NAME_LEN];       // "Shambler" — display / editor label
    char  category[MOB_NAME_LEN];   // "mob" vs "player"; groups in the palette

    // ---- render ----
    char  model[MOB_MODEL_LEN];     // "zombie.glb" (resolved by the engine)
    float modelScale;
    float modelYaw;
    Color tint;                     // editor marker / proxy colour
    char  animWalk[MOB_ANIM_LEN];   // logical state -> clip name inside the .glb
    char  animAttack[MOB_ANIM_LEN];
    char  animDeath[MOB_ANIM_LEN];
    char  animIdle[MOB_ANIM_LEN];

    // ---- behaviour (the one word that names code, not data) ----
    char  behaviour[MOB_BEHAVIOUR_LEN];  // "chaser" — resolved via the registry
    float moveSpeed;                // round-1 base speed (game scales by round)
    float pathRepath;               // seconds between target re-picks (hint)

    // ---- stats (data the game reads; the engine/editor never does) ----
    int   healthBase;               // round-1 HP baseline; the round curve scales it
    int   damage;                   // contact damage
    float attackWindup;
    float attackRange;

    bool  valid;                    // a usable def was parsed
} MobDef;

// ---- catalog --------------------------------------------------------------

// Scan data/mobs/ (and ../ / ./ prefixes) for *.mob files and populate the
// catalog. Idempotent: safe to call from any entry point before it needs mobs.
// Prints a one-line summary and any validation issues to stderr.
void          Mobs_Load(void);

int           Mob_Count(void);
const MobDef *Mob_Get(int index);            // NULL if out of range
const MobDef *Mob_Find(const char *id);      // by id tag, NULL if absent
int           Mob_FindIndex(const char *id); // -1 if absent

// ---- validation (structural; mirrors MapDoc_Validate) ---------------------
// Behaviour-registration checks are layered on by the AI registry; this covers
// the data/file half: required fields, numeric sanity, unique ids, model file
// resolves on disk.
typedef enum { MOB_OK = 0, MOB_WARN = 1, MOB_ERROR = 2 } MobSeverity;
typedef struct {
    int  severity;                  // MobSeverity
    char mobId[MOB_TAG_LEN];        // offending mob, or "" for catalog-level
    char msg[160];
} MobIssue;

// Validate the loaded catalog. Writes up to `max` issues, returns the total
// found (may exceed max). 0 = clean.
int Mob_Validate(MobIssue *out, int max);

#endif // SHOOTER_MOBS_H
