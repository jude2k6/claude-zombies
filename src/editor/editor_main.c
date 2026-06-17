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
#include "ui.h"   // house UI toolkit: theme + scaled text + tool button
#include "cfg.h"  // editor.cfg persistence (key=value)

#include <stdio.h>
#include <string.h>
#include <math.h>

#define ED_PANEL_W_BASE 220   // unscaled toolbar width; actual = base * uiScale
#define ED_UI_MIN 0.7f
#define ED_UI_MAX 3.0f

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

    bool          dragging;
    EngGizmoDrag  drag;
    Vector3       dragStartPos;     // selected entity pos at grab
    uint32_t      dragTag;          // coalesce token for this drag

    EdProxy       proxies[ED_MAX_ENTS];
    int           proxyCount;

    // ---- persisted settings (editor.cfg; see EdSettingsLoad/Save) ----------
    float         camFlySpeed;      // free-fly move speed
    float         camFlyBoost;      // free-fly speed while Shift held
    float         camLookSens;      // fly mouselook sensitivity
    float         camOrbitSens;     // orbit rotate sensitivity
    float         camZoomSpeed;     // ortho wheel-zoom step fraction
    float         viewFov;          // perspective fly FOV
    EdViewMode    viewDefault;      // view mode the editor opens in
    float         viewOrthoH;       // default ortho zoom on open (seeds orthoH)
    float         gridSpacing;      // world units per grid cell
    int           gridSlices;       // grid extent (cells from origin)
    bool          snapEnabled;      // snap placement + drags to snapStep
    float         snapStep;         // snap increment (world units)
    bool          barricadeAutoSpawn; // place a paired ZOMBIE spawn with a barricade
    int           undoDepth;        // history ring size (applied at launch)
    float         uiScale;          // toolbar font/layout scale
    int           winW, winH;       // window size (applied at launch)
    bool          vsync;            // vsync (applied at launch)
    int           fpsCap;           // FPS cap, 0 = uncapped (applied at launch)

    // ---- transient UI ------------------------------------------------------
    bool          showSettings;     // settings overlay open
    bool          showValidate;     // validation-report overlay open
    MapDocIssue   issues[64];       // last validation result
    int           issueCount;       // total issues found (may exceed array)
} ed;

static EngCfg edCfg;   // resolved editor.cfg path + loaded pairs

// Toolbar width tracks the UI scale so the panel grows with the font.
static int EdPanelW(void) { return (int)(ED_PANEL_W_BASE * ed.uiScale); }

// ---- settings persistence (editor.cfg) -------------------------------------
//
// Loaded once in main() BEFORE the window opens, so window/vsync/fps and the
// default view take effect on this launch. uiScale uses 0 as "absent" and falls
// back to the display recommendation in EdInit (the monitor isn't known yet at
// load time). Saved on demand (settings panel) and again on shutdown.

