#include "menu.h"
#include "player.h"
#include "game.h"
#include "net.h"
#include "protocol.h"
#include "entities.h"
#include "level.h"
#include "pad.h"
#include "settings.h"
#include "raygui.h"
#include "eng_ui.h"   // house UI toolkit: theme + shadowed/centered text
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

MapEntry mapList[MAP_LIST_MAX];
int      mapListCount = 0;
int      selectedMapIdx = 0;

// ---- menu styling --------------------------------------------------------
// Theme + shadowed/centered text come from the engine UI toolkit (eng_ui):
// ENGUI_* colors, EngUi_TextShadow / EngUi_TextCentered, EngUi_ApplyTheme.
// Only the menu-specific full-screen gradient background stays local.
static void MenuBG(int sw, int sh) {
    DrawRectangleGradientV(0, 0, sw, sh, (Color){ 20, 24, 32, 255 }, (Color){ 9, 10, 15, 255 });
    DrawRectangleGradientV(0, 0, sw, sh / 4, (Color){ 0, 0, 0, 90 }, (Color){ 0, 0, 0, 0 });
    DrawRectangleGradientV(0, sh - sh / 4, sw, sh / 4, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 130 });
}

void Menu_ScanMaps(void) {
    mapListCount = 0;
    const char *dirs[] = { "data/maps", "../data/maps", "./data/maps" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0] && mapListCount == 0; i++) {
        if (!DirectoryExists(dirs[i])) continue;
        FilePathList list = LoadDirectoryFilesEx(dirs[i], ".map", false);
        for (unsigned int j = 0; j < list.count && mapListCount < MAP_LIST_MAX; j++) {
            strncpy(mapList[mapListCount].path, list.paths[j], sizeof mapList[0].path - 1);
            mapList[mapListCount].path[sizeof mapList[0].path - 1] = 0;
            const char *base = GetFileNameWithoutExt(list.paths[j]);
            strncpy(mapList[mapListCount].name, base ? base : "?", sizeof mapList[0].name - 1);
            mapList[mapListCount].name[sizeof mapList[0].name - 1] = 0;
            mapListCount++;
        }
        UnloadDirectoryFiles(list);
    }
    if (selectedMapIdx >= mapListCount) selectedMapIdx = 0;
    EngUi_ApplyTheme();
}

static void LoadSelectedMap(void) {
    if (mapListCount > 0 && selectedMapIdx >= 0 && selectedMapIdx < mapListCount) {
        if (Level_LoadFromFile(mapList[selectedMapIdx].path)) return;
    }
    Level_Build();
}

// Full vertical map-selection panel centred on cx, starting at top. Clickable
// rows (mouse) + Up/Down keys when canEdit. Returns the panel's bottom y.
static int DrawMapList(int cx, int top, bool canEdit) {
    int w = 440, rowH = 48, pad = 12, x = cx - w / 2;
    EngUi_TextShadow("SELECT MAP", x + 2, top, 18, ENGUI_GOLD);
    int py = top + 28;
    if (mapListCount == 0) {
        DrawRectangleRounded((Rectangle){ x, py, w, rowH + 2 * pad }, 0.08f, 8, ENGUI_PANEL);
        EngUi_TextCentered("(no maps found)", cx, py + pad + rowH / 2 - 10, 20, ENGUI_DIM);
        return py + rowH + 2 * pad;
    }
    int panelH = mapListCount * rowH + 2 * pad;
    DrawRectangleRounded((Rectangle){ x, py, w, panelH }, 0.04f, 8, ENGUI_PANEL);

    if (canEdit) {
        if (IsKeyPressed(KEY_DOWN)) selectedMapIdx = (selectedMapIdx + 1) % mapListCount;
        if (IsKeyPressed(KEY_UP))   selectedMapIdx = (selectedMapIdx - 1 + mapListCount) % mapListCount;
    }
    Vector2 m = GetMousePosition();
    bool click = canEdit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    for (int i = 0; i < mapListCount; i++) {
        Rectangle row = { x + pad, py + pad + i * rowH, w - 2 * pad, rowH - 6 };
        bool hover = canEdit && CheckCollisionPointRec(m, row);
        if (hover && click) selectedMapIdx = i;
        bool sel = (i == selectedMapIdx);
        Color rb = sel   ? (Color){ 54, 46, 24, 255 }
                 : hover ? (Color){ 34, 40, 52, 255 }
                         : (Color){ 26, 30, 40, 255 };
        DrawRectangleRounded(row, 0.25f, 6, rb);
        if (sel) DrawRectangleRounded((Rectangle){ row.x, row.y, 5, row.height }, 1.0f, 4, ENGUI_GOLD);
        EngUi_TextShadow(mapList[i].name, (int)row.x + 20, (int)(row.y + row.height / 2 - 11), 22,
                sel ? ENGUI_GOLD : ENGUI_TEXT);
        char no[16]; snprintf(no, sizeof no, "%d", i + 1);
        EngUi_TextShadow(no, (int)(row.x + row.width - 22), (int)(row.y + row.height / 2 - 9), 16, ENGUI_DIM);
    }
    return py + panelH;
}

