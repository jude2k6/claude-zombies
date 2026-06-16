// ============================================================================
//  editor_main.c — the scene / map builder, skeleton.
//
//  The editor is a SIBLING APPLICATION to the game (src/game/), not a mode
//  inside it: it is its own GameModule hosted by the same engine (Eng_Run),
//  and it depends on the engine ONLY — never a game header. Its document is
//  the engine-neutral MapDoc; its verbs are the toolkit primitives:
//    pick.h     — turn the cursor into a world ray + hit entities
//    gizmo.h    — translate/rotate/scale handle math
//    mapedit.h  — id-addressed MapDoc edits + EngMapHistory undo/redo
//    debugdraw.h— selection/handle overlay
//    gfx.h      — draw the scene
//  See docs/scene-builder.md for the design and docs/engine-layers.md for why
//  this lives here rather than in the game.
//
//  This is a SKELETON (roadmap step 2 + a taste of step 3): load a map, fly a
//  camera, draw entities as boxes, click to select, and drag the translate
//  gizmo to move the selection — proving the whole pick→gizmo→mapedit→history
//  stack composes. It is deliberately thin: no asset rendering, no tool
//  palette, no save dialog yet (see the doc's roadmap).
// ============================================================================

#include "raylib.h"
#include "raymath.h"
#include "raygui.h"   // UI only; RAYGUI_IMPLEMENTATION lives in engine/app.c

#include "app.h"
#include "mapdoc.h"
#include "mapedit.h"
#include "pick.h"
#include "gizmo.h"
#include "debugdraw.h"
#include "gfx.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define ED_PANEL_W 220   // left toolbar width (px); 3D viewport is to its right

// ---- editor state (single instance; the editor is a single document tool) --

#define ED_MAX_ENTS  (MAPDOC_MAX_SPAWNS + MAPDOC_MAX_WALLS + MAPDOC_MAX_WINDOWS + \
                      MAPDOC_MAX_OBSTACLES + MAPDOC_MAX_PROPS + MAPDOC_MAX_WALLBUYS + \
                      MAPDOC_MAX_PERKS + MAPDOC_MAX_SECTORS + 4)
#define ED_WALL_H    3.0f   // assumed wall height for drawing/picking
#define ED_MARKER    0.6f   // half-size of point-entity markers (spawn/perk/…)

// A draw/pick proxy: one selectable box per MapDoc entity, tagged by stable id.
typedef struct {
    int           id;
    EngMapEntKind kind;
    BoundingBox   box;
    Color         col;
} EdProxy;

// View modes. FLY is the free no-clip camera (perspective); ORBIT and TOP are
// orthographic editor views around a focus point. yaw/pitch are shared by all
// modes so switching is seamless.
typedef enum { ED_VIEW_FLY = 0, ED_VIEW_ORBIT, ED_VIEW_TOP } EdViewMode;

// Placement tool: what a ground-click drops. NONE = click selects instead.
typedef enum { ED_PLACE_NONE = 0, ED_PLACE_PLAYER, ED_PLACE_ZOMBIE,
               ED_PLACE_BARRICADE, ED_PLACE_COUNT } EdPlaceTool;

static struct {
    char          path[512];
    MapDoc        doc;
    EngMapHistory hist;

    Camera3D      cam;
    EdViewMode    view;
    float         yaw, pitch;       // shared look angles (fly + orbit)
    bool          looking;          // fly: RMB held → mouselook
    Vector3       focus;            // orbit/top: point the view pivots around
    float         orthoH;           // orbit/top: orthographic view height (zoom)

    int           selectedId;       // -1 = nothing selected
    EngGizmoMode  mode;             // current gizmo mode

    EdPlaceTool   placeTool;        // active placement tool (NONE = select mode)
    bool          barricadeAutoSpawn; // place a paired ZOMBIE spawn with a barricade

    bool          dragging;
    EngGizmoDrag  drag;
    Vector3       dragStartPos;     // selected entity pos at grab
    uint32_t      dragTag;          // coalesce token for this drag

    EdProxy       proxies[ED_MAX_ENTS];
    int           proxyCount;
} ed;

// ---- proxy build (MapDoc → selectable boxes) -------------------------------

// Floor height for an entity's sector index (entities take Y from their sector;
// sectorId is an array index per mapdoc.h, -1 = ungrouped → ground at 0).
static float SectorFloorY(int sectorId) {
    if (sectorId < 0 || sectorId >= ed.doc.sectorCount) return 0.0f;
    return ed.doc.sectors[sectorId].yLow;
}

