#include "pack.h"

#include "content.h"   // Eng_LocateRoot
#include "deffile.h"   // Eng_DefForEachLine
#include "raylib.h"    // DirectoryExists, FileExists, LoadFileText, LoadDirectoryFiles, GetFileName

#include <stdio.h>
#include <stdlib.h>   // atoi
#include <string.h>

// ---------------------------------------------------------------------------
//  Eng_PackReadManifest
// ---------------------------------------------------------------------------

// deffile line callback: copy the recognised keys into the EngPackInfo. The
// description may be multi-word, so its tokens are re-joined with spaces (same
// idiom as edcatalog.c's multi-word `name`).
static void PackManifestLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    if (n < 2) return;
    EngPackInfo *p = (EngPackInfo *)user;
    const char *k = toks[0];
    if      (strcmp(k, "id")       == 0) snprintf(p->id,       sizeof p->id,       "%s", toks[1]);
    else if (strcmp(k, "name")     == 0) snprintf(p->name,     sizeof p->name,     "%s", toks[1]);
    else if (strcmp(k, "version")  == 0) p->version = atoi(toks[1]);
    else if (strcmp(k, "author")   == 0) snprintf(p->author,   sizeof p->author,   "%s", toks[1]);
    else if (strcmp(k, "requires") == 0) snprintf(p->requires, sizeof p->requires, "%s", toks[1]);
    else if (strcmp(k, "description") == 0) {
        size_t cap = sizeof p->description, off = 0;
        for (int t = 1; t < n && off < cap - 1; t++) {
            int w = snprintf(p->description + off, cap - off, "%s%s", t > 1 ? " " : "", toks[t]);
            if (w < 0) break;
            off += (size_t)w;
        }
    }
    // Unknown keys are ignored (forward compatibility).
}

bool Eng_PackReadManifest(const char *packDir, EngPackInfo *out) {
    if (!packDir || !out) return false;

    char manifest[768];
    snprintf(manifest, sizeof manifest, "%s/pack.manifest", packDir);
    if (!FileExists(manifest)) return false;

    char *text = LoadFileText(manifest);
    if (!text) return false;

    memset(out, 0, sizeof *out);
    out->version = 1;  // sensible default if absent
    snprintf(out->dir, sizeof out->dir, "%s", packDir);

    Eng_DefForEachLine(text, PackManifestLineCb, out);
    UnloadFileText(text);

    // The filesystem (the type subdirs) is the source of truth; an id is the
    // minimum we need to treat this as a real pack. Fall back to the folder
    // name when the manifest omits it.
    if (!out->id[0]) snprintf(out->id, sizeof out->id, "%s", GetFileName(packDir));
    if (!out->name[0]) snprintf(out->name, sizeof out->name, "%s", out->id);
    return true;
}

// ---------------------------------------------------------------------------
//  Eng_PackList
// ---------------------------------------------------------------------------

int Eng_PackList(EngPackInfo *out, int max) {
    if (!out || max <= 0) return 0;

    char packsRoot[512];
    if (!Eng_LocateRoot("packs", packsRoot, sizeof packsRoot)) return 0;

    int n = 0;
    FilePathList entries = LoadDirectoryFiles(packsRoot);
    for (unsigned int i = 0; i < entries.count && n < max; i++) {
        const char *path = entries.paths[i];
        if (!DirectoryExists(path)) continue;   // packs are folders
        EngPackInfo info;
        if (Eng_PackReadManifest(path, &info)) out[n++] = info;
    }
    UnloadDirectoryFiles(entries);
    return n;
}

// ---------------------------------------------------------------------------
//  Eng_PackDirs
// ---------------------------------------------------------------------------

int Eng_PackDirs(const char *packDir, const char *relSubdir,
                 char dirs[][512], int maxDirs) {
    if (!packDir || !relSubdir || maxDirs <= 0) return 0;
    char cand[512];
    snprintf(cand, sizeof cand, "%s/%s", packDir, relSubdir);
    if (!DirectoryExists(cand)) return 0;
    snprintf(dirs[0], 512, "%s", cand);
    return 1;
}
