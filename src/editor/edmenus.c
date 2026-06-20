// ============================================================================
//  edmenus.c — the editor's menu bar + map tools + unsaved/recovery modals.
//
//  Split out of builtins.c. Contributes two plugins: EdBuiltin_Menus (File /
//  Edit / View / Help, the recents dynamic hook, and all their actions) and
//  EdBuiltin_MapTools (the Tools ▸ Validate + Edit ▸ Preferences settings
//  dialog). The unsaved-changes guard and the crash-recovery prompt live here
//  too since they gate File operations. Recents storage lives in ed_recents.c.
// ============================================================================

#include "builtins.h"
#include "builtins_internal.h"
#include "edscene.h"
#include "edfiledialog.h"
#include "edproject.h"  // EdProject_Open, EdProject_New, EdProject_Read

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"
#include "app.h"      // Eng_RequestClose

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Label-storage sizes for the dynamic recents provider (mirror ed_recents.c).
#define ED_RECENT_MAX  8
#define ED_GAME_MAX    8

// ============================================================================
//  Command Palette  (Ctrl+P)
// ============================================================================

#define CMD_PAL_MAX  256   // max items we snapshot

typedef struct {
    const EdMenuItem *item;
    char              menu[48];   // copy of item->menu at snapshot time
} CmdPalEntry;

static char         g_palQuery[64];
static CmdPalEntry  g_palEntries[CMD_PAL_MAX];
static int          g_palCount;
static int          g_palSel;    // selected row (into the *filtered* set)

// Case-insensitive substring check.
static bool pal_contains(const char *haystack, const char *needle) {
    if (!needle[0]) return true;
    size_t nl = strlen(needle);
    for (; *haystack; haystack++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char h = haystack[i]; char n = needle[i];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) break;
        }
        if (i == nl) return true;
    }
    return false;
}

// Snapshot callback — collect items once when the palette opens.
static void pal_collect(const char *menu, const char *label, const char *shortcut,
                        const EdMenuItem *item, void *user) {
    (void)menu; (void)label; (void)shortcut; (void)user;
    if (!item->label) return;   // skip separators
    if (g_palCount >= CMD_PAL_MAX) return;
    g_palEntries[g_palCount].item = item;
    snprintf(g_palEntries[g_palCount].menu, sizeof g_palEntries[0].menu,
             "%s", item->menu ? item->menu : "");
    g_palCount++;
}

