// ============================================================================
//  edthumb.{c,h} — lazy, cached thumbnail renderer for the asset browser.
//
//  Renders a GLB model to a small off-screen RenderTexture ONCE and caches it
//  by path; later requests for the same path return the cached texture. Used by
//  the ASSETS panel (builtins.c) to show a preview next to each model row.
//
//  Engine-clean: depends on raylib + the engine content loaders (content.h)
//  only. Never includes a src/game/ header (the editor seam rule).
// ============================================================================
#ifndef SHOOTER_EDTHUMB_H
#define SHOOTER_EDTHUMB_H

#include "raylib.h"

// Return a cached square thumbnail for the model at `path` (a root-relative or
// already-resolved .glb path; resolved via Eng_LoadModel's own path probing).
// On the FIRST request for a given path the model is loaded and rendered to an
// off-screen RenderTexture — framed so its bounding box fills the view — and
// the result is cached keyed by `path`. Subsequent calls return the same
// texture without re-rendering. `px` is the desired square edge length in
// pixels, honoured only when the thumbnail is first created for that path.
//
// Returns a zero-id Texture2D ((Texture2D){0}) on failure (missing/invalid
// model, cache full); callers must treat .id == 0 as "no thumbnail" and fall
// back to a plain text row. Never crashes on a bad path.
Texture2D EdThumb_Model(const char *path, int px);

// Free every cached thumbnail RenderTexture and reset the cache. Must be called
// on the main thread before CloseWindow (RenderTextures hold GL handles).
void EdThumb_Shutdown(void);

#endif // SHOOTER_EDTHUMB_H