static BoundingBox BoxAround(float cx, float cy, float cz, float hx, float hy, float hz) {
    return (BoundingBox){ { cx - hx, cy - hy, cz - hz }, { cx + hx, cy + hy, cz + hz } };
}

static void AddProxy(int id, EngMapEntKind kind, BoundingBox box, Color col) {
    if (ed.proxyCount >= ED_MAX_ENTS) return;
    ed.proxies[ed.proxyCount++] = (EdProxy){ id, kind, box, col };
}

// Rebuild the proxy list from the current document. Cheap; called every frame
// so edits (move/add/delete) are reflected immediately.
static void RebuildProxies(void) {
    ed.proxyCount = 0;
    const MapDoc *d = &ed.doc;

    for (int i = 0; i < d->sectorCount; i++) {
        const MapDocSector *s = &d->sectors[i];
        float cy = (s->yLow + s->yHigh) * 0.5f;
        AddProxy(s->id, ENGMAPENT_SECTOR,
                 BoxAround(s->x, cy, s->z, s->sx * 0.5f, 0.1f, s->sz * 0.5f),
                 (Color){ 60, 70, 90, 255 });
    }
    for (int i = 0; i < d->wallCount; i++) {
        const MapDocWall *w = &d->walls[i];
        float y  = SectorFloorY(w->sectorId);
        float cx = (w->x1 + w->x2) * 0.5f, cz = (w->z1 + w->z2) * 0.5f;
        float hx = fabsf(w->x2 - w->x1) * 0.5f + 0.15f;
        float hz = fabsf(w->z2 - w->z1) * 0.5f + 0.15f;
        AddProxy(w->id, ENGMAPENT_WALL,
                 BoxAround(cx, y + ED_WALL_H * 0.5f, cz, hx, ED_WALL_H * 0.5f, hz),
                 (Color){ 150, 140, 120, 255 });
    }
    for (int i = 0; i < d->obstacleCount; i++) {
        const MapDocObstacle *o = &d->obstacles[i];
        float y = SectorFloorY(o->sectorId);
        AddProxy(o->id, ENGMAPENT_OBSTACLE,
                 BoxAround(o->x, y + o->h * 0.5f, o->z, o->sx * 0.5f, o->h * 0.5f, o->sz * 0.5f),
                 (Color){ 120, 110, 90, 255 });
    }
    for (int i = 0; i < d->propCount; i++) {
        const MapDocProp *p = &d->props[i];
        float y = SectorFloorY(p->sectorId);
        float h = ED_MARKER * p->scale;
        AddProxy(p->id, ENGMAPENT_PROP,
                 BoxAround(p->x, y + h, p->z, h, h, h), (Color){ 90, 160, 110, 255 });
    }
    for (int i = 0; i < d->spawnCount; i++) {
        const MapDocSpawn *s = &d->spawns[i];
        float y = SectorFloorY(s->sectorId);
        bool player = (strcmp(s->mob, "PLAYER") == 0);
        AddProxy(s->id, ENGMAPENT_SPAWN,
                 BoxAround(s->x, y + ED_MARKER, s->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 player ? (Color){ 90, 130, 200, 255 }    // PLAYER = blue
                        : (Color){ 200, 80, 80, 255 });   // mob = red
    }
    for (int i = 0; i < d->windowCount; i++) {
        const MapDocWindow *w = &d->windows[i];
        float y = SectorFloorY(w->sectorId);
        AddProxy(w->id, ENGMAPENT_WINDOW,
                 BoxAround(w->x, y + ED_MARKER, w->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 200, 170, 90, 255 });
    }
    for (int i = 0; i < d->wallbuyCount; i++) {
        const MapDocWallbuy *w = &d->wallbuys[i];
        float y = SectorFloorY(w->sectorId);
        AddProxy(w->id, ENGMAPENT_WALLBUY,
                 BoxAround(w->x, y + ED_MARKER, w->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 200, 120, 90, 255 });
    }
    for (int i = 0; i < d->perkCount; i++) {
        const MapDocPerk *p = &d->perks[i];
        float y = SectorFloorY(p->sectorId);
        AddProxy(p->id, ENGMAPENT_PERK,
                 BoxAround(p->x, y + ED_MARKER, p->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 170, 90, 200, 255 });
    }
}

static const EdProxy *FindProxy(int id) {
    for (int i = 0; i < ed.proxyCount; i++)
        if (ed.proxies[i].id == id) return &ed.proxies[i];
    return NULL;
}

static const char *KindName(EngMapEntKind k) {
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

static bool SelectedOrigin(Vector3 *out) {
    if (ed.selectedId < 0) return false;
    const EdProxy *p = FindProxy(ed.selectedId);
    if (!p) return false;
    float cx, cz;
    if (!Eng_GetPos(&ed.doc, ed.selectedId, &cx, &cz)) return false;
    float cy = (p->box.min.y + p->box.max.y) * 0.5f;
    *out = (Vector3){ cx, cy, cz };
    return true;
}

// ---- cameras (fly / orbit / top) -------------------------------------------

#define ED_ORBIT_DIST 200.0f   // ortho eye distance (apparent size is fovy, not this)

static Vector3 ForwardFrom(float yaw, float pitch) {
    return (Vector3){ cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw) };
}

// View basis (right, up) for a forward dir; guards the top-down singularity
// where forward is parallel to world-up by falling back to -Z as the up ref.
static void ViewBasis(Vector3 fwd, Vector3 *right, Vector3 *up) {
    Vector3 worldUp = (fabsf(fwd.y) > 0.999f) ? (Vector3){ 0, 0, -1 } : (Vector3){ 0, 1, 0 };
    *right = Vector3Normalize(Vector3CrossProduct(fwd, worldUp));
    *up    = Vector3Normalize(Vector3CrossProduct(*right, fwd));
}

// Switch view mode, carrying the look angles across so the cut is seamless:
// entering an ortho view pivots around the current focus; entering fly places
// the eye back where the ortho camera was looking from.
static void SwitchView(EdViewMode m) {
    if (m == ed.view) return;
    if (m == ED_VIEW_FLY) {
        // Un-pin a straight-down top view so the fly cam is usable.
        if (ed.view == ED_VIEW_TOP) ed.pitch = -1.2f;
        Vector3 fwd = ForwardFrom(ed.yaw, ed.pitch);
        ed.cam.position = Vector3Subtract(ed.focus, Vector3Scale(fwd, 20.0f));
        if (ed.looking) { ed.looking = false; EnableCursor(); }
    } else if (m == ED_VIEW_TOP) {
        ed.pitch = -PI * 0.5f;               // straight down (north-up)
    } else if (m == ED_VIEW_ORBIT && ed.view == ED_VIEW_TOP) {
        ed.pitch = -0.9f;                    // restore an isometric tilt
    }
    ed.view = m;
}

static void UpdateCamFly(float dt) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { ed.looking = true;  DisableCursor(); }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)){ ed.looking = false; EnableCursor();  }

    if (ed.looking) {
        Vector2 md = GetMouseDelta();
        ed.yaw   += md.x * 0.003f;
        ed.pitch -= md.y * 0.003f;
        if (ed.pitch >  1.55f) ed.pitch =  1.55f;
        if (ed.pitch < -1.55f) ed.pitch = -1.55f;
    }

    Vector3 fwd   = ForwardFrom(ed.yaw, ed.pitch);
    Vector3 right = { cosf(ed.yaw), 0, sinf(ed.yaw) };
    Vector3 up    = { 0, 1, 0 };
    Vector3 move  = { 0 };
    if (ed.looking) {
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
        if (IsKeyDown(KEY_E)) move = Vector3Add(move, up);
        if (IsKeyDown(KEY_Q)) move = Vector3Subtract(move, up);
    }
    float speed = IsKeyDown(KEY_LEFT_SHIFT) ? 40.0f : 16.0f;
    if (Vector3LengthSqr(move) > 1e-4f)
        ed.cam.position = Vector3Add(ed.cam.position, Vector3Scale(Vector3Normalize(move), speed * dt));

    ed.cam.target     = Vector3Add(ed.cam.position, fwd);
    ed.cam.up         = up;
    ed.cam.fovy       = 60.0f;
    ed.cam.projection = CAMERA_PERSPECTIVE;
    // Keep the orbit focus tracking what the fly cam looks at, so toggling to
    // an ortho view pivots around roughly what was on screen.
    ed.focus = Vector3Add(ed.cam.position, Vector3Scale(fwd, 20.0f));
}

