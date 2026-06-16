/*
 * mapedit.c — id-addressed mutators + undo/redo history for MapDoc.
 *
 * See mapedit.h for the design rationale (snapshot model, coalescing
 * protocol, per-kind transform support). No game state is touched here.
 *
 * Dependencies: C standard library only (no raylib, no game headers).
 */

#include "mapedit.h"
#include <stdlib.h>
#include <string.h>

/* ---- find ---- */

EngMapEntHandle EngMapEnt_Find(const MapDoc *doc, int id) {
    EngMapEntHandle none = { ENGMAPENT_NONE, -1 };

    for (int i = 0; i < doc->spawnCount; i++)
        if (doc->spawns[i].id == id) return (EngMapEntHandle){ ENGMAPENT_SPAWN, i };
    for (int i = 0; i < doc->wallCount; i++)
        if (doc->walls[i].id == id) return (EngMapEntHandle){ ENGMAPENT_WALL, i };
    for (int i = 0; i < doc->windowCount; i++)
        if (doc->windows[i].id == id) return (EngMapEntHandle){ ENGMAPENT_WINDOW, i };
    for (int i = 0; i < doc->obstacleCount; i++)
        if (doc->obstacles[i].id == id) return (EngMapEntHandle){ ENGMAPENT_OBSTACLE, i };
    for (int i = 0; i < doc->propCount; i++)
        if (doc->props[i].id == id) return (EngMapEntHandle){ ENGMAPENT_PROP, i };
    for (int i = 0; i < doc->wallbuyCount; i++)
        if (doc->wallbuys[i].id == id) return (EngMapEntHandle){ ENGMAPENT_WALLBUY, i };
    for (int i = 0; i < doc->perkCount; i++)
        if (doc->perks[i].id == id) return (EngMapEntHandle){ ENGMAPENT_PERK, i };
    for (int i = 0; i < doc->sectorCount; i++)
        if (doc->sectors[i].id == id) return (EngMapEntHandle){ ENGMAPENT_SECTOR, i };

    return none;
}

/* Shared bounds + dispatch for Ptr/PtrConst: returns a non-const pointer to
   the slot, or NULL if the handle is out of range. Const-ness is applied by
   the two public wrappers below — the lookup logic itself is identical. */
static void *PtrMutable(MapDoc *doc, EngMapEntHandle h, EngMapEntKind *outKind) {
    if (outKind) *outKind = h.kind;
    if (h.index < 0) return NULL;

    switch (h.kind) {
        case ENGMAPENT_SPAWN:
            return (h.index < doc->spawnCount) ? &doc->spawns[h.index] : NULL;
        case ENGMAPENT_WALL:
            return (h.index < doc->wallCount) ? &doc->walls[h.index] : NULL;
        case ENGMAPENT_WINDOW:
            return (h.index < doc->windowCount) ? &doc->windows[h.index] : NULL;
        case ENGMAPENT_OBSTACLE:
            return (h.index < doc->obstacleCount) ? &doc->obstacles[h.index] : NULL;
        case ENGMAPENT_PROP:
            return (h.index < doc->propCount) ? &doc->props[h.index] : NULL;
        case ENGMAPENT_WALLBUY:
            return (h.index < doc->wallbuyCount) ? &doc->wallbuys[h.index] : NULL;
        case ENGMAPENT_PERK:
            return (h.index < doc->perkCount) ? &doc->perks[h.index] : NULL;
        case ENGMAPENT_SECTOR:
            return (h.index < doc->sectorCount) ? &doc->sectors[h.index] : NULL;
        case ENGMAPENT_NONE:
        default:
            return NULL;
    }
}

void *EngMapEnt_Ptr(MapDoc *doc, EngMapEntHandle h, EngMapEntKind *outKind) {
    return PtrMutable(doc, h, outKind);
}

const void *EngMapEnt_PtrConst(const MapDoc *doc, EngMapEntHandle h, EngMapEntKind *outKind) {
    /* MapDoc isn't mutated; the cast back to const below is safe. */
    return PtrMutable((MapDoc *)doc, h, outKind);
}

/* ---- transform accessors ----
 *
 * Each pair below looks up the handle once and switches on kind. Position
 * is supported everywhere; yaw/scale only on PROP; size/height only on
 * OBSTACLE or SECTOR as documented in mapedit.h. */

