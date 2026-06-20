// ============================================================================
//  edsettings.c — EdScene persisted settings (editor.cfg) load/save.
//
//  Split out of edscene.c: the cam/view/grid/edit/ui/win keys are the editor's
//  preferences, independent of the document and viewport. EdScene_PutSettingKeys
//  is the single source of truth for those keys — both writers (here and the
//  recents writer in ed_recents.c) call it, so adding a setting is a one-line
//  change in one place instead of a drift trap.
// ============================================================================

#include "edscene.h"

#include <stdio.h>
#include <string.h>

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
    // Gizmo handle scale multiplier (1.0 = engine default; 1.4 = larger, more
    // clickable handles).  Clamped to [0.5, 4.0] so extreme values don't break
    // the viewport.  A Settings-dialog slider is a follow-up (edmenus.c).
    s->gizmoScale         = EngCfg_Float(cfg, "edit.gizmoScale", 1.4f);
    if (s->gizmoScale < 0.5f) s->gizmoScale = 0.5f;
    if (s->gizmoScale > 4.0f) s->gizmoScale = 4.0f;
    // gizmoHidden is a transient screenshot aid — always starts off at launch.
    s->gizmoHidden        = false;
    s->uiScale            = EngCfg_Float(cfg, "ui.scale",       0.0f);  // 0 = use recommendation
    s->winW               = EngCfg_Int  (cfg, "win.width",      1280);
    s->winH               = EngCfg_Int  (cfg, "win.height",     800);
    s->vsync              = EngCfg_Bool (cfg, "win.vsync",      true);
    s->fpsCap             = EngCfg_Int  (cfg, "win.fpsCap",     60);

    if (s->viewDefault < ED_VIEW_FLY || s->viewDefault > ED_VIEW_SIDE) s->viewDefault = ED_VIEW_ORBIT;
    if (s->undoDepth < 1) s->undoDepth = ENGMAPHISTORY_DEFAULT_DEPTH;

    // Recently-used placeables (content browser "Recent" category).
    s->recentCount = EngCfg_Int(cfg, "place.recentCount", 0);
    if (s->recentCount < 0) s->recentCount = 0;
    if (s->recentCount > ED_MAX_RECENTS) s->recentCount = ED_MAX_RECENTS;
    for (int i = 0; i < s->recentCount; i++) {
        char k[48];
        snprintf(k, sizeof k, "place.recent.%d.tool", i);
        s->recents[i].tool = EngCfg_Int(cfg, k, 0);
        snprintf(k, sizeof k, "place.recent.%d.id", i);
        snprintf(s->recents[i].id, sizeof s->recents[i].id, "%s", EngCfg_Str(cfg, k, ""));
    }

    // Camera bookmarks (9 slots).
    for (int i = 0; i < 9; i++) {
        char k[48];
        snprintf(k, sizeof k, "bookmark.%d.set", i + 1);
        s->bookmarks[i].set = EngCfg_Bool(cfg, k, false);
        if (!s->bookmarks[i].set) continue;
        snprintf(k, sizeof k, "bookmark.%d.x",      i + 1); s->bookmarks[i].x      = EngCfg_Float(cfg, k, 0.0f);
        snprintf(k, sizeof k, "bookmark.%d.y",      i + 1); s->bookmarks[i].y      = EngCfg_Float(cfg, k, 0.0f);
        snprintf(k, sizeof k, "bookmark.%d.z",      i + 1); s->bookmarks[i].z      = EngCfg_Float(cfg, k, 0.0f);
        snprintf(k, sizeof k, "bookmark.%d.yaw",    i + 1); s->bookmarks[i].yaw    = EngCfg_Float(cfg, k, 0.0f);
        snprintf(k, sizeof k, "bookmark.%d.pitch",  i + 1); s->bookmarks[i].pitch  = EngCfg_Float(cfg, k, -0.6f);
        snprintf(k, sizeof k, "bookmark.%d.view",   i + 1); s->bookmarks[i].view   = (EdViewMode)EngCfg_Int(cfg, k, (int)ED_VIEW_ORBIT);
        snprintf(k, sizeof k, "bookmark.%d.orthoH", i + 1); s->bookmarks[i].orthoH = EngCfg_Float(cfg, k, 40.0f);
        // Clamp view to valid range.
        if (s->bookmarks[i].view < ED_VIEW_FLY || s->bookmarks[i].view > ED_VIEW_SIDE)
            s->bookmarks[i].view = ED_VIEW_ORBIT;
    }
}

// Emit every SCENE-OWNED setting key (cam/view/grid/edit/ui/win) into an open
// EngCfg save stream. This is the single source of truth for those keys — both
// writers (EdScene_SaveSettings here and Recents_Save in ed_recents.c) call it,
// so adding a setting is a one-line change in one place instead of a drift trap.
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
    EngCfg_PutFloat(f, "edit.gizmoScale", s->gizmoScale);
    EngCfg_PutFloat(f, "ui.scale",      s->uiScale);
    EngCfg_PutInt  (f, "win.width",     s->winW);
    EngCfg_PutInt  (f, "win.height",    s->winH);
    EngCfg_PutBool (f, "win.vsync",     s->vsync);
    EngCfg_PutInt  (f, "win.fpsCap",    s->fpsCap);

    // Recently-used placeables (content browser "Recent" category).
    EngCfg_PutInt(f, "place.recentCount", s->recentCount);
    for (int i = 0; i < s->recentCount; i++) {
        char k[48];
        snprintf(k, sizeof k, "place.recent.%d.tool", i); EngCfg_PutInt(f, k, s->recents[i].tool);
        snprintf(k, sizeof k, "place.recent.%d.id", i);   EngCfg_PutStr(f, k, s->recents[i].id);
    }

    // Camera bookmarks.
    for (int i = 0; i < 9; i++) {
        char k[48];
        snprintf(k, sizeof k, "bookmark.%d.set", i + 1);
        EngCfg_PutBool(f, k, s->bookmarks[i].set);
        if (!s->bookmarks[i].set) continue;
        snprintf(k, sizeof k, "bookmark.%d.x",      i + 1); EngCfg_PutFloat(f, k, s->bookmarks[i].x);
        snprintf(k, sizeof k, "bookmark.%d.y",      i + 1); EngCfg_PutFloat(f, k, s->bookmarks[i].y);
        snprintf(k, sizeof k, "bookmark.%d.z",      i + 1); EngCfg_PutFloat(f, k, s->bookmarks[i].z);
        snprintf(k, sizeof k, "bookmark.%d.yaw",    i + 1); EngCfg_PutFloat(f, k, s->bookmarks[i].yaw);
        snprintf(k, sizeof k, "bookmark.%d.pitch",  i + 1); EngCfg_PutFloat(f, k, s->bookmarks[i].pitch);
        snprintf(k, sizeof k, "bookmark.%d.view",   i + 1); EngCfg_PutInt  (f, k, (int)s->bookmarks[i].view);
        snprintf(k, sizeof k, "bookmark.%d.orthoH", i + 1); EngCfg_PutFloat(f, k, s->bookmarks[i].orthoH);
    }
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
