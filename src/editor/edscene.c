// ============================================================================
//  edscene.c — implementation of the editor's core document + viewport.
//  See edscene.h for the role split (scene = document + 3D view; shell = UI).
// ============================================================================

#include "edscene.h"
#include "edscene_internal.h"

#include "raymath.h"
#include "pick.h"
#include "debugdraw.h"
#include "gfx.h"
#include "edthumb.h"      // asset-browser thumbnail cache (freed at shutdown)
#include "edproject.h"    // Play Test: resolve the game binary from the manifest
#include "content.h"      // Eng_GetGameRoot — the open game's directory

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <spawn.h>     // posix_spawn — Play Test launches the game as a child
#include <signal.h>    // SIG_IGN SIGCHLD — auto-reap finished play-test children

extern char **environ;  // pass the editor's environment to the spawned game

#define ED_WALL_H    3.0f   // assumed wall height for drawing/picking
#define ED_ORBIT_DIST 200.0f
#define ED_SEL_SECONDARY ((Color){ 255, 140, 0, 255 })  // non-primary multi-selection outline

// Settings persistence (editor.cfg) lives in edsettings.c.
// Material-mode rendering lives in edmat.c; placement lives in edplace.c. The
// few helpers those siblings share with this file are declared in
// edscene_internal.h (and defined non-static below).

float EdSnap(EdScene *s, float v) {
    if (!s->snapEnabled || s->snapStep <= 0.0f) return v;
    return roundf(v / s->snapStep) * s->snapStep;
}

// ---- proxy build (MapDoc → selectable boxes) -------------------------------

float SectorFloorY(EdScene *s, int sectorId) {
    if (sectorId < 0 || sectorId >= s->doc.sectorCount) return 0.0f;
    return s->doc.sectors[sectorId].yLow;
}

static BoundingBox BoxAround(float cx, float cy, float cz, float hx, float hy, float hz) {
    return (BoundingBox){ { cx - hx, cy - hy, cz - hz }, { cx + hx, cy + hy, cz + hz } };
}

static void AddProxy(EdScene *s, int id, EngMapEntKind kind, BoundingBox box, Color col) {
    if (s->proxyCount >= ED_MAX_ENTS) return;
    s->proxies[s->proxyCount++] = (EdProxy){ id, kind, box, col };
}

void EdScene_RebuildProxies(EdScene *s) {
    s->proxyCount = 0;
    const MapDoc *d = &s->doc;

    for (int i = 0; i < d->sectorCount; i++) {
        const MapDocSector *sc = &d->sectors[i];
        float cy = (sc->yLow + sc->yHigh) * 0.5f;
        AddProxy(s, sc->id, ENGMAPENT_SECTOR,
                 BoxAround(sc->x, cy, sc->z, sc->sx * 0.5f, 0.1f, sc->sz * 0.5f),
                 (Color){ 60, 70, 90, 255 });
    }
    for (int i = 0; i < d->wallCount; i++) {
        const MapDocWall *w = &d->walls[i];
        float y  = SectorFloorY(s, w->sectorId);
        float cx = (w->x1 + w->x2) * 0.5f, cz = (w->z1 + w->z2) * 0.5f;
        float hx = fabsf(w->x2 - w->x1) * 0.5f + 0.15f;
        float hz = fabsf(w->z2 - w->z1) * 0.5f + 0.15f;
        AddProxy(s, w->id, ENGMAPENT_WALL,
                 BoxAround(cx, y + ED_WALL_H * 0.5f, cz, hx, ED_WALL_H * 0.5f, hz),
                 (Color){ 150, 140, 120, 255 });
    }
    for (int i = 0; i < d->obstacleCount; i++) {
        const MapDocObstacle *o = &d->obstacles[i];
        float y = SectorFloorY(s, o->sectorId);
        AddProxy(s, o->id, ENGMAPENT_OBSTACLE,
                 BoxAround(o->x, y + o->h * 0.5f, o->z, o->sx * 0.5f, o->h * 0.5f, o->sz * 0.5f),
                 (Color){ 120, 110, 90, 255 });
    }
    for (int i = 0; i < d->propCount; i++) {
        const MapDocProp *p = &d->props[i];
        float y = SectorFloorY(s, p->sectorId);
        float h = ED_MARKER * p->scale;
        AddProxy(s, p->id, ENGMAPENT_PROP,
                 BoxAround(p->x, y + h, p->z, h, h, h), (Color){ 90, 160, 110, 255 });
    }
    for (int i = 0; i < d->spawnCount; i++) {
        const MapDocSpawn *sp = &d->spawns[i];
        float y = SectorFloorY(s, sp->sectorId);
        bool player = (strcmp(sp->mob, "PLAYER") == 0);
        AddProxy(s, sp->id, ENGMAPENT_SPAWN,
                 BoxAround(sp->x, y + ED_MARKER, sp->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 player ? (Color){ 90, 130, 200, 255 } : (Color){ 200, 80, 80, 255 });
    }
    for (int i = 0; i < d->windowCount; i++) {
        const MapDocWindow *w = &d->windows[i];
        float y = SectorFloorY(s, w->sectorId);
        AddProxy(s, w->id, ENGMAPENT_WINDOW,
                 BoxAround(w->x, y + ED_MARKER, w->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 200, 170, 90, 255 });
    }
    for (int i = 0; i < d->wallbuyCount; i++) {
        const MapDocWallbuy *w = &d->wallbuys[i];
        float y = SectorFloorY(s, w->sectorId);
        AddProxy(s, w->id, ENGMAPENT_WALLBUY,
                 BoxAround(w->x, y + ED_MARKER, w->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 200, 120, 90, 255 });
    }
    for (int i = 0; i < d->perkCount; i++) {
        const MapDocPerk *p = &d->perks[i];
        float y = SectorFloorY(s, p->sectorId);
        AddProxy(s, p->id, ENGMAPENT_PERK,
                 BoxAround(p->x, y + ED_MARKER, p->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 170, 90, 200, 255 });
    }
}

static Vector3 ForwardFrom(float yaw, float pitch);   // defined with the cameras below

// Fit the camera to an arbitrary world AABB: centre the focus, set orthoH (ortho)
// or pull-back distance (fly). Shared by frame-all (union box) and frame-selected
// (one proxy box). Rotation-independent so it holds at any iso yaw.
static void FrameBox(EdScene *s, BoundingBox b) {
    Vector3 fwd = (s->view == ED_VIEW_TOP) ? (Vector3){ 0, -1, 0 }
                                           : ForwardFrom(s->yaw, s->pitch);
    Vector3 c = { (b.min.x + b.max.x) * 0.5f, (b.min.y + b.max.y) * 0.5f,
                  (b.min.z + b.max.z) * 0.5f };
    s->focus = c;

    float sx = b.max.x - b.min.x, sy = b.max.y - b.min.y, sz = b.max.z - b.min.z;
    // Footprint diagonal guarantees the bounds fit whatever yaw the iso camera is
    // at (a plain max would clip on the diagonal). Add the vertical extent so tall
    // multi-floor content still fits.
    float footprint = sqrtf(sx * sx + sz * sz);
    float aspect = (s->vpH > 0) ? (float)s->vpW / (float)s->vpH : 1.6f;

    float fitH = fmaxf(footprint, sy);
    if (aspect > 0.01f && footprint / aspect > fitH) fitH = footprint / aspect;
    s->orthoH = fminf(400.0f, fmaxf(2.0f, fitH * 1.15f + 4.0f));   // clamp to zoom range

    if (s->view == ED_VIEW_FLY) {
        float radius = fmaxf(footprint, sy) * 0.5f + 1.0f;
        float dist = radius / tanf(s->viewFov * 0.5f * DEG2RAD) + 4.0f;
        s->cam.position = Vector3Subtract(c, Vector3Scale(fwd, dist));
    }
}

void EdScene_FrameAll(EdScene *s) {
    s->framePending = false;
    if (s->proxyCount == 0) {        // empty map → recenter on the origin
        Vector3 fwd = (s->view == ED_VIEW_TOP) ? (Vector3){ 0, -1, 0 }
                                               : ForwardFrom(s->yaw, s->pitch);
        s->focus = (Vector3){ 0, 0, 0 };
        s->orthoH = s->viewOrthoH;
        s->cam.position = Vector3Subtract(s->focus, Vector3Scale(fwd, ED_ORBIT_DIST));
        return;
    }
    // Union of every proxy box → the document's world bounds.
    BoundingBox b = s->proxies[0].box;
    for (int i = 1; i < s->proxyCount; i++) {
        BoundingBox o = s->proxies[i].box;
        b.min.x = fminf(b.min.x, o.min.x); b.max.x = fmaxf(b.max.x, o.max.x);
        b.min.y = fminf(b.min.y, o.min.y); b.max.y = fmaxf(b.max.y, o.max.y);
        b.min.z = fminf(b.min.z, o.min.z); b.max.z = fmaxf(b.max.z, o.max.z);
    }
    FrameBox(s, b);
}

// Frame the PRIMARY selection (its proxy box). No-op if nothing is selected or
// its proxy isn't built yet. A small point entity gets a minimum box so the zoom
// doesn't slam to the 2-unit clamp.
void EdScene_FrameSelected(EdScene *s) {
    const EdProxy *p = (s->selectedId >= 0) ? FindProxy(s, s->selectedId) : NULL;
    if (!p) return;
    BoundingBox b = p->box;
    const float MINH = 3.0f;   // half-extent floor so tiny markers don't over-zoom
    if (b.max.x - b.min.x < MINH) { b.min.x -= MINH; b.max.x += MINH; }
    if (b.max.z - b.min.z < MINH) { b.min.z -= MINH; b.max.z += MINH; }
    FrameBox(s, b);
}

const EdProxy *FindProxy(EdScene *s, int id) {
    for (int i = 0; i < s->proxyCount; i++)
        if (s->proxies[i].id == id) return &s->proxies[i];
    return NULL;
}

// ---- selection set ---------------------------------------------------------
// Internal helpers maintain the invariant that selectedId == selIds[selCount-1]
// (the last-added member is the primary) and selectedId == -1 ⇔ selCount == 0.

static int SelIndexOf(const EdScene *s, int id) {
    for (int i = 0; i < s->selCount; i++) if (s->selIds[i] == id) return i;
    return -1;
}

void EdScene_ClearSelection(EdScene *s) { s->selCount = 0; s->selectedId = -1; }

static void SelAdd(EdScene *s, int id) {
    if (id < 0 || s->selCount >= ED_MAX_ENTS) return;
    if (SelIndexOf(s, id) < 0) s->selIds[s->selCount++] = id;
    s->selectedId = id;   // newest member is the primary
}

static void SelRemove(EdScene *s, int id) {
    int idx = SelIndexOf(s, id);
    if (idx < 0) return;
    for (int i = idx; i < s->selCount - 1; i++) s->selIds[i] = s->selIds[i + 1];
    s->selCount--;
    s->selectedId = s->selCount > 0 ? s->selIds[s->selCount - 1] : -1;
}

void SelSetSingle(EdScene *s, int id) {
    s->selCount = 0;
    if (id >= 0) s->selIds[s->selCount++] = id;
    s->selectedId = id >= 0 ? id : -1;
}

bool EdScene_IsSelected(const EdScene *s, int id) { return SelIndexOf(s, id) >= 0; }
int  EdScene_SelCount(const EdScene *s)           { return s->selCount; }

void EdScene_SelectClick(EdScene *s, int id, bool additive) {
    if (additive) { if (id >= 0) (SelIndexOf(s, id) >= 0 ? SelRemove : SelAdd)(s, id); }
    else          SelSetSingle(s, id);
}

void EdScene_SelectAll(EdScene *s) {
    EdScene_ClearSelection(s);
    for (int i = 0; i < s->proxyCount; i++) SelAdd(s, s->proxies[i].id);
}

const char *EdScene_KindName(EngMapEntKind k) {
    switch (k) {
        case ENGMAPENT_SPAWN:    return "spawn";
        case ENGMAPENT_WALL:     return "wall";
        case ENGMAPENT_WINDOW:   return "window";
        case ENGMAPENT_OBSTACLE: return "obstacle";
        case ENGMAPENT_PROP:     return "prop";
        case ENGMAPENT_WALLBUY:  return "wallbuy";
        case ENGMAPENT_PERK:     return "perk";
        case ENGMAPENT_SECTOR:   return "sector";
        default:                 return "none";
    }
}

// ---- selection origin (gizmo pivot) ----------------------------------------

static bool SelectedOrigin(EdScene *s, Vector3 *out) {
    if (s->selectedId < 0) return false;
    const EdProxy *p = FindProxy(s, s->selectedId);
    if (!p) return false;
    float cx, cz;
    if (!Eng_GetPos(&s->doc, s->selectedId, &cx, &cz)) return false;
    float cy = (p->box.min.y + p->box.max.y) * 0.5f;
    *out = (Vector3){ cx, cy, cz };
    return true;
}

// ---- cameras (fly / orbit / top) -------------------------------------------

static Vector3 ForwardFrom(float yaw, float pitch) {
    return (Vector3){ cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw) };
}

