// ============================================================================
//  builtins.h — the editor's first-party plugins.
//
//  Each is an ordinary EdPluginDesc, registered compiled-in by editor_main via
//  EdHost_RegisterBuiltin. They use the SAME EdHost_* API a third-party .so
//  would; the only liberty they take is including edscene.h to drive the
//  document directly (in-tree plugins are allowed that — see edhost.h).
// ============================================================================
#ifndef SHOOTER_EDITOR_BUILTINS_H
#define SHOOTER_EDITOR_BUILTINS_H

#include "edhost.h"

const EdPluginDesc *EdBuiltin_Menus(void);      // File / Edit / View / Help
const EdPluginDesc *EdBuiltin_Panels(void);     // Tools palette, Hierarchy, Inspector, Console
const EdPluginDesc *EdBuiltin_MapTools(void);   // Tools menu: Settings… + Validate
const EdPluginDesc *EdBuiltin_StatusBar(void);  // status segments

#endif
