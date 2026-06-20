// ============================================================================
//  edlauncher.c — the editor's start / welcome screen. See edlauncher.h.
// ============================================================================
#include "edlauncher.h"

#include "edproject.h"      // EdProject_Read — recent-game display names; EdProject_CopyTree
#include "edfiledialog.h"   // folder / file pickers
#include "content.h"        // Eng_LocateRoot — locate the packs/ tier for Install Pack
#include "ui.h"             // Eng_Ui* chrome + theme colours
#include "raylib.h"
#include "raygui.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Init — load the recent games (game.0..7) and resolve each display name.
// ---------------------------------------------------------------------------
void EdLauncher_Init(EdLauncher *L, EngCfg *cfg, float scale) {
    memset(L, 0, sizeof *L);
    L->cfg   = cfg;
    L->scale = (scale > 0.0f) ? scale : 1.0f;
    L->page  = 0;

    for (int i = 0; i < EDL_MAX_RECENT; i++) {
        char key[32]; snprintf(key, sizeof key, "game.%d", i);
        const char *dir = EngCfg_Str(cfg, key, "");
        if (!dir || !dir[0]) break;
        if (!DirectoryExists(dir)) continue;   // skip games that moved/were deleted

        int n = L->recentCount;
        snprintf(L->recentDir[n], sizeof L->recentDir[n], "%s", dir);
        // Prefer the manifest's name; fall back to the folder name.
        EdProject proj;
        if (EdProject_Read(dir, &proj) && proj.name[0])
            snprintf(L->recentName[n], sizeof L->recentName[n], "%s", proj.name);
        else
            snprintf(L->recentName[n], sizeof L->recentName[n], "%s", GetFileName(dir));
        L->recentCount++;
    }
}

// A flat clickable row (name + dim subtitle). Returns true on a left-click.
static bool LauncherRow(Rectangle r, float sc, const char *title,
                        const char *subtitle) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Eng_UiPanelBg(r, hover ? (Color){ 52, 58, 72, 255 } : (Color){ 32, 36, 46, 255 });
    Eng_UiText(title, r.x + 12 * sc, r.y + 8 * sc, 16, ENG_UI_TEXT);
    if (subtitle && subtitle[0])
        Eng_UiText(subtitle, r.x + 12 * sc, r.y + 28 * sc, 11, ENG_UI_DIM);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// ---------------------------------------------------------------------------
