// ============================================================================
//  props.c — game-side placeable-prop catalog. See props.h.
// ============================================================================
#include "props.h"

#include "content.h"   // Eng_ContentDirs, Eng_LoadModel, Eng_ModelGet
#include "deffile.h"   // Eng_DefForEachLine — the shared .prop/.mob reader

#include <stdio.h>
#include <stdlib.h>    // atof
#include <string.h>

static GamePropDef g_props[GAME_MAX_PROPS];
static int         g_propCount = 0;

// Parse state: the def-in-progress plus the model basename (resolved after the
// whole file is read, so field order in the .prop doesn't matter).
typedef struct { GamePropDef def; char model[128]; bool sawId; } PropParse;

static void PropLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    PropParse *pp = (PropParse *)user;
    const char *k = toks[0];
    if (strcmp(k, "id") == 0 && n >= 2) {
        snprintf(pp->def.id, sizeof pp->def.id, "%s", toks[1]);
        pp->sawId = true;
    } else if (strcmp(k, "model") == 0 && n >= 2) {
        snprintf(pp->model, sizeof pp->model, "%s", toks[1]);
    } else if (strcmp(k, "model_scale") == 0 && n >= 2) {
        pp->def.modelScale = (float)atof(toks[1]);
    } else if (strcmp(k, "foot_y") == 0 && n >= 2) {
        pp->def.footY = (float)atof(toks[1]);
    } else if (strcmp(k, "collide_half") == 0 && n >= 4) {
        pp->def.halfExtent = (Vector3){ (float)atof(toks[1]),
                                        (float)atof(toks[2]),
                                        (float)atof(toks[3]) };
    }
    // id/name are the editor's identity; everything else is ours.
}

void Props_Load(void) {
    g_propCount = 0;

    char dirs[4][512];
    int nDirs = Eng_ContentDirs("props", dirs, 4);

    for (int d = 0; d < nDirs && g_propCount < GAME_MAX_PROPS; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ".prop", true);
        for (unsigned i = 0; i < files.count && g_propCount < GAME_MAX_PROPS; i++) {
            char *text = LoadFileText(files.paths[i]);
            if (!text) continue;
            PropParse pp = { .def = { .modelScale = 1.0f }, .model = "", .sawId = false };
            Eng_DefForEachLine(text, PropLineCb, &pp);
            UnloadFileText(text);
            if (!pp.sawId) continue;

            // De-dup by id so a game prop shadows a same-id library one.
            bool dup = false;
            for (int j = 0; j < g_propCount; j++)
                if (strcmp(g_props[j].id, pp.def.id) == 0) { dup = true; break; }
            if (dup) continue;

            if (pp.model[0]) {
                EngModel h = Eng_LoadModel(pp.model);
                Model *m = Eng_ModelGet(h);
                if (m) { pp.def.model = *m; pp.def.loaded = true; }
                else fprintf(stderr, "prop: model '%s' for '%s' not found — cube fallback\n",
                             pp.model, pp.def.id);
            }
            g_props[g_propCount++] = pp.def;
        }
        UnloadDirectoryFiles(files);
    }
    fprintf(stderr, "props: loaded %d placeable prop definition(s)\n", g_propCount);
}

void Props_Unload(void) {
    for (int i = 0; i < g_propCount; i++) g_props[i].loaded = false;
    g_propCount = 0;   // Eng_ContentFlush (Assets_Unload) frees the GL models
}

void Props_ApplyWorldShader(Shader sh) {
    for (int i = 0; i < g_propCount; i++) {
        if (!g_props[i].loaded) continue;
        for (int m = 0; m < g_props[i].model.materialCount; m++)
            g_props[i].model.materials[m].shader = sh;
    }
}

int Props_Count(void) { return g_propCount; }

const GamePropDef *Props_At(int idx) {
    return (idx >= 0 && idx < g_propCount) ? &g_props[idx] : NULL;
}

int Props_IndexByName(const char *id) {
    for (int i = 0; i < g_propCount; i++)
        if (strcmp(g_props[i].id, id) == 0) return i;
    return -1;
}
