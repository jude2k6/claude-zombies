#ifndef SHOOTER_MAPEDIT_H
#define SHOOTER_MAPEDIT_H

/*
 * mapedit.h — id-addressed mutators + undo/redo history for MapDoc.
 *
 * This header and mapedit.c depend ONLY on the C standard library and
 * mapdoc.h, so a future editor can link them standalone without pulling in
 * raylib or any game headers — same seam rule as mapdoc.h/.c.
 *
 * This is the engine half of "Editor: gizmo + undo/redo" (see
 * docs/engine-roadmap.md, Phase C). It does not know about gizmos, input, or
 * rendering; it answers two questions for whatever does:
 *   (A) "where is the entity with id N, and how do I read/write its
 *       transform?" — EngMapEnt_Find / Eng_GetPos / Eng_SetPos / etc.
 *   (B) "how do I get one undo step per drag, not one per frame?" —
 *       EngMapHistory + its coalescing tag protocol.
 *
 * ---- ID-ADDRESSED MUTATORS ----
 *
 * MapDoc arrays are flat and index-shifting: deleting element 3 moves
 * everything after it down by one. Stable `id` fields (see mapdoc.h's header
 * comment) exist precisely so a gizmo or selection list can hold onto "the
 * thing with id 17" across such shifts. EngMapEnt_Find walks every array
 * once and returns a tagged handle (kind + index) so the caller can recover
 * a typed pointer with EngMapEnt_Ptr / EngMapEnt_PtrConst, or just use the
 * Get/Set position and yaw/scale helpers directly by id.
 *
 * Per-kind transform support (a gizmo should query this, not assume it):
 *   - Position (x,z):        ALL kinds. The implied world Y comes from the
 *                             entity's sector surface, not stored here.
 *   - Yaw (degrees):         PROP only.
 *   - Uniform scale:         PROP only.
 *   - Footprint size (sx,sz) and height (h): OBSTACLE and SECTOR only.
 *     Sectors additionally have yLow/yHigh instead of a single h; see
 *     Eng_SetSectorHeights.
 * Calling a setter on a kind that doesn't support that field is a no-op
 * that returns false — it is never undefined behaviour.
 *
 * Add/Delete:
 *   - EngMapEnt_Add mints a fresh id via MapDoc_AllocId, zero-initialises
 *     the new element (sectorId = -1, dir = "+x" where applicable), appends
 *     it, and returns the new id, or -1 if the kind's array is already at
 *     its MAPDOC_MAX_* cap.
 *   - EngMapEnt_Delete compacts the owning array (swap-with-last is NOT
 *     used — order matters for MapDoc_Save's sector-grouped output, so this
 *     is a stable left-shift of everything after the removed slot).
 *     Deleting a SECTOR additionally fixes up every entity's `sectorId` and
 *     every other sector's `linkA`/`linkB` that referenced it: references to
 *     the deleted sector become -1 (ungrouped/unlinked) and references past
 *     it are decremented to track the shift. This is the one place sector
 *     indices need bookkeeping; entities never need it since their `id`
 *     fields are untouched by any compaction.
 *
 * ---- UNDO/REDO HISTORY ----
 *
 * MapDoc is a flat POD struct: fixed inline arrays, no pointers, no owned
 * heap memory. That makes whole-document snapshotting both correct and
 * simple — there is no fragile per-field "inverse command" to get wrong,
 * and a snapshot is just a memcpy. EngMapHistory is a fixed-depth stack of
 * MapDoc snapshots (heap-allocated once at init, depth fixed at creation):
 *   - EngMapHistory_Commit pushes `*now` as a new checkpoint, discarding any
 *     redo tail (the usual "editing after undo abandons the future" rule).
 *     A commit that is MapDoc_Equal to the current top is a no-op — no
 *     wasted history slot for a "drag" that didn't actually move anything.
 *   - EngMapHistory_Undo / _Redo write a snapshot into a caller-provided
 *     `MapDoc *out` and move the cursor; CanUndo/CanRedo report whether
 *     there's anywhere to move.
 *   - At capacity, committing drops the OLDEST checkpoint (ring buffer) —
 *     undo depth is bounded, redo is exact within that window.
 *
 * COALESCING — collapsing one continuous gizmo drag into a single undo step:
 *   A drag fires many commits (every frame while the mouse moves), but the
 *   user expects one undo to fully revert the drag. Tag the commit with a
 *   non-zero token shared across the whole drag:
 *     1. On mouse-down (begin drag): pick a fresh non-zero tag, e.g. an
 *        incrementing counter or `(uint32_t)entityId` if only one thing can
 *        be dragged at a time.
 *     2. Every frame while dragging: EngMapHistory_Commit(hist, &doc, tag).
 *        Each call with the SAME tag as the previous commit overwrites that
 *        checkpoint in place instead of pushing a new one — so the history
 *        depth doesn't grow during a long drag.
 *     3. On mouse-up (end drag): commit one last time with tag 0 (or simply
 *        let the next unrelated commit use tag 0) — tag 0 always pushes a
 *        new step and never coalesces, so it cleanly closes the drag and
 *        the next edit starts fresh.
 *   Tag matching is consecutive-only: it compares against the tag of the
 *   most recent commit, so two unrelated drags that happen to reuse a tag
 *   value are still safe as long as something else (even a no-op) doesn't
 *   sit between them with the same tag.
 */

