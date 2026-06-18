// ============================================================================
//  edproject.c — game-folder (game.project) management for the editor.
//
//  Reads/writes the deffile-format game.project manifest, scaffolds new game
//  folders, and wires the content overlay roots when a game is opened.
//
//  Engine + stdlib + raylib only — NO src/game/ include (seam rule).
// ============================================================================

#include "edproject.h"

#include "deffile.h"   // Eng_DefForEachLine, tokeniser
#include "content.h"   // Eng_SetGameRoot, Eng_GetLibraryRoot

#include "raylib.h"    // DirectoryExists, FileExists, LoadFileText, UnloadFileText,
                       // LoadDirectoryFiles, UnloadDirectoryFiles, GetFileName

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // mkdir (POSIX)
#ifdef _WIN32
#  include <direct.h>  // _mkdir (Win32)
#endif

// ---------------------------------------------------------------------------
// Portable mkdir (single component — not recursive).
// ---------------------------------------------------------------------------
static int MkDir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

// ---------------------------------------------------------------------------
// EdProject_Read
// ---------------------------------------------------------------------------

// Callback state for the deffile line-walker.
typedef struct {
    EdProject *out;
} ProjectReadState;

static void ProjectLineCallback(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    if (n < 2) return;  // need at least key + value
    EdProject *p = ((ProjectReadState *)user)->out;
    const char *key = toks[0];
    const char *val = toks[1];

    if      (strcmp(key, "id")             == 0) snprintf(p->id,          sizeof p->id,          "%s", val);
    else if (strcmp(key, "name")           == 0) snprintf(p->name,        sizeof p->name,        "%s", val);
    else if (strcmp(key, "engine_version") == 0) p->engine_version = atoi(val);
    else if (strcmp(key, "default_map")    == 0) snprintf(p->default_map, sizeof p->default_map, "%s", val);
    // Unknown keys are silently ignored (forward compatibility).
}

bool EdProject_Read(const char *gameDir, EdProject *out) {
    if (!gameDir || !out) return false;

    char manifest[768];
    snprintf(manifest, sizeof manifest, "%s/game.project", gameDir);

    if (!FileExists(manifest)) {
        fprintf(stderr, "edproject: no game.project in '%s'\n", gameDir);
        return false;
    }

    char *text = LoadFileText(manifest);
    if (!text) {
        fprintf(stderr, "edproject: could not read '%s'\n", manifest);
        return false;
    }

    memset(out, 0, sizeof *out);
    out->engine_version = 1;  // sensible default if absent

    ProjectReadState st = { .out = out };
    Eng_DefForEachLine(text, ProjectLineCallback, &st);

    UnloadFileText(text);
    return true;
}

// ---------------------------------------------------------------------------
// EdProject_Write
// ---------------------------------------------------------------------------

