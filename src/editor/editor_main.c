// ============================================================================
//  editor_main.c — the scene / map builder entry point.
//
//  The editor is a SIBLING APPLICATION to the game (src/game/), not a mode
//  inside it: its own GameModule hosted by the engine (Eng_Run), depending on
//  the engine ONLY — never a game header. See docs/scene-builder.md.
//
//  This file is now just the WIRING. The IDE is three layers:
//    edscene.{c,h}  — the document + 3D viewport (MapDoc, camera, pick/gizmo).
//    edhost.{c,h}   — the shell: menu bar, dock zones, status bar, plugin API.
//    builtins.c     — first-party plugins that fill the default IDE.
//  main() loads settings, opens the window, builds the host, registers the
//  built-in plugins + any dynamic ones from ./plugins, and runs the loop.
// ============================================================================

#include "raylib.h"
#include "app.h"
#include "ui.h"
#include "cfg.h"
#include "eng_render.h"   // Eng_RenderLoad — world shader for material-mode lighting
#include "mapdoc.h"
#include "content.h"   // Eng_ResolveAssetPath

#include "edscene.h"
#include "edhost.h"
#include "builtins.h"
#include "edlauncher.h"
#include "edproject.h"

#include <stdio.h>
#include <stdlib.h>   // atoi — --select <id>
#include <string.h>

// Single-document tool: one scene, one host. The GameModule vtable is plain
// function pointers with no user arg, so these live at file scope.
static EdScene s_scene;
static EdHost *s_host;
static EngCfg  s_cfg;

// App has two modes: the start screen (launcher) and the editor proper. We open
// on the launcher unless an explicit map arg / --shot was given (CLI + CI must
// drop straight into the editor). s_editing flips true once the editor is
// entered; s_host stays NULL until then. See docs/scene-builder.md §4.
static EdLauncher s_launcher;
static bool       s_editing = false;

// --shot <file>: capture one frame after the UI settles, then quit (a headed
// smoke/verify hook, the analogue of the game's --screenshot-* dev modes).
static int  s_shotAt = -1;     // frame to capture on; -1 = normal interactive run
static int  s_frame  = 0;
static char s_shotPath[256];
static int  s_selectId = -1;   // --select <id>: preselect this entity for the shot

// Build the IDE shell for the map at s_scene.path (already set by the caller).
// Factored out so it can run either at startup (explicit map / --shot) or in
// response to a launcher action. Flips s_editing so the draw/shutdown steps
// switch from the launcher to the editor proper.
static void EnterEditor(void) {
    EdScene_Init(&s_scene);   // parses s_scene.path into the document

    s_host = EdHost_Create(&s_scene);
    EdHost_RegisterBuiltin(s_host, EdBuiltin_Menus());
    EdHost_RegisterBuiltin(s_host, EdBuiltin_Panels());
    EdHost_RegisterBuiltin(s_host, EdBuiltin_MapTools());
    EdHost_RegisterBuiltin(s_host, EdBuiltin_StatusBar());

    // Third-party plugins: any *.so in ./plugins is dlopen'd and registered
    // through the same API. Quietly skips when the folder doesn't exist.
    int dyn = EdHost_LoadDynamicPlugins(s_host, "plugins");
    EdHost_Log(s_host, ED_LOG_INFO, "Scene Builder ready — %s (%d plugin(s) from ./plugins)",
               GetFileName(s_scene.path), dyn);
    s_editing = true;
}

// A launcher OPEN_GAME / NEW_GAME resolved to `gameDir` (already EdProject_Open'd
// by the caller): resolve its default map, enter the editor on it, and record it
// in recents — the same end state as File ▸ Open Game.
static void EnterEditorForGame(const char *gameDir) {
    EdProject proj;
    const char *mapRel = "maps/default.map";
    if (EdProject_Read(gameDir, &proj) && proj.default_map[0]) mapRel = proj.default_map;

    char mapBuf[sizeof s_scene.path];   // dest is path[512]; no point going wider
    snprintf(mapBuf, sizeof mapBuf, "%s/%s", gameDir, mapRel);
    if (FileExists(mapBuf)) snprintf(s_scene.path, sizeof s_scene.path, "%s", mapBuf);
    // else: keep the default-resolved path already in s_scene.path as a fallback.

    EnterEditor();
    EdBuiltins_RememberGame(s_host, gameDir);
}

