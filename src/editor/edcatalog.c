// ============================================================================
//  edcatalog.c — content catalog scanning for the editor's placement palette.
//
//  Split out of edscene.c. The editor reads data/{mobs,props,perks,weapons}
//  *.def files through the engine's shared deffile reader, taking ONLY the
//  placeable identity (id + display name + marker colour) and ignoring the
//  game-only rest (behaviour / model / collision / stats) — that seam is what
//  keeps the editor engine-clean. New catalog kinds get added here, not in the
//  scene/viewport core.
// ============================================================================

#include "edscene.h"

#include "deffile.h"   // shared .def reader (engine-side, game-clean)
#include "content.h"   // Eng_ContentDirs

#include <stdio.h>
#include <string.h>

// ---- mob catalog scan (data/mobs/*/*.mob via the shared deffile reader) -----

// Accumulator for one .mob file: we read only the editor-relevant fields.
// `modelFile` is the bare `model` value (e.g. "zombie.glb"); the scan loop joins
// it with the def file's directory to make a resolvable thumbnail path.
typedef struct { EdMobDef def; bool sawId; char modelFile[64]; } EdMobParse;

static void EdMobLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    EdMobParse *mp = (EdMobParse *)user;
    const char *k = toks[0];
    if (strcmp(k, "id") == 0 && n >= 2) {
        snprintf(mp->def.id, sizeof mp->def.id, "%s", toks[1]);
        mp->sawId = true;
    } else if (strcmp(k, "name") == 0 && n >= 2) {
        snprintf(mp->def.name, sizeof mp->def.name, "%s", toks[1]);
    } else if (strcmp(k, "model") == 0 && n >= 2) {
        snprintf(mp->modelFile, sizeof mp->modelFile, "%s", toks[1]);
    } else if (strcmp(k, "tint") == 0 && n >= 4) {
        unsigned char rgba[4];
        Eng_DefParseColor(toks, n, 1, rgba);
        mp->def.tint = (Color){ rgba[0], rgba[1], rgba[2], rgba[3] };
    }
    // behaviour / stats are game-only — the editor never reads them.
}

// Store the def's bare `model` filename (e.g. "zombie.glb"). The browser's
// thumbnail renderer (EdThumb_Model → Eng_LoadModel) resolves it the same way
// the game does — by prepending the content "models/" prefix — so a bare name is
// exactly what loads. Leaves dst empty when there's no model (browser → icon).
static void EdBuildModelPath(char *dst, int cap, const char *defFilePath, const char *modelFile) {
    (void)defFilePath;
    snprintf(dst, cap, "%s", modelFile);
}

void EdScene_ScanMobs(EdScene *s) {
    s->mobDefCount = 0;

    // Collect the ordered root directories for "mobs" via the engine resolver
    // (game root first, then library, then data/ dev fallbacks).
    char dirs[4][512];
    int nDirs = Eng_ContentDirs("mobs", dirs, 4);

    // Track which mob ids we have already added so that a game entry silently
    // shadows a same-named library/data entry (de-dup by id, first-seen wins).
    char seen[ED_MAX_MOBDEFS][ED_MOBID_LEN];
    int  nSeen = 0;

    for (int d = 0; d < nDirs && s->mobDefCount < ED_MAX_MOBDEFS; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ".mob", true);
        for (unsigned i = 0; i < files.count && s->mobDefCount < ED_MAX_MOBDEFS; i++) {
            char *text = LoadFileText(files.paths[i]);
            if (!text) continue;
            EdMobParse mp = { .def = { .name = "", .tint = { 200, 80, 80, 255 } }, .sawId = false, .modelFile = "" };
            Eng_DefForEachLine(text, EdMobLineCb, &mp);
            UnloadFileText(text);
            if (!mp.sawId) continue;
            // De-dup: skip if we already added a mob with this id.
            bool dup = false;
            for (int k = 0; k < nSeen; k++) {
                if (strcmp(seen[k], mp.def.id) == 0) { dup = true; break; }
            }
            if (dup) continue;
            if (!mp.def.name[0]) snprintf(mp.def.name, sizeof mp.def.name, "%s", mp.def.id);
            EdBuildModelPath(mp.def.model, sizeof mp.def.model, files.paths[i], mp.modelFile);
            s->mobDefs[s->mobDefCount++] = mp.def;
            if (nSeen < ED_MAX_MOBDEFS) snprintf(seen[nSeen++], ED_MOBID_LEN, "%s", mp.def.id);
        }
        UnloadDirectoryFiles(files);
    }

    // Always offer at least the zombie so the palette has a mob tool even when
    // run from a directory without a data/mobs catalog.
    if (s->mobDefCount == 0) {
        EdMobDef z = { .id = "ZOMBIE", .name = "Zombie spawn", .tint = { 200, 80, 80, 255 } };
        s->mobDefs[s->mobDefCount++] = z;
    }
    // Default the active mob tool to the first scanned mob.
    snprintf(s->placeMobId, sizeof s->placeMobId, "%s", s->mobDefs[0].id);
}

