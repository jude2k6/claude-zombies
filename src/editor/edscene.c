// ============================================================================
//  edscene.c — implementation of the editor's core document + viewport.
//  See edscene.h for the role split (scene = document + 3D view; shell = UI).
// ============================================================================

#include "edscene.h"

#include "raymath.h"
#include "pick.h"
#include "debugdraw.h"
#include "gfx.h"
#include "eng_render.h"   // Eng_RenderSetLighting / Begin/EndWorld for material mode
#include "deffile.h"      // shared .mob reader (engine-side, game-clean)
#include "content.h"      // Eng_ContentDirs, Eng_ResolveAssetPath, Eng_LoadTexture*
#include "edthumb.h"      // asset-browser thumbnail cache (freed at shutdown)

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <spawn.h>     // posix_spawn — Play Test launches the game as a child
#include <signal.h>    // SIG_IGN SIGCHLD — auto-reap finished play-test children

extern char **environ;  // pass the editor's environment to the spawned game

#define ED_WALL_H    3.0f   // assumed wall height for drawing/picking
#define ED_MARKER    0.6f   // half-size of point-entity markers (spawn/perk/…)
#define ED_ORBIT_DIST 200.0f
#define ED_SEL_SECONDARY ((Color){ 255, 140, 0, 255 })  // non-primary multi-selection outline

// ---- settings persistence (editor.cfg) -------------------------------------

void EdScene_LoadSettings(EdScene *s, EngCfg *cfg) {
    s->cfg = cfg;
    const char *paths[] = { "editor.cfg", "../editor.cfg", "./editor.cfg" };
    EngCfg_Load(cfg, paths, 3);
    s->camFlySpeed        = EngCfg_Float(cfg, "cam.flySpeed",   16.0f);
    s->camFlyBoost        = EngCfg_Float(cfg, "cam.flyBoost",   40.0f);
    s->camLookSens        = EngCfg_Float(cfg, "cam.lookSens",   0.003f);
    s->camOrbitSens       = EngCfg_Float(cfg, "cam.orbitSens",  0.005f);
    s->camZoomSpeed       = EngCfg_Float(cfg, "cam.zoomSpeed",  0.1f);
    s->viewFov            = EngCfg_Float(cfg, "view.fov",       60.0f);
    s->viewDefault        = (EdViewMode)EngCfg_Int(cfg, "view.default", ED_VIEW_ORBIT);
    s->viewOrthoH         = EngCfg_Float(cfg, "view.orthoHeight", 40.0f);
    s->gridSpacing        = EngCfg_Float(cfg, "grid.spacing",   1.0f);
    s->gridSlices         = EngCfg_Int  (cfg, "grid.slices",    80);
    s->gridVisible        = EngCfg_Bool (cfg, "grid.visible",   true);
    s->snapEnabled        = EngCfg_Bool (cfg, "grid.snap",      false);
    s->snapStep           = EngCfg_Float(cfg, "grid.snapStep",  1.0f);
    s->barricadeAutoSpawn = EngCfg_Bool (cfg, "edit.barricadeAutoSpawn", true);
    s->undoDepth          = EngCfg_Int  (cfg, "edit.undoDepth", ENGMAPHISTORY_DEFAULT_DEPTH);
    s->uiScale            = EngCfg_Float(cfg, "ui.scale",       0.0f);  // 0 = use recommendation
    s->winW               = EngCfg_Int  (cfg, "win.width",      1280);
    s->winH               = EngCfg_Int  (cfg, "win.height",     800);
    s->vsync              = EngCfg_Bool (cfg, "win.vsync",      true);
    s->fpsCap             = EngCfg_Int  (cfg, "win.fpsCap",     60);

    if (s->viewDefault < ED_VIEW_FLY || s->viewDefault > ED_VIEW_TOP) s->viewDefault = ED_VIEW_ORBIT;
    if (s->undoDepth < 1) s->undoDepth = ENGMAPHISTORY_DEFAULT_DEPTH;
}