static void CmdPaletteModal(EdHost *h, Rectangle area, void *user) {
    (void)user;
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);

    float pw = 480 * sc, ph = 360 * sc;
    ph = fminf(ph, area.height - 20 * sc);
    float px = (area.width  - pw) / 2.0f;
    float py = (area.height - ph) / 2.0f; if (py < 8 * sc) py = 8 * sc;

    // Background panel
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 20, 23, 30, 255 });

    float X = px + 12 * sc, W = pw - 24 * sc, y = py + 10 * sc;

    // Title
    Eng_UiText("COMMAND PALETTE", X, y, 14, ENG_UI_GOLD); y += 22 * sc;

    // Query text box
    bool queryChanged = false;
    {
        char prev[64]; snprintf(prev, sizeof prev, "%s", g_palQuery);
        GuiTextBox((Rectangle){ X, y, W, 22 * sc }, g_palQuery, sizeof g_palQuery, true);
        if (strcmp(prev, g_palQuery) != 0) { queryChanged = true; }
    }
    y += 28 * sc;

    if (queryChanged) g_palSel = 0;

    // Build filtered list for this frame.
    // We build it on the stack since we need it for both input and draw.
    static int    g_filtIdx[CMD_PAL_MAX];
    int filtCount = 0;
    for (int i = 0; i < g_palCount; i++) {
        const EdMenuItem *it = g_palEntries[i].item;
        // Compose search string: "Menu Label"
        char combined[128];
        snprintf(combined, sizeof combined, "%s %s",
                 g_palEntries[i].menu, it->label ? it->label : "");
        if (!pal_contains(combined, g_palQuery)) continue;
        g_filtIdx[filtCount++] = i;
    }

    // Keyboard navigation
    if (IsKeyPressed(KEY_DOWN)) {
        g_palSel++;
        if (g_palSel >= filtCount) g_palSel = filtCount > 0 ? filtCount - 1 : 0;
    }
    if (IsKeyPressed(KEY_UP)) {
        g_palSel--;
        if (g_palSel < 0) g_palSel = 0;
    }

    // Escape closes
    if (IsKeyPressed(KEY_ESCAPE)) { EdHost_SetModal(h, NULL, NULL); return; }

    // Click outside closes
    Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !CheckCollisionPointRec(mp, (Rectangle){ px, py, pw, ph })) {
        EdHost_SetModal(h, NULL, NULL);
        return;
    }

    // Enter executes selected item
    if (IsKeyPressed(KEY_ENTER) && filtCount > 0 && g_palSel < filtCount) {
        const EdMenuItem *it = g_palEntries[g_filtIdx[g_palSel]].item;
        bool enabled = !it->enabled || it->enabled(h, it->user);
        if (enabled && it->onClick) {
            EdHost_SetModal(h, NULL, NULL);
            it->onClick(h, it->user);
            return;
        }
    }

    // Draw list rows
    float rowH   = 22 * sc;
    float listH  = ph - (y - py) - 8 * sc;
    int   maxVis = (int)(listH / rowH);

    // Scroll offset so selected row stays visible
    static int g_palScroll = 0;
    if (queryChanged) g_palScroll = 0;
    if (g_palSel < g_palScroll)              g_palScroll = g_palSel;
    if (g_palSel >= g_palScroll + maxVis)    g_palScroll = g_palSel - maxVis + 1;
    if (g_palScroll < 0)                     g_palScroll = 0;

    BeginScissorMode((int)X, (int)y, (int)W, (int)listH);

    for (int r = g_palScroll; r < filtCount && r < g_palScroll + maxVis; r++) {
        int ei = g_filtIdx[r];
        const EdMenuItem *it = g_palEntries[ei].item;
        bool enabled = !it->enabled || it->enabled(h, it->user);
        bool selected = (r == g_palSel);

        float ry = y + (r - g_palScroll) * rowH;

        // Row background
        if (selected) {
            DrawRectangle((int)X, (int)ry, (int)W, (int)rowH,
                          (Color){ 60, 100, 180, 200 });
        } else if (CheckCollisionPointRec(mp, (Rectangle){ X, ry, W, rowH })) {
            DrawRectangle((int)X, (int)ry, (int)W, (int)rowH,
                          (Color){ 40, 44, 55, 200 });
            // Click on row: select + execute
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && enabled && it->onClick) {
                g_palSel = r;
                EdHost_SetModal(h, NULL, NULL);
                it->onClick(h, it->user);
                EndScissorMode();
                return;
            } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                g_palSel = r;
            }
        }

        // "Menu ▸ Label" on the left
        char label[128];
        snprintf(label, sizeof label, "%s \xe2\x96\xb8 %s",
                 g_palEntries[ei].menu, it->label ? it->label : "");
        Color textCol = enabled ? ENG_UI_TEXT : (Color){ 100, 105, 115, 255 };
        Eng_UiText(label, X + 6 * sc, ry + 4 * sc, 12, textCol);

        // Shortcut on the right (dimmed)
        if (it->shortcut && it->shortcut[0]) {
            float scw = MeasureText(it->shortcut, (int)(12 * sc));
            Eng_UiText(it->shortcut, X + W - scw - 8 * sc, ry + 4 * sc, 12,
                       (Color){ 130, 130, 150, 200 });
        }
    }

    EndScissorMode();

    // Count hint
    if (filtCount == 0) {
        Eng_UiText("No matching commands.", X + 6 * sc, y + 6 * sc, 12,
                   (Color){ 120, 120, 130, 200 });
    }
}

void EdBuiltins_OpenCommandPalette(EdHost *h) {
    g_palQuery[0] = '\0';
    g_palSel      = 0;
    g_palCount    = 0;
    EdHost_ForEachMenuItem(h, pal_collect, NULL);
    EdHost_SetModal(h, CmdPaletteModal, NULL);
}

