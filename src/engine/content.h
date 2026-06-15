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

// ---- Path probing -----------------------------------------------------------

// Try each prefix in the built-in list ("data/X", "../data/X", "./data/X")
// prepended to `rel`, writing the first existing path into `buf`.
// Returns buf on success, NULL if nothing exists.
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

// Unload all models, textures, shaders, and clip sets and reset every registry
// table.  Must be called from the main thread before CloseWindow.  After this
// call all previously issued handles are invalid.
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
