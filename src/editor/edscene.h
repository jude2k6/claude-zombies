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
// orthographic editor views pivoting around a focus point. FRONT looks along
// -Z (camera south of focus, level); SIDE looks along -X (camera west of
// focus, level). Both are orthographic, pan/zoom like TOP, no free rotate.
typedef enum { ED_VIEW_FLY = 0, ED_VIEW_ORBIT, ED_VIEW_TOP,
               ED_VIEW_FRONT, ED_VIEW_SIDE } EdViewMode;

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
#define ED_MODELPATH_LEN 256     // resolved .glb path for a thumbnail (may be empty)
typedef struct {
    char  id[ED_MOBID_LEN];   // SPAWN MOB <id> written on placement
    char  name[48];           // palette label
    Color tint;               // marker colour
    char  model[ED_MODELPATH_LEN]; // resolved .glb path for the browser thumbnail ("" = none)
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
    char  model[ED_MODELPATH_LEN]; // resolved .glb path for the browser thumbnail ("" = none)
} EdPropDef;

// A draw/pick proxy: one selectable box per MapDoc entity, tagged by stable id.
typedef struct {
    int           id;
    EngMapEntKind kind;
    BoundingBox   box;
    Color         col;
} EdProxy;

// One clipboard slot: an entity's kind plus a full copy of its struct. The
// union holds whichever MapDoc payload matches `kind`; paste re-adds an entity
// of that kind and copies the payload back (minus the id, which is re-minted).
typedef struct {
    EngMapEntKind kind;
    union {
        MapDocSpawn    spawn;
        MapDocWall     wall;
        MapDocWindow   window;
        MapDocObstacle obstacle;
        MapDocProp     prop;
        MapDocWallbuy  wallbuy;
        MapDocPerk     perk;
        MapDocSector   sector;
    } u;
} EdClipEnt;

