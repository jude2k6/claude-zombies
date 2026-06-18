#include "content.h"
#include "gfx.h"    // Eng_GfxDefaultShaderId() — load-failure check
#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
//  Content roots — the game-over-library overlay (docs/game-projects.md §3)
// ============================================================================
//
//  Every content lookup resolves against an ordered root stack:
//      1. <game>     the active game folder    (writable, wins)
//      2. <library>  the engine stock library  (read-only fallback)
//      3. the legacy data/ dev fallbacks       (running from build/src tree)
//
//  Both roots are unset by default, which leaves only the data/ fallbacks —
//  identical to the pre-overlay behaviour, so this is pure plumbing until a
//  host calls Eng_SetGameRoot / Eng_SetLibraryRoot.  A relative content path
//  ("models/zombie.glb", "mobs/zombie/zombie.mob", "maps/arena.map") means the
//  same asset regardless of which root answers; whether you've imported it just
//  decides which copy resolves first.

static char s_gameRoot[512] = "";   // empty == unset
static char s_libRoot [512] = "";   // empty == unset

// Legacy dev fallbacks, kept last so a build/source-tree checkout still runs
// with no roots configured.  These carry the category subdir already.
static const char *DATA_FALLBACKS[] = { "data", "../data", "./data" };

#define NARR(a) ((int)(sizeof(a)/sizeof(a)[0]))

void Eng_SetGameRoot(const char *dir) {
    snprintf(s_gameRoot, sizeof s_gameRoot, "%s", dir ? dir : "");
}
void Eng_SetLibraryRoot(const char *dir) {
    snprintf(s_libRoot, sizeof s_libRoot, "%s", dir ? dir : "");
}
const char *Eng_GetGameRoot(void)    { return s_gameRoot[0] ? s_gameRoot : NULL; }
const char *Eng_GetLibraryRoot(void) { return s_libRoot [0] ? s_libRoot  : NULL; }

// Build the ordered list of root directory prefixes to probe (game, library,
// then the data/ fallbacks).  Returns the count written into `out` (sized for
// at least 2 + NARR(DATA_FALLBACKS)).
static int collect_roots(const char *out[]) {
    int n = 0;
    if (s_gameRoot[0]) out[n++] = s_gameRoot;
    if (s_libRoot [0]) out[n++] = s_libRoot;
    for (int i = 0; i < NARR(DATA_FALLBACKS); i++) out[n++] = DATA_FALLBACKS[i];
    return n;
}

// Resolve a root-relative content path ("models/x.glb", "weapons/p/p.weapon",
// "maps/a.map") against the root stack.  Writes the first existing path into
// `buf` and returns it, or NULL if nothing matched.
static const char *resolve_rel(const char *rel, char *buf, int bufsz) {
    const char *roots[2 + NARR(DATA_FALLBACKS)];
    int nr = collect_roots(roots);
    for (int i = 0; i < nr; i++) {
        snprintf(buf, (size_t)bufsz, "%s/%s", roots[i], rel);
        if (FileExists(buf)) return buf;
    }
    return NULL;
}

// ============================================================================
//  Eng_ResolveAssetPath — public helper
// ============================================================================

// Generic resolver for callers that hand us a root-relative path.  Order:
//   1. as-is (absolute path, or a full "data/..." path from a legacy caller),
//   2. the root stack (game, library, data/ fallbacks),
//   3. the bare "../" / "./" dev prefixes (a full path one level up).
const char *Eng_ResolveAssetPath(const char *rel, char *buf, int bufsz) {
    if (!rel || !buf || bufsz <= 0) return NULL;
    // Try as-is first (absolute path, or already contains a prefix).
    if (FileExists(rel)) {
        snprintf(buf, (size_t)bufsz, "%s", rel);
        return buf;
    }
    if (resolve_rel(rel, buf, bufsz)) return buf;
    // Bare dev prefixes for a caller that already embedded "data/" in `rel`.
    const char *bare[] = { "../", "./" };
    for (int i = 0; i < NARR(bare); i++) {
        snprintf(buf, (size_t)bufsz, "%s%s", bare[i], rel);
        if (FileExists(buf)) return buf;
    }
    return NULL;
}

// ============================================================================
//  Eng_ContentDirs — two-pass directory scan support
// ============================================================================