static void ViewBasis(Vector3 fwd, Vector3 *right, Vector3 *up) {
    Vector3 worldUp = (fabsf(fwd.y) > 0.999f) ? (Vector3){ 0, 0, -1 } : (Vector3){ 0, 1, 0 };
    *right = Vector3Normalize(Vector3CrossProduct(fwd, worldUp));
    *up    = Vector3Normalize(Vector3CrossProduct(*right, fwd));
}

// Derive the raylib camera purely from view state (focus/orthoH/yaw/pitch/view),
// no input. Shared by the interactive camera updates and the off-viewport path so
// the camera always tracks focus/orthoH (frame-all, menu view-switch, --shot).
static void DeriveCamera(EdScene *s) {
    if (s->view == ED_VIEW_FLY) {
        Vector3 fwd = ForwardFrom(s->yaw, s->pitch);
        s->cam.target     = Vector3Add(s->cam.position, fwd);
        s->cam.up         = (Vector3){ 0, 1, 0 };
        s->cam.fovy       = s->viewFov;
        s->cam.projection = CAMERA_PERSPECTIVE;
        s->focus = Vector3Add(s->cam.position, Vector3Scale(fwd, 20.0f));
    } else {
        Vector3 fwd = (s->view == ED_VIEW_TOP) ? (Vector3){ 0, -1, 0 }
                                               : ForwardFrom(s->yaw, s->pitch);
        Vector3 right, up;
        ViewBasis(fwd, &right, &up);
        s->cam.position   = Vector3Subtract(s->focus, Vector3Scale(fwd, ED_ORBIT_DIST));
        s->cam.target     = s->focus;
        s->cam.up         = up;
        s->cam.fovy       = s->orthoH;
        s->cam.projection = CAMERA_ORTHOGRAPHIC;
    }
}

void EdScene_SwitchView(EdScene *s, EdViewMode m) {
    if (m == s->view) return;
    if (m == ED_VIEW_FLY) {
        if (s->view == ED_VIEW_TOP) s->pitch = -1.2f;
        Vector3 fwd = ForwardFrom(s->yaw, s->pitch);
        s->cam.position = Vector3Subtract(s->focus, Vector3Scale(fwd, 20.0f));
        if (s->looking) { s->looking = false; ShowCursor(); }
    } else if (m == ED_VIEW_TOP) {
        s->pitch = -PI * 0.5f;
    } else if (m == ED_VIEW_ORBIT && s->view == ED_VIEW_TOP) {
        s->pitch = -0.9f;
    }
    s->view = m;
}

// Fly camera: use HideCursor()/ShowCursor() + per-frame viewport-centre warp
// instead of DisableCursor()/EnableCursor().  DisableCursor locks the cursor
// to the WINDOW centre (not the viewport centre), which on Wayland is
// asynchronous and temporarily blocks all cursor events; on every platform it
// means ptIn is false for the frame after the warp, relying on vpActive/looking
// to keep vpInput true.  The simpler and more IDE-friendly approach: hide the
// cursor while looking, read GetMouseDelta() normally (always frame-accurate),
// then warp back to the viewport centre so the cursor stays inside the viewport
// and edge-clamping never limits look range.
static void UpdateCamFly(EdScene *s, float dt, Rectangle vp) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { s->looking = true;  HideCursor(); }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && s->looking) { s->looking = false; ShowCursor(); }

    if (s->looking) {
        Vector2 md = GetMouseDelta();
        s->yaw   += md.x * s->camLookSens;
        s->pitch -= md.y * s->camLookSens;
        if (s->pitch >  1.55f) s->pitch =  1.55f;
        if (s->pitch < -1.55f) s->pitch = -1.55f;
        // Warp cursor to viewport centre so subsequent GetMouseDelta() measures
        // from a stable, in-viewport reference point (prevents edge-clamping).
        SetMousePosition((int)(vp.x + vp.width  * 0.5f),
                         (int)(vp.y + vp.height * 0.5f));
    }

    Vector3 fwd   = ForwardFrom(s->yaw, s->pitch);
    Vector3 right = { cosf(s->yaw), 0, sinf(s->yaw) };
    Vector3 up    = { 0, 1, 0 };
    Vector3 move  = { 0 };
    if (s->looking) {
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
        if (IsKeyDown(KEY_E)) move = Vector3Add(move, up);
        if (IsKeyDown(KEY_Q)) move = Vector3Subtract(move, up);
    }
    float speed = IsKeyDown(KEY_LEFT_SHIFT) ? s->camFlyBoost : s->camFlySpeed;
    if (Vector3LengthSqr(move) > 1e-4f)
        s->cam.position = Vector3Add(s->cam.position, Vector3Scale(Vector3Normalize(move), speed * dt));

    DeriveCamera(s);
}