static void EdSettingsLoad(void) {
    const char *paths[] = { "editor.cfg", "../editor.cfg", "./editor.cfg" };
    EngCfg_Load(&edCfg, paths, 3);
    ed.camFlySpeed       = EngCfg_Float(&edCfg, "cam.flySpeed",   16.0f);
    ed.camFlyBoost       = EngCfg_Float(&edCfg, "cam.flyBoost",   40.0f);
    ed.camLookSens       = EngCfg_Float(&edCfg, "cam.lookSens",   0.003f);
    ed.camOrbitSens      = EngCfg_Float(&edCfg, "cam.orbitSens",  0.005f);
    ed.camZoomSpeed      = EngCfg_Float(&edCfg, "cam.zoomSpeed",  0.1f);
    ed.viewFov           = EngCfg_Float(&edCfg, "view.fov",       60.0f);
    ed.viewDefault       = (EdViewMode)EngCfg_Int(&edCfg, "view.default", ED_VIEW_ORBIT);
    ed.viewOrthoH        = EngCfg_Float(&edCfg, "view.orthoHeight", 40.0f);
    ed.gridSpacing       = EngCfg_Float(&edCfg, "grid.spacing",   1.0f);
    ed.gridSlices        = EngCfg_Int  (&edCfg, "grid.slices",    80);
    ed.snapEnabled       = EngCfg_Bool (&edCfg, "grid.snap",      false);
    ed.snapStep          = EngCfg_Float(&edCfg, "grid.snapStep",  1.0f);
    ed.barricadeAutoSpawn= EngCfg_Bool (&edCfg, "edit.barricadeAutoSpawn", true);
    ed.undoDepth         = EngCfg_Int  (&edCfg, "edit.undoDepth", ENGMAPHISTORY_DEFAULT_DEPTH);
    ed.uiScale           = EngCfg_Float(&edCfg, "ui.scale",       0.0f);  // 0 = use recommendation
    ed.winW              = EngCfg_Int  (&edCfg, "win.width",      1280);
    ed.winH              = EngCfg_Int  (&edCfg, "win.height",     800);
    ed.vsync             = EngCfg_Bool (&edCfg, "win.vsync",      true);
    ed.fpsCap            = EngCfg_Int  (&edCfg, "win.fpsCap",     60);

    if (ed.viewDefault < ED_VIEW_FLY || ed.viewDefault > ED_VIEW_TOP) ed.viewDefault = ED_VIEW_ORBIT;
    if (ed.undoDepth < 1) ed.undoDepth = ENGMAPHISTORY_DEFAULT_DEPTH;
}

static void EdSettingsSave(void) {
    FILE *f = EngCfg_BeginSave(&edCfg, "Claude Zombies map editor settings");
    if (!f) return;
    EngCfg_PutFloat(f, "cam.flySpeed",  ed.camFlySpeed);
    EngCfg_PutFloat(f, "cam.flyBoost",  ed.camFlyBoost);
    EngCfg_PutFloat(f, "cam.lookSens",  ed.camLookSens);
    EngCfg_PutFloat(f, "cam.orbitSens", ed.camOrbitSens);
    EngCfg_PutFloat(f, "cam.zoomSpeed", ed.camZoomSpeed);
    EngCfg_PutFloat(f, "view.fov",      ed.viewFov);
    EngCfg_PutInt  (f, "view.default",  (int)ed.viewDefault);
    EngCfg_PutFloat(f, "view.orthoHeight", ed.viewOrthoH);
    EngCfg_PutFloat(f, "grid.spacing",  ed.gridSpacing);
    EngCfg_PutInt  (f, "grid.slices",   ed.gridSlices);
    EngCfg_PutBool (f, "grid.snap",     ed.snapEnabled);
    EngCfg_PutFloat(f, "grid.snapStep", ed.snapStep);
    EngCfg_PutBool (f, "edit.barricadeAutoSpawn", ed.barricadeAutoSpawn);
    EngCfg_PutInt  (f, "edit.undoDepth", ed.undoDepth);
    EngCfg_PutFloat(f, "ui.scale",      ed.uiScale);
    EngCfg_PutInt  (f, "win.width",     ed.winW);
    EngCfg_PutInt  (f, "win.height",    ed.winH);
    EngCfg_PutBool (f, "win.vsync",     ed.vsync);
    EngCfg_PutInt  (f, "win.fpsCap",    ed.fpsCap);
    EngCfg_EndSave(f);
}

// Round to the snap grid when snapping is on (used by placement + drags).
static float EdSnap(float v) {
    if (!ed.snapEnabled || ed.snapStep <= 0.0f) return v;
    return roundf(v / ed.snapStep) * ed.snapStep;
}

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
        ed.yaw   += md.x * ed.camLookSens;
        ed.pitch -= md.y * ed.camLookSens;
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
    float speed = IsKeyDown(KEY_LEFT_SHIFT) ? ed.camFlyBoost : ed.camFlySpeed;
    if (Vector3LengthSqr(move) > 1e-4f)
        ed.cam.position = Vector3Add(ed.cam.position, Vector3Scale(Vector3Normalize(move), speed * dt));

    ed.cam.target     = Vector3Add(ed.cam.position, fwd);
    ed.cam.up         = up;
    ed.cam.fovy       = ed.viewFov;
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
        ed.yaw   += md.x * ed.camOrbitSens;
        ed.pitch -= md.y * ed.camOrbitSens;
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
        ed.orthoH *= (1.0f - wheel * ed.camZoomSpeed);
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
               EdSnap(ed.dragStartPos.x + d.translate.x),
               EdSnap(ed.dragStartPos.z + d.translate.z));
    EngMapHistory_Commit(&ed.hist, &ed.doc, ed.dragTag);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) ed.dragging = false;
}