// thunk so the palette can be registered as a normal menu item
static void a_open_cmd_palette(EdHost *h, void *u) { (void)u; EdBuiltins_OpenCommandPalette(h); }

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
void RequestOpenMap(EdHost *h, const char *path) {
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
static bool q_material (EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->materialMode; }

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

// Play Test: save + launch the game on the current map (detached child). Same
// logic backs the Ctrl+R global shortcut in edhost.c.
static void a_playtest(EdHost *h, void *u) {
    (void)u;
    char m[256];
    bool ok = EdScene_PlayTest(EdHost_Scene(h), m, sizeof m);
    EdHost_Log(h, ok ? ED_LOG_INFO : ED_LOG_ERROR, "%s", m);
}

static void a_exit(EdHost *h, void *u) {
    (void)u;
    if (EdHost_Scene(h)->dirty) { g_pending = PEND_EXIT; EdHost_SetModal(h, DirtyGuardModal, NULL); }
    else                          do_exit(h);
}

// ---- Dynamic recents provider -----------------------------------------------

// onClick for a recent-file entry: user = (void*)(intptr_t)index into the list.
static void a_open_recent(EdHost *h, void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= Recents_Count()) return;
    EdScene *s = EdHost_Scene(h);
    char path[512]; snprintf(path, sizeof path, "%s", Recents_Get(idx));
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
    int rc = Recents_Count();
    int nm = rc < max ? rc : max;
    for (int i = 0; i < nm && pos < max; i++, pos++) {
        snprintf(g_recentLabels[i], sizeof g_recentLabels[i], "%s", GetFileName(Recents_Get(i)));
        out[pos] = (EdMenuItem){
            .menu    = "File",
            .label   = g_recentLabels[i],
            .onClick = a_open_recent,
            .user    = (void*)(intptr_t)i,
        };
    }

    // --- game recents (separator + entries) ---
    int gc = GameRecents_Count();
    if (gc > 0 && pos < max) {
        out[pos++] = (EdMenuItem){ .menu = "File", .label = NULL };  // separator
    }
    for (int i = 0; i < gc && pos < max; i++, pos++) {
        snprintf(g_gameLabels[i], sizeof g_gameLabels[i], "[game] %s", GetFileName(GameRecents_Get(i)));
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
    if (idx < 0 || idx >= GameRecents_Count()) return;

    char dir[512]; snprintf(dir, sizeof dir, "%s", GameRecents_Get(idx));

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
static void a_material(EdHost *h, void *u) { (void)u; EdScene *s = EdHost_Scene(h); s->materialMode = !s->materialMode; }
static void a_frame_sel(EdHost *h, void *u){ (void)u; EdScene_FrameSelected(EdHost_Scene(h)); }
static void a_frame_all(EdHost *h, void *u){ (void)u; EdScene_FrameAll(EdHost_Scene(h)); }
static void a_giz_move (EdHost *h, void *u) { (void)u; EdHost_Scene(h)->mode = ENG_GIZMO_TRANSLATE; }
static void a_giz_rot  (EdHost *h, void *u) { (void)u; EdHost_Scene(h)->mode = ENG_GIZMO_ROTATE; }
static void a_giz_scale(EdHost *h, void *u) { (void)u; EdHost_Scene(h)->mode = ENG_GIZMO_SCALE; }

// Controls reference — a modal (instead of a console dump that scrolls away).
// Two columns: key chord (gold) + what it does. Grouped by activity.
static void ControlsModal(EdHost *h, Rectangle area, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);
    float pw = 540 * sc, ph = 470 * sc;
    float px = (area.width  - pw) / 2.0f;
    float py = (area.height - ph) / 2.0f; if (py < 6 * sc) py = 6 * sc;
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });

    float X = px + 18 * sc, kx = X, dx = X + 170 * sc, y = py + 14 * sc;
    Eng_UiText("CONTROLS", X, y, 18, ENG_UI_GOLD); y += 30 * sc;

    // {key, desc}; a NULL key with a desc is a section header, NULL/NULL a gap.
    static const char *rows[][2] = {
        { NULL, "VIEW" },
        { "F1 / F2 / F3",   "Fly / Isometric / Top-down camera" },
        { "F / Home",       "Frame selection / Frame all" },
        { "G / M",          "Show grid / Material (textured) mode" },
        { "Ctrl + = / -",   "Zoom the UI in / out" },
        { NULL, NULL },
        { NULL, "EDIT" },
        { "1 / 2 / 3",      "Gizmo: Move / Rotate / Scale" },
        { "P",              "Cycle place tool (click ground to drop)" },
        { "R",              "Retag spawn / rotate barricade" },
        { "X / Del",        "Delete selection" },
        { "Click / Shift",  "Select / add to selection;  Ctrl+A all" },
        { "Ctrl+C/X/V/D",   "Copy / Cut / Paste / Duplicate" },
        { "Ctrl+Z / Ctrl+Y","Undo / Redo" },
        { NULL, NULL },
        { NULL, "FILE / NAVIGATION" },
        { "Ctrl+S / Ctrl+O","Save / Open" },
        { "Ctrl+R",         "Play test (save + launch the game)" },
        { "RMB + WASDQE",   "Fly (Shift = fast)" },
        { "RMB / MMB / Wheel","Orbit / Pan / Zoom (ortho views)" },
    };
    int n = (int)(sizeof rows / sizeof rows[0]);
    for (int i = 0; i < n; i++) {
        if (!rows[i][0] && !rows[i][1]) { y += 8 * sc; continue; }       // gap
        if (!rows[i][0]) { Eng_UiText(rows[i][1], X, y, 13, ENG_UI_GOLD); y += 18 * sc; continue; } // header
        Eng_UiText(rows[i][0], kx, y, 12, (Color){ 220, 200, 130, 255 });
        Eng_UiText(rows[i][1], dx, y, 12, ENG_UI_TEXT);
        y += 17 * sc;
    }

    float bw = 100 * sc, by = py + ph - 32 * sc;
    if (GuiButton((Rectangle){ px + (pw - bw) / 2.0f, by, bw, 24 * sc }, "Close"))
        EdHost_SetModal(h, NULL, NULL);
}

