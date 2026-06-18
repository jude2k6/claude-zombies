// ============================================================================
//  edfiledialog.c — native file-dialog implementation using tinyfiledialogs.
//
//  tinyfiledialogs (vendored at src/editor/vendor/) wraps zenity, kdialog,
//  or the equivalent on each platform, popping a real OS dialog. Its warnings
//  are suppressed in CMakeLists.txt; this file is warning-clean under our own
//  -Wall -Wextra flags.
// ============================================================================

#include "edfiledialog.h"
#include "vendor/tinyfiledialogs.h"

#include <string.h>
#include <stdio.h>

// The only filter patterns we expose: *.map files.
static const char *const k_patterns[] = { "*.map" };
static const int          k_nPatterns = 1;
static const char        *k_desc      = "map files (*.map)";

// Build a combined "dir/name" default path for the dialogs.
// Returns `buf`; if startDir is NULL we use an empty string (CWD).
static const char *MakeDefault(char *buf, int cap, const char *startDir, const char *name) {
    if (startDir && startDir[0]) {
        if (name && name[0])
            snprintf(buf, cap, "%s/%s", startDir, name);
        else
            snprintf(buf, cap, "%s/", startDir);
    } else {
        snprintf(buf, cap, "%s", name ? name : "");
    }
    return buf;
}

bool EdFileDialog_Open(char *out, int cap, const char *startDir) {
    char def[512];
    MakeDefault(def, sizeof def, startDir, "");

    const char *result = tinyfd_openFileDialog(
        "Open map",         // title
        def,                // default path
        k_nPatterns,        // number of filter patterns
        k_patterns,         // filter patterns
        k_desc,             // filter description
        0                   // allow multiple: no
    );

    if (!result) return false;
    snprintf(out, cap, "%s", result);
    return true;
}

bool EdFileDialog_Save(char *out, int cap, const char *startDir, const char *defaultName) {
    char def[512];
    MakeDefault(def, sizeof def, startDir, defaultName ? defaultName : "untitled.map");

    const char *result = tinyfd_saveFileDialog(
        "Save map as",      // title
        def,                // default path + filename
        k_nPatterns,        // number of filter patterns
        k_patterns,         // filter patterns
        k_desc              // filter description
    );

    if (!result) return false;
    snprintf(out, cap, "%s", result);
    return true;
}

bool EdFileDialog_SelectFolder(char *out, int cap, const char *title, const char *startDir) {
    const char *result = tinyfd_selectFolderDialog(
        title ? title : "Select folder",
        startDir ? startDir : ""
    );
    if (!result) return false;
    snprintf(out, cap, "%s", result);
    return true;
}
