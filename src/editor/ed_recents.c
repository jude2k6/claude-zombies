// ============================================================================
//  ed_recents.c — recent maps + recent games, persisted in editor.cfg.
//
//  Split out of builtins.c. Two small fixed-size MRU lists (recent.0..7 and
//  game.0..7) plus the maps start-directory resolver for file dialogs. The menu
//  bar (edmenus.c) drives these; the launcher records a game it opened through
//  EdBuiltins_RememberGame.
// ============================================================================

#include "builtins.h"
#include "builtins_internal.h"
#include "edscene.h"

#include "cfg.h"
#include "content.h"  // Eng_ContentDirs

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Derive the maps start directory for file dialogs.
// When a game root is active, ask the resolver for the first existing "maps"
// dir (game root wins over library).  Without a game root, fall back to the
// dev-tree "data/maps".
// ---------------------------------------------------------------------------
void MapsStartDir(char *buf, int cap) {
    char dirs[4][512];
    int n = Eng_ContentDirs("maps", dirs, 4);
    for (int i = 0; i < n; i++) {
        if (DirectoryExists(dirs[i])) {
            // %.*s bounds the field so gcc can prove no truncation (the 2D
            // dirs[] array otherwise reads as a 2047-byte object to -Wformat).
            snprintf(buf, cap, "%.*s", cap - 1, dirs[i]);
            return;
        }
    }
    snprintf(buf, cap, "games/shooter/maps");
}

// ============================================================================
//  Recent files — persisted in editor.cfg as recent.0 .. recent.7
// ============================================================================

#define ED_RECENT_MAX  8
#define ED_RECENT_LEN  512

static char g_recent[ED_RECENT_MAX][ED_RECENT_LEN];
static int  g_recentCount = 0;

// Forward declarations for the game-recents helpers defined later in this file.
static void GameRecents_Load(EngCfg *cfg);
static void GameRecents_WriteKeys(FILE *f);

// Load recents (maps + games) from the scene's cfg handle.
void Recents_Load(EngCfg *cfg) {
    g_recentCount = 0;
    for (int i = 0; i < ED_RECENT_MAX; i++) {
        char key[32]; snprintf(key, sizeof key, "recent.%d", i);
        const char *v = EngCfg_Str(cfg, key, "");
        if (!v || !v[0]) break;
        snprintf(g_recent[g_recentCount++], ED_RECENT_LEN, "%s", v);
    }
    GameRecents_Load(cfg);
}

// Save recents back via the cfg BeginSave/Put/EndSave path — called after any
// change so the list is always current on disk. We do a full cfg rewrite here
// (same pattern as EdScene_SaveSettings) to keep everything in one file.
static void Recents_Save(EdScene *s) {
    FILE *f = EngCfg_BeginSave(s->cfg, "Claude Zombies map editor settings");
    if (!f) return;
    // Scene-owned setting keys come from the single shared writer (no drift).
    EdScene_PutSettingKeys(s, f);
    // --- recents (owned here / by the launcher) ---
    for (int i = 0; i < g_recentCount; i++) {
        char key[32]; snprintf(key, sizeof key, "recent.%d", i);
        EngCfg_PutStr(f, key, g_recent[i]);
    }
    // --- recent games ---
    GameRecents_WriteKeys(f);
    EngCfg_EndSave(f);
}

// Push a path to the front of the recents list (dedup, cap ED_RECENT_MAX).
void Recents_Push(EdScene *s, const char *path) {
    if (!path || !path[0]) return;
    // Collect existing entries that differ from `path` into a temp buffer.
    int n = 0;
    int keep[ED_RECENT_MAX];
    for (int i = 0; i < g_recentCount; i++) {
        if (strcmp(g_recent[i], path) != 0) keep[n++] = i;
    }
    // Shift kept entries down by one slot (making room for `path` at index 0).
    for (int i = n - 1; i >= 0 && i + 1 < ED_RECENT_MAX; i--)
        memcpy(g_recent[i + 1], g_recent[keep[i]], ED_RECENT_LEN);
    // Write the new path at the front.
    strncpy(g_recent[0], path, ED_RECENT_LEN - 1);
    g_recent[0][ED_RECENT_LEN - 1] = '\0';
    g_recentCount = (n + 1 < ED_RECENT_MAX) ? n + 1 : ED_RECENT_MAX;
    Recents_Save(s);
}

// ============================================================================
//  Recent games — persisted in editor.cfg as game.0 .. game.7
// ============================================================================

#define ED_GAME_MAX  8
#define ED_GAME_LEN  512

static char g_games[ED_GAME_MAX][ED_GAME_LEN];
static int  g_gameCount = 0;

// Load the game recents list from the cfg handle.
static void GameRecents_Load(EngCfg *cfg) {
    g_gameCount = 0;
    for (int i = 0; i < ED_GAME_MAX; i++) {
        char key[32]; snprintf(key, sizeof key, "game.%d", i);
        const char *v = EngCfg_Str(cfg, key, "");
        if (!v || !v[0]) break;
        snprintf(g_games[g_gameCount++], ED_GAME_LEN, "%s", v);
    }
}

// Save the game recents alongside everything else (piggybacks on Recents_Save's
// full cfg rewrite; this helper is called from inside that path).
static void GameRecents_WriteKeys(FILE *f) {
    for (int i = 0; i < g_gameCount; i++) {
        char key[32]; snprintf(key, sizeof key, "game.%d", i);
        EngCfg_PutStr(f, key, g_games[i]);
    }
}

// Push a game dir to the front of the recent-games list (dedup, cap ED_GAME_MAX).
void GameRecents_Push(EdScene *s, const char *dir) {
    if (!dir || !dir[0]) return;
    int n = 0;
    int keep[ED_GAME_MAX];
    for (int i = 0; i < g_gameCount; i++) {
        if (strcmp(g_games[i], dir) != 0) keep[n++] = i;
    }
    for (int i = n - 1; i >= 0 && i + 1 < ED_GAME_MAX; i--)
        memcpy(g_games[i + 1], g_games[keep[i]], ED_GAME_LEN);
    strncpy(g_games[0], dir, ED_GAME_LEN - 1);
    g_games[0][ED_GAME_LEN - 1] = '\0';
    g_gameCount = (n + 1 < ED_GAME_MAX) ? n + 1 : ED_GAME_MAX;
    Recents_Save(s);  // full rewrite that also calls GameRecents_WriteKeys
}

// Public wrapper so the launcher (editor_main) can record a game it opened
// without reaching into builtins' static recents list. See builtins.h.
void EdBuiltins_RememberGame(EdHost *h, const char *dir) {
    GameRecents_Push(EdHost_Scene(h), dir);
}

// ---- read-only accessors for the menu's recent-entry actions/provider ------
int         Recents_Count(void)       { return g_recentCount; }
const char *Recents_Get(int i)        { return (i >= 0 && i < g_recentCount) ? g_recent[i] : ""; }
int         GameRecents_Count(void)   { return g_gameCount; }
const char *GameRecents_Get(int i)    { return (i >= 0 && i < g_gameCount) ? g_games[i] : ""; }
