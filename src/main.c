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
#include "viewmodel.h"
#include "devtools.h"
#include "hud.h"
#include "menu.h"
#include "pad.h"
#include "settings.h"
#include "fx.h"
#include "audio.h"
#include "audio_director.h"
#include "decals.h"
#include "particles.h"
#include "assets.h"
#include "anim.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float snapshotAccum = 0.0f;
static float inputAccum    = 0.0f;

// ---- Post-FX state ---------------------------------------------------------
// hitFlash: spikes to 1.0 when the local player's HP drops in a single frame
// (damage taken), then decays over ~0.35 s. Does NOT trigger on HP gains
// (regen, Juggernog) or large upward jumps (respawn: hp reset from 0 -> max).
// lowHp: rises as HP falls below ~40% of max; 1.0 when downed.
static float postfx_hitFlash     = 0.0f;
static float postfx_lowHp        = 0.0f;
static int   postfx_prevHp       = -1;   // -1 = uninitialised (first frame)

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
    // CLI dev modes (--validate, --screenshot-*, --anim-test) are handled
    // in devtools.c. Each mode exits without opening the game window.
    {
        int devExitCode = 0;
        if (Devtools_HandleCLI(argc, argv, &devExitCode)) return devExitCode;
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W_DEFAULT, WINDOW_H_DEFAULT, "Claude Zombies");
    SetTargetFPS(60);
    SetExitKey(0);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    Audio_Init();
    Weapons_Load();   // before Assets_Load: enrols weapon models for the world shader
    Assets_Load();
    Render_LoadZombieAnim();        // shared rigged zombie.glb (skinned shader from Assets_Load)
    Viewmodel_LoadArms();           // shared first-person arms (non-pistol guns bolt onto hand.R)
    Viewmodel_LoadCombinedRigs();   // per-weapon combined rigs (arms+gun glTF, if present)
    Render_LoadPlayerAnim();        // rigged third-person soldier for co-op teammates
    Settings_Load();
    Decals_Init();
    Particles_Reset();
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
            // Leaving gameplay → menu: invalidate prevHp so re-entering doesn't
            // false-trigger a hit-flash (HP is reset between rounds/restarts).
            if (uiState != UI_PLAY && uiState != UI_PAUSE) postfx_prevHp = -1;
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
                // While noclipping, the body is left behind — frozen at its
                // entry position AND facing — and the fly camera is fully
                // detached, so we do NOT write specYaw/specPitch back onto the
                // player. (Dead spectating doesn't move the body either.)
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
        }

        Fx_Tick(dt);
        if (uiState == UI_PLAY) AudioDirector_Tick(me);

        // ---- Post-FX state update -------------------------------------------
        // Only when in gameplay (UI_PLAY or UI_PAUSE) and me is a valid player.
        if (uiState == UI_PLAY || uiState == UI_PAUSE) {
            int curHp = me->hp;
            int maxHp = Perk_EffMaxHP(me);

            // hitFlash: spike on damage, decay toward 0
            if (postfx_prevHp >= 0 && curHp < postfx_prevHp) {
                // HP dropped: spike to 1.0
                postfx_hitFlash = 1.0f;
            }
            postfx_hitFlash -= dt / 0.35f;
            if (postfx_hitFlash < 0.0f) postfx_hitFlash = 0.0f;
            postfx_prevHp = curHp;

            // lowHp: 0 when hp >= 40% max, ramps to 1.0 as hp → 0.
            // Force 1.0 while downed.
            float targetLowHp;
            if (me->downed) {
                targetLowHp = 1.0f;
            } else {
                float threshold = (float)maxHp * 0.4f;
                float ratio = ((float)curHp - 0.0f) / fmaxf(threshold, 1.0f);
                // ratio: 1.0 at threshold, 0.0 at 0 hp — clamp to [0,1]
                ratio = 1.0f - fminf(1.0f, fmaxf(0.0f, ratio));
                // ratio is now 0 at full HP, 1 at 0 HP, but only active below threshold
                targetLowHp = (curHp < (int)threshold) ? ratio : 0.0f;
            }
            // Smooth toward target (~0.15 s to settle)
            postfx_lowHp += (targetLowHp - postfx_lowHp) * (1.0f - expf(-dt / 0.15f));
            if (postfx_lowHp < 0.001f) postfx_lowHp = 0.0f;
        }
        // ---- End post-FX state update ----------------------------------------

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
            // World pass — rendered into the post-FX RT when the shader loaded.
            // Render_BeginPostFX / EndPostFX are no-ops when shader is missing.
            Render_BeginPostFX();
            ClearBackground((Color){20,25,35,255});
            Render_World3D(camera);
            Render_WorldLabels(camera, sw, sh, me);
            Render_EndPostFX(postfx_hitFlash, postfx_lowHp);
            // HUD and menus are drawn AFTER EndPostFX — they must not be
            // post-processed (would apply bloom/vignette to the HUD).
            Hud_Draw(sw, sh, me, (uiState == UI_PLAY) ? ix : (Interact){ IK_NONE, -1, 0 });
            if (uiState == UI_PAUSE) Menu_DrawPause(sw, sh);
            if (gamePhase == GS_GAME_OVER) Menu_DrawGameOver(sw, sh);
        }

        EndDrawing();
        Settings_TickTriggerEdges();
    }

    Settings_Save();
    Render_UnloadPostFX();
    Render_UnloadZombieAnim();
    Viewmodel_UnloadArms();
    Viewmodel_UnloadCombinedRigs();
    Render_UnloadPlayerAnim();
    Assets_Unload();
    Weapons_Unload();
    Audio_Shutdown();
    Net_Shutdown();
    CloseWindow();
    return 0;
}
