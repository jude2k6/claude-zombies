#ifndef SHOOTER_APP_H
#define SHOOTER_APP_H

// ============================================================================
//  Engine application host (Phase 6 of the engine/game split).
//
//  The engine owns main()'s body: the window, the GL/UI/audio substrate, the
//  frame loop, and frame timing. The game is a *module* it hosts — it hands the
//  engine a GameModule vtable and the engine calls *down* into it each frame.
//  The engine never includes a game header (§2 cardinal rule); the vtable is
//  plain function pointers, so app.c needs no game type.
// ============================================================================

typedef struct {
    int         w, h;
    const char *title;
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

#endif // SHOOTER_APP_H