typedef struct EdScene {
    char          path[512];
    bool          dirty;            // unsaved edits since last save/open
    float         autosaveAccum;    // seconds of dirty time since last autosave
    long          diskMtime;        // mtime stamp at last Open/Save (live-reload guard)

    // ---- measure / distance tool (T key) -------------------------------------
    // measureMode: T toggles; first LMB sets pointA, second sets pointB and draws
    // the line+label. A third click resets to a new A. Esc exits measure mode.
    bool          measureMode;
    bool          measureHasA;      // point A is set (first click done)
    bool          measureHasB;      // point B is set (second click done, draw seg)
    Vector3       measureA, measureB;  // world positions (ground pick)
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

    // ---- camera bookmarks (Ctrl+1..9 saves, bare 1..9 recalls) ---------------
    // Each slot captures the full camera state; `set` is false until written.
    // Persisted in editor.cfg as bookmark.<n>.{x,y,z,yaw,pitch,view,orthoH,set}.
    struct {
        float      x, y, z;         // focus position
        float      yaw, pitch;
        EdViewMode view;
        float      orthoH;
        bool       set;
    } bookmarks[9];

    // ---- selection / tools -------------------------------------------------
    // selectedId is the PRIMARY (active) selection: gizmo pivot, inspector
    // target, status line. selIds[] is the full multi-selection set and always
    // CONTAINS selectedId when selCount > 0 (selectedId == -1 ⇔ selCount == 0).
    // Group ops (move/delete/duplicate) iterate selIds; single-entity edits use
    // selectedId. Shift-click toggles membership and makes the hit the primary.
    int           selectedId;       // -1 = nothing selected
    int           selIds[ED_MAX_ENTS];
    int           selCount;
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

    // ED_PLACE_SECTOR RECT-drag state: press anchors sectorStart{x,z}, drag
    // tracks sectorCur{x,z} (live preview), release creates the sector.
    bool          sectorDragging;
    float         sectorStartX, sectorStartZ;
    float         sectorCurX, sectorCurZ;

    // Sector edge-handle resize (selected sector only): resizeEdge 0=W/1=E/2=N/3=S,
    // resizeFixed = the opposite edge's world coordinate (held fixed during drag).
    bool          resizing;
    int           resizeEdge;
    float         resizeFixed;

    // Wall endpoint drag (selected wall only): wallVert 0 = endpoint (x1,z1),
    // 1 = endpoint (x2,z2). The translate gizmo still moves the whole wall;
    // these handles move one endpoint, mirroring the sector edge-handle flow.
    bool          wallEditing;
    int           wallVert;

    // Sector vertical height drag (selected sector only): a handle floating above
    // the footprint centre raises/lowers the floor. Reuses the gizmo Y-axis
    // translate constraint (heightDrag) for the drag math.
    bool          heightEditing;
    EngGizmoDrag  heightDrag;
    float         heightStartLow, heightStartHigh;  // sector heights at grab

    // Rubber-band marquee multi-select: a left-drag that begins in empty space
    // (no proxy/gizmo/handle under the cursor) sweeps out a screen rectangle;
    // release selects every entity whose proxy centre projects inside it. A drag
    // shorter than ED_MARQUEE_MIN_PX is treated as a plain empty click instead.
    bool          marqueeActive;    // a marquee drag is in progress
    bool          marqueeMoved;     // moved past the start threshold (click vs drag)
    bool          marqueeAdditive;  // shift at press → add to the set, else replace
    Vector2       marqueeStart;     // viewport-relative press position
    Vector2       marqueeCur;       // viewport-relative current position

    // Ramp link-pick mode: 0 = off, 1 = picking link A, 2 = picking link B. Armed
    // from the Inspector; the next viewport click on a sector sets that link on
    // the selected ramp. The Inspector reads/sets this directly on EdScene.
    int           linkPick;

    // Transient out-of-bounds placement warning (T1-3): set by PlaceAt/SectorAt
    // in edplace.c when a click lands outside every sector footprint and falls
    // back to sector 0. Rendered by the viewport ToolStrip overlay in edpanels.c
    // for 3 seconds (placeWarnUntil is a GetTime()-based expiry). No per-frame
    // tick needed — the overlay polls GetTime() to decide visibility.
    char          placeWarn[96];
    double        placeWarnUntil;

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
    Vector3       dragStartPos;     // PRIMARY entity pos at grab (TRANSLATE pivot)
    Vector3       dragStart[ED_MAX_ENTS]; // per-selected start pos (group translate)
    float         dragStartYaw;     // selected prop's yaw (deg) at grab (ROTATE)
    float         dragStartScale;   // selected prop's scale at grab (SCALE)
    uint32_t      dragTag;          // coalesce token for this drag

    // ---- clipboard (copy/cut → paste) --------------------------------------
    // Snapshots of copied entities (kind + full struct, absolute positions).
    // Survives across edits and does not reference live ids, so cut-then-paste
    // is safe. clipPasteSeq steps each paste so repeated pastes fan out instead
    // of stacking; it resets on every copy/cut.
    EdClipEnt     clip[ED_MAX_ENTS];
    int           clipCount;
    int           clipPasteSeq;

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
    bool          framePending;     // request a "frame all" on the next viewport
                                    // update (set by Open, when vpW/vpH aren't known yet)

    // ---- persisted settings (editor.cfg) -----------------------------------
    float         camFlySpeed, camFlyBoost, camLookSens, camOrbitSens, camZoomSpeed;
    float         viewFov;
    EdViewMode    viewDefault;
    float         viewOrthoH;
    float         gridSpacing;
    int           gridSlices;
    bool          gridVisible;   // draw the grid? (independent of snapEnabled)
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

// Write the scene-owned setting keys (cam/view/grid/edit/ui/win) into an open
// EngCfg save stream. Shared by EdScene_SaveSettings and the panels plugin's
// recents writer so the key list lives in exactly one place. Recents (recent.*
// / game.*) are written by the caller after this, before EngCfg_EndSave.
void EdScene_PutSettingKeys(const EdScene *s, FILE *f);

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

// Fit the camera to ALL entities (or recenter on the origin when the map is
// empty). For ortho views this sets focus + orthoH; for fly it pulls the camera
// back. Needs current proxies + a sized viewport — open-time callers set
// s->framePending instead so it runs once vpW/vpH are known.
void EdScene_FrameAll(EdScene *s);

// Frame the primary selection (zoom+centre on its proxy box). No-op with nothing
// selected. Used by the F key and hierarchy double-click.
void EdScene_FrameSelected(EdScene *s);

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
// Coalescing commit: consecutive commits with the same non-zero tag collapse to
// one undo step (tag 0 = always a fresh step). EdScene_NextTag hands out fresh
// never-zero tags shared across all continuous edits (gizmo + inspector sliders).
void     EdScene_CommitTagged(EdScene *s, uint32_t tag);
uint32_t EdScene_NextTag(EdScene *s);
void EdScene_Undo(EdScene *s);
void EdScene_Redo(EdScene *s);
void EdScene_DeleteSelected(EdScene *s);     // delete the whole selection set

// ---- selection set ---------------------------------------------------------
// Click selection: `additive` (shift-click) toggles `id` in/out of the set and
// makes it the primary; non-additive replaces the set with just `id`. id < 0
// with additive=false clears the selection.
void EdScene_SelectClick(EdScene *s, int id, bool additive);
void EdScene_SelectAll(EdScene *s);          // select every entity in the doc
void EdScene_ClearSelection(EdScene *s);
bool EdScene_IsSelected(const EdScene *s, int id);
int  EdScene_SelCount(const EdScene *s);

// ---- clipboard / duplicate -------------------------------------------------
void EdScene_DuplicateSelected(EdScene *s);  // clone selection in place + nudge, select clones
void EdScene_CopySelection(EdScene *s);      // snapshot selection into the clipboard
void EdScene_Cut(EdScene *s);                // copy then delete the selection
void EdScene_Paste(EdScene *s);              // instantiate the clipboard, select the result
void EdScene_SwitchView(EdScene *s, EdViewMode m);
void EdScene_CycleSelected(EdScene *s);      // R: retag spawn / rotate window facing
bool EdScene_Save(EdScene *s);               // MapDoc_Save → s->path; clears dirty
bool EdScene_Open(EdScene *s, const char *path);  // load a different map (resets history)
void EdScene_New(EdScene *s);                // reset to a fresh empty document
bool EdScene_SaveAs(EdScene *s, const char *path); // set path then Save; returns success
// Save then launch the game on the current map as a detached child (Play Test).
// Writes a result string into msg; returns whether the game launched.
bool EdScene_PlayTest(EdScene *s, char *msg, int cap);

// ---- autosave / crash recovery ---------------------------------------------
// Autosave writes "<path>.autosave" while the document is dirty (every
// ED_AUTOSAVE_SECS of edit time); a clean manual save deletes it. On load, a
// newer "<path>.autosave" than the map itself means the last session crashed
// before saving — the shell offers to restore it (EdScene_RestoreRecovery) or
// throw it away (EdScene_DiscardRecovery).
void EdScene_AutosaveTick(EdScene *s);            // call once per frame
bool EdScene_RecoveryAvailable(const EdScene *s); // newer .autosave than the map?
bool EdScene_RestoreRecovery(EdScene *s);         // load the .autosave, mark dirty, consume it
void EdScene_DiscardRecovery(EdScene *s);         // delete the .autosave

const char *EdScene_KindName(EngMapEntKind k);

// True if the active gizmo mode (s->mode) can act on the current selection.
// TRANSLATE applies to every kind; ROTATE/SCALE only to PROPs. Drives both the
// "don't draw a dead gizmo" suppression and the status-bar "(n/a)" hint.
bool EdScene_GizmoModeApplies(const EdScene *s);

#endif // SHOOTER_EDSCENE_H
