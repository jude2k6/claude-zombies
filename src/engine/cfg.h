#ifndef SHOOTER_CFG_H
#define SHOOTER_CFG_H

// ============================================================================
//  cfg.h — a tiny key=value config reader/writer. Engine-side, game-clean
//  (C stdlib only), so any app (game, editor, future tools) can persist plain
//  text preference files in one shared format.
//
//  Format: one `key=value` per line, optional `#` comment lines, whitespace
//  around key/value trimmed. This matches the game's settings.cfg style so the
//  files are interchangeable and hand-editable.
//
//  Usage:
//    EngCfg c;
//    const char *paths[] = { "editor.cfg", "../editor.cfg" };
//    EngCfg_Load(&c, paths, 2);
//    float spd = EngCfg_Float(&c, "cam.flySpeed", 16.0f);
//    ...
//    FILE *f = EngCfg_BeginSave(&c, "My app prefs");
//    EngCfg_PutFloat(f, "cam.flySpeed", spd);
//    EngCfg_EndSave(f);
// ============================================================================

#include <stdio.h>
#include <stdbool.h>

#define ENGCFG_MAX_PAIRS 128
#define ENGCFG_KEY_LEN    64
#define ENGCFG_VAL_LEN   192

typedef struct { char key[ENGCFG_KEY_LEN]; char val[ENGCFG_VAL_LEN]; } EngCfgPair;

typedef struct {
    EngCfgPair pairs[ENGCFG_MAX_PAIRS];
    int        count;
    char       path[512];   // resolved load path, else candidates[0] (save target)
} EngCfg;

// Load `key=value` lines from the first existing candidate path. A missing file
// is NOT an error (the cfg is left empty); `c->path` is set to the resolved path
// when found, otherwise to candidates[0] so a later save lands somewhere sane.
// Returns true if a file was actually read.
bool EngCfg_Load(EngCfg *c, const char *const *candidates, int nCandidates);

// Typed lookups — return `def` when the key is absent or unparseable.
float       EngCfg_Float(const EngCfg *c, const char *key, float def);
int         EngCfg_Int  (const EngCfg *c, const char *key, int   def);
bool        EngCfg_Bool (const EngCfg *c, const char *key, bool  def);
const char *EngCfg_Str  (const EngCfg *c, const char *key, const char *def);

// Writing: open `c->path` for writing (emits `header` as a leading `# ` comment
// when non-NULL), put typed lines, then EngCfg_EndSave. BeginSave returns NULL
// on I/O error — guard it before putting.
FILE *EngCfg_BeginSave(const EngCfg *c, const char *header);
void  EngCfg_PutFloat(FILE *f, const char *key, float v);
void  EngCfg_PutInt  (FILE *f, const char *key, int   v);
void  EngCfg_PutBool (FILE *f, const char *key, bool  v);
void  EngCfg_PutStr  (FILE *f, const char *key, const char *v);
void  EngCfg_EndSave (FILE *f);

#endif // SHOOTER_CFG_H
