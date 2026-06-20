// ============================================================================
//  plugin.c — the engine plugin host (see plugin.h).
//
//  A fixed registry of EngPluginDesc + a dlopen scan that mirrors the editor's
//  plugin loader (edhost.c) and the game behaviour loader (mob_ai.c). Engine-
//  clean: it knows lifecycle function pointers, never a game type.
// ============================================================================

#include "plugin.h"

#include "raylib.h"    // Directory* helpers (dynamic scan)
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
  // Dynamic plugins are a POSIX-dlopen feature for now; on Windows the host
  // still runs compiled-in plugins registered via Eng_PluginRegister.
#else
  #include <dlfcn.h>
#endif

#define ENG_MAX_PLUGINS 32

typedef struct {
    EngPluginDesc desc;   // copied so a dynamic descriptor outlives entry()'s frame
    void         *lib;    // dlopen handle (NULL for compiled-in); dlclose'd at shutdown
} PluginSlot;

static PluginSlot g_plugins[ENG_MAX_PLUGINS];
static int        g_pluginCount = 0;

// ---- registration ----------------------------------------------------------

// Shared register path for both compiled-in and dynamic plugins. `lib` is the
// dlopen handle to release at shutdown (NULL for compiled-in).
static bool RegisterSlot(const EngPluginDesc *desc, void *lib) {
    if (!desc || !desc->name || !desc->name[0]) return false;
    if (desc->abiVersion != ENG_PLUGIN_ABI) {
        fprintf(stderr, "plugin: '%s' ABI %d != host %d — refused\n",
                desc->name, desc->abiVersion, ENG_PLUGIN_ABI);
        return false;
    }
    if (g_pluginCount >= ENG_MAX_PLUGINS) {
        fprintf(stderr, "plugin: registry full, dropping '%s'\n", desc->name);
        return false;
    }
    g_plugins[g_pluginCount].desc = *desc;
    g_plugins[g_pluginCount].lib  = lib;
    g_pluginCount++;
    return true;
}

void Eng_PluginRegister(const EngPluginDesc *desc) { RegisterSlot(desc, NULL); }

// ---- dynamic .so loader ----------------------------------------------------

int Eng_PluginLoadDir(const char *dir) {
#if defined(_WIN32)
    (void)dir;
    return 0;
#else
    if (!dir || !DirectoryExists(dir)) return 0;
    FilePathList files = LoadDirectoryFilesEx(dir, ".so", true);
    int n = 0;
    for (unsigned i = 0; i < files.count; i++) {
        void *h = dlopen(files.paths[i], RTLD_NOW | RTLD_LOCAL);
        if (!h) { fprintf(stderr, "plugin: dlopen failed: %s\n", dlerror()); continue; }
        EngPluginMainFn entry = (EngPluginMainFn)(void (*)(void))dlsym(h, ENG_PLUGIN_ENTRY);
        if (!entry) { dlclose(h); continue; }   // not an engine plugin — skip quietly
        const EngPluginDesc *d = entry();
        if (!RegisterSlot(d, h)) {
            fprintf(stderr, "plugin: %s: bad/incompatible descriptor\n", files.paths[i]);
            dlclose(h);
            continue;
        }
        fprintf(stderr, "plugin: loaded '%s' from %s\n", d->name, files.paths[i]);
        n++;
    }
    UnloadDirectoryFiles(files);
    return n;
#endif
}

// ---- introspection ---------------------------------------------------------

int Eng_PluginCount(void) { return g_pluginCount; }

const char *Eng_PluginName(int i) {
    if (i < 0 || i >= g_pluginCount) return NULL;
    return g_plugins[i].desc.name;
}

// ---- host dispatch (app.c) -------------------------------------------------

void Eng_PluginInitAll(void) {
    for (int i = 0; i < g_pluginCount; i++)
        if (g_plugins[i].desc.init) g_plugins[i].desc.init();
}

void Eng_PluginFrameAll(float dt, int w, int h) {
    for (int i = 0; i < g_pluginCount; i++)
        if (g_plugins[i].desc.frame) g_plugins[i].desc.frame(dt, w, h);
}

void Eng_PluginFixedAll(float dt) {
    for (int i = 0; i < g_pluginCount; i++)
        if (g_plugins[i].desc.fixed) g_plugins[i].desc.fixed(dt);
}

void Eng_PluginDrawAll(int w, int h) {
    for (int i = 0; i < g_pluginCount; i++)
        if (g_plugins[i].desc.draw) g_plugins[i].desc.draw(w, h);
}

bool Eng_PluginAnyFixed(void) {
    for (int i = 0; i < g_pluginCount; i++)
        if (g_plugins[i].desc.fixed) return true;
    return false;
}

void Eng_PluginShutdownAll(void) {
    // Reverse registration order (LIFO): a later plugin may depend on an earlier.
    for (int i = g_pluginCount - 1; i >= 0; i--)
        if (g_plugins[i].desc.shutdown) g_plugins[i].desc.shutdown();
#if !defined(_WIN32)
    for (int i = g_pluginCount - 1; i >= 0; i--)
        if (g_plugins[i].lib) dlclose(g_plugins[i].lib);
#endif
    g_pluginCount = 0;
}