// Orbit (isometric) + top-down share this: an orthographic view that pivots
// around `focus`. `allowRotate` is false for the locked top-down view.
static void UpdateCamOrtho(int sw, int sh, bool allowRotate) {
    // Rotate the viewpoint by dragging RMB (orbit only).
    if (allowRotate && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 md = GetMouseDelta();
        ed.yaw   += md.x * 0.005f;
        ed.pitch -= md.y * 0.005f;
        if (ed.pitch < -1.55f) ed.pitch = -1.55f;
        if (ed.pitch >  1.55f) ed.pitch =  1.55f;
    }

    Vector3 fwd = (ed.view == ED_VIEW_TOP) ? (Vector3){ 0, -1, 0 }
                                           : ForwardFrom(ed.yaw, ed.pitch);
    Vector3 right, up;
    ViewBasis(fwd, &right, &up);

    // Pan the focus by dragging MMB (grab-the-canvas).
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 md = GetMouseDelta();
        float wpp = ed.orthoH / (float)sh;   // world units per screen pixel
        ed.focus = Vector3Add(ed.focus, Vector3Scale(right, -md.x * wpp));
        ed.focus = Vector3Add(ed.focus, Vector3Scale(up,     md.y * wpp));
    }

    // Zoom with the wheel (ortho height).
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        ed.orthoH *= (1.0f - wheel * 0.1f);
        if (ed.orthoH <   2.0f) ed.orthoH =   2.0f;
        if (ed.orthoH > 400.0f) ed.orthoH = 400.0f;
    }

    ed.cam.position   = Vector3Subtract(ed.focus, Vector3Scale(fwd, ED_ORBIT_DIST));
    ed.cam.target     = ed.focus;
    ed.cam.up         = up;
    ed.cam.fovy       = ed.orthoH;
    ed.cam.projection = CAMERA_ORTHOGRAPHIC;
    (void)sw;
}