static void a_help_controls(EdHost *h, void *u) { (void)u; EdHost_SetModal(h, ControlsModal, NULL); }
static void a_help_about(EdHost *h, void *u) {
    (void)u;
    EdHost_Log(h, ED_LOG_INFO, "Scene Builder — engine-native map editor (libengine.a, no game code).");
}

static void RegisterMenus(EdHost *h) {
    // Load recents from cfg on first registration (scene is already loaded by now).
    Recents_Load(EdHost_Scene(h)->cfg);

    // File menu:
    //   New / Open... / Save / Save As... / ─ / Play Test / ─ / [map recents] / ─ /
    //   New Game... / Open Game... / ─ / [game recents] / ─ /
    //   Reload / Exit
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="New",        .onClick=a_new });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Open...",    .shortcut="Ctrl+O", .onClick=a_open });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Save",       .shortcut="Ctrl+S", .onClick=a_save });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Save As...", .onClick=a_save_as });
    EdHost_AddMenuSeparator(h, "File");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="File", .label="Play Test",  .shortcut="Ctrl+R", .onClick=a_playtest });
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

    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Command Palette...", .shortcut="Ctrl+P", .onClick=a_open_cmd_palette });
    EdHost_AddMenuSeparator(h, "Edit");
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
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Frame selection", .shortcut="F",    .onClick=a_frame_sel, .enabled=q_has_sel });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Frame all",       .shortcut="Home", .onClick=a_frame_all });
    EdHost_AddMenuSeparator(h, "View");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Show grid",     .shortcut="G", .onClick=a_grid, .checked=q_grid });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Snap to grid",  .onClick=a_snap, .checked=q_snap });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .label="Material mode", .shortcut="M", .onClick=a_material, .checked=q_material });
    // Gizmo mode (audit P1-B): menu-discoverable counterpart to keys 1/2/3, now
    // that the panel toggle is gone.
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .submenu="Gizmo mode", .label="Move",   .shortcut="1", .onClick=a_giz_move,  .checked=q_giz_move });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .submenu="Gizmo mode", .label="Rotate", .shortcut="2", .onClick=a_giz_rot,   .checked=q_giz_rot });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="View", .submenu="Gizmo mode", .label="Scale",  .shortcut="3", .onClick=a_giz_scale, .checked=q_giz_scale });
    // NOTE: Help menu is registered in RegisterMapTools (after Tools) so the
    // bar order is File / Edit / View / Tools / Help (Fix #8).
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