static void UpdateCamOrtho(EdScene *s, bool allowRotate) {
    if (allowRotate && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 md = GetMouseDelta();
        s->yaw   += md.x * s->camOrbitSens;
        s->pitch -= md.y * s->camOrbitSens;
        if (s->pitch < -1.55f) s->pitch = -1.55f;
        if (s->pitch >  1.55f) s->pitch =  1.55f;
    }

    Vector3 fwd = (s->view == ED_VIEW_TOP) ? (Vector3){ 0, -1, 0 }
                                           : ForwardFrom(s->yaw, s->pitch);
    Vector3 right, up;
    ViewBasis(fwd, &right, &up);

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 md = GetMouseDelta();
        float wpp = s->orthoH / (float)s->vpH;
        s->focus = Vector3Add(s->focus, Vector3Scale(right, -md.x * wpp));
        s->focus = Vector3Add(s->focus, Vector3Scale(up,     md.y * wpp));
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        s->orthoH *= (1.0f - wheel * s->camZoomSpeed);
        if (s->orthoH <   2.0f) s->orthoH =   2.0f;
        if (s->orthoH > 400.0f) s->orthoH = 400.0f;
    }

    DeriveCamera(s);
}

static void EdUpdateCamera(EdScene *s, float dt, Rectangle vp, bool allowInput) {
    if (allowInput) {
        switch (s->view) {
            case ED_VIEW_FLY:   UpdateCamFly(s, dt, vp);  break;
            case ED_VIEW_ORBIT: UpdateCamOrtho(s, true);  break;
            case ED_VIEW_TOP:   UpdateCamOrtho(s, false); break;
        }
    } else {
        // Mouse isn't driving the camera this frame, but still re-derive it from
        // focus/orthoH/view so a frame-all or menu view-switch made off-viewport
        // is reflected immediately (and so headless --shot framing works).
        DeriveCamera(s);
    }
}

// ---- pick selection --------------------------------------------------------

static void PickSelection(EdScene *s, bool additive) {
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    int   best = -1;
    float bestT = 1e30f;
    for (int i = 0; i < s->proxyCount; i++) {
        float t;
        if (Eng_PickRayAABB(ray, s->proxies[i].box, &t) && t < bestT) {
            bestT = t; best = s->proxies[i].id;
        }
    }
    // Shift-click into empty space is a no-op (keeps the current set); a plain
    // click into empty space clears. Hits toggle (additive) or replace.
    if (additive && best < 0) return;
    EdScene_SelectClick(s, best, additive);
}

// ---- commit + sector query (placement itself lives in edplace.c) -----------

// Monotonic coalesce-token source; see EdScene_NextTag. Lives here (above the
// first user) so both the commit helpers and the gizmo drag code can reach it.
static uint32_t g_nextTag = 1;

void EdScene_Commit(EdScene *s) { EdScene_CommitTagged(s, 0); }

// Tagged commit: consecutive commits sharing a non-zero tag coalesce into one
// undo step (the EngMapHistory protocol). Tag 0 always pushes a fresh step.
void EdScene_CommitTagged(EdScene *s, uint32_t tag) {
    EngMapHistory_Commit(&s->hist, &s->doc, tag);
    s->dirty = true;
}

// Hand out a fresh, never-zero coalesce token. Shared by every continuous edit
// (gizmo drags, inspector sliders) so two unrelated interactions can never reuse
// a tag and accidentally merge into one undo step.
uint32_t EdScene_NextTag(EdScene *s) {
    (void)s;
    uint32_t t = g_nextTag++;
    if (g_nextTag == 0) g_nextTag = 1;
    return t;
}

// Strict variant for ramp link-picking: the sector whose footprint contains
// (x,z), excluding index `excl` (the ramp itself); -1 when none contains it (so
// clicking empty space / the ramp clears the link). No fallback-to-0.
static int SectorContainingExcl(EdScene *s, float x, float z, int excl) {
    for (int i = 0; i < s->doc.sectorCount; i++) {
        if (i == excl) continue;
        const MapDocSector *sc = &s->doc.sectors[i];
        if (fabsf(x - sc->x) <= sc->sx * 0.5f && fabsf(z - sc->z) <= sc->sz * 0.5f) return i;
    }
    return -1;
}

void EdScene_CycleSelected(EdScene *s) {
    if (s->selectedId < 0) return;
    EngMapEntKind k; EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    if (k == ENGMAPENT_WINDOW) {
        char d[4]; Eng_GetWindowDir(&s->doc, s->selectedId, d, sizeof d);
        const char *order[4] = { "+x", "+z", "-x", "-z" };
        int cur = 0; for (int i = 0; i < 4; i++) if (!strcmp(d, order[i])) cur = i;
        Eng_SetWindowDir(&s->doc, s->selectedId, order[(cur + 1) % 4]);
        EdScene_Commit(s);
    } else if (k == ENGMAPENT_SPAWN) {
        char m[MAPDOC_SPAWN_MOB_LEN]; Eng_GetSpawnMob(&s->doc, s->selectedId, m, sizeof m);
        Eng_SetSpawnMob(&s->doc, s->selectedId, strcmp(m, "PLAYER") == 0 ? "ZOMBIE" : "PLAYER");
        EdScene_Commit(s);
    }
}

void EdScene_DeleteSelected(EdScene *s) {
    if (s->selCount == 0) return;
    // Snapshot the ids first: EngMapEnt_Delete compacts arrays but ids are
    // stable, so deleting by id in any order is safe.
    int ids[ED_MAX_ENTS], n = s->selCount;
    memcpy(ids, s->selIds, (size_t)n * sizeof(int));
    EdScene_ClearSelection(s);
    for (int i = 0; i < n; i++) EngMapEnt_Delete(&s->doc, ids[i]);
    EdScene_Commit(s);
}

// Position nudge for duplicate/paste: one grid cell when snapping, else 2 units.
static float EdNudge(const EdScene *s) { return s->snapEnabled ? s->snapStep : 2.0f; }

void EdScene_DuplicateSelected(EdScene *s) {
    if (s->selCount == 0) return;
    int src[ED_MAX_ENTS], srcN = s->selCount;
    memcpy(src, s->selIds, (size_t)srcN * sizeof(int));
    float d = EdNudge(s);

    int clones[ED_MAX_ENTS], n = 0;
    for (int i = 0; i < srcN; i++) {
        int nid = EngMapEnt_Clone(&s->doc, src[i]);
        if (nid < 0) continue;   // kind at cap → skip
        float x, z;
        if (Eng_GetPos(&s->doc, nid, &x, &z)) Eng_SetPos(&s->doc, nid, x + d, z + d);
        clones[n++] = nid;
    }
    if (n == 0) return;
    EdScene_ClearSelection(s);
    for (int i = 0; i < n; i++) SelAdd(s, clones[i]);
    EdScene_Commit(s);
}

void EdScene_CopySelection(EdScene *s) {
    s->clipCount = 0;
    s->clipPasteSeq = 0;
    for (int i = 0; i < s->selCount && s->clipCount < ED_MAX_ENTS; i++) {
        EngMapEntHandle h = EngMapEnt_Find(&s->doc, s->selIds[i]);
        if (h.kind == ENGMAPENT_NONE) continue;
        EdClipEnt *c = &s->clip[s->clipCount++];
        c->kind = h.kind;
        switch (h.kind) {
            case ENGMAPENT_SPAWN:    c->u.spawn    = s->doc.spawns[h.index];    break;
            case ENGMAPENT_WALL:     c->u.wall     = s->doc.walls[h.index];     break;
            case ENGMAPENT_WINDOW:   c->u.window   = s->doc.windows[h.index];   break;
            case ENGMAPENT_OBSTACLE: c->u.obstacle = s->doc.obstacles[h.index]; break;
            case ENGMAPENT_PROP:     c->u.prop     = s->doc.props[h.index];     break;
            case ENGMAPENT_WALLBUY:  c->u.wallbuy  = s->doc.wallbuys[h.index];  break;
            case ENGMAPENT_PERK:     c->u.perk     = s->doc.perks[h.index];     break;
            case ENGMAPENT_SECTOR:   c->u.sector   = s->doc.sectors[h.index];   break;
            default: s->clipCount--; break;
        }
    }
}

void EdScene_Cut(EdScene *s) {
    if (s->selCount == 0) return;
    EdScene_CopySelection(s);
    EdScene_DeleteSelected(s);   // commits
}