static void EdUpdateCamera(float dt, int sw, int sh) {
    switch (ed.view) {
        case ED_VIEW_FLY:   UpdateCamFly(dt);                  break;
        case ED_VIEW_ORBIT: UpdateCamOrtho(sw, sh, true);      break;
        case ED_VIEW_TOP:   UpdateCamOrtho(sw, sh, false);     break;
    }
}

// ---- pick selection --------------------------------------------------------

static void PickSelection(int sw, int sh) {
    Ray ray = Eng_PickRayFromScreen(ed.cam, GetMousePosition(), sw, sh);
    int   best = -1;
    float bestT = 1e30f;
    for (int i = 0; i < ed.proxyCount; i++) {
        float t;
        if (Eng_PickRayAABB(ray, ed.proxies[i].box, &t) && t < bestT) {
            bestT = t; best = ed.proxies[i].id;
        }
    }
    ed.selectedId = best;
}

// ---- placement (mapedit add + retag) ---------------------------------------

static void CommitEdit(void) { EngMapHistory_Commit(&ed.hist, &ed.doc, 0); }

// A barricade faces the arena interior (origin): its normal points to centre,
// so the facing dir is the opposite of the point's dominant axis.
static const char *FacingToward(Vector3 p) {
    if (fabsf(p.x) >= fabsf(p.z)) return p.x > 0 ? "-x" : "+x";
    return p.z > 0 ? "-z" : "+z";
}
static Vector3 DirNormal(const char *d) {
    if (!strcmp(d, "+x")) return (Vector3){  1, 0,  0 };
    if (!strcmp(d, "-x")) return (Vector3){ -1, 0,  0 };
    if (!strcmp(d, "+z")) return (Vector3){  0, 0,  1 };
    return (Vector3){ 0, 0, -1 };  // -z
}

// The sector whose footprint contains (x,z) — placed entities must belong to a
// real sector to be saved (the grammar has no ungrouped entities). Falls back
// to sector 0, or -1 if the doc has no sectors at all.
static int SectorAt(float x, float z) {
    for (int i = 0; i < ed.doc.sectorCount; i++) {
        const MapDocSector *s = &ed.doc.sectors[i];
        if (fabsf(x - s->x) <= s->sx * 0.5f && fabsf(z - s->z) <= s->sz * 0.5f) return i;
    }
    return ed.doc.sectorCount > 0 ? 0 : -1;
}

static int AddSpawn(const char *mob, float x, float z) {
    int id = EngMapEnt_Add(&ed.doc, ENGMAPENT_SPAWN);
    if (id < 0) return -1;
    Eng_SetSpawnMob(&ed.doc, id, mob);
    Eng_SetSector(&ed.doc, id, SectorAt(x, z));
    Eng_SetPos(&ed.doc, id, x, z);
    return id;
}

