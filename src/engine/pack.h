#ifndef SHOOTER_PACK_H
#define SHOOTER_PACK_H

// ============================================================================
//  pack.h — content-pack enumeration (docs/content-packs.md §2/§3).
//
//  A *pack* is a themed bundle of placeables (mobs, perks, weapons, props) plus
//  its own unique models/textures, living under the `packs/` content tier next
//  to the executable. Each pack folder carries a `pack.manifest` (deffile
//  format). Packs are IMPORT SOURCES ONLY — they are never loaded at runtime
//  (the runtime overlay stays game → stdlib, see content.h); this module just
//  enumerates them and reads their metadata so the editor can offer imports.
//
//  Engine-clean: depends on content.h (Eng_LocateRoot) + deffile.h + raylib FS
//  only. No game header (§2 cardinal rule).
// ============================================================================

#include <stdbool.h>

// One pack's manifest metadata plus the directory it lives in.
typedef struct {
    char id[64];
    char name[128];
    int  version;          // defaults to 1 when absent
    char author[64];
    char description[256];
    char requires[64];     // declared dependency, e.g. "stdlib" (optional)
    char dir[512];         // the pack root (folder containing pack.manifest)
} EngPackInfo;

// Read <packDir>/pack.manifest into *out (sets out->dir = packDir). Missing
// fields keep their zero/default values (version defaults to 1). Returns false
// if the manifest is missing or unreadable.
bool Eng_PackReadManifest(const char *packDir, EngPackInfo *out);

// Enumerate the available import-source packs: locate the "packs" tier via
// Eng_LocateRoot, scan its immediate subdirectories, and read each one's
// pack.manifest. Subdirs without a readable manifest are skipped. Fills out[]
// with up to `max` entries; returns the count written.
int Eng_PackList(EngPackInfo *out, int max);

// Fill dirs[] with the existing <packDir>/<relSubdir> directory (0 or 1 entry)
// — the per-pack analogue of Eng_ContentDirs, scoped to one pack so a caller can
// enumerate a single pack's mobs/perks/weapons/props. Returns the count written.
int Eng_PackDirs(const char *packDir, const char *relSubdir,
                 char dirs[][512], int maxDirs);

#endif // SHOOTER_PACK_H
