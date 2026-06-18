#ifndef SHOOTER_CONTENT_H
#define SHOOTER_CONTENT_H

// ============================================================================
//  Engine content registry — Phase 4 of the engine/game split.
//
//  Provides handle-based, dedup'd loaders for models, textures, shaders, and
//  animated models (clip sets).  All path probing is centralised here so game
//  code never reimplements the "data/X → ../data/X → ./data/X" prefix list.
//
//  Handles are small structs wrapping a uint32_t id; 0 == invalid.  Loading
//  the same resolved path twice returns the same handle (dedup is by canonical
//  resolved path).
//
//  Game-registered parsers: the engine can load *any* extension's bytes and
//  hand them to a game-supplied callback, so the engine reads .weapon/.map
//  files without knowing their meaning — see Eng_RegisterContentType.
//
//  No game header is ever included here (§2 cardinal rule).
// ============================================================================

#include "raylib.h"
#include <stddef.h>
#include <stdint.h>

// ---- Opaque handles (0 == invalid) ------------------------------------------

typedef struct { uint32_t id; } EngModel;
typedef struct { uint32_t id; } EngTexture;
typedef struct { uint32_t id; } EngShader;
typedef struct { uint32_t id; } EngClipSet;  // model + animation clips

// ---- Content roots (game-over-library overlay) ------------------------------
//
// The resolver probes an ordered root stack: the active game folder first, the
// engine's read-only stock library next, then legacy data/ dev fallbacks (so a
// build/source checkout still runs with neither root set).  A relative content
// path resolves the same regardless of which root answers — importing a stock
// asset (copying it under the game root) just makes the game copy win.
// See docs/game-projects.md §3.  Pass NULL/"" to clear a root.
void        Eng_SetGameRoot(const char *dir);     // <game>/    (writable, wins)
void        Eng_SetLibraryRoot(const char *dir);  // <library>/ (read-only)
const char *Eng_GetGameRoot(void);                // NULL if unset
const char *Eng_GetLibraryRoot(void);             // NULL if unset

// Fill `dirs` with the existing directory paths for content subdir `relSubdir`
// ("maps", "mobs", ...), game root first then library then data/ fallbacks.
// Callers scan each in order and de-dup entry names so a game entry shadows a
// same-named library entry.  Returns the count written (<= maxDirs).
int Eng_ContentDirs(const char *relSubdir, char dirs[][512], int maxDirs);

// ---- Path probing -----------------------------------------------------------

// Resolve a root-relative content path against the root stack (game, library,
// data/ fallbacks), writing the first existing path into `buf`.  A full path
// (absolute, or one already starting with "data/") is honoured as-is first, so
// legacy callers keep working.  Returns buf on success, NULL if nothing exists.
const char *Eng_ResolveAssetPath(const char *rel, char *buf, int bufsz);

// ---- Loaders ----------------------------------------------------------------

// Load a Model from a path (probed with Eng_ResolveAssetPath).  Deduplicates
// by resolved path so calling twice with the same file returns the same handle.
// Returns an invalid handle (id==0) on failure; logs one stderr line.
EngModel   Eng_LoadModel    (const char *path);

// Load a Texture2D.  After load, wrap is set to REPEAT, mipmaps are generated,
// and filter is set to TRILINEAR (standard for world-surface textures).
EngTexture Eng_LoadTexture  (const char *path);

// Load a shader pair.  vsPath may be NULL (raylib built-in passthrough VS).
// fsPath is probed; vsPath (if non-NULL) is probed separately.
EngShader  Eng_LoadShader   (const char *vsPath, const char *fsPath);

// Load a skinned glTF/GLB as a clip set (model + embedded animations).
EngClipSet Eng_LoadAnimModel(const char *path);

// ---- Accessors --------------------------------------------------------------
// Extract the underlying raylib value from a handle.  The returned pointer /
// value is valid until Eng_ContentFlush().  Returns a zero-initialised value
// for an invalid handle (model with meshCount==0, texture with id==0, etc.).

Model     *Eng_ModelGet    (EngModel h);
Texture2D *Eng_TextureGet  (EngTexture h);
Shader    *Eng_ShaderGet   (EngShader h);
// ClipSet accessor: returns the raw Model* (includes animation clips / bones).
Model     *Eng_ClipSetModel(EngClipSet h);

