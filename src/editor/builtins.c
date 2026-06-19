// ============================================================================
//  builtins.c — the editor's first-party plugins (see builtins.h).
//
//  Four plugins contribute the whole default IDE: the menu bar, the docked
//  panels (Tools palette / Hierarchy / Inspector / Console), the Tools-menu map
//  utilities (Settings dialog + Validate), and the status bar. They are written
//  exactly as a third-party plugin would be, against the EdHost_* API — the
//  proof that the seam is real.
// ============================================================================

#include "builtins.h"
#include "edscene.h"
#include "edfiledialog.h"
#include "edproject.h"  // EdProject_Open, EdProject_New, EdProject_Read
#include "edthumb.h"    // EdThumb_Model — asset-browser model previews

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"
#include "cfg.h"
#include "app.h"      // Eng_RequestClose
#include "content.h"  // Eng_ContentDirs, Eng_GetGameRoot

#include <stdio.h>
#include <stdlib.h>   // atof, for inspector number fields
#include <string.h>
#include <stdint.h>
#include <ctype.h>    // tolower, for case-insensitive filter

// ---------------------------------------------------------------------------
// Derive the maps start directory for file dialogs.
// When a game root is active, ask the resolver for the first existing "maps"
// dir (game root wins over library).  Without a game root, fall back to the
// dev-tree "data/maps".
// ---------------------------------------------------------------------------
static void MapsStartDir(char *buf, int cap) {
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
static void Recents_Load(EngCfg *cfg) {
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
static void Recents_Push(EdScene *s, const char *path) {
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
static void GameRecents_Push(EdScene *s, const char *dir) {
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

// ============================================================================
//  Unsaved-changes guard — Feature 4
// ============================================================================

static enum { PEND_NONE = 0, PEND_NEW, PEND_OPEN, PEND_EXIT, PEND_OPENPATH } g_pending = PEND_NONE;
static char g_pendingPath[512];   // target for PEND_OPENPATH (asset-browser open)

// Forward-declare the do_* helpers (bodies below, after a_save is declared).
static void do_new (EdHost *h);
static void do_open(EdHost *h);
static void do_exit(EdHost *h);
static void do_openpath(EdHost *h, const char *path);
static void DirtyGuardModal(EdHost *h, Rectangle area, void *u);

static void RunPending(EdHost *h) {
    int p = g_pending;
    g_pending = PEND_NONE;
    if      (p == PEND_NEW)      do_new(h);
    else if (p == PEND_OPEN)     do_open(h);
    else if (p == PEND_EXIT)     do_exit(h);
    else if (p == PEND_OPENPATH) do_openpath(h, g_pendingPath);
}

// Open a specific map by path, gated by the unsaved-changes guard — the shared
// entry point the asset browser uses when a map row is clicked.
static void RequestOpenMap(EdHost *h, const char *path) {
    if (EdHost_Scene(h)->dirty) {
        snprintf(g_pendingPath, sizeof g_pendingPath, "%s", path);
        g_pending = PEND_OPENPATH;
        EdHost_SetModal(h, DirtyGuardModal, NULL);
    } else {
        do_openpath(h, path);
    }
}

static void DirtyGuardModal(EdHost *h, Rectangle area, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);
    float pw = 340 * sc, ph = 140 * sc;
    float px = (area.width  - pw) / 2.0f;
    float py = (area.height - ph) / 2.0f; if (py < 6 * sc) py = 6 * sc;
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });

    float X = px + 16 * sc, W = pw - 32 * sc, y = py + 16 * sc;
    Eng_UiText("UNSAVED CHANGES", X, y, 16, ENG_UI_GOLD); y += 28 * sc;
    Eng_UiText("You have unsaved changes.", X, y, 13, ENG_UI_TEXT); y += 32 * sc;

    float bw = (W - 16 * sc) / 3.0f;
    // Save → save then run pending
    if (GuiButton((Rectangle){ X, y, bw, 24 * sc }, "Save")) {
        // Call a_save logic inline: we need access to it, call via the existing
        // action function pointer — just replicate the EdScene_Save call chain.
        EdScene *s = EdHost_Scene(h);
        bool saved = false;
        if (strcmp(GetFileName(s->path), "untitled.map") == 0) {
            char startDir[512]; MapsStartDir(startDir, sizeof startDir);
            char chosen[512];
            if (EdFileDialog_Save(chosen, sizeof chosen, startDir, "untitled.map")) {
                if (EdScene_SaveAs(s, chosen)) {
                    Recents_Push(s, chosen);
                    EdHost_Log(h, ED_LOG_INFO, "saved %s", chosen);
                    saved = true;
                } else {
                    EdHost_Log(h, ED_LOG_ERROR, "save FAILED: %s", chosen);
                }
            }
        } else {
            if (EdScene_Save(s)) {
                Recents_Push(s, s->path);
                EdHost_Log(h, ED_LOG_INFO, "saved %s", s->path);
                saved = true;
            } else {
                EdHost_Log(h, ED_LOG_ERROR, "save FAILED: %s", s->path);
            }
        }
        if (saved) {
            EdHost_SetModal(h, NULL, NULL);
            RunPending(h);
        }
    }
    // Discard → discard edits, run pending
    if (GuiButton((Rectangle){ X + bw + 8 * sc, y, bw, 24 * sc }, "Discard")) {
        EdHost_SetModal(h, NULL, NULL);
        RunPending(h);
    }
    // Cancel → do nothing
    if (GuiButton((Rectangle){ X + (bw + 8 * sc) * 2, y, bw, 24 * sc }, "Cancel")) {
        g_pending = PEND_NONE;
        EdHost_SetModal(h, NULL, NULL);
    }
}

// ---- crash recovery prompt -------------------------------------------------
// Shown once when a map loads with a newer "<path>.autosave" than the map (the
// last session crashed mid-edit). Restore loads the autosave; Discard deletes
// it. EdBuiltins_RecoveryGuard (called once per frame by editor_main) detects a
// path change and raises this when recovery is available and no modal is up.

static void RecoveryModal(EdHost *h, Rectangle area, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);
    float pw = 360 * sc, ph = 150 * sc;
    float px = (area.width  - pw) / 2.0f;
    float py = (area.height - ph) / 2.0f; if (py < 6 * sc) py = 6 * sc;
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 80, 66, 60, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });

    float X = px + 16 * sc, W = pw - 32 * sc, y = py + 16 * sc;
    Eng_UiText("RECOVER UNSAVED WORK", X, y, 16, ENG_UI_GOLD); y += 26 * sc;
    Eng_UiText("A newer autosave was found for this map —", X, y, 12, ENG_UI_TEXT); y += 16 * sc;
    Eng_UiText("the last session likely closed without saving.", X, y, 12, ENG_UI_TEXT); y += 30 * sc;

    EdScene *s = EdHost_Scene(h);
    float bw = (W - 8 * sc) / 2.0f;
    if (GuiButton((Rectangle){ X, y, bw, 26 * sc }, "Restore")) {
        if (EdScene_RestoreRecovery(s)) EdHost_Log(h, ED_LOG_INFO, "restored autosave for %s", s->path);
        else                            EdHost_Log(h, ED_LOG_ERROR, "autosave restore FAILED (kept loaded map)");
        EdHost_SetModal(h, NULL, NULL);
    }
    if (GuiButton((Rectangle){ X + bw + 8 * sc, y, bw, 26 * sc }, "Discard")) {
        EdScene_DiscardRecovery(s);
        EdHost_Log(h, ED_LOG_INFO, "discarded autosave for %s", s->path);
        EdHost_SetModal(h, NULL, NULL);
    }
}

void EdBuiltins_RecoveryGuard(EdHost *h) {
    static char checked[512] = "\x01";   // sentinel that never equals a real path
    EdScene *s = EdHost_Scene(h);
    if (strcmp(checked, s->path) == 0) return;   // already evaluated this map
    snprintf(checked, sizeof checked, "%s", s->path);
    if (!EdHost_HasModal(h) && EdScene_RecoveryAvailable(s))
        EdHost_SetModal(h, RecoveryModal, NULL);
}

// ============================================================================
//  Menus: File / Edit / View / Help   (Tools is owned by EdBuiltin_MapTools)
// ============================================================================

static bool q_can_undo(EdHost *h, void *u) { (void)u; return EngMapHistory_CanUndo(&EdHost_Scene(h)->hist); }
static bool q_can_redo(EdHost *h, void *u) { (void)u; return EngMapHistory_CanRedo(&EdHost_Scene(h)->hist); }
static bool q_has_sel (EdHost *h, void *u) { (void)u; return EdHost_SelectedId(h) >= 0; }
static bool q_has_clip(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->clipCount > 0; }
static bool q_dirty   (EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->dirty; }

static bool q_view_fly(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->view == ED_VIEW_FLY; }
static bool q_view_iso(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->view == ED_VIEW_ORBIT; }
static bool q_view_top(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->view == ED_VIEW_TOP; }
static bool q_snap    (EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->snapEnabled; }
static bool q_grid    (EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->gridVisible; }
static bool q_giz_move (EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->mode == ENG_GIZMO_TRANSLATE; }
static bool q_giz_rot  (EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->mode == ENG_GIZMO_ROTATE; }
static bool q_giz_scale(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->mode == ENG_GIZMO_SCALE; }