// Draw one frame; returns the resolved action (EDL_NONE until chosen).
// ---------------------------------------------------------------------------
EdLauncherResult EdLauncher_Draw(EdLauncher *L, int w, int h) {
    EdLauncherResult res = { .action = EDL_NONE };
    float sc = L->scale;

    Eng_UiSetScale(sc);
    Eng_UiApplyFont(14);
    ClearBackground((Color){ 18, 20, 26, 255 });

    // ---- header band -------------------------------------------------------
    Eng_UiPanelBg((Rectangle){ 0, 0, (float)w, 84 * sc }, (Color){ 24, 27, 34, 255 });
    Eng_UiText("SCENE BUILDER", 40 * sc, 22 * sc, 30, ENG_UI_GOLD);
    Eng_UiText("Map editor  ·  games as projects", 42 * sc, 56 * sc, 13, ENG_UI_DIM);

    float top  = 116 * sc;
    float padX = 40 * sc;

    // =======================================================================
    //  PAGE 1 — New Game template picker
    // =======================================================================
    if (L->page == 1) {
        Eng_UiText("NEW GAME", padX, top, 22, ENG_UI_TEXT);
        Eng_UiText("Choose a template:", padX, top + 34 * sc, 14, ENG_UI_DIM);

        float cardY = top + 70 * sc;
        float cardW = (float)w - 2 * padX;

        // Empty — the working template.
        if (LauncherRow((Rectangle){ padX, cardY, cardW, 52 * sc }, sc,
                        "Empty", "A bare GameModule (init / frame / fixed / draw + Eng_Run)")) {
            char start[512]; EdProject_DefaultGamesDir(start, sizeof start);
            char dir[512];
            if (EdFileDialog_SelectFolder(dir, sizeof dir,
                                          "New Game — choose a folder", start)) {
                res.action = EDL_NEW_GAME;
                snprintf(res.gameDir, sizeof res.gameDir, "%s", dir);
                snprintf(res.templateName, sizeof res.templateName, "empty");
                snprintf(res.gameName, sizeof res.gameName, "%s", GetFileName(dir));
            }
        }

        // FPS — not yet shipped (library/templates/fps is TODO).
        GuiSetState(STATE_DISABLED);
        LauncherRow((Rectangle){ padX, cardY + 60 * sc, cardW, 52 * sc }, sc,
                    "FPS  (coming soon)", "A first-person starter — controller + HUD");
        GuiSetState(STATE_NORMAL);

        if (GuiButton((Rectangle){ padX, cardY + 132 * sc, 110 * sc, 30 * sc }, "Back"))
            L->page = 0;
        return res;
    }

    // =======================================================================
    //  PAGE 0 — welcome: actions (left) + recent games (right)
    // =======================================================================
    float colGap = 28 * sc;
    float leftW  = 220 * sc;
    float leftX  = padX;
    float rightX = leftX + leftW + colGap;
    float rightW = (float)w - rightX - padX;
    float bh     = 38 * sc;   // action button height
    float by     = top;

    // ---- left column: actions ---------------------------------------------
    if (GuiButton((Rectangle){ leftX, by, leftW, bh }, "#149#  New Game")) {
        L->page = 1;
    }
    by += bh + 10 * sc;
    if (GuiButton((Rectangle){ leftX, by, leftW, bh }, "#1#  Open Game...")) {
        char start[512]; EdProject_DefaultGamesDir(start, sizeof start);
        char dir[512];
        if (EdFileDialog_SelectFolder(dir, sizeof dir, "Open Game — choose a folder", start)) {
            res.action = EDL_OPEN_GAME;
            snprintf(res.gameDir, sizeof res.gameDir, "%s", dir);
        }
    }
    by += bh + 10 * sc;
    if (GuiButton((Rectangle){ leftX, by, leftW, bh }, "#12#  Open Map...")) {
        char start[512]; EdProject_DefaultGamesDir(start, sizeof start);
        char file[512];
        if (EdFileDialog_Open(file, sizeof file, start)) {
            res.action = EDL_OPEN_MAP;
            snprintf(res.path, sizeof res.path, "%s", file);
        }
    }
    by += bh + 10 * sc;
    // Install Pack — copy a pack source folder into packs/ so it's importable for
    // every project. Self-contained (no game needed), so it runs inline here and
    // stays on the launch screen; feedback goes to the status line below.
    // (docs/content-packs.md §4 — mirrors File ▸ Game project ▸ Install Pack…)
    if (GuiButton((Rectangle){ leftX, by, leftW, bh }, "#80#  Install Pack...")) {
        char packsRoot[512];
        if (!Eng_LocateRoot("packs", packsRoot, sizeof packsRoot)) {
            snprintf(L->status, sizeof L->status, "Install failed: packs/ not found next to editor");
        } else {
            char start[512]; EdProject_DefaultGamesDir(start, sizeof start);
            char dir[512];
            if (EdFileDialog_SelectFolder(dir, sizeof dir,
                                          "Install Pack — choose a pack folder", start)) {
                const char *name = GetFileName(dir);
                if (name && name[0]) {
                    char dst[768]; snprintf(dst, sizeof dst, "%s/%s", packsRoot, name);
                    EdProject_CopyTree(dir, dst);
                    snprintf(L->status, sizeof L->status, "Installed pack '%s' into packs/", name);
                }
            }
        }
    }
    by += bh + 10 * sc;
    if (GuiButton((Rectangle){ leftX, by, leftW, bh }, "#159#  Quit"))
        res.action = EDL_QUIT;

    // Status line (Install Pack feedback) under the action column.
    if (L->status[0])
        Eng_UiText(L->status, leftX, by + bh + 12 * sc, 12, ENG_UI_GOLD);

    // ---- right column: recent games ---------------------------------------
    Eng_UiText("RECENT GAMES", rightX, top - 26 * sc, 13, ENG_UI_GOLD);
    if (L->recentCount == 0) {
        Eng_UiPanelBg((Rectangle){ rightX, top, rightW, 52 * sc }, (Color){ 28, 31, 40, 255 });
        Eng_UiText("No recent games yet — New Game or Open Game to begin.",
                   rightX + 12 * sc, top + 18 * sc, 13, ENG_UI_DIM);
    } else {
        float ry = top;
        float rh = 46 * sc;
        for (int i = 0; i < L->recentCount; i++) {
            if (LauncherRow((Rectangle){ rightX, ry, rightW, rh - 6 * sc }, sc,
                            L->recentName[i], L->recentDir[i])) {
                res.action = EDL_OPEN_GAME;
                snprintf(res.gameDir, sizeof res.gameDir, "%s", L->recentDir[i]);
            }
            ry += rh;
        }
    }

    return res;
}