void EdScene_Paste(EdScene *s) {
    if (s->clipCount == 0) return;
    // Fan successive pastes out by one nudge step each so they don't stack.
    float off = EdNudge(s) * (float)(++s->clipPasteSeq);

    int pasted[ED_MAX_ENTS], n = 0;
    for (int i = 0; i < s->clipCount; i++) {
        const EdClipEnt *c = &s->clip[i];
        int nid = EngMapEnt_Add(&s->doc, c->kind);
        if (nid < 0) continue;
        EngMapEntHandle h = EngMapEnt_Find(&s->doc, nid);
        switch (c->kind) {
            case ENGMAPENT_SPAWN:    s->doc.spawns[h.index]    = c->u.spawn;    s->doc.spawns[h.index].id    = nid; break;
            case ENGMAPENT_WALL:     s->doc.walls[h.index]     = c->u.wall;     s->doc.walls[h.index].id     = nid; break;
            case ENGMAPENT_WINDOW:   s->doc.windows[h.index]   = c->u.window;   s->doc.windows[h.index].id   = nid; break;
            case ENGMAPENT_OBSTACLE: s->doc.obstacles[h.index] = c->u.obstacle; s->doc.obstacles[h.index].id = nid; break;
            case ENGMAPENT_PROP:     s->doc.props[h.index]     = c->u.prop;     s->doc.props[h.index].id     = nid; break;
            case ENGMAPENT_WALLBUY:  s->doc.wallbuys[h.index]  = c->u.wallbuy;  s->doc.wallbuys[h.index].id  = nid; break;
            case ENGMAPENT_PERK:     s->doc.perks[h.index]     = c->u.perk;     s->doc.perks[h.index].id     = nid; break;
            case ENGMAPENT_SECTOR:   s->doc.sectors[h.index]   = c->u.sector;   s->doc.sectors[h.index].id   = nid; break;
            default: EngMapEnt_Delete(&s->doc, nid); continue;
        }
        // Offset walls by both endpoints; all other kinds carry a single x/z.
        if (c->kind == ENGMAPENT_WALL) {
            MapDocWall *w = &s->doc.walls[h.index];
            w->x1 += off; w->z1 += off; w->x2 += off; w->z2 += off;
        } else {
            float x, z;
            if (Eng_GetPos(&s->doc, nid, &x, &z)) Eng_SetPos(&s->doc, nid, x + off, z + off);
        }
        pasted[n++] = nid;
    }
    if (n == 0) return;
    EdScene_ClearSelection(s);
    for (int i = 0; i < n; i++) SelAdd(s, pasted[i]);
    EdScene_Commit(s);
}

void EdScene_Undo(EdScene *s) { if (EngMapHistory_Undo(&s->hist, &s->doc)) s->dirty = true; }
void EdScene_Redo(EdScene *s) { if (EngMapHistory_Redo(&s->hist, &s->doc)) s->dirty = true; }

// ---- gizmo drag (translate) → mapedit → history ----------------------------

static float GizmoHandleSize(EdScene *s, Vector3 origin) {
    if (s->view == ED_VIEW_FLY)
        return Vector3Distance(s->cam.position, origin) * 0.12f + 0.5f;
    return s->orthoH * 0.06f + 0.5f;
}

