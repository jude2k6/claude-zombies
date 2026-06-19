// ============================================================================
//  edpanels.c — Tools palette / Hierarchy / Assets / Console panels + status bar.
//
//  Split out of builtins.c. Contributes two plugins: EdBuiltin_Panels (the
//  docked panels — TOOLS + HIERARCHY left, ASSETS right, CONSOLE bottom) and
//  EdBuiltin_StatusBar (the bottom-bar segments). The Inspector lives in its
//  own file (panel_inspector.c) and is registered here via the PanelInspector
//  declaration in builtins_internal.h; clicking a map row opens it through the
//  unsaved-changes guard (RequestOpenMap, edmenus.c).
// ============================================================================

#include "builtins.h"
#include "builtins_internal.h"
#include "edscene.h"

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>    // tolower, for case-insensitive filter

// ============================================================================
//  Panels: Tools palette (L), Hierarchy (L), Inspector (R), Console (B)
// ============================================================================

// ---- Tools palette ---------------------------------------------------------
// Is a row at [y, y+hh] at least partly inside the panel rect c? Used to skip
// drawing AND input-processing widgets scrolled out of the panel, so a button
// parked off-panel can't be clicked through the menu bar above it.
static bool ToolRowVisible(Rectangle c, float y, float hh) {
    return (y + hh > c.y) && (y < c.y + c.height);
}

// Draw the PLACEMENT tools (Select / Spawns / Geometry / Props / Wallbuys /
// Perks) into a panel's running layout, advancing *yp. These live in their own
// always-visible left-column TOOLS panel (PanelTools below) so placement is never
// hidden behind the asset filter — the single biggest "user will fumble" issue
// from the 2026-06-19 review (TODO item 1).
// Every section is data-driven off the scanned catalogs (mobs/props/perks/
// weapons), so a new catalog file is a new button with no rebuild. `area` is the
// panel content rect; rows scrolled out of it are skipped (ToolRowVisible) so an
// off-panel button can't be clicked. The caller owns the scroll + scissor.
static void DrawPlaceTools(EdHost *h, Rectangle area, float *yp, float sc) {
    EdScene *s = EdHost_Scene(h);
    float X = area.x, W = area.width;
    float rowH = 22 * sc, gap = 25 * sc;   // button height + inter-row stride
    float y = *yp;

    // Select / move is always first.
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Select / move", s->placeTool == ED_PLACE_NONE))
        s->placeTool = ED_PLACE_NONE;
    y += gap;

    // ---- Spawns ------------------------------------------------------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Spawns");
    y += 14 * sc;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Player spawn", s->placeTool == ED_PLACE_PLAYER))
        s->placeTool = ED_PLACE_PLAYER;
    y += gap;
    // One button per mob from the data/mobs catalog (data-driven; see
    // EdScene_ScanMobs). Clicking arms ED_PLACE_MOB with that mob's tag.
    for (int i = 0; i < s->mobDefCount; i++) {
        const EdMobDef *m = &s->mobDefs[i];
        bool active = (s->placeTool == ED_PLACE_MOB && strcmp(s->placeMobId, m->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, m->name, active)) {
            s->placeTool = ED_PLACE_MOB;
            snprintf(s->placeMobId, sizeof s->placeMobId, "%s", m->id);
        }
        y += gap;
    }

    // ---- Geometry ----------------------------------------------------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Geometry");
    y += 14 * sc;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Barricade", s->placeTool == ED_PLACE_BARRICADE))
        s->placeTool = ED_PLACE_BARRICADE;
    y += gap;
    {
        // Wall: label shows "(click 2)" cue when first endpoint is pending.
        const char *wallLabel = (s->placeTool == ED_PLACE_WALL && s->wallPending)
                                ? "Wall  (click 2)" : "Wall  (2-click)";
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, wallLabel, s->placeTool == ED_PLACE_WALL)) {
            s->placeTool = ED_PLACE_WALL;
            s->wallPending = false;   // reset on re-arm so a stale start is cleared
        }
    }
    y += gap;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Obstacle", s->placeTool == ED_PLACE_OBSTACLE))
        s->placeTool = ED_PLACE_OBSTACLE;
    y += gap;
    if (ToolRowVisible(area, y, rowH) &&
        Eng_UiToolButton((Rectangle){ X, y, W, rowH }, "Sector (drag)", s->placeTool == ED_PLACE_SECTOR))
        s->placeTool = ED_PLACE_SECTOR;
    y += gap;

    // ---- Props (data-driven from props/*.prop, like Spawns) ----------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Props");
    y += 14 * sc;
    if (s->propDefCount == 0) {
        if (ToolRowVisible(area, y, rowH))
            GuiLabel((Rectangle){ X, y, W, rowH }, "  (no props/ catalog)");
        y += gap;
    }
    for (int i = 0; i < s->propDefCount; i++) {
        const EdPropDef *pd = &s->propDefs[i];
        bool active = (s->placeTool == ED_PLACE_PROP && strcmp(s->placePropId, pd->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, pd->name, active)) {
            s->placeTool = ED_PLACE_PROP;
            snprintf(s->placePropId, sizeof s->placePropId, "%s", pd->id);
        }
        y += gap;
    }

    // ---- Wallbuys (data-driven from the weapons/*.weapon catalog) ----------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Wallbuys");
    y += 14 * sc;
    if (s->weaponDefCount == 0) {
        if (ToolRowVisible(area, y, rowH)) GuiLabel((Rectangle){ X, y, W, rowH }, "  (no weapons/ catalog)");
        y += gap;
    }
    for (int i = 0; i < s->weaponDefCount; i++) {
        const EdPropDef *wd = &s->weaponDefs[i];
        bool active = (s->placeTool == ED_PLACE_WALLBUY && strcmp(s->placeWeaponId, wd->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, wd->name, active)) {
            s->placeTool = ED_PLACE_WALLBUY;
            snprintf(s->placeWeaponId, sizeof s->placeWeaponId, "%s", wd->id);
        }
        y += gap;
    }

    // ---- Perks (data-driven from the perks/*.perk catalog) -----------------
    if (ToolRowVisible(area, y, 12 * sc)) GuiLabel((Rectangle){ X, y, W, 12 * sc }, "Perks");
    y += 14 * sc;
    if (s->perkDefCount == 0) {
        if (ToolRowVisible(area, y, rowH)) GuiLabel((Rectangle){ X, y, W, rowH }, "  (no perks/ catalog)");
        y += gap;
    }
    for (int i = 0; i < s->perkDefCount; i++) {
        const EdPropDef *kd = &s->perkDefs[i];
        bool active = (s->placeTool == ED_PLACE_PERK && strcmp(s->placePerkId, kd->id) == 0);
        if (ToolRowVisible(area, y, rowH) &&
            Eng_UiToolButton((Rectangle){ X, y, W, rowH }, kd->name, active)) {
            s->placeTool = ED_PLACE_PERK;
            snprintf(s->placePerkId, sizeof s->placePerkId, "%s", kd->id);
        }
        y += gap;
    }

    *yp = y;
}