// Scroll accumulator for SettingsModal body; reset when the modal opens.
static float g_settingsScroll = 0;
static bool  g_settingsResetScroll = false;  // set by a_settings, consumed first draw

static void SettingsModal(EdHost *h, Rectangle area, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);
    float pw = 420 * sc;
    float ph = 552 * sc;
    ph = fminf(ph, area.height - 12 * sc);   // clamp to available screen height
    float px = (area.width - pw) / 2.0f, py = (area.height - ph) / 2.0f; if (py < 6 * sc) py = 6 * sc;
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });

    // Reset scroll on first draw after modal opens.
    if (g_settingsResetScroll) { g_settingsScroll = 0; g_settingsResetScroll = false; }

    float X = px + 16 * sc, W = pw - 32 * sc;

    // ---- Title bar (pinned, not scrolled) ------------------------------------
    float titleH = 40 * sc;   // "EDITOR SETTINGS" text + gap below
    Eng_UiText("EDITOR SETTINGS", X, py + 12 * sc, 18, ENG_UI_GOLD);

    // ---- Button row (pinned to the bottom, not scrolled) --------------------
    float btnH  = 32 * sc;
    float by    = py + ph - btnH;
    float hw    = (W - 8 * sc) / 2.0f;
    if (GuiButton((Rectangle){ X, by, hw, 24 * sc }, "Save")) {
        EdScene_SaveSettings(s, s->cfg);
        EdHost_Log(h, ED_LOG_INFO, "settings saved to editor.cfg");
    }
    if (GuiButton((Rectangle){ X + hw + 8 * sc, by, hw, 24 * sc }, "Close"))
        EdHost_SetModal(h, NULL, NULL);

    // ---- Scrollable body (scissored) ----------------------------------------
    float bodyY  = py + titleH;
    float bodyH  = ph - titleH - btnH;
    Rectangle bodyRect = { px, bodyY, pw, bodyH };

    // Accumulate mouse-wheel scroll when cursor is over the panel.
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ px, py, pw, ph }))
        g_settingsScroll -= GetMouseWheelMove() * 22 * sc * 2.0f;

    // Virtual layout pass to measure content height (no draw calls).
    float cy = 0;  // relative accumulator
    cy += 18 * sc; // CAMERA header
    cy += 5 * 24 * sc; // 5 sliders
    cy += 18 * sc; // VIEW header
    cy += 26 * sc; // default view toggle
    cy += 2 * 24 * sc; // FOV + zoom sliders
    cy += 18 * sc; // GRID/SNAP header
    cy += 3 * 24 * sc; // 3 sliders
    cy += 2 * 24 * sc; // 2 checkboxes
    cy += 24 * sc; // snap step slider
    cy += 18 * sc; // EDITING header
    cy += 24 * sc; // checkbox
    cy += 24 * sc; // undo slider
    cy += 18 * sc; // DISPLAY header
    cy += 24 * sc; // vsync checkbox
    cy += 24 * sc; // fps slider
    cy += 20 * sc; // "Requires restart" banner header
    float contentH = cy;

    // Clamp scroll to [0, max scrollable].
    float maxScroll = contentH - bodyH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_settingsScroll < 0) g_settingsScroll = 0;
    if (g_settingsScroll > maxScroll) g_settingsScroll = maxScroll;

    BeginScissorMode((int)bodyRect.x, (int)bodyRect.y, (int)bodyRect.width, (int)bodyRect.height);

    float y = bodyY - g_settingsScroll;

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
    // Undo depth takes effect on the NEXT map Open (EngMapHistory_Init), not on
    // app restart — so it gets its own note rather than the DISPLAY "*" (restart).
    float depth = (float)s->undoDepth;
    OvSlider(X, W, &y, sc, "Undo depth (next open)", &depth, 16, 256, "%.0f"); s->undoDepth = (int)depth;

    OvSection(X, W, &y, sc, "DISPLAY");
    // "Requires restart" banner — groups VSync and FPS cap (window settings that
    // only take effect when the editor is restarted).
    Eng_UiText("\xe2\x80\x94 Requires restart \xe2\x80\x94", X, y + 2 * sc, 11,
               (Color){ 180, 160, 70, 220 });
    y += 20 * sc;
    OvCheck(X, &y, sc, " VSync", &s->vsync);
    float fps = (float)s->fpsCap;
    OvSlider(X, W, &y, sc, "FPS cap", &fps, 0, 240, "%.0f"); s->fpsCap = (int)fps;

    EndScissorMode();
}