// Fill `dirs` with the existing directory paths for content subdir `relSubdir`
// (e.g. "maps", "mobs"), game root first, then library, then data/ fallbacks.
// Callers scan each in order and de-dup entry names themselves so that a game
// entry shadows a same-named library entry.  Returns the count written.
int Eng_ContentDirs(const char *relSubdir, char dirs[][512], int maxDirs) {
    if (!relSubdir || maxDirs <= 0) return 0;
    const char *roots[2 + NARR(DATA_FALLBACKS)];
    int nr = collect_roots(roots);
    int n = 0;
    for (int i = 0; i < nr && n < maxDirs; i++) {
        char cand[512];
        snprintf(cand, sizeof cand, "%s/%s", roots[i], relSubdir);
        if (DirectoryExists(cand)) {
            snprintf(dirs[n], 512, "%s", cand);
            n++;
        }
    }
    return n;
}

// ============================================================================
//  Registry tables — models, textures, shaders, clip sets
// ============================================================================

#define MAX_MODELS    128
#define MAX_TEXTURES  128
#define MAX_SHADERS    32
#define MAX_CLIPSETS   32

// Each entry stores a copy of the resolved path for dedup, plus a refcount:
// a dedup-hit during load increments it instead of allocating a new slot;
// Eng_Unload*() decrements it and only frees the raylib resource (and frees
// the slot for reuse) when it reaches zero.  See content.h "Flush /
// lifecycle" for the full rule.
typedef struct {
    char  path[512];
    Model model;
    bool  used;
    int   refcount;
} ModelEntry;

typedef struct {
    char      path[512];
    Texture2D tex;
    bool      used;
    int       refcount;
} TexEntry;

typedef struct {
    char   vsPath[512]; // may be empty for null-VS shaders
    char   fsPath[512];
    Shader shader;
    bool   used;
    int    refcount;
} ShaderEntry;

typedef struct {
    char  path[512];
    Model model;   // glTF/GLB — Model contains bones + animations
    bool  used;
    int   refcount;
} ClipSetEntry;

static ModelEntry   s_models  [MAX_MODELS];
static int          s_modelCount   = 0;
static TexEntry     s_textures[MAX_TEXTURES];
static int          s_texCount     = 0;
static ShaderEntry  s_shaders [MAX_SHADERS];
static int          s_shaderCount  = 0;
static ClipSetEntry s_clipsets[MAX_CLIPSETS];
static int          s_clipCount    = 0;

// ============================================================================
//  Eng_LoadModel
// ============================================================================

