#include "menu.h"
#include "player.h"
#include "game.h"
#include "net.h"
#include "protocol.h"
#include "entities.h"
#include "raygui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

UiState uiState = UI_MENU;
UiState prevUi  = UI_PAUSE;

float   mouseSens   = 0.0025f;
float   fovSetting  = 75.0f;
bool    fullscreen  = false;
int     windowedW   = WINDOW_W_DEFAULT;
int     windowedH   = WINDOW_H_DEFAULT;

char    playerName[32] = "Player";
bool    nameEditing = false;
char    joinIp[64]  = "127.0.0.1";
bool    joinIpEditing = false;
char    statusMsg[128] = "";

char    hostIps[8][64];
int     hostIpCount = 0;

NetMode netMode = NET_SOLO;

static const Color PLAYER_COLORS[NET_MAX_PLAYERS] = {
    {220, 80, 80, 255}, {80, 120, 220, 255}, {90, 200, 90, 255}, {220, 200, 80, 255},
};

void Menu_ToggleFullscreenSafe(void) {
    int monitor = GetCurrentMonitor();
    if (!IsWindowFullscreen()) {
        windowedW = GetScreenWidth(); windowedH = GetScreenHeight();
        SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
        ToggleFullscreen();
    } else {
        ToggleFullscreen();
        SetWindowSize(windowedW, windowedH);
    }
    fullscreen = IsWindowFullscreen();
}

void Menu_StartSoloGame(void) {
    netMode = NET_SOLO;
    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    Player_ResetForGame(0, playerName);
    PowerUps_ClearAll();
    Game_StartRound(1);
    gamePhase = GS_ROUND_BREAK;
    roundBreakTimer = 3.0f;
    uiState = UI_PLAY;
}

void Menu_StartHosting(void) {
    if (!Net_InitHost(NET_PORT_DEFAULT)) {
        snprintf(statusMsg, sizeof statusMsg, "Failed to start server on port %d", NET_PORT_DEFAULT);
        return;
    }
    netMode = NET_HOST;
    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    Player_ResetForGame(0, playerName);
    gamePhase = GS_PRE_GAME;
    uiState = UI_HOST_LOBBY;
    hostIpCount = Net_GetLocalIPs(hostIps, 8);
    snprintf(statusMsg, sizeof statusMsg, "Hosting on port %d", NET_PORT_DEFAULT);
}

void Menu_StartHostedGame(void) {
    PowerUps_ClearAll();
    Game_StartRound(1);
    gamePhase = GS_ROUND_BREAK;
    roundBreakTimer = 3.0f;
    PktStart s = { .type = PKT_START };
    Net_Broadcast(&s, sizeof s, true);
    uiState = UI_PLAY;
}

void Menu_StartConnecting(void) {
    if (!Net_InitClient(joinIp, NET_PORT_DEFAULT)) {
        snprintf(statusMsg, sizeof statusMsg, "Failed to start client");
        return;
    }
    netMode = NET_CLIENT;
    snprintf(statusMsg, sizeof statusMsg, "Connecting to %s...", joinIp);
    uiState = UI_CONNECTING;
}

void Menu_DrawMenu(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "CLAUDE  ZOMBIES";
    int ts = 72;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2 + 3, sh/6 + 3, ts, (Color){80, 0, 0, 255});
    DrawText(title, sw/2 - tw/2,     sh/6,     ts, (Color){220, 40, 40, 255});

    const char *tagline = "Hold the line. Buy weapons, perks and Pack-a-Punch.";
    int tagw = MeasureText(tagline, 20);
    DrawText(tagline, sw/2 - tagw/2, sh/6 + ts + 16, 20, (Color){200,200,200,255});

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 - 80;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by,        bw, bh}, "HOST GAME"))   Menu_StartHosting();
    if (GuiButton((Rectangle){bx, by + 64,   bw, bh}, "JOIN GAME"))   { uiState = UI_JOIN_INPUT; statusMsg[0]=0; }
    if (GuiButton((Rectangle){bx, by + 128,  bw, bh}, "SOLO PLAY"))   Menu_StartSoloGame();
    if (GuiButton((Rectangle){bx, by + 192,  bw, bh}, "SETTINGS"))    uiState = UI_SETTINGS;
    if (GuiButton((Rectangle){bx, by + 256,  bw, bh}, "QUIT"))        { CloseWindow(); exit(0); }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    if (statusMsg[0]) {
        int tw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - tw2/2, sh - 60, 18, (Color){200,180,180,255});
    }
}