#include "mapdoc.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- entity kinds ---- */

typedef enum {
    ENGMAPENT_NONE = 0,
    ENGMAPENT_SPAWN,
    ENGMAPENT_WALL,
    ENGMAPENT_WINDOW,
    ENGMAPENT_OBSTACLE,
    ENGMAPENT_PROP,
    ENGMAPENT_WALLBUY,
    ENGMAPENT_PERK,
    ENGMAPENT_SECTOR,
} EngMapEntKind;

/* A located entity: which array, and which slot in it. `index` is only
 * valid until the next Add/Delete on `doc` — re-resolve by id after either. */
typedef struct {
    EngMapEntKind kind;
    int           index;
} EngMapEntHandle;

/* ---- find ---- */

/*
 * EngMapEnt_Find — search every entity array of `doc` for `id`.
 * Returns a handle with kind == ENGMAPENT_NONE if not found.
 */
EngMapEntHandle EngMapEnt_Find(const MapDoc *doc, int id);

/*
 * EngMapEnt_Ptr / EngMapEnt_PtrConst — recover a typed pointer from a handle.
 * `outKind` (if non-NULL) receives the handle's kind so the caller can
 * switch on it without re-checking. Returns NULL for an ENGMAPENT_NONE
 * handle or an out-of-range index.
 */
void       *EngMapEnt_Ptr(MapDoc *doc, EngMapEntHandle h, EngMapEntKind *outKind);
const void *EngMapEnt_PtrConst(const MapDoc *doc, EngMapEntHandle h, EngMapEntKind *outKind);

/* ---- uniform transform access by id ----
 *
 * All of these look up `id` internally (via EngMapEnt_Find) — callers don't
 * need to hold a handle. Each returns false (and leaves outputs untouched)
 * if `id` doesn't exist or the kind doesn't support the field; see the
 * per-kind support table in the header comment above. */

/* Position (x,z). Supported by every kind. */
bool Eng_GetPos(const MapDoc *doc, int id, float *outX, float *outZ);
bool Eng_SetPos(MapDoc *doc, int id, float x, float z);

/* Yaw, degrees. PROP only. */
bool Eng_GetYaw(const MapDoc *doc, int id, float *outYawDeg);
bool Eng_SetYaw(MapDoc *doc, int id, float yawDeg);

/* Uniform scale factor. PROP only. */
bool Eng_GetScale(const MapDoc *doc, int id, float *outScale);
bool Eng_SetScale(MapDoc *doc, int id, float scale);

/* Footprint size (sx,sz) and height (h). OBSTACLE only.
 * (SECTOR has its own footprint+height accessors below since it additionally
 * tracks yLow/yHigh rather than a single h, and rampAxis/links.) */
bool Eng_GetObstacleSize(const MapDoc *doc, int id, float *outSx, float *outSz, float *outH);
bool Eng_SetObstacleSize(MapDoc *doc, int id, float sx, float sz, float h);

/* Footprint size (sx,sz). SECTOR only. */
bool Eng_GetSectorSize(const MapDoc *doc, int id, float *outSx, float *outSz);
bool Eng_SetSectorSize(MapDoc *doc, int id, float sx, float sz);

/* Floor heights. SECTOR only: FLAT sectors should keep yLow == yHigh. */
bool Eng_GetSectorHeights(const MapDoc *doc, int id, float *outYLow, float *outYHigh);
bool Eng_SetSectorHeights(MapDoc *doc, int id, float yLow, float yHigh);

/* Spawn mob tag. SPAWN only. "PLAYER" = a player start; any other string is a
 * mob spawn (e.g. "ZOMBIE"). Set truncates to MAPDOC_SPAWN_MOB_LEN. */
bool Eng_GetSpawnMob(const MapDoc *doc, int id, char *outMob, int cap);
bool Eng_SetSpawnMob(MapDoc *doc, int id, const char *mob);

/* Facing direction, one of "+x" "-x" "+z" "-z". WINDOW only. Set rejects any
 * other string (returns false). */
bool Eng_GetWindowDir(const MapDoc *doc, int id, char *outDir, int cap);
bool Eng_SetWindowDir(MapDoc *doc, int id, const char *dir);