// ---- GameModule ------------------------------------------------------------

static void EdInit(void) {
    if (MapDoc_Parse(ed.path, &ed.doc, stderr) > 0)
        fprintf(stderr, "editor: '%s' parsed with errors (continuing)\n", ed.path);
    // Settings (EdSettingsLoad in main) drive history depth, view, zoom, scale.
    EngMapHistory_Init(&ed.hist, ed.undoDepth);
    EngMapHistory_Commit(&ed.hist, &ed.doc, 0);   // initial checkpoint
    Eng_DebugSetEnabled(true);

    ed.cam = (Camera3D){ .position = { 0, 24, 28 }, .target = { 0, 0, 0 },
                         .up = { 0, 1, 0 }, .fovy = ed.viewFov, .projection = CAMERA_PERSPECTIVE };
    ed.view   = ed.viewDefault;    // open in the configured view mode
    ed.yaw    = PI * 0.25f;        // 45° azimuth
    ed.pitch  = -0.6f;             // ~34° down → isometric-ish
    ed.focus  = (Vector3){ 0, 0, 0 };
    ed.orthoH = ed.viewOrthoH;     // configured default zoom
    ed.selectedId = -1;
    ed.mode = ENG_GIZMO_TRANSLATE;
    ed.placeTool = ED_PLACE_NONE;
    if (ed.uiScale <= 0.0f) ed.uiScale = Eng_UiRecommendedScale();  // 0 = absent in cfg
    Eng_UiApplyTheme();   // dark/gold house style for every raygui control
}

