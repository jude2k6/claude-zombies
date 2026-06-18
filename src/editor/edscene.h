// ============================================================================
//  edscene.h — the editor's CORE document + viewport, independent of the shell.
//
//  EdScene is everything that is "the map being edited and the 3D view of it":
//  the MapDoc, its undo history, the camera (fly / iso / top), selection, the
//  gizmo drag, placement tools, and the persisted editor settings. It knows
//  NOTHING about menus, panels, docking, or plugins — that is the shell's job
//  (edhost.{c,h}). The shell hosts a viewport rectangle and calls into the
//  scene to update + draw it; menus and panels read/mutate the scene through
//  the small verb surface at the bottom of this header.
//
//  Like the rest of the editor this depends on the ENGINE ONLY (MapDoc + the
//  pick/gizmo/mapedit/debugdraw toolkit), never a src/game/ header.
// ============================================================================
#ifndef SHOOTER_EDSCENE_H
#define SHOOTER_EDSCENE_H

#include "raylib.h"
#include "mapdoc.h"
#include "mapedit.h"
#include "gizmo.h"
#include "cfg.h"
#include "edassets.h"
#include <stdbool.h>

// Upper bound on simultaneously-selectable proxies (one box per MapDoc entity).
#define ED_MAX_ENTS  (MAPDOC_MAX_SPAWNS + MAPDOC_MAX_WALLS + MAPDOC_MAX_WINDOWS + \
                      MAPDOC_MAX_OBSTACLES + MAPDOC_MAX_PROPS + MAPDOC_MAX_WALLBUYS + \
                      MAPDOC_MAX_PERKS + MAPDOC_MAX_SECTORS + 4)

// View modes. FLY is the free no-clip perspective camera; ORBIT and TOP are
// orthographic editor views pivoting around a focus point.
typedef enum { ED_VIEW_FLY = 0, ED_VIEW_ORBIT, ED_VIEW_TOP } EdViewMode;

// Placement tool: what a ground-click drops. NONE = click selects instead.
// ED_PLACE_MOB drops a SPAWN whose mob tag is `placeMobId` — the data-driven
// path fed by the data/mobs/ catalog scan (replaces a hardcoded ZOMBIE tool).
// ED_PLACE_WALL is two-click: first click sets wallStart, second click places.
typedef enum {
    ED_PLACE_NONE      = 0,
    // Spawns
    ED_PLACE_PLAYER,
    ED_PLACE_MOB,
    // Geometry
    ED_PLACE_BARRICADE,
    ED_PLACE_WALL,
    ED_PLACE_OBSTACLE,
    ED_PLACE_PROP,
    ED_PLACE_SECTOR,
    // Buyables
    ED_PLACE_WALLBUY,
    ED_PLACE_PERK,
    ED_PLACE_COUNT
} EdPlaceTool;

// A mob the editor can place, scanned from data/mobs/*/*.mob via the engine's
// shared deffile reader. The editor only needs the placeable identity (id +
// label + marker colour); it never reads `behaviour` (game-only) — same seam
// as docs/editor-content-extensibility.md §3.
#define ED_MAX_MOBDEFS 24
#define ED_MOBID_LEN   24
typedef struct {
    char  id[ED_MOBID_LEN];   // SPAWN MOB <id> written on placement
    char  name[48];           // palette label
    Color tint;               // marker colour
} EdMobDef;

// A placeable definition scanned from a content catalog — the editor's view of
// a prop (props/*.prop), perk (perks/*.perk), or wallbuy weapon (weapons/
// *.weapon). Only the placeable identity (id written to the map + a label); the
// game owns the rest (model/collision/effect/stats) — the seam.
#define ED_MAX_PROPDEFS 32
#define ED_MAX_BUYDEFS  16   // perks / wallbuy weapons
#define ED_PROPID_LEN   32
typedef struct {
    char  id[ED_PROPID_LEN];  // id written on placement (e.g. MapDocProp.name)
    char  name[48];           // palette label
} EdPropDef;

// A draw/pick proxy: one selectable box per MapDoc entity, tagged by stable id.
typedef struct {
    int           id;
    EngMapEntKind kind;
    BoundingBox   box;
    Color         col;
} EdProxy;

