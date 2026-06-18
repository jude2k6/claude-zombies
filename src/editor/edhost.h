// ============================================================================
//  edhost.h — the editor SHELL + plugin API (the "IDE frame").
//
//  EdHost is the Unity-style application frame: a top menu bar (File / Edit /
//  View / Tools / Help), resizable dock zones (left / right / bottom) holding
//  panels around a central 3D viewport, and a status bar. It owns NONE of the
//  map-editing logic — that is EdScene (edscene.h). The host's job is layout +
//  input routing + a registration surface that plugins use to contribute menu
//  items, panels, status segments, and log output.
//
//  PLUGINS. Everything the shell shows is contributed through the EdHost_Add*
//  API below — the built-in tools (place palette, hierarchy, inspector,
//  console, File/Edit/View/Tools/Help menus) are themselves "first-party
//  plugins" that call it at startup. A plugin is just an EdRegisterFn. It can
//  reach the editor two ways:
//      • compiled-in  — linked into the `editor` binary, registered directly.
//      • dynamic (.so)— dropped in ./plugins/, dlopen'd at launch, calling the
//                       SAME EdHost_* functions (the binary is linked
//                       -rdynamic so the symbols resolve).
//  Both paths converge on one descriptor + one API, so a built-in and a
//  third-party plugin are written identically. In-tree plugins may include
//  edscene.h and drive the scene directly; third-party plugins should stay on
//  the stable EdHost_* accessor surface near the bottom of this header.
// ============================================================================
#ifndef SHOOTER_EDHOST_H
#define SHOOTER_EDHOST_H

#include "raylib.h"
#include "mapdoc.h"
#include <stdbool.h>

typedef struct EdHost  EdHost;
typedef struct EdScene EdScene;   // concrete type in edscene.h

// ---- plugin ABI ------------------------------------------------------------
// Bump ED_PLUGIN_ABI on any breaking change to the EdHost_* surface; dynamic
// plugins whose abiVersion doesn't match are refused at load.
#define ED_PLUGIN_ABI   1
#define ED_PLUGIN_ENTRY "ed_plugin_main"   // symbol a .so must export

typedef void (*EdRegisterFn)(EdHost *host);

typedef struct {
    const char  *name;        // human-readable plugin name (for logs)
    int          abiVersion;  // must equal ED_PLUGIN_ABI
    EdRegisterFn registerFn;  // called once at load to contribute UI
} EdPluginDesc;

// A dynamic plugin (.so) exports:  const EdPluginDesc *ed_plugin_main(void);
typedef const EdPluginDesc *(*EdPluginMainFn)(void);

// ---- dock zones ------------------------------------------------------------
typedef enum { ED_DOCK_LEFT = 0, ED_DOCK_RIGHT, ED_DOCK_BOTTOM, ED_DOCK_COUNT } EdDockZone;

// ---- callbacks -------------------------------------------------------------
typedef void (*EdActionFn)(EdHost *h, void *user);              // menu click
typedef bool (*EdQueryFn) (EdHost *h, void *user);              // enabled / checked
typedef void (*EdPanelFn) (EdHost *h, Rectangle content, void *user); // draw a panel
typedef void (*EdStatusFn)(EdHost *h, char *out, int cap, void *user);// fill status text

// ---- menu registration -----------------------------------------------------
// `menu` names a top-level menu, created on first reference. A NULL `label`
// (via EdHost_AddMenuSeparator) inserts a divider. `enabled`/`checked` are
// polled each frame the menu is open; NULL `enabled` = always enabled.
typedef struct {
    const char *menu;
    const char *submenu;    // optional: group into a "<submenu> ▸" flyout under
                            // `menu`. NULL = a normal top-level item. Items sharing
                            // a (menu, submenu) pair appear together in the flyout.
    const char *label;
    const char *shortcut;   // right-aligned hint, e.g. "Ctrl+S" (may be NULL)
    EdActionFn  onClick;
    EdQueryFn   enabled;
    EdQueryFn   checked;    // non-NULL and true → draw a check mark
    void       *user;
} EdMenuItem;