// ---- do_* helpers (real work, no dirty check) --------------------------------

static void do_new(EdHost *h) {
    EdScene *s = EdHost_Scene(h);
    EdScene_New(s);
    EdHost_Log(h, ED_LOG_INFO, "new map — untitled.map");
}

static void do_open(EdHost *h) {
    EdScene *s = EdHost_Scene(h);
    char startDir[512]; snprintf(startDir, sizeof startDir, "%s", s->path);
    char *slash = strrchr(startDir, '/');
    if (slash) *slash = '\0'; else MapsStartDir(startDir, sizeof startDir);

    char chosen[512];
    if (!EdFileDialog_Open(chosen, sizeof chosen, startDir)) return;
    if (EdScene_Open(s, chosen)) {
        Recents_Push(s, chosen);
        EdHost_Log(h, ED_LOG_INFO, "opened %s", chosen);
    } else {
        EdHost_Log(h, ED_LOG_ERROR, "open FAILED: %s", chosen);
    }
}

static void do_openpath(EdHost *h, const char *path) {
    EdScene *s = EdHost_Scene(h);
    if (EdScene_Open(s, path)) {
        Recents_Push(s, path);
        EdHost_Log(h, ED_LOG_INFO, "opened %s", path);
    } else {
        EdHost_Log(h, ED_LOG_ERROR, "open FAILED: %s", path);
    }
}

static void do_exit(EdHost *h) {
    (void)h;
    Eng_RequestClose();
}

// ---- File menu actions -------------------------------------------------------

static void a_new(EdHost *h, void *u) {
    (void)u;
    if (EdHost_Scene(h)->dirty) { g_pending = PEND_NEW;  EdHost_SetModal(h, DirtyGuardModal, NULL); }
    else                          do_new(h);
}

static void a_open(EdHost *h, void *u) {
    (void)u;
    if (EdHost_Scene(h)->dirty) { g_pending = PEND_OPEN; EdHost_SetModal(h, DirtyGuardModal, NULL); }
    else                          do_open(h);
}

static void a_save(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    // If the file has never been saved (untitled), behave like Save As.
    if (strcmp(GetFileName(s->path), "untitled.map") == 0) {
        char startDir[512]; MapsStartDir(startDir, sizeof startDir);
        char chosen[512];
        if (!EdFileDialog_Save(chosen, sizeof chosen, startDir, "untitled.map")) return;
        if (EdScene_SaveAs(s, chosen)) {
            Recents_Push(s, chosen);
            EdHost_Log(h, ED_LOG_INFO, "saved %s", chosen);
        } else {
            EdHost_Log(h, ED_LOG_ERROR, "save FAILED: %s", chosen);
        }
        return;
    }
    if (EdScene_Save(s)) {
        Recents_Push(s, s->path);
        EdHost_Log(h, ED_LOG_INFO, "saved %s", s->path);
    } else {
        EdHost_Log(h, ED_LOG_ERROR, "save FAILED: %s", s->path);
    }
}

static void a_save_as(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    char startDir[512]; snprintf(startDir, sizeof startDir, "%s", s->path);
    char *slash = strrchr(startDir, '/');
    if (slash) *slash = '\0'; else MapsStartDir(startDir, sizeof startDir);
    const char *defName = GetFileName(s->path);

    char chosen[512];
    if (!EdFileDialog_Save(chosen, sizeof chosen, startDir, defName)) return;
    if (EdScene_SaveAs(s, chosen)) {
        Recents_Push(s, chosen);
        EdHost_Log(h, ED_LOG_INFO, "saved as %s", chosen);
    } else {
        EdHost_Log(h, ED_LOG_ERROR, "save as FAILED: %s", chosen);
    }
}

static void a_reload(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    char path[512]; snprintf(path, sizeof path, "%s", s->path);
    if (EdScene_Open(s, path)) EdHost_Log(h, ED_LOG_INFO, "reloaded %s (edits discarded)", path);
    else                       EdHost_Log(h, ED_LOG_ERROR, "reload FAILED: %s", path);
}

static void a_exit(EdHost *h, void *u) {
    (void)u;
    if (EdHost_Scene(h)->dirty) { g_pending = PEND_EXIT; EdHost_SetModal(h, DirtyGuardModal, NULL); }
    else                          do_exit(h);
}

// ---- Dynamic recents provider -----------------------------------------------

// onClick for a recent-file entry: user = (void*)(intptr_t)index into g_recent.
static void a_open_recent(EdHost *h, void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= g_recentCount) return;
    EdScene *s = EdHost_Scene(h);
    char path[ED_RECENT_LEN]; snprintf(path, sizeof path, "%s", g_recent[idx]);
    if (EdScene_Open(s, path)) {
        Recents_Push(s, path);   // bump to front
        EdHost_Log(h, ED_LOG_INFO, "opened %s", path);
    } else {
        EdHost_Log(h, ED_LOG_ERROR, "open FAILED: %s", path);
    }
}

// Labels need persistent storage — we write them into a static ring each frame.
static char g_recentLabels[ED_RECENT_MAX][256];
// Persistent label storage for game recent entries (filled by RecentsDynProvider).
static char g_gameLabels[ED_GAME_MAX][256];

// Forward declarations for the game-menu actions defined below.
static void a_open_recent_game(EdHost *h, void *u);
static void a_open_game(EdHost *h, void *u);
static void a_new_game(EdHost *h, void *u);

// Combined dynamic provider: emits map-recents, then (when game recents exist) a
// separator + game recents.  Both groups share the single File-menu dyn slot.
// Entries beyond max are silently truncated; that is safe given typical list sizes.
static int RecentsDynProvider(EdHost *h, EdMenuItem *out, int max, void *user) {
    (void)h; (void)user;
    int pos = 0;

    // --- map recents ---
    int nm = g_recentCount < max ? g_recentCount : max;
    for (int i = 0; i < nm && pos < max; i++, pos++) {
        snprintf(g_recentLabels[i], sizeof g_recentLabels[i], "%s", GetFileName(g_recent[i]));
        out[pos] = (EdMenuItem){
            .menu    = "File",
            .label   = g_recentLabels[i],
            .onClick = a_open_recent,
            .user    = (void*)(intptr_t)i,
        };
    }

    // --- game recents (separator + entries) ---
    if (g_gameCount > 0 && pos < max) {
        out[pos++] = (EdMenuItem){ .menu = "File", .label = NULL };  // separator
    }
    for (int i = 0; i < g_gameCount && pos < max; i++, pos++) {
        snprintf(g_gameLabels[i], sizeof g_gameLabels[i], "[game] %s", GetFileName(g_games[i]));
        out[pos] = (EdMenuItem){
            .menu    = "File",
            .label   = g_gameLabels[i],
            .onClick = a_open_recent_game,
            .user    = (void*)(intptr_t)i,
        };
    }

    return pos;
}

// ---- Open Game / New Game actions ------------------------------------------

// Open an existing game folder, then load its default map.
static void a_open_game(EdHost *h, void *u) {
    (void)u;
    char startDir[512]; EdProject_DefaultGamesDir(startDir, sizeof startDir);

    char chosen[512];
    if (!EdFileDialog_SelectFolder(chosen, sizeof chosen, "Open Game folder", startDir)) return;

    if (!EdProject_Open(chosen)) {
        EdHost_Log(h, ED_LOG_ERROR, "open game FAILED: no valid game.project in %s", chosen);
        return;
    }

    // Resolve and load the game's default map (overlay now points at the new game root).
    char mapBuf[1024];   // gameDir (512) + '/' + default_map (256) — no truncation
    EdProject proj;
    const char *mapRel = "maps/default.map";  // safe fallback
    if (EdProject_Read(chosen, &proj) && proj.default_map[0]) mapRel = proj.default_map;

    // Resolve via the game root directly (Eng_SetGameRoot was just called above).
    snprintf(mapBuf, sizeof mapBuf, "%s/%s", chosen, mapRel);

    EdScene *s = EdHost_Scene(h);
    EdScene_RescanContent(s);   // new game root → refresh palette + asset browser
    if (FileExists(mapBuf)) {
        if (EdScene_Open(s, mapBuf)) {
            Recents_Push(s, mapBuf);
            EdHost_Log(h, ED_LOG_INFO, "opened game '%s' — map: %s", proj.name[0] ? proj.name : chosen, mapBuf);
        } else {
            EdHost_Log(h, ED_LOG_ERROR, "game opened but map load FAILED: %s", mapBuf);
        }
    } else {
        EdHost_Log(h, ED_LOG_INFO, "opened game '%s' (no default map found at %s)", proj.name[0] ? proj.name : chosen, mapBuf);
    }

    GameRecents_Push(s, chosen);
}

