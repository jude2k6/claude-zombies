#include "raylib.h"
#include "raymath.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "types.h"
#include "net.h"
#include "level.h"
#include "player.h"
#include "weapons.h"
#include "perks.h"
#include "entities.h"
#include "interact.h"
#include "game.h"
#include "protocol.h"
#include "render.h"
#include "hud.h"
#include "menu.h"
#include "pad.h"
#include "settings.h"
#include "fx.h"
#include "audio.h"
#include "decals.h"
#include "assets.h"
#include "anim.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float snapshotAccum = 0.0f;
static float inputAccum    = 0.0f;

// Spectator camera state (used while local player is dead, or via F4 noclip cheat)
static Vector3 specPos;
static float   specYaw   = 0.0f;
static float   specPitch = 0.0f;
static bool    specInit  = false;
static int     specTarget = -1; // teammate idx the cam was last snapped to

// First-person view bob — purely visual, advanced from the local player's
// actual horizontal velocity each frame and smoothly faded in/out.
static float   bobPhase     = 0.0f;
static float   bobAmp       = 0.0f;
static Vector3 bobPrevPos   = { 0 };
static bool    bobPrevValid = false;

static int Spectator_NextTeammate(int after) {
    for (int step = 1; step <= NET_MAX_PLAYERS; step++) {
        int idx = (after + step) % NET_MAX_PLAYERS;
        if (idx == localPlayerIdx) continue;
        if (players[idx].active && players[idx].alive) return idx;
    }
    return -1;
}

static void Spectator_SnapTo(int idx) {
    if (idx < 0) return;
    Player *t = &players[idx];
    Vector3 fwd = { sinf(t->yaw), 0, -cosf(t->yaw) };
    // 3rd-person-ish, behind their shoulder.
    specPos = (Vector3){ t->pos.x - fwd.x * 2.6f,
                         t->pos.y + 1.2f,
                         t->pos.z - fwd.z * 2.6f };
    specYaw = t->yaw;
    specPitch = -0.15f;
    specTarget = idx;
}

static void PollNetwork(void) {
    NetEvent events[32];
    int n = Net_Poll(events, 32);
    for (int i = 0; i < n; i++) {
        NetEvent *ev = &events[i];
        if (netMode == NET_HOST) {
            if (ev->kind == NEV_DISCONNECT) {
                if (ev->peerIdx >= 0 && ev->peerIdx < NET_MAX_PLAYERS)
                    players[ev->peerIdx].active = false;
                Protocol_HostSendLobby();
            } else if (ev->kind == NEV_RECEIVE) {
                Protocol_HostHandlePacket(ev->peerIdx, ev->data, ev->len);
            }
        } else if (netMode == NET_CLIENT) {
            if (ev->kind == NEV_CONNECT) {
                PktHello h = { .type = PKT_HELLO, .proto = NET_PROTO_VERSION };
                strncpy(h.name, playerName, 31);
                Net_SendTo(0, &h, sizeof h, true);
            } else if (ev->kind == NEV_DISCONNECT) {
                snprintf(statusMsg, sizeof statusMsg, "Disconnected from server");
                Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
            } else if (ev->kind == NEV_RECEIVE) {
                Protocol_ClientHandlePacket(ev->data, ev->len, playerName);
            }
        }
        Net_FreeEvent(ev);
    }
}