static void EdFrame(float dt, int sw, int sh) {
    RebuildProxies();

    // While a modal overlay (settings / validation) is open it owns the input:
    // suppress world hotkeys, camera, and 3D clicks so nothing leaks through.
    bool overlay = ed.showSettings || ed.showValidate;
    if (overlay && IsKeyPressed(KEY_ESCAPE)) { ed.showSettings = ed.showValidate = false; }

    if (!overlay) {
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
    }

    // Undo / redo + UI scale (Ctrl + = / -) — allowed even with an overlay open.
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_Z)) EngMapHistory_Undo(&ed.hist, &ed.doc);
    if (ctrl && IsKeyPressed(KEY_Y)) EngMapHistory_Redo(&ed.hist, &ed.doc);
    if (ctrl && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)))
        ed.uiScale = fminf(ED_UI_MAX, ed.uiScale + 0.1f);
    if (ctrl && (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)))
        ed.uiScale = fmaxf(ED_UI_MIN, ed.uiScale - 0.1f);

    // The camera is being driven when fly-looking or while orbit/pan buttons are
    // held — suppress selection/placement so those clicks don't double up.
    // Also ignore 3D clicks that land on the toolbar or a modal overlay.
    bool overPanel = overlay || (GetMousePosition().x < EdPanelW());
    bool camBusy = ed.looking || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                   IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    if (!camBusy && !overPanel && !ed.dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (ed.placeTool != ED_PLACE_NONE) {
            // Placement mode: drop the tool's entity where the cursor ray meets
            // the ground plane.
            Vector3 hit;
            Ray ray = Eng_PickRayFromScreen(ed.cam, GetMousePosition(), sw, sh);
            if (Eng_PickRayGroundY(ray, 0.0f, &hit)) {
                hit.x = EdSnap(hit.x); hit.z = EdSnap(hit.z);
                PlaceAt(hit);
            }
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
//
// Everything is laid out in scaled pixels: `s = ed.uiScale` multiplies fonts,
// row heights, and the panel width, so the toolbar grows/shrinks as one. The
// house style, scaled text, and accent-bar tool button come from ui; the
// global UI scale is pushed once per frame so its helpers track ed.uiScale.

static void DrawToolPanel(int sh) {
    float s = ed.uiScale;
    int   pw = EdPanelW();
    float X = 10 * s, W = pw - 20 * s;
    Eng_UiSetScale(s);          // ui text/widgets follow the editor's scale
    Eng_UiApplyFont(16);        // scale every raygui control's font (16 * s)

    DrawRectangle(0, 0, pw, sh, (Color){ 24, 27, 34, 255 });
    DrawRectangle(pw - 1, 0, 1, sh, (Color){ 60, 66, 80, 255 });

    float y = 10 * s;
    Eng_UiText("SCENE BUILDER", X, y, 20, (Color){ 230, 200, 120, 255 }); y += 26 * s;
    Eng_UiText(TextFormat("%s   (%d ents)", GetFileName(ed.path), ed.proxyCount),
               X, y, 12, (Color){ 170, 175, 185, 255 }); y += 20 * s;

    // UI scale (recommended from the display; adjust here or with Ctrl +/-).
    GuiSlider((Rectangle){ X + 18 * s, y, W - 60 * s, 16 * s }, "UI",
              TextFormat("%.0f%%", ed.uiScale * 100.0f), &ed.uiScale, ED_UI_MIN, ED_UI_MAX);
    y += 24 * s;

    // View mode (fly / iso / top).
    GuiLabel((Rectangle){ X, y, W, 16 * s }, "VIEW"); y += 18 * s;
    int v = (int)ed.view;
    GuiToggleGroup((Rectangle){ X, y, (W - 8 * s) / 3.0f, 24 * s }, "Fly;Iso;Top", &v);
    if (v != (int)ed.view) SwitchView((EdViewMode)v);
    y += 34 * s;

    // Gizmo mode.
    GuiLabel((Rectangle){ X, y, W, 16 * s }, "GIZMO (move drag only)"); y += 18 * s;
    int g = (int)ed.mode;
    GuiToggleGroup((Rectangle){ X, y, (W - 8 * s) / 3.0f, 24 * s }, "Move;Rot;Scale", &g);
    ed.mode = (EngGizmoMode)g;
    y += 34 * s;

    // Placement tool.
    GuiLabel((Rectangle){ X, y, W, 16 * s }, "PLACE (click ground)"); y += 18 * s;
    struct { const char *label; EdPlaceTool tool; } tools[] = {
        { "Select / move",  ED_PLACE_NONE },
        { "Player spawn",   ED_PLACE_PLAYER },
        { "Zombie spawn",   ED_PLACE_ZOMBIE },
        { "Barricade",      ED_PLACE_BARRICADE },
    };
    for (int i = 0; i < 4; i++) {
        if (Eng_UiToolButton((Rectangle){ X, y, W, 24 * s }, tools[i].label,
                             ed.placeTool == tools[i].tool))
            ed.placeTool = tools[i].tool;
        y += 28 * s;
    }
    GuiCheckBox((Rectangle){ X, y, 18 * s, 18 * s }, " auto-spawn ZOMBIE", &ed.barricadeAutoSpawn);
    y += 30 * s;

    // Edit actions.
    GuiLabel((Rectangle){ X, y, W, 16 * s }, "EDIT"); y += 18 * s;
    float hw = (W - 6 * s) / 2.0f;
    if (!EngMapHistory_CanUndo(&ed.hist)) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){ X, y, hw, 24 * s }, "Undo")) EngMapHistory_Undo(&ed.hist, &ed.doc);
    GuiSetState(STATE_NORMAL);
    if (!EngMapHistory_CanRedo(&ed.hist)) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){ X + hw + 6 * s, y, hw, 24 * s }, "Redo")) EngMapHistory_Redo(&ed.hist, &ed.doc);
    GuiSetState(STATE_NORMAL);
    y += 30 * s;
    if (ed.selectedId < 0) GuiSetState(STATE_DISABLED);
    if (GuiButton((Rectangle){ X, y, W, 24 * s }, "Delete selected") && ed.selectedId >= 0) {
        EngMapEnt_Delete(&ed.doc, ed.selectedId); ed.selectedId = -1; CommitEdit();
    }
    GuiSetState(STATE_NORMAL);
    y += 36 * s;

    // Map tools: settings + validation overlays.
    GuiLabel((Rectangle){ X, y, W, 16 * s }, "MAP"); y += 18 * s;
    if (Eng_UiToolButton((Rectangle){ X, y, W, 24 * s }, "Settings...", ed.showSettings))
        ed.showSettings = !ed.showSettings;
    y += 28 * s;
    if (Eng_UiToolButton((Rectangle){ X, y, W, 24 * s }, "Validate map", ed.showValidate)) {
        ed.issueCount = MapDoc_Validate(&ed.doc, ed.issues, 64);
        ed.showValidate = true;
    }
    y += 34 * s;

    // Selection readout.
    GuiLabel((Rectangle){ X, y, W, 16 * s }, "SELECTION"); y += 18 * s;
    if (ed.selectedId >= 0) {
        EngMapEntKind k; EngMapEnt_Ptr(&ed.doc, EngMapEnt_Find(&ed.doc, ed.selectedId), &k);
        Eng_UiText(TextFormat("#%d  %s", ed.selectedId, KindName(k)), X, y, 16, YELLOW); y += 20 * s;
        if (k == ENGMAPENT_SPAWN) {
            char m[MAPDOC_SPAWN_MOB_LEN]; Eng_GetSpawnMob(&ed.doc, ed.selectedId, m, sizeof m);
            Eng_UiText(TextFormat("mob: %s", m), X, y, 14, RAYWHITE); y += 18 * s;
            Eng_UiText("[R] flip PLAYER/ZOMBIE", X, y, 12, (Color){ 150, 155, 165, 255 }); y += 18 * s;
        } else if (k == ENGMAPENT_WINDOW) {
            char d[4]; Eng_GetWindowDir(&ed.doc, ed.selectedId, d, sizeof d);
            Eng_UiText(TextFormat("facing: %s", d), X, y, 14, RAYWHITE); y += 18 * s;
            Eng_UiText("[R] rotate facing", X, y, 12, (Color){ 150, 155, 165, 255 }); y += 18 * s;
        }
    } else {
        Eng_UiText("(nothing selected)", X, y, 14, (Color){ 130, 135, 145, 255 });
    }

    // Nav hint pinned to the bottom.
    const char *nav = ed.view == ED_VIEW_FLY
        ? "RMB+WASDQE fly" : "RMB orbit / MMB pan / wheel zoom";
    Eng_UiText(nav, X, sh - 22 * s, 12, (Color){ 130, 135, 145, 255 });
}