void Menu_DrawSettings(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "SETTINGS";
    int ts = 56;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, 60, ts, RAYWHITE);

    int x = sw/2 - 200, y = 160, lh = 66;

    DrawText("Player Name", x, y + 6, 22, RAYWHITE);
    if (GuiTextBox((Rectangle){x + 280, y + 4, 240, 28}, playerName, sizeof playerName, nameEditing)) nameEditing = !nameEditing;

    y += lh;
    DrawText("Fullscreen", x, y + 6, 22, RAYWHITE);
    bool fs = fullscreen;
    GuiCheckBox((Rectangle){x + 280, y + 8, 24, 24}, NULL, &fs);
    if (fs != fullscreen) Menu_ToggleFullscreenSafe();

    y += lh;
    DrawText("Mouse Sensitivity", x, y + 6, 22, RAYWHITE);
    char sbuf[32]; snprintf(sbuf, sizeof sbuf, "%.4f", mouseSens);
    GuiSlider((Rectangle){x + 280, y + 8, 240, 24}, NULL, sbuf, &mouseSens, 0.0005f, 0.006f);

    y += lh;
    DrawText("Field of View", x, y + 6, 22, RAYWHITE);
    char fbuf[32]; snprintf(fbuf, sizeof fbuf, "%.0f", fovSetting);
    GuiSlider((Rectangle){x + 280, y + 8, 240, 24}, NULL, fbuf, &fovSetting, 60.0f, 110.0f);

    y += lh + 30;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){sw/2 - 130, y, 260, 50}, "BACK")) uiState = UI_MENU;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    DrawText("WASD move, mouse look, LMB shoot, R reload,",
             sw/2 - 280, sh - 100, 18, GRAY);
    DrawText("Q swap, F buy / use, hold E repair, ESC pause, F3 god mode",
             sw/2 - 280, sh - 76, 18, GRAY);
}

void Menu_DrawJoinInput(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "JOIN GAME";
    int ts = 56;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, 80, ts, RAYWHITE);

    int x = sw/2 - 200, y = 220;
    DrawText("Server IP", x, y, 22, RAYWHITE);
    if (GuiTextBox((Rectangle){x, y + 32, 400, 36}, joinIp, sizeof joinIp, joinIpEditing)) joinIpEditing = !joinIpEditing;
    DrawText("Player Name", x, y + 90, 22, RAYWHITE);
    if (GuiTextBox((Rectangle){x, y + 122, 400, 36}, playerName, sizeof playerName, nameEditing)) nameEditing = !nameEditing;

    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){sw/2 - 200, y + 200, 180, 48}, "CONNECT")) Menu_StartConnecting();
    if (GuiButton((Rectangle){sw/2 +  20, y + 200, 180, 48}, "BACK"))    uiState = UI_MENU;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    if (statusMsg[0]) {
        int tw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - tw2/2, sh - 60, 18, (Color){200,180,180,255});
    }
}

