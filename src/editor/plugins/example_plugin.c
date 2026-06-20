// ============================================================================
//  example_plugin.c — a minimal third-party editor plugin (.so).
//
//  Built as build/plugins/example_plugin.so; loaded at launch when the editor
//  finds it in ./plugins. It demonstrates the entire plugin contract:
//    1. export ed_plugin_main() returning an EdPluginDesc;
//    2. in the register fn, contribute UI via the stable EdHost_* API only.
//  It links NOTHING — the EdHost_*/Eng_* symbols it calls are resolved from the
//  editor binary's exported table at dlopen time (editor is built -rdynamic).
//
//  What it adds: a "Plugins/Say hello" menu item that logs to the console, and
//  a status-bar segment showing the live selection id. Note it never includes
//  edscene.h — a third-party plugin stays on the EdHost_* accessors, so it is
//  insulated from the editor's internal layout.
// ============================================================================

#include "edhost.h"
#include <stdio.h>

static void say_hello(EdHost *h, void *user) {
    (void)user;
    EdHost_Log(h, ED_LOG_INFO, "hello from example_plugin.so — selection is #%d",
               EdHost_SelectedId(h));
}

static void status_selection(EdHost *h, char *out, int cap, void *user) {
    (void)user;
    int id = EdHost_SelectedId(h);
    if (id >= 0) snprintf(out, cap, "plugin: sel=#%d", id);
    else         snprintf(out, cap, "plugin: idle");
}

// Demo place-tool: shows up under the PALETTE's "Plugins" category; clicking the
// tile invokes this (the seam for plugin-contributed placement, EdHost_AddPlaceTool).
static void place_marker(EdHost *h, void *user) {
    (void)user;
    EdHost_Log(h, ED_LOG_INFO, "example_plugin: 'Marker' place-tool clicked (demo)");
}

static void Register(EdHost *h) {
    EdHost_AddMenuItem(h, &(EdMenuItem){ .menu = "Plugins", .label = "Say hello", .onClick = say_hello });
    EdHost_AddStatusItem(h, status_selection, NULL);
    EdHost_AddPlaceTool(h, &(EdHostPlaceTool){ .label = "Marker",
        .tint = (Color){ 120, 200, 255, 255 }, .onPlace = place_marker });
}

const EdPluginDesc *ed_plugin_main(void) {
    static const EdPluginDesc desc = {
        .name       = "example",
        .abiVersion = ED_PLUGIN_ABI,
        .registerFn = Register,
    };
    return &desc;
}