static void EdInit(void) {
    if (s_scene.uiScale <= 0.0f) s_scene.uiScale = Eng_UiRecommendedScale();  // 0 = absent in cfg
    Eng_UiApplyTheme();
    Eng_RenderLoad();   // world/skinned shaders for material mode (graceful no-op if missing)

    if (s_editing) EnterEditor();                              // explicit map / --shot
    else           EdLauncher_Init(&s_launcher, &s_cfg, s_scene.uiScale);
}

static void EdDraw(int w, int h) {
    if (!s_editing) {
        EdLauncherResult r = EdLauncher_Draw(&s_launcher, w, h);
        switch (r.action) {
        case EDL_OPEN_GAME:
            if (EdProject_Open(r.gameDir)) EnterEditorForGame(r.gameDir);
            break;
        case EDL_NEW_GAME:
            if (EdProject_New(r.gameDir, r.templateName, r.gameName) &&
                EdProject_Open(r.gameDir)) EnterEditorForGame(r.gameDir);
            break;
        case EDL_OPEN_MAP:
            snprintf(s_scene.path, sizeof s_scene.path, "%s", r.path);
            EnterEditor();
            break;
        case EDL_QUIT:
            Eng_RequestClose();
            break;
        case EDL_NONE:
            break;
        }
        return;
    }

    // --select: apply the preselection once, before the first frame is drawn, so
    // the inspector / edit handles render for the capture. Also frame the entity
    // up close (overriding the open-time frame-all) so its handles are legible.
    static bool s_selectApplied = false;
    if (s_selectId >= 0 && !s_selectApplied) {
        EdScene_SelectClick(&s_scene, s_selectId, false);
        EdScene_RebuildProxies(&s_scene);   // FrameSelected needs the proxy boxes
        EdScene_FrameSelected(&s_scene);
        s_scene.framePending = false;       // don't let the open-time frame-all override
        s_selectApplied = true;
    }

    EdBuiltins_RecoveryGuard(s_host);   // offer to restore a crash autosave on (re)load
    EdScene_AutosaveTick(&s_scene);     // periodic "<map>.autosave" while dirty
    EdHost_Frame(s_host, w, h);

    // Window title carries the document name + dirty marker (audit P1-C) — the
    // conventional place for it. Only push to the OS when it changes.
    static char s_lastTitle[600] = "";
    char title[600];
    snprintf(title, sizeof title, "%s%s — Scene Builder",
             GetFileName(s_scene.path), s_scene.dirty ? " *" : "");
    if (strcmp(title, s_lastTitle) != 0) {
        SetWindowTitle(title);
        snprintf(s_lastTitle, sizeof s_lastTitle, "%s", title);
    }

    if (s_shotAt >= 0 && ++s_frame >= s_shotAt) {
        TakeScreenshot(s_shotPath);
        Eng_RequestClose();
    }
}

static void EdShutdown(void) {
    Eng_RenderUnloadPostFX();             // release the postFX RT (shaders freed at exit)
    if (!s_editing || !s_host) return;   // quit straight from the launcher: nothing to persist
    EdScene_SaveSettings(&s_scene, &s_cfg);   // persist tweaks made this session
    EdHost_Destroy(s_host);
    EdScene_Shutdown(&s_scene);
}

static GameModule Editor_Module(void) {
    return (GameModule){ .init = EdInit, .draw = EdDraw, .shutdown = EdShutdown };
}

// ---- headless check: parse a map, build proxies, validate, exit (no window) -

