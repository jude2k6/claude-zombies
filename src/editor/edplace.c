// ============================================================================
//  edplace.c — placement tools: turning a ground click into a MapDoc entity.
//
//  Split out of edscene.c. Everything here answers "the user armed a tool and
//  clicked the ground — what gets stamped, where, in which sector?". The core
//  scene (edscene.c) decides WHEN to call these (see EdScene_UpdateViewport);
//  this file owns the WHAT. Shared helpers (EdSnap, SelSetSingle, the sector-min
//  size) come from edscene_internal.h.
// ============================================================================

#include "edscene.h"
#include "edscene_internal.h"

#include "mapedit.h"   // EngMapEnt_* + Eng_Set* document mutators
#include "pick.h"      // Eng_PickRayFromScreen / Eng_PickRayGroundY

#include <stdio.h>
#include <string.h>
#include <math.h>

// ---- placement (mapedit add + retag) ---------------------------------------

static const char *FacingToward(Vector3 p) {
    if (fabsf(p.x) >= fabsf(p.z)) return p.x > 0 ? "-x" : "+x";
    return p.z > 0 ? "-z" : "+z";
}
static Vector3 DirNormal(const char *d) {
    if (!strcmp(d, "+x")) return (Vector3){  1, 0,  0 };
    if (!strcmp(d, "-x")) return (Vector3){ -1, 0,  0 };
    if (!strcmp(d, "+z")) return (Vector3){  0, 0,  1 };
    return (Vector3){ 0, 0, -1 };
}

// Find the sector whose footprint contains (x,z). Returns the sector index when
// the point is inside a footprint, or -1 when outside every sector (or when
// there are no sectors at all). This is the inner test — callers use SectorAt.
static int SectorHit(const EdScene *s, float x, float z) {
    for (int i = 0; i < s->doc.sectorCount; i++) {
        const MapDocSector *sc = &s->doc.sectors[i];
        if (fabsf(x - sc->x) <= sc->sx * 0.5f && fabsf(z - sc->z) <= sc->sz * 0.5f) return i;
    }
    return -1;
}

// The sector whose footprint contains (x,z), or sector 0 as a fallback when
// none does (so a placed entity always belongs somewhere). -1 only when there
// are no sectors at all. When the fallback fires, sets s->placeWarn for 3 s so
// the viewport tool-strip overlay can show an amber warning (T1-3).
static int SectorAt(EdScene *s, float x, float z) {
    int hit = SectorHit(s, x, z);
    if (hit >= 0) return hit;
    if (s->doc.sectorCount <= 0) return -1;
    // Point is outside every sector footprint — fall back to sector 0 and warn.
    snprintf(s->placeWarn, sizeof s->placeWarn, "placed outside all sectors -> sector 0");
    s->placeWarnUntil = GetTime() + 3.0;
    return 0;
}

static int AddSpawn(EdScene *s, const char *mob, float x, float z) {
    int id = EngMapEnt_Add(&s->doc, ENGMAPENT_SPAWN);
    if (id < 0) return -1;
    Eng_SetSpawnMob(&s->doc, id, mob);
    Eng_SetSector(&s->doc, id, SectorAt(s, x, z));
    Eng_SetPos(&s->doc, id, x, z);
    return id;
}