// ---- Flush / lifecycle ------------------------------------------------------
//
// Per-handle unload, dedup/refcount rule:
//
// Loads dedup by resolved path, so two Eng_Load*() calls for the same file
// return the *same* handle/slot.  To make per-handle unload coherent with
// that sharing, every slot carries a refcount: a dedup-hit during load
// increments it instead of allocating a new slot, and Eng_Unload*() decrements
// it.  The underlying raylib resource is actually freed (UnloadModel /
// UnloadTexture / UnloadShader) only when the count reaches zero, at which
// point the slot is marked free (reusable by a future load of a *different*
// path) and its handle becomes invalid.  Until the count reaches zero, every
// outstanding handle to that path remains valid and Eng_*Get keeps returning
// the live resource.
//
// Unloading an invalid handle (id==0) or a handle whose slot is already free
// is a silent no-op.  After a slot's refcount hits zero, Eng_*Get on any
// (now-stale) handle that pointed at it returns NULL — the same safe
// "invalid handle" placeholder used for id==0 — it never crashes.
//
// Freed slots are recycled by future Eng_Load*() calls (first-fit over the
// table), so handle values are *not* stable identities across an unload+load
// pair the way path-dedup makes them stable across repeated loads of the same
// path.

// Unload one model.  Decrements the slot's refcount; frees the underlying
// raylib Model and frees the slot for reuse only when it reaches zero.
void Eng_UnloadModel(EngModel h);

// Unload one texture.  Same refcount/free-at-zero rule as Eng_UnloadModel.
void Eng_UnloadTexture(EngTexture h);

// Unload one shader.  Same refcount/free-at-zero rule as Eng_UnloadModel.
void Eng_UnloadShader(EngShader h);

// Unload one clip set.  Same refcount/free-at-zero rule as Eng_UnloadModel.
void Eng_UnloadClipSet(EngClipSet h);

// Hot-reload: re-read the same resolved path from disk into the *same* slot,
// so every outstanding handle to it (refcount may be > 1) immediately sees
// the new data.  The old raylib resource is unloaded first.  Returns false
// (leaving the old resource in place) if the handle is invalid/freed or the
// reload fails to produce a usable resource; returns true on success.
bool Eng_ReloadModel(EngModel h);
bool Eng_ReloadTexture(EngTexture h);

// Unload all models, textures, shaders, and clip sets and reset every registry
// table.  Must be called from the main thread before CloseWindow.  After this
// call all previously issued handles are invalid.  This is unconditional —
// it ignores refcounts and frees everything regardless of how many handles
// were outstanding.
void Eng_ContentFlush(void);

// ---- Name-keyed texture convenience -----------------------------------------
// Thin wrapper: resolves data/textures/<name>.png, calls Eng_LoadTexture.
// Returns an invalid handle if missing.
EngTexture Eng_LoadTextureByName(const char *name);

// ---- Game-registered content parsers ----------------------------------------
//
// The game may register a parser for an arbitrary file extension.  When
// Eng_LoadContent is called the engine:
//   1. Probes the path.
//   2. Reads raw bytes via LoadFileText / FileExists.
//   3. Calls the registered callback with the path, bytes, byte-count, and the
//      user pointer the game supplied at registration time.
//   4. Returns the opaque void* the callback returns (NULL == failure).
//
// The game owns the lifetime of the returned object.

// Callback signature: given the resolved path + file contents, return an
// opaque game object (or NULL on failure).
typedef void *(*EngContentParseFn)(const char *resolvedPath,
                                   const char *bytes, size_t len,
                                   void *user);

// Register a parser for a file extension (e.g. ".weapon").  At most
// ENG_CONTENT_TYPE_MAX registrations; silently ignored if full.
#define ENG_CONTENT_TYPE_MAX 16
void Eng_RegisterContentType(const char *ext,
                             EngContentParseFn fn,
                             void *user);

// Probe `path`, read its bytes, find the registered parser for its extension,
// call it, and return the result.  Returns NULL if path not found, no parser
// registered, or the parser returns NULL.
void *Eng_LoadContent(const char *path);

#endif // SHOOTER_CONTENT_H