static void a_settings(EdHost *h, void *u) {
    (void)u;
    g_settingsResetScroll = true;
    EdHost_SetModal(h, SettingsModal, NULL);
}

// ---- Map Settings modal (Fix #3) --------------------------------------------
// Shows the map-property editor (name / atmosphere / textures) in a modal panel
// so the Inspector's default empty state can be a quiet "Nothing selected" hint.

// Scroll accumulator for MapSettingsModal body; reset when the modal opens.
static float g_mapSettingsScroll = 0;
static bool  g_mapSettingsResetScroll = false;  // set by a_map_settings, consumed first draw

static void MapSettingsModal(EdHost *h, Rectangle area, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);
    Eng_UiSetScale(sc); Eng_UiApplyFont(13);
    float pw = 380 * sc;
    float ph = 500 * sc;
    ph = fminf(ph, area.height - 12 * sc);   // clamp to available screen height
    float px = (area.width  - pw) / 2.0f;
    float py = (area.height - ph) / 2.0f; if (py < 6 * sc) py = 6 * sc;
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });

    // Reset scroll on first draw after modal opens.
    if (g_mapSettingsResetScroll) { g_mapSettingsScroll = 0; g_mapSettingsResetScroll = false; }

    float X = px + 16 * sc, W = pw - 32 * sc;

    // ---- Title bar (pinned, not scrolled) ------------------------------------
    float titleH = 40 * sc;   // "MAP SETTINGS" text + gap below
    Eng_UiText("MAP SETTINGS", X, py + 12 * sc, 18, ENG_UI_GOLD);

    // ---- Button row (pinned to the bottom, not scrolled) --------------------
    float btnH = 32 * sc;
    float by   = py + ph - btnH;
    if (GuiButton((Rectangle){ X + (W - 100 * sc) / 2.0f, by, 100 * sc, 24 * sc }, "Close"))
        EdHost_SetModal(h, NULL, NULL);

    // ---- Scrollable body (scissored) ----------------------------------------
    float bodyY = py + titleH;
    float bodyH = ph - titleH - btnH;
    Rectangle bodyRect = { px, bodyY, pw, bodyH };

    // Accumulate mouse-wheel scroll when cursor is over the panel.
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ px, py, pw, ph }))
        g_mapSettingsScroll -= GetMouseWheelMove() * 22 * sc * 2.0f;

    // Estimate content height: map name + atmosphere (fog colour picker +
    // fog start/end sliders + sky tint checkbox + optional sky picker) +
    // textures (5 rows). Use a generous upper bound; the scissor clips the rest.
    // The content height is dominated by the two GuiColorPicker widgets (~80px
    // each at sc=1), so we estimate conservatively and let over-scroll clamp.
    float contentH = 600 * sc;   // generous upper bound; real content <= this

    float maxScroll = contentH - bodyH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_mapSettingsScroll < 0) g_mapSettingsScroll = 0;
    if (g_mapSettingsScroll > maxScroll) g_mapSettingsScroll = maxScroll;

    BeginScissorMode((int)bodyRect.x, (int)bodyRect.y, (int)bodyRect.width, (int)bodyRect.height);

    // PanelInspector_DrawMapProps draws from body.y downward; offset by scroll.
    Rectangle scrolledBody = { X, bodyY - g_mapSettingsScroll, W, contentH };
    PanelInspector_DrawMapProps(h, scrolledBody);

    EndScissorMode();
}

