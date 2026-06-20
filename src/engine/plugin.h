#ifndef SHOOTER_PLUGIN_H
#define SHOOTER_PLUGIN_H

// ============================================================================
//  plugin.h — the engine PLUGIN host (extend the engine without touching it).
//
//  A plugin is "a second GameModule": the engine runs it ALONGSIDE the game,
//  calling its init/frame/fixed/draw/shutdown at the matching points in the
//  Eng_Run loop. A plugin uses ordinary engine services (Net_*, Audio_*,
//  Eng_Ui*, Eng_Input*, ...) to do its work and draws its own overlay on top of
//  the game. This is the seam for optional, self-contained features that any
//  game on the engine might want — voice chat, a friends/presence system, a
//  debug console, a recorder — added without modifying the engine or the game.
//
//  Two ways a plugin reaches the engine, converging on ONE descriptor:
//    • compiled-in  — linked into the host binary, registered directly with
//                     Eng_PluginRegister(&desc) before Eng_Run.
//    • dynamic (.so)— dropped in a folder, dlopen'd by Eng_PluginLoadDir().
//                     Each .so exports `const EngPluginDesc *eng_plugin_main(void)`.
//                     The host binary must be linked -rdynamic (ENABLE_EXPORTS)
//                     so the plugin's Eng_* calls resolve against the host's
//                     symbol table — a dynamic plugin links nothing.
//
//  This is the engine-level analogue of the editor plugin host (edhost.h) and
//  the game behaviour registry (mob_ai.h); it stays game-clean (the §2 cardinal
//  rule) — the descriptor is plain function pointers, so plugin.c needs no game
//  type and a plugin needs no game header to hook the loop.
// ============================================================================

#include <stdbool.h>

// ---- plugin ABI ------------------------------------------------------------
// Bump on any breaking change to EngPluginDesc / the host surface; dynamic
// plugins whose abiVersion doesn't match are refused at load.
//   1 — initial surface (init/frame/fixed/draw/shutdown lifecycle hooks)
#define ENG_PLUGIN_ABI   1
#define ENG_PLUGIN_ENTRY "eng_plugin_main"   // symbol a .so must export

// The plugin's callback vtable — the same lifecycle the engine drives for the
// game's GameModule (app.h), so a plugin hooks the loop identically. Any
// callback may be NULL. See Eng_Run for the exact call order; in short:
//   init     once, after the game's init() (engine + game state are up).
//   frame    each rendered frame, after the game's frame() — per-frame work
//            (poll a service, sample input) and visual state. No GL begin/end.
//   fixed    zero or more times per frame at the constant ENG_FIXED_DT, after
//            the game's fixed() — authoritative/deterministic work.
//   draw     each frame, after the game's draw() — overlays land ON TOP of the
//            game's HUD. Engine owns Begin/EndDrawing.
//   shutdown once, before the game's shutdown(), in reverse registration order.
typedef struct {
    const char *name;        // human-readable plugin name (for logs)
    int         abiVersion;  // must equal ENG_PLUGIN_ABI
    void (*init)    (void);
    void (*frame)   (float dt, int w, int h);
    void (*fixed)   (float dt);
    void (*draw)    (int w, int h);
    void (*shutdown)(void);
} EngPluginDesc;

// A dynamic plugin (.so) exports:  const EngPluginDesc *eng_plugin_main(void);
typedef const EngPluginDesc *(*EngPluginMainFn)(void);

// ---- registration ----------------------------------------------------------
// Register a compiled-in plugin. Call before Eng_Run (registrations after init
// has run are still accepted, but miss the init dispatch). The descriptor is
// borrowed, not copied — it must outlive the run (a static literal is the norm).
// Silently ignores a NULL/incompatible descriptor or an over-full registry.
void Eng_PluginRegister(const EngPluginDesc *desc);

// Scan `dir` for *.so, dlopen each, version-check, and register. Returns the
// number registered. A missing folder is fine (returns 0). A .so that doesn't
// export ENG_PLUGIN_ENTRY is skipped silently, so this folder may be shared
// with other plugin kinds (e.g. editor plugins). POSIX-only: a no-op on Windows
// (compiled-in plugins still work everywhere).
int  Eng_PluginLoadDir(const char *dir);

// ---- introspection ---------------------------------------------------------
int         Eng_PluginCount(void);     // number of registered plugins
const char *Eng_PluginName(int i);     // name of plugin i, or NULL out of range

// ============================================================================
//  Host dispatch — called by app.c (Eng_Run). NOT for plugins or game code.
// ============================================================================
void Eng_PluginInitAll(void);
void Eng_PluginFrameAll(float dt, int w, int h);
void Eng_PluginFixedAll(float dt);
void Eng_PluginDrawAll(int w, int h);
void Eng_PluginShutdownAll(void);      // shutdown (reverse order) + dlclose
bool Eng_PluginAnyFixed(void);         // true if any plugin has a fixed() hook

#endif // SHOOTER_PLUGIN_H