// Create a new game folder, scaffold it, then open it.
static void a_new_game(EdHost *h, void *u) {
    (void)u;
    char startDir[512]; EdProject_DefaultGamesDir(startDir, sizeof startDir);

    char chosen[512];
    if (!EdFileDialog_SelectFolder(chosen, sizeof chosen, "New Game — select (or create) game folder", startDir)) return;

    // Use the folder's basename as the display name; the user can edit game.project later.
    const char *baseName = GetFileName(chosen);
    if (!baseName || !baseName[0]) baseName = "mygame";

    if (!EdProject_New(chosen, "empty", baseName)) {
        EdHost_Log(h, ED_LOG_ERROR, "new game FAILED: could not scaffold '%s'", chosen);
        return;
    }

    if (!EdProject_Open(chosen)) {
        EdHost_Log(h, ED_LOG_ERROR, "new game FAILED: could not open just-scaffolded '%s'", chosen);
        return;
    }

    EdScene *s = EdHost_Scene(h);
    EdScene_RescanContent(s);   // new game root → refresh palette + asset browser

    // Load the freshly seeded default map.
    char mapBuf[1024];   // gameDir (512) + '/' + default_map (256) — no truncation
    snprintf(mapBuf, sizeof mapBuf, "%s/maps/default.map", chosen);
    if (FileExists(mapBuf)) {
        if (EdScene_Open(s, mapBuf)) {
            Recents_Push(s, mapBuf);
            EdHost_Log(h, ED_LOG_INFO, "new game '%s' — scaffolded and opened", baseName);
        } else {
            EdHost_Log(h, ED_LOG_ERROR, "game scaffolded but map load FAILED: %s", mapBuf);
        }
    } else {
        EdHost_Log(h, ED_LOG_INFO, "new game '%s' scaffolded (no starter map)", baseName);
    }

    GameRecents_Push(s, chosen);
}

// onClick handler for a recent-game entry.
static void a_open_recent_game(EdHost *h, void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= g_gameCount) return;

    char dir[ED_GAME_LEN]; snprintf(dir, sizeof dir, "%s", g_games[idx]);

    if (!EdProject_Open(dir)) {
        EdHost_Log(h, ED_LOG_ERROR, "open recent game FAILED: %s", dir);
        return;
    }

    // Load the game's default map.
    EdProject proj;
    const char *mapRel = "maps/default.map";
    if (EdProject_Read(dir, &proj) && proj.default_map[0]) mapRel = proj.default_map;

    char mapBuf[1024];   // gameDir (512) + '/' + default_map (256) — no truncation
    snprintf(mapBuf, sizeof mapBuf, "%s/%s", dir, mapRel);

    EdScene *s = EdHost_Scene(h);
    EdScene_RescanContent(s);   // new game root → refresh palette + asset browser
    if (FileExists(mapBuf)) {
        if (EdScene_Open(s, mapBuf)) {
            Recents_Push(s, mapBuf);
            EdHost_Log(h, ED_LOG_INFO, "opened recent game: %s", dir);
        } else {
            EdHost_Log(h, ED_LOG_ERROR, "game opened but map load FAILED: %s", mapBuf);
        }
    } else {
        EdHost_Log(h, ED_LOG_INFO, "opened recent game: %s (no map)", dir);
    }

    GameRecents_Push(s, dir);
}

// ---- non-File actions -------------------------------------------------------

static void a_undo(EdHost *h, void *u)  { (void)u; EdScene_Undo(EdHost_Scene(h)); }
static void a_redo(EdHost *h, void *u)  { (void)u; EdScene_Redo(EdHost_Scene(h)); }
static void a_del (EdHost *h, void *u)  { (void)u; EdScene_DeleteSelected(EdHost_Scene(h)); }
static void a_dup (EdHost *h, void *u)  { (void)u; EdScene_DuplicateSelected(EdHost_Scene(h)); }
static void a_copy(EdHost *h, void *u)  { (void)u; EdScene_CopySelection(EdHost_Scene(h)); }
static void a_cut (EdHost *h, void *u)  { (void)u; EdScene_Cut(EdHost_Scene(h)); }
static void a_paste(EdHost *h, void *u) { (void)u; EdScene_Paste(EdHost_Scene(h)); }
static void a_selall(EdHost *h, void *u){ (void)u; EdScene_SelectAll(EdHost_Scene(h)); }

static void a_view_fly(EdHost *h, void *u) { (void)u; EdScene_SwitchView(EdHost_Scene(h), ED_VIEW_FLY); }
static void a_view_iso(EdHost *h, void *u) { (void)u; EdScene_SwitchView(EdHost_Scene(h), ED_VIEW_ORBIT); }
static void a_view_top(EdHost *h, void *u) { (void)u; EdScene_SwitchView(EdHost_Scene(h), ED_VIEW_TOP); }
static void a_snap(EdHost *h, void *u)     { (void)u; EdScene *s = EdHost_Scene(h); s->snapEnabled = !s->snapEnabled; }
static void a_grid(EdHost *h, void *u)     { (void)u; EdScene *s = EdHost_Scene(h); s->gridVisible = !s->gridVisible; }
static void a_giz_move (EdHost *h, void *u) { (void)u; EdHost_Scene(h)->mode = ENG_GIZMO_TRANSLATE; }
static void a_giz_rot  (EdHost *h, void *u) { (void)u; EdHost_Scene(h)->mode = ENG_GIZMO_ROTATE; }
static void a_giz_scale(EdHost *h, void *u) { (void)u; EdHost_Scene(h)->mode = ENG_GIZMO_SCALE; }

static void a_help_controls(EdHost *h, void *u) {
    (void)u;
    EdHost_Log(h, ED_LOG_INFO, "View: F1 fly / F2 iso / F3 top   Gizmo: 1 move 2 rot 3 scale   G show/hide grid");
    EdHost_Log(h, ED_LOG_INFO, "Place: P cycle tool, click ground   R retag/rotate   X/Del delete");
    EdHost_Log(h, ED_LOG_INFO, "Select: click   Shift+click add/remove   Ctrl+A all   move gizmo drags the set");
    EdHost_Log(h, ED_LOG_INFO, "Fly: RMB+WASDQE (Shift fast)   Ortho: RMB orbit / MMB pan / wheel zoom");
    EdHost_Log(h, ED_LOG_INFO, "Edit: Ctrl+Z undo / Ctrl+Y redo / Ctrl+S save   Ctrl+D dup / Ctrl+C copy / Ctrl+X cut / Ctrl+V paste");
}
static void a_help_about(EdHost *h, void *u) {
    (void)u;
    EdHost_Log(h, ED_LOG_INFO, "Scene Builder — engine-native map editor (libengine.a, no game code).");
}

static void RegisterMenus(EdHost *h) {
    // Load recents from cfg on first registration (scene is already loaded by now).
    Recents_Load(EdHost_Scene(h)->cfg);

    // File menu:
    //   New / Open... / Save / Save As... / ─ / [map recents] / ─ /
    //   New Game... / Open Game... / ─ / [game recents] / ─ /
    //   Reload / Exit
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="New",        .onClick=a_new });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Open...",    .shortcut="Ctrl+O", .onClick=a_open });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Save",       .shortcut="Ctrl+S", .onClick=a_save });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Save As...", .onClick=a_save_as });
    EdHost_AddMenuSeparator(h, "File");
    // (map recents appear here via the dynamic hook — registered below)
    EdHost_AddMenuSeparator(h, "File");
    // Project-level ops live under their own flyout so they don't blur into the
    // document-level New/Open above (audit P1-E).
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .submenu="Game project", .label="New Game...",  .onClick=a_new_game });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .submenu="Game project", .label="Open Game...", .onClick=a_open_game });
    EdHost_AddMenuSeparator(h, "File");
    // (recent games appear here via the second dynamic hook — registered below)
    EdHost_AddMenuSeparator(h, "File");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Reload", .onClick=a_reload, .enabled=q_dirty });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Exit",   .onClick=a_exit });

    // Combined dynamic provider: map recents then (separated) game recents.
    // One slot per menu name — see RecentsDynProvider for the combined layout.
    EdHost_SetMenuDynamic(h, "File", RecentsDynProvider, NULL);

    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Undo", .shortcut="Ctrl+Z", .onClick=a_undo, .enabled=q_can_undo });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Redo", .shortcut="Ctrl+Y", .onClick=a_redo, .enabled=q_can_redo });
    EdHost_AddMenuSeparator(h, "Edit");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Duplicate",   .shortcut="Ctrl+D", .onClick=a_dup,   .enabled=q_has_sel });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Copy",        .shortcut="Ctrl+C", .onClick=a_copy,  .enabled=q_has_sel });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Cut",         .shortcut="Ctrl+X", .onClick=a_cut,   .enabled=q_has_sel });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Paste",       .shortcut="Ctrl+V", .onClick=a_paste, .enabled=q_has_clip });
    EdHost_AddMenuSeparator(h, "Edit");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Select all",      .shortcut="Ctrl+A", .onClick=a_selall });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Delete selected", .shortcut="X",      .onClick=a_del, .enabled=q_has_sel });

    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Fly camera",       .shortcut="F1", .onClick=a_view_fly, .checked=q_view_fly });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Isometric camera", .shortcut="F2", .onClick=a_view_iso, .checked=q_view_iso });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Top-down camera",  .shortcut="F3", .onClick=a_view_top, .checked=q_view_top });
    EdHost_AddMenuSeparator(h, "View");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Show grid",    .shortcut="G", .onClick=a_grid, .checked=q_grid });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Snap to grid", .onClick=a_snap, .checked=q_snap });
    // Gizmo mode (audit P1-B): menu-discoverable counterpart to keys 1/2/3, now
    // that the panel toggle is gone.
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .submenu="Gizmo mode", .label="Move",   .shortcut="1", .onClick=a_giz_move,  .checked=q_giz_move });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .submenu="Gizmo mode", .label="Rotate", .shortcut="2", .onClick=a_giz_rot,   .checked=q_giz_rot });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .submenu="Gizmo mode", .label="Scale",  .shortcut="3", .onClick=a_giz_scale, .checked=q_giz_scale });

    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Help", .label="Controls", .onClick=a_help_controls });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Help", .label="About",    .onClick=a_help_about });
}