static void a_map_settings(EdHost *h, void *u) {
    (void)u;
    g_mapSettingsResetScroll = true;
    EdHost_SetModal(h, MapSettingsModal, NULL);
}

static void a_rescan(EdHost *h, void *u) {
    (void)u;
    EdScene_RescanContent(EdHost_Scene(h));
    EdHost_Log(h, ED_LOG_INFO, "content rescanned — palette + asset browser refreshed");
}

static void a_stats(EdHost *h, void *u) {
    (void)u; MapDoc *d = &EdHost_Scene(h)->doc;
    EdHost_Log(h, ED_LOG_INFO, "map '%s': sectors %d  walls %d  windows %d  obstacles %d",
               d->name[0] ? d->name : "(unnamed)",
               d->sectorCount, d->wallCount, d->windowCount, d->obstacleCount);
    EdHost_Log(h, ED_LOG_INFO, "          props %d  wallbuys %d  perks %d  spawns %d",
               d->propCount, d->wallbuyCount, d->perkCount, d->spawnCount);
}

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

// ============================================================================
//  Align / Distribute
// ============================================================================

// enabled guards
static bool q_sel2(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->selCount >= 2; }
static bool q_sel3(EdHost *h, void *u) { (void)u; return EdHost_Scene(h)->selCount >= 3; }

// Helper: collect (x,z) for each selected entity.  Returns count.
// Out arrays must be at least selCount in size.
static int align_gather(EdScene *s, float *xs, float *zs) {
    int n = s->selCount;
    for (int i = 0; i < n; i++) {
        float x = 0, z = 0;
        Eng_GetPos(&s->doc, s->selIds[i], &x, &z);
        xs[i] = x; zs[i] = z;
    }
    return n;
}

// Align: set every selected entity's X to the minimum X across the selection.
static void a_align_min_x(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    int n = s->selCount; if (n < 2) return;
    float xs[ED_MAX_ENTS], zs[ED_MAX_ENTS];
    align_gather(s, xs, zs);
    float target = xs[0];
    for (int i = 1; i < n; i++) if (xs[i] < target) target = xs[i];
    for (int i = 0; i < n; i++) Eng_SetPos(&s->doc, s->selIds[i], target, zs[i]);
    EdHost_CommitEdit(h);
}

// Align: set every selected entity's X to the maximum X across the selection.
static void a_align_max_x(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    int n = s->selCount; if (n < 2) return;
    float xs[ED_MAX_ENTS], zs[ED_MAX_ENTS];
    align_gather(s, xs, zs);
    float target = xs[0];
    for (int i = 1; i < n; i++) if (xs[i] > target) target = xs[i];
    for (int i = 0; i < n; i++) Eng_SetPos(&s->doc, s->selIds[i], target, zs[i]);
    EdHost_CommitEdit(h);
}

// Align: set every selected entity's Z to the minimum Z across the selection.
static void a_align_min_z(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    int n = s->selCount; if (n < 2) return;
    float xs[ED_MAX_ENTS], zs[ED_MAX_ENTS];
    align_gather(s, xs, zs);
    float target = zs[0];
    for (int i = 1; i < n; i++) if (zs[i] < target) target = zs[i];
    for (int i = 0; i < n; i++) Eng_SetPos(&s->doc, s->selIds[i], xs[i], target);
    EdHost_CommitEdit(h);
}

// Align: set every selected entity's Z to the maximum Z across the selection.
static void a_align_max_z(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    int n = s->selCount; if (n < 2) return;
    float xs[ED_MAX_ENTS], zs[ED_MAX_ENTS];
    align_gather(s, xs, zs);
    float target = zs[0];
    for (int i = 1; i < n; i++) if (zs[i] > target) target = zs[i];
    for (int i = 0; i < n; i++) Eng_SetPos(&s->doc, s->selIds[i], xs[i], target);
    EdHost_CommitEdit(h);
}

// Simple insertion sort on a parallel index array by a key array.
static void sort_by(int *idx, int n, float *key) {
    for (int i = 1; i < n; i++) {
        int   ti = idx[i];
        float tk = key[ti];
        int j = i - 1;
        while (j >= 0 && key[idx[j]] > tk) { idx[j+1] = idx[j]; j--; }
        idx[j+1] = ti;
    }
}

