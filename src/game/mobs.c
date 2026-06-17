// ============================================================================
//  mobs.c — the data-driven mob catalog loader (see mobs.h).
//
//  Mirrors weapons.c: scan a data/ folder, parse each def through the engine's
//  shared deffile reader, and populate a flat table. No GL / model loading
//  happens here (render.c owns that) — the catalog is pure data so a headless
//  --list-mobs run and the engine-only editor can both consume it.
// ============================================================================

#include "mobs.h"
#include "deffile.h"   // shared key=value reader

#include "raylib.h"    // LoadFileText / UnloadFileText / Directory* helpers
#include <stdio.h>
#include <string.h>

static MobDef g_mobs[MAX_MOB_DEFS];
static int    g_mobCount = 0;

// ---- per-file parse --------------------------------------------------------

typedef struct {
    MobDef     *d;
    const char *path;
    bool        sawId;
} MobParseCtx;

static void CopyStr(char *dst, int cap, const char *src) {
    snprintf(dst, cap, "%s", src);
}

static void MobLineCb(int lineNo, int n, char **toks, void *user) {
    MobParseCtx *ctx = (MobParseCtx *)user;
    MobDef *d = ctx->d;
    const char *k = toks[0];

    if (strcmp(k, "id") == 0 && n >= 2) {
        CopyStr(d->id, sizeof d->id, toks[1]);
        ctx->sawId = true;
        return;
    }
    if (!ctx->sawId) {
        fprintf(stderr, "mob: %s line %d: '%s' before 'id'\n", ctx->path, lineNo, k);
        return;
    }

    if      (strcmp(k, "name") == 0 && n >= 2)        CopyStr(d->name, sizeof d->name, toks[1]);
    else if (strcmp(k, "category") == 0 && n >= 2)    CopyStr(d->category, sizeof d->category, toks[1]);
    else if (strcmp(k, "model") == 0 && n >= 2)       CopyStr(d->model, sizeof d->model, toks[1]);
    else if (strcmp(k, "model_scale") == 0 && n >= 2) Eng_DefParseFloat(toks[1], &d->modelScale);
    else if (strcmp(k, "model_yaw") == 0 && n >= 2)   Eng_DefParseFloat(toks[1], &d->modelYaw);
    else if (strcmp(k, "tint") == 0 && n >= 4) {
        unsigned char rgba[4];
        Eng_DefParseColor(toks, n, 1, rgba);
        d->tint = (Color){ rgba[0], rgba[1], rgba[2], rgba[3] };
    }
    else if (strcmp(k, "anim_walk") == 0 && n >= 2)   CopyStr(d->animWalk, sizeof d->animWalk, toks[1]);
    else if (strcmp(k, "anim_attack") == 0 && n >= 2) CopyStr(d->animAttack, sizeof d->animAttack, toks[1]);
    else if (strcmp(k, "anim_death") == 0 && n >= 2)  CopyStr(d->animDeath, sizeof d->animDeath, toks[1]);
    else if (strcmp(k, "anim_idle") == 0 && n >= 2)   CopyStr(d->animIdle, sizeof d->animIdle, toks[1]);
    else if (strcmp(k, "behaviour") == 0 && n >= 2)   CopyStr(d->behaviour, sizeof d->behaviour, toks[1]);
    else if (strcmp(k, "move_speed") == 0 && n >= 2)  Eng_DefParseFloat(toks[1], &d->moveSpeed);
    else if (strcmp(k, "path_repath") == 0 && n >= 2) Eng_DefParseFloat(toks[1], &d->pathRepath);
    else if (strcmp(k, "health_base") == 0 && n >= 2) Eng_DefParseInt(toks[1], &d->healthBase);
    else if (strcmp(k, "damage") == 0 && n >= 2)      Eng_DefParseInt(toks[1], &d->damage);
    else if (strcmp(k, "attack_windup") == 0 && n >= 2) Eng_DefParseFloat(toks[1], &d->attackWindup);
    else if (strcmp(k, "attack_range") == 0 && n >= 2)  Eng_DefParseFloat(toks[1], &d->attackRange);
    // Stats the editor/game don't read yet but a .mob may legally carry
    // (sfx_*, health curves, …) are tolerated silently rather than warned, so
    // authors can stage fields ahead of code. Unknown KEYS still warn:
    else if (strncmp(k, "sfx_", 4) == 0) { /* reserved for audio wiring */ }
    else fprintf(stderr, "mob: %s line %d: unknown key '%s'\n", ctx->path, lineNo, k);
}