// Does the active gizmo mode apply to the current selection? TRANSLATE works for
// every kind; ROTATE/SCALE only make sense for PROPs (the only kind carrying yaw
// + scale). Used to suppress a dead rotate/scale gizmo here and to flag "(n/a)"
// in the status bar (st_view, edpanels.c) so the no-op is no longer silent.
bool EdScene_GizmoModeApplies(const EdScene *s) {
    if (s->selectedId < 0) return false;
    if (s->mode == ENG_GIZMO_TRANSLATE) return true;
    EngMapEntKind k;
    EngMapEnt_PtrConst(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    return (k == ENGMAPENT_PROP);
}

static void UpdateGizmo(EdScene *s, bool allowGrab) {
    Vector3 origin;
    if (!SelectedOrigin(s, &origin)) { s->dragging = false; return; }

    float handleSize = GizmoHandleSize(s, origin);
    Vector2 mouse = s->vpMouse;

    if (!s->dragging) {
        // Don't draw or arm a gizmo the selection can't use (rotate/scale on a
        // non-prop) — a phantom handle that rejects every drag is worse than none.
        if (!EdScene_GizmoModeApplies(s)) return;

        EngGizmoAxis hot = Eng_GizmoHitTest(s->cam, mouse, s->vpW, s->vpH, origin, s->mode, handleSize);
        Eng_GizmoDebugDraw(origin, s->mode, handleSize, hot);

        if (allowGrab && hot != ENG_GIZMO_AXIS_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s->drag = Eng_GizmoBeginDrag(s->cam, mouse, s->vpW, s->vpH, origin, s->mode, hot);
            if (s->drag.axis != ENG_GIZMO_AXIS_NONE) {
                float x, z; Eng_GetPos(&s->doc, s->selectedId, &x, &z);
                s->dragStartPos = (Vector3){ x, origin.y, z };
                // Snapshot every selected entity's start pos so a TRANSLATE
                // drag moves the whole set rigidly by the primary's delta.
                for (int i = 0; i < s->selCount; i++) {
                    float sx = 0, sz = 0;
                    Eng_GetPos(&s->doc, s->selIds[i], &sx, &sz);
                    s->dragStart[i] = (Vector3){ sx, 0, sz };
                }
                Eng_GetYaw(&s->doc, s->selectedId, &s->dragStartYaw);
                Eng_GetScale(&s->doc, s->selectedId, &s->dragStartScale);
                s->dragTag = EdScene_NextTag(s);
                s->dragging = true;
            }
        }
        return;
    }

    Eng_GizmoDebugDraw(origin, s->mode, handleSize, s->drag.axis);
    EngGizmoDelta d = Eng_GizmoUpdateDrag(s->drag, s->cam, mouse, s->vpW, s->vpH);

    switch (s->mode) {
        case ENG_GIZMO_TRANSLATE: {
            // Snap the PRIMARY to the grid, then move every selected entity by
            // that same (snapped) delta so the group keeps its relative layout.
            float nx = EdSnap(s, s->dragStartPos.x + d.translate.x);
            float nz = EdSnap(s, s->dragStartPos.z + d.translate.z);
            float dx = nx - s->dragStartPos.x, dz = nz - s->dragStartPos.z;
            for (int i = 0; i < s->selCount; i++)
                Eng_SetPos(&s->doc, s->selIds[i],
                           s->dragStart[i].x + dx, s->dragStart[i].z + dz);
            break;
        }
        case ENG_GIZMO_ROTATE:
            Eng_SetYaw(&s->doc, s->selectedId, s->dragStartYaw + d.rotateRadians * RAD2DEG);
            break;
        case ENG_GIZMO_SCALE: {
            // d.scale is a per-axis multiplier; pick the component for the active axis.
            float factor;
            switch (s->drag.axis) {
                case ENG_GIZMO_AXIS_Y:  factor = d.scale.y; break;
                case ENG_GIZMO_AXIS_Z:  factor = d.scale.z; break;
                default:                factor = d.scale.x; break;
            }
            float newScale = s->dragStartScale * factor;
            if (newScale < 0.05f) newScale = 0.05f;
            Eng_SetScale(&s->doc, s->selectedId, newScale);
            break;
        }
    }

    EngMapHistory_Commit(&s->hist, &s->doc, s->dragTag);
    s->dirty = true;

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s->dragging = false;
}

// ---- sector edge-handle resize ---------------------------------------------
// Sectors are quads, not point entities, so resizing uses its own handles (one
// per edge: W/E on X, N/S on Z) hit-tested as small AABBs against the pick ray —
// the same ray primitives selection uses, so no screen-projection convention is
// needed. Dragging an edge moves it to the cursor's (snapped) ground position
// while the OPPOSITE edge stays fixed, re-deriving centre+size each frame from
// that fixed coordinate (drift-free, mirroring the gizmo drag convention).
//
// resizeEdge: 0=W(-X) 1=E(+X) 2=N(-Z) 3=S(+Z); resizeFixed = opposite edge coord.

// Primary selection is a sector? Fill its centre + full size.
static bool SelectedSector(EdScene *s, float *cx, float *cz, float *sx, float *sz) {
    if (s->selectedId < 0) return false;
    EngMapEntKind k;
    EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    if (k != ENGMAPENT_SECTOR) return false;
    return Eng_GetPos(&s->doc, s->selectedId, cx, cz) &&
           Eng_GetSectorSize(&s->doc, s->selectedId, sx, sz);
}

// The 4 edge-handle boxes (W,E,N,S), each a cube of half-extent `half` centred
// on its edge midpoint at y=0.
static void SectorHandleBoxes(float cx, float cz, float sx, float sz, float half, BoundingBox out[4]) {
    float xW = cx - sx * 0.5f, xE = cx + sx * 0.5f;
    float zN = cz - sz * 0.5f, zS = cz + sz * 0.5f;
    Vector3 mid[4] = { { xW, 0, cz }, { xE, 0, cz }, { cx, 0, zN }, { cx, 0, zS } };
    for (int i = 0; i < 4; i++) {
        out[i].min = (Vector3){ mid[i].x - half, -half, mid[i].z - half };
        out[i].max = (Vector3){ mid[i].x + half,  half, mid[i].z + half };
    }
}

static float SectorHandleHalf(EdScene *s, float cx, float cz) {
    return fmaxf(0.5f, GizmoHandleSize(s, (Vector3){ cx, 0.0f, cz }) * 0.5f);
}

// On press over a sector edge handle: begin a resize drag. Returns true if a
// handle was hit (caller then skips selection-picking).
static bool BeginSectorResize(EdScene *s) {
    float cx, cz, sx, sz;
    if (!SelectedSector(s, &cx, &cz, &sx, &sz)) return false;
    BoundingBox bb[4];
    SectorHandleBoxes(cx, cz, sx, sz, SectorHandleHalf(s, cx, cz), bb);
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    int best = -1; float bestT = 1e30f, t;
    for (int i = 0; i < 4; i++)
        if (Eng_PickRayAABB(ray, bb[i], &t) && t < bestT) { bestT = t; best = i; }
    if (best < 0) return false;
    switch (best) {
        case 0: s->resizeFixed = cx + sx * 0.5f; break;  // W moves, E fixed
        case 1: s->resizeFixed = cx - sx * 0.5f; break;  // E moves, W fixed
        case 2: s->resizeFixed = cz + sz * 0.5f; break;  // N moves, S fixed
        case 3: s->resizeFixed = cz - sz * 0.5f; break;  // S moves, N fixed
    }
    s->resizeEdge = best;
    s->dragTag = EdScene_NextTag(s);
    s->resizing = true;
    return true;
}

static void UpdateSectorResize(EdScene *s) {
    float cx, cz, sx, sz;
    if (!SelectedSector(s, &cx, &cz, &sx, &sz)) { s->resizing = false; return; }
    Vector3 g;
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    if (Eng_PickRayGroundY(ray, 0.0f, &g)) {
        g.x = EdSnap(s, g.x); g.z = EdSnap(s, g.z);
        const float MIN = ED_SECTOR_MIN_SIZE, fixed = s->resizeFixed;
        if (s->resizeEdge <= 1) {                          // X edge
            float lo, hi;
            if (s->resizeEdge == 0) { lo = fminf(g.x, fixed - MIN); hi = fixed; }
            else                    { lo = fixed; hi = fmaxf(g.x, fixed + MIN); }
            Eng_SetSectorSize(&s->doc, s->selectedId, hi - lo, sz);
            Eng_SetPos(&s->doc, s->selectedId, (lo + hi) * 0.5f, cz);
        } else {                                           // Z edge
            float lo, hi;
            if (s->resizeEdge == 2) { lo = fminf(g.z, fixed - MIN); hi = fixed; }
            else                    { lo = fixed; hi = fmaxf(g.z, fixed + MIN); }
            Eng_SetSectorSize(&s->doc, s->selectedId, sx, hi - lo);
            Eng_SetPos(&s->doc, s->selectedId, cx, (lo + hi) * 0.5f);
        }
        EngMapHistory_Commit(&s->hist, &s->doc, s->dragTag);
        s->dirty = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s->resizing = false;
}

// Draw the selected sector's edge handles (skipped while a placement tool is
// armed). Highlights the hovered handle, or the active one mid-resize.
static void DrawSectorHandles(EdScene *s) {
    if (s->placeTool != ED_PLACE_NONE) return;
    float cx, cz, sx, sz;
    if (!SelectedSector(s, &cx, &cz, &sx, &sz)) return;
    BoundingBox bb[4];
    SectorHandleBoxes(cx, cz, sx, sz, SectorHandleHalf(s, cx, cz), bb);
    int hover = -1;
    if (!s->resizing) {
        Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
        float bestT = 1e30f, t;
        for (int i = 0; i < 4; i++)
            if (Eng_PickRayAABB(ray, bb[i], &t) && t < bestT) { bestT = t; hover = i; }
    }
    for (int i = 0; i < 4; i++) {
        Color c = (s->resizing && i == s->resizeEdge) ? (Color){ 255, 255, 120, 255 }
                : (i == hover)                         ? (Color){ 130, 235, 255, 255 }
                :                                        (Color){ 90, 160, 210, 255 };
        Vector3 ctr = Vector3Scale(Vector3Add(bb[i].min, bb[i].max), 0.5f);
        Vector3 sz3 = Vector3Subtract(bb[i].max, bb[i].min);
        Eng_GfxDrawCubeWiresV(ctr, sz3, c);
    }
}

// ---- wall endpoint handles -------------------------------------------------
// Walls are two-point segments, so (unlike point entities) the translate gizmo
// can only shift the whole wall. Endpoint handles — one cube per end — let each
// vertex be dragged on the ground plane, mirroring the sector edge-handle flow.

// Primary selection is a wall? Return its typed pointer (NULL if not / not found).
static MapDocWall *SelectedWall(EdScene *s) {
    if (s->selectedId < 0) return NULL;
    EngMapEntKind k;
    void *p = EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    return (p && k == ENGMAPENT_WALL) ? (MapDocWall *)p : NULL;
}

// The 2 endpoint-handle boxes, cubes of half-extent `half` at each end (y=0).
static void WallHandleBoxes(const MapDocWall *w, float half, BoundingBox out[2]) {
    Vector3 end[2] = { { w->x1, 0, w->z1 }, { w->x2, 0, w->z2 } };
    for (int i = 0; i < 2; i++) {
        out[i].min = (Vector3){ end[i].x - half, -half, end[i].z - half };
        out[i].max = (Vector3){ end[i].x + half,  half, end[i].z + half };
    }
}

// On press over a wall endpoint handle: begin an endpoint drag. Returns true if a
// handle was hit (caller then skips selection-picking).
static bool BeginWallEdit(EdScene *s) {
    MapDocWall *w = SelectedWall(s);
    if (!w) return false;
    float half = SectorHandleHalf(s, 0.5f * (w->x1 + w->x2), 0.5f * (w->z1 + w->z2));
    BoundingBox bb[2];
    WallHandleBoxes(w, half, bb);
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    int best = -1; float bestT = 1e30f, t;
    for (int i = 0; i < 2; i++)
        if (Eng_PickRayAABB(ray, bb[i], &t) && t < bestT) { bestT = t; best = i; }
    if (best < 0) return false;
    s->wallVert = best;
    s->dragTag = EdScene_NextTag(s);
    s->wallEditing = true;
    return true;
}

static void UpdateWallEdit(EdScene *s) {
    MapDocWall *w = SelectedWall(s);
    if (!w) { s->wallEditing = false; return; }
    Vector3 g;
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    if (Eng_PickRayGroundY(ray, 0.0f, &g)) {
        g.x = EdSnap(s, g.x); g.z = EdSnap(s, g.z);
        if (s->wallVert == 0) { w->x1 = g.x; w->z1 = g.z; }
        else                  { w->x2 = g.x; w->z2 = g.z; }
        EngMapHistory_Commit(&s->hist, &s->doc, s->dragTag);
        s->dirty = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s->wallEditing = false;
}

// Draw the selected wall's endpoint handles (skipped while a placement tool is
// armed). Highlights the hovered handle, or the active one mid-drag.
static void DrawWallHandles(EdScene *s) {
    if (s->placeTool != ED_PLACE_NONE) return;
    MapDocWall *w = SelectedWall(s);
    if (!w) return;
    float half = SectorHandleHalf(s, 0.5f * (w->x1 + w->x2), 0.5f * (w->z1 + w->z2));
    BoundingBox bb[2];
    WallHandleBoxes(w, half, bb);
    int hover = -1;
    if (!s->wallEditing) {
        Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
        float bestT = 1e30f, t;
        for (int i = 0; i < 2; i++)
            if (Eng_PickRayAABB(ray, bb[i], &t) && t < bestT) { bestT = t; hover = i; }
    }
    for (int i = 0; i < 2; i++) {
        Color c = (s->wallEditing && i == s->wallVert) ? (Color){ 255, 255, 120, 255 }
                : (i == hover)                          ? (Color){ 130, 235, 255, 255 }
                :                                         (Color){ 210, 150, 90, 255 };
        Vector3 ctr = Vector3Scale(Vector3Add(bb[i].min, bb[i].max), 0.5f);
        Vector3 sz3 = Vector3Subtract(bb[i].max, bb[i].min);
        Eng_GfxDrawCubeWiresV(ctr, sz3, c);
    }
}

// ---- sector vertical height handle -----------------------------------------
// X/Z resize lives on the ground plane; floor HEIGHT had no viewport handle (it
// was inspector-typing only). This adds one cube floating above the footprint
// centre: dragging it vertically raises/lowers the floor. FLAT keeps yHigh==yLow;
// RAMP shifts both edges by the same delta so its rise is preserved. The vertical
// drag reuses the gizmo's Y-axis translate constraint (drift-free since grab).

static float SectorFloorTop(EdScene *s) {
    float lo, hi; Eng_GetSectorHeights(&s->doc, s->selectedId, &lo, &hi);
    return fmaxf(lo, hi);
}

// Handle centre: above the footprint centre, clear of the floor so it reads as a
// separate grab. `half` is its cube half-extent (also the AABB pick size).
static Vector3 SectorHeightHandlePos(EdScene *s, float cx, float cz, float half) {
    return (Vector3){ cx, SectorFloorTop(s) + half * 5.0f, cz };
}

static bool BeginSectorHeight(EdScene *s) {
    float cx, cz, sx, sz;
    if (!SelectedSector(s, &cx, &cz, &sx, &sz)) return false;
    float half = SectorHandleHalf(s, cx, cz);
    Vector3 hp = SectorHeightHandlePos(s, cx, cz, half);
    BoundingBox bb = { { hp.x - half, hp.y - half, hp.z - half },
                       { hp.x + half, hp.y + half, hp.z + half } };
    Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
    float t;
    if (!Eng_PickRayAABB(ray, bb, &t)) return false;
    s->heightDrag = Eng_GizmoBeginDrag(s->cam, s->vpMouse, s->vpW, s->vpH, hp,
                                       ENG_GIZMO_TRANSLATE, ENG_GIZMO_AXIS_Y);
    if (s->heightDrag.axis == ENG_GIZMO_AXIS_NONE) return false;
    Eng_GetSectorHeights(&s->doc, s->selectedId, &s->heightStartLow, &s->heightStartHigh);
    s->dragTag = EdScene_NextTag(s);
    s->heightEditing = true;
    return true;
}

static void UpdateSectorHeight(EdScene *s) {
    float cx, cz, sx, sz;
    if (!SelectedSector(s, &cx, &cz, &sx, &sz)) { s->heightEditing = false; return; }
    EngGizmoDelta d = Eng_GizmoUpdateDrag(s->heightDrag, s->cam, s->vpMouse, s->vpW, s->vpH);
    float newLow = EdSnap(s, s->heightStartLow + d.translate.y);
    float dy = newLow - s->heightStartLow;
    int kind = SECTOR_FLAT; Eng_GetSectorKind(&s->doc, s->selectedId, &kind);
    if (kind == SECTOR_RAMP)
        Eng_SetSectorHeights(&s->doc, s->selectedId, newLow, s->heightStartHigh + dy);
    else
        Eng_SetSectorHeights(&s->doc, s->selectedId, newLow, newLow);
    EngMapHistory_Commit(&s->hist, &s->doc, s->dragTag);
    s->dirty = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s->heightEditing = false;
}

static void DrawSectorHeightHandle(EdScene *s) {
    if (s->placeTool != ED_PLACE_NONE) return;
    float cx, cz, sx, sz;
    if (!SelectedSector(s, &cx, &cz, &sx, &sz)) return;
    float half = SectorHandleHalf(s, cx, cz);
    Vector3 hp = SectorHeightHandlePos(s, cx, cz, half);
    bool hover = false;
    if (!s->heightEditing) {
        BoundingBox bb = { { hp.x - half, hp.y - half, hp.z - half },
                           { hp.x + half, hp.y + half, hp.z + half } };
        Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
        float t; hover = Eng_PickRayAABB(ray, bb, &t);
    }
    Color c = s->heightEditing ? (Color){ 255, 255, 120, 255 }
            : hover            ? (Color){ 150, 255, 170, 255 }
            :                    (Color){ 110, 210, 140, 255 };
    // Stalk from the floor centre up to the handle, so its height reads clearly.
    Eng_GfxDrawLine3D((Vector3){ cx, SectorFloorTop(s), cz }, hp, c);
    Eng_GfxDrawCubeWiresV(hp, (Vector3){ half * 2, half * 2, half * 2 }, c);
}

// ---- ramp visualization ----------------------------------------------------
// Draw every RAMP sector's inclined surface as a wireframe (perimeter + slope
// rungs) plus an uphill arrow, so ramps read as sloped in the viewport instead
// of looking like a flat sector. Mirrors the game's interpolation (level.c
// DocSectorY): axis 1 rises along +X, axis 2 along +Z; the -axis edge is yLow,
// the +axis edge yHigh. The selected ramp draws brighter.
static void DrawRampOverlay(EdScene *s) {
    const MapDoc *d = &s->doc;
    for (int i = 0; i < d->sectorCount; i++) {
        const MapDocSector *sc = &d->sectors[i];
        if (sc->kind != SECTOR_RAMP) continue;

        float minX = sc->x - sc->sx * 0.5f, maxX = sc->x + sc->sx * 0.5f;
        float minZ = sc->z - sc->sz * 0.5f, maxZ = sc->z + sc->sz * 0.5f;
        bool  axisX = (sc->rampAxis != 2);   // default/1 = +X, 2 = +Z
        if ((axisX ? sc->sx : sc->sz) < 1e-4f) continue;   // degenerate rise → skip (no div0)
        bool  sel   = EdScene_IsSelected(s, sc->id);
        Color col   = sel ? (Color){ 255, 210, 120, 255 } : (Color){ 235, 150, 40, 255 };

        // Surface height at a footprint point (clamped t, like the game).
        #define RAMP_Y(px, pz) (sc->yLow + (sc->yHigh - sc->yLow) * \
            (axisX ? ((px) - minX) / sc->sx : ((pz) - minZ) / sc->sz))

        Vector3 c00 = { minX, RAMP_Y(minX, minZ), minZ };
        Vector3 c10 = { maxX, RAMP_Y(maxX, minZ), minZ };
        Vector3 c11 = { maxX, RAMP_Y(maxX, maxZ), maxZ };
        Vector3 c01 = { minX, RAMP_Y(minX, maxZ), maxZ };
        Eng_GfxDrawLine3D(c00, c10, col);  Eng_GfxDrawLine3D(c10, c11, col);
        Eng_GfxDrawLine3D(c11, c01, col);  Eng_GfxDrawLine3D(c01, c00, col);

        // Slope rungs (constant-height lines) at 1/4, 1/2, 3/4 up the rise.
        for (int r = 1; r <= 3; r++) {
            float t = r * 0.25f;
            if (axisX) {
                float px = minX + sc->sx * t, py = sc->yLow + (sc->yHigh - sc->yLow) * t;
                Eng_GfxDrawLine3D((Vector3){ px, py, minZ }, (Vector3){ px, py, maxZ }, col);
            } else {
                float pz = minZ + sc->sz * t, py = sc->yLow + (sc->yHigh - sc->yLow) * t;
                Eng_GfxDrawLine3D((Vector3){ minX, py, pz }, (Vector3){ maxX, py, pz }, col);
            }
        }

        // Uphill arrow along the rise centre line (low edge → high edge).
        Vector3 base = axisX ? (Vector3){ minX, sc->yLow,  sc->z } : (Vector3){ sc->x, sc->yLow,  minZ };
        Vector3 tip  = axisX ? (Vector3){ maxX, sc->yHigh, sc->z } : (Vector3){ sc->x, sc->yHigh, maxZ };
        Eng_GfxDrawLine3D(base, tip, col);
        Vector3 dir  = Vector3Normalize(Vector3Subtract(tip, base));
        Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, (Vector3){ 0, 1, 0 }));
        float   hl   = fminf(2.0f, fminf(sc->sx, sc->sz) * 0.18f);
        Vector3 back = Vector3Scale(dir, -hl);
        Eng_GfxDrawLine3D(tip, Vector3Add(tip, Vector3Add(back, Vector3Scale(side,  hl * 0.6f))), col);
        Eng_GfxDrawLine3D(tip, Vector3Add(tip, Vector3Add(back, Vector3Scale(side, -hl * 0.6f))), col);

        #undef RAMP_Y
    }
}