// ============================================================================
//  Panels: Tools palette (L), Hierarchy (L), Inspector (R), Console (B)
// ============================================================================

// ---- Tools palette ---------------------------------------------------------
// Is a row at [y, y+hh] at least partly inside the panel rect c? Used to skip
// drawing AND input-processing widgets scrolled out of the panel, so a button
// parked off-panel can't be clicked through the menu bar above it.
static bool ToolRowVisible(Rectangle c, float y, float hh) {
    return (y + hh > c.y) && (y < c.y + c.height);
}

// Draw the PLACEMENT tools (Select / Spawns / Geometry / Props / Wallbuys /
// Perks) into the ASSETS panel's running layout, advancing *yp. Placement used
// to be its own left-column TOOLS panel; it now shares the asset-browser surface
// so the left column is just HIERARCHY (see docs/scene-builder.md §"IDE frame").
// Every section is data-driven off the scanned catalogs (mobs/props/perks/
// weapons), so a new catalog file is a new button with no rebuild. `area` is the
// panel content rect; rows scrolled out of it are skipped (ToolRowVisible) so an
// off-panel button can't be clicked. The caller owns the scroll + scissor.
static void DrawPlaceTools(EdHost *h, Rectangle area, float *yp, float sc) {
    EdScene *s = EdHost_Scene(h);
    float X = area.x, W = area.width;
    float rowH = 22 * sc, gap = 25 * sc;   // button height + inter-row stride
    float y = *yp;

    // Select / move is always first.
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Select / move", s->placeTool == ED_PLACE_NONE))
        s->placeTool = ED_PLACE_NONE;
    y += gap;

    // ---- Spawns ------------------------------------------------------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Spawns");
    y += 14 * sc;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Player spawn", s->placeTool == ED_PLACE_PLAYER))
        s->placeTool = ED_PLACE_PLAYER;
    y += gap;
    // One button per mob from the data/mobs catalog (data-driven; see
    // EdScene_ScanMobs). Clicking arms ED_PLACE_MOB with that mob's tag.
    for (int i = 0; i < s->mobDefCount; i++) {
        const EdMobDef *m = &s->mobDefs[i];
        bool active = (s->placeTool == ED_PLACE_MOB && strcmp(s->placeMobId, m->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, m->name, active)) {
            s->placeTool = ED_PLACE_MOB;
            snprintf(s->placeMobId, sizeof s->placeMobId, "%s", m->id);
        }
        y += gap;
    }

    // ---- Geometry ----------------------------------------------------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Geometry");
    y += 14 * sc;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Barricade", s->placeTool == ED_PLACE_BARRICADE))
        s->placeTool = ED_PLACE_BARRICADE;
    y += gap;
    {
        // Wall: label shows "(click 2)" cue when first endpoint is pending.
        const char *wallLabel = (s->placeTool == ED_PLACE_WALL && s->wallPending)
                                ? "Wall  (click 2)" : "Wall  (2-click)";
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, wallLabel, s->placeTool == ED_PLACE_WALL)) {
            s->placeTool = ED_PLACE_WALL;
            s->wallPending = false;   // reset on re-arm so a stale start is cleared
        }
    }
    y += gap;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Obstacle", s->placeTool == ED_PLACE_OBSTACLE))
        s->placeTool = ED_PLACE_OBSTACLE;
    y += gap;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Sector (20x20)", s->placeTool == ED_PLACE_SECTOR))
        s->placeTool = ED_PLACE_SECTOR;
    y += gap;

    // ---- Props (data-driven from props/*.prop, like Spawns) ----------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Props");
    y += 14 * sc;
    if (s->propDefCount == 0) {
        if (ToolRowVisible(area, y, rowH))
            GuiLabel((Rectangle){ X, y, W, rowH }, "  (no props/ catalog)");
        y += gap;
    }
    for (int i = 0; i < s->propDefCount; i++) {
        const EdPropDef *pd = &s->propDefs[i];
        bool active = (s->placeTool == ED_PLACE_PROP && strcmp(s->placePropId, pd->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, pd->name, active)) {
            s->placeTool = ED_PLACE_PROP;
            snprintf(s->placePropId, sizeof s->placePropId, "%s", pd->id);
        }
        y += gap;
    }

    // ---- Wallbuys (data-driven from the weapons/*.weapon catalog) ----------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Wallbuys");
    y += 14 * sc;
    if (s->weaponDefCount == 0) {
        if (ToolRowVisible(area, y, rowH)) GuiLabel((Rectangle){ X, y, W, rowH }, "  (no weapons/ catalog)");
        y += gap;
    }
    for (int i = 0; i < s->weaponDefCount; i++) {
        const EdPropDef *wd = &s->weaponDefs[i];
        bool active = (s->placeTool == ED_PLACE_WALLBUY && strcmp(s->placeWeaponId, wd->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, wd->name, active)) {
            s->placeTool = ED_PLACE_WALLBUY;
            snprintf(s->placeWeaponId, sizeof s->placeWeaponId, "%s", wd->id);
        }
        y += gap;
    }

    // ---- Perks (data-driven from the perks/*.perk catalog) -----------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Perks");
    y += 14 * sc;
    if (s->perkDefCount == 0) {
        if (ToolRowVisible(area, y, rowH)) GuiLabel((Rectangle){ X, y, W, rowH }, "  (no perks/ catalog)");
        y += gap;
    }
    for (int i = 0; i < s->perkDefCount; i++) {
        const EdPropDef *kd = &s->perkDefs[i];
        bool active = (s->placeTool == ED_PLACE_PERK && strcmp(s->placePerkId, kd->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, kd->name, active)) {
            s->placeTool = ED_PLACE_PERK;
            snprintf(s->placePerkId, sizeof s->placePerkId, "%s", kd->id);
        }
        y += gap;
    }

    *yp = y;
}

// ---- Hierarchy — Feature 2: filter input -----------------------------------
static float g_hierScroll = 0;
static char  g_hierFilter[64]  = "";
static bool  g_hierFilterEdit  = false;

static void PanelHierarchy(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    float rowH   = 16 * sc;
    float filterH = 20 * sc;

    // --- filter text box ---
    Rectangle filterRect = { c.x, c.y, c.width, filterH };
    if (GuiTextBox(filterRect, g_hierFilter, sizeof g_hierFilter, g_hierFilterEdit)) {
        g_hierFilterEdit = !g_hierFilterEdit;
    }

    // shift the list area down past the filter box
    Rectangle listArea = { c.x, c.y + filterH + 2 * sc, c.width, c.height - filterH - 2 * sc };

    // Build the filtered index list (stack-allocated, max ED_MAX_ENTS).
    int filtered[ED_MAX_ENTS];
    int nFiltered = 0;
    bool hasFilter = (g_hierFilter[0] != '\0');
    for (int i = 0; i < s->proxyCount; i++) {
        EdProxy *p = &s->proxies[i];
        if (hasFilter) {
            // Build the display string and do a case-insensitive substring check.
            const char *rowStr = TextFormat("#%d %s", p->id, EdScene_KindName(p->kind));
            // Simple tolower strstr via manual scan.
            const char *haystack = rowStr;
            const char *needle   = g_hierFilter;
            bool found = false;
            for (int hi = 0; haystack[hi]; hi++) {
                int ni = 0;
                while (needle[ni] && tolower((unsigned char)haystack[hi + ni]) == tolower((unsigned char)needle[ni]))
                    ni++;
                if (!needle[ni]) { found = true; break; }
            }
            if (!found) continue;
        }
        filtered[nFiltered++] = i;
    }

    float total = nFiltered * rowH;
    if (CheckCollisionPointRec(GetMousePosition(), listArea))
        g_hierScroll -= GetMouseWheelMove() * rowH * 3.0f;
    float maxScroll = total - listArea.height; if (maxScroll < 0) maxScroll = 0;
    if (g_hierScroll < 0) g_hierScroll = 0;
    if (g_hierScroll > maxScroll) g_hierScroll = maxScroll;

    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    bool interactive = EdHost_PanelsInteractive(h);
    for (int fi = 0; fi < nFiltered; fi++) {
        float ry = listArea.y - g_hierScroll + fi * rowH;
        if (ry + rowH < listArea.y || ry > listArea.y + listArea.height) continue;
        EdProxy *p = &s->proxies[filtered[fi]];
        Rectangle row = { listArea.x, ry, listArea.width, rowH };
        bool sel = EdScene_IsSelected(s, p->id);
        bool hot = CheckCollisionPointRec(GetMousePosition(), row);
        if (sel)      DrawRectangleRec(row, (Color){ 60, 70, 90, 255 });
        else if (hot) DrawRectangleRec(row, (Color){ 40, 46, 56, 255 });
        Color dot = p->col; dot.a = 255;
        DrawRectangle((int)(listArea.x + 2 * sc), (int)(ry + 4 * sc), (int)(8 * sc), (int)(8 * sc), dot);
        Eng_UiText(TextFormat("#%d %s", p->id, EdScene_KindName(p->kind)),
                   listArea.x + 14 * sc, ry + 1 * sc, 11, sel ? ENG_UI_GOLD : ENG_UI_TEXT);
        if (interactive && hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            EdScene_SelectClick(s, p->id, shift);
        }
    }
    EndScissorMode();
    if (nFiltered == 0) {
        const char *msg = (s->proxyCount == 0) ? "(empty map)" : "(no match)";
        Eng_UiText(msg, listArea.x + 4 * sc, listArea.y + 2 * sc, 11, ENG_UI_DIM);
    }
}