// Compact one-line ‹ name › picker for space-constrained screens (host lobby).
static void DrawMapStrip(int cx, int y, bool canEdit) {
    int w = 300, ah = 38, x = cx - (w + 2 * ah + 16) / 2;
    if (mapListCount == 0) { EngUi_TextCentered("(no maps found)", cx, y + ah / 2 - 10, 20, ENGUI_DIM); return; }
    if (canEdit && GuiButton((Rectangle){ (float)x, (float)y, (float)ah, (float)ah }, "<"))
        selectedMapIdx = (selectedMapIdx - 1 + mapListCount) % mapListCount;
    DrawRectangleRounded((Rectangle){ (float)(x + ah + 8), (float)y, (float)w, (float)ah }, 0.3f, 8, ENGUI_PANEL);
    char label[128]; snprintf(label, sizeof label, "MAP:  %s", mapList[selectedMapIdx].name);
    EngUi_TextCentered(label, x + ah + 8 + w / 2, y + ah / 2 - 10, 20, ENGUI_GOLD);
    if (canEdit && GuiButton((Rectangle){ (float)(x + ah + 8 + w + 8), (float)y, (float)ah, (float)ah }, ">"))
        selectedMapIdx = (selectedMapIdx + 1) % mapListCount;
}

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
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    LoadSelectedMap();
    Level_Reset();
    Player_ResetForGame(0, playerName);
    PowerUps_ClearAll();
    Enemies_ClearAll();
    Bullets_ClearAll();
    Throwables_ClearAll();
    // Start in the round-break: GS_ROUND_BREAK with g_world.roundNum=0 rolls into
    // Game_StartRound(1) cleanly. Avoids the round-1 skip the prior code had.
    g_world.roundNum = 0;
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
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&g_world.players[i], 0, sizeof g_world.players[i]);
    Player_ResetForGame(0, playerName);
    gamePhase = GS_PRE_GAME;
    uiState = UI_HOST_LOBBY;
    hostIpCount = Net_GetLocalIPs(hostIps, 8);
    snprintf(statusMsg, sizeof statusMsg, "Hosting on port %d", NET_PORT_DEFAULT);
}

void Menu_StartHostedGame(void) {
    LoadSelectedMap();
    Level_Reset();
    PowerUps_ClearAll();
    Enemies_ClearAll();
    Bullets_ClearAll();
    Throwables_ClearAll();
    g_world.roundNum = 0;
    gamePhase = GS_ROUND_BREAK;
    roundBreakTimer = 3.0f;
    PktStart s = { .type = PKT_START };
    if (mapListCount > 0 && selectedMapIdx >= 0 && selectedMapIdx < mapListCount) {
        strncpy(s.mapName, mapList[selectedMapIdx].name, sizeof s.mapName - 1);
    }
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
    EngUi_ApplyTheme();
    MenuBG(sw, sh);

    const char *title = "CLAUDE  ZOMBIES";
    int ts = 78, ty = sh / 6, tw = MeasureText(title, ts);
    EngUi_TextCentered(title, sw / 2, ty, ts, ENGUI_RED);
    DrawRectangle(sw / 2 - tw / 2, ty + ts + 8, tw, 3, ENGUI_GOLD);
    EngUi_TextCentered("Round-based survival  -  guns, perks, Pack-a-Punch", sw / 2, ty + ts + 20, 20, ENGUI_DIM);

    int bw = 300, bh = 54, bx = sw / 2 - bw / 2, by = sh / 2 - 40, gap = 66;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 24);
    if (GuiButton((Rectangle){ (float)bx, (float)by,            (float)bw, (float)bh }, "SOLO PLAY"))   { uiState = UI_SOLO_LOBBY; statusMsg[0] = 0; }
    if (GuiButton((Rectangle){ (float)bx, (float)(by + gap),    (float)bw, (float)bh }, "MULTIPLAYER")) { uiState = UI_MP_MENU;    statusMsg[0] = 0; }
    if (GuiButton((Rectangle){ (float)bx, (float)(by + 2*gap),  (float)bw, (float)bh }, "SETTINGS"))    uiState = UI_SETTINGS;
    if (GuiButton((Rectangle){ (float)bx, (float)(by + 3*gap),  (float)bw, (float)bh }, "QUIT"))        { CloseWindow(); exit(0); }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    EngUi_TextCentered("Built with Claude Code", sw / 2, sh - 34, 16, (Color){ 92, 98, 112, 255 });
    if (statusMsg[0]) EngUi_TextCentered(statusMsg, sw / 2, sh - 62, 18, (Color){ 214, 150, 150, 255 });
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
    if (GuiButton((Rectangle){sw/2 - 270, y, 260, 50}, "CONTROLLER BINDINGS")) uiState = UI_BINDINGS;
    if (GuiButton((Rectangle){sw/2 +  10, y, 260, 50}, "BACK")) { Settings_Save(); uiState = UI_MENU; }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    DrawText("WASD move, mouse look, LMB shoot, R reload,",
             sw/2 - 280, sh - 100, 18, GRAY);
    DrawText("Q swap, F buy / use, hold E repair, ESC pause, F3 god mode, F4 noclip",
             sw/2 - 280, sh - 76, 18, GRAY);
}