// ---- Tools palette panel (left dock, always visible) -----------------------
// Wraps DrawPlaceTools with its own scroll + scissor, mirroring PanelAssets'
// browse list. Lives in ED_DOCK_LEFT above HIERARCHY so the placement tools are
// always reachable regardless of the asset filter (TODO item 1).
static float g_toolsScroll = 0;

static void PanelTools(EdHost *h, Rectangle c, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);

    if (CheckCollisionPointRec(GetMousePosition(), c))
        g_toolsScroll -= GetMouseWheelMove() * 22 * sc * 2.0f;
    static float prevTotal = 0;
    float maxScroll = prevTotal - c.height; if (maxScroll < 0) maxScroll = 0;
    if (g_toolsScroll < 0) g_toolsScroll = 0;
    if (g_toolsScroll > maxScroll) g_toolsScroll = maxScroll;

    BeginScissorMode((int)c.x, (int)c.y, (int)c.width, (int)c.height);
    float y = c.y - g_toolsScroll;
    DrawPlaceTools(h, c, &y, sc);
    EndScissorMode();
    prevTotal = (y + g_toolsScroll) - c.y;
}

// ---- Hierarchy — Feature 2: filter input -----------------------------------
static float g_hierScroll = 0;
static char  g_hierFilter[64]  = "";
static bool  g_hierFilterEdit  = false;

