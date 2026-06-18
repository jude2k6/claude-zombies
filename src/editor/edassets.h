// ============================================================================
//  edassets.{c,h} — the editor's ASSET INDEX (for the asset-browser panel).
//
//  Scans the content overlay (active game root → read-only library → data/ dev
//  fallbacks, via Eng_ContentDirs) for the three asset families the browser
//  shows: MAPS (*.map), MODELS (*.glb), and TEXTURES (*.png). Each family is a
//  de-duped list of { display name, full path } — a game entry shadows a
//  same-named library entry, exactly like the .mob / .prop catalog scans in
//  edscene.c.
//
//  This is a pure file index: it reads directory listings only, never parses or
//  loads the assets. Rendering a model/texture thumbnail is edthumb.{c,h}; the
//  panel UI that displays this index is the ASSETS built-in in builtins.c.
//
//  Engine-clean: raylib FS + content.h only; never a src/game/ header.
// ============================================================================
#ifndef SHOOTER_EDASSETS_H
#define SHOOTER_EDASSETS_H

// One indexed asset: a display name (file stem) + the resolved path to load it.
#define ED_ASSET_NAME_LEN 64
#define ED_ASSET_PATH_LEN 512
typedef struct {
    char name[ED_ASSET_NAME_LEN];  // file stem, e.g. "zombie" (no dir, no ext)
    char path[ED_ASSET_PATH_LEN];  // full path usable by Open / loaders
} EdAsset;

// The three families the browser lists. Caps are generous; overflow is dropped.
#define ED_MAX_MAP_ASSETS     128
#define ED_MAX_MODEL_ASSETS   128
#define ED_MAX_TEXTURE_ASSETS 128

typedef struct {
    EdAsset maps[ED_MAX_MAP_ASSETS];         int mapCount;
    EdAsset models[ED_MAX_MODEL_ASSETS];     int modelCount;
    EdAsset textures[ED_MAX_TEXTURE_ASSETS]; int textureCount;
} EdAssetIndex;

// (Re)scan all three families across the content roots into `ix`. Idempotent;
// safe to call again after the game root changes (e.g. Open Game). De-dups by
// display name within each family (first root wins → game shadows library).
void EdAssets_Scan(EdAssetIndex *ix);

#endif // SHOOTER_EDASSETS_H