// State for the bindings screen: which action is being captured (-1 = none).
static int bindingCaptureFor = -1;

bool Menu_BindingsCaptureActive(void) { return bindingCaptureFor >= 0; }

void Menu_DrawBindings(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "CONTROLLER  BINDINGS";
    int ts = 44;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, 40, ts, RAYWHITE);

    const char *sub = Pad_Connected()
        ? "Click an action and press a controller button to rebind."
        : "(no controller detected - plug one in to rebind)";
    int sbw = MeasureText(sub, 18);
    DrawText(sub, sw/2 - sbw/2, 92, 18, (Color){200,200,200,220});

    int rows = BA_COUNT;
    int colW = 380;
    int rowH = 36;
    int x = sw/2 - colW/2;
    int y = 130;

    if (bindingCaptureFor >= 0) {
        int got = Bind_PollAny();
        if (got != BIND_NONE) {
            bindButton[bindingCaptureFor] = got;
            bindingCaptureFor = -1;
            Settings_Save();
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            // ESC cancels capture without rebinding.
            bindingCaptureFor = -1;
        }
    }

    for (int i = 0; i < rows; i++) {
        int ry = y + i * rowH;
        DrawRectangle(x, ry, colW, rowH - 4, (Color){25,30,40,255});
        DrawText(Bind_ActionName((BindAction)i), x + 14, ry + 8, 18, RAYWHITE);

        char btnLabel[48];
        if (bindingCaptureFor == i) snprintf(btnLabel, sizeof btnLabel, "press a button...");
        else snprintf(btnLabel, sizeof btnLabel, "%s", Bind_ButtonLabel(bindButton[i]));

        Rectangle btnRc = { x + colW - 180, ry + 2, 160, rowH - 8 };
        if (GuiButton(btnRc, btnLabel)) {
            bindingCaptureFor = (bindingCaptureFor == i) ? -1 : i;
        }
    }

    int by = y + rows * rowH + 20;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
    if (GuiButton((Rectangle){sw/2 - 280, by, 180, 44}, "RESET DEFAULTS")) {
        bindButton[BA_FIRE]     = BIND_TRIG_R;
        bindButton[BA_ADS]      = BIND_TRIG_L;
        bindButton[BA_RELOAD]   = PAD_Y;
        bindButton[BA_INTERACT] = PAD_X;
        bindButton[BA_MELEE]    = PAD_RB;
        bindButton[BA_SWAP]     = PAD_LB;
        bindButton[BA_SLOT1]    = PAD_DP_LEFT;
        bindButton[BA_SLOT2]    = PAD_DP_RIGHT;
        bindButton[BA_JUMP]     = PAD_A;
        bindButton[BA_CROUCH]   = PAD_B;
        bindButton[BA_SPRINT]   = PAD_L3;
        bindButton[BA_PAUSE]    = PAD_START;
        bindButton[BA_SCORE]    = PAD_BACK;
        bindButton[BA_NOCLIP]   = PAD_R3;
        bindButton[BA_THROW_LETHAL]   = PAD_DP_UP;
        bindButton[BA_THROW_TACTICAL] = PAD_DP_DOWN;
        bindingCaptureFor = -1;
        Settings_Save();
    }
    if (GuiButton((Rectangle){sw/2 -  90, by, 180, 44}, "CLEAR")) {
        if (bindingCaptureFor >= 0) {
            bindButton[bindingCaptureFor] = BIND_NONE;
            bindingCaptureFor = -1;
            Settings_Save();
        }
    }
    if (GuiButton((Rectangle){sw/2 + 100, by, 180, 44}, "BACK")) {
        bindingCaptureFor = -1;
        Settings_Save();
        uiState = UI_SETTINGS;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
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

void Menu_DrawMultiplayer(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *t = "MULTIPLAYER";
    int ts = 56; int tw = MeasureText(t, ts);
    DrawText(t, sw/2 - tw/2, 60, ts, RAYWHITE);

    const char *sub = "Host your own server, or join a friend.";
    int sbw = MeasureText(sub, 20);
    DrawText(sub, sw/2 - sbw/2, 60 + ts + 12, 20, (Color){200,200,200,255});

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 - 40;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by,       bw, bh}, "HOST GAME")) Menu_StartHosting();
    if (GuiButton((Rectangle){bx, by + 64,  bw, bh}, "JOIN GAME")) { uiState = UI_JOIN_INPUT; statusMsg[0]=0; }
    if (GuiButton((Rectangle){bx, by + 128, bw, bh}, "BACK"))      uiState = UI_MENU;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    if (statusMsg[0]) {
        int tw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - tw2/2, sh - 60, 18, (Color){200,180,180,255});
    }
}

