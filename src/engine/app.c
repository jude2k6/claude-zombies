#include "app.h"
#include "raylib.h"

// raygui's implementation lives here — the engine owns the UI substrate. Game
// UI code (menu.c, hud.c) includes raygui.h without the implementation define.
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "audio.h"   // engine audio mixer

void Eng_Run(const EngConfig *cfg, GameModule game) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(cfg->w, cfg->h, cfg->title);
    SetTargetFPS(60);
    SetExitKey(0);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
    Audio_Init();

    if (game.init) game.init();

    // Fixed-timestep accumulator. Real elapsed time is banked here and drained
    // in constant ENG_FIXED_DT steps so the authoritative sim is frame-rate
    // independent. Clamped so a hitch (alt-tab, breakpoint) can't queue a huge
    // catch-up burst ("spiral of death").
    float simAccum = 0.0f;
    const float MAX_ACCUM = 0.25f;   // at most ~15 fixed steps after one stall

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int   sw = GetScreenWidth();
        int   sh = GetScreenHeight();

        // Per-frame work (input, prediction, camera, visuals) first, so input
        // sampled this frame is consumed by this frame's fixed steps below.
        if (game.frame) game.frame(dt, sw, sh);

        // Drain the accumulator in fixed steps.
        if (game.fixed) {
            simAccum += dt;
            if (simAccum > MAX_ACCUM) simAccum = MAX_ACCUM;
            while (simAccum >= ENG_FIXED_DT) {
                game.fixed(ENG_FIXED_DT);
                simAccum -= ENG_FIXED_DT;
            }
        }

        BeginDrawing();
        if (game.draw) game.draw(sw, sh);
        EndDrawing();
    }

    if (game.shutdown) game.shutdown();
    Audio_Shutdown();
    CloseWindow();
}
