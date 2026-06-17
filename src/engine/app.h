#ifndef SHOOTER_APP_H
#define SHOOTER_APP_H

#include <stdbool.h>

// ============================================================================
//  Engine application host (Phase 6 of the engine/game split).
//
//  The engine owns main()'s body: the window, the GL/UI/audio substrate, the
//  frame loop, and frame timing. The game is a *module* it hosts — it hands the
//  engine a GameModule vtable and the engine calls *down* into it each frame.
//  The engine never includes a game header (§2 cardinal rule); the vtable is
//  plain function pointers, so app.c needs no game type.
// ============================================================================

// Window / presentation config. The bool flags and fpsCap map to raylib's
// SetConfigFlags / SetTargetFPS applied before window creation, so the host
// (not the engine) decides them — a competitive client wants uncapped/high
// refresh, the editor wants a plain resizable tool window. A zero-initialised
// config is windowed, no-vsync, no-MSAA, fixed-size, uncapped; set the fields
// you want explicitly.
typedef struct {
    int         w, h;
    const char *title;
    bool        vsync;       // cap present rate to the display refresh
    bool        msaa4x;      // request 4x MSAA
    bool        resizable;   // resizable window
    bool        fullscreen;  // start in fullscreen mode
    int         fpsCap;      // SetTargetFPS value; 0 = uncapped
} EngConfig;

// The game's callback vtable. The engine wraps each frame's draw step in
// BeginDrawing()/EndDrawing(), so `draw` issues draws but never opens/closes
// the frame itself.
//
// Timestep model (see Eng_Run):
//   - `frame` runs once per rendered frame with the real elapsed `dt`. Put
//     per-frame work here: input sampling, local-player prediction, camera,
//     and purely visual updates (bob, camera shake, post-FX ramps).
//   - `fixed` runs zero or more times per rendered frame, each with a CONSTANT
//     dt (ENG_FIXED_DT), draining a time accumulator. Put the authoritative
//     simulation here so its outcome is independent of render frame rate. The
//     engine clamps the accumulator so a long stall can't spiral into an
//     unbounded catch-up. `frame` is called before the fixed drain, so input
//     sampled this frame is consumed by this frame's fixed steps.
//   Either callback may be NULL.
typedef struct {
    void (*init)    (void);                  // load content, build the world
    void (*frame)   (float dt, int w, int h);// per-frame: input, prediction, camera, visuals (no GL)
    void (*fixed)   (float dt);              // fixed-step authoritative simulation (dt == ENG_FIXED_DT)
    void (*draw)    (int w, int h);          // issue draws (engine owns Begin/EndDrawing)
    void (*shutdown)(void);                  // free content
} GameModule;

// Fixed simulation step (seconds). 60 Hz — matches the headless --sim-tick rate
// and the prior frame-locked behaviour at 60 fps, so gameplay tuning is unchanged.
#define ENG_FIXED_DT (1.0f / 60.0f)

// Owns window + GL + raygui + audio init, the frame loop, and teardown.
// Returns when the window is closed.
void Eng_Run(const EngConfig *cfg, GameModule game);

// Ask the frame loop to exit after the current frame (the programmatic
// equivalent of clicking the window's close box). Use this for an in-app
// "Quit" / "Exit" command — the engine disables the ESC-to-quit key, so this
// is the only way for a module to end Eng_Run from inside a callback.
void Eng_RequestClose(void);

#endif // SHOOTER_APP_H