static int RunCheck(void) {
    int errs = MapDoc_Parse(s_scene.path, &s_scene.doc, stderr);
    EdScene_RebuildProxies(&s_scene);
    printf("editor --check: '%s' parsed (%d errors), %d selectable entities\n",
           s_scene.path, errs, s_scene.proxyCount);
    printf("  sectors:%d walls:%d windows:%d obstacles:%d props:%d wallbuys:%d perks:%d spawns:%d\n",
           s_scene.doc.sectorCount, s_scene.doc.wallCount, s_scene.doc.windowCount,
           s_scene.doc.obstacleCount, s_scene.doc.propCount, s_scene.doc.wallbuyCount,
           s_scene.doc.perkCount, s_scene.doc.spawnCount);

    MapDocIssue issues[64];
    int nIssues = MapDoc_Validate(&s_scene.doc, issues, 64);
    int errors = 0;
    if (nIssues == 0) {
        printf("  validation: OK\n");
    } else {
        printf("  validation: %d issue(s)\n", nIssues);
        for (int i = 0; i < nIssues && i < 64; i++) {
            printf("    [%s] %s\n", issues[i].severity == MAPDOC_ERROR ? "ERROR" : "warn", issues[i].msg);
            if (issues[i].severity == MAPDOC_ERROR) errors++;
        }
    }
    return (errs > 0 || errors > 0) ? 1 : 0;
}

int main(int argc, char **argv) {
    // Content overlay (docs/game-projects.md): wire the two roots before
    // resolving anything. Defaults to the bundled "shooter" game; Open Game /
    // New Game will repoint the game root at runtime.
    char rootBuf[512];
    if (Eng_LocateRoot("stdlib",        rootBuf, sizeof rootBuf)) Eng_SetLibraryRoot(rootBuf);
    if (Eng_LocateRoot("games/shooter", rootBuf, sizeof rootBuf)) Eng_SetGameRoot(rootBuf);

    // Resolve the default map via the engine's root stack so that, once a game
    // root is set at runtime (Open Game / New Game), the game's default map wins.
    static char s_defaultMapBuf[512];
    const char *path = Eng_ResolveAssetPath("maps/default.map", s_defaultMapBuf, sizeof s_defaultMapBuf);
    if (!path) path = "games/shooter/maps/default.map";  // last-resort fallback
    bool check = false;
    bool hasMapArg = false;  // explicit map path on the CLI → skip the launcher
    int viewOverride = -1;  // -1 = no override; otherwise EdViewMode value
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) check = true;
        else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            snprintf(s_shotPath, sizeof s_shotPath, "%s", argv[++i]);
            s_shotAt = 30;     // ~0.5s of settle frames, then capture + quit
        }
        // --view <fly|iso|top>: override the starting view mode.
        // Useful with --shot for CI runs that want to verify a specific camera.
        // Applied after LoadSettings so it wins over the persisted default.
        else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
            ++i;
            if      (strcmp(argv[i], "fly") == 0) viewOverride = (int)ED_VIEW_FLY;
            else if (strcmp(argv[i], "iso") == 0) viewOverride = (int)ED_VIEW_ORBIT;
            else if (strcmp(argv[i], "top") == 0) viewOverride = (int)ED_VIEW_TOP;
        }
        // --select <id>: preselect an entity by stable id (with --shot, lets CI
        // verify selection-dependent UI — inspector fields, gizmo, edit handles).
        else if (strcmp(argv[i], "--select") == 0 && i + 1 < argc) {
            s_selectId = atoi(argv[++i]);
        }
        else { path = argv[i]; hasMapArg = true; }   // last non-flag arg = map path
    }
    snprintf(s_scene.path, sizeof s_scene.path, "%s", path);

    if (check) return RunCheck();

    // Start straight in the editor for an explicit map or a --shot (CLI + CI);
    // otherwise open on the launcher and let the user pick a game/map.
    s_editing = hasMapArg || (s_shotAt >= 0);

    EdScene_LoadSettings(&s_scene, &s_cfg);   // before the window: win/vsync/fps + default view
    if (viewOverride >= 0) s_scene.viewDefault = (EdViewMode)viewOverride;
    EngConfig cfg = { .w = s_scene.winW, .h = s_scene.winH, .title = "Scene Builder",
                      .vsync = s_scene.vsync, .resizable = true, .fpsCap = s_scene.fpsCap };
    Eng_Run(&cfg, Editor_Module());
    return 0;
}
