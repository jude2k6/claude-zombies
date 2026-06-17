// ============================================================================
//  mob_ai.c — the mob behaviour registry (see mob_ai.h).
//
//  A small name->fn table plus a dlopen scan that mirrors the editor's plugin
//  loader. Game-side only; the engine never sees behaviours.
// ============================================================================

#include "mob_ai.h"

#include "raylib.h"    // Directory* helpers + TraceLog
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
  // Dynamic behaviours are a POSIX-dlopen feature for now; on Windows the
  // registry still works for compiled-in (Tier 1) behaviours.
#else
  #include <dlfcn.h>
#endif

#define MAX_BEHAVIOURS  32
#define BEHAVIOUR_NAME_LEN 32

typedef struct {
    char           name[BEHAVIOUR_NAME_LEN];
    MobBehaviourFn fn;
} BehaviourEntry;

static BehaviourEntry g_beh[MAX_BEHAVIOURS];
static int            g_behCount = 0;

void Game_RegisterBehaviour(const char *name, MobBehaviourFn fn) {
    if (!name || !name[0] || !fn) return;
    for (int i = 0; i < g_behCount; i++) {           // overwrite a duplicate
        if (strcmp(g_beh[i].name, name) == 0) { g_beh[i].fn = fn; return; }
    }
    if (g_behCount >= MAX_BEHAVIOURS) {
        fprintf(stderr, "behaviour: registry full, dropping '%s'\n", name);
        return;
    }
    snprintf(g_beh[g_behCount].name, sizeof g_beh[g_behCount].name, "%s", name);
    g_beh[g_behCount].fn = fn;
    g_behCount++;
}

MobBehaviourFn Game_FindBehaviour(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_behCount; i++)
        if (strcmp(g_beh[i].name, name) == 0) return g_beh[i].fn;
    return NULL;
}

bool Game_BehaviourRegistered(const char *name) { return Game_FindBehaviour(name) != NULL; }

void Mobs_RunBehaviours(float dt) {
    for (int i = 0; i < g_behCount; i++) g_beh[i].fn(dt);
}

// ---- Tier-2: dynamic .so loader -------------------------------------------

#if !defined(_WIN32)
static int LoadPluginsFrom(const char *dir) {
    if (!DirectoryExists(dir)) return 0;
    FilePathList files = LoadDirectoryFilesEx(dir, ".so", true);
    int n = 0;
    for (unsigned i = 0; i < files.count; i++) {
        void *h = dlopen(files.paths[i], RTLD_NOW | RTLD_LOCAL);
        if (!h) { fprintf(stderr, "behaviour: dlopen failed: %s\n", dlerror()); continue; }
        GameBehaviourMainFn entry = (GameBehaviourMainFn)dlsym(h, GAME_BEHAVIOUR_ENTRY);
        if (!entry) { dlclose(h); continue; }
        const GameBehaviourDesc *d = entry();
        if (!d || d->abiVersion != GAME_BEHAVIOUR_ABI || !d->name || !d->fn) {
            fprintf(stderr, "behaviour: %s: bad/incompatible descriptor\n", files.paths[i]);
            dlclose(h);   // never unloaded again — fn would dangle if we did
            continue;
        }
        Game_RegisterBehaviour(d->name, d->fn);
        fprintf(stderr, "behaviour: loaded '%s' from %s\n", d->name, files.paths[i]);
        n++;
    }
    UnloadDirectoryFiles(files);
    return n;
}
#endif

int Game_LoadBehaviourPlugins(void) {
#if defined(_WIN32)
    return 0;
#else
    int n = 0;
    n += LoadPluginsFrom("behaviours");
    n += LoadPluginsFrom("../behaviours");
    // Per-mob-folder behaviours: a .so living beside its .mob (locality follows
    // ownership — see editor-content-extensibility.md §4).
    n += LoadPluginsFrom("data/mobs");
    n += LoadPluginsFrom("../data/mobs");
    return n;
#endif
}