void Menu_DrawSoloLobby(int sw, int sh) {
    EngUi_ApplyTheme();
    MenuBG(sw, sh);
    EngUi_TextCentered("SOLO", sw / 2, 64, 56, ENGUI_TEXT);
    EngUi_TextCentered("Pick a map, then start your run.", sw / 2, 134, 20, ENGUI_DIM);

    DrawMapList(sw / 2, 196, true);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 24);
    if (GuiButton((Rectangle){ (float)(sw/2 - 210), (float)(sh - 110), 190, 52 }, "START")) Menu_StartSoloGame();
    if (GuiButton((Rectangle){ (float)(sw/2 + 20),  (float)(sh - 110), 190, 52 }, "BACK"))  uiState = UI_MENU;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

void Menu_DrawLobby(int sw, int sh, bool isHost) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *t = isHost ? "HOST  LOBBY" : "WAITING FOR HOST";
    int ts = 48; int tw = MeasureText(t, ts);
    DrawText(t, sw/2 - tw/2, 50, ts, RAYWHITE);

    char info[128];
    snprintf(info, sizeof info, "Port %d  -  %d / %d g_world.players",
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
        if (g_world.players[i].active) snprintf(line, sizeof line, "%s", g_world.players[i].name[0] ? g_world.players[i].name : "Player");
        else                   snprintf(line, sizeof line, "(empty)");
        Color tc = g_world.players[i].active ? RAYWHITE : GRAY;
        DrawText(line, sw/2 - 180, ry + 8, 22, tc);
        if (i == localPlayerIdx) DrawText("(you)", sw/2 + 140, ry + 10, 18, YELLOW);
    }

    // Map picker sits between the player list and the action buttons. Only
    // the host can change it; clients see the host's selection at start time
    // via PktStart.
    int pickerY = listY + 4 * rowH + 18;
    if (isHost) {
        EngUi_TextCentered("MAP", sw/2, pickerY, 18, ENGUI_GOLD);
        DrawMapStrip(sw/2, pickerY + 22, true);
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
        const char *sub = "(other g_world.players keep playing)";
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
    // Dim the world behind, slight red tint.
    DrawRectangle(0, 0, sw, sh, (Color){10, 0, 0, 180});

    const char *msg = "GAME OVER";
    int fs = 76; int tw = MeasureText(msg, fs);
    DrawText(msg, sw/2 - tw/2 + 3, 40 + 3, fs, (Color){80, 0, 0, 255});
    DrawText(msg, sw/2 - tw/2,     40,     fs, (Color){230, 60, 60, 255});

    // Big "Round X reached" centerpiece.
    char rb[64]; snprintf(rb, sizeof rb, "REACHED  ROUND  %d", g_world.roundNum);
    int rfs = 36;
    int rw = MeasureText(rb, rfs);
    DrawText(rb, sw/2 - rw/2, 130, rfs, (Color){220, 220, 220, 240});

    // Find MVP (highest points).
    int mvpIdx = -1, mvpPts = -1;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (g_world.players[i].active && g_world.players[i].points > mvpPts) { mvpPts = g_world.players[i].points; mvpIdx = i; }

    // Stats table
    int colW = 110;
    int totalW = 220 + colW * 6;
    int x0 = sw/2 - totalW/2;
    int y0 = 210;

    DrawRectangle(x0 - 20, y0 - 14, totalW + 40, 24, (Color){0,0,0,180});
    DrawText("PLAYER",     x0,                       y0, 18, (Color){220,220,220,255});
    DrawText("POINTS",     x0 + 220,                 y0, 18, YELLOW);
    DrawText("KILLS",      x0 + 220 + colW,          y0, 18, RAYWHITE);
    DrawText("HEADSHOTS",  x0 + 220 + colW*2,        y0, 18, RAYWHITE);
    DrawText("MELEE",      x0 + 220 + colW*3,        y0, 18, RAYWHITE);
    DrawText("ACC %",      x0 + 220 + colW*4,        y0, 18, RAYWHITE);
    DrawText("REVIVES",    x0 + 220 + colW*5,        y0, 18, RAYWHITE);
    DrawLine(x0, y0 + 24, x0 + totalW, y0 + 24, (Color){200,200,200,180});

    int teamKills = 0, teamHeads = 0, teamMelee = 0, teamRevives = 0;
    int teamPoints = 0, teamFired = 0, teamHit = 0;
    int row = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g_world.players[i].active) continue;
        Player *p = &g_world.players[i];
        int yr = y0 + 36 + row * 28;
        if (i == mvpIdx) DrawRectangle(x0 - 20, yr - 4, totalW + 40, 26, (Color){80, 60, 0, 120});

        const char *nameStr = p->name[0] ? p->name : "Player";
        if (i == mvpIdx) {
            DrawText("MVP", x0 - 64, yr, 18, (Color){240, 220, 60, 255});
        }
        DrawText(nameStr, x0, yr, 18, RAYWHITE);

        char buf[32];
        snprintf(buf, sizeof buf, "%d",  p->points);     DrawText(buf, x0 + 220,           yr, 18, YELLOW);
        snprintf(buf, sizeof buf, "%d",  p->kills);      DrawText(buf, x0 + 220 + colW,    yr, 18, RAYWHITE);
        snprintf(buf, sizeof buf, "%d",  p->headshots);  DrawText(buf, x0 + 220 + colW*2,  yr, 18, RAYWHITE);
        snprintf(buf, sizeof buf, "%d",  p->meleeKills); DrawText(buf, x0 + 220 + colW*3,  yr, 18, RAYWHITE);
        float acc = (p->shotsFired > 0) ? 100.0f * p->shotsHit / p->shotsFired : 0.0f;
        snprintf(buf, sizeof buf, "%.1f", acc);          DrawText(buf, x0 + 220 + colW*4,  yr, 18, RAYWHITE);
        snprintf(buf, sizeof buf, "%d",  p->revives);    DrawText(buf, x0 + 220 + colW*5,  yr, 18, RAYWHITE);
        teamKills += p->kills;       teamHeads  += p->headshots;
        teamMelee += p->meleeKills;  teamRevives += p->revives;
        teamPoints += p->points;     teamFired  += p->shotsFired; teamHit += p->shotsHit;
        row++;
    }

    if (row > 1) {
        // Team totals line.
        int yr = y0 + 36 + row * 28 + 6;
        DrawLine(x0, yr - 6, x0 + totalW, yr - 6, (Color){200,200,200,140});
        DrawText("TEAM", x0, yr, 18, (Color){200,200,200,200});
        char buf[32];
        snprintf(buf, sizeof buf, "%d",  teamPoints);  DrawText(buf, x0 + 220,           yr, 18, YELLOW);
        snprintf(buf, sizeof buf, "%d",  teamKills);   DrawText(buf, x0 + 220 + colW,    yr, 18, (Color){200,200,200,200});
        snprintf(buf, sizeof buf, "%d",  teamHeads);   DrawText(buf, x0 + 220 + colW*2,  yr, 18, (Color){200,200,200,200});
        snprintf(buf, sizeof buf, "%d",  teamMelee);   DrawText(buf, x0 + 220 + colW*3,  yr, 18, (Color){200,200,200,200});
        float teamAcc = (teamFired > 0) ? 100.0f * teamHit / teamFired : 0.0f;
        snprintf(buf, sizeof buf, "%.1f", teamAcc);    DrawText(buf, x0 + 220 + colW*4,  yr, 18, (Color){200,200,200,200});
        snprintf(buf, sizeof buf, "%d",  teamRevives); DrawText(buf, x0 + 220 + colW*5,  yr, 18, (Color){200,200,200,200});
    }

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh - 120;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by, bw, bh}, "MAIN MENU")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}