// Drop the active tool's entity at world point p; selects the result and
// commits one undo step.
static void PlaceAt(Vector3 p) {
    switch (ed.placeTool) {
        case ED_PLACE_PLAYER: ed.selectedId = AddSpawn("PLAYER", p.x, p.z); CommitEdit(); break;
        case ED_PLACE_ZOMBIE: ed.selectedId = AddSpawn("ZOMBIE", p.x, p.z); CommitEdit(); break;
        case ED_PLACE_BARRICADE: {
            int wid = EngMapEnt_Add(&ed.doc, ENGMAPENT_WINDOW);
            if (wid < 0) return;
            const char *dir = FacingToward(p);
            Eng_SetWindowDir(&ed.doc, wid, dir);
            Eng_SetSector(&ed.doc, wid, SectorAt(p.x, p.z));
            Eng_SetPos(&ed.doc, wid, p.x, p.z);
            ed.selectedId = wid;
            // Opt-in paired mob spawn just outside the barricade (pos - normal*5).
            if (ed.barricadeAutoSpawn) {
                Vector3 n = DirNormal(dir);
                AddSpawn("ZOMBIE", p.x - n.x * 5.0f, p.z - n.z * 5.0f);
            }
            CommitEdit();
            break;
        }
        default: break;
    }
}

// Edit the selected entity in place (R key): cycle a window's facing, or flip a
// spawn between PLAYER and ZOMBIE.
static void CycleSelected(void) {
    if (ed.selectedId < 0) return;
    EngMapEntKind k; EngMapEnt_Ptr(&ed.doc, EngMapEnt_Find(&ed.doc, ed.selectedId), &k);
    if (k == ENGMAPENT_WINDOW) {
        char d[4]; Eng_GetWindowDir(&ed.doc, ed.selectedId, d, sizeof d);
        const char *order[4] = { "+x", "+z", "-x", "-z" };
        int cur = 0; for (int i = 0; i < 4; i++) if (!strcmp(d, order[i])) cur = i;
        Eng_SetWindowDir(&ed.doc, ed.selectedId, order[(cur + 1) % 4]);
        CommitEdit();
    } else if (k == ENGMAPENT_SPAWN) {
        char m[MAPDOC_SPAWN_MOB_LEN]; Eng_GetSpawnMob(&ed.doc, ed.selectedId, m, sizeof m);
        Eng_SetSpawnMob(&ed.doc, ed.selectedId, strcmp(m, "PLAYER") == 0 ? "ZOMBIE" : "PLAYER");
        CommitEdit();
    }
}

// ---- gizmo drag (translate) → mapedit → history ----------------------------

static uint32_t g_nextTag = 1;

// Apparent handle size: perspective scales with distance; ortho with zoom.
static float GizmoHandleSize(Vector3 origin) {
    if (ed.view == ED_VIEW_FLY)
        return Vector3Distance(ed.cam.position, origin) * 0.12f + 0.5f;
    return ed.orthoH * 0.06f + 0.5f;
}

static void UpdateGizmo(int sw, int sh, bool allowGrab) {
    Vector3 origin;
    if (!SelectedOrigin(&origin)) { ed.dragging = false; return; }

    float handleSize = GizmoHandleSize(origin);
    Vector2 mouse = GetMousePosition();

    if (!ed.dragging) {
        EngGizmoAxis hot = Eng_GizmoHitTest(ed.cam, mouse, sw, sh, origin, ed.mode, handleSize);
        Eng_GizmoDebugDraw(origin, ed.mode, handleSize, hot);

        // Start a drag if the user presses on a handle (translate only for now).
        if (allowGrab && hot != ENG_GIZMO_AXIS_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            ed.mode == ENG_GIZMO_TRANSLATE) {
            ed.drag = Eng_GizmoBeginDrag(ed.cam, mouse, sw, sh, origin, ed.mode, hot);
            if (ed.drag.axis != ENG_GIZMO_AXIS_NONE) {
                float x, z; Eng_GetPos(&ed.doc, ed.selectedId, &x, &z);
                ed.dragStartPos = (Vector3){ x, origin.y, z };
                ed.dragTag = g_nextTag++;
                if (g_nextTag == 0) g_nextTag = 1;
                ed.dragging = true;
            }
        }
        return;
    }

    // Dragging: recompute position from the grab snapshot (drift-free) and
    // commit each frame under the same tag so the whole drag coalesces into
    // ONE undo step.
    Eng_GizmoDebugDraw(origin, ed.mode, handleSize, ed.drag.axis);
    EngGizmoDelta d = Eng_GizmoUpdateDrag(ed.drag, ed.cam, mouse, sw, sh);
    Eng_SetPos(&ed.doc, ed.selectedId,
               ed.dragStartPos.x + d.translate.x,
               ed.dragStartPos.z + d.translate.z);
    EngMapHistory_Commit(&ed.hist, &ed.doc, ed.dragTag);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) ed.dragging = false;
}