// Parse one .mob file into `out`. Returns true if an `id` was found.
static bool Mob_ParseFile(const char *path, MobDef *out) {
    char *text = LoadFileText(path);
    if (!text) { fprintf(stderr, "mob: cannot read %s\n", path); return false; }

    *out = (MobDef){ 0 };
    out->modelScale = 1.0f;
    out->tint       = (Color){ 200, 200, 200, 255 };
    CopyStr(out->behaviour, sizeof out->behaviour, "chaser");

    MobParseCtx ctx = { .d = out, .path = path, .sawId = false };
    Eng_DefForEachLine(text, MobLineCb, &ctx);
    UnloadFileText(text);

    if (!ctx.sawId) { fprintf(stderr, "mob: %s missing 'id' field\n", path); return false; }
    out->valid = true;
    return true;
}

// ---- catalog ---------------------------------------------------------------

void Mobs_Load(void) {
    g_mobCount = 0;

    static const char *prefixes[] = { "data/mobs", "../data/mobs", "./data/mobs" };
    for (size_t p = 0; p < sizeof prefixes / sizeof prefixes[0]; p++) {
        if (!DirectoryExists(prefixes[p])) continue;
        FilePathList files = LoadDirectoryFilesEx(prefixes[p], ".mob", true);
        for (unsigned i = 0; i < files.count && g_mobCount < MAX_MOB_DEFS; i++) {
            MobDef d;
            if (Mob_ParseFile(files.paths[i], &d)) g_mobs[g_mobCount++] = d;
        }
        UnloadDirectoryFiles(files);
        if (g_mobCount > 0) break;   // first existing root wins (mirrors weapons)
    }

    fprintf(stderr, "mob: parsed %d .mob file(s)\n", g_mobCount);

    MobIssue issues[32];
    int n = Mob_Validate(issues, 32);
    for (int i = 0; i < n && i < 32; i++)
        fprintf(stderr, "mob: %s %s%s%s\n",
                issues[i].severity == MOB_ERROR ? "ERROR" : "warn",
                issues[i].mobId[0] ? issues[i].mobId : "",
                issues[i].mobId[0] ? ": " : "",
                issues[i].msg);
}

int Mob_Count(void) { return g_mobCount; }

const MobDef *Mob_Get(int index) {
    if (index < 0 || index >= g_mobCount) return NULL;
    return &g_mobs[index];
}

int Mob_FindIndex(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < g_mobCount; i++)
        if (strcmp(g_mobs[i].id, id) == 0) return i;
    return -1;
}

const MobDef *Mob_Find(const char *id) {
    int i = Mob_FindIndex(id);
    return i < 0 ? NULL : &g_mobs[i];
}

// ---- validation ------------------------------------------------------------

static bool ModelResolves(const char *model) {
    if (!model || !model[0]) return false;
    // Probe the same data/models roots the engine content loader uses, plus a
    // bare relative path, so a headless run can verify the asset exists.
    char path[512];
    static const char *roots[] = { "data/models/", "../data/models/", "./data/models/", "" };
    for (size_t r = 0; r < sizeof roots / sizeof roots[0]; r++) {
        snprintf(path, sizeof path, "%s%s", roots[r], model);
        if (FileExists(path)) return true;
    }
    return false;
}

int Mob_Validate(MobIssue *out, int max) {
    int total = 0;
    #define ADD(sev, mid, ...) do {                                   \
        if (total < max) {                                            \
            out[total].severity = (sev);                              \
            snprintf(out[total].mobId, sizeof out[total].mobId,       \
                     "%.*s", (int)sizeof(out[total].mobId) - 1, (mid)); \
            snprintf(out[total].msg, sizeof out[total].msg, __VA_ARGS__);     \
        }                                                             \
        total++;                                                      \
    } while (0)

    if (g_mobCount == 0) ADD(MOB_WARN, "", "no .mob files loaded (data/mobs/ missing or empty)");

    for (int i = 0; i < g_mobCount; i++) {
        const MobDef *d = &g_mobs[i];
        if (!d->name[0])        ADD(MOB_ERROR, d->id, "missing 'name'");
        if (!d->model[0])       ADD(MOB_ERROR, d->id, "missing 'model'");
        else if (!ModelResolves(d->model)) ADD(MOB_WARN, d->id, "model '%s' not found on disk", d->model);
        if (!d->behaviour[0])   ADD(MOB_ERROR, d->id, "missing 'behaviour'");
        if (d->healthBase <= 0) ADD(MOB_ERROR, d->id, "health_base must be > 0 (got %d)", d->healthBase);
        if (d->moveSpeed < 0)   ADD(MOB_ERROR, d->id, "move_speed must be >= 0");
        if (d->modelScale <= 0) ADD(MOB_ERROR, d->id, "model_scale must be > 0");
        // Duplicate id check (against earlier entries only).
        for (int j = 0; j < i; j++)
            if (strcmp(d->id, g_mobs[j].id) == 0) { ADD(MOB_ERROR, d->id, "duplicate id"); break; }
    }
    #undef ADD
    return total;
}