static void PanelHierarchy(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    float rowH   = 16 * sc;
    float filterH = 20 * sc;

    // --- filter text box ---
    Rectangle filterRect = { c.x, c.y, c.width, filterH };
    if (GuiTextBox(filterRect, g_hierFilter, sizeof g_hierFilter, g_hierFilterEdit)) {
        g_hierFilterEdit = !g_hierFilterEdit;
    }

    // shift the list area down past the filter box
    Rectangle listArea = { c.x, c.y + filterH + 2 * sc, c.width, c.height - filterH - 2 * sc };

    // Build the filtered index list (stack-allocated, max ED_MAX_ENTS).
    int filtered[ED_MAX_ENTS];
    int nFiltered = 0;
    bool hasFilter = (g_hierFilter[0] != '\0');
    for (int i = 0; i < s->proxyCount; i++) {
        EdProxy *p = &s->proxies[i];
        if (hasFilter) {
            // Build the display string and do a case-insensitive substring check.
            const char *rowStr = TextFormat("#%d %s", p->id, EdScene_KindName(p->kind));
            // Simple tolower strstr via manual scan.
            const char *haystack = rowStr;
            const char *needle   = g_hierFilter;
            bool found = false;
            for (int hi = 0; haystack[hi]; hi++) {
                int ni = 0;
                while (needle[ni] && tolower((unsigned char)haystack[hi + ni]) == tolower((unsigned char)needle[ni]))
                    ni++;
                if (!needle[ni]) { found = true; break; }
            }
            if (!found) continue;
        }
        filtered[nFiltered++] = i;
    }

    float total = nFiltered * rowH;
    if (CheckCollisionPointRec(GetMousePosition(), listArea))
        g_hierScroll -= GetMouseWheelMove() * rowH * 3.0f;
    float maxScroll = total - listArea.height; if (maxScroll < 0) maxScroll = 0;
    if (g_hierScroll < 0) g_hierScroll = 0;
    if (g_hierScroll > maxScroll) g_hierScroll = maxScroll;

    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    bool interactive = EdHost_PanelsInteractive(h);
    for (int fi = 0; fi < nFiltered; fi++) {
        float ry = listArea.y - g_hierScroll + fi * rowH;
        if (ry + rowH < listArea.y || ry > listArea.y + listArea.height) continue;
        EdProxy *p = &s->proxies[filtered[fi]];
        Rectangle row = { listArea.x, ry, listArea.width, rowH };
        bool sel = EdScene_IsSelected(s, p->id);
        bool hot = CheckCollisionPointRec(GetMousePosition(), row);
        if (sel)      DrawRectangleRec(row, (Color){ 60, 70, 90, 255 });
        else if (hot) DrawRectangleRec(row, (Color){ 40, 46, 56, 255 });
        Color dot = p->col; dot.a = 255;
        DrawRectangle((int)(listArea.x + 2 * sc), (int)(ry + 4 * sc), (int)(8 * sc), (int)(8 * sc), dot);
        Eng_UiText(TextFormat("#%d %s", p->id, EdScene_KindName(p->kind)),
                   listArea.x + 14 * sc, ry + 1 * sc, 11, sel ? ENG_UI_GOLD : ENG_UI_TEXT);
        if (interactive && hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            EdScene_SelectClick(s, p->id, shift);
        }
    }
    EndScissorMode();
    if (nFiltered == 0) {
        const char *msg = (s->proxyCount == 0) ? "(empty map)" : "(no match)";
        Eng_UiText(msg, listArea.x + 4 * sc, listArea.y + 2 * sc, 11, ENG_UI_DIM);
    }
}

// ---- Assets panel — browse maps --------------------------------------------
// A scrollable index of the maps in the content overlay (EdScene.assets) — click
// one to open it (via the unsaved-changes guard). The filter box narrows the
// list. Trimmed to maps-only: the old Models/Textures browse lists were
// click-to-log dead weight, and placement props come from the TOOLS palette, not
// from arming a raw model stem. This panel is the seam for the planned
// asset-import browser (games-as-projects); thumbnails return with that work.
static float g_assetsScroll = 0;
static char  g_assetFilter[64] = "";
static bool  g_assetFilterEdit = false;