// ---- GameModule ------------------------------------------------------------

static void EdInit(void) {
    if (MapDoc_Parse(ed.path, &ed.doc, stderr) > 0)
        fprintf(stderr, "editor: '%s' parsed with errors (continuing)\n", ed.path);
    EngMapHistory_Init(&ed.hist, ENGMAPHISTORY_DEFAULT_DEPTH);
    EngMapHistory_Commit(&ed.hist, &ed.doc, 0);   // initial checkpoint
    Eng_DebugSetEnabled(true);

    ed.cam = (Camera3D){ .position = { 0, 24, 28 }, .target = { 0, 0, 0 },
                         .up = { 0, 1, 0 }, .fovy = 60.0f, .projection = CAMERA_PERSPECTIVE };
    ed.view   = ED_VIEW_ORBIT;     // start in the isometric editor view
    ed.yaw    = PI * 0.25f;        // 45° azimuth
    ed.pitch  = -0.6f;             // ~34° down → isometric-ish
    ed.focus  = (Vector3){ 0, 0, 0 };
    ed.orthoH = 40.0f;
    ed.selectedId = -1;
    ed.mode = ENG_GIZMO_TRANSLATE;
    ed.placeTool = ED_PLACE_NONE;
    ed.barricadeAutoSpawn = true;
}

static void EdFrame(float dt, int sw, int sh) {
    RebuildProxies();

    // View-mode hotkeys: F1 fly (no-clip), F2 isometric orbit, F3 top-down.
    if (IsKeyPressed(KEY_F1)) SwitchView(ED_VIEW_FLY);
    if (IsKeyPressed(KEY_F2)) SwitchView(ED_VIEW_ORBIT);
    if (IsKeyPressed(KEY_F3)) SwitchView(ED_VIEW_TOP);
    EdUpdateCamera(dt, sw, sh);

    // Gizmo mode hotkeys.
    if (IsKeyPressed(KEY_ONE))   ed.mode = ENG_GIZMO_TRANSLATE;
    if (IsKeyPressed(KEY_TWO))   ed.mode = ENG_GIZMO_ROTATE;
    if (IsKeyPressed(KEY_THREE)) ed.mode = ENG_GIZMO_SCALE;

    // Placement: P cycles the place tool, O toggles the barricade auto-spawn,
    // R edits the selection in place, X deletes it.
    if (IsKeyPressed(KEY_P)) ed.placeTool = (ed.placeTool + 1) % ED_PLACE_COUNT;
    if (IsKeyPressed(KEY_O)) ed.barricadeAutoSpawn = !ed.barricadeAutoSpawn;
    if (IsKeyPressed(KEY_R)) CycleSelected();
    if (IsKeyPressed(KEY_X) && ed.selectedId >= 0) {
        EngMapEnt_Delete(&ed.doc, ed.selectedId);
        ed.selectedId = -1;
        CommitEdit();
    }

    // Undo / redo.
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_Z)) EngMapHistory_Undo(&ed.hist, &ed.doc);
    if (ctrl && IsKeyPressed(KEY_Y)) EngMapHistory_Redo(&ed.hist, &ed.doc);

    // The camera is being driven when fly-looking or while orbit/pan buttons are
    // held — suppress selection/placement so those clicks don't double up.
    // Also ignore 3D clicks that land on the toolbar (the panel owns them).
    bool overPanel = (GetMousePosition().x < ED_PANEL_W);
    bool camBusy = ed.looking || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                   IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    if (!camBusy && !overPanel && !ed.dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (ed.placeTool != ED_PLACE_NONE) {
            // Placement mode: drop the tool's entity where the cursor ray meets
            // the ground plane.
            Vector3 hit;
            Ray ray = Eng_PickRayFromScreen(ed.cam, GetMousePosition(), sw, sh);
            if (Eng_PickRayGroundY(ray, 0.0f, &hit)) PlaceAt(hit);
        } else {
            // Select — unless the click landed on a gizmo handle (handled below).
            Vector3 origin;
            EngGizmoAxis hot = ENG_GIZMO_AXIS_NONE;
            if (SelectedOrigin(&origin))
                hot = Eng_GizmoHitTest(ed.cam, GetMousePosition(), sw, sh, origin, ed.mode,
                                       GizmoHandleSize(origin));
            if (hot == ENG_GIZMO_AXIS_NONE) PickSelection(sw, sh);
        }
    }

    // The gizmo only grabs in select mode (not while placing, not over the panel).
    UpdateGizmo(sw, sh, !camBusy && !overPanel && ed.placeTool == ED_PLACE_NONE);
}

