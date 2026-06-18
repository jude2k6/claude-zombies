// ============================================================================
//  main.c — minimal GameModule skeleton for a new engine-based game.
//
//  Wires a GameModule vtable with stubs for init / frame / fixed / draw /
//  shutdown and hands it to Eng_Run. Replace the stub bodies with real game
//  logic as the project grows.
//
//  Build: see src/CMakeLists.txt — finds the engine (libengine.a) and compiles
//  this file into an executable that links it.
// ============================================================================

#include "app.h"       // GameModule, EngConfig, Eng_Run, Eng_RequestClose
#include "content.h"   // Eng_LocateRoot, Eng_SetLibraryRoot, Eng_SetGameRoot
#include "raylib.h"    // drawing helpers

#include <string.h>

// ---- module callbacks -------------------------------------------------------

static void GameInit(void) {
    // Load content, set up world state.
}

static void GameFrame(float dt, int w, int h) {
    (void)dt; (void)w; (void)h;
    // Per-frame: input sampling, camera, visual updates (no GL calls here).
    if (IsKeyPressed(KEY_ESCAPE)) Eng_RequestClose();
}

static void GameFixed(float dt) {
    (void)dt;
    // Fixed-rate authoritative simulation (ENG_FIXED_DT seconds per step).
}

static void GameDraw(int w, int h) {
    (void)w; (void)h;
    ClearBackground((Color){ 20, 20, 30, 255 });
    DrawText("New Game — replace me with real draw code!", 40, 40, 20, RAYWHITE);
}

static void GameShutdown(void) {
    // Unload content.
}

// ---- entry point ------------------------------------------------------------

int main(void) {
    // Wire the two content roots so asset lookups resolve game-first,
    // then fall back to the read-only engine library.
    char rootBuf[512];
    if (Eng_LocateRoot("library",    rootBuf, sizeof rootBuf)) Eng_SetLibraryRoot(rootBuf);
    if (Eng_LocateRoot(".",          rootBuf, sizeof rootBuf)) Eng_SetGameRoot(rootBuf);

    EngConfig cfg = {
        .w         = 1280,
        .h         = 720,
        .title     = "My Game",
        .vsync     = true,
        .resizable = true,
    };

    GameModule mod = {
        .init     = GameInit,
        .frame    = GameFrame,
        .fixed    = GameFixed,
        .draw     = GameDraw,
        .shutdown = GameShutdown,
    };

    Eng_Run(&cfg, mod);
    return 0;
}
