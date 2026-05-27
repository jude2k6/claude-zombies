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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float snapshotAccum = 0.0f;
static float inputAccum    = 0.0f;

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
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_TAB)) {
        int other = (me->currentSlot + 1) % INV_SLOTS;
        if (me->inventory[other].owned) me->currentSlot = other; // local predict
    }
    if (IsKeyPressed(KEY_ONE) && me->inventory[0].owned) me->currentSlot = 0;
    if (IsKeyPressed(KEY_TWO) && me->inventory[1].owned) me->currentSlot = 1;

    bool reloadEdge = IsKeyPressed(KEY_R) && me->alive;
    bool swapEdge   = (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_TAB)) && me->alive;
    bool slot1Edge  = IsKeyPressed(KEY_ONE) && me->alive;
    bool slot2Edge  = IsKeyPressed(KEY_TWO) && me->alive;
    bool interactEdge = IsKeyPressed(KEY_F) && me->alive;

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
    }
}

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W_DEFAULT, WINDOW_H_DEFAULT, "Claude Zombies");
    SetTargetFPS(60);
    SetExitKey(0);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    Level_Build();
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

        if (IsKeyPressed(KEY_ESCAPE)) {
            if      (uiState == UI_PLAY)       uiState = UI_PAUSE;
            else if (uiState == UI_PAUSE)      uiState = UI_PLAY;
            else if (uiState == UI_SETTINGS)   uiState = UI_MENU;
            else if (uiState == UI_JOIN_INPUT) uiState = UI_MENU;
        }
        if (IsKeyPressed(KEY_F11)) Menu_ToggleFullscreenSafe();
        if (IsKeyPressed(KEY_F3))  godMode = !godMode;

        if (netMode != NET_SOLO) PollNetwork();

        Player *me = &players[localPlayerIdx];
        Interact ix = { IK_NONE, -1, 0 };

        if (uiState == UI_PLAY) {
            if (me->alive) {
                Player_ApplyLocalLook(me, mouseSens);
                Player_ApplyLocalMove(me, dt);
            }
            me->fireHeld     = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && me->alive;
            me->interactHeld = IsKeyDown(KEY_E) && me->alive;

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

        camera.position = (Vector3){ me->pos.x, PLAYER_EYE, me->pos.z };
        Vector3 dir = Player_LookDir(me->yaw, me->pitch);
        camera.target = Vector3Add(camera.position, dir);
        camera.fovy = fovSetting;

        BeginDrawing();
        ClearBackground((Color){20,25,35,255});

        if (uiState == UI_MENU) {
            Menu_DrawMenu(sw, sh);
        } else if (uiState == UI_SETTINGS) {
            Menu_DrawSettings(sw, sh);
        } else if (uiState == UI_JOIN_INPUT) {
            Menu_DrawJoinInput(sw, sh);
        } else if (uiState == UI_CONNECTING) {
            Menu_DrawConnecting(sw, sh);
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
    }

    Net_Shutdown();
    CloseWindow();
    return 0;
}