bool Eng_GetPos(const MapDoc *doc, int id, float *outX, float *outZ) {
    EngMapEntKind kind;
    const void *p = EngMapEnt_PtrConst(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p) return false;

    switch (kind) {
        case ENGMAPENT_SPAWN:    { const MapDocSpawn    *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_WINDOW:   { const MapDocWindow   *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_OBSTACLE: { const MapDocObstacle *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_PROP:     { const MapDocProp     *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_WALLBUY:  { const MapDocWallbuy  *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_PERK:     { const MapDocPerk     *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_SECTOR:   { const MapDocSector   *e = p; *outX = e->x; *outZ = e->z; return true; }
        case ENGMAPENT_WALL: {
            /* A wall has two endpoints, not a single position; expose its
               midpoint so a gizmo can still grab-and-move the whole wall. */
            const MapDocWall *e = p;
            *outX = 0.5f * (e->x1 + e->x2);
            *outZ = 0.5f * (e->z1 + e->z2);
            return true;
        }
        case ENGMAPENT_NONE:
        default:
            return false;
    }
}

bool Eng_SetPos(MapDoc *doc, int id, float x, float z) {
    EngMapEntKind kind;
    void *p = EngMapEnt_Ptr(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p) return false;

    switch (kind) {
        case ENGMAPENT_SPAWN:    { MapDocSpawn    *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_WINDOW:   { MapDocWindow   *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_OBSTACLE: { MapDocObstacle *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_PROP:     { MapDocProp     *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_WALLBUY:  { MapDocWallbuy  *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_PERK:     { MapDocPerk     *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_SECTOR:   { MapDocSector   *e = p; e->x = x; e->z = z; return true; }
        case ENGMAPENT_WALL: {
            /* Translate both endpoints together, preserving the wall's
               length and orientation — move-by-delta from the old midpoint. */
            MapDocWall *e = p;
            float oldMidX = 0.5f * (e->x1 + e->x2);
            float oldMidZ = 0.5f * (e->z1 + e->z2);
            float dx = x - oldMidX, dz = z - oldMidZ;
            e->x1 += dx; e->z1 += dz;
            e->x2 += dx; e->z2 += dz;
            return true;
        }
        case ENGMAPENT_NONE:
        default:
            return false;
    }
}

bool Eng_GetYaw(const MapDoc *doc, int id, float *outYawDeg) {
    EngMapEntKind kind;
    const void *p = EngMapEnt_PtrConst(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_PROP) return false;
    *outYawDeg = ((const MapDocProp *)p)->yawDeg;
    return true;
}

bool Eng_SetYaw(MapDoc *doc, int id, float yawDeg) {
    EngMapEntKind kind;
    void *p = EngMapEnt_Ptr(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_PROP) return false;
    ((MapDocProp *)p)->yawDeg = yawDeg;
    return true;
}

bool Eng_GetScale(const MapDoc *doc, int id, float *outScale) {
    EngMapEntKind kind;
    const void *p = EngMapEnt_PtrConst(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_PROP) return false;
    *outScale = ((const MapDocProp *)p)->scale;
    return true;
}

bool Eng_SetScale(MapDoc *doc, int id, float scale) {
    EngMapEntKind kind;
    void *p = EngMapEnt_Ptr(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_PROP) return false;
    ((MapDocProp *)p)->scale = scale;
    return true;
}

bool Eng_GetObstacleSize(const MapDoc *doc, int id, float *outSx, float *outSz, float *outH) {
    EngMapEntKind kind;
    const void *p = EngMapEnt_PtrConst(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_OBSTACLE) return false;
    const MapDocObstacle *e = p;
    *outSx = e->sx; *outSz = e->sz; *outH = e->h;
    return true;
}

bool Eng_SetObstacleSize(MapDoc *doc, int id, float sx, float sz, float h) {
    EngMapEntKind kind;
    void *p = EngMapEnt_Ptr(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_OBSTACLE) return false;
    MapDocObstacle *e = p;
    e->sx = sx; e->sz = sz; e->h = h;
    return true;
}

bool Eng_GetSectorSize(const MapDoc *doc, int id, float *outSx, float *outSz) {
    EngMapEntKind kind;
    const void *p = EngMapEnt_PtrConst(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_SECTOR) return false;
    const MapDocSector *e = p;
    *outSx = e->sx; *outSz = e->sz;
    return true;
}

bool Eng_SetSectorSize(MapDoc *doc, int id, float sx, float sz) {
    EngMapEntKind kind;
    void *p = EngMapEnt_Ptr(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_SECTOR) return false;
    MapDocSector *e = p;
    e->sx = sx; e->sz = sz;
    return true;
}

bool Eng_GetSectorHeights(const MapDoc *doc, int id, float *outYLow, float *outYHigh) {
    EngMapEntKind kind;
    const void *p = EngMapEnt_PtrConst(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_SECTOR) return false;
    const MapDocSector *e = p;
    *outYLow = e->yLow; *outYHigh = e->yHigh;
    return true;
}

bool Eng_SetSectorHeights(MapDoc *doc, int id, float yLow, float yHigh) {
    EngMapEntKind kind;
    void *p = EngMapEnt_Ptr(doc, EngMapEnt_Find(doc, id), &kind);
    if (!p || kind != ENGMAPENT_SECTOR) return false;
    MapDocSector *e = p;
    e->yLow = yLow; e->yHigh = yHigh;
    return true;
}

/* ---- add ---- */

int EngMapEnt_Add(MapDoc *doc, EngMapEntKind kind) {
    switch (kind) {
        case ENGMAPENT_SPAWN: {
            if (doc->spawnCount >= MAPDOC_MAX_SPAWNS) return -1;
            MapDocSpawn *e = &doc->spawns[doc->spawnCount++];
            memset(e, 0, sizeof *e);
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_WALL: {
            if (doc->wallCount >= MAPDOC_MAX_WALLS) return -1;
            MapDocWall *e = &doc->walls[doc->wallCount++];
            memset(e, 0, sizeof *e);
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_WINDOW: {
            if (doc->windowCount >= MAPDOC_MAX_WINDOWS) return -1;
            MapDocWindow *e = &doc->windows[doc->windowCount++];
            memset(e, 0, sizeof *e);
            strcpy(e->dir, "+x");
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_OBSTACLE: {
            if (doc->obstacleCount >= MAPDOC_MAX_OBSTACLES) return -1;
            MapDocObstacle *e = &doc->obstacles[doc->obstacleCount++];
            memset(e, 0, sizeof *e);
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_PROP: {
            if (doc->propCount >= MAPDOC_MAX_PROPS) return -1;
            MapDocProp *e = &doc->props[doc->propCount++];
            memset(e, 0, sizeof *e);
            e->scale = 1.0f;
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_WALLBUY: {
            if (doc->wallbuyCount >= MAPDOC_MAX_WALLBUYS) return -1;
            MapDocWallbuy *e = &doc->wallbuys[doc->wallbuyCount++];
            memset(e, 0, sizeof *e);
            strcpy(e->dir, "+x");
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_PERK: {
            if (doc->perkCount >= MAPDOC_MAX_PERKS) return -1;
            MapDocPerk *e = &doc->perks[doc->perkCount++];
            memset(e, 0, sizeof *e);
            e->sectorId = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_SECTOR: {
            if (doc->sectorCount >= MAPDOC_MAX_SECTORS) return -1;
            MapDocSector *e = &doc->sectors[doc->sectorCount++];
            memset(e, 0, sizeof *e);
            e->linkA = -1;
            e->linkB = -1;
            e->id = MapDoc_AllocId(doc);
            return e->id;
        }
        case ENGMAPENT_NONE:
        default:
            return -1;
    }
}

/* ---- delete ---- */

/* Left-shift `arr[idx+1 .. count-1]` down by one slot (removing `arr[idx]`)
   and decrement `*count`. Order-preserving compaction, used by every kind
   below — swap-with-last would silently reorder MapDoc_Save's output. */
#define COMPACT(arr, count, idx) do {                                   \
        memmove(&(arr)[(idx)], &(arr)[(idx) + 1],                       \
                (size_t)((count) - (idx) - 1) * sizeof (arr)[0]);       \
        (count)--;                                                      \
    } while (0)

/* Fix up every sectorId-bearing field in `doc` after sector index `removed`
   is deleted from doc->sectors: references to `removed` become -1
   (ungrouped), references past it shift down by one to track the compaction. */
static void FixupSectorRefs(MapDoc *doc, int removed) {
    #define FIX(ref) do { \
            if ((ref) == removed) (ref) = -1; \
            else if ((ref) > removed) (ref)--; \
        } while (0)

    for (int i = 0; i < doc->spawnCount; i++)    FIX(doc->spawns[i].sectorId);
    for (int i = 0; i < doc->wallCount; i++)     FIX(doc->walls[i].sectorId);
    for (int i = 0; i < doc->windowCount; i++)   FIX(doc->windows[i].sectorId);
    for (int i = 0; i < doc->obstacleCount; i++) FIX(doc->obstacles[i].sectorId);
    for (int i = 0; i < doc->propCount; i++)     FIX(doc->props[i].sectorId);
    for (int i = 0; i < doc->wallbuyCount; i++)  FIX(doc->wallbuys[i].sectorId);
    for (int i = 0; i < doc->perkCount; i++)     FIX(doc->perks[i].sectorId);
    if (doc->hasPap)        FIX(doc->pap.sectorId);
    if (doc->mbox.present)  FIX(doc->mbox.sectorId);

    /* RAMP sectors link to two other sectors by index — same fix-up. */
    for (int i = 0; i < doc->sectorCount; i++) {
        FIX(doc->sectors[i].linkA);
        FIX(doc->sectors[i].linkB);
    }

    #undef FIX
}

bool EngMapEnt_Delete(MapDoc *doc, int id) {
    EngMapEntHandle h = EngMapEnt_Find(doc, id);

    switch (h.kind) {
        case ENGMAPENT_SPAWN:
            COMPACT(doc->spawns, doc->spawnCount, h.index);
            return true;
        case ENGMAPENT_WALL:
            COMPACT(doc->walls, doc->wallCount, h.index);
            return true;
        case ENGMAPENT_WINDOW:
            COMPACT(doc->windows, doc->windowCount, h.index);
            return true;
        case ENGMAPENT_OBSTACLE:
            COMPACT(doc->obstacles, doc->obstacleCount, h.index);
            return true;
        case ENGMAPENT_PROP:
            COMPACT(doc->props, doc->propCount, h.index);
            return true;
        case ENGMAPENT_WALLBUY:
            COMPACT(doc->wallbuys, doc->wallbuyCount, h.index);
            return true;
        case ENGMAPENT_PERK:
            COMPACT(doc->perks, doc->perkCount, h.index);
            return true;
        case ENGMAPENT_SECTOR:
            COMPACT(doc->sectors, doc->sectorCount, h.index);
            FixupSectorRefs(doc, h.index);
            return true;
        case ENGMAPENT_NONE:
        default:
            return false;
    }
}

#undef COMPACT

/* ---- undo/redo history ----
 *
 * Logical stack indices 0..count-1 map onto the physical ring via
 * physical = (head + logical) % depth. `cursor` is the logical index of
 * the snapshot currently considered "now"; -1 means the history is empty. */

static int PhysIndex(const EngMapHistory *hist, int logical) {
    return (hist->head + logical) % hist->depth;
}

bool EngMapHistory_Init(EngMapHistory *hist, int depth) {
    memset(hist, 0, sizeof *hist);
    if (depth <= 0) return false;

    hist->snaps = malloc((size_t)depth * sizeof *hist->snaps);
    if (!hist->snaps) return false;

    hist->depth = depth;
    hist->count = 0;
    hist->cursor = -1;
    hist->head = 0;
    hist->lastTag = 0;
    return true;
}

void EngMapHistory_Free(EngMapHistory *hist) {
    if (!hist) return;
    free(hist->snaps);
    memset(hist, 0, sizeof *hist);
}

bool EngMapHistory_Commit(EngMapHistory *hist, const MapDoc *now, uint32_t tag) {
    /* No-op guard: don't record a step that changed nothing. */
    if (hist->count > 0 && hist->cursor >= 0) {
        const MapDoc *top = &hist->snaps[PhysIndex(hist, hist->cursor)];
        if (MapDoc_Equal(top, now)) return false;
    }

    /* Coalescing: same non-zero tag as the last commit overwrites the
       current top checkpoint in place instead of pushing a new one. */
    if (tag != 0 && tag == hist->lastTag && hist->count > 0 && hist->cursor == hist->count - 1) {
        hist->snaps[PhysIndex(hist, hist->cursor)] = *now;
        return true;
    }

    /* Ordinary push: drop any redo tail beyond the cursor first. */
    hist->count = hist->cursor + 1;

    if (hist->count == hist->depth) {
        /* At capacity: drop the oldest entry to make room. Logical indices
           all shift down by one as a result, so the cursor (which pointed
           at the old top, count-1) tracks automatically once count is
           recomputed below. */
        hist->head = (hist->head + 1) % hist->depth;
        hist->count--;
        hist->cursor--;
    }

    hist->snaps[PhysIndex(hist, hist->count)] = *now;
    hist->count++;
    hist->cursor = hist->count - 1;
    hist->lastTag = tag;
    return true;
}

bool EngMapHistory_CanUndo(const EngMapHistory *hist) {
    return hist->cursor > 0;
}

bool EngMapHistory_CanRedo(const EngMapHistory *hist) {
    return hist->cursor >= 0 && hist->cursor < hist->count - 1;
}

bool EngMapHistory_Undo(EngMapHistory *hist, MapDoc *out) {
    if (!EngMapHistory_CanUndo(hist)) return false;
    hist->cursor--;
    *out = hist->snaps[PhysIndex(hist, hist->cursor)];
    hist->lastTag = 0;
    return true;
}

bool EngMapHistory_Redo(EngMapHistory *hist, MapDoc *out) {
    if (!EngMapHistory_CanRedo(hist)) return false;
    hist->cursor++;
    *out = hist->snaps[PhysIndex(hist, hist->cursor)];
    hist->lastTag = 0;
    return true;
}