static void HandleLocalActions(Player *me) {
    // Downed players can't act. They lay there until revived or bled out.
    bool canAct = me->alive && !me->downed;
    bool kbdSwap  = IsKeyPressed(KEY_Q) && canAct;
    bool padSwap  = Bind_Pressed(BA_SWAP) && canAct;
    if (kbdSwap || padSwap) {
        int other = (me->currentSlot + 1) % INV_SLOTS;
        if (me->inventory[other].owned) me->currentSlot = other; // local predict
    }
    if (canAct && (IsKeyPressed(KEY_ONE) || Bind_Pressed(BA_SLOT1)))  { if (me->inventory[0].owned) me->currentSlot = 0; }
    if (canAct && (IsKeyPressed(KEY_TWO) || Bind_Pressed(BA_SLOT2))) { if (me->inventory[1].owned) me->currentSlot = 1; }

    bool reloadEdge   = (IsKeyPressed(KEY_R) || Bind_Pressed(BA_RELOAD)) && canAct;
    bool swapEdge     = (kbdSwap || padSwap);
    bool slot1Edge    = canAct && (IsKeyPressed(KEY_ONE) || Bind_Pressed(BA_SLOT1));
    bool slot2Edge    = canAct && (IsKeyPressed(KEY_TWO) || Bind_Pressed(BA_SLOT2));
    bool interactEdge = canAct && (IsKeyPressed(KEY_F) || Bind_Pressed(BA_INTERACT));
    bool meleeEdge    = canAct && (IsKeyPressed(KEY_V) || Bind_Pressed(BA_MELEE));
    bool lethalEdge   = canAct && (IsKeyPressed(KEY_G) || Bind_Pressed(BA_THROW_LETHAL));
    bool tacticalEdge = canAct && (IsKeyPressed(KEY_H) || Bind_Pressed(BA_THROW_TACTICAL));

    int swapTarget = -1;
    if (swapEdge) {
        int other = (me->currentSlot + 1) % INV_SLOTS;
        if (me->inventory[other].owned) swapTarget = other;
    } else if (slot1Edge && me->inventory[0].owned) swapTarget = 0;
    else if (slot2Edge && me->inventory[1].owned) swapTarget = 1;

    bool isHost = (netMode != NET_CLIENT);
    if (isHost) {
        if (reloadEdge)   Weapon_StartReload(me);
        if (swapTarget >= 0) Weapon_SwapSlot(me, swapTarget);
        if (interactEdge) Interact_Do(me);
        if (meleeEdge)    Weapon_Melee(me);
        if (lethalEdge)   Throwables_Throw(me, TH_FRAG);
        if (tacticalEdge) Throwables_Throw(me, TH_STUN);
    } else {
        if (reloadEdge) {
            PktAction a = { .type = PKT_ACTION, .action = ACT_RELOAD };
            Net_SendTo(0, &a, sizeof a, true);
        }
        if (swapTarget >= 0) {
            me->currentSlot = swapTarget;
            PktAction a = { .type = PKT_ACTION, .action = ACT_SWAP_SLOT, .arg = (uint8_t)swapTarget };
            Net_SendTo(0, &a, sizeof a, true);
        }
        if (interactEdge) {
            PktAction a = { .type = PKT_ACTION, .action = ACT_INTERACT_F };
            Net_SendTo(0, &a, sizeof a, true);
        }
        if (meleeEdge) {
            PktAction a = { .type = PKT_ACTION, .action = ACT_MELEE };
            Net_SendTo(0, &a, sizeof a, true);
        }
        if (lethalEdge) {
            PktAction a = { .type = PKT_ACTION, .action = ACT_THROW_LETHAL };
            Net_SendTo(0, &a, sizeof a, true);
        }
        if (tacticalEdge) {
            PktAction a = { .type = PKT_ACTION, .action = ACT_THROW_TACTICAL };
            Net_SendTo(0, &a, sizeof a, true);
        }
    }
}