// ---- toolbar (raygui left panel) -------------------------------------------

// One stacked, full-width tool button with a gold accent bar when active.
static bool ToolRow(float x, float y, float w, const char *label, bool active) {
    if (active) DrawRectangleRec((Rectangle){ x - 5, y, 3, 24 }, (Color){ 230, 180, 60, 255 });
    return GuiButton((Rectangle){ x, y, w, 24 }, label);
}

static void DrawToolPanel(int sh) {
    const float X = 10, W = ED_PANEL_W - 20;
    DrawRectangle(0, 0, ED_PANEL_W, sh, (Color){ 24, 27, 34, 255 });
    DrawRectangle(ED_PANEL_W - 1, 0, 1, sh, (Color){ 60, 66, 80, 255 });

    float y = 10;
    DrawText("SCENE BUILDER", (int)X, (int)y, 20, (Color){ 230, 200, 120, 255 }); y += 26;
    const char *base = GetFileName(ed.path);
    DrawText(TextFormat("%s   (%d ents)", base, ed.proxyCount), (int)X, (int)y, 12,
             (Color){ 170, 175, 185, 255 }); y += 22;

    // View mode (orbit / top / fly).
    GuiLabel((Rectangle){ X, y, W, 16 }, "VIEW"); y += 18;
    int v = (int)ed.view;
    GuiToggleGroup((Rectangle){ X, y, (W - 8) / 3.0f, 24 }, "Fly;Iso;Top", &v);
    if (v != (int)ed.view) SwitchView((EdViewMode)v);
    y += 34;

    // Gizmo mode.
    GuiLabel((Rectangle){ X, y, W, 16 }, "GIZMO (move drag only)"); y += 18;
    int g = (int)ed.mode;
    GuiToggleGroup((Rectangle){ X, y, (W - 8) / 3.0f, 24 }, "Move;Rot;Scale", &g);
    ed.mode = (EngGizmoMode)g;
    y += 34;

    // Placement tool.
    GuiLabel((Rectangle){ X, y, W, 16 }, "PLACE (click ground)"); y += 18;
    struct { const char *label; EdPlaceTool tool; } tools[] = {
        { "Select / move",  ED_PLACE_NONE },
        { "Player spawn",   ED_PLACE_PLAYER },
        { "Zombie spawn",   ED_PLACE_ZOMBIE },
        { "Barricade",      ED_PLACE_BARRICADE },
    };
    for (int i = 0; i < 4; i++) {
        if (ToolRow(X, y, W, tools[i].label, ed.placeTool == tools[i].tool))
            ed.placeTool = tools[i].tool;
        y += 28;
    }
    GuiCheckBox((Rectangle){ X, y, 18, 18 }, " auto-spawn ZOMBIE", &ed.barricadeAutoSpawn);
    y += 30;

    // Edit actions.
    GuiLabel((Rectangle){ X, y, W, 16 }, "EDIT"); y += 18;
    float hw = (W - 6) / 2.0f;
    if (!EngMapHistory_CanUndo(&ed.hist)) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){ X, y, hw, 24 }, "Undo")) EngMapHistory_Undo(&ed.hist, &ed.doc);
    GuiSetState(STATE_NORMAL);
    if (!EngMapHistory_CanRedo(&ed.hist)) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){ X + hw + 6, y, hw, 24 }, "Redo")) EngMapHistory_Redo(&ed.hist, &ed.doc);
    GuiSetState(STATE_NORMAL);
    y += 30;
    if (ed.selectedId < 0) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){ X, y, W, 24 }, "Delete selected") && ed.selectedId >= 0) {
        EngMapEnt_Delete(&ed.doc, ed.selectedId); ed.selectedId = -1; CommitEdit();
    }
    GuiSetState(STATE_NORMAL);
    y += 36;

    // Selection readout.
    GuiLabel((Rectangle){ X, y, W, 16 }, "SELECTION"); y += 18;
    if (ed.selectedId >= 0) {
        EngMapEntKind k; EngMapEnt_Ptr(&ed.doc, EngMapEnt_Find(&ed.doc, ed.selectedId), &k);
        DrawText(TextFormat("#%d  %s", ed.selectedId, KindName(k)), (int)X, (int)y, 16, YELLOW); y += 20;
        if (k == ENGMAPENT_SPAWN) {
            char m[MAPDOC_SPAWN_MOB_LEN]; Eng_GetSpawnMob(&ed.doc, ed.selectedId, m, sizeof m);
            DrawText(TextFormat("mob: %s", m), (int)X, (int)y, 14, RAYWHITE); y += 18;
            DrawText("[R] flip PLAYER/ZOMBIE", (int)X, (int)y, 12, (Color){ 150, 155, 165, 255 }); y += 18;
        } else if (k == ENGMAPENT_WINDOW) {
            char d[4]; Eng_GetWindowDir(&ed.doc, ed.selectedId, d, sizeof d);
            DrawText(TextFormat("facing: %s", d), (int)X, (int)y, 14, RAYWHITE); y += 18;
            DrawText("[R] rotate facing", (int)X, (int)y, 12, (Color){ 150, 155, 165, 255 }); y += 18;
        }
    } else {
        DrawText("(nothing selected)", (int)X, (int)y, 14, (Color){ 130, 135, 145, 255 });
    }

    // Nav hint pinned to the bottom.
    const char *nav = ed.view == ED_VIEW_FLY
        ? "RMB+WASDQE fly" : "RMB orbit · MMB pan · wheel zoom";
    DrawText(nav, (int)X, sh - 22, 12, (Color){ 130, 135, 145, 255 });
}