// Content catalog scanning (mobs/props/perks/weapons) lives in edcatalog.c.

// ---- lifecycle -------------------------------------------------------------

void EdScene_Init(EdScene *s) {
    if (MapDoc_Parse(s->path, &s->doc, stderr) > 0)
        fprintf(stderr, "editor: '%s' parsed with errors (continuing)\n", s->path);
    EngMapHistory_Init(&s->hist, s->undoDepth);
    EngMapHistory_Commit(&s->hist, &s->doc, 0);
    Eng_DebugSetEnabled(true);

    s->cam = (Camera3D){ .position = { 0, 24, 28 }, .target = { 0, 0, 0 },
                         .up = { 0, 1, 0 }, .fovy = s->viewFov, .projection = CAMERA_PERSPECTIVE };
    s->view   = s->viewDefault;
    s->yaw    = PI * 0.25f;
    s->pitch  = -0.6f;
    s->focus  = (Vector3){ 0, 0, 0 };
    s->orthoH = s->viewOrthoH;
    s->framePending = true;   // fit the camera to the startup map (deferred: needs vpW/vpH)
    EdScene_ClearSelection(s);
    s->mode = ENG_GIZMO_TRANSLATE;
    s->placeTool = ED_PLACE_NONE;
    s->wallPending = false;
    EdScene_ScanMobs(s);
    EdScene_ScanProps(s);
    EdScene_ScanPerks(s);
    EdScene_ScanWeapons(s);
    EdAssets_Scan(&s->assets);
    s->dirty = false;
}

void EdScene_Shutdown(EdScene *s) {
    EngMapHistory_Free(&s->hist);
    EdThumb_Shutdown();   // free cached asset-browser thumbnail textures
    if (s->vpTexValid) { UnloadRenderTexture(s->vpTex); s->vpTexValid = false; }
}

bool EdScene_Save(EdScene *s) {
    int rc = MapDoc_Save(s->path, &s->doc);
    if (rc == 0) {
        s->dirty = false;
        s->autosaveAccum = 0.0f;
        EdScene_DiscardRecovery(s);   // the on-disk map is now authoritative
    }
    return rc == 0;
}

// ---- autosave / crash recovery ---------------------------------------------

#define ED_AUTOSAVE_SECS 30.0f

// "untitled.map" is the unsaved-scratch name New gives a fresh doc; it has no
// real on-disk home, so we don't autosave/recover it.
static bool EdHasRealPath(const EdScene *s) {
    return s->path[0] && strcmp(s->path, "untitled.map") != 0;
}
static void EdRecoveryPath(const EdScene *s, char *buf, int cap) {
    snprintf(buf, (size_t)cap, "%s.autosave", s->path);
}