bool EdProject_Write(const char *gameDir, const EdProject *p) {
    if (!gameDir || !p) return false;

    char manifest[768];
    snprintf(manifest, sizeof manifest, "%s/game.project", gameDir);

    FILE *f = fopen(manifest, "w");
    if (!f) {
        fprintf(stderr, "edproject: could not write '%s'\n", manifest);
        return false;
    }

    // Header comment matches the existing shooter/game.project style.
    fprintf(f, "# %s — game project manifest (deffile format: key value).\n", p->name[0] ? p->name : p->id);
    fprintf(f, "# See docs/game-projects.md for the full schema.\n\n");

    fprintf(f, "id              %s\n", p->id);
    fprintf(f, "name            %s\n", p->name);
    fprintf(f, "engine_version  %d\n", p->engine_version > 0 ? p->engine_version : 1);
    fprintf(f, "default_map     %s\n", p->default_map[0] ? p->default_map : "maps/default.map");

    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// EdProject_Open
// ---------------------------------------------------------------------------

bool EdProject_Open(const char *gameDir) {
    if (!gameDir || !gameDir[0]) return false;
    if (!DirectoryExists(gameDir)) {
        fprintf(stderr, "edproject: directory not found: '%s'\n", gameDir);
        return false;
    }

    EdProject proj;
    if (!EdProject_Read(gameDir, &proj)) return false;

    Eng_SetGameRoot(gameDir);
    return true;
}

// Copy a directory tree recursively into dst (one level deep, then recurse).
// We walk with LoadDirectoryFiles; if an entry is a directory we mkdir+recurse.
static void CopyDirTree(const char *src, const char *dst) {
    FilePathList entries = LoadDirectoryFiles(src);
    for (unsigned int i = 0; i < entries.count; i++) {
        const char *srcPath = entries.paths[i];
        const char *name    = GetFileName(srcPath);

        if (DirectoryExists(srcPath)) {
            // Sub-directory: create it in dst and recurse.
            char dstSub[768];
            snprintf(dstSub, sizeof dstSub, "%s/%s", dst, name);
            MkDir(dstSub);  // ignore error if already exists
            CopyDirTree(srcPath, dstSub);
        } else {
            // Regular file: copy verbatim.
            char dstPath[768];
            snprintf(dstPath, sizeof dstPath, "%s/%s", dst, name);
            char *data = LoadFileText(srcPath);
            if (data) {
                FILE *f = fopen(dstPath, "w");
                if (f) { fputs(data, f); fclose(f); }
                UnloadFileText(data);
            }
        }
    }
    UnloadDirectoryFiles(entries);
}

// ---------------------------------------------------------------------------
// Minimal starter map — a single 40×40 SECTOR with one PLAYER spawn.
// Written into <gameDir>/maps/default.map when no template provides it.
// Grammar is a strict subset of what MapDoc_Parse + MapDoc_Validate accept.
// ---------------------------------------------------------------------------
static const char *k_StarterMap =
    "# Default map - minimal starter seeded by New Game.\n"
    "\n"
    "NAME Default\n"
    "\n"
    "SECTOR main  0 0  40 40  0\n"
    "    SPAWN PLAYER  0 0\n"
    "END\n";

// ---------------------------------------------------------------------------
// EdProject_New
// ---------------------------------------------------------------------------

bool EdProject_New(const char *gameDir, const char *templateName,
                   const char *displayName) {
    if (!gameDir || !gameDir[0]) return false;

    // Create the game root directory itself if it doesn't exist.
    if (!DirectoryExists(gameDir)) {
        if (MkDir(gameDir) != 0) {
            fprintf(stderr, "edproject: could not create '%s'\n", gameDir);
            return false;
        }
    }

    // Create maps/ sub-directory.
    char mapsDir[768];
    snprintf(mapsDir, sizeof mapsDir, "%s/maps", gameDir);
    MkDir(mapsDir);  // ignore error if already exists

    // Copy template contents into the game folder if the template exists.
    // Template dir: <library>/templates/<templateName>/
    bool templateCopied = false;
    const char *libRoot = Eng_GetLibraryRoot();
    if (libRoot && libRoot[0] && templateName && templateName[0]) {
        char tmplDir[768];
        snprintf(tmplDir, sizeof tmplDir, "%s/templates/%s", libRoot, templateName);
        if (DirectoryExists(tmplDir)) {
            CopyDirTree(tmplDir, gameDir);
            templateCopied = true;
        }
    }

    // Derive the game id from the last path component of gameDir.
    const char *baseName = GetFileName(gameDir);
    if (!baseName || !baseName[0]) baseName = "mygame";

    // Build the manifest. Use displayName for the human name, baseName for the id.
    EdProject proj;
    memset(&proj, 0, sizeof proj);
    snprintf(proj.id,          sizeof proj.id,   "%s", baseName);
    snprintf(proj.name,        sizeof proj.name, "%s", displayName && displayName[0] ? displayName : baseName);
    proj.engine_version = 1;
    snprintf(proj.default_map, sizeof proj.default_map, "maps/default.map");

    if (!EdProject_Write(gameDir, &proj)) return false;

    // Seed a minimal default.map unless the template already provided one.
    char defaultMap[768];
    snprintf(defaultMap, sizeof defaultMap, "%s/maps/default.map", gameDir);
    if (!templateCopied || !FileExists(defaultMap)) {
        FILE *f = fopen(defaultMap, "w");
        if (!f) {
            fprintf(stderr, "edproject: could not write starter map '%s'\n", defaultMap);
            return false;
        }
        fputs(k_StarterMap, f);
        fclose(f);
    }

    return true;
}

// ---------------------------------------------------------------------------
// (no game.c-style init/shutdown needed — this is a stateless utility module)
// ---------------------------------------------------------------------------
