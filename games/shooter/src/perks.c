#include "perks.h"

#include "content.h"   // Eng_ContentDirs
#include "deffile.h"   // Eng_DefForEachLine / Eng_DefParseColor
#include "raylib.h"    // LoadDirectoryFilesEx, LoadFileText, DirectoryExists

#include <stdio.h>
#include <stdlib.h>    // atoi
#include <string.h>

// Default perk data — the historical hardcoded values, kept as the fallback
// when perks/*.perk is absent. Perks_Load() overlays the catalog on top.
PerkDef PERKS[PERK_COUNT] = {
    [PERK_JUG]    = { "Juggernog",   2500, (Color){220, 40, 40, 255} },
    [PERK_SPEED]  = { "Speed Cola",  3000, (Color){ 40,180, 80, 255} },
    [PERK_DTAP]   = { "Double Tap",  2000, (Color){240,180, 40, 255} },
    [PERK_STAMIN] = { "Stamin-Up",   2000, (Color){ 60,140,220, 255} },
};

// A perk's `id` (in its .perk file / the map's PERK <id>) → its PerkId slot.
// The set of perks is fixed by code because each has a hardcoded effect; the
// catalog only supplies their tunable data.
static int PerkIdToIdx(const char *id) {
    if (strcmp(id, "JUG")    == 0) return PERK_JUG;
    if (strcmp(id, "SPEED")  == 0) return PERK_SPEED;
    if (strcmp(id, "DTAP")   == 0) return PERK_DTAP;
    if (strcmp(id, "STAMIN") == 0) return PERK_STAMIN;
    return -1;
}

typedef struct {
    char  id[16], name[32];
    int   cost;  Color tint;
    bool  sawId, sawName, sawCost, sawTint;
} PerkParse;

static void PerkLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    PerkParse *pp = (PerkParse *)user;
    const char *k = toks[0];
    if (strcmp(k, "id") == 0 && n >= 2) {
        snprintf(pp->id, sizeof pp->id, "%s", toks[1]); pp->sawId = true;
    } else if (strcmp(k, "name") == 0 && n >= 2) {
        size_t cap = sizeof pp->name, off = 0;
        for (int t = 1; t < n && off < cap - 1; t++) {
            int w = snprintf(pp->name + off, cap - off, "%s%s", t > 1 ? " " : "", toks[t]);
            if (w < 0) break;
            off += (size_t)w;
        }
        pp->sawName = true;
    } else if (strcmp(k, "cost") == 0 && n >= 2) {
        pp->cost = atoi(toks[1]); pp->sawCost = true;
    } else if (strcmp(k, "tint") == 0 && n >= 5) {
        unsigned char rgba[4];
        Eng_DefParseColor(toks, n, 1, rgba);
        pp->tint = (Color){ rgba[0], rgba[1], rgba[2], rgba[3] }; pp->sawTint = true;
    }
}

void Perks_Load(void) {
    char dirs[4][512];
    int nDirs = Eng_ContentDirs("perks", dirs, 4);
    bool filled[PERK_COUNT] = { false };

    for (int d = 0; d < nDirs; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ".perk", true);
        for (unsigned i = 0; i < files.count; i++) {
            char *text = LoadFileText(files.paths[i]);
            if (!text) continue;
            PerkParse pp = { 0 };
            Eng_DefForEachLine(text, PerkLineCb, &pp);
            UnloadFileText(text);
            if (!pp.sawId) continue;
            int idx = PerkIdToIdx(pp.id);
            if (idx < 0 || filled[idx]) continue;   // unknown id, or game shadows library
            if (pp.sawName) snprintf(PERKS[idx].name, sizeof PERKS[idx].name, "%s", pp.name);
            if (pp.sawCost) PERKS[idx].cost = pp.cost;
            if (pp.sawTint) PERKS[idx].tint = pp.tint;
            filled[idx] = true;
        }
        UnloadDirectoryFiles(files);
    }
}

int Perk_EffMaxHP(Player *p) {
    return p->hasPerk[PERK_JUG] ? 250 : 100;
}

float Perk_EffMoveSpeed(Player *p) {
    return BASE_MOVE_SPEED * (p->hasPerk[PERK_STAMIN] ? 1.4f : 1.0f);
}