/* Owning sector index (-1 = ungrouped). Supported by every placed kind EXCEPT
 * SECTOR itself. NOTE: an entity must belong to a real sector to be saved —
 * MapDoc_Save only emits entities inside a SECTOR/RAMP block, and the grammar
 * forbids ungrouped placed entities. An editor must assign a valid sector
 * (e.g. the one under the cursor) to anything it adds. */
bool Eng_GetSector(const MapDoc *doc, int id, int *outSector);
bool Eng_SetSector(MapDoc *doc, int id, int sectorIndex);

/* ---- add / delete ---- */

/*
 * EngMapEnt_Add — append a zero-initialised entity of `kind` to `doc`.
 * Mints a fresh id via MapDoc_AllocId and stamps it into the new element.
 * Defaults: sectorId = -1 (ungrouped), dir = "+x" where the kind has a dir
 * field, scale = 1.0f for props. Returns the new id, or -1 if that kind's
 * array is already at its MAPDOC_MAX_* cap (doc is left unmodified).
 * ENGMAPENT_NONE is not a valid kind to add and also returns -1.
 */
int EngMapEnt_Add(MapDoc *doc, EngMapEntKind kind);

/*
 * EngMapEnt_Delete — remove the entity with `id` from `doc`, left-shifting
 * everything after it in the same array (order is preserved; see the
 * header comment on why swap-with-last is wrong here).
 * Deleting a SECTOR fixes up every entity's `sectorId` and every remaining
 * sector's `linkA`/`linkB`: indices pointing at the deleted sector become
 * -1, indices past it shift down by one. No other kind needs reference
 * fix-up since nothing else stores an array index into another kind.
 * Returns false if `id` does not exist.
 */
bool EngMapEnt_Delete(MapDoc *doc, int id);

/* ---- undo/redo history ---- */

/* Default ring depth if the caller doesn't have an opinion. */
#define ENGMAPHISTORY_DEFAULT_DEPTH 64

typedef struct {
    MapDoc   *snaps;      /* heap array of `depth` MapDoc snapshots (ring) */
    int       depth;      /* capacity, fixed for the lifetime of the history */
    int       count;      /* number of valid snapshots currently stored */
    int       cursor;     /* index (into the logical stack, 0..count-1) of
                              the current state; undo decrements, redo
                              increments */
    int       head;       /* ring index of snapshot 0 (oldest live entry) */
    uint32_t  lastTag;     /* tag of the most recent commit; 0 = uncoalesced */
} EngMapHistory;

/*
 * EngMapHistory_Init — allocate a history with room for `depth` snapshots
 * (use ENGMAPHISTORY_DEFAULT_DEPTH if unsure). Starts empty: CanUndo and
 * CanRedo both report false until the first Commit. Returns false (and
 * leaves *hist zeroed) on allocation failure or depth <= 0.
 */
bool EngMapHistory_Init(EngMapHistory *hist, int depth);

/*
 * EngMapHistory_Free — release the snapshot ring. Safe to call on a
 * zero-initialised or already-freed history (no-op).
 */
void EngMapHistory_Free(EngMapHistory *hist);

/*
 * EngMapHistory_Commit — push `*now` as a new checkpoint, truncating any
 * redo tail beyond the current cursor.
 *   - If `*now` is MapDoc_Equal to the current top snapshot, this is a
 *     no-op (returns false): nothing changed, so no step is recorded.
 *   - If `tag` is non-zero AND equals the tag of the most recent commit,
 *     this OVERWRITES that checkpoint in place instead of pushing a new
 *     one (the coalescing path — see the header comment's drag protocol).
 *   - Otherwise this pushes a brand new checkpoint. At capacity, the
 *     oldest checkpoint is dropped to make room.
 * Pass tag = 0 for an ordinary, never-coalesced commit.
 * Returns true if history state changed (pushed or overwrote), false if
 * the no-op case applied.
 */
bool EngMapHistory_Commit(EngMapHistory *hist, const MapDoc *now, uint32_t tag);

/* EngMapHistory_CanUndo / CanRedo — is there a snapshot to move to? */
bool EngMapHistory_CanUndo(const EngMapHistory *hist);
bool EngMapHistory_CanRedo(const EngMapHistory *hist);

/*
 * EngMapHistory_Undo — move the cursor one step back and write that
 * snapshot into `*out`. Returns false (leaving `*out` untouched) if
 * CanUndo would report false. Also clears the coalescing tag so the next
 * Commit always starts a fresh step rather than coalescing into history
 * that the user just navigated away from.
 */
bool EngMapHistory_Undo(EngMapHistory *hist, MapDoc *out);

/*
 * EngMapHistory_Redo — move the cursor one step forward and write that
 * snapshot into `*out`. Returns false (leaving `*out` untouched) if
 * CanRedo would report false. Also clears the coalescing tag.
 */
bool EngMapHistory_Redo(EngMapHistory *hist, MapDoc *out);

#endif /* SHOOTER_MAPEDIT_H */