int main(int argc, char **argv) {
    // CLI: `--validate path/to/map.map` runs the parser without opening
    // a window and exits 0 (clean) or 1 (errors). Useful from editor
    // save hooks.
    if (argc >= 3 && strcmp(argv[1], "--validate") == 0) {
        SetTraceLogLevel(LOG_ERROR);
        int errs = Level_Validate(argv[2]);
        return errs > 0 ? 1 : 0;
    }

    // CLI: `--screenshot-viewmodels` renders each weapon's first-person
    // viewmodel against a neutral background and saves a PNG per weapon
    // to viewmodel_<NAME>.png in the CWD. Used to verify viewmodel scale
    // / yaw / connectivity without running the full game.
    if (argc >= 2 && strcmp(argv[1], "--screenshot-viewmodels") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_MSAA_4X_HINT);
        InitWindow(1280, 720, "Viewmodel screenshot");
        Weapons_Load();
        Assets_Load();
        Assets_ApplyWorldShader();

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

    // CLI: `--anim-test <file.glb> [clip]` loads a skinned glTF model through
    // the animation pipeline and writes a strip of PNGs sampling the clip
    // (anim_test_0..3.png) so skinning + lighting can be verified without the
    // full game. Add `--live` to instead open an interactive spinning viewer.
    if (argc >= 3 && strcmp(argv[1], "--anim-test") == 0) {
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

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W_DEFAULT, WINDOW_H_DEFAULT, "Claude Zombies");
    SetTargetFPS(60);
    SetExitKey(0);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    Audio_Init();
    Weapons_Load();   // must run before Assets_Load — Assets_ApplyWorldShader iterates weaponModels[]
    Assets_Load();
    Render_LoadZombieAnim();   // shared rigged zombie.glb (skinned shader from Assets_Load)
    Settings_Load();
    Decals_Init();
    if (fullscreen && !IsWindowFullscreen()) Menu_ToggleFullscreenSafe();
    Level_Build();
    Menu_ScanMaps();
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    Player_ResetForGame(0, playerName);

    Camera camera = { 0 };
    camera.position   = (Vector3){ 0.0f, PLAYER_EYE, 0.0f };
    camera.target     = (Vector3){ 0.0f, PLAYER_EYE, -1.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f,  0.0f };
    camera.fovy       = fovSetting;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        bool wantCursor = (uiState != UI_PLAY);
        if (uiState != prevUi) {
            if (wantCursor) EnableCursor(); else DisableCursor();
            prevUi = uiState;
        }

        // Suppress pad pause-edge in the bindings screen so the user can rebind
        // START itself without bouncing back to Settings.
        bool padPause = (uiState != UI_BINDINGS) && Bind_Pressed(BA_PAUSE);
        bool escEdge  = IsKeyPressed(KEY_ESCAPE);
        // In the bindings UI, ESC is consumed by an in-progress capture (to
        // cancel) before it can pop the screen.
        if (uiState == UI_BINDINGS && Menu_BindingsCaptureActive()) escEdge = false;
        bool pauseEdge = escEdge || padPause;
        if (pauseEdge) {
            if      (uiState == UI_PLAY)        uiState = UI_PAUSE;
            else if (uiState == UI_PAUSE)       uiState = UI_PLAY;
            else if (uiState == UI_BINDINGS)    uiState = UI_SETTINGS;
            else if (uiState == UI_SETTINGS)    uiState = UI_MENU;
            else if (uiState == UI_JOIN_INPUT)  uiState = UI_MENU;
            else if (uiState == UI_SOLO_LOBBY)  uiState = UI_MENU;
            else if (uiState == UI_MP_MENU)     uiState = UI_MENU;
        }
        if (IsKeyPressed(KEY_F11)) Menu_ToggleFullscreenSafe();
        if (IsKeyPressed(KEY_F3))  godMode = !godMode;
        bool noclipToggle = IsKeyPressed(KEY_F4);
        if (uiState == UI_PLAY && Bind_Pressed(BA_NOCLIP)) noclipToggle = true;
        if (noclipToggle) { noclipMode = !noclipMode; specInit = false; }

        if (netMode != NET_SOLO) PollNetwork();

        Player *me = &players[localPlayerIdx];
        Interact ix = { IK_NONE, -1, 0 };

        bool useFlyCam = (uiState == UI_PLAY) && (!me->alive || noclipMode);
        bool meDowned = me->alive && me->downed;
        if (uiState == UI_PLAY) {
            if (me->alive && !noclipMode) {
                Player_ApplyLocalLook(me, mouseSens);
                if (!meDowned) Player_ApplyLocalMove(me, dt);
                specInit = false; // re-init next time we detach
            }
            if (useFlyCam) {
                if (!specInit) {
                    // If we just died (not noclip) and a teammate is alive,
                    // start the spectate cam looking over their shoulder.
                    int snap = (!me->alive && !noclipMode) ? Spectator_NextTeammate(localPlayerIdx) : -1;
                    if (snap >= 0) {
                        Spectator_SnapTo(snap);
                    } else {
                        specPos   = (Vector3){ me->pos.x, me->pos.y + 1.5f, me->pos.z };
                        specYaw   = me->yaw;
                        specPitch = me->pitch;
                        specTarget = -1;
                    }
                    specInit  = true;
                }
                // Press F / A to cycle to next teammate while spectating dead.
                if (!me->alive && !noclipMode &&
                    (IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || Bind_Pressed(BA_JUMP))) {
                    int next = Spectator_NextTeammate(specTarget >= 0 ? specTarget : localPlayerIdx);
                    if (next >= 0) Spectator_SnapTo(next);
                }
                Vector2 md = GetMouseDelta();
                specYaw   += md.x * mouseSens;
                specPitch -= md.y * mouseSens;
                if (specPitch >  1.55f) specPitch =  1.55f;
                if (specPitch < -1.55f) specPitch = -1.55f;

                Vector3 fwd = Player_LookDir(specYaw, specPitch);
                Vector3 right = { cosf(specYaw), 0, sinf(specYaw) };
                Vector3 up    = { 0, 1, 0 };
                Vector3 move  = { 0 };
                if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
                if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
                if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
                if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
                if (IsKeyDown(KEY_SPACE))      move = Vector3Add(move, up);
                if (IsKeyDown(KEY_LEFT_SHIFT)) move = Vector3Subtract(move, up);
                float flySpeed = IsKeyDown(KEY_LEFT_CONTROL) ? 36.0f : 14.0f;
                if (Vector3LengthSqr(move) > 0.0001f) {
                    move = Vector3Scale(Vector3Normalize(move), flySpeed * dt);
                    specPos = Vector3Add(specPos, move);
                }
                // While noclipping alive, keep the player's view direction in
                // sync so the body faces wherever the camera is looking.
                if (noclipMode && me->alive) { me->yaw = specYaw; me->pitch = specPitch; }
            }
            bool actable = me->alive && !me->downed && !noclipMode;
            me->fireHeld     = (IsMouseButtonDown(MOUSE_BUTTON_LEFT)  || Bind_Down(BA_FIRE)) && actable;
            // E (keyboard) or holding the interact bind (gamepad) drives revive/repair.
            me->interactHeld = (IsKeyDown(KEY_E) || Bind_Down(BA_INTERACT))   && actable;
            me->adsHeld      = (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || Bind_Down(BA_ADS)) && actable;

            HandleLocalActions(me);

            bool isHost = (netMode != NET_CLIENT);
            if (isHost) {
                Game_Tick(dt);
                if (godMode) {
                    Player *gp = &players[localPlayerIdx];
                    gp->points = 999999;
                    gp->hp = Perk_EffMaxHP(gp);
                    gp->alive = true;
                }
                snapshotAccum += dt;
                if (snapshotAccum >= 1.0f / SNAPSHOT_HZ && netMode == NET_HOST) {
                    snapshotAccum = 0;
                    Protocol_HostBroadcastSnapshot();
                }
            } else {
                inputAccum += dt;
                if (inputAccum >= 1.0f / INPUT_HZ) {
                    inputAccum = 0;
                    Protocol_ClientSendInput(me);
                }
            }

            ix = Interact_FindFor(me);
            if (muzzleFlashLocal > 0) muzzleFlashLocal -= dt;
        }

        Fx_Tick(dt);
        if (uiState == UI_PLAY) Audio_Tick(me);

        float eyeY = me->pos.y;
        if (meDowned) eyeY = me->pos.y - 1.1f; // prone
        else if (me->alive && !noclipMode && uiState == UI_PLAY && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C))) eyeY -= 0.6f;

        // Walk bob - vertical bobs twice per stride (each footstep), lateral
        // sways once per stride. Amplitude tracks actual horizontal velocity so
        // sprinting bobs more, ADS bobs less, and the bob settles to zero when
        // standing still. Only applies in first-person; spectator/noclip skip.
        float bobOffX = 0.0f, bobOffY = 0.0f;
        if (uiState == UI_PLAY && me->alive && !me->downed && !noclipMode) {
            float dx = bobPrevValid ? (me->pos.x - bobPrevPos.x) : 0.0f;
            float dz = bobPrevValid ? (me->pos.z - bobPrevPos.z) : 0.0f;
            float vh = sqrtf(dx*dx + dz*dz) / fmaxf(dt, 1e-4f);
            float target = (me->onGround && vh > 0.5f) ? fminf(vh / BASE_MOVE_SPEED, 1.7f) : 0.0f;
            if (me->adsHeld) target *= 0.35f;
            // Smooth blend (~0.12s to settle).
            bobAmp += (target - bobAmp) * (1.0f - expf(-8.0f * dt));
            if (bobAmp < 0.001f) bobAmp = 0.0f;
            float cadence = 6.5f + 3.0f * me->sprintBlend;
            bobPhase += cadence * dt;
            bobOffX = sinf(bobPhase)        * 0.035f * bobAmp;
            bobOffY = sinf(bobPhase * 2.0f) * 0.045f * bobAmp;
        } else {
            // Decay quickly when not in normal play so the cam doesn't keep
            // bobbing during spectate/pause.
            bobAmp += (0.0f - bobAmp) * (1.0f - expf(-12.0f * dt));
        }
        bobPrevPos = me->pos;
        bobPrevValid = (uiState == UI_PLAY);

        float yawJ = 0, pitchJ = 0;
        Vector3 shakeOff = Fx_CameraOffset(&yawJ, &pitchJ);
        if (useFlyCam && specInit) {
            camera.position = specPos;
            Vector3 dir = Player_LookDir(specYaw, specPitch);
            camera.target = Vector3Add(camera.position, dir);
        } else {
            // Lateral bob is in screen-right of the look direction so it sways
            // independent of where you're facing.
            Vector3 right = { cosf(me->yaw), 0, sinf(me->yaw) };
            camera.position = (Vector3){
                me->pos.x + shakeOff.x + right.x * bobOffX,
                eyeY      + shakeOff.y + bobOffY,
                me->pos.z + shakeOff.z + right.z * bobOffX,
            };
            Vector3 dir = Player_LookDir(me->yaw + yawJ, me->pitch + pitchJ);
            camera.target = Vector3Add(camera.position, dir);
        }
        // Smoothly transition FOV when aiming down sights.
        {
            float targetFov = me->adsHeld ? (fovSetting * ADS_FOV_MUL) : fovSetting;
            camera.fovy += (targetFov - camera.fovy) * fminf(1.0f, dt * 12.0f);
        }

        BeginDrawing();
        ClearBackground((Color){20,25,35,255});

        if (uiState == UI_MENU) {
            Menu_DrawMenu(sw, sh);
        } else if (uiState == UI_SETTINGS) {
            Menu_DrawSettings(sw, sh);
        } else if (uiState == UI_BINDINGS) {
            Menu_DrawBindings(sw, sh);
        } else if (uiState == UI_JOIN_INPUT) {
            Menu_DrawJoinInput(sw, sh);
        } else if (uiState == UI_CONNECTING) {
            Menu_DrawConnecting(sw, sh);
        } else if (uiState == UI_SOLO_LOBBY) {
            Menu_DrawSoloLobby(sw, sh);
        } else if (uiState == UI_MP_MENU) {
            Menu_DrawMultiplayer(sw, sh);
        } else if (uiState == UI_HOST_LOBBY) {
            Menu_DrawLobby(sw, sh, true);
        } else if (uiState == UI_CLIENT_LOBBY) {
            Menu_DrawLobby(sw, sh, false);
        } else {
            Render_World3D(camera);
            Render_WorldLabels(camera, sw, sh, me);
            Hud_Draw(sw, sh, me, (uiState == UI_PLAY) ? ix : (Interact){ IK_NONE, -1, 0 });
            if (uiState == UI_PAUSE) Menu_DrawPause(sw, sh);
            if (gamePhase == GS_GAME_OVER) Menu_DrawGameOver(sw, sh);
        }

        EndDrawing();
        Settings_TickTriggerEdges();
    }

    Settings_Save();
    Render_UnloadZombieAnim();
    Assets_Unload();
    Weapons_Unload();
    Audio_Shutdown();
    Net_Shutdown();
    CloseWindow();
    return 0;
}