// ---- modal overlays (settings / validation) --------------------------------
//
// Centered panels drawn over the viewport; EdFrame routes input away from the
// world while one is open. All geometry is in scaled pixels like the toolbar.

// A labeled float slider row; value shown at the right. Advances *y.
static void OvSlider(float X, float W, float *y, float s, const char *label,
                     float *v, float lo, float hi, const char *valfmt) {
    Eng_UiText(label, X, *y + 1 * s, 13, (Color){ 200, 205, 215, 255 });
    char buf[32]; snprintf(buf, sizeof buf, valfmt, *v);
    GuiSlider((Rectangle){ X + 130 * s, *y, W - 175 * s, 16 * s }, "", buf, v, lo, hi);
    *y += 24 * s;
}
static void OvCheck(float X, float *y, float s, const char *label, bool *v) {
    GuiCheckBox((Rectangle){ X, *y, 16 * s, 16 * s }, label, v);
    *y += 24 * s;
}
static void OvSection(float X, float W, float *y, float s, const char *label) {
    GuiLabel((Rectangle){ X, *y, W, 16 * s }, label);
    *y += 18 * s;
}
// Common dark panel with a 1px border; returns the inner content origin.
static void OvPanel(float px, float py, float pw, float ph) {
    Eng_UiPanelBg((Rectangle){ px - 2, py - 2, pw + 4, ph + 4 }, (Color){ 60, 66, 80, 255 });
    Eng_UiPanelBg((Rectangle){ px, py, pw, ph },                 (Color){ 24, 27, 34, 255 });
}