// Emit every SCENE-OWNED setting key (cam/view/grid/edit/ui/win) into an open
// EngCfg save stream. This is the single source of truth for those keys — both
// writers (EdScene_SaveSettings here and Recents_Save in builtins.c) call it, so
// adding a setting is a one-line change in one place instead of a drift trap.
// The recents (recent.* / game.*) are owned by other modules and are written by
// each caller AFTER this, before EngCfg_EndSave.
void EdScene_PutSettingKeys(const EdScene *s, FILE *f) {
    EngCfg_PutFloat(f, "cam.flySpeed",  s->camFlySpeed);
    EngCfg_PutFloat(f, "cam.flyBoost",  s->camFlyBoost);
    EngCfg_PutFloat(f, "cam.lookSens",  s->camLookSens);
    EngCfg_PutFloat(f, "cam.orbitSens", s->camOrbitSens);
    EngCfg_PutFloat(f, "cam.zoomSpeed", s->camZoomSpeed);
    EngCfg_PutFloat(f, "view.fov",      s->viewFov);
    EngCfg_PutInt  (f, "view.default",  (int)s->viewDefault);
    EngCfg_PutFloat(f, "view.orthoHeight", s->viewOrthoH);
    EngCfg_PutFloat(f, "grid.spacing",  s->gridSpacing);
    EngCfg_PutInt  (f, "grid.slices",   s->gridSlices);
    EngCfg_PutBool (f, "grid.visible",  s->gridVisible);
    EngCfg_PutBool (f, "grid.snap",     s->snapEnabled);
    EngCfg_PutFloat(f, "grid.snapStep", s->snapStep);
    EngCfg_PutBool (f, "edit.barricadeAutoSpawn", s->barricadeAutoSpawn);
    EngCfg_PutInt  (f, "edit.undoDepth", s->undoDepth);
    EngCfg_PutFloat(f, "ui.scale",      s->uiScale);
    EngCfg_PutInt  (f, "win.width",     s->winW);
    EngCfg_PutInt  (f, "win.height",    s->winH);
    EngCfg_PutBool (f, "win.vsync",     s->vsync);
    EngCfg_PutInt  (f, "win.fpsCap",    s->fpsCap);
}

void EdScene_SaveSettings(EdScene *s, EngCfg *cfg) {
    // The recents lists (recent.* maps, game.* games) live in the same
    // editor.cfg but are owned by the panels plugin / launcher, not the scene.
    // BeginSave truncates the file, so snapshot those keys from disk FIRST and
    // re-emit them below — otherwise a clean exit would wipe the recents (and
    // the launcher's Recent Games list would be empty next launch).
    EngCfg disk;
    const char *paths[] = { cfg->path };
    EngCfg_Load(&disk, paths, 1);

    FILE *f = EngCfg_BeginSave(cfg, "Claude Zombies map editor settings");
    if (!f) return;
    EdScene_PutSettingKeys(s, f);
    // Preserve the recents owned by other modules (see note above).
    for (int i = 0; i < disk.count; i++) {
        const char *k = disk.pairs[i].key;
        if (strncmp(k, "recent.", 7) == 0 || strncmp(k, "game.", 5) == 0)
            EngCfg_PutStr(f, k, disk.pairs[i].val);
    }
    EngCfg_EndSave(f);
}

static float EdSnap(EdScene *s, float v) {
    if (!s->snapEnabled || s->snapStep <= 0.0f) return v;
    return roundf(v / s->snapStep) * s->snapStep;
}

// ---- proxy build (MapDoc → selectable boxes) -------------------------------

