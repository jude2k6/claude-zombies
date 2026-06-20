// ============================================================================
//  builtins_internal.h — shared surface between the split built-in plugin files.
//
//  builtins.c was split into ed_recents.c (recents lists), edmenus.c (menu bar
//  + map tools + dirty/recovery modals), panel_inspector.c (the Inspector), and
//  edpanels.c (Hierarchy / Assets / Console panels + status bar). These few
//  declarations are the cross-file calls that used to be file-statics. The
//  public plugin entry points stay in builtins.h.
// ============================================================================
#ifndef SHOOTER_EDITOR_BUILTINS_INTERNAL_H
#define SHOOTER_EDITOR_BUILTINS_INTERNAL_H

#include "builtins.h"   // EdHost, EdPluginDesc
#include "edscene.h"    // EdScene, EngCfg, Rectangle (via raylib.h)

// --- ed_recents.c -----------------------------------------------------------
// Maps start directory for file dialogs (game root wins, else data/maps).
void MapsStartDir(char *buf, int cap);
// Load the recents lists (maps + games) from the scene's cfg handle.
void Recents_Load(EngCfg *cfg);
// Push a map path / game dir to the front of its recents list (dedup + persist).
void Recents_Push(EdScene *s, const char *path);
void GameRecents_Push(EdScene *s, const char *dir);
// Read-only access to the lists for the menu's recent-entry actions/provider.
// Get returns "" for an out-of-range index.
int         Recents_Count(void);
const char *Recents_Get(int i);
int         GameRecents_Count(void);
const char *GameRecents_Get(int i);

// --- edmenus.c --------------------------------------------------------------
// Open a map by path, gated by the unsaved-changes guard (used by the asset panel).
void RequestOpenMap(EdHost *h, const char *path);

// --- panel_inspector.c ------------------------------------------------------
// The Inspector panel draw callback (registered by edpanels.c's RegisterPanels).
void PanelInspector(EdHost *h, Rectangle c, void *u);
// Map-property editor body (name + atmosphere + textures). Called from the
// Map Settings… modal in edmenus.c. `area` is the available content rectangle.
void PanelInspector_DrawMapProps(EdHost *h, Rectangle area);

#endif // SHOOTER_EDITOR_BUILTINS_INTERNAL_H
