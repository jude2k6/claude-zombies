#include "devtools.h"
#include "raylib.h"
#include "raymath.h"
#include "types.h"
#include "level.h"
#include "player.h"
#include "weapons.h"
#include "entities.h"
#include "interact.h"
#include "assets.h"
#include "anim.h"
#include "render.h"
#include "viewmodel.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// ---- --validate ------------------------------------------------------------

static int Dev_Validate(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: --validate <map.map>\n"); return 1; }
    SetTraceLogLevel(LOG_ERROR);
    int errs = Level_Validate(argv[2]);
    return errs > 0 ? 1 : 0;
}

// ---- --screenshot-viewmodels -----------------------------------------------

static int Dev_ScreenshotViewmodels(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Viewmodel screenshot");
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Viewmodel_LoadArms();     // shared arms + bolted gun for the other 4

    // Need a minimal level so Render_World3D doesn't barf on the
    // walls/floor draw paths. Level_Build initializes the hardcoded
    // fallback which is sufficient as a backdrop.
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    players[0].active = true;
    players[0].alive = true;
    players[0].pos = (Vector3){ 0, PLAYER_EYE, 0 };
    players[0].yaw = 0.0f;
    players[0].pitch = 0.0f;
    players[0].currentSlot = 0;

    Camera cam = {
        .position   = players[0].pos,
        .target     = Vector3Add(players[0].pos, Player_LookDir(0, 0)),
        .up         = (Vector3){ 0, 1, 0 },
        .fovy       = 75.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    for (int wi = 0; wi < W_COUNT; wi++) {
        if (!weaponModelLoaded[wi]) {
            fprintf(stderr, "skip %s — model not loaded\n",
                    WEAPONS[wi].idName ? WEAPONS[wi].idName : "?");
            continue;
        }
        players[0].inventory[0].weaponIdx = wi;
        players[0].inventory[0].owned = true;
        players[0].inventory[0].ammo = WEAPONS[wi].magSize;
        players[0].inventory[0].reserve = WEAPONS[wi].reserveMax;

        // Render_World3D activates the world shader and draws the
        // viewmodel inside it — so we get lit materials instead of
        // a black silhouette. The empty level just draws a sky + floor
        // backdrop which makes scale easier to gauge anyway.
        BeginDrawing();
        ClearBackground((Color){ 60, 65, 75, 255 });
        Render_World3D(cam);
        DrawText(WEAPONS[wi].idName ? WEAPONS[wi].idName : "?",
                 20, 20, 36, (Color){240,240,240,255});
        DrawText(WEAPONS[wi].name ? WEAPONS[wi].name : "?",
                 20, 64, 24, (Color){200,200,200,255});
        char dims[64];
        snprintf(dims, sizeof dims, "model_scale=%.1f  yaw=%.0f deg",
                 weaponTune[wi].scale, weaponTune[wi].yawDeg);
        DrawText(dims, 20, 96, 18, (Color){180,180,200,255});
        EndDrawing();

        char fname[128];
        snprintf(fname, sizeof fname, "viewmodel_%s.png",
                 WEAPONS[wi].idName ? WEAPONS[wi].idName : "x");
        TakeScreenshot(fname);
        fprintf(stderr, "wrote %s\n", fname);
    }
    Assets_Unload();
    Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --screenshot-coop -----------------------------------------------------

static int Dev_ScreenshotCoop(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Coop player screenshot");
    SetTargetFPS(60);   // realistic GetFrameTime() so the speed EMA works
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Render_LoadPlayerAnim();
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    players[0].active = true; players[0].alive = true;
    players[0].pos = (Vector3){ 0, PLAYER_EYE, 9 };  // not drawn (local)

    // Camera looks down -Z at the teammates clustered near the origin.
    Camera cam = {
        .position   = (Vector3){ 0, 1.9f, 6.2f },
        .target     = (Vector3){ 0, 1.0f, 0 },
        .up         = (Vector3){ 0, 1, 0 },
        .fovy       = 55.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    // Each scene: configure slots 1..3, run `frames` frames (advancing the
    // movers), then screenshot. Teammates face +Z (toward camera): yaw=PI.
    const float YAW = PI;
    struct { const char *name; int frames; } scenes[] = {
        { "coop_locomotion", 30 }, { "coop_states", 45 }, { "coop_revive", 45 },
        { "coop_noclip", 30 },
    };
    for (int s = 0; s < 4; s++) {
        // reset all teammate slots inactive each scene
        for (int i = 1; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
        // The noclip scene shows YOUR OWN body left behind: move the local
        // player into view, flip noclipMode on so it's drawn + the
        // first-person viewmodel is suppressed. Restored after the shot.
        noclipMode = (s == 3);
        if (s == 3) {
            players[0].pos = (Vector3){ 0, PLAYER_EYE, 0 };
            players[0].yaw = YAW;
            players[0].inventory[0].owned = true;  // would show a gun if not noclip
        } else {
            players[0].pos = (Vector3){ 0, PLAYER_EYE, 9 };  // behind cam (not drawn)
        }
        for (int f = 0; f < scenes[s].frames; f++) {
            float dt = GetFrameTime(); if (dt <= 0) dt = 1.0f/60.0f;
            if (s == 0) {
                // walker (slot1), runner (slot2), idler (slot3)
                if (f == 0) {
                    players[1].active=players[1].alive=true; players[1].yaw=YAW;
                    players[1].pos=(Vector3){-2.2f,PLAYER_EYE,0};
                    players[2].active=players[2].alive=true; players[2].yaw=YAW;
                    players[2].pos=(Vector3){-3.6f,PLAYER_EYE,1.4f};
                    players[3].active=players[3].alive=true; players[3].yaw=YAW;
                    players[3].pos=(Vector3){2.2f,PLAYER_EYE,0.4f};
                }
                players[1].pos.x += 7.0f * dt;    // walk speed
                players[2].pos.x += 11.0f * dt;   // sprint speed
            } else if (s == 1) {
                // reloading (slot1), downed (slot2), dead (slot3)
                if (f == 0) {
                    players[1].active=players[1].alive=true; players[1].yaw=YAW;
                    players[1].pos=(Vector3){-2.4f,PLAYER_EYE,0};
                    players[1].inventory[0].owned=true;
                    players[2].active=players[2].alive=true; players[2].downed=true;
                    players[2].yaw=YAW; players[2].pos=(Vector3){0,PLAYER_EYE,0.3f};
                    players[3].active=true; players[3].alive=false;
                    players[3].yaw=YAW; players[3].pos=(Vector3){2.4f,PLAYER_EYE,0.3f};
                }
                players[1].inventory[0].reloadTimer = 1.5f;  // hold in reload
            } else if (s == 2) {
                // reviver (slot1) kneeling over a downed teammate (slot2)
                if (f == 0) {
                    players[1].active=players[1].alive=true; players[1].yaw=YAW;
                    players[1].pos=(Vector3){-0.8f,PLAYER_EYE,0};
                    players[2].active=players[2].alive=true; players[2].downed=true;
                    players[2].yaw=YAW+PI*0.5f; players[2].pos=(Vector3){0.7f,PLAYER_EYE,0.2f};
                }
                players[2].reviveAsTarget = 2.0f;  // being revived
            }
            BeginDrawing();
            ClearBackground((Color){ 60, 65, 75, 255 });
            Render_World3D(cam);
            DrawText(scenes[s].name, 20, 20, 30, (Color){240,240,240,255});
            EndDrawing();
        }
        char fn[64]; snprintf(fn, sizeof fn, "%s.png", scenes[s].name);
        TakeScreenshot(fn);
        fprintf(stderr, "wrote %s\n", fn);
    }
    noclipMode = false;
    Render_UnloadPlayerAnim();
    Assets_Unload(); Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --screenshot-pap ------------------------------------------------------

static int Dev_ScreenshotPaP(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "PaP screenshot");
    SetTargetFPS(60);
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Render_LoadPlayerAnim();
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    players[0].active = true; players[0].alive = true;
    players[0].pos = (Vector3){ 0, PLAYER_EYE, 6 };

    pap.pos = (Vector3){ 0, 0, 0 };           // bring it to the origin
    pap.ownerPlayer = 0; pap.slotInProgress = 0; pap.weaponIdx = W_RIFLE;

    Camera cam = {
        .position   = (Vector3){ 1.6f, 1.7f, 3.4f },
        .target     = (Vector3){ 0, 1.7f, 0 },
        .up         = (Vector3){ 0, 1, 0 },
        .fovy       = 55.0f, .projection = CAMERA_PERSPECTIVE,
    };
    struct { const char *name; int phase; float timer; } shots[] = {
        { "pap_0_idle",   PAP_IDLE,   0.0f },
        { "pap_1_insert", PAP_INSERT, PAP_INSERT_TIME * 0.45f },
        { "pap_2_work",   PAP_WORK,   PAP_WORK_TIME * 0.5f },
        { "pap_3_ready",  PAP_READY,  0.0f },
    };
    for (int s = 0; s < 4; s++) {
        pap.phase = shots[s].phase; pap.timer = shots[s].timer;
        pap.bob = 1.2f;
        BeginDrawing();
        ClearBackground((Color){ 50, 52, 60, 255 });
        Render_World3D(cam);
        DrawText(shots[s].name, 20, 20, 28, (Color){240,240,240,255});
        EndDrawing();
        char fn[64]; snprintf(fn, sizeof fn, "%s.png", shots[s].name);
        TakeScreenshot(fn);
        fprintf(stderr, "wrote %s\n", fn);
    }
    Render_UnloadPlayerAnim();
    Assets_Unload(); Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --anim-test -----------------------------------------------------------

static int Dev_AnimTest(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: --anim-test <file.glb> [clip] [--live]\n"); return 1; }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Anim test");
    Assets_Load();   // loads world + skinned shaders

    AnimModel am;
    if (!Anim_Load(&am, argv[2])) { CloseWindow(); return 1; }
    if (worldSkinnedShaderLoaded) Anim_ApplyShader(&am, worldSkinnedShader);

    int clip = (argc >= 4 && argv[3][0] >= '0' && argv[3][0] <= '9')
             ? atoi(argv[3]) : 0;
    bool live = false;
    for (int a = 3; a < argc; a++) if (strcmp(argv[a], "--live") == 0) live = true;

    // No level: drive fog far away so the model isn't washed out, and use
    // the default sun/ambient set in assets.c.
    fogStart = 100.0f; fogEnd = 200.0f; fogColor = (Color){ 30, 33, 40, 255 };

    Camera cam = {
        .position = { 2.6f, 1.4f, 2.6f }, .target = { 0, 0.8f, 0 },
        .up = { 0, 1, 0 }, .fovy = 55.0f, .projection = CAMERA_PERSPECTIVE,
    };
    AnimState st; Anim_Play(&st, clip, true, 1.0f);
    float dur = Anim_ClipDuration(&am, clip);

    // Helper to push the skinned shader's fog/sun uniforms (BeginWorldShader
    // is internal to render.c; the test harness sets them directly).
    #define PUSH_SKIN_UNIFORMS() do { \
        if (worldSkinnedShaderLoaded) { \
            float fc[4]={fogColor.r/255.f,fogColor.g/255.f,fogColor.b/255.f,1}; \
            float sd[3]={sunDir.x,sunDir.y,sunDir.z}; \
            float sc[3]={sunColor.x,sunColor.y,sunColor.z}; \
            float ac[3]={ambientColor.x,ambientColor.y,ambientColor.z}; \
            SetShaderValue(worldSkinnedShader, worldSkinnedShader_fogColorLoc, fc, SHADER_UNIFORM_VEC4); \
            SetShaderValue(worldSkinnedShader, worldSkinnedShader_fogStartLoc, &fogStart, SHADER_UNIFORM_FLOAT); \
            SetShaderValue(worldSkinnedShader, worldSkinnedShader_fogEndLoc,   &fogEnd,   SHADER_UNIFORM_FLOAT); \
            SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunDirLoc,       sd, SHADER_UNIFORM_VEC3); \
            SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunColorLoc,     sc, SHADER_UNIFORM_VEC3); \
            SetShaderValue(worldSkinnedShader, worldSkinnedShader_ambientColorLoc, ac, SHADER_UNIFORM_VEC3); \
        } } while (0)

    if (live) {
        float yaw = 0.0f;
        while (!WindowShouldClose()) {
            Anim_Update(&am, &st, GetFrameTime());
            yaw += GetFrameTime() * 40.0f;
            BeginDrawing();
            ClearBackground((Color){ 50, 55, 65, 255 });
            PUSH_SKIN_UNIFORMS();
            BeginMode3D(cam);
                DrawGrid(10, 0.5f);
                Anim_Draw(&am, &st, (Vector3){0,0,0}, yaw, 1.0f, WHITE);
            EndMode3D();
            DrawText(TextFormat("clip %d/%d  %.2fs", clip, am.animCount, st.time), 20, 20, 24, RAYWHITE);
            EndDrawing();
        }
    } else {
        for (int i = 0; i < 4; i++) {
            st.time = dur * (i / 4.0f);
            float yaw = 35.0f + i * 12.0f;
            BeginDrawing();
            ClearBackground((Color){ 50, 55, 65, 255 });
            PUSH_SKIN_UNIFORMS();
            BeginMode3D(cam);
                DrawGrid(10, 0.5f);
                Anim_Draw(&am, &st, (Vector3){0,0,0}, yaw, 1.0f, WHITE);
            EndMode3D();
            DrawText(TextFormat("%s  clip %d  t=%.2fs/%.2fs", am.name, clip, st.time, dur),
                     20, 20, 22, RAYWHITE);
            EndDrawing();
            char fn[64]; snprintf(fn, sizeof fn, "anim_test_%d.png", i);
            TakeScreenshot(fn);
            fprintf(stderr, "wrote %s\n", fn);
        }
    }
    #undef PUSH_SKIN_UNIFORMS
    Anim_Unload(&am);
    Assets_Unload();
    CloseWindow();
    return 0;
}

// ---- dispatcher ------------------------------------------------------------

bool Devtools_HandleCLI(int argc, char **argv, int *exitCode) {
    if (argc >= 3 && strcmp(argv[1], "--validate") == 0) {
        *exitCode = Dev_Validate(argc, argv);
        return true;
    }
    if (argc >= 2 && strcmp(argv[1], "--screenshot-viewmodels") == 0) {
        *exitCode = Dev_ScreenshotViewmodels();
        return true;
    }
    if (argc >= 2 && strcmp(argv[1], "--screenshot-coop") == 0) {
        *exitCode = Dev_ScreenshotCoop();
        return true;
    }
    if (argc >= 2 && strcmp(argv[1], "--screenshot-pap") == 0) {
        *exitCode = Dev_ScreenshotPaP();
        return true;
    }
    if (argc >= 3 && strcmp(argv[1], "--anim-test") == 0) {
        *exitCode = Dev_AnimTest(argc, argv);
        return true;
    }
    return false;
}