void Menu_DrawConnecting(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *t = "CONNECTING...";
    int ts = 48; int tw = MeasureText(t, ts);
    DrawText(t, sw/2 - tw/2, sh/2 - 30, ts, RAYWHITE);
    if (statusMsg[0]) {
        int sw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - sw2/2, sh/2 + 40, 18, GRAY);
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
    if (GuiButton((Rectangle){sw/2 - 100, sh - 100, 200, 44}, "CANCEL")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

void Menu_DrawLobby(int sw, int sh, bool isHost) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *t = isHost ? "HOST  LOBBY" : "WAITING FOR HOST";
    int ts = 48; int tw = MeasureText(t, ts);
    DrawText(t, sw/2 - tw/2, 50, ts, RAYWHITE);

    char info[128];
    snprintf(info, sizeof info, "Port %d  -  %d / %d players",
             NET_PORT_DEFAULT, Player_ActiveCount(), NET_MAX_PLAYERS);
    int iw = MeasureText(info, 20);
    DrawText(info, sw/2 - iw/2, 108, 20, GRAY);

    int listY = 280;
    if (isHost) {
        const char *hint = "Players join by entering one of these addresses:";
        int hw = MeasureText(hint, 18);
        DrawText(hint, sw/2 - hw/2, 140, 18, (Color){200,200,200,200});
        if (hostIpCount == 0) {
            const char *na = "(no network interface found - use 127.0.0.1 on this machine)";
            int nw = MeasureText(na, 18);
            DrawText(na, sw/2 - nw/2, 168, 18, GRAY);
        } else {
            int boxW = 380, boxH = 30 * hostIpCount + 16;
            int bx = sw/2 - boxW/2, by = 164;
            DrawRectangle(bx, by, boxW, boxH, (Color){25,30,40,255});
            DrawRectangleLines(bx, by, boxW, boxH, (Color){200,200,200,180});
            for (int i = 0; i < hostIpCount; i++) {
                char line[80];
                snprintf(line, sizeof line, "%s : %d", hostIps[i], NET_PORT_DEFAULT);
                int lw = MeasureText(line, 24);
                DrawText(line, sw/2 - lw/2, by + 8 + i*30, 24, YELLOW);
            }
            listY = by + boxH + 24;
        }
    }

    int rowH = 44;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        int ry = listY + i * rowH;
        DrawRectangle(sw/2 - 220, ry, 440, rowH - 6, (Color){25,30,40,255});
        DrawRectangle(sw/2 - 210, ry + 8, 20, 20, PLAYER_COLORS[i]);
        char line[64];
        if (players[i].active) snprintf(line, sizeof line, "%s", players[i].name[0] ? players[i].name : "Player");
        else                   snprintf(line, sizeof line, "(empty)");
        Color tc = players[i].active ? RAYWHITE : GRAY;
        DrawText(line, sw/2 - 180, ry + 8, 22, tc);
        if (i == localPlayerIdx) DrawText("(you)", sw/2 + 140, ry + 10, 18, YELLOW);
    }

    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (isHost) {
        if (GuiButton((Rectangle){sw/2 - 200, sh - 110, 180, 50}, "START GAME")) Menu_StartHostedGame();
    }
    if (GuiButton((Rectangle){sw/2 + 20, sh - 110, 180, 50}, "LEAVE")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

void Menu_DrawPause(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 170});
    const char *title = "PAUSED";
    int ts = 64; int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, sh/4, ts, RAYWHITE);
    if (netMode != NET_SOLO) {
        const char *sub = "(other players keep playing)";
        int sw2 = MeasureText(sub, 18);
        DrawText(sub, sw/2 - sw2/2, sh/4 + ts + 12, 18, GRAY);
    }

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 - 20;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by,        bw, bh}, "RESUME"))     uiState = UI_PLAY;
    if (GuiButton((Rectangle){bx, by + 64,   bw, bh}, "SETTINGS"))   uiState = UI_SETTINGS;
    if (GuiButton((Rectangle){bx, by + 128,  bw, bh}, "QUIT TO MENU")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

void Menu_DrawGameOver(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){120, 0, 0, 170});
    const char *msg = "GAME OVER";
    int fs = 96; int tw = MeasureText(msg, fs);
    DrawText(msg, sw/2 - tw/2, sh/3, fs, RAYWHITE);
    char rb[96]; snprintf(rb, sizeof rb, "Round %d reached", roundNum);
    int rw = MeasureText(rb, 28);
    DrawText(rb, sw/2 - rw/2, sh/3 + fs + 20, 28, RAYWHITE);

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 + 100;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by, bw, bh}, "MAIN MENU")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}