EngModel Eng_LoadModel(const char *path) {
    if (!path || path[0] == '\0') return (EngModel){0};

    // Resolve "models/<path>" against the root stack (game, library, data/).
    char resolved[512];
    char rel[512];
    snprintf(rel, sizeof rel, "models/%s", path);
    bool found = (resolve_rel(rel, resolved, sizeof resolved) != NULL);
    // If not found under models/, try as a raw/full path.
    if (!found && FileExists(path)) {
        snprintf(resolved, sizeof resolved, "%s", path);
        found = true;
    }
    if (!found) {
        fprintf(stderr, "content: model '%s' not found\n", path);
        return (EngModel){0};
    }

    // Dedup by resolved path.
    for (int i = 0; i < s_modelCount; i++) {
        if (s_models[i].used && strcmp(s_models[i].path, resolved) == 0) {
            s_models[i].refcount++;
            return (EngModel){ (uint32_t)(i + 1) };
        }
    }

    // Look for a freed slot to recycle before growing the table.
    int slot = -1;
    for (int i = 0; i < s_modelCount; i++) {
        if (!s_models[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        // Capacity check.
        if (s_modelCount >= MAX_MODELS) {
            fprintf(stderr, "content: model registry full (cap=%d)\n", MAX_MODELS);
            return (EngModel){0};
        }
        slot = s_modelCount++;
    }

    s_models[slot].model = LoadModel(resolved);
    if (s_models[slot].model.meshCount == 0) {
        fprintf(stderr, "content: model '%s' loaded but has no meshes\n", resolved);
        if (slot == s_modelCount - 1) s_modelCount--;  // don't keep the empty slot
        return (EngModel){0};
    }
    snprintf(s_models[slot].path, sizeof s_models[slot].path, "%s", resolved);
    s_models[slot].used = true;
    s_models[slot].refcount = 1;
    fprintf(stderr, "model: loaded %s (meshes=%d)\n", resolved,
            s_models[slot].model.meshCount);
    return (EngModel){ (uint32_t)(slot + 1) };
}

Model *Eng_ModelGet(EngModel h) {
    if (h.id == 0 || h.id > (uint32_t)s_modelCount) return NULL;
    int idx = (int)h.id - 1;
    if (!s_models[idx].used) return NULL;
    return &s_models[idx].model;
}

void Eng_UnloadModel(EngModel h) {
    if (h.id == 0 || h.id > (uint32_t)s_modelCount) return;
    int idx = (int)h.id - 1;
    if (!s_models[idx].used) return;  // already freed: silent no-op
    if (--s_models[idx].refcount > 0) return;
    UnloadModel(s_models[idx].model);
    s_models[idx].used = false;
    s_models[idx].path[0] = '\0';
    s_models[idx].refcount = 0;
}

bool Eng_ReloadModel(EngModel h) {
    if (h.id == 0 || h.id > (uint32_t)s_modelCount) return false;
    int idx = (int)h.id - 1;
    if (!s_models[idx].used) return false;
    Model fresh = LoadModel(s_models[idx].path);
    if (fresh.meshCount == 0) {
        fprintf(stderr, "content: reload model '%s' failed, keeping old data\n",
                s_models[idx].path);
        UnloadModel(fresh);
        return false;
    }
    UnloadModel(s_models[idx].model);
    s_models[idx].model = fresh;
    fprintf(stderr, "model: reloaded %s (meshes=%d)\n", s_models[idx].path,
            fresh.meshCount);
    return true;
}

// ============================================================================
//  Eng_LoadTexture  (also used by Eng_LoadTextureByName)
// ============================================================================

// Internal: load from a fully resolved path, applying standard wrap/mipmap.
static EngTexture LoadTextureFromResolved(const char *resolved) {
    // Dedup.
    for (int i = 0; i < s_texCount; i++) {
        if (s_textures[i].used && strcmp(s_textures[i].path, resolved) == 0) {
            s_textures[i].refcount++;
            return (EngTexture){ (uint32_t)(i + 1) };
        }
    }
    // Look for a freed slot to recycle before growing the table.
    int slot = -1;
    for (int i = 0; i < s_texCount; i++) {
        if (!s_textures[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_texCount >= MAX_TEXTURES) {
            fprintf(stderr, "content: texture registry full (cap=%d)\n", MAX_TEXTURES);
            return (EngTexture){0};
        }
        slot = s_texCount++;
    }
    s_textures[slot].tex = LoadTexture(resolved);
    if (s_textures[slot].tex.id == 0) {
        fprintf(stderr, "content: texture '%s' loaded but id==0\n", resolved);
        if (slot == s_texCount - 1) s_texCount--;
        return (EngTexture){0};
    }
    SetTextureWrap(s_textures[slot].tex, TEXTURE_WRAP_REPEAT);
    GenTextureMipmaps(&s_textures[slot].tex);
    SetTextureFilter(s_textures[slot].tex, TEXTURE_FILTER_TRILINEAR);
    snprintf(s_textures[slot].path, sizeof s_textures[slot].path, "%s", resolved);
    s_textures[slot].used = true;
    s_textures[slot].refcount = 1;
    fprintf(stderr, "texture: loaded %s (%dx%d)\n", resolved,
            s_textures[slot].tex.width, s_textures[slot].tex.height);
    return (EngTexture){ (uint32_t)(slot + 1) };
}

EngTexture Eng_LoadTexture(const char *path) {
    if (!path || path[0] == '\0') return (EngTexture){0};
    char resolved[512];
    char rel[512];
    snprintf(rel, sizeof rel, "textures/%s", path);
    if (resolve_rel(rel, resolved, sizeof resolved)) return LoadTextureFromResolved(resolved);
    // Fallback: try as raw path.
    if (FileExists(path)) return LoadTextureFromResolved(path);
    fprintf(stderr, "content: texture '%s' not found\n", path);
    return (EngTexture){0};
}

EngTexture Eng_LoadTextureByName(const char *name) {
    if (!name || name[0] == '\0') return (EngTexture){0};
    char resolved[512];
    char rel[512];
    snprintf(rel, sizeof rel, "textures/%s.png", name);
    if (resolve_rel(rel, resolved, sizeof resolved)) return LoadTextureFromResolved(resolved);
    fprintf(stderr, "content: texture by name '%s.png' not found\n", name);
    return (EngTexture){0};
}

Texture2D *Eng_TextureGet(EngTexture h) {
    if (h.id == 0 || h.id > (uint32_t)s_texCount) return NULL;
    int idx = (int)h.id - 1;
    if (!s_textures[idx].used) return NULL;
    return &s_textures[idx].tex;
}

void Eng_UnloadTexture(EngTexture h) {
    if (h.id == 0 || h.id > (uint32_t)s_texCount) return;
    int idx = (int)h.id - 1;
    if (!s_textures[idx].used) return;  // already freed: silent no-op
    if (--s_textures[idx].refcount > 0) return;
    UnloadTexture(s_textures[idx].tex);
    s_textures[idx].used = false;
    s_textures[idx].path[0] = '\0';
    s_textures[idx].refcount = 0;
}

bool Eng_ReloadTexture(EngTexture h) {
    if (h.id == 0 || h.id > (uint32_t)s_texCount) return false;
    int idx = (int)h.id - 1;
    if (!s_textures[idx].used) return false;
    Texture2D fresh = LoadTexture(s_textures[idx].path);
    if (fresh.id == 0) {
        fprintf(stderr, "content: reload texture '%s' failed, keeping old data\n",
                s_textures[idx].path);
        return false;
    }
    SetTextureWrap(fresh, TEXTURE_WRAP_REPEAT);
    GenTextureMipmaps(&fresh);
    SetTextureFilter(fresh, TEXTURE_FILTER_TRILINEAR);
    UnloadTexture(s_textures[idx].tex);
    s_textures[idx].tex = fresh;
    fprintf(stderr, "texture: reloaded %s (%dx%d)\n", s_textures[idx].path,
            fresh.width, fresh.height);
    return true;
}

// ============================================================================
//  Eng_LoadShader
// ============================================================================

EngShader Eng_LoadShader(const char *vsPath, const char *fsPath) {
    if (!fsPath || fsPath[0] == '\0') return (EngShader){0};

    // Resolve VS path (may be NULL for the built-in passthrough).
    char vsResolved[512]; vsResolved[0] = '\0';
    bool vsOk = (vsPath == NULL);
    if (vsPath && vsPath[0] != '\0') {
        char rel[512];
        snprintf(rel, sizeof rel, "shaders/%s", vsPath);
        if (resolve_rel(rel, vsResolved, sizeof vsResolved)) vsOk = true;
        else if (FileExists(vsPath)) {
            snprintf(vsResolved, sizeof vsResolved, "%s", vsPath);
            vsOk = true;
        }
    }

    // Resolve FS path.
    char fsResolved[512]; fsResolved[0] = '\0';
    bool fsOk = false;
    {
        char rel[512];
        snprintf(rel, sizeof rel, "shaders/%s", fsPath);
        if (resolve_rel(rel, fsResolved, sizeof fsResolved)) fsOk = true;
        else if (FileExists(fsPath)) {
            snprintf(fsResolved, sizeof fsResolved, "%s", fsPath);
            fsOk = true;
        }
    }

    if (!fsOk) {
        fprintf(stderr, "content: shader fs '%s' not found\n", fsPath);
        return (EngShader){0};
    }
    if (vsPath && !vsOk) {
        fprintf(stderr, "content: shader vs '%s' not found\n", vsPath);
        return (EngShader){0};
    }

    // Dedup by resolved vs+fs paths.
    for (int i = 0; i < s_shaderCount; i++) {
        if (!s_shaders[i].used) continue;
        if (strcmp(s_shaders[i].fsPath, fsResolved) == 0 &&
            strcmp(s_shaders[i].vsPath, vsPath ? vsResolved : "") == 0) {
            s_shaders[i].refcount++;
            return (EngShader){ (uint32_t)(i + 1) };
        }
    }

    // Look for a freed slot to recycle before growing the table.
    int slot = -1;
    for (int i = 0; i < s_shaderCount; i++) {
        if (!s_shaders[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_shaderCount >= MAX_SHADERS) {
            fprintf(stderr, "content: shader registry full (cap=%d)\n", MAX_SHADERS);
            return (EngShader){0};
        }
        slot = s_shaderCount++;
    }

    s_shaders[slot].shader = LoadShader(vsPath ? vsResolved : NULL, fsResolved);
    if (s_shaders[slot].shader.id == 0 ||
        s_shaders[slot].shader.id == Eng_GfxDefaultShaderId()) {
        fprintf(stderr, "content: shader load failed for '%s'\n", fsResolved);
        if (slot == s_shaderCount - 1) s_shaderCount--;
        return (EngShader){0};
    }
    snprintf(s_shaders[slot].vsPath, sizeof s_shaders[slot].vsPath,
             "%s", vsPath ? vsResolved : "");
    snprintf(s_shaders[slot].fsPath, sizeof s_shaders[slot].fsPath,
             "%s", fsResolved);
    s_shaders[slot].used = true;
    s_shaders[slot].refcount = 1;
    fprintf(stderr, "shader: loaded vs=%s fs=%s\n",
            vsPath ? vsResolved : "(builtin-vs)", fsResolved);
    return (EngShader){ (uint32_t)(slot + 1) };
}

Shader *Eng_ShaderGet(EngShader h) {
    if (h.id == 0 || h.id > (uint32_t)s_shaderCount) return NULL;
    int idx = (int)h.id - 1;
    if (!s_shaders[idx].used) return NULL;
    return &s_shaders[idx].shader;
}

void Eng_UnloadShader(EngShader h) {
    if (h.id == 0 || h.id > (uint32_t)s_shaderCount) return;
    int idx = (int)h.id - 1;
    if (!s_shaders[idx].used) return;  // already freed: silent no-op
    if (--s_shaders[idx].refcount > 0) return;
    UnloadShader(s_shaders[idx].shader);
    s_shaders[idx].used = false;
    s_shaders[idx].vsPath[0] = '\0';
    s_shaders[idx].fsPath[0] = '\0';
    s_shaders[idx].refcount = 0;
}

// ============================================================================
//  Eng_LoadAnimModel (clip set: skinned model + embedded clips)
// ============================================================================

EngClipSet Eng_LoadAnimModel(const char *path) {
    if (!path || path[0] == '\0') return (EngClipSet){0};

    char resolved[512];
    char rel[512];
    // Anim models are GLB/GLTF in the same models/ subdir.
    snprintf(rel, sizeof rel, "models/%s", path);
    bool found = (resolve_rel(rel, resolved, sizeof resolved) != NULL);
    if (!found && FileExists(path)) {
        snprintf(resolved, sizeof resolved, "%s", path);
        found = true;
    }
    if (!found) {
        fprintf(stderr, "content: anim model '%s' not found\n", path);
        return (EngClipSet){0};
    }

    // Dedup.
    for (int i = 0; i < s_clipCount; i++) {
        if (s_clipsets[i].used && strcmp(s_clipsets[i].path, resolved) == 0) {
            s_clipsets[i].refcount++;
            return (EngClipSet){ (uint32_t)(i + 1) };
        }
    }

    // Look for a freed slot to recycle before growing the table.
    int slot = -1;
    for (int i = 0; i < s_clipCount; i++) {
        if (!s_clipsets[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_clipCount >= MAX_CLIPSETS) {
            fprintf(stderr, "content: clip set registry full (cap=%d)\n", MAX_CLIPSETS);
            return (EngClipSet){0};
        }
        slot = s_clipCount++;
    }

    s_clipsets[slot].model = LoadModel(resolved);
    if (s_clipsets[slot].model.meshCount == 0) {
        fprintf(stderr, "content: anim model '%s' has no meshes\n", resolved);
        if (slot == s_clipCount - 1) s_clipCount--;
        return (EngClipSet){0};
    }
    snprintf(s_clipsets[slot].path, sizeof s_clipsets[slot].path, "%s", resolved);
    s_clipsets[slot].used = true;
    s_clipsets[slot].refcount = 1;
    fprintf(stderr, "model: anim loaded %s (meshes=%d)\n", resolved,
            s_clipsets[slot].model.meshCount);
    return (EngClipSet){ (uint32_t)(slot + 1) };
}

Model *Eng_ClipSetModel(EngClipSet h) {
    if (h.id == 0 || h.id > (uint32_t)s_clipCount) return NULL;
    int idx = (int)h.id - 1;
    if (!s_clipsets[idx].used) return NULL;
    return &s_clipsets[idx].model;
}

void Eng_UnloadClipSet(EngClipSet h) {
    if (h.id == 0 || h.id > (uint32_t)s_clipCount) return;
    int idx = (int)h.id - 1;
    if (!s_clipsets[idx].used) return;  // already freed: silent no-op
    if (--s_clipsets[idx].refcount > 0) return;
    UnloadModel(s_clipsets[idx].model);
    s_clipsets[idx].used = false;
    s_clipsets[idx].path[0] = '\0';
    s_clipsets[idx].refcount = 0;
}

// ============================================================================
//  Eng_ContentFlush
// ============================================================================

void Eng_ContentFlush(void) {
    for (int i = 0; i < s_modelCount; i++) {
        if (s_models[i].used) {
            UnloadModel(s_models[i].model);
            s_models[i].used = false;
            s_models[i].path[0] = '\0';
        }
    }
    s_modelCount = 0;

    for (int i = 0; i < s_texCount; i++) {
        if (s_textures[i].used) {
            UnloadTexture(s_textures[i].tex);
            s_textures[i].used = false;
            s_textures[i].path[0] = '\0';
        }
    }
    s_texCount = 0;

    for (int i = 0; i < s_shaderCount; i++) {
        if (s_shaders[i].used) {
            UnloadShader(s_shaders[i].shader);
            s_shaders[i].used = false;
        }
    }
    s_shaderCount = 0;

    for (int i = 0; i < s_clipCount; i++) {
        if (s_clipsets[i].used) {
            UnloadModel(s_clipsets[i].model);
            s_clipsets[i].used = false;
            s_clipsets[i].path[0] = '\0';
        }
    }
    s_clipCount = 0;
}

// ============================================================================
//  Game-registered content type parsers
// ============================================================================

typedef struct {
    char              ext[16];
    EngContentParseFn fn;
    void             *user;
    bool              used;
} ContentTypeEntry;

static ContentTypeEntry s_contentTypes[ENG_CONTENT_TYPE_MAX];
static int              s_contentTypeCount = 0;

void Eng_RegisterContentType(const char *ext, EngContentParseFn fn, void *user) {
    if (!ext || !fn) return;
    if (s_contentTypeCount >= ENG_CONTENT_TYPE_MAX) {
        fprintf(stderr, "content: content-type registry full (cap=%d)\n",
                ENG_CONTENT_TYPE_MAX);
        return;
    }
    // Check for duplicate extension registration — update in place.
    for (int i = 0; i < s_contentTypeCount; i++) {
        if (s_contentTypes[i].used &&
            strcmp(s_contentTypes[i].ext, ext) == 0) {
            s_contentTypes[i].fn   = fn;
            s_contentTypes[i].user = user;
            return;
        }
    }
    int slot = s_contentTypeCount++;
    snprintf(s_contentTypes[slot].ext, sizeof s_contentTypes[slot].ext, "%s", ext);
    s_contentTypes[slot].fn   = fn;
    s_contentTypes[slot].user = user;
    s_contentTypes[slot].used = true;
}

void *Eng_LoadContent(const char *path) {
    if (!path || path[0] == '\0') return NULL;

    // Find file extension.
    const char *dot = strrchr(path, '.');
    if (!dot) {
        fprintf(stderr, "content: Eng_LoadContent: no extension in '%s'\n", path);
        return NULL;
    }

    // Find registered parser.
    EngContentParseFn fn = NULL;
    void *user = NULL;
    for (int i = 0; i < s_contentTypeCount; i++) {
        if (s_contentTypes[i].used &&
            strcmp(s_contentTypes[i].ext, dot) == 0) {
            fn   = s_contentTypes[i].fn;
            user = s_contentTypes[i].user;
            break;
        }
    }
    if (!fn) {
        fprintf(stderr, "content: no parser registered for extension '%s'\n", dot);
        return NULL;
    }

    // Probe the path.
    char resolved[512];
    const char *r = Eng_ResolveAssetPath(path, resolved, (int)sizeof resolved);
    if (!r) {
        fprintf(stderr, "content: '%s' not found\n", path);
        return NULL;
    }

    // Read bytes.
    char *bytes = LoadFileText(resolved);
    if (!bytes) {
        fprintf(stderr, "content: failed to read '%s'\n", resolved);
        return NULL;
    }

    size_t len = strlen(bytes);
    void *result = fn(resolved, bytes, len, user);
    UnloadFileText(bytes);
    return result;
}
