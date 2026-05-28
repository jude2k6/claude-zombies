#ifndef SHOOTER_MENU_H
#define SHOOTER_MENU_H

#include "types.h"

// UI / settings state shared with main loop
extern UiState uiState;
extern UiState prevUi;

extern float   mouseSens;
extern float   fovSetting;
extern bool    fullscreen;
extern int     windowedW;
extern int     windowedH;

extern char    playerName[32];
extern bool    nameEditing;
extern char    joinIp[64];
extern bool    joinIpEditing;
extern char    statusMsg[128];

extern char    hostIps[8][64];
extern int     hostIpCount;

extern NetMode netMode;

#define MAP_LIST_MAX 16
typedef struct { char path[256]; char name[64]; } MapEntry;
extern MapEntry mapList[MAP_LIST_MAX];
extern int      mapListCount;
extern int      selectedMapIdx;

void Menu_ScanMaps(void);
void Menu_ToggleFullscreenSafe(void);

void Menu_StartSoloGame(void);
void Menu_StartHosting(void);
void Menu_StartHostedGame(void);
void Menu_StartConnecting(void);

void Menu_DrawMenu(int sw, int sh);
void Menu_DrawSettings(int sw, int sh);
void Menu_DrawJoinInput(int sw, int sh);
void Menu_DrawConnecting(int sw, int sh);
void Menu_DrawLobby(int sw, int sh, bool isHost);
void Menu_DrawPause(int sw, int sh);
void Menu_DrawGameOver(int sw, int sh);

#endif