void PlaceAt(EdScene *s, Vector3 p) {
    switch (s->placeTool) {
        case ED_PLACE_PLAYER: SelSetSingle(s, AddSpawn(s, "PLAYER", p.x, p.z)); EdScene_Commit(s); break;
        case ED_PLACE_MOB: {
            const char *mob = s->placeMobId[0] ? s->placeMobId : "ZOMBIE";
            SelSetSingle(s, AddSpawn(s, mob, p.x, p.z)); EdScene_Commit(s); break;
        }
        case ED_PLACE_BARRICADE: {
            int wid = EngMapEnt_Add(&s->doc, ENGMAPENT_WINDOW);
            if (wid < 0) return;
            const char *dir = FacingToward(p);
            Eng_SetWindowDir(&s->doc, wid, dir);
            Eng_SetSector(&s->doc, wid, SectorAt(s, p.x, p.z));
            Eng_SetPos(&s->doc, wid, p.x, p.z);
            SelSetSingle(s, wid);
            if (s->barricadeAutoSpawn) {
                Vector3 n = DirNormal(dir);
                AddSpawn(s, "ZOMBIE", p.x - n.x * 5.0f, p.z - n.z * 5.0f);
            }
            EdScene_Commit(s);
            break;
        }
        case ED_PLACE_WALL: {
            // Two-click flow: first click stores start; second click places.
            if (!s->wallPending) {
                s->wallPending  = true;
                s->wallStartX   = p.x;
                s->wallStartZ   = p.z;
                // Nothing committed yet — show pending visually via wallPending flag.
            } else {
                int wid = EngMapEnt_Add(&s->doc, ENGMAPENT_WALL);
                if (wid < 0) { s->wallPending = false; return; }
                // Set wall endpoints directly via the typed pointer.
                EngMapEntHandle h = EngMapEnt_Find(&s->doc, wid);
                MapDocWall *w = (MapDocWall *)EngMapEnt_Ptr(&s->doc, h, NULL);
                if (w) {
                    w->x1 = s->wallStartX; w->z1 = s->wallStartZ;
                    w->x2 = p.x;           w->z2 = p.z;
                }
                // Assign to the sector under the midpoint.
                float midX = (s->wallStartX + p.x) * 0.5f;
                float midZ = (s->wallStartZ + p.z) * 0.5f;
                Eng_SetSector(&s->doc, wid, SectorAt(s, midX, midZ));
                SelSetSingle(s, wid);
                s->wallPending = false;
                EdScene_Commit(s);
            }
            break;
        }
        case ED_PLACE_OBSTACLE: {
            int oid = EngMapEnt_Add(&s->doc, ENGMAPENT_OBSTACLE);
            if (oid < 0) return;
            Eng_SetPos(&s->doc, oid, p.x, p.z);
            Eng_SetObstacleSize(&s->doc, oid, 4.0f, 4.0f, 3.0f);
            Eng_SetSector(&s->doc, oid, SectorAt(s, p.x, p.z));
            SelSetSingle(s, oid);
            EdScene_Commit(s);
            break;
        }
        case ED_PLACE_PROP: {
            int pid = EngMapEnt_Add(&s->doc, ENGMAPENT_PROP);
            if (pid < 0) return;
            Eng_SetPos(&s->doc, pid, p.x, p.z);
            Eng_SetYaw(&s->doc, pid, 0.0f);
            Eng_SetScale(&s->doc, pid, 1.0f);
            // Stamp the active prop id (from the catalog) directly on the typed
            // pointer; falls back to the first known prop if none is armed.
            {
                EngMapEntHandle h = EngMapEnt_Find(&s->doc, pid);
                MapDocProp *prop = (MapDocProp *)EngMapEnt_Ptr(&s->doc, h, NULL);
                if (prop) {
                    const char *id = s->placePropId[0] ? s->placePropId : "obstacle_barrel";
                    snprintf(prop->name, sizeof prop->name, "%s", id);
                }
            }
            Eng_SetSector(&s->doc, pid, SectorAt(s, p.x, p.z));
            SelSetSingle(s, pid);
            EdScene_Commit(s);
            break;
        }
        case ED_PLACE_SECTOR:
            // Sectors use RECT-drag (UpdateSectorDrag), not click-to-drop, so the
            // SECTOR tool never reaches PlaceAt — see EdScene_UpdateViewport.
            break;
        case ED_PLACE_WALLBUY: {
            int wid = EngMapEnt_Add(&s->doc, ENGMAPENT_WALLBUY);
            if (wid < 0) return;
            Eng_SetPos(&s->doc, wid, p.x, p.z);
            // Set default weapon string directly on the typed pointer.
            {
                EngMapEntHandle h = EngMapEnt_Find(&s->doc, wid);
                MapDocWallbuy *wb = (MapDocWallbuy *)EngMapEnt_Ptr(&s->doc, h, NULL);
                if (wb) {
                    const char *wid = s->placeWeaponId[0] ? s->placeWeaponId : "PISTOL";
                    snprintf(wb->weapon, sizeof wb->weapon, "%.*s", (int)(sizeof wb->weapon - 1), wid);
                    // Default facing: point away from the origin (toward nearest wall).
                    snprintf(wb->dir, sizeof wb->dir, "%s", FacingToward(p));
                }
            }
            Eng_SetSector(&s->doc, wid, SectorAt(s, p.x, p.z));
            SelSetSingle(s, wid);
            EdScene_Commit(s);
            break;
        }
        case ED_PLACE_PERK: {
            int kid = EngMapEnt_Add(&s->doc, ENGMAPENT_PERK);
            if (kid < 0) return;
            Eng_SetPos(&s->doc, kid, p.x, p.z);
            // Set default perk string directly on the typed pointer.
            {
                EngMapEntHandle h = EngMapEnt_Find(&s->doc, kid);
                MapDocPerk *pk = (MapDocPerk *)EngMapEnt_Ptr(&s->doc, h, NULL);
                if (pk) {
                    const char *kid = s->placePerkId[0] ? s->placePerkId : "JUG";
                    snprintf(pk->perk, sizeof pk->perk, "%.*s", (int)(sizeof pk->perk - 1), kid);
                }
            }
            Eng_SetSector(&s->doc, kid, SectorAt(s, p.x, p.z));
            SelSetSingle(s, kid);
            EdScene_Commit(s);
            break;
        }
        default: break;
    }
}

