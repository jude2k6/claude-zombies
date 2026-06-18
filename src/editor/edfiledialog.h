// ============================================================================
//  edfiledialog.h — thin OS native file-dialog wrapper for the map editor.
//
//  Hides the backend (tinyfiledialogs / zenity fallback) from all callers.
//  Both functions filter to *.map files. `startDir` may be NULL. On cancel
//  (or failure) the function returns false and `out` is untouched.
// ============================================================================
#ifndef SHOOTER_EDFILEDIALOG_H
#define SHOOTER_EDFILEDIALOG_H

#include <stdbool.h>

// Open-file dialog — writes an absolute path into `out` (up to `cap` bytes).
// Returns false if the user cancelled.
bool EdFileDialog_Open(char *out, int cap, const char *startDir);

// Save-file dialog — `defaultName` is the suggested filename (basename only).
// Returns false if the user cancelled.
bool EdFileDialog_Save(char *out, int cap, const char *startDir, const char *defaultName);

// Folder-select dialog — title is shown in the OS dialog header.
// `startDir` may be NULL (defaults to the user's home dir).
// Writes the chosen folder path into `out`.  Returns false if cancelled.
bool EdFileDialog_SelectFolder(char *out, int cap, const char *title, const char *startDir);

#endif // SHOOTER_EDFILEDIALOG_H