static float SectorFloorY(EdScene *s, int sectorId) {
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

static const EdProxy *FindProxy(EdScene *s, int id) {
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

static void SelSetSingle(EdScene *s, int id) {
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

    s->cam.target     = Vector3Add(s->cam.position, fwd);
    s->cam.up         = up;
    s->cam.fovy       = s->viewFov;
    s->cam.projection = CAMERA_PERSPECTIVE;
    s->focus = Vector3Add(s->cam.position, Vector3Scale(fwd, 20.0f));
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

    s->cam.position   = Vector3Subtract(s->focus, Vector3Scale(fwd, ED_ORBIT_DIST));
    s->cam.target     = s->focus;
    s->cam.up         = up;
    s->cam.fovy       = s->orthoH;
    s->cam.projection = CAMERA_ORTHOGRAPHIC;
}

static void EdUpdateCamera(EdScene *s, float dt, Rectangle vp) {
    switch (s->view) {
        case ED_VIEW_FLY:   UpdateCamFly(s, dt, vp);     break;
        case ED_VIEW_ORBIT: UpdateCamOrtho(s, true);      break;
        case ED_VIEW_TOP:   UpdateCamOrtho(s, false);     break;
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

// ---- placement (mapedit add + retag) ---------------------------------------

void EdScene_Commit(EdScene *s) {
    EngMapHistory_Commit(&s->hist, &s->doc, 0);
    s->dirty = true;
}

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

static int SectorAt(EdScene *s, float x, float z) {
    for (int i = 0; i < s->doc.sectorCount; i++) {
        const MapDocSector *sc = &s->doc.sectors[i];
        if (fabsf(x - sc->x) <= sc->sx * 0.5f && fabsf(z - sc->z) <= sc->sz * 0.5f) return i;
    }
    return s->doc.sectorCount > 0 ? 0 : -1;
}

static int AddSpawn(EdScene *s, const char *mob, float x, float z) {
    int id = EngMapEnt_Add(&s->doc, ENGMAPENT_SPAWN);
    if (id < 0) return -1;
    Eng_SetSpawnMob(&s->doc, id, mob);
    Eng_SetSector(&s->doc, id, SectorAt(s, x, z));
    Eng_SetPos(&s->doc, id, x, z);
    return id;
}

static void PlaceAt(EdScene *s, Vector3 p) {
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
        case ED_PLACE_SECTOR: {
            int sid = EngMapEnt_Add(&s->doc, ENGMAPENT_SECTOR);
            if (sid < 0) return;
            Eng_SetPos(&s->doc, sid, p.x, p.z);
            Eng_SetSectorSize(&s->doc, sid, 20.0f, 20.0f);
            Eng_SetSectorHeights(&s->doc, sid, 0.0f, 0.0f);
            // Sectors are not owned by another sector; no Eng_SetSector call needed.
            SelSetSingle(s, sid);
            EdScene_Commit(s);
            break;
        }
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

static uint32_t g_nextTag = 1;

static float GizmoHandleSize(EdScene *s, Vector3 origin) {
    if (s->view == ED_VIEW_FLY)
        return Vector3Distance(s->cam.position, origin) * 0.12f + 0.5f;
    return s->orthoH * 0.06f + 0.5f;
}

static void UpdateGizmo(EdScene *s, bool allowGrab) {
    Vector3 origin;
    if (!SelectedOrigin(s, &origin)) { s->dragging = false; return; }

    float handleSize = GizmoHandleSize(s, origin);
    Vector2 mouse = s->vpMouse;

    if (!s->dragging) {
        EngGizmoAxis hot = Eng_GizmoHitTest(s->cam, mouse, s->vpW, s->vpH, origin, s->mode, handleSize);
        Eng_GizmoDebugDraw(origin, s->mode, handleSize, hot);

        if (allowGrab && hot != ENG_GIZMO_AXIS_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // ROTATE and SCALE only make sense for props; TRANSLATE works for all kinds.
            bool modeOk = (s->mode == ENG_GIZMO_TRANSLATE);
            if (!modeOk) {
                EngMapEntKind k;
                EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
                modeOk = (k == ENGMAPENT_PROP);
            }
            if (modeOk) {
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
                    s->dragTag = g_nextTag++;
                    if (g_nextTag == 0) g_nextTag = 1;
                    s->dragging = true;
                }
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

// ---- mob catalog scan (data/mobs/*/*.mob via the shared deffile reader) -----

// Accumulator for one .mob file: we read only the editor-relevant fields.
typedef struct { EdMobDef def; bool sawId; } EdMobParse;

static void EdMobLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    EdMobParse *mp = (EdMobParse *)user;
    const char *k = toks[0];
    if (strcmp(k, "id") == 0 && n >= 2) {
        snprintf(mp->def.id, sizeof mp->def.id, "%s", toks[1]);
        mp->sawId = true;
    } else if (strcmp(k, "name") == 0 && n >= 2) {
        snprintf(mp->def.name, sizeof mp->def.name, "%s", toks[1]);
    } else if (strcmp(k, "tint") == 0 && n >= 4) {
        unsigned char rgba[4];
        Eng_DefParseColor(toks, n, 1, rgba);
        mp->def.tint = (Color){ rgba[0], rgba[1], rgba[2], rgba[3] };
    }
    // behaviour / stats are game-only — the editor never reads them.
}

void EdScene_ScanMobs(EdScene *s) {
    s->mobDefCount = 0;

    // Collect the ordered root directories for "mobs" via the engine resolver
    // (game root first, then library, then data/ dev fallbacks).
    char dirs[4][512];
    int nDirs = Eng_ContentDirs("mobs", dirs, 4);

    // Track which mob ids we have already added so that a game entry silently
    // shadows a same-named library/data entry (de-dup by id, first-seen wins).
    char seen[ED_MAX_MOBDEFS][ED_MOBID_LEN];
    int  nSeen = 0;

    for (int d = 0; d < nDirs && s->mobDefCount < ED_MAX_MOBDEFS; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ".mob", true);
        for (unsigned i = 0; i < files.count && s->mobDefCount < ED_MAX_MOBDEFS; i++) {
            char *text = LoadFileText(files.paths[i]);
            if (!text) continue;
            EdMobParse mp = { .def = { .name = "", .tint = { 200, 80, 80, 255 } }, .sawId = false };
            Eng_DefForEachLine(text, EdMobLineCb, &mp);
            UnloadFileText(text);
            if (!mp.sawId) continue;
            // De-dup: skip if we already added a mob with this id.
            bool dup = false;
            for (int k = 0; k < nSeen; k++) {
                if (strcmp(seen[k], mp.def.id) == 0) { dup = true; break; }
            }
            if (dup) continue;
            if (!mp.def.name[0]) snprintf(mp.def.name, sizeof mp.def.name, "%s", mp.def.id);
            s->mobDefs[s->mobDefCount++] = mp.def;
            if (nSeen < ED_MAX_MOBDEFS) snprintf(seen[nSeen++], ED_MOBID_LEN, "%s", mp.def.id);
        }
        UnloadDirectoryFiles(files);
    }

    // Always offer at least the zombie so the palette has a mob tool even when
    // run from a directory without a data/mobs catalog.
    if (s->mobDefCount == 0) {
        EdMobDef z = { .id = "ZOMBIE", .name = "Zombie spawn", .tint = { 200, 80, 80, 255 } };
        s->mobDefs[s->mobDefCount++] = z;
    }
    // Default the active mob tool to the first scanned mob.
    snprintf(s->placeMobId, sizeof s->placeMobId, "%s", s->mobDefs[0].id);
}

// ---- id+name catalogs (props / perks / wallbuy weapons) --------------------
// All three are the editor's view of a content catalog: read the placeable
// identity (id + display name) and ignore the game-only rest (model/collision/
// effect/stats) — the seam. One generic scanner serves all three; only the
// subdir + extension differ.
typedef struct { EdPropDef def; bool sawId; } EdPropParse;

static void EdPropLineCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    EdPropParse *pp = (EdPropParse *)user;
    const char *k = toks[0];
    if (strcmp(k, "id") == 0 && n >= 2) {
        snprintf(pp->def.id, sizeof pp->def.id, "%s", toks[1]);
        pp->sawId = true;
    } else if (strcmp(k, "name") == 0 && n >= 2) {
        // Join the remaining tokens so multi-word labels ("Sandbag stack") survive.
        size_t cap = sizeof pp->def.name, off = 0;
        for (int t = 1; t < n && off < cap - 1; t++) {
            int w = snprintf(pp->def.name + off, cap - off, "%s%s", t > 1 ? " " : "", toks[t]);
            if (w < 0) break;
            off += (size_t)w;
        }
    }
    // everything else (model / collide_half / cost / stats) is game-only.
}

// Scan <subdir>/*<ext> across the content roots into out[] (id + label), de-dup
// by id (game shadows library). Returns the count written. `placeId` (if non-
// NULL) is seeded with the first def's id as the default armed tool.
static int EdScanIdNameCatalog(const char *subdir, const char *ext,
                               EdPropDef out[], int max,
                               char *placeId, int placeCap) {
    int count = 0;
    char dirs[4][512];
    int nDirs = Eng_ContentDirs(subdir, dirs, 4);
    char seen[ED_MAX_PROPDEFS][ED_PROPID_LEN];
    int  nSeen = 0;

    for (int d = 0; d < nDirs && count < max; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ext, true);
        for (unsigned i = 0; i < files.count && count < max; i++) {
            char *text = LoadFileText(files.paths[i]);
            if (!text) continue;
            EdPropParse pp = { .def = { .name = "" }, .sawId = false };
            Eng_DefForEachLine(text, EdPropLineCb, &pp);
            UnloadFileText(text);
            if (!pp.sawId) continue;
            bool dup = false;
            for (int k = 0; k < nSeen && k < ED_MAX_PROPDEFS; k++)
                if (strcmp(seen[k], pp.def.id) == 0) { dup = true; break; }
            if (dup) continue;
            if (!pp.def.name[0]) snprintf(pp.def.name, sizeof pp.def.name, "%s", pp.def.id);
            out[count++] = pp.def;
            if (nSeen < ED_MAX_PROPDEFS) snprintf(seen[nSeen++], ED_PROPID_LEN, "%s", pp.def.id);
        }
        UnloadDirectoryFiles(files);
    }
    if (placeId && count > 0) snprintf(placeId, placeCap, "%s", out[0].id);
    return count;
}

void EdScene_ScanProps(EdScene *s) {
    s->propDefCount = EdScanIdNameCatalog("props", ".prop", s->propDefs,
                                          ED_MAX_PROPDEFS, s->placePropId, sizeof s->placePropId);
}
void EdScene_ScanPerks(EdScene *s) {
    s->perkDefCount = EdScanIdNameCatalog("perks", ".perk", s->perkDefs,
                                          ED_MAX_BUYDEFS, s->placePerkId, sizeof s->placePerkId);
}
void EdScene_ScanWeapons(EdScene *s) {
    s->weaponDefCount = EdScanIdNameCatalog("weapons", ".weapon", s->weaponDefs,
                                            ED_MAX_BUYDEFS, s->placeWeaponId, sizeof s->placeWeaponId);
}

void EdScene_RescanContent(EdScene *s) {
    EdScene_ScanMobs(s);
    EdScene_ScanProps(s);
    EdScene_ScanPerks(s);
    EdScene_ScanWeapons(s);
    EdAssets_Scan(&s->assets);
}

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
    // The game binary sits next to the editor binary (same build/install dir).
    char exe[1024];
    snprintf(exe, sizeof exe, "%sshooter", GetApplicationDirectory());  // trailing slash incl.
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

void EdScene_UpdateViewport(EdScene *s, Rectangle vp, bool inputAllowed) {
    float dt = GetFrameTime();
    s->vpW = (int)vp.width  < 1 ? 1 : (int)vp.width;
    s->vpH = (int)vp.height < 1 ? 1 : (int)vp.height;
    s->vpMouse = (Vector2){ GetMousePosition().x - vp.x, GetMousePosition().y - vp.y };

    EdScene_RebuildProxies(s);

    if (inputAllowed) {
        if (IsKeyPressed(KEY_F1)) EdScene_SwitchView(s, ED_VIEW_FLY);
        if (IsKeyPressed(KEY_F2)) EdScene_SwitchView(s, ED_VIEW_ORBIT);
        if (IsKeyPressed(KEY_F3)) EdScene_SwitchView(s, ED_VIEW_TOP);
        EdUpdateCamera(s, dt, vp);

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

        // F: recenter the view on the selected entity.
        if (IsKeyPressed(KEY_F) && s->selectedId >= 0) {
            Vector3 origin;
            if (SelectedOrigin(s, &origin)) {
                s->focus = origin;
                if (s->view == ED_VIEW_FLY) {
                    // Pull the fly camera back 20 units so the entity is in frame.
                    s->cam.position = Vector3Subtract(origin,
                        Vector3Scale(ForwardFrom(s->yaw, s->pitch), 20.0f));
                }
            }
        }
    } else if (s->looking) {
        // Lost focus mid-mouselook: show cursor again so the OS pointer is visible.
        s->looking = false; ShowCursor();
    }

    // Cancel a pending wall first-click whenever the tool is switched away.
    if (s->wallPending && s->placeTool != ED_PLACE_WALL) s->wallPending = false;

    bool camBusy = s->looking || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                   IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    if (inputAllowed && !camBusy && !s->dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
            if (hot == ENG_GIZMO_AXIS_NONE) {
                bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                PickSelection(s, additive);
            }
        }
    }

    UpdateGizmo(s, inputAllowed && !camBusy && s->placeTool == ED_PLACE_NONE);
}

// ---- material-mode helpers -------------------------------------------------

// Resolve a texture slot name via Eng_LoadTextureByName.  Returns NULL when
// the slot is empty or the asset is missing (caller falls back to a flat colour).
static Texture2D *MatTex(const char *slotName) {
    if (!slotName || !slotName[0]) return NULL;
    EngTexture h = Eng_LoadTextureByName(slotName);
    return Eng_TextureGet(h);  // NULL when handle invalid
}

// Draw floor/ceiling quads for every sector.
static void DrawMatSectors(EdScene *s, Texture2D *floorTex) {
    const MapDoc *d = &s->doc;
    for (int i = 0; i < d->sectorCount; i++) {
        const MapDocSector *sc = &d->sectors[i];
        Vector3 floorCentre = { sc->x, sc->yLow, sc->z };
        Eng_DrawTexturedFloorV(floorCentre, sc->sx, sc->sz,
                               floorTex, 4.0f, WHITE, (Color){ 80, 70, 55, 255 });
    }
}

// Draw walls as textured boxes sized from their proxy bounding boxes.
static void DrawMatWalls(EdScene *s, Texture2D *wallTex) {
    const MapDoc *d = &s->doc;
    for (int i = 0; i < d->wallCount; i++) {
        const MapDocWall *w = &d->walls[i];
        // Per-surface TEX wins; fall back to map-slot wall_ext.
        Texture2D *tex = wallTex;
        if (w->texName[0]) {
            EngTexture h = Eng_LoadTextureByName(w->texName);
            Texture2D *t = Eng_TextureGet(h);
            if (t) tex = t;
        }
        const EdProxy *ep = FindProxy(s, w->id);
        if (!ep) continue;
        Vector3 c  = Vector3Scale(Vector3Add(ep->box.min, ep->box.max), 0.5f);
        Vector3 sz = Vector3Subtract(ep->box.max, ep->box.min);
        Eng_DrawTexturedBoxV(c, sz, tex, 2.0f, WHITE, (Color){ 130, 120, 100, 255 });
    }
}

// Draw obstacles as textured boxes sized from their proxy bounding boxes.
static void DrawMatObstacles(EdScene *s, Texture2D *wallTex) {
    const MapDoc *d = &s->doc;
    for (int i = 0; i < d->obstacleCount; i++) {
        const MapDocObstacle *o = &d->obstacles[i];
        // Per-surface TEX wins; fall back to map-slot wall_ext.
        Texture2D *tex = wallTex;
        if (o->texName[0]) {
            EngTexture h = Eng_LoadTextureByName(o->texName);
            Texture2D *t = Eng_TextureGet(h);
            if (t) tex = t;
        }
        const EdProxy *ep = FindProxy(s, o->id);
        if (!ep) continue;
        Vector3 c  = Vector3Scale(Vector3Add(ep->box.min, ep->box.max), 0.5f);
        Vector3 sz = Vector3Subtract(ep->box.max, ep->box.min);
        Eng_DrawTexturedBoxV(c, sz, tex, 2.0f, WHITE, (Color){ 110, 100, 80, 255 });
    }
}

// Draw props: attempt to load the prop model from props/<name>/<name>.glb.
// Falls back to a small textured box on load failure (which is fine for the
// editor — we just need something visible at the correct position).
static void DrawMatProps(EdScene *s) {
    const MapDoc *d = &s->doc;
    for (int i = 0; i < d->propCount; i++) {
        const MapDocProp *p = &d->props[i];
        float y = SectorFloorY(s, p->sectorId);
        Vector3 pos = { p->x, y, p->z };

        // Build props/<name>/<name>.glb path and try loading via content registry.
        char relPath[128];
        snprintf(relPath, sizeof relPath, "props/%s/%s.glb", p->name, p->name);
        EngModel mh = Eng_LoadModel(relPath);
        Model *m = Eng_ModelGet(mh);
        if (m && m->meshCount > 0) {
            // Stamp the world shader onto the model's materials so props get the
            // same fog/sun lighting as the immediate-mode floor/wall draws (which
            // pick up the bound shader automatically). DrawModel uses the per-
            // material shader, not the rlgl batch shader, so this is required.
            if (Eng_RenderWorldShaderLoaded()) {
                Shader ws = Eng_RenderWorldShader();
                for (int k = 0; k < m->materialCount; k++) m->materials[k].shader = ws;
            }
            // Yaw around Y axis (prop uses degrees).
            Vector3 axis  = { 0.0f, 1.0f, 0.0f };
            Vector3 scale = { p->scale, p->scale, p->scale };
            Eng_GfxDrawModelEx(*m, pos, axis, p->yawDeg, scale, WHITE);
        } else {
            // No model: draw a placeholder textured box at marker size.
            float h = ED_MARKER * p->scale;
            Vector3 c  = { p->x, y + h, p->z };
            Vector3 sz = { h * 2.0f, h * 2.0f, h * 2.0f };
            Eng_DrawTexturedBoxV(c, sz, NULL, 1.0f, WHITE, (Color){ 90, 160, 110, 255 });
        }
    }
}

// Push a fixed, bright editor lighting state and bind the world shader so the
// material-mode draws get the same fog/sun/ambient program the game uses. The
// editor calls Eng_RenderLoad once at startup (editor_main); when the shader is
// missing the begin/end pair is a graceful no-op and the draws stay unlit but
// correct. Unlike the game's night fog, these defaults are deliberately bright
// (high ambient, fog pushed far past any editor map) so geometry reads clearly.
static void DrawMaterialWorld(EdScene *s) {
    const MapDocTextures *tx = &s->doc.textures;

    // Resolve map-slot textures (empty slot → NULL → flat-colour fallback).
    Texture2D *floorTex = MatTex(tx->floor[0]   ? tx->floor    : "floor_concrete");
    Texture2D *wallTex  = MatTex(tx->wall_ext[0] ? tx->wall_ext : "wall_brick");

    Eng_RenderSetLighting((EngLighting){
        .sunDir       = (Vector3){ -0.35f, -0.88f, -0.32f },  // shader normalises
        .sunColor     = (Vector3){ 1.00f,  0.98f,  0.92f },
        .ambientColor = (Vector3){ 0.45f,  0.47f,  0.52f },   // high → no black faces
        .fogColor     = (Color){ 18, 20, 26, 255 },           // matches the viewport clear
        .fogStart     = 200.0f,                               // editor maps are small;
        .fogEnd       = 2000.0f,                              // keep fog effectively off
    });
    Eng_RenderBeginWorld();
    DrawMatSectors(s, floorTex);
    DrawMatWalls(s, wallTex);
    DrawMatObstacles(s, wallTex);
    DrawMatProps(s);
    Eng_RenderEndWorld();
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

    Eng_DebugDraw3D(s->cam);
    Eng_GfxEndMode3D();
    EndTextureMode();

    // Blit (render textures are y-flipped → negative source height).
    DrawTextureRec(s->vpTex.texture,
                   (Rectangle){ 0, 0, (float)s->vpW, -(float)s->vpH },
                   (Vector2){ vp.x, vp.y }, WHITE);
}