// Distribute: spread entities evenly along X between the first and last (by X).
static void a_dist_x(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    int n = s->selCount; if (n < 3) return;
    float xs[ED_MAX_ENTS], zs[ED_MAX_ENTS];
    align_gather(s, xs, zs);

    // Build sorted index array by X.
    int idx[ED_MAX_ENTS];
    for (int i = 0; i < n; i++) idx[i] = i;
    sort_by(idx, n, xs);

    float xMin = xs[idx[0]], xMax = xs[idx[n-1]];
    float step = (xMax - xMin) / (float)(n - 1);
    for (int r = 1; r < n - 1; r++) {
        int i = idx[r];
        Eng_SetPos(&s->doc, s->selIds[i], xMin + step * r, zs[i]);
    }
    EdHost_CommitEdit(h);
}

// Distribute: spread entities evenly along Z between the first and last (by Z).
static void a_dist_z(EdHost *h, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    int n = s->selCount; if (n < 3) return;
    float xs[ED_MAX_ENTS], zs[ED_MAX_ENTS];
    align_gather(s, xs, zs);

    // Build sorted index array by Z.
    int idx[ED_MAX_ENTS];
    for (int i = 0; i < n; i++) idx[i] = i;
    sort_by(idx, n, zs);

    float zMin = zs[idx[0]], zMax = zs[idx[n-1]];
    float step = (zMax - zMin) / (float)(n - 1);
    for (int r = 1; r < n - 1; r++) {
        int i = idx[r];
        Eng_SetPos(&s->doc, s->selIds[i], xs[i], zMin + step * r);
    }
    EdHost_CommitEdit(h);
}

static void RegisterMapTools(EdHost *h) {
    // Tools: Map Settings first (Fix #3), then Validate. Editor preferences stay
    // at the bottom of Edit (audit P1-D). Help is registered here — after Tools —
    // so the bar order is File / Edit / View / Tools / Help (Fix #8).
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .label="Map Settings...", .onClick=a_map_settings });
    EdHost_AddMenuSeparator(h, "Tools");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .label="Validate map",    .onClick=a_validate });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .label="Map statistics",  .onClick=a_stats });
    EdHost_AddMenuSeparator(h, "Tools");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .label="Rescan content",  .onClick=a_rescan });
    EdHost_AddMenuSeparator(h, "Tools");
    // Align submenu — needs ≥ 2 selected entities.
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .submenu="Align", .label="Min X", .onClick=a_align_min_x, .enabled=q_sel2 });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .submenu="Align", .label="Max X", .onClick=a_align_max_x, .enabled=q_sel2 });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .submenu="Align", .label="Min Z", .onClick=a_align_min_z, .enabled=q_sel2 });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .submenu="Align", .label="Max Z", .onClick=a_align_max_z, .enabled=q_sel2 });
    // Distribute submenu — needs ≥ 3 selected entities.
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .submenu="Distribute", .label="Across X", .onClick=a_dist_x, .enabled=q_sel3 });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Tools", .submenu="Distribute", .label="Across Z", .onClick=a_dist_z, .enabled=q_sel3 });
    EdHost_AddMenuSeparator(h, "Edit");
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Edit", .label="Preferences...", .onClick=a_settings });

    // Help is last in the bar (Fix #8). RegisterMenus owns File/Edit/View; we
    // own Tools, so we register Help here to ensure it follows Tools.
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Help", .label="Controls", .onClick=a_help_controls });
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu="Help", .label="About",    .onClick=a_help_about });
}

// ============================================================================
//  Descriptors
// ============================================================================

const EdPluginDesc *EdBuiltin_Menus(void) {
    static const EdPluginDesc d = { "menus", ED_PLUGIN_ABI, RegisterMenus }; return &d;
}
const EdPluginDesc *EdBuiltin_MapTools(void) {
    static const EdPluginDesc d = { "maptools", ED_PLUGIN_ABI, RegisterMapTools }; return &d;
}
