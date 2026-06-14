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

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int   sw = GetScreenWidth();
        int   sh = GetScreenHeight();

        if (game.frame) game.frame(dt, sw, sh);

        BeginDrawing();
        if (game.draw) game.draw(sw, sh);
        EndDrawing();
    }

    if (game.shutdown) game.shutdown();
    Audio_Shutdown();
    CloseWindow();
}
