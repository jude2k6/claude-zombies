// ============================================================================
//  props.h — game-side placeable-prop catalog, loaded from props/*.prop.
//
//  The data-driven counterpart to the editor's EdScene_ScanProps: the same
//  catalog files, read here for the render model + collision a placed
//  MapDocProp needs. A new .prop (+ its model) becomes placeable in the editor
//  AND renderable in the game with no recompile. See
//  docs/editor-content-extensibility.md. Distinct from the PropId enum in
//  assets.h, which still owns the game's non-placeable internal models
//  (player, zombie, perk machines, …).
// ============================================================================
#ifndef SHOOTER_GAME_PROPS_H
#define SHOOTER_GAME_PROPS_H

#include "raylib.h"
#include <stdbool.h>

#define GAME_MAX_PROPS   32
#define GAME_PROPID_LEN  32

typedef struct {
    char    id[GAME_PROPID_LEN];  // matches MapDocProp.name (the PROP <id>)
    Model   model;
    bool    loaded;               // false = model file missing → cube fallback
    Vector3 halfExtent;           // collision half-size
    float   footY;                // model origin → collider centre Y
    float   modelScale;           // baked display scale (× the per-instance scale)
} GamePropDef;

// Scan props/*.prop across the content roots and load each model. Idempotent;
// safe to call again after Props_Unload. Called from Assets_Load.
void Props_Load(void);

// Clear the catalog. The underlying GL models are released by the engine
// content flush in Assets_Unload, so this only resets bookkeeping.
void Props_Unload(void);

// Set the world (fog) shader on every loaded catalog model, so placed props
// shade like the rest of the scene. Called from Assets_ApplyWorldShader.
void Props_ApplyWorldShader(Shader sh);

int                Props_Count(void);
const GamePropDef *Props_At(int idx);                 // NULL if out of range
int                Props_IndexByName(const char *id); // -1 if unknown

#endif // SHOOTER_GAME_PROPS_H
