#include "devtools.h"
#include "raylib.h"
#include "raymath.h"
#include "types.h"
#include "level.h"
#include "mapdoc.h"
#include "player.h"
#include "weapons.h"
#include "entities.h"
#include "interact.h"
#include "assets.h"
#include "anim.h"
#include "render.h"
#include "viewmodel.h"
#include "particles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ---- --validate ------------------------------------------------------------

static int Dev_Validate(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: --validate <map.map>\n"); return 1; }
    SetTraceLogLevel(LOG_ERROR);
    int errs = Level_Validate(argv[2]);
    return errs > 0 ? 1 : 0;
}

// ---- --map-roundtrip -------------------------------------------------------
// Headless: parse -> save to tmp -> parse again -> compare.
// Exits 0 on PASS, 1 on FAIL or I/O error.

static int Dev_MapRoundtrip(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: --map-roundtrip <file.map>\n"); return 1; }
    const char *path = argv[2];

    /* Step 1: parse original */
    MapDoc doc1;
    int e1 = MapDoc_Parse(path, &doc1, stderr);
    if (e1 > 0) {
        fprintf(stderr, "roundtrip: original parse had %d error(s) — FAIL\n", e1);
        return 1;
    }
    fprintf(stderr, "roundtrip: parsed  '%s'  OK\n", path);

    /* Step 2: save to temp file */
    char tmpPath[512];
    snprintf(tmpPath, sizeof tmpPath, "%s.roundtrip_tmp", path);
    if (MapDoc_Save(tmpPath, &doc1) != 0) {
        fprintf(stderr, "roundtrip: could not write temp file '%s' — FAIL\n", tmpPath);
        return 1;
    }
    fprintf(stderr, "roundtrip: saved   '%s'\n", tmpPath);

    /* Step 3: re-parse the saved file */
    MapDoc doc2;
    int e2 = MapDoc_Parse(tmpPath, &doc2, stderr);
    remove(tmpPath);
    if (e2 > 0) {
        fprintf(stderr, "roundtrip: re-parse had %d error(s) — FAIL\n", e2);
        return 1;
    }
    fprintf(stderr, "roundtrip: re-parsed OK\n");

    /* Step 4: compare */
    if (!MapDoc_Equal(&doc1, &doc2)) {
        fprintf(stderr, "roundtrip: documents differ — FAIL\n");
        return 1;
    }
    fprintf(stderr, "roundtrip: %s — PASS\n", path);
    return 0;
}

// ---- --screenshot-viewmodels -----------------------------------------------