typedef struct EdScene {
    char          path[512];
    bool          dirty;            // unsaved edits since last save/open
    MapDoc        doc;
    EngMapHistory hist;

    // ---- camera / view -----------------------------------------------------
    Camera3D      cam;
    EdViewMode    view;
    float         yaw, pitch;       // shared look angles (fly + orbit)
    bool          looking;          // fly: RMB held → mouselook
    Vector3       focus;            // orbit/top: point the view pivots around
    float         orthoH;           // orbit/top: orthographic view height (zoom)
    bool          materialMode;     // M key toggle: draw textured geometry instead of proxy boxes

    // ---- selection / tools -------------------------------------------------
    int           selectedId;       // -1 = nothing selected
    EngGizmoMode  mode;             // current gizmo mode
    EdPlaceTool   placeTool;        // active placement tool (NONE = select mode)
    char          placeMobId[ED_MOBID_LEN];    // ED_PLACE_MOB: which mob tag to drop
    char          placePropId[ED_PROPID_LEN];  // ED_PLACE_PROP: which prop id to drop
    char          placePerkId[ED_PROPID_LEN];  // ED_PLACE_PERK: which perk id to drop
    char          placeWeaponId[ED_PROPID_LEN];// ED_PLACE_WALLBUY: which weapon id

    // ED_PLACE_WALL two-click state: first click stores wallPending = true and
    // wallStart{x,z}; second click completes the wall segment.
    bool          wallPending;      // true = first click done, awaiting second
    float         wallStartX, wallStartZ;  // world position of first wall click

    // ---- mob catalog (scanned from data/mobs/) -----------------------------
    EdMobDef      mobDefs[ED_MAX_MOBDEFS];
    int           mobDefCount;
    EdPropDef     propDefs[ED_MAX_PROPDEFS];   // scanned from props/*/*.prop
    int           propDefCount;
    EdPropDef     perkDefs[ED_MAX_BUYDEFS];    // scanned from perks/*/*.perk
    int           perkDefCount;
    EdPropDef     weaponDefs[ED_MAX_BUYDEFS];  // scanned from weapons/*/*.weapon
    int           weaponDefCount;

    EdAssetIndex  assets;           // maps/models/textures index (asset browser)

    bool          dragging;
    EngGizmoDrag  drag;
    Vector3       dragStartPos;     // selected entity pos at grab (TRANSLATE)
    float         dragStartYaw;     // selected prop's yaw (deg) at grab (ROTATE)
    float         dragStartScale;   // selected prop's scale at grab (SCALE)
    uint32_t      dragTag;          // coalesce token for this drag

    EdProxy       proxies[ED_MAX_ENTS];
    int           proxyCount;

    // ---- viewport frame (set by EdScene_UpdateViewport each frame) ---------
    // Coordinates are RELATIVE to the viewport rect's top-left, and vpW/vpH are
    // the viewport size — so picking and the gizmo match the render-to-texture
    // image regardless of where the viewport sits in the window.
    Vector2       vpMouse;
    int           vpW, vpH;
    RenderTexture2D vpTex;          // lazily (re)created to vpW×vpH
    bool          vpTexValid;

    // ---- persisted settings (editor.cfg) -----------------------------------
    float         camFlySpeed, camFlyBoost, camLookSens, camOrbitSens, camZoomSpeed;
    float         viewFov;
    EdViewMode    viewDefault;
    float         viewOrthoH;
    float         gridSpacing;
    int           gridSlices;
    bool          snapEnabled;
    float         snapStep;
    bool          barricadeAutoSpawn;
    int           undoDepth;
    float         uiScale;          // shell font/layout scale
    int           winW, winH;
    bool          vsync;
    int           fpsCap;

    EngCfg       *cfg;              // editor.cfg handle (retained by LoadSettings)
} EdScene;

// ---- lifecycle -------------------------------------------------------------

// Load editor.cfg into the scene's settings fields (call BEFORE the window so
// window/vsync/fps + default view take effect this launch). `cfg` is retained
// by reference for the matching Save.
void EdScene_LoadSettings(EdScene *s, EngCfg *cfg);
void EdScene_SaveSettings(EdScene *s, EngCfg *cfg);

// Parse the map at s->path and prime the camera/history/selection. Settings
// must already be loaded (they drive history depth, default view, zoom, scale).
void EdScene_Init(EdScene *s);
void EdScene_Shutdown(EdScene *s);   // frees history + viewport texture

// Scan data/mobs/*/*.mob into s->mobDefs (called by EdScene_Init). Falls back
// to a single built-in "ZOMBIE" entry when no catalog is found, so the place
// palette always has at least one mob tool.
void EdScene_ScanMobs(EdScene *s);

// Scan content catalogs into the matching def lists (all called by EdScene_Init).
// Props/perks have no built-in fallback — an empty catalog = an empty section.
void EdScene_ScanProps(EdScene *s);    // props/*.prop   → propDefs
void EdScene_ScanPerks(EdScene *s);    // perks/*.perk   → perkDefs
void EdScene_ScanWeapons(EdScene *s);  // weapons/*.weapon → weaponDefs (wallbuys)

// Re-run every content scan (mobs/props/perks/weapons + the asset index) against
// the CURRENT content roots. Call after the active game root changes mid-session
// (Open Game / New Game) so the placement palette + asset browser reflect the new
// game's catalogs — EdScene_Init scans once at startup, but switching games does
// not re-init the scene.
void EdScene_RescanContent(EdScene *s);

// Rebuild the proxy list from the document (cheap; called each frame).
void EdScene_RebuildProxies(EdScene *s);

// ---- per-frame viewport ----------------------------------------------------

// Drive camera + editing interaction for this frame. `vp` is the viewport rect
// in window pixels; `inputAllowed` is false when a menu/overlay owns the mouse
// or the cursor is outside the viewport (suppresses world clicks/hotkeys, but
// global undo/redo + UI-scale keys are handled in the shell, not here).
void EdScene_UpdateViewport(EdScene *s, Rectangle vp, bool inputAllowed);

// Render the 3D scene into its render texture and blit it into `vp`.
void EdScene_DrawViewport(EdScene *s, Rectangle vp);

// ---- verbs (used by the shell, menus, and panels) --------------------------

void EdScene_Commit(EdScene *s);             // push one undo step + mark dirty
void EdScene_Undo(EdScene *s);
void EdScene_Redo(EdScene *s);
void EdScene_DeleteSelected(EdScene *s);
void EdScene_SwitchView(EdScene *s, EdViewMode m);
void EdScene_CycleSelected(EdScene *s);      // R: retag spawn / rotate window facing
bool EdScene_Save(EdScene *s);               // MapDoc_Save → s->path; clears dirty
bool EdScene_Open(EdScene *s, const char *path);  // load a different map (resets history)
void EdScene_New(EdScene *s);                // reset to a fresh empty document
bool EdScene_SaveAs(EdScene *s, const char *path); // set path then Save; returns success

const char *EdScene_KindName(EngMapEntKind k);

#endif // SHOOTER_EDSCENE_H