// ---- Assets panel — placement tools + browse maps / models / textures ------
// The editor's single content surface (the left column is just HIERARCHY now).
// Top: the PLACEMENT tools (DrawPlaceTools — Select / Spawns / Geometry / Props /
// Wallbuys / Perks). Below: a scrollable index of the content overlay
// (EdScene.assets) — click a MAP to open it (via the unsaved-changes guard);
// MODELS and TEXTURES are browse-only previews (props are placed from the
// catalog-backed Props palette, not by arming a raw model stem), and
// models/textures show a thumbnail (EdThumb_Model / the
// engine texture cache). Typing in the filter box is a global asset search: it
// hides the placement tools and narrows the maps/models/textures lists.
static float g_assetsScroll = 0;
static char  g_assetFilter[64] = "";
static bool  g_assetFilterEdit = false;

// Case-insensitive substring test (empty needle always matches).
static bool ContainsCI(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

// One asset row: thumbnail (optional) + name, hover/click. Returns true if the
// row was left-clicked this frame. `thumb` with .id==0 draws a plain swatch.
static bool AssetRow(EdHost *h, Rectangle area, float y, float rowH, float sc,
                     Texture2D thumb, bool flip, const char *name, Color tint) {
    Rectangle row = { area.x, y, area.width, rowH };
    bool hot = CheckCollisionPointRec(GetMousePosition(), row);
    if (hot) DrawRectangleRec(row, (Color){ 40, 46, 56, 255 });
    float th = rowH - 4 * sc;
    Rectangle dst = { area.x + 2 * sc, y + 2 * sc, th, th };
    if (thumb.id) {
        Rectangle srcR = { 0, 0, (float)thumb.width, flip ? -(float)thumb.height : (float)thumb.height };
        DrawTexturePro(thumb, srcR, dst, (Vector2){ 0, 0 }, 0, WHITE);
    } else {
        DrawRectangleRec(dst, tint);
    }
    Eng_UiText(name, dst.x + th + 6 * sc, y + (rowH - 11 * sc) * 0.5f, 11, ENG_UI_TEXT);
    return hot && EdHost_PanelsInteractive(h) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void PanelAssets(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    const EdAssetIndex *ix = &s->assets;
    float rowH = 30 * sc, labelH = 16 * sc;

    // filter box
    Rectangle filterRect = { c.x, c.y, c.width, 20 * sc };
    if (GuiTextBox(filterRect, g_assetFilter, sizeof g_assetFilter, g_assetFilterEdit))
        g_assetFilterEdit = !g_assetFilterEdit;
    Rectangle area = { c.x, c.y + 22 * sc, c.width, c.height - 22 * sc };

    // Pre-render model thumbnails with the panel scissor LIFTED. The host clips
    // every panel draw to its content rect (edhost.c), and BeginTextureMode
    // honours the active GL scissor — so rendering a thumbnail under that scissor
    // clips the off-screen pass to the panel rect and produces an empty texture.
    // Lift the host clip, render (EdThumb caches → one render per model), then
    // restore it for the rest of this panel's drawing.
    EndScissorMode();
    for (int i = 0; i < ix->modelCount; i++)
        if (ContainsCI(ix->models[i].name, g_assetFilter)) EdThumb_Model(ix->models[i].path, 64);
    BeginScissorMode((int)c.x, (int)c.y, (int)c.width, (int)c.height);

    if (CheckCollisionPointRec(GetMousePosition(), area))
        g_assetsScroll -= GetMouseWheelMove() * rowH * 2.0f;
    static float prevTotal = 0;
    float maxScroll = prevTotal - area.height; if (maxScroll < 0) maxScroll = 0;
    if (g_assetsScroll < 0) g_assetsScroll = 0;
    if (g_assetsScroll > maxScroll) g_assetsScroll = maxScroll;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    float y = area.y - g_assetsScroll;
    bool vis;   // a row at [y, y+rowH] overlaps the panel?
    #define ROW_VIS(hh) ((y + (hh) > area.y) && (y < area.y + area.height))

    bool filtering = (g_assetFilter[0] != '\0');

    // ---- Placement tools (hidden while the filter narrows the asset lists) --
    if (!filtering) {
        DrawPlaceTools(h, area, &y, sc);
        y += 6 * sc;   // breathing room before the browse sections
    }

    // ---- Maps (click to open) ----
    if (ROW_VIS(labelH)) GuiLabel((Rectangle){ area.x, y, area.width, labelH }, "Maps");
    y += labelH;
    for (int i = 0; i < ix->mapCount; i++) {
        if (!ContainsCI(ix->maps[i].name, g_assetFilter)) continue;
        vis = ROW_VIS(rowH);
        if (vis && AssetRow(h, area, y, rowH, sc, (Texture2D){0}, false,
                            ix->maps[i].name, (Color){ 90, 110, 150, 255 }))
            RequestOpenMap(h, ix->maps[i].path);
        y += rowH;
    }

    // ---- Models (browse-only preview) ----
    // These are raw engine models from models/*.glb (characters, viewmodels,
    // test rigs) — NOT placeable props. A prop is resolved as
    // props/<id>/<id>.glb from a props/*.prop catalog entry (collision, tint,
    // scale included), so arming a bare model stem here would place a "prop"
    // that has no def and renders as a placeholder box. Place props from the
    // catalog-driven Props palette above instead; this section is for browsing.
    if (ROW_VIS(labelH)) GuiLabel((Rectangle){ area.x, y, area.width, labelH }, "Models");
    y += labelH;
    for (int i = 0; i < ix->modelCount; i++) {
        if (!ContainsCI(ix->models[i].name, g_assetFilter)) continue;
        vis = ROW_VIS(rowH);
        if (vis) {
            // Only render a (one-time) thumbnail when the row is actually on
            // screen, so off-screen models never pay the render cost.
            Texture2D t = EdThumb_Model(ix->models[i].path, 64);
            if (AssetRow(h, area, y, rowH, sc, t, true, ix->models[i].name,
                         (Color){ 70, 80, 70, 255 }))
                EdHost_Log(h, ED_LOG_INFO, "model: %s (not a placeable prop — use the Props palette)",
                           ix->models[i].name);
        }
        y += rowH;
    }

    // ---- Textures (browse-only preview) ----
    if (ROW_VIS(labelH)) GuiLabel((Rectangle){ area.x, y, area.width, labelH }, "Textures");
    y += labelH;
    for (int i = 0; i < ix->textureCount; i++) {
        if (!ContainsCI(ix->textures[i].name, g_assetFilter)) continue;
        vis = ROW_VIS(rowH);
        if (vis) {
            Texture2D t = {0};
            Texture2D *tp = Eng_TextureGet(Eng_LoadTexture(ix->textures[i].path));
            if (tp) t = *tp;
            if (AssetRow(h, area, y, rowH, sc, t, false, ix->textures[i].name,
                         (Color){ 80, 70, 70, 255 }))
                EdHost_Log(h, ED_LOG_INFO, "texture: %s", ix->textures[i].name);
        }
        y += rowH;
    }

    #undef ROW_VIS
    EndScissorMode();
    prevTotal = (y + g_assetsScroll) - area.y;
}

// ============================================================================
//  Inspector helpers — tiny field widgets used by PanelInspector
// ============================================================================

// Draw a float field via a GuiSlider.  Returns true and writes *v if changed.
static bool InspSlider(float X, float W, float *y, float sc,
                       const char *label, float *v, float lo, float hi) {
    float oldV = *v;
    Eng_UiText(label, X, *y + 1 * sc, 12, ENG_UI_TEXT);
    char buf[32]; snprintf(buf, sizeof buf, "%.2f", *v);
    GuiSlider((Rectangle){ X + 60 * sc, *y, W - 100 * sc, 16 * sc }, "", buf, v, lo, hi);
    *y += 22 * sc;
    return (*v != oldV);
}

// Float text-box state (one set per named field — we use a small static array).
// We track edit mode + buffer per logical field via a simple index.
#define INSP_FLOAT_FIELDS 4
static bool g_inspFloatEdit[INSP_FLOAT_FIELDS];
static char g_inspFloatBuf[INSP_FLOAT_FIELDS][32];
static int  g_inspLastId = -1;  // reset buffers when selection changes

// Draw a float field via a GuiTextBox; idx indexes g_inspFloatBuf/Edit.
// Returns true and writes *v if the user committed a change (toggled out of edit).
static bool InspFloatBox(float X, float W, float *y, float sc,
                         const char *label, float *v, int idx) {
    if (idx < 0 || idx >= INSP_FLOAT_FIELDS) return false;
    Eng_UiText(label, X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool changed = false;
    bool wasEdit = g_inspFloatEdit[idx];
    if (!wasEdit) snprintf(g_inspFloatBuf[idx], sizeof g_inspFloatBuf[idx], "%.3f", *v);
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc },
                   g_inspFloatBuf[idx], sizeof g_inspFloatBuf[idx], wasEdit)) {
        g_inspFloatEdit[idx] = !wasEdit;
        if (wasEdit) {
            // toggling out of edit → parse and commit
            float parsed = (float)atof(g_inspFloatBuf[idx]);
            if (parsed != *v) { *v = parsed; changed = true; }
        }
    }
    *y += 22 * sc;
    return changed;
}