// Case-insensitive substring test (empty needle always matches).
static bool ContainsCI(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

// One asset row: a colour swatch + name, hover/click. Returns true if the row
// was left-clicked this frame. (Thumbnail support lived here for the old
// Models/Textures browse lists; it returns with the asset-import work.)
static bool AssetRow(EdHost *h, Rectangle area, float y, float rowH, float sc,
                     const char *name, Color tint) {
    Rectangle row = { area.x, y, area.width, rowH };
    bool hot = CheckCollisionPointRec(GetMousePosition(), row);
    if (hot) DrawRectangleRec(row, (Color){ 40, 46, 56, 255 });
    float th = rowH - 4 * sc;
    Rectangle dst = { area.x + 2 * sc, y + 2 * sc, th, th };
    DrawRectangleRec(dst, tint);
    Eng_UiText(name, dst.x + th + 6 * sc, y + (rowH - 11 * sc) * 0.5f, 11, ENG_UI_TEXT);
    return hot && EdHost_PanelsInteractive(h) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void PanelAssets(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    const EdAssetIndex *ix = &s->assets;
    float rowH = 30 * sc, labelH = 16 * sc;

    // filter box
    Rectangle filterRect = { c.x, c.y, c.width, 20 * sc };
    if (GuiTextBox(filterRect, g_assetFilter, sizeof g_assetFilter, g_assetFilterEdit))
        g_assetFilterEdit = !g_assetFilterEdit;
    Rectangle area = { c.x, c.y + 22 * sc, c.width, c.height - 22 * sc };

    if (CheckCollisionPointRec(GetMousePosition(), area))
        g_assetsScroll -= GetMouseWheelMove() * rowH * 2.0f;
    static float prevTotal = 0;
    float maxScroll = prevTotal - area.height; if (maxScroll < 0) maxScroll = 0;
    if (g_assetsScroll < 0) g_assetsScroll = 0;
    if (g_assetsScroll > maxScroll) g_assetsScroll = maxScroll;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    float y = area.y - g_assetsScroll;
    #define ROW_VIS(hh) ((y + (hh) > area.y) && (y < area.y + area.height))

    // ---- Maps (click to open) ----
    if (ROW_VIS(labelH)) GuiLabel((Rectangle){ area.x, y, area.width, labelH }, "Maps");
    y += labelH;
    for (int i = 0; i < ix->mapCount; i++) {
        if (!ContainsCI(ix->maps[i].name, g_assetFilter)) continue;
        if (ROW_VIS(rowH) && AssetRow(h, area, y, rowH, sc,
                                      ix->maps[i].name, (Color){ 90, 110, 150, 255 }))
            RequestOpenMap(h, ix->maps[i].path);
        y += rowH;
    }

    #undef ROW_VIS
    EndScissorMode();
    prevTotal = (y + g_assetsScroll) - area.y;
}

// ---- Console ---------------------------------------------------------------
// Minimum severity shown: 0 = all, 1 = warnings+errors, 2 = errors only.
// Maps directly onto EdLogLevel (INFO<WARN<ERROR), so the test is a >= compare.
static int g_conMinLvl = 0;

static void PanelConsole(EdHost *h, Rectangle c, void *u) {
    (void)u;
    float sc = EdHost_UiScale(h);

    // Header: Clear + a severity-filter cycle button (audit P1-F). The host
    // GuiLock()s panels while a menu/modal is open, so these can't misfire.
    float btnH = 18 * sc;
    if (GuiButton((Rectangle){ c.x, c.y, 56 * sc, btnH }, "Clear"))
        EdHost_LogClear(h);
    const char *fl = g_conMinLvl == 0 ? "Show: all"
                   : g_conMinLvl == 1 ? "Show: warn+" : "Show: errors";
    if (GuiButton((Rectangle){ c.x + 60 * sc, c.y, 96 * sc, btnH }, fl))
        g_conMinLvl = (g_conMinLvl + 1) % 3;

    Rectangle list = { c.x, c.y + btnH + 2 * sc, c.width, c.height - btnH - 2 * sc };
    float rowH = 14 * sc;
    int   total = EdHost_LogCount(h);
    int   fit = (int)(list.height / rowH); if (fit < 1) fit = 1;

    // Count lines passing the filter, then skip all but the last `fit` of them
    // so the newest stays in view.
    int passing = 0;
    for (int i = 0; i < total; i++) {
        EdLogLevel lvl; EdHost_LogLine(h, i, &lvl);
        if ((int)lvl >= g_conMinLvl) passing++;
    }
    int skip = passing - fit; if (skip < 0) skip = 0;

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    float y = list.y;
    int seen = 0;
    for (int i = 0; i < total; i++) {
        EdLogLevel lvl; const char *line = EdHost_LogLine(h, i, &lvl);
        if ((int)lvl < g_conMinLvl) continue;
        if (seen++ < skip) continue;
        Color col = lvl == ED_LOG_ERROR ? (Color){ 235, 110, 110, 255 }
                  : lvl == ED_LOG_WARN  ? ENG_UI_GOLD : ENG_UI_TEXT;
        Eng_UiText(line, list.x + 2 * sc, y, 11, col);
        y += rowH;
    }
    EndScissorMode();
}

static void RegisterPanels(EdHost *h) {
    EdHost_AddPanel(h, &(EdPanel){ .id="tools",     .title="TOOLS",     .zone=ED_DOCK_LEFT,   .draw=PanelTools });
    EdHost_AddPanel(h, &(EdPanel){ .id="hierarchy", .title="HIERARCHY", .zone=ED_DOCK_LEFT,   .draw=PanelHierarchy });
    EdHost_AddPanel(h, &(EdPanel){ .id="inspector", .title="INSPECTOR", .zone=ED_DOCK_RIGHT,  .draw=PanelInspector });
    EdHost_AddPanel(h, &(EdPanel){ .id="assets",    .title="ASSETS",    .zone=ED_DOCK_RIGHT,  .draw=PanelAssets });
    EdHost_AddPanel(h, &(EdPanel){ .id="console",   .title="CONSOLE",   .zone=ED_DOCK_BOTTOM, .draw=PanelConsole });
}

// ============================================================================
//  Status bar
// ============================================================================

static void st_map(EdHost *h, char *out, int cap, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    snprintf(out, cap, "%s%s", GetFileName(s->path), s->dirty ? " *" : "");
}
static void st_count(EdHost *h, char *out, int cap, void *u) {
    (void)u; snprintf(out, cap, "%d ents", EdHost_Scene(h)->proxyCount);
}
static void st_view(EdHost *h, char *out, int cap, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    const char *v = s->view == ED_VIEW_FLY ? "fly" : s->view == ED_VIEW_ORBIT ? "iso" : "top";
    // Gizmo mode moved out of the Tools panel (audit P0-B); surface it read-only
    // here so the active transform mode (keys 1/2/3) stays discoverable.
    const char *g = s->mode == ENG_GIZMO_TRANSLATE ? "move"
                  : s->mode == ENG_GIZMO_ROTATE    ? "rotate" : "scale";
    // Flag a rotate/scale mode that can't act on the current selection so the
    // user sees why no gizmo appears, instead of a silent no-op (TODO item 2).
    const char *na = (s->selectedId >= 0 && !EdScene_GizmoModeApplies(s)) ? " (n/a)" : "";
    snprintf(out, cap, "view: %s  %s%s%s%s", v, g, na,
             s->snapEnabled ? "  snap" : "", s->gridVisible ? "" : "  no-grid");
}
static void st_sel(EdHost *h, char *out, int cap, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    if (s->selectedId < 0) { out[0] = '\0'; return; }   // no dead "no selection" text (audit P1-C)
    EngMapEntKind k; EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    int n = EdScene_SelCount(s);
    if (n > 1) snprintf(out, cap, "sel #%d %s (+%d)", s->selectedId, EdScene_KindName(k), n - 1);
    else       snprintf(out, cap, "sel #%d %s", s->selectedId, EdScene_KindName(k));
}

static void RegisterStatusBar(EdHost *h) {
    EdHost_AddStatusItem(h, st_map,   NULL);
    EdHost_AddStatusItem(h, st_count, NULL);
    EdHost_AddStatusItem(h, st_view,  NULL);
    EdHost_AddStatusItem(h, st_sel,   NULL);
}

// ============================================================================
//  Descriptors
// ============================================================================

const EdPluginDesc *EdBuiltin_Panels(void) {
    static const EdPluginDesc d = { "panels", ED_PLUGIN_ABI, RegisterPanels }; return &d;
}
const EdPluginDesc *EdBuiltin_StatusBar(void) {
    static const EdPluginDesc d = { "statusbar", ED_PLUGIN_ABI, RegisterStatusBar }; return &d;
}
