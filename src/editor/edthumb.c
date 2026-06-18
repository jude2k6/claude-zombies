// ============================================================================
//  edthumb.c — lazy, cached GLB thumbnail renderer for the asset browser.
//  See edthumb.h for the contract. Renders each model to a small off-screen
//  RenderTexture ONCE, framed by its bounding sphere, and caches it by path.
//
//  Engine-clean: raylib + the engine content loaders (content.h) only — never
//  a src/game/ header (the editor seam rule).
// ============================================================================

#include "edthumb.h"

#include "raymath.h"
#include "content.h"   // Eng_LoadModel, Eng_ModelGet

#include <string.h>

#define EDTHUMB_MAX 64   // fixed cache; asset browsers show far fewer at once
#define EDTHUMB_MIN_PX 64 // clamp floor for a degenerate px request

// One cached, already-rendered thumbnail keyed by its request path. `valid`
// distinguishes a live RenderTexture from a negative-cache entry (a path we
// tried and failed to render — re-trying it every frame would thrash).
typedef struct {
    char            path[512];
    RenderTexture2D rt;
    bool            valid;   // rt holds a live GL texture worth unloading
} ThumbEntry;

static ThumbEntry s_cache[EDTHUMB_MAX];
static int        s_count;

// Linear scan — the cache is tiny and lookups are O(rows on screen).
static ThumbEntry *FindCached(const char *path) {
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_cache[i].path, path, sizeof s_cache[i].path) == 0) {
            return &s_cache[i];
        }
    }
    return NULL;
}

Texture2D EdThumb_Model(const char *path, int px) {
    if (!path) return (Texture2D){0};
    if (px < EDTHUMB_MIN_PX) px = EDTHUMB_MIN_PX;

    // Cache hit (incl. negative cache): return as-is, never re-render.
    ThumbEntry *hit = FindCached(path);
    if (hit) return hit->valid ? hit->rt.texture : (Texture2D){0};

    if (s_count >= EDTHUMB_MAX) return (Texture2D){0};

    // Claim a slot up-front so even a load failure is remembered (negative
    // cache), keeping a missing/bad model from re-loading on every request.
    ThumbEntry *e = &s_cache[s_count++];
    memset(e, 0, sizeof *e);
    strncpy(e->path, path, sizeof e->path - 1);
    e->valid = false;

    // The engine content cache owns/dedups the model — do NOT unload it here.
    EngModel mh = Eng_LoadModel(path);
    Model    *m = Eng_ModelGet(mh);
    if (!m || m->meshCount == 0) return (Texture2D){0};

    // Framing: drop a camera on a fixed 3/4 angle and back it off far enough
    // that the model's bounding sphere fits the vertical FOV. For fovy F the
    // half-view at distance d spans d*tan(F/2); set that to the sphere radius
    // (plus a little margin) and solve for d. Target the bounds centre so the
    // model sits in frame regardless of where its own origin lies.
    // Frame by the bounding sphere. We TRANSLATE the model so its bounds centre
    // lands on the world origin (draw at -center) and aim the camera at the
    // origin — this avoids any ambiguity about where the model's own origin sits
    // (rigged GLBs are often authored off-origin). Distance is solved from the
    // FOV so the sphere fits the vertical view, with a generous margin.
    BoundingBox bb     = GetModelBoundingBox(*m);
    Vector3     center = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
    float       radius = Vector3Length(Vector3Subtract(bb.max, bb.min)) * 0.5f;
    if (radius < 0.0001f) radius = 1.0f;  // degenerate / empty bounds

    const float fovy = 45.0f;
    float halfFov = (fovy * 0.5f) * DEG2RAD;
    float dist    = (radius * 1.3f) / tanf(halfFov);  // 30% margin

    Vector3 dir = Vector3Normalize((Vector3){0.9f, 0.6f, 1.0f});
    Camera3D cam = {0};
    cam.position   = Vector3Scale(dir, dist);
    cam.target     = (Vector3){0.0f, 0.0f, 0.0f};
    cam.up         = (Vector3){0.0f, 1.0f, 0.0f};
    cam.fovy       = fovy;
    cam.projection = CAMERA_PERSPECTIVE;

    RenderTexture2D rt = LoadRenderTexture(px, px);

    BeginTextureMode(rt);
        ClearBackground((Color){40, 44, 52, 255});  // dark slate
        BeginMode3D(cam);
            DrawModel(*m, Vector3Negate(center), 1.0f, WHITE);  // centre at origin
        EndMode3D();
    EndTextureMode();

    // Note: raylib RenderTextures are flipped vertically — the caller flips on
    // draw. We return rt.texture as-is.
    e->rt    = rt;
    e->valid = true;
    return rt.texture;
}

void EdThumb_Shutdown(void) {
    for (int i = 0; i < s_count; i++) {
        if (s_cache[i].valid) UnloadRenderTexture(s_cache[i].rt);
    }
    s_count = 0;
}
