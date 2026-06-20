#include "app.h"
#include "raylib.h"

// raygui's implementation lives here — the engine owns the UI substrate. Game
// UI code (menu.c, hud.c) includes raygui.h without the implementation define.
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "audio.h"   // engine audio mixer
#include "stats.h"   // per-frame instrumentation
#include "plugin.h"  // engine plugin host (runs alongside the game module)

// Set by Eng_RequestClose() to break the frame loop from inside a callback.
static bool g_engCloseRequested = false;
void Eng_RequestClose(void) { g_engCloseRequested = true; }

void Eng_Run(const EngConfig *cfg, GameModule game) {
    unsigned int flags = 0;
    if (cfg->msaa4x)     flags |= FLAG_MSAA_4X_HINT;
    if (cfg->vsync)      flags |= FLAG_VSYNC_HINT;
    if (cfg->resizable)  flags |= FLAG_WINDOW_RESIZABLE;
    if (cfg->fullscreen) flags |= FLAG_FULLSCREEN_MODE;
    SetConfigFlags(flags);
    InitWindow(cfg->w, cfg->h, cfg->title);
    if (cfg->fpsCap > 0) SetTargetFPS(cfg->fpsCap);
    SetExitKey(0);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
    Audio_Init();

    if (game.init) game.init();
    Eng_PluginInitAll();   // plugins init after the game (engine + game state are up)

    // Fixed-timestep accumulator. Real elapsed time is banked here and drained
    // in constant ENG_FIXED_DT steps so the authoritative sim is frame-rate
    // independent. Clamped so a hitch (alt-tab, breakpoint) can't queue a huge
    // catch-up burst ("spiral of death").
    float simAccum = 0.0f;
    const float MAX_ACCUM = 0.25f;   // at most ~15 fixed steps after one stall

    while (!WindowShouldClose() && !g_engCloseRequested) {
        float dt = GetFrameTime();
        int   sw = GetScreenWidth();
        int   sh = GetScreenHeight();

        Eng_StatsBeginFrame(dt);

        // Per-frame work (input, prediction, camera, visuals) first, so input
        // sampled this frame is consumed by this frame's fixed steps below.
        if (game.frame) game.frame(dt, sw, sh);
        Eng_PluginFrameAll(dt, sw, sh);   // plugins after the game's frame

        // Drain the accumulator in fixed steps. Run if EITHER the game or any
        // plugin has a fixed() hook, so a plugin's sim still ticks when the game
        // has none (and vice-versa).
        int fixedSteps = 0;
        if (game.fixed || Eng_PluginAnyFixed()) {
            simAccum += dt;
            if (simAccum > MAX_ACCUM) simAccum = MAX_ACCUM;
            while (simAccum >= ENG_FIXED_DT) {
                if (game.fixed) game.fixed(ENG_FIXED_DT);
                Eng_PluginFixedAll(ENG_FIXED_DT);
                simAccum -= ENG_FIXED_DT;
                fixedSteps++;
            }
        }

        BeginDrawing();
        if (game.draw) game.draw(sw, sh);
        Eng_PluginDrawAll(sw, sh);        // overlays land on top of the game's draw
        EndDrawing();

        Eng_StatsEndFrame(fixedSteps);
    }

    Eng_PluginShutdownAll();   // plugins down before the game (reverse of init)
    if (game.shutdown) game.shutdown();
    Audio_Shutdown();
    CloseWindow();
}