static int Dev_ScreenshotViewmodels(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Viewmodel screenshot");
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Viewmodel_LoadArms();          // shared arms + bolted gun (fallback path)
    Viewmodel_LoadCombinedRigs();  // per-weapon combined rigs (takes priority when loaded)
    vmDebugMarkers = true;         // hand-bone markers for vm_grip_* tuning (arms path only)

    // Need a minimal level so Render_World3D doesn't barf on the
    // walls/floor draw paths. Level_Build initializes the hardcoded
    // fallback which is sufficient as a backdrop.
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true;
    g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 0 };
    g_world.players[0].yaw = 0.0f;
    g_world.players[0].pitch = 0.0f;
    g_world.players[0].currentSlot = 0;

    Camera cam = {
        .position   = g_world.players[0].pos,
        .target     = Vector3Add(g_world.players[0].pos, Player_LookDir(0, 0)),
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
        g_world.players[0].inventory[0].weaponIdx = wi;
        g_world.players[0].inventory[0].owned = true;
        g_world.players[0].inventory[0].ammo = WEAPONS[wi].magSize;
        g_world.players[0].inventory[0].reserve = WEAPONS[wi].reserveMax;

        // Render_World3D activates the world shader and draws the
        // viewmodel inside it — so we get lit materials instead of
        // a black silhouette. The empty level just draws a sky + floor
        // backdrop which makes scale easier to gauge anyway.
        //
        // The arms VM plays its `raise` clip on every weapon change, so a
        // single frame would capture the gun mid-raise (low, half off
        // screen). Render ~1.25 s of settle frames first so the capture is
        // the true idle pose — that's the pose grips are tuned against.
        SetTargetFPS(60);
        for (int f = 0; f < 75; f++) {
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
        }

        char fname[128];
        snprintf(fname, sizeof fname, "viewmodel_%s.png",
                 WEAPONS[wi].idName ? WEAPONS[wi].idName : "x");
        TakeScreenshot(fname);
        fprintf(stderr, "wrote %s\n", fname);
    }
    Viewmodel_UnloadCombinedRigs();
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
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 9 };  // not drawn (local)

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
        for (int i = 1; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
        // The noclip scene shows YOUR OWN body left behind: move the local
        // player into view, flip noclipMode on so it's drawn + the
        // first-person viewmodel is suppressed. Restored after the shot.
        noclipMode = (s == 3);
        if (s == 3) {
            g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 0 };
            g_world.players[0].yaw = YAW;
            g_world.players[0].inventory[0].owned = true;  // would show a gun if not noclip
        } else {
            g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 9 };  // behind cam (not drawn)
        }
        for (int f = 0; f < scenes[s].frames; f++) {
            float dt = GetFrameTime(); if (dt <= 0) dt = 1.0f/60.0f;
            if (s == 0) {
                // walker (slot1), runner (slot2), idler (slot3)
                if (f == 0) {
                    g_world.players[1].active=g_world.players[1].alive=true; g_world.players[1].yaw=YAW;
                    g_world.players[1].pos=(Vector3){-2.2f,PLAYER_EYE,0};
                    g_world.players[2].active=g_world.players[2].alive=true; g_world.players[2].yaw=YAW;
                    g_world.players[2].pos=(Vector3){-3.6f,PLAYER_EYE,1.4f};
                    g_world.players[3].active=g_world.players[3].alive=true; g_world.players[3].yaw=YAW;
                    g_world.players[3].pos=(Vector3){2.2f,PLAYER_EYE,0.4f};
                }
                g_world.players[1].pos.x += 7.0f * dt;    // walk speed
                g_world.players[2].pos.x += 11.0f * dt;   // sprint speed
            } else if (s == 1) {
                // reloading (slot1), downed (slot2), dead (slot3)
                if (f == 0) {
                    g_world.players[1].active=g_world.players[1].alive=true; g_world.players[1].yaw=YAW;
                    g_world.players[1].pos=(Vector3){-2.4f,PLAYER_EYE,0};
                    g_world.players[1].inventory[0].owned=true;
                    g_world.players[2].active=g_world.players[2].alive=true; g_world.players[2].downed=true;
                    g_world.players[2].yaw=YAW; g_world.players[2].pos=(Vector3){0,PLAYER_EYE,0.3f};
                    g_world.players[3].active=true; g_world.players[3].alive=false;
                    g_world.players[3].yaw=YAW; g_world.players[3].pos=(Vector3){2.4f,PLAYER_EYE,0.3f};
                }
                g_world.players[1].inventory[0].reloadTimer = 1.5f;  // hold in reload
            } else if (s == 2) {
                // reviver (slot1) kneeling over a downed teammate (slot2)
                if (f == 0) {
                    g_world.players[1].active=g_world.players[1].alive=true; g_world.players[1].yaw=YAW;
                    g_world.players[1].pos=(Vector3){-0.8f,PLAYER_EYE,0};
                    g_world.players[2].active=g_world.players[2].alive=true; g_world.players[2].downed=true;
                    g_world.players[2].yaw=YAW+PI*0.5f; g_world.players[2].pos=(Vector3){0.7f,PLAYER_EYE,0.2f};
                }
                g_world.players[2].reviveAsTarget = 2.0f;  // being revived
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
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 6 };

    g_world.pap.pos = (Vector3){ 0, 0, 0 };           // bring it to the origin
    g_world.pap.ownerPlayer = 0; g_world.pap.slotInProgress = 0; g_world.pap.weaponIdx = W_RIFLE;

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
        g_world.pap.phase = shots[s].phase; g_world.pap.timer = shots[s].timer;
        g_world.pap.bob = 1.2f;
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

// ---- --screenshot-zombies --------------------------------------------------
// Spawns three enemies (walk / attack / dying) and screenshots them so the
// death clip pose and attack clip can be visually verified. Mirrors
// --screenshot-coop: loads assets, sets up dummy state, runs N frames to
// advance the animation, then writes zombies_*.png.

static int Dev_ScreenshotZombies(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Zombie anim screenshot");
    SetTargetFPS(60);
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Render_LoadZombieAnim();
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 9 };   // behind camera (not drawn)

    // Clear all enemy slots.
    for (int i = 0; i < MAX_ENEMIES; i++) memset(&enemies[i], 0, sizeof enemies[i]);

    // Slot 0: walking zombie (alive, no timers).
    enemies[0].alive = true;
    enemies[0].pos   = (Vector3){ -2.5f, ENEMY_HEIGHT * 0.5f, 0 };
    enemies[0].type  = ZT_NORMAL;
    enemies[0].speed = 1.8f;
    enemies[0].targetPlayer = 0;

    // Slot 1: attacking zombie (alive, simAttackTimer set mid-clip).
    enemies[1].alive          = true;
    enemies[1].pos            = (Vector3){  0.0f, ENEMY_HEIGHT * 0.5f, 0 };
    enemies[1].type           = ZT_NORMAL;
    enemies[1].speed          = 1.8f;
    enemies[1].simAttackTimer = ENEMY_ATTACK_TIMER * 0.5f;
    enemies[1].targetPlayer   = 0;

    // Slot 2: dying zombie (alive=false, dyingTimer set ~halfway through).
    enemies[2].alive      = false;
    enemies[2].dyingTimer = ENEMY_DEATH_WINDOW * 0.5f;
    enemies[2].pos        = (Vector3){  2.5f, ENEMY_HEIGHT * 0.5f, 0 };
    enemies[2].type       = ZT_NORMAL;
    enemies[2].speed      = 1.8f;
    enemies[2].targetPlayer = 0;

    // Camera looks down -Z at the three zombies.
    Camera cam = {
        .position   = (Vector3){ 0, 2.2f, 5.5f },
        .target     = (Vector3){ 0, 0.9f, 0 },
        .up         = (Vector3){ 0, 1, 0 },
        .fovy       = 55.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    // Run enough frames to advance the animation states then screenshot.
    // 45 frames at 60 fps gives the clips time to reach a mid-pose.
    const int WARMUP = 45;
    for (int f = 0; f < WARMUP; f++) {
        BeginDrawing();
        ClearBackground((Color){ 60, 65, 75, 255 });
        Render_World3D(cam);
        EndDrawing();
    }
    TakeScreenshot("zombies_states.png");
    fprintf(stderr, "wrote zombies_states.png  (left=walk, centre=attack, right=dying)\n");

    Render_UnloadZombieAnim();
    Assets_Unload(); Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --screenshot-particles ------------------------------------------------
// Spawns muzzle-flash, blood-mist, and explosion bursts at fixed world
// positions; renders 3 frames at increasing ages → particles_0.png … 2.
// Visual check: additive glow visible for flash/explosion, dark-red puffs
// for blood, particles spread/fade over the three frames.
//
// Because Render_World3D calls Particles_Update each frame and the short-lived
// flash particles die in <0.1 s, we re-spawn fresh bursts for each screenshot
// (so each image captures the "just spawned" state), then walk forward a few
// frames to show spread/fade progression.

static int Dev_ScreenshotParticles(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Particle screenshot");
    SetTargetFPS(60);
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 9 };

    // Camera positioned to show all three effects at a closer range so the
    // billboard particles subtend enough pixels to be clearly visible.
    Camera cam = {
        .position   = (Vector3){ 0, 1.8f, 3.5f },
        .target     = (Vector3){ 0, 1.0f, 0 },
        .up         = (Vector3){ 0, 1, 0 },
        .fovy       = 80.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    // Drain the large first-frame GetFrameTime spike (window init takes ~0.5s
    // and that dt would kill all particles in one update). Run enough dummy
    // frames (with no particles in the pool) to normalise dt back to ~16 ms.
    Particles_Reset();
    for (int f = 0; f < 10; f++) {
        BeginDrawing(); ClearBackground((Color){ 50, 52, 62, 255 });
        Render_World3D(cam); EndDrawing();
    }

    // Each screenshot: spawn fresh, advance N frames, take screenshot on the
    // final frame by calling TakeScreenshot *before* EndDrawing (raylib captures
    // it at EndDrawing time). Frame counts chosen so all three ages show visibly
    // different states of the explosion sparks (longest-lived effect).
    // flash life ~0.04-0.10 s, blood ~0.20-0.45 s, explosion ~0.25-0.80 s.
    int advances[3] = { 1, 4, 18 };
    for (int s = 0; s < 3; s++) {
        Particles_Reset();
        Particles_MuzzleFlash((Vector3){ -1.8f, 1.4f, 0.0f }, (Vector3){ 0, 0, -1 });
        Particles_CasingEject((Vector3){ -1.8f, 1.4f, 0.0f }, (Vector3){ 1, 0, 0 });
        Particles_BloodMist((Vector3){  0.0f, 1.2f, 0.0f }, (Vector3){ 0, 0, 1 }, true);
        Particles_Explosion((Vector3){  1.8f, 0.1f, 0.0f });

        char fn[64]; snprintf(fn, sizeof fn, "particles_%d.png", s);
        for (int f = 0; f < advances[s]; f++) {
            BeginDrawing();
            ClearBackground((Color){ 50, 52, 62, 255 });
            Render_World3D(cam);
            if (f == advances[s] - 1) TakeScreenshot(fn);
            EndDrawing();
        }
        fprintf(stderr, "wrote %s  (%d frame(s) in)\n", fn, advances[s]);
    }

    Assets_Unload(); Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --screenshot-postfx ---------------------------------------------------
// Renders the world scene through the post-FX pipeline three times and saves
// each as a separate PNG:
//   postfx_baseline.png  — hitFlash=0 / lowHp=0   (vignette + bloom only)
//   postfx_hitflash.png  — hitFlash=0.8            (red-tinted edges)
//   postfx_lowhp.png     — lowHp=0.9               (desaturated heartbeat pulse)
// Passes the hitFlash / lowHp values directly to Render_EndPostFX.

static int Dev_ScreenshotPostFX(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Post-FX screenshot");
    SetTargetFPS(60);
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Level_Build();

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 0 };
    g_world.players[0].yaw = 0.0f;
    g_world.players[0].pitch = 0.0f;

    Camera cam = {
        .position   = (Vector3){ 0, PLAYER_EYE, 5.0f },
        .target     = (Vector3){ 0, PLAYER_EYE, 0 },
        .up         = (Vector3){ 0, 1, 0 },
        .fovy       = 75.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    // Warm-up frames to get a stable dt and let the shader load settle.
    for (int f = 0; f < 10; f++) {
        BeginDrawing();
        ClearBackground((Color){ 20, 25, 35, 255 });
        Render_BeginPostFX();
        ClearBackground((Color){ 20, 25, 35, 255 });
        Render_World3D(cam);
        Render_EndPostFX(0.0f, 0.0f);
        EndDrawing();
    }

    struct { const char *name; float hitFlash; float lowHp; } shots[] = {
        { "postfx_baseline.png",  0.0f, 0.0f },
        { "postfx_hitflash.png",  0.8f, 0.0f },
        { "postfx_lowhp.png",     0.0f, 0.9f },
    };
    for (int s = 0; s < 3; s++) {
        BeginDrawing();
        ClearBackground((Color){ 20, 25, 35, 255 });
        Render_BeginPostFX();
        ClearBackground((Color){ 20, 25, 35, 255 });
        Render_World3D(cam);
        Render_EndPostFX(shots[s].hitFlash, shots[s].lowHp);
        TakeScreenshot(shots[s].name);
        EndDrawing();
        fprintf(stderr, "wrote %s  (hitFlash=%.1f lowHp=%.1f)\n",
                shots[s].name, shots[s].hitFlash, shots[s].lowHp);
    }

    Render_UnloadPostFX();
    Assets_Unload(); Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --screenshot-map <file.map> -------------------------------------------
// Loads an arbitrary map and renders it from a few vantage points so map
// geometry — including multi-floor decks/ramps — can be eyeballed headless.

static int Dev_ScreenshotMap(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: --screenshot-map <file.map>\n"); return 1; }
    const char *path = argv[2];
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Map screenshot");
    SetTargetFPS(60);
    Weapons_Load();
    Assets_Load();
    Assets_ApplyWorldShader();
    Render_LoadPlayerAnim();
    if (!Level_LoadFromFile(path)) {
        fprintf(stderr, "failed to load map: %s\n", path);
        Render_UnloadPlayerAnim(); Assets_Unload(); Weapons_Unload();
        CloseWindow();
        return 1;
    }

    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    g_world.players[0].pos = (Vector3){ 0, PLAYER_EYE, 6 };

    // A 3/4 aerial that frames the whole arena, and a low angle that shows
    // floor elevation in profile. Targets the arena centre-ish.
    struct { const char *name; Vector3 pos, target; } shots[] = {
        { "map_aerial", (Vector3){ 38, 42, 46 }, (Vector3){ 8, 4, 0 } },
        { "map_profile",(Vector3){ -6, 6, 26 },  (Vector3){ 12, 4, -6 } },
    };
    for (int s = 0; s < 2; s++) {
        Camera cam = {
            .position = shots[s].pos, .target = shots[s].target,
            .up = (Vector3){ 0, 1, 0 }, .fovy = 60.0f, .projection = CAMERA_PERSPECTIVE,
        };
        BeginDrawing();
        ClearBackground((Color){ 50, 52, 60, 255 });
        Render_World3D(cam);
        DrawText(TextFormat("%s  (%d floor regions)", path, g_world.floorCount),
                 20, 20, 24, (Color){240,240,240,255});
        EndDrawing();
        char fn[96]; snprintf(fn, sizeof fn, "%s.png", shots[s].name);
        TakeScreenshot(fn);
        fprintf(stderr, "wrote %s\n", fn);
    }
    Render_UnloadPlayerAnim();
    Assets_Unload(); Weapons_Unload();
    CloseWindow();
    return 0;
}

// ---- --sim-navtest <file.map> ----------------------------------------------
// Headless behavioural test for cross-floor AI nav: load a map, put a zombie
// on the ground and the player on the highest deck at (15,-10) [the demo map's
// elevated floor], tick the sim, and check the zombie climbs a ramp up to the
// player's floor. Exit 0 = PASS. No rendering — just simulation.

static int Dev_SimNavtest(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: --sim-navtest <file.map>\n"); return 1; }
    const char *path = argv[2];
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(320, 240, "navtest");   // GL context for asset/texture lookups
    Weapons_Load();
    Assets_Load();
    if (!Level_LoadFromFile(path)) {
        fprintf(stderr, "failed to load map: %s\n", path);
        Assets_Unload(); Weapons_Unload(); CloseWindow();
        return 1;
    }

    godMode = true;             // keep the test player alive while chased
    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    g_world.players[0].active = true; g_world.players[0].alive = true;
    float deckSurf = Level_FloorHeightAt(15, -10, 100.0f);   // highest surface there
    g_world.players[0].pos = (Vector3){ 15, deckSurf + PLAYER_EYE, -10 };

    Enemies_ClearAll();
    enemies[0] = (Enemy){
        .pos = { 0, ENEMY_HEIGHT * 0.5f, -10 },
        .hp = 100000, .maxHp = 100000, .alive = true, .speed = 3.5f,
        .state = ZS_INSIDE, .type = ZT_NORMAL, .targetPlayer = 0, .targetWindow = 0,
    };

    float startY = enemies[0].pos.y, maxY = startY;
    float wantCentreY = deckSurf + ENEMY_HEIGHT * 0.5f;
    int   reachedFrame = -1;
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < 1800 && reachedFrame < 0; f++) {   // up to 30 s
        Enemies_Update(dt);
        if (enemies[0].pos.y > maxY) maxY = enemies[0].pos.y;
        if (fabsf(enemies[0].pos.y - wantCentreY) < 0.6f) reachedFrame = f;
    }
    bool pass = reachedFrame >= 0;
    fprintf(stderr,
            "navtest %s: deck surf=%.2f  zombie startY=%.2f maxY=%.2f endY=%.2f  -> %s",
            path, deckSurf, startY, maxY, enemies[0].pos.y,
            pass ? "" : "FAIL\n");
    if (pass)
        fprintf(stderr, "PASS (reached deck in %.2fs)\n", reachedFrame / 60.0f);

    Assets_Unload(); Weapons_Unload(); CloseWindow();
    return pass ? 0 : 1;
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
    if (argc >= 2 && strcmp(argv[1], "--screenshot-zombies") == 0) {
        *exitCode = Dev_ScreenshotZombies();
        return true;
    }
    if (argc >= 2 && strcmp(argv[1], "--screenshot-particles") == 0) {
        *exitCode = Dev_ScreenshotParticles();
        return true;
    }
    if (argc >= 2 && strcmp(argv[1], "--screenshot-postfx") == 0) {
        *exitCode = Dev_ScreenshotPostFX();
        return true;
    }
    if (argc >= 3 && strcmp(argv[1], "--anim-test") == 0) {
        *exitCode = Dev_AnimTest(argc, argv);
        return true;
    }
    if (argc >= 3 && strcmp(argv[1], "--map-roundtrip") == 0) {
        *exitCode = Dev_MapRoundtrip(argc, argv);
        return true;
    }
    if (argc >= 3 && strcmp(argv[1], "--screenshot-map") == 0) {
        *exitCode = Dev_ScreenshotMap(argc, argv);
        return true;
    }
    if (argc >= 3 && strcmp(argv[1], "--sim-navtest") == 0) {
        *exitCode = Dev_SimNavtest(argc, argv);
        return true;
    }
    return false;
}
