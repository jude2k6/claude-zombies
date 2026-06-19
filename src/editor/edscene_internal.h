// ============================================================================
//  edscene_internal.h — editor-PRIVATE surface shared between edscene.c and its
//  split siblings (edplace.c material/placement, edmat.c material rendering).
//
//  These are NOT part of the public verb API in edscene.h — they are the few
//  former file-statics that had to gain external linkage when edscene.c was
//  split into focused translation units. Only the editor's own .c files include
//  this; plugins and the shell use edscene.h.
// ============================================================================
#ifndef SHOOTER_EDSCENE_INTERNAL_H
#define SHOOTER_EDSCENE_INTERNAL_H

#include "edscene.h"

// Below this footprint extent a sector RECT-drag is treated as a plain click
// (falls back to a default footprint). Shared by edplace.c (create) and
// edscene.c (edge resize).
#define ED_SECTOR_MIN_SIZE 1.0f

// Half-size of point-entity markers (spawn/perk/…) and the prop placeholder box.
// Shared by edscene.c (proxy build) and edmat.c (material-mode prop fallback).
#define ED_MARKER 0.6f

// --- shared helpers owned by edscene.c --------------------------------------
// Grid snapping (no-op when snapping is disabled).
float EdSnap(EdScene *s, float v);
// A sector's floor height (yLow), or 0 when the id is out of range.
float SectorFloorY(EdScene *s, int sectorId);
// The selectable proxy box for an entity id, or NULL.
const EdProxy *FindProxy(EdScene *s, int id);
// Replace the selection with a single id (-1 clears). Keeps the primary invariant.
void SelSetSingle(EdScene *s, int id);

// --- placement (edplace.c) --------------------------------------------------
// Drop whatever the active placement tool stamps at ground point p (already snapped).
void PlaceAt(EdScene *s, Vector3 p);
// RECT-drag sector authoring: press to anchor, drag to size, release to create.
void UpdateSectorDrag(EdScene *s);

// --- material-mode rendering (edmat.c) --------------------------------------
// Draw textured geometry (floor/walls/obstacles/props) with the world shader.
void DrawMaterialWorld(EdScene *s);

#endif // SHOOTER_EDSCENE_INTERNAL_H
