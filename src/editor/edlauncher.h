// ============================================================================
//  edlauncher.h — the editor's start / welcome screen (IDE-style).
//
//  The editor opens here when launched with no explicit map argument: a list of
//  recent GAMES to reopen, plus New Game (pick a preset → scaffold) and Open
//  Game / Open Map. Picking one resolves to an action the host acts on, then it
//  drops the user into the editor proper (edhost). Engine + raylib only — no
//  game header (the seam rule applies to the whole editor).
//
//  This is the "recent projects" welcome window every IDE has (JetBrains/VS),
//  and the front door for the games-as-projects model (docs/game-projects.md §8).
// ============================================================================
#ifndef SHOOTER_EDLAUNCHER_H
#define SHOOTER_EDLAUNCHER_H

#include "cfg.h"
#include <stdbool.h>

// What the launcher resolved to. EDL_NONE = still on the launcher this frame.
typedef enum {
    EDL_NONE = 0,
    EDL_OPEN_GAME,   // res.gameDir = an existing game folder to open
    EDL_NEW_GAME,    // res.gameDir + res.templateName + res.gameName to scaffold
    EDL_OPEN_MAP,    // res.path = a .map to open directly (no game switch)
    EDL_QUIT
} EdLauncherAction;

#define EDL_MAX_RECENT 8

typedef struct {
    EngCfg *cfg;        // editor.cfg — the recents source (read-only here)
    float   scale;      // UI scale (seeded from cfg / display)
    int     page;       // 0 = welcome, 1 = New Game template picker
    int     recentCount;
    char    recentDir [EDL_MAX_RECENT][512];   // from game.0..7
    char    recentName[EDL_MAX_RECENT][128];   // display name from each game.project
} EdLauncher;

typedef struct {
    EdLauncherAction action;
    char gameDir[512];
    char templateName[64];
    char gameName[128];
    char path[512];
} EdLauncherResult;

// Load the recent-games list from cfg and set the UI scale.
void EdLauncher_Init(EdLauncher *L, EngCfg *cfg, float scale);

// Draw one launcher frame; returns the chosen action (EDL_NONE until the user
// picks something). Call inside the editor's draw step, full-window.
EdLauncherResult EdLauncher_Draw(EdLauncher *L, int w, int h);

#endif // SHOOTER_EDLAUNCHER_H
