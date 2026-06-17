// cfg.c — implementation of the key=value config helper. C stdlib only.
#include "cfg.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void TrimEol(char *s) {
    char *nl = strchr(s, '\n'); if (nl) *nl = 0;
    char *cr = strchr(s, '\r'); if (cr) *cr = 0;
}

// Trim leading/trailing ASCII whitespace in place; returns the start pointer.
static char *Trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

bool EngCfg_Load(EngCfg *c, const char *const *candidates, int nCandidates) {
    c->count = 0;
    c->path[0] = 0;
    if (nCandidates > 0) {
        strncpy(c->path, candidates[0], sizeof c->path - 1);
        c->path[sizeof c->path - 1] = 0;
    }

    FILE *f = NULL;
    for (int i = 0; i < nCandidates; i++) {
        f = fopen(candidates[i], "r");
        if (f) {
            strncpy(c->path, candidates[i], sizeof c->path - 1);
            c->path[sizeof c->path - 1] = 0;
            break;
        }
    }
    if (!f) return false;

    char line[ENGCFG_KEY_LEN + ENGCFG_VAL_LEN + 8];
    while (fgets(line, sizeof line, f)) {
        TrimEol(line);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;   // strip comments
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = Trim(line);
        char *val = Trim(eq + 1);
        if (!*key) continue;
        if (c->count >= ENGCFG_MAX_PAIRS) break;
        EngCfgPair *p = &c->pairs[c->count++];
        strncpy(p->key, key, sizeof p->key - 1); p->key[sizeof p->key - 1] = 0;
        strncpy(p->val, val, sizeof p->val - 1); p->val[sizeof p->val - 1] = 0;
    }
    fclose(f);
    return true;
}

static const char *Find(const EngCfg *c, const char *key) {
    for (int i = 0; i < c->count; i++)
        if (strcmp(c->pairs[i].key, key) == 0) return c->pairs[i].val;
    return NULL;
}

float EngCfg_Float(const EngCfg *c, const char *key, float def) {
    const char *v = Find(c, key); return v ? (float)atof(v) : def;
}
int EngCfg_Int(const EngCfg *c, const char *key, int def) {
    const char *v = Find(c, key); return v ? atoi(v) : def;
}
bool EngCfg_Bool(const EngCfg *c, const char *key, bool def) {
    const char *v = Find(c, key); return v ? (atoi(v) != 0) : def;
}
const char *EngCfg_Str(const EngCfg *c, const char *key, const char *def) {
    const char *v = Find(c, key); return v ? v : def;
}

FILE *EngCfg_BeginSave(const EngCfg *c, const char *header) {
    const char *path = c->path[0] ? c->path : "config.cfg";
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    if (header) fprintf(f, "# %s\n", header);
    return f;
}
void EngCfg_PutFloat(FILE *f, const char *key, float v) { fprintf(f, "%s=%.6g\n", key, v); }
void EngCfg_PutInt  (FILE *f, const char *key, int   v) { fprintf(f, "%s=%d\n",   key, v); }
void EngCfg_PutBool (FILE *f, const char *key, bool  v) { fprintf(f, "%s=%d\n",   key, v ? 1 : 0); }
void EngCfg_PutStr  (FILE *f, const char *key, const char *v) { fprintf(f, "%s=%s\n", key, v ? v : ""); }
void EngCfg_EndSave (FILE *f) { if (f) fclose(f); }
