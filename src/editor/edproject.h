// ============================================================================
//  edproject.h — game-folder (game.project) management for the editor.
//
//  A "game" is a self-contained folder holding a game.project manifest, a
//  maps/ subdir, and (after import) content that shadows the read-only library.
//  See docs/game-projects.md for the full design.
//
//  Engine + stdlib + raylib only — NO src/game/ include (seam rule).
// ============================================================================
#ifndef SHOOTER_EDPROJECT_H
#define SHOOTER_EDPROJECT_H

#include <stdbool.h>

#define EDPROJECT_ID_LEN     64
#define EDPROJECT_NAME_LEN   128
#define EDPROJECT_MAP_LEN    256
#define EDPROJECT_BINARY_LEN 128

// Parsed fields of a game.project manifest (deffile format).
typedef struct {
    char id             [EDPROJECT_ID_LEN];     // id              shooter
    char name           [EDPROJECT_NAME_LEN];   // name            Claude Zombies
    int  engine_version;                         // engine_version  1
    char default_map    [EDPROJECT_MAP_LEN];    // default_map     maps/default.map
    char binary         [EDPROJECT_BINARY_LEN]; // binary          shooter  (Play Test exe; "" → id)
} EdProject;

// The default directory the New Game / Open Game pickers open in: the "games/"
// root that holds the bundled games (e.g. games/shooter), resolved to an
// absolute path. Falls back to ~/games, then ".", when that root can't be
// located. Writes into `buf` and returns it. Creates nothing.
const char *EdProject_DefaultGamesDir(char *buf, int cap);

// Parse <gameDir>/game.project into `out`.  Returns false (and logs to stderr)
// on I/O or parse failure.  Fields absent in the file are left at zero/empty.
bool EdProject_Read(const char *gameDir, EdProject *out);

// Write a canonical game.project into <gameDir>/game.project, overwriting any
// existing file.  Returns false on I/O error.
bool EdProject_Write(const char *gameDir, const EdProject *p);

// Validate that `gameDir` is a game folder (contains game.project), parse it,
// then wire the content overlay via Eng_SetGameRoot.  Returns false when the
// folder is missing or the manifest cannot be parsed.
bool EdProject_Open(const char *gameDir);

// Scaffold a new game folder at `gameDir`:
//   • creates <gameDir>/maps/
//   • copies library/templates/<templateName>/ into <gameDir>/ (if present)
//   • writes a game.project (id = basename of gameDir, name = displayName)
//   • seeds <gameDir>/maps/default.map if the template did not provide one
// Returns false on any mkdir / write failure.
bool EdProject_New(const char *gameDir, const char *templateName,
                   const char *displayName);

#endif // SHOOTER_EDPROJECT_H
