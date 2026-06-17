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
#include "mapdoc.h"

#include "edscene.h"
#include "edhost.h"
#include "builtins.h"

#include <stdio.h>
#include <string.h>

// Single-document tool: one scene, one host. The GameModule vtable is plain
// function pointers with no user arg, so these live at file scope.
static EdScene s_scene;
static EdHost *s_host;
static EngCfg  s_cfg;

// --shot <file>: capture one frame after the UI settles, then quit (a headed
// smoke/verify hook, the analogue of the game's --screenshot-* dev modes).
static int  s_shotAt = -1;     // frame to capture on; -1 = normal interactive run
static int  s_frame  = 0;
static char s_shotPath[256];

static void EdInit(void) {
    EdScene_Init(&s_scene);
    if (s_scene.uiScale <= 0.0f) s_scene.uiScale = Eng_UiRecommendedScale();  // 0 = absent in cfg
    Eng_UiApplyTheme();

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
}

static void EdDraw(int w, int h) {
    EdHost_Frame(s_host, w, h);
    if (s_shotAt >= 0 && ++s_frame >= s_shotAt) {
        TakeScreenshot(s_shotPath);
        Eng_RequestClose();
    }
}

static void EdShutdown(void) {
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
    const char *path = "data/maps/default.map";
    bool check = false;
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
        else path = argv[i];   // last non-flag arg = map path
    }
    snprintf(s_scene.path, sizeof s_scene.path, "%s", path);

    if (check) return RunCheck();

    EdScene_LoadSettings(&s_scene, &s_cfg);   // before the window: win/vsync/fps + default view
    if (viewOverride >= 0) s_scene.viewDefault = (EdViewMode)viewOverride;
    EngConfig cfg = { .w = s_scene.winW, .h = s_scene.winH, .title = "Scene Builder",
                      .vsync = s_scene.vsync, .resizable = true, .fpsCap = s_scene.fpsCap };
    Eng_Run(&cfg, Editor_Module());
    return 0;
}