void EdScene_AutosaveTick(EdScene *s) {
    if (!EdHasRealPath(s)) return;
    if (!s->dirty) { s->autosaveAccum = 0.0f; return; }
    s->autosaveAccum += GetFrameTime();
    if (s->autosaveAccum < ED_AUTOSAVE_SECS) return;
    s->autosaveAccum = 0.0f;
    char rp[600]; EdRecoveryPath(s, rp, sizeof rp);
    MapDoc_Save(rp, &s->doc);   // best-effort; a failed autosave must not disrupt editing
}

bool EdScene_RecoveryAvailable(const EdScene *s) {
    if (!EdHasRealPath(s)) return false;
    char rp[600]; EdRecoveryPath(s, rp, sizeof rp);
    if (!FileExists(rp)) return false;
    if (!FileExists(s->path)) return true;             // map gone but a recovery survives
    return GetFileModTime(rp) > GetFileModTime(s->path);
}

void EdScene_DiscardRecovery(EdScene *s) {
    if (!EdHasRealPath(s)) return;
    char rp[600]; EdRecoveryPath(s, rp, sizeof rp);
    if (FileExists(rp)) remove(rp);
}

bool EdScene_RestoreRecovery(EdScene *s) {
    if (!EdScene_RecoveryAvailable(s)) return false;
    char rp[600]; EdRecoveryPath(s, rp, sizeof rp);
    MapDoc nd; memset(&nd, 0, sizeof nd);
    if (MapDoc_Parse(rp, &nd, stderr) > 0) return false;   // corrupt recovery → keep the loaded map
    s->doc = nd;
    EngMapHistory_Free(&s->hist);
    EngMapHistory_Init(&s->hist, s->undoDepth);
    EngMapHistory_Commit(&s->hist, &s->doc, 0);
    EdScene_ClearSelection(s);
    s->dragging = false;
    s->dirty = true;            // restored edits are unsaved vs the on-disk map
    remove(rp);                 // consumed
    return true;
}

bool EdScene_Open(EdScene *s, const char *path) {
    MapDoc nd; memset(&nd, 0, sizeof nd);
    if (MapDoc_Parse(path, &nd, stderr) > 0) return false;
    s->doc = nd;
    snprintf(s->path, sizeof s->path, "%s", path);
    EngMapHistory_Free(&s->hist);
    EngMapHistory_Init(&s->hist, s->undoDepth);
    EngMapHistory_Commit(&s->hist, &s->doc, 0);
    EdScene_ClearSelection(s);
    s->dragging = false;
    s->dirty = false;
    s->framePending = true;   // fit the camera to the new map (deferred: needs vpW/vpH)
    return true;
}

void EdScene_New(EdScene *s) {
    // Reset to a fresh empty document: one default sector so the .map grammar
    // has somewhere to group entities into.
    memset(&s->doc, 0, sizeof s->doc);
    int sid = EngMapEnt_Add(&s->doc, ENGMAPENT_SECTOR);
    if (sid >= 0) {
        Eng_SetSectorSize(&s->doc, sid, 40.0f, 40.0f);
        Eng_SetSectorHeights(&s->doc, sid, 0.0f, 0.0f);
    }
    EngMapHistory_Free(&s->hist);
    EngMapHistory_Init(&s->hist, s->undoDepth);
    EngMapHistory_Commit(&s->hist, &s->doc, 0);
    EdScene_ClearSelection(s);
    s->dragging = false;
    snprintf(s->path, sizeof s->path, "untitled.map");
    s->dirty = true;
    s->framePending = true;   // fit the camera to the fresh map (deferred)
}

bool EdScene_SaveAs(EdScene *s, const char *path) {
    snprintf(s->path, sizeof s->path, "%s", path);
    return EdScene_Save(s);
}

// ---- play test -------------------------------------------------------------

// Save the current map, then launch the game binary on it as a detached child
// so the editor keeps running (the game boots straight into solo play on the
// passed map — see src/game/main.c). The seam stays intact: we hand the game a
// path string and never include a game header. Writes a human-readable result
// into msg (success OR the reason it failed); returns whether the game launched.
bool EdScene_PlayTest(EdScene *s, char *msg, int cap) {
    if (!EdHasRealPath(s)) {                       // untitled scratch has no on-disk map
        snprintf(msg, (size_t)cap, "play test: save the map first (untitled)");
        return false;
    }
    if (!EdScene_Save(s)) {                        // play the latest edits
        snprintf(msg, (size_t)cap, "play test: save FAILED: %s", s->path);
        return false;
    }
    // Which binary? The open game's manifest decides (game-agnostic): its
    // "binary" key, else its id, else the legacy "shooter" — so Play Test works
    // for any game project, not just the bundled one. The binary sits next to the
    // editor binary (same build/install dir).
    const char *binName = "shooter";
    EdProject proj;
    const char *gameRoot = Eng_GetGameRoot();
    if (gameRoot && EdProject_Read(gameRoot, &proj)) {
        if      (proj.binary[0]) binName = proj.binary;
        else if (proj.id[0])     binName = proj.id;
    }
    char exe[1024];
    snprintf(exe, sizeof exe, "%s%s", GetApplicationDirectory(), binName);  // dir has trailing slash
    if (!FileExists(exe)) {
        snprintf(msg, (size_t)cap, "play test: game binary not found at %s", exe);
        return false;
    }
    // Detach: ignore SIGCHLD so the finished child is auto-reaped (no zombies),
    // then posix_spawn without waiting so the editor's frame loop is unblocked.
    signal(SIGCHLD, SIG_IGN);
    char *const argv[] = { exe, s->path, NULL };
    pid_t pid;
    int rc = posix_spawn(&pid, exe, NULL, NULL, argv, environ);
    if (rc != 0) {
        snprintf(msg, (size_t)cap, "play test: launch failed (errno %d)", rc);
        return false;
    }
    snprintf(msg, (size_t)cap, "play test: launched on %s", s->path);
    return true;
}

// ---- per-frame viewport ----------------------------------------------------

// Ramp link-pick: while armed (s->linkPick), the next viewport click on a sector
// sets that link on the selected ramp. Returns true while armed at entry, so the
// caller skips all other viewport interaction this frame. Cancels on Esc or if
// the selection is no longer a ramp.
static bool UpdateLinkPick(EdScene *s, bool canInteract) {
    if (!s->linkPick) return false;
    int kind = -1;
    bool isRamp = s->selectedId >= 0 &&
                  Eng_GetSectorKind(&s->doc, s->selectedId, &kind) && kind == SECTOR_RAMP;
    if (!isRamp || IsKeyPressed(KEY_ESCAPE)) { s->linkPick = 0; return true; }
    if (canInteract && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector3 g;
        Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
        if (Eng_PickRayGroundY(ray, 0.0f, &g)) {
            int self = EngMapEnt_Find(&s->doc, s->selectedId).index;
            int idx = SectorContainingExcl(s, g.x, g.z, self);
            int axis, la, lb;
            Eng_GetSectorRamp(&s->doc, s->selectedId, &axis, &la, &lb);
            if (s->linkPick == 1) la = idx; else lb = idx;
            Eng_SetSectorRamp(&s->doc, s->selectedId, axis, la, lb);
            EdScene_Commit(s);
        }
        s->linkPick = 0;
    }
    return true;
}