// Per-surface texture-override field for WALL / OBSTACLE. Reads/writes the
// MapDoc TEX field via the engine accessors; a blank string clears the override
// so the surface falls back to the map's wall_ext slot. Commits on edit-toggle.
static void InspTexField(EdHost *h, EdScene *s, float X, float W, float *y, float sc) {
    static char buf[MAPDOC_TEX_NAME_LEN] = "";
    static bool edit = false;
    static int  lastId = -1;
    char cur[MAPDOC_TEX_NAME_LEN] = "";
    Eng_GetSurfaceTex(&s->doc, s->selectedId, cur, sizeof cur);
    if (lastId != s->selectedId) {
        lastId = s->selectedId;
        snprintf(buf, sizeof buf, "%s", cur);
        edit = false;
    }
    Eng_UiText("tex", X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool was = edit;
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc }, buf, sizeof buf, was)) {
        edit = !was;
        if (was && strcmp(buf, cur) != 0) {
            Eng_SetSurfaceTex(&s->doc, s->selectedId, buf);
            EdHost_CommitEdit(h);
        }
    }
    *y += 20 * sc;
    Eng_UiText("(blank = map default)", X, *y, 9, ENG_UI_DIM);
    *y += 14 * sc;
}

// ---- Inspector — Feature 1: editable fields / Feature 3: map metadata ------
static void PanelInspector(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    float X = c.x, W = c.width, y = c.y;

    // Reset per-field buffers when the selection changes (avoids stale text).
    if (s->selectedId != g_inspLastId) {
        g_inspLastId = s->selectedId;
        for (int i = 0; i < INSP_FLOAT_FIELDS; i++) {
            g_inspFloatEdit[i] = false;
            g_inspFloatBuf[i][0] = '\0';
        }
    }

    // ---- Map properties when nothing is selected (audit P1-A) --------------
    if (s->selectedId < 0) {
        Eng_UiText("MAP PROPERTIES", X, y, 14, ENG_UI_GOLD); y += 16 * sc;
        Eng_UiText("Nothing selected — editing the whole map.", X, y, 10, ENG_UI_DIM); y += 16 * sc;

        // Map name (doc.name)
        Eng_UiText("Name", X, y + 1 * sc, 12, ENG_UI_TEXT);
        static char g_mapNameBuf[MAPDOC_NAME_LEN] = "";
        static bool g_mapNameEdit = false;
        static int  g_mapNameLastId = -2; // use -2 as "uninitialised sentinel"
        // Reset buffer when we come back to the no-selection state and the doc
        // name might have changed from an Open/New.
        if (g_mapNameLastId != s->selectedId) {
            g_mapNameLastId = s->selectedId;
            snprintf(g_mapNameBuf, sizeof g_mapNameBuf, "%s", s->doc.name);
            g_mapNameEdit = false;
        }
        bool wasNameEdit = g_mapNameEdit;
        if (GuiTextBox((Rectangle){ X + 60 * sc, y, W - 60 * sc, 18 * sc },
                       g_mapNameBuf, sizeof g_mapNameBuf, wasNameEdit)) {
            g_mapNameEdit = !wasNameEdit;
            if (wasNameEdit && strcmp(g_mapNameBuf, s->doc.name) != 0) {
                snprintf(s->doc.name, sizeof s->doc.name, "%s", g_mapNameBuf);
                EdHost_CommitEdit(h);
            }
        }
        y += 22 * sc;

        // ---- Atmosphere -------------------------------------------------------
        MapDocAtmosphere *atm = &s->doc.atmosphere;
        y += 4 * sc;
        Eng_UiText("ATMOSPHERE", X, y, 13, (Color){ 200, 205, 215, 255 }); y += 18 * sc;

        // Fog color — stored as float 0-255 components.
        Color fogCol = { (unsigned char)atm->fogR, (unsigned char)atm->fogG,
                         (unsigned char)atm->fogB, 255 };
        Color newFogCol = fogCol;
        // GuiColorPicker needs a square-ish area; give it a compact block.
        float cpSz = 80 * sc;
        Eng_UiText("Fog color", X, y + 1 * sc, 12, ENG_UI_TEXT);
        GuiColorPicker((Rectangle){ X + 80 * sc, y, cpSz, cpSz }, NULL, &newFogCol);
        if (newFogCol.r != fogCol.r || newFogCol.g != fogCol.g || newFogCol.b != fogCol.b) {
            atm->fogR = (float)newFogCol.r;
            atm->fogG = (float)newFogCol.g;
            atm->fogB = (float)newFogCol.b;
            atm->present = true;
            EdHost_CommitEdit(h);
        }
        y += cpSz + 4 * sc;

        // Fog range sliders.
        float oldFogStart = atm->fogStart, oldFogEnd = atm->fogEnd;
        Eng_UiText("Fog start", X, y + 1 * sc, 12, ENG_UI_TEXT);
        char fsBuf[32]; snprintf(fsBuf, sizeof fsBuf, "%.1f", atm->fogStart);
        GuiSlider((Rectangle){ X + 80 * sc, y, W - 120 * sc, 16 * sc }, "", fsBuf, &atm->fogStart, 0, 500);
        y += 22 * sc;
        Eng_UiText("Fog end", X, y + 1 * sc, 12, ENG_UI_TEXT);
        char feBuf[32]; snprintf(feBuf, sizeof feBuf, "%.1f", atm->fogEnd);
        GuiSlider((Rectangle){ X + 80 * sc, y, W - 120 * sc, 16 * sc }, "", feBuf, &atm->fogEnd, 0, 2000);
        y += 22 * sc;
        if (atm->fogStart != oldFogStart || atm->fogEnd != oldFogEnd) {
            atm->present = true;
            EdHost_CommitEdit(h);
        }

        // Sky tint (only shown when hasSkyTint or to enable it).
        y += 4 * sc;
        Eng_UiText("Sky tint", X, y + 1 * sc, 12, ENG_UI_TEXT);
        GuiCheckBox((Rectangle){ X + 80 * sc, y, 14 * sc, 14 * sc }, " enable", &atm->hasSkyTint);
        y += 20 * sc;
        if (atm->hasSkyTint) {
            Color skyCol    = { (unsigned char)atm->skyR, (unsigned char)atm->skyG,
                                (unsigned char)atm->skyB, 255 };
            Color newSkyCol = skyCol;
            GuiColorPicker((Rectangle){ X + 80 * sc, y, cpSz, cpSz }, NULL, &newSkyCol);
            if (newSkyCol.r != skyCol.r || newSkyCol.g != skyCol.g || newSkyCol.b != skyCol.b) {
                atm->skyR = (float)newSkyCol.r;
                atm->skyG = (float)newSkyCol.g;
                atm->skyB = (float)newSkyCol.b;
                atm->present = true;
                EdHost_CommitEdit(h);
            }
            y += cpSz + 4 * sc;
        }

        // ---- Textures (read-only display) ------------------------------------
        MapDocTextures *tex = &s->doc.textures;
        if (tex->present) {
            y += 4 * sc;
            Eng_UiText("TEXTURES", X, y, 13, (Color){ 200, 205, 215, 255 }); y += 18 * sc;
            const struct { const char *lbl; const char *val; } slots[] = {
                { "floor",    tex->floor    },
                { "ground",   tex->ground   },
                { "wall_ext", tex->wall_ext },
                { "wall_int", tex->wall_int },
                { "ceiling",  tex->ceiling  },
            };
            for (int i = 0; i < 5; i++) {
                Eng_UiText(TextFormat("%s: %s", slots[i].lbl,
                           slots[i].val[0] ? slots[i].val : "(default)"),
                           X, y, 11, ENG_UI_DIM);
                y += 15 * sc;
            }
        }
        return;
    }

    // ---- Editable inspector for the selected entity ------------------------
    // Affordance to reach the map-properties view without hunting for empty
    // space to click (audit P1-A): deselect → the no-selection branch above.
    if (GuiButton((Rectangle){ X, y, W, 18 * sc }, "#185# Map properties...")) {
        s->selectedId = -1;
        return;
    }
    y += 24 * sc;

    EngMapEntKind k;
    EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    Eng_UiText(TextFormat("#%d  %s", s->selectedId, EdScene_KindName(k)), X, y, 13, ENG_UI_GOLD);
    if (EdScene_SelCount(s) > 1)
        Eng_UiText(TextFormat("%d selected (editing primary)", EdScene_SelCount(s)),
                   X, y + 14 * sc, 10, ENG_UI_DIM);
    y += 22 * sc;

    // Position x, z — all kinds.
    float px = 0, pz = 0;
    if (Eng_GetPos(&s->doc, s->selectedId, &px, &pz)) {
        float newX = px, newZ = pz;
        bool cx = InspFloatBox(X, W, &y, sc, "pos x", &newX, 0);
        bool cz = InspFloatBox(X, W, &y, sc, "pos z", &newZ, 1);
        if (cx || cz) {
            Eng_SetPos(&s->doc, s->selectedId, newX, newZ);
            EdHost_CommitEdit(h);
        }
        y += 2 * sc;
    }

    // Kind-specific fields.
    if (k == ENGMAPENT_SPAWN) {
        // Spawn mob text field.
        char mob[MAPDOC_SPAWN_MOB_LEN];
        Eng_GetSpawnMob(&s->doc, s->selectedId, mob, sizeof mob);

        Eng_UiText("mob", X, y + 1 * sc, 12, ENG_UI_TEXT);
        static char g_spawnMobBuf[MAPDOC_SPAWN_MOB_LEN] = "";
        static bool g_spawnMobEdit = false;
        static int  g_spawnMobLastId = -1;
        if (g_spawnMobLastId != s->selectedId) {
            g_spawnMobLastId  = s->selectedId;
            snprintf(g_spawnMobBuf, sizeof g_spawnMobBuf, "%s", mob);
            g_spawnMobEdit = false;
        }
        bool wasMobEdit = g_spawnMobEdit;
        if (GuiTextBox((Rectangle){ X + 60 * sc, y, W - 60 * sc, 18 * sc },
                       g_spawnMobBuf, sizeof g_spawnMobBuf, wasMobEdit)) {
            g_spawnMobEdit = !wasMobEdit;
            if (wasMobEdit && strcmp(g_spawnMobBuf, mob) != 0) {
                Eng_SetSpawnMob(&s->doc, s->selectedId, g_spawnMobBuf);
                EdHost_CommitEdit(h);
            }
        }
        y += 22 * sc;

        // Quick-toggle PLAYER / ZOMBIE buttons.
        float bw = (W - 8 * sc) / 2.0f;
        bool isPlayer = (strcmp(mob, "PLAYER") == 0);
        bool isZombie = (strcmp(mob, "ZOMBIE") == 0);
        if (GuiButton((Rectangle){ X, y, bw, 20 * sc }, isPlayer ? "[PLAYER]" : "PLAYER")) {
            if (!isPlayer) {
                Eng_SetSpawnMob(&s->doc, s->selectedId, "PLAYER");
                snprintf(g_spawnMobBuf, sizeof g_spawnMobBuf, "PLAYER");
                EdHost_CommitEdit(h);
            }
        }
        if (GuiButton((Rectangle){ X + bw + 8 * sc, y, bw, 20 * sc }, isZombie ? "[ZOMBIE]" : "ZOMBIE")) {
            if (!isZombie) {
                Eng_SetSpawnMob(&s->doc, s->selectedId, "ZOMBIE");
                snprintf(g_spawnMobBuf, sizeof g_spawnMobBuf, "ZOMBIE");
                EdHost_CommitEdit(h);
            }
        }
        y += 26 * sc;

    } else if (k == ENGMAPENT_WINDOW) {
        // Window facing toggle group.
        char dir[4]; Eng_GetWindowDir(&s->doc, s->selectedId, dir, sizeof dir);
        const char *dirs[4] = { "+x", "+z", "-x", "-z" };
        int di = 0;
        for (int i = 0; i < 4; i++) if (strcmp(dir, dirs[i]) == 0) { di = i; break; }
        Eng_UiText("facing", X, y + 1 * sc, 12, ENG_UI_TEXT); y += 16 * sc;
        int newDi = di;
        GuiToggleGroup((Rectangle){ X, y, (W - 12 * sc) / 4.0f, 20 * sc }, "+x;+z;-x;-z", &newDi);
        if (newDi != di) {
            Eng_SetWindowDir(&s->doc, s->selectedId, dirs[newDi]);
            EdHost_CommitEdit(h);
        }
        y += 26 * sc;

    } else if (k == ENGMAPENT_PROP) {
        // Prop yaw + uniform scale sliders.
        float yawDeg = 0, scale = 1;
        Eng_GetYaw(&s->doc, s->selectedId, &yawDeg);
        Eng_GetScale(&s->doc, s->selectedId, &scale);
        float newYaw = yawDeg, newScale = scale;
        if (InspSlider(X, W, &y, sc, "yaw",   &newYaw,   0,   360)) {
            Eng_SetYaw(&s->doc, s->selectedId, newYaw);
            EdHost_CommitEdit(h);
        }
        if (InspSlider(X, W, &y, sc, "scale", &newScale, 0.1f, 5)) {
            Eng_SetScale(&s->doc, s->selectedId, newScale);
            EdHost_CommitEdit(h);
        }

    } else if (k == ENGMAPENT_WALL) {
        // Walls carry only a per-surface texture override in the inspector;
        // their geometry is edited by dragging the move gizmo / endpoints.
        InspTexField(h, s, X, W, &y, sc);

    } else if (k == ENGMAPENT_OBSTACLE) {
        // Obstacle sx / sz / h.
        float sx = 1, sz = 1, oh = 1;
        Eng_GetObstacleSize(&s->doc, s->selectedId, &sx, &sz, &oh);
        float nsx = sx, nsz = sz, noh = oh;
        bool csx = InspFloatBox(X, W, &y, sc, "size x", &nsx, 0);
        bool csz = InspFloatBox(X, W, &y, sc, "size z", &nsz, 1);
        bool coh = InspFloatBox(X, W, &y, sc, "height", &noh, 2);
        if (csx || csz || coh) {
            Eng_SetObstacleSize(&s->doc, s->selectedId, nsx, nsz, noh);
            EdHost_CommitEdit(h);
        }
        y += 4 * sc;
        InspTexField(h, s, X, W, &y, sc);

    } else if (k == ENGMAPENT_SECTOR) {
        // Sector sx / sz + yLow / yHigh.
        float sx = 10, sz = 10, yLow = 0, yHigh = 0;
        Eng_GetSectorSize(&s->doc, s->selectedId, &sx, &sz);
        Eng_GetSectorHeights(&s->doc, s->selectedId, &yLow, &yHigh);
        float nsx = sx, nsz = sz, nyLow = yLow, nyHigh = yHigh;
        bool csx   = InspFloatBox(X, W, &y, sc, "size x", &nsx,   0);
        bool csz   = InspFloatBox(X, W, &y, sc, "size z", &nsz,   1);
        bool cyLow = InspFloatBox(X, W, &y, sc, "y low",  &nyLow, 2);
        bool cyHi  = InspFloatBox(X, W, &y, sc, "y high", &nyHigh,3);
        if (csx || csz) {
            Eng_SetSectorSize(&s->doc, s->selectedId, nsx, nsz);
            EdHost_CommitEdit(h);
        }
        if (cyLow || cyHi) {
            Eng_SetSectorHeights(&s->doc, s->selectedId, nyLow, nyHigh);
            EdHost_CommitEdit(h);
        }
    }
}