// ---- id+name catalogs (props / perks / wallbuy weapons) --------------------
// All three are the editor's view of a content catalog: read the placeable
// identity (id + display name) and ignore the game-only rest (model/collision/
// effect/stats) — the seam. One generic scanner serves all three; only the
// subdir + extension differ.
typedef struct { EdPropDef def; bool sawId; char modelFile[64]; } EdPropParse;

static void EdPropLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    EdPropParse *pp = (EdPropParse *)user;
    const char *k = toks[0];
    if (strcmp(k, "id") == 0 && n >= 2) {
        snprintf(pp->def.id, sizeof pp->def.id, "%s", toks[1]);
        pp->sawId = true;
    } else if (strcmp(k, "model") == 0 && n >= 2) {
        snprintf(pp->modelFile, sizeof pp->modelFile, "%s", toks[1]);
    } else if (strcmp(k, "name") == 0 && n >= 2) {
        // Join the remaining tokens so multi-word labels ("Sandbag stack") survive.
        size_t cap = sizeof pp->def.name, off = 0;
        for (int t = 1; t < n && off < cap - 1; t++) {
            int w = snprintf(pp->def.name + off, cap - off, "%s%s", t > 1 ? " " : "", toks[t]);
            if (w < 0) break;
            off += (size_t)w;
        }
    }
    // everything else (model / collide_half / cost / stats) is game-only.
}

// Scan <subdir>/*<ext> across the content roots into out[] (id + label), de-dup
// by id (game shadows library). Returns the count written. `placeId` (if non-
// NULL) is seeded with the first def's id as the default armed tool.
static int EdScanIdNameCatalog(const char *subdir, const char *ext,
                               EdPropDef out[], int max,
                               char *placeId, int placeCap) {
    int count = 0;
    char dirs[4][512];
    int nDirs = Eng_ContentDirs(subdir, dirs, 4);
    char seen[ED_MAX_PROPDEFS][ED_PROPID_LEN];
    int  nSeen = 0;

    for (int d = 0; d < nDirs && count < max; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ext, true);
        for (unsigned i = 0; i < files.count && count < max; i++) {
            char *text = LoadFileText(files.paths[i]);
            if (!text) continue;
            EdPropParse pp = { .def = { .name = "" }, .sawId = false, .modelFile = "" };
            Eng_DefForEachLine(text, EdPropLineCb, &pp);
            UnloadFileText(text);
            if (!pp.sawId) continue;
            bool dup = false;
            for (int k = 0; k < nSeen && k < ED_MAX_PROPDEFS; k++)
                if (strcmp(seen[k], pp.def.id) == 0) { dup = true; break; }
            if (dup) continue;
            if (!pp.def.name[0]) snprintf(pp.def.name, sizeof pp.def.name, "%s", pp.def.id);
            EdBuildModelPath(pp.def.model, sizeof pp.def.model, files.paths[i], pp.modelFile);
            out[count++] = pp.def;
            if (nSeen < ED_MAX_PROPDEFS) snprintf(seen[nSeen++], ED_PROPID_LEN, "%s", pp.def.id);
        }
        UnloadDirectoryFiles(files);
    }
    if (placeId && count > 0) snprintf(placeId, placeCap, "%s", out[0].id);
    return count;
}

void EdScene_ScanProps(EdScene *s) {
    s->propDefCount = EdScanIdNameCatalog("props", ".prop", s->propDefs,
                                          ED_MAX_PROPDEFS, s->placePropId, sizeof s->placePropId);
}
void EdScene_ScanPerks(EdScene *s) {
    s->perkDefCount = EdScanIdNameCatalog("perks", ".perk", s->perkDefs,
                                          ED_MAX_BUYDEFS, s->placePerkId, sizeof s->placePerkId);
}
void EdScene_ScanWeapons(EdScene *s) {
    s->weaponDefCount = EdScanIdNameCatalog("weapons", ".weapon", s->weaponDefs,
                                            ED_MAX_BUYDEFS, s->placeWeaponId, sizeof s->placeWeaponId);
}

void EdScene_RescanContent(EdScene *s) {
    EdScene_ScanMobs(s);
    EdScene_ScanProps(s);
    EdScene_ScanPerks(s);
    EdScene_ScanWeapons(s);
    EdAssets_Scan(&s->assets);
}