void EdHost_AddMenuItem(EdHost *h, const EdMenuItem *item);
void EdHost_AddMenuSeparator(EdHost *h, const char *menu);

// Dynamic items hook — called each frame the menu is open, after static items.
// `fn` fills `out[0..max-1]` and returns the count written (≤ max).
// A separator is automatically drawn before the dynamic items when static items exist.
// Pass fn=NULL to clear. Only one dynamic provider per named menu is supported.
typedef int (*EdMenuDynFn)(EdHost *h, EdMenuItem *out, int max, void *user);
void EdHost_SetMenuDynamic(EdHost *h, const char *menu, EdMenuDynFn fn, void *user);

// ---- panel registration ----------------------------------------------------
typedef struct {
    const char *id;      // unique id (also used for logs)
    const char *title;   // header / tab title
    EdDockZone  zone;
    EdPanelFn   draw;    // called with the panel's content rectangle
    void       *user;
    float       prefH;   // preferred content height, UNSCALED px; 0 = flexible.
                         // Fixed-height panels keep prefH*uiScale; flexible ones
                         // share the leftover space in their zone.
} EdPanel;

void EdHost_AddPanel(EdHost *h, const EdPanel *panel);

// ---- status bar ------------------------------------------------------------
// Segments are drawn left-to-right separated by "  |  ", in registration order.
void EdHost_AddStatusItem(EdHost *h, EdStatusFn fn, void *user);

// ---- modal overlay ---------------------------------------------------------
// A single full-window modal panel (e.g. the Settings dialog). While set, it is
// drawn over everything and captures all input (the viewport + panels are
// suppressed). `fn` gets the whole window area; pass NULL to close.
typedef void (*EdModalFn)(EdHost *h, Rectangle area, void *user);
void EdHost_SetModal(EdHost *h, EdModalFn fn, void *user);
bool EdHost_HasModal(EdHost *h);

// ---- console / log ---------------------------------------------------------
typedef enum { ED_LOG_INFO = 0, ED_LOG_WARN, ED_LOG_ERROR } EdLogLevel;
void EdHost_Log(EdHost *h, EdLogLevel lvl, const char *fmt, ...);

// Read-side, for the console panel: number of buffered lines and line access.
int         EdHost_LogCount(EdHost *h);
const char *EdHost_LogLine(EdHost *h, int i, EdLogLevel *outLvl);
void        EdHost_LogClear(EdHost *h);   // empty the ring buffer

// ---- editor context accessors (stable ABI for dynamic plugins) -------------
MapDoc  *EdHost_Doc(EdHost *h);
int      EdHost_SelectedId(EdHost *h);
void     EdHost_Select(EdHost *h, int id);
void     EdHost_CommitEdit(EdHost *h);   // push one undo step + mark dirty
EdScene *EdHost_Scene(EdHost *h);        // concrete context (in-tree plugins)
float    EdHost_UiScale(EdHost *h);

// True when a panel's own (non-raygui) hit-tests should act this frame — i.e.
// no modal is up, no menu dropdown is open, and no splitter is being dragged.
// raygui widgets are already locked in those states; use this to gate manual
// CheckCollisionPointRec/IsMouseButtonPressed handling (e.g. a list row click).
bool     EdHost_PanelsInteractive(EdHost *h);

// ============================================================================
//  Shell lifecycle (used by editor_main.c, not by plugins)
// ============================================================================

EdHost *EdHost_Create(EdScene *scene);
void    EdHost_Destroy(EdHost *h);

// Register a compiled-in plugin (called before the loop).
void    EdHost_RegisterBuiltin(EdHost *h, const EdPluginDesc *desc);
// Scan `dir` for *.so, dlopen each, version-check, and register. Returns count.
int     EdHost_LoadDynamicPlugins(EdHost *h, const char *dir);

// One frame: layout, viewport update+draw, menus, panels, status bar.
void    EdHost_Frame(EdHost *h, int w, int h_px);

#endif // SHOOTER_EDHOST_H