// ---- Console ---------------------------------------------------------------
// Minimum severity shown: 0 = all, 1 = warnings+errors, 2 = errors only.
// Maps directly onto EdLogLevel (INFO<WARN<ERROR), so the test is a >= compare.
static int g_conMinLvl = 0;

static void PanelConsole(EdHost *h, Rectangle c, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);

    // Header: Clear + a severity-filter cycle button (audit P1-F). The host
    // GuiLock()s panels while a menu/modal is open, so these can't misfire.
    float btnH = 18 * sc;
    if (GuiButton((Rectangle){ c.x, c.y, 56 * sc, btnH }, "Clear"))
        EdHost_LogClear(h);
    const char *fl = g_conMinLvl == 0 ? "Show: all"
                   : g_conMinLvl == 1 ? "Show: warn+" : "Show: errors";
    if (GuiButton((Rectangle){ c.x + 60 * sc, c.y, 96 * sc, btnH }, fl))
        g_conMinLvl = (g_conMinLvl + 1) % 3;

    Rectangle list = { c.x, c.y + btnH + 2 * sc, c.width, c.height - btnH - 2 * sc };
    float rowH = 14 * sc;
    int   total = EdHost_LogCount(h);
    int   fit = (int)(list.height / rowH); if (fit < 1) fit = 1;

    // Count lines passing the filter, then skip all but the last `fit` of them
    // so the newest stays in view.
    int passing = 0;
    for (int i = 0; i < total; i++) {
        EdLogLevel lvl; EdHost_LogLine(h, i, &lvl);
        if ((int)lvl >= g_conMinLvl) passing++;
    }
    int skip = passing - fit; if (skip < 0) skip = 0;

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    float y = list.y;
    int seen = 0;
    for (int i = 0; i < total; i++) {
        EdLogLevel lvl; const char *line = EdHost_LogLine(h, i, &lvl);
        if ((int)lvl < g_conMinLvl) continue;
        if (seen++ < skip) continue;
        Color col = lvl == ED_LOG_ERROR ? (Color){ 235, 110, 110, 255 }
                  : lvl == ED_LOG_WARN  ? ENG_UI_GOLD : ENG_UI_TEXT;
        Eng_UiText(line, list.x + 2 * sc, y, 11, col);
        y += rowH;
    }
    EndScissorMode();
}