static void DrawSettingsOverlay(int sw, int sh) {
    float s = ed.uiScale;
    Eng_UiSetScale(s); Eng_UiApplyFont(13);
    float pw = 420 * s, ph = 552 * s;
    float px = (sw - pw) / 2.0f, py = (sh - ph) / 2.0f; if (py < 6 * s) py = 6 * s;
    OvPanel(px, py, pw, ph);
    float X = px + 16 * s, W = pw - 32 * s, y = py + 12 * s;
    Eng_UiText("EDITOR SETTINGS", X, y, 18, (Color){ 230, 200, 120, 255 }); y += 28 * s;

    OvSection(X, W, &y, s, "CAMERA");
    OvSlider(X, W, &y, s, "Fly speed",  &ed.camFlySpeed,  2, 60,        "%.0f");
    OvSlider(X, W, &y, s, "Fly boost",  &ed.camFlyBoost,  10, 120,      "%.0f");
    OvSlider(X, W, &y, s, "Look sens",  &ed.camLookSens,  0.0005f, 0.01f, "%.4f");
    OvSlider(X, W, &y, s, "Orbit sens", &ed.camOrbitSens, 0.001f, 0.02f, "%.3f");
    OvSlider(X, W, &y, s, "Zoom speed", &ed.camZoomSpeed, 0.02f, 0.3f,  "%.2f");

    OvSection(X, W, &y, s, "VIEW");
    Eng_UiText("Default view", X, y + 1 * s, 13, (Color){ 200, 205, 215, 255 });
    int vd = (int)ed.viewDefault;
    GuiToggleGroup((Rectangle){ X + 130 * s, y, (W - 132 * s) / 3.0f, 18 * s }, "Fly;Iso;Top", &vd);
    ed.viewDefault = (EdViewMode)vd; y += 26 * s;
    OvSlider(X, W, &y, s, "FOV",          &ed.viewFov,    45, 100, "%.0f");
    OvSlider(X, W, &y, s, "Default zoom", &ed.viewOrthoH, 5, 200,  "%.0f");

    OvSection(X, W, &y, s, "GRID / SNAP");
    OvSlider(X, W, &y, s, "Grid spacing", &ed.gridSpacing, 0.25f, 10, "%.2f");
    float slices = (float)ed.gridSlices;
    OvSlider(X, W, &y, s, "Grid extent",  &slices, 10, 400, "%.0f"); ed.gridSlices = (int)slices;
    OvCheck(X, &y, s, " Snap to grid", &ed.snapEnabled);
    OvSlider(X, W, &y, s, "Snap step",    &ed.snapStep, 0.1f, 10, "%.2f");

    OvSection(X, W, &y, s, "EDITING");
    OvCheck(X, &y, s, " Barricade auto-spawn ZOMBIE", &ed.barricadeAutoSpawn);
    float depth = (float)ed.undoDepth;
    OvSlider(X, W, &y, s, "Undo depth*", &depth, 16, 256, "%.0f"); ed.undoDepth = (int)depth;

    OvSection(X, W, &y, s, "DISPLAY   (* applies on restart)");
    OvCheck(X, &y, s, " VSync*", &ed.vsync);
    float fps = (float)ed.fpsCap;
    OvSlider(X, W, &y, s, "FPS cap*", &fps, 0, 240, "%.0f"); ed.fpsCap = (int)fps;

    float by = py + ph - 32 * s, hw = (W - 8 * s) / 2.0f;
    if (GuiButton((Rectangle){ X, by, hw, 24 * s }, "Save"))  EdSettingsSave();
    if (GuiButton((Rectangle){ X + hw + 8 * s, by, hw, 24 * s }, "Close")) ed.showSettings = false;
}