void EdScene_UpdateViewport(EdScene *s, Rectangle vp, bool inputAllowed) {
    float dt = GetFrameTime();
    s->vpW = (int)vp.width  < 1 ? 1 : (int)vp.width;
    s->vpH = (int)vp.height < 1 ? 1 : (int)vp.height;
    s->vpMouse = (Vector2){ GetMousePosition().x - vp.x, GetMousePosition().y - vp.y };

    EdScene_RebuildProxies(s);

    // Deferred "frame all" from Open/New: now that proxies are current and the
    // viewport is sized (vpW/vpH above), fit the camera to the map.
    if (s->framePending) EdScene_FrameAll(s);

    // Always re-derive the camera (input-gated inside) so a frame-all or an
    // off-viewport view switch is reflected even when the mouse isn't driving it.
    EdUpdateCamera(s, dt, vp, inputAllowed);

    if (inputAllowed) {
        if (IsKeyPressed(KEY_F1)) EdScene_SwitchView(s, ED_VIEW_FLY);
        if (IsKeyPressed(KEY_F2)) EdScene_SwitchView(s, ED_VIEW_ORBIT);
        if (IsKeyPressed(KEY_F3)) EdScene_SwitchView(s, ED_VIEW_TOP);

        if (IsKeyPressed(KEY_ONE))   s->mode = ENG_GIZMO_TRANSLATE;
        if (IsKeyPressed(KEY_TWO))   s->mode = ENG_GIZMO_ROTATE;
        if (IsKeyPressed(KEY_THREE)) s->mode = ENG_GIZMO_SCALE;

        if (IsKeyPressed(KEY_M)) s->materialMode = !s->materialMode;
        if (IsKeyPressed(KEY_G)) s->gridVisible = !s->gridVisible;
        if (IsKeyPressed(KEY_P)) {
            // Cancel any pending two-click wall when cycling away from WALL.
            s->wallPending = false;
            s->placeTool = (s->placeTool + 1) % ED_PLACE_COUNT;
        }
        if (IsKeyPressed(KEY_O)) s->barricadeAutoSpawn = !s->barricadeAutoSpawn;
        if (IsKeyPressed(KEY_R)) EdScene_CycleSelected(s);
        // Bare X or Delete removes the selection; Ctrl+X (Cut) is handled by the
        // shell, so ignore X while Ctrl is held to avoid a redundant delete.
        bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if ((IsKeyPressed(KEY_X) && !ctrlHeld) || IsKeyPressed(KEY_DELETE))
            EdScene_DeleteSelected(s);

        // F: frame the selected entity; with nothing selected, frame the whole
        // map (so F is never a dead key). Home always frames all.
        if (IsKeyPressed(KEY_HOME)) {
            EdScene_FrameAll(s);
        } else if (IsKeyPressed(KEY_F)) {
            if (s->selectedId >= 0) EdScene_FrameSelected(s);
            else                    EdScene_FrameAll(s);
        }
    } else if (s->looking) {
        // Lost focus mid-mouselook: show cursor again so the OS pointer is visible.
        s->looking = false; ShowCursor();
    }

    // Cancel a pending wall first-click whenever the tool is switched away.
    if (s->wallPending && s->placeTool != ED_PLACE_WALL) s->wallPending = false;

    bool camBusy = s->looking || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                   IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    bool canInteract = inputAllowed && !camBusy && !s->dragging;
    bool picking = UpdateLinkPick(s, canInteract);   // ramp link-pick consumes the frame
    if (picking) {
        s->sectorDragging = false;
    } else if (canInteract && s->placeTool == ED_PLACE_SECTOR) {
        UpdateSectorDrag(s);   // RECT-drag create (press → drag → release)
    } else {
        s->sectorDragging = false;   // tool switched / camera busy / focus lost mid-drag
        if (canInteract && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (s->placeTool != ED_PLACE_NONE) {
                Vector3 hit;
                Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
                if (Eng_PickRayGroundY(ray, 0.0f, &hit)) {
                    hit.x = EdSnap(s, hit.x); hit.z = EdSnap(s, hit.z);
                    PlaceAt(s, hit);
                }
            } else {
                Vector3 origin;
                EngGizmoAxis hot = ENG_GIZMO_AXIS_NONE;
                if (SelectedOrigin(s, &origin))
                    hot = Eng_GizmoHitTest(s->cam, s->vpMouse, s->vpW, s->vpH, origin, s->mode,
                                           GizmoHandleSize(s, origin));
                if (hot == ENG_GIZMO_AXIS_NONE && !BeginSectorResize(s) &&
                    !BeginSectorHeight(s) && !BeginWallEdit(s)) {
                    bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                    PickSelection(s, additive);
                }
            }
        }
    }

    if (picking) {
        /* no gizmo/resize while link-picking */
    } else if (s->resizing) {
        if (inputAllowed && !camBusy) UpdateSectorResize(s);
        else                          s->resizing = false;   // focus/camera took over mid-drag
    } else if (s->wallEditing) {
        if (inputAllowed && !camBusy) UpdateWallEdit(s);
        else                          s->wallEditing = false;
    } else if (s->heightEditing) {
        if (inputAllowed && !camBusy) UpdateSectorHeight(s);
        else                          s->heightEditing = false;
    } else {
        UpdateGizmo(s, inputAllowed && !camBusy && s->placeTool == ED_PLACE_NONE);
    }
}

// ---- per-frame draw --------------------------------------------------------

void EdScene_DrawViewport(EdScene *s, Rectangle vp) {
    // (Re)create the render texture when the viewport size changes.
    if (!s->vpTexValid || s->vpTex.texture.width != s->vpW || s->vpTex.texture.height != s->vpH) {
        if (s->vpTexValid) UnloadRenderTexture(s->vpTex);
        s->vpTex = LoadRenderTexture(s->vpW, s->vpH);
        s->vpTexValid = true;
    }

    BeginTextureMode(s->vpTex);
    ClearBackground((Color){ 18, 20, 26, 255 });
    Eng_GfxBeginMode3D(s->cam);
    if (s->gridVisible) Eng_GfxDrawGrid(s->gridSlices, s->gridSpacing);

    if (s->materialMode) {
        // Material mode: textured geometry instead of flat proxy boxes.
        DrawMaterialWorld(s);
        // Still outline the selection so the user knows what is selected: the
        // primary in YELLOW, other members of a multi-selection in ORANGE.
        for (int i = 0; i < s->proxyCount; i++) {
            EdProxy *p = &s->proxies[i];
            if (!EdScene_IsSelected(s, p->id)) continue;
            Color hl = (p->id == s->selectedId) ? YELLOW : ED_SEL_SECONDARY;
            Vector3 c  = Vector3Scale(Vector3Add(p->box.min, p->box.max), 0.5f);
            Vector3 sz = Vector3Subtract(p->box.max, p->box.min);
            Eng_GfxDrawCubeWiresV(c, sz, hl);
            Eng_DebugBox(p->box, hl);
        }
    } else {
        // Default proxy-box mode.
        for (int i = 0; i < s->proxyCount; i++) {
            EdProxy *p = &s->proxies[i];
            Vector3 c  = Vector3Scale(Vector3Add(p->box.min, p->box.max), 0.5f);
            Vector3 sz = Vector3Subtract(p->box.max, p->box.min);
            bool sel = EdScene_IsSelected(s, p->id);
            bool primary = (p->id == s->selectedId);
            Color hl = primary ? YELLOW : ED_SEL_SECONDARY;
            Color fill = p->col; fill.a = sel ? 220 : 150;
            Eng_GfxDrawCubeV(c, sz, fill);
            Eng_GfxDrawCubeWiresV(c, sz, sel ? hl : (Color){ 0, 0, 0, 120 });
            if (sel) Eng_DebugBox(p->box, hl);
        }
    }

    // Validation: outline offending entities in red (both modes).
    {
        MapDocIssue issues[64];
        int n = MapDoc_Validate(&s->doc, issues, 64);
        for (int i = 0; i < n && i < 64; i++) {
            if (issues[i].entId < 0) continue;
            const EdProxy *ep = FindProxy(s, issues[i].entId);
            if (ep) Eng_DebugBox(ep->box, RED);
        }
    }

    // Two-click wall placement: draw a sphere at the pending start point while
    // the user is choosing the second endpoint (both modes).
    if (s->wallPending)
        Eng_DebugSphere((Vector3){ s->wallStartX, 0.0f, s->wallStartZ },
                        ED_MARKER, (Color){ 255, 220, 60, 255 });

    // RECT-drag sector: outline the footprint being dragged out (live preview).
    if (s->sectorDragging) {
        float minX = fminf(s->sectorStartX, s->sectorCurX), maxX = fmaxf(s->sectorStartX, s->sectorCurX);
        float minZ = fminf(s->sectorStartZ, s->sectorCurZ), maxZ = fmaxf(s->sectorStartZ, s->sectorCurZ);
        BoundingBox bb = { { minX, 0.0f, minZ }, { maxX, 0.1f, maxZ } };
        Eng_DebugBox(bb, (Color){ 90, 200, 255, 255 });
    }

    // Placement ghost: when a POINT/LINE tool is armed and the cursor is over the
    // viewport, preview where a click would drop the entity (a snapped marker +
    // footprint hint), so the armed tool reads in the viewport, not only the
    // tool-strip (editor-ux-review.md §5 / §1-D). Sector RECT-drag has its own
    // live preview above; skip the ghost mid-drag.
    if (s->placeTool != ED_PLACE_NONE && !s->sectorDragging &&
        s->vpMouse.x >= 0 && s->vpMouse.x <= s->vpW &&
        s->vpMouse.y >= 0 && s->vpMouse.y <= s->vpH) {
        Ray ray = Eng_PickRayFromScreen(s->cam, s->vpMouse, s->vpW, s->vpH);
        Vector3 g;
        if (Eng_PickRayGroundY(ray, 0.0f, &g)) {
            g.x = EdSnap(s, g.x); g.z = EdSnap(s, g.z);
            Color ghost = { 120, 220, 255, 220 };
            Eng_DebugSphere(g, ED_MARKER * 0.6f, ghost);
            BoundingBox gb = { { g.x - 1, 0.0f, g.z - 1 }, { g.x + 1, 1.5f, g.z + 1 } };
            Eng_DebugBox(gb, ghost);
            // Wall: rubber-band the segment from the pending first endpoint.
            if (s->placeTool == ED_PLACE_WALL && s->wallPending)
                Eng_DebugLine((Vector3){ s->wallStartX, 0.1f, s->wallStartZ },
                              (Vector3){ g.x, 0.1f, g.z }, ghost);
        }
    }

    // Edge-resize handles for the selected sector / endpoint handles for the
    // selected wall (when no placement tool armed).
    DrawSectorHandles(s);
    DrawSectorHeightHandle(s);
    DrawWallHandles(s);

    // Ramp slope wireframe + uphill arrow for every RAMP sector.
    DrawRampOverlay(s);

    Eng_DebugDraw3D(s->cam);
    Eng_GfxEndMode3D();
    EndTextureMode();

    // Blit (render textures are y-flipped → negative source height).
    DrawTextureRec(s->vpTex.texture,
                   (Rectangle){ 0, 0, (float)s->vpW, -(float)s->vpH },
                   (Vector2){ vp.x, vp.y }, WHITE);
}