static void RegisterPanels(EdHost *h) {
    EdHost_AddPanel(h, &(EdPanel){ .id="hierarchy", .title="HIERARCHY", .zone=ED_DOCK_LEFT,   .draw=PanelHierarchy });
    EdHost_AddPanel(h, &(EdPanel){ .id="inspector", .title="INSPECTOR", .zone=ED_DOCK_RIGHT,  .draw=PanelInspector });
    EdHost_AddPanel(h, &(EdPanel){ .id="assets",    .title="ASSETS",    .zone=ED_DOCK_RIGHT,  .draw=PanelAssets });
    EdHost_AddPanel(h, &(EdPanel){ .id="console",   .title="CONSOLE",   .zone=ED_DOCK_BOTTOM, .draw=PanelConsole });
}

// ============================================================================
//  Map tools: Tools menu (Settings dialog + Validate map)
// ============================================================================

static void OvSlider(float X, float W, float *y, float s, const char *label,
                     float *v, float lo, float hi, const char *valfmt) {
    Eng_UiText(label, X, *y + 1 * s, 13, (Color){ 200, 205, 215, 255 });
    char buf[32]; snprintf(buf, sizeof buf, valfmt, *v);
    GuiSlider((Rectangle){ X + 130 * s, *y, W - 175 * s, 16 * s }, "", buf, v, lo, hi);
    *y += 24 * s;
}
static void OvCheck(float X, float *y, float s, const char *label, bool *v) {
    GuiCheckBox((Rectangle){ X, *y, 16 * s, 16 * s }, label, v); *y += 24 * s;
}
static void OvSection(float X, float W, float *y, float s, const char *label) {
    GuiLabel((Rectangle){ X, *y, W, 16 * s }, label); *y += 18 * s;
}

static void SettingsModal(EdHost *h, Rectangle area, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);
    float pw = 420 * sc, ph = 552 * sc;
    float px = (area.width - pw) / 2.0f, py = (area.height - ph) / 2.0f; if (py < 6 * sc) py = 6 * sc;
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });
    float X = px + 16 * sc, W = pw - 32 * sc, y = py + 12 * sc;
    Eng_UiText("EDITOR SETTINGS", X, y, 18, ENG_UI_GOLD); y += 28 * sc;

    OvSection(X, W, &y, sc, "CAMERA");
    OvSlider(X, W, &y, sc, "Fly speed",  &s->camFlySpeed,  2, 60,         "%.0f");
    OvSlider(X, W, &y, sc, "Fly boost",  &s->camFlyBoost,  10, 120,       "%.0f");
    OvSlider(X, W, &y, sc, "Look sens",  &s->camLookSens,  0.0005f, 0.01f,"%.4f");
    OvSlider(X, W, &y, sc, "Orbit sens", &s->camOrbitSens, 0.001f, 0.02f, "%.3f");
    OvSlider(X, W, &y, sc, "Zoom speed", &s->camZoomSpeed, 0.02f, 0.3f,   "%.2f");

    OvSection(X, W, &y, sc, "VIEW");
    Eng_UiText("Default view", X, y + 1 * sc, 13, (Color){ 200, 205, 215, 255 });
    int vd = (int)s->viewDefault;
    GuiToggleGroup((Rectangle){ X + 130 * sc, y, (W - 132 * sc) / 3.0f, 18 * sc }, "Fly;Iso;Top", &vd);
    s->viewDefault = (EdViewMode)vd; y += 26 * sc;
    OvSlider(X, W, &y, sc, "FOV",          &s->viewFov,    45, 100, "%.0f");
    OvSlider(X, W, &y, sc, "Default zoom", &s->viewOrthoH, 5, 200,  "%.0f");

    OvSection(X, W, &y, sc, "GRID / SNAP");
    OvSlider(X, W, &y, sc, "Grid spacing", &s->gridSpacing, 0.25f, 10, "%.2f");
    float slices = (float)s->gridSlices;
    OvSlider(X, W, &y, sc, "Grid extent",  &slices, 10, 400, "%.0f"); s->gridSlices = (int)slices;
    OvCheck(X, &y, sc, " Show grid",    &s->gridVisible);
    OvCheck(X, &y, sc, " Snap to grid", &s->snapEnabled);
    OvSlider(X, W, &y, sc, "Snap step",    &s->snapStep, 0.1f, 10, "%.2f");

    OvSection(X, W, &y, sc, "EDITING");
    OvCheck(X, &y, sc, " Barricade auto-spawn ZOMBIE", &s->barricadeAutoSpawn);
    float depth = (float)s->undoDepth;
    OvSlider(X, W, &y, sc, "Undo depth*", &depth, 16, 256, "%.0f"); s->undoDepth = (int)depth;

    OvSection(X, W, &y, sc, "DISPLAY   (* applies on restart)");
    OvCheck(X, &y, sc, " VSync*", &s->vsync);
    float fps = (float)s->fpsCap;
    OvSlider(X, W, &y, sc, "FPS cap*", &fps, 0, 240, "%.0f"); s->fpsCap = (int)fps;

    float by = py + ph - 32 * sc, hw = (W - 8 * sc) / 2.0f;
    if (GuiButton((Rectangle){ X, by, hw, 24 * sc }, "Save")) {
        EdScene_SaveSettings(s, s->cfg);
        EdHost_Log(h, ED_LOG_INFO, "settings saved to editor.cfg");
    }
    if (GuiButton((Rectangle){ X + hw + 8 * sc, by, hw, 24 * sc }, "Close")) EdHost_SetModal(h, NULL, NULL);
}

static void a_settings(EdHost *h, void *u) { (void)u; EdHost_SetModal(h, SettingsModal, NULL); }

static void a_validate(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    MapDocIssue issues[64];
    int n = MapDoc_Validate(&s->doc, issues, 64);
    if (n == 0) { EdHost_Log(h, ED_LOG_INFO, "validate: OK — no problems found"); return; }
    EdHost_Log(h, ED_LOG_WARN, "validate: %d issue(s):", n);
    for (int i = 0; i < n && i < 64; i++)
        EdHost_Log(h, issues[i].severity == MAPDOC_ERROR ? ED_LOG_ERROR : ED_LOG_WARN,
                   "  %s %s", issues[i].severity == MAPDOC_ERROR ? "[E]" : "[w]", issues[i].msg);
}

static void RegisterMapTools(EdHost *h) {
    // Tools leads with its map operation; editor preferences move to the bottom
    // of Edit (the conventional home), renamed from "Settings…" (audit P1-D).
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .label="Validate map", .onClick=a_validate });
    EdHost_AddMenuSeparator(h, "Edit");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Preferences...", .onClick=a_settings });
}

// ============================================================================
//  Status bar
// ============================================================================

static void st_map(EdHost *h, char *out, int cap, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    snprintf(out, cap, "%s%s", GetFileName(s->path), s->dirty ? " *" : "");
}
static void st_count(EdHost *h, char *out, int cap, void *u) {
    (void)u; snprintf(out, cap, "%d ents", EdHost_Scene(h)->proxyCount);
}
static void st_view(EdHost *h, char *out, int cap, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    const char *v = s->view == ED_VIEW_FLY ? "fly" : s->view == ED_VIEW_ORBIT ? "iso" : "top";
    // Gizmo mode moved out of the Tools panel (audit P0-B); surface it read-only
    // here so the active transform mode (keys 1/2/3) stays discoverable.
    const char *g = s->mode == ENG_GIZMO_TRANSLATE ? "move"
                  : s->mode == ENG_GIZMO_ROTATE    ? "rotate" : "scale";
    snprintf(out, cap, "view: %s  %s%s%s", v, g,
             s->snapEnabled ? "  snap" : "", s->gridVisible ? "" : "  no-grid");
}
static void st_sel(EdHost *h, char *out, int cap, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    if (s->selectedId < 0) { out[0] = '\0'; return; }   // no dead "no selection" text (audit P1-C)
    EngMapEntKind k; EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    int n = EdScene_SelCount(s);
    if (n > 1) snprintf(out, cap, "sel #%d %s (+%d)", s->selectedId, EdScene_KindName(k), n - 1);
    else       snprintf(out, cap, "sel #%d %s", s->selectedId, EdScene_KindName(k));
}

static void RegisterStatusBar(EdHost *h) {
    EdHost_AddStatusItem(h, st_map,   NULL);
    EdHost_AddStatusItem(h, st_count, NULL);
    EdHost_AddStatusItem(h, st_view,  NULL);
    EdHost_AddStatusItem(h, st_sel,   NULL);
}

// ============================================================================
//  Descriptors
// ============================================================================

const EdPluginDesc *EdBuiltin_Menus(void) {
    static const EdPluginDesc d = { "menus", ED_PLUGIN_ABI, RegisterMenus }; return &d;
}
const EdPluginDesc *EdBuiltin_Panels(void) {
    static const EdPluginDesc d = { "panels", ED_PLUGIN_ABI, RegisterPanels }; return &d;
}
const EdPluginDesc *EdBuiltin_MapTools(void) {
    static const EdPluginDesc d = { "maptools", ED_PLUGIN_ABI, RegisterMapTools }; return &d;
}
const EdPluginDesc *EdBuiltin_StatusBar(void) {
    static const EdPluginDesc d = { "statusbar", ED_PLUGIN_ABI, RegisterStatusBar }; return &d;
}