static void DrawValidateOverlay(int sw, int sh) {
    float s = ed.uiScale;
    Eng_UiSetScale(s); Eng_UiApplyFont(13);
    float pw = 470 * s, ph = 420 * s;
    float px = (sw - pw) / 2.0f, py = (sh - ph) / 2.0f; if (py < 6 * s) py = 6 * s;
    OvPanel(px, py, pw, ph);
    float X = px + 16 * s, W = pw - 32 * s, y = py + 12 * s;
    Eng_UiText("MAP VALIDATION", X, y, 18, (Color){ 230, 200, 120, 255 }); y += 28 * s;

    if (ed.issueCount == 0) {
        Eng_UiText("No problems found - map looks good.", X, y, 14, (Color){ 120, 210, 140, 255 });
    } else {
        Eng_UiText(TextFormat("%d issue(s)  (click a row to select it):", ed.issueCount),
                   X, y, 13, (Color){ 235, 200, 120, 255 });
        y += 22 * s;
        int shown = ed.issueCount < 64 ? ed.issueCount : 64;
        for (int i = 0; i < shown; i++) {
            bool err = ed.issues[i].severity == MAPDOC_ERROR;
            Rectangle row = { X, y, W, 15 * s };
            if (ed.issues[i].entId >= 0 && CheckCollisionPointRec(GetMousePosition(), row) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                ed.selectedId = ed.issues[i].entId;
            Eng_UiText(TextFormat("%s %s", err ? "[E]" : "[w]", ed.issues[i].msg), X, y, 12,
                       err ? (Color){ 235, 110, 110, 255 } : (Color){ 230, 200, 120, 255 });
            y += 16 * s;
            if (y > py + ph - 48 * s) { Eng_UiText("...", X, y, 12, (Color){ 150, 150, 150, 255 }); break; }
        }
    }

    float by = py + ph - 32 * s, hw = (W - 8 * s) / 2.0f;
    if (GuiButton((Rectangle){ X, by, hw, 24 * s }, "Re-check"))
        ed.issueCount = MapDoc_Validate(&ed.doc, ed.issues, 64);
    if (GuiButton((Rectangle){ X + hw + 8 * s, by, hw, 24 * s }, "Close")) ed.showValidate = false;
}

static void EdDraw(int sw, int sh) {
    ClearBackground((Color){ 18, 20, 26, 255 });

    Eng_GfxBeginMode3D(ed.cam);
    Eng_GfxDrawGrid(ed.gridSlices, ed.gridSpacing);
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
    if (ed.showSettings) DrawSettingsOverlay(sw, sh);
    if (ed.showValidate) DrawValidateOverlay(sw, sh);
}

static void EdShutdown(void) {
    EdSettingsSave();   // persist any tweaks made this session
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

    MapDocIssue issues[64];
    int nIssues = MapDoc_Validate(&ed.doc, issues, 64);
    int errors = 0;
    if (nIssues == 0) {
        printf("  validation: OK\n");
    } else {
        printf("  validation: %d issue(s)\n", nIssues);
        for (int i = 0; i < nIssues && i < 64; i++) {
            printf("    [%s] %s\n", issues[i].severity == MAPDOC_ERROR ? "ERROR" : "warn", issues[i].msg);
            if (issues[i].severity == MAPDOC_ERROR) errors++;
        }
    }
    return (errs > 0 || errors > 0) ? 1 : 0;
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

    EdSettingsLoad();   // before the window: window/vsync/fps + default view apply now
    EngConfig cfg = { .w = ed.winW, .h = ed.winH, .title = "Scene Builder",
                      .vsync = ed.vsync, .resizable = true, .fpsCap = ed.fpsCap };
    Eng_Run(&cfg, Editor_Module());
    return 0;
}
