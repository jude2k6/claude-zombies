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
typedef struct {
    void (*init)    (void);                  // load content, build the world
    void (*frame)   (float dt, int w, int h);// input + simulation + camera (no GL)
    void (*draw)    (int w, int h);          // issue draws (engine owns Begin/EndDrawing)
    void (*shutdown)(void);                  // free content
} GameModule;

// Owns window + GL + raygui + audio init, the frame loop, and teardown.
// Returns when the window is closed.
void Eng_Run(const EngConfig *cfg, GameModule game);

#endif // SHOOTER_APP_H