// Ground point under the cursor, snapped to the grid. Returns false if the pick
// ray misses the y=0 plane (e.g. camera looking at the sky).
static bool GroundPoint(EdScene *s, Vector3 *out) {
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    if (!Eng_PickRayGroundY(ray, 0.0f, out)) return false;
    out->x = EdSnap(s, out->x);
    out->z = EdSnap(s, out->z);
    return true;
}

// RECT-drag sector authoring: press to anchor a corner, drag to size the
// footprint (live preview drawn in EdScene_DrawViewport), release to create.
// A click (or a too-thin drag) falls back to a default 20×20 at the anchor so
// the SECTOR tool still works without dragging. Created sectors own no parent
// sector (no Eng_SetSector), get FLAT heights at y=0, and commit one undo step.
void UpdateSectorDrag(EdScene *s) {
    Vector3 g;
    if (!s->sectorDragging) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && GroundPoint(s, &g)) {
            s->sectorDragging = true;
            s->sectorStartX = s->sectorCurX = g.x;
            s->sectorStartZ = s->sectorCurZ = g.z;
        }
        return;
    }
    if (GroundPoint(s, &g)) { s->sectorCurX = g.x; s->sectorCurZ = g.z; }
    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) return;

    s->sectorDragging = false;
    float minX = fminf(s->sectorStartX, s->sectorCurX), maxX = fmaxf(s->sectorStartX, s->sectorCurX);
    float minZ = fminf(s->sectorStartZ, s->sectorCurZ), maxZ = fmaxf(s->sectorStartZ, s->sectorCurZ);
    float sx = maxX - minX, sz = maxZ - minZ, cx, cz;
    if (sx < ED_SECTOR_MIN_SIZE || sz < ED_SECTOR_MIN_SIZE) {   // click → default footprint
        sx = sz = 20.0f; cx = s->sectorStartX; cz = s->sectorStartZ;
    } else {
        cx = (minX + maxX) * 0.5f; cz = (minZ + maxZ) * 0.5f;
    }
    int sid = EngMapEnt_Add(&s->doc, ENGMAPENT_SECTOR);
    if (sid < 0) return;
    Eng_SetPos(&s->doc, sid, cx, cz);
    Eng_SetSectorSize(&s->doc, sid, sx, sz);
    Eng_SetSectorHeights(&s->doc, sid, 0.0f, 0.0f);
    SelSetSingle(s, sid);
    EdScene_Commit(s);
}