static void EdDraw(int sw, int sh) {
    ClearBackground((Color){ 18, 20, 26, 255 });

    Eng_GfxBeginMode3D(ed.cam);
    Eng_GfxDrawGrid(80, 1.0f);
    for (int i = 0; i < ed.proxyCount; i++) {
        EdProxy *p = &ed.proxies[i];
        Vector3 c = Vector3Scale(Vector3Add(p->box.min, p->box.max), 0.5f);
        Vector3 s = Vector3Subtract(p->box.max, p->box.min);
        bool sel = (p->id == ed.selectedId);
        Color fill = p->col; fill.a = sel ? 220 : 150;
        Eng_GfxDrawCubeV(c, s, fill);
        Eng_GfxDrawCubeWiresV(c, s, sel ? YELLOW : (Color){ 0, 0, 0, 120 });
        if (sel) Eng_DebugBox(p->box, YELLOW);
    }
    Eng_DebugDraw3D(ed.cam);
    Eng_GfxEndMode3D();
    Eng_DebugDrawLabels(ed.cam);

    DrawToolPanel(sh);
    (void)sw;
}

static void EdShutdown(void) {
    EngMapHistory_Free(&ed.hist);
}

static GameModule Editor_Module(void) {
    return (GameModule){ .init = EdInit, .frame = EdFrame, .draw = EdDraw, .shutdown = EdShutdown };
}

// ---- headless check: parse a map, build proxies, print, exit (no window) ----

static int RunCheck(void) {
    int errs = MapDoc_Parse(ed.path, &ed.doc, stderr);
    RebuildProxies();
    printf("editor --check: '%s' parsed (%d errors), %d selectable entities\n",
           ed.path, errs, ed.proxyCount);
    printf("  sectors:%d walls:%d windows:%d obstacles:%d props:%d wallbuys:%d perks:%d spawns:%d\n",
           ed.doc.sectorCount, ed.doc.wallCount, ed.doc.windowCount, ed.doc.obstacleCount,
           ed.doc.propCount, ed.doc.wallbuyCount, ed.doc.perkCount, ed.doc.spawnCount);
    return errs > 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *path = "data/maps/default.map";
    bool check = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) check = true;
        else path = argv[i];   // last non-flag arg = map path
    }
    snprintf(ed.path, sizeof ed.path, "%s", path);

    if (check) return RunCheck();

    EngConfig cfg = { .w = 1280, .h = 800, .title = "Scene Builder",
                      .vsync = true, .resizable = true, .fpsCap = 60 };
    Eng_Run(&cfg, Editor_Module());
    return 0;
}
