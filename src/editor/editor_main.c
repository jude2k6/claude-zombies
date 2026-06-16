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

static struct {
    char          path[512];
    MapDoc        doc;
    EngMapHistory hist;

    Camera3D      cam;
    float         yaw, pitch;       // fly-cam look angles
    bool          looking;          // RMB held → mouselook

    int           selectedId;       // -1 = nothing selected
    EngGizmoMode  mode;             // current gizmo mode

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
        AddProxy(s->id, ENGMAPENT_SPAWN,
                 BoxAround(s->x, y + ED_MARKER, s->z, ED_MARKER, ED_MARKER, ED_MARKER),
                 (Color){ 90, 130, 200, 255 });
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

// ---- fly camera ------------------------------------------------------------

static void UpdateFlyCam(float dt) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { ed.looking = true;  DisableCursor(); }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)){ ed.looking = false; EnableCursor();  }

    if (ed.looking) {
        Vector2 md = GetMouseDelta();
        ed.yaw   += md.x * 0.003f;
        ed.pitch -= md.y * 0.003f;
        if (ed.pitch >  1.55f) ed.pitch =  1.55f;
        if (ed.pitch < -1.55f) ed.pitch = -1.55f;
    }

    Vector3 fwd   = { cosf(ed.pitch) * sinf(ed.yaw), sinf(ed.pitch), -cosf(ed.pitch) * cosf(ed.yaw) };
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
    ed.cam.target = Vector3Add(ed.cam.position, fwd);
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

// ---- gizmo drag (translate) → mapedit → history ----------------------------

static uint32_t g_nextTag = 1;

static void UpdateGizmo(int sw, int sh) {
    Vector3 origin;
    if (!SelectedOrigin(&origin)) { ed.dragging = false; return; }

    float handleSize = Vector3Distance(ed.cam.position, origin) * 0.12f + 0.5f;
    Vector2 mouse = GetMousePosition();

    if (!ed.dragging) {
        EngGizmoAxis hot = Eng_GizmoHitTest(ed.cam, mouse, sw, sh, origin, ed.mode, handleSize);
        Eng_GizmoDebugDraw(origin, ed.mode, handleSize, hot);

        // Start a drag if the user presses on a handle (translate only for now).
        if (hot != ENG_GIZMO_AXIS_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
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
    ed.yaw = 0.0f; ed.pitch = -0.6f;
    ed.selectedId = -1;
    ed.mode = ENG_GIZMO_TRANSLATE;
}

static void EdFrame(float dt, int sw, int sh) {
    RebuildProxies();
    UpdateFlyCam(dt);

    // Gizmo mode hotkeys.
    if (IsKeyPressed(KEY_ONE))   ed.mode = ENG_GIZMO_TRANSLATE;
    if (IsKeyPressed(KEY_TWO))   ed.mode = ENG_GIZMO_ROTATE;
    if (IsKeyPressed(KEY_THREE)) ed.mode = ENG_GIZMO_SCALE;

    // Undo / redo.
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_Z)) EngMapHistory_Undo(&ed.hist, &ed.doc);
    if (ctrl && IsKeyPressed(KEY_Y)) EngMapHistory_Redo(&ed.hist, &ed.doc);

    // Left-click selects when not interacting with a gizmo handle/drag.
    if (!ed.looking && !ed.dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector3 origin; float hs = 0;
        EngGizmoAxis hot = ENG_GIZMO_AXIS_NONE;
        if (SelectedOrigin(&origin)) {
            hs = Vector3Distance(ed.cam.position, origin) * 0.12f + 0.5f;
            hot = Eng_GizmoHitTest(ed.cam, GetMousePosition(), sw, sh, origin, ed.mode, hs);
        }
        if (hot == ENG_GIZMO_AXIS_NONE) PickSelection(sw, sh);
    }

    if (!ed.looking) UpdateGizmo(sw, sh);
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

    // Minimal text HUD (raw raylib until the Eng_Ui* facade lands — see
    // docs/engine-layers.md Open #1).
    const char *modeName = ed.mode == ENG_GIZMO_TRANSLATE ? "TRANSLATE"
                         : ed.mode == ENG_GIZMO_ROTATE    ? "ROTATE" : "SCALE";
    DrawText(TextFormat("%s   ents:%d   %s", ed.path, ed.proxyCount, modeName), 10, 10, 18, RAYWHITE);
    if (ed.selectedId >= 0) {
        EngMapEntKind k; EngMapEnt_Ptr(&ed.doc, EngMapEnt_Find(&ed.doc, ed.selectedId), &k);
        DrawText(TextFormat("selected: #%d (%s)", ed.selectedId, KindName(k)), 10, 32, 18, YELLOW);
    }
    DrawText("RMB+WASDQE fly  LMB select/drag  1/2/3 mode  Ctrl+Z/Y undo/redo",
             10, sh - 24, 16, (Color){ 170, 175, 185, 255 });
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
