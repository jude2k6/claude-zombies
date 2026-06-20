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
#include "edthumb.h"   // EdThumb_Model — GLB thumbnails for the content browser

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>    // tolower, for case-insensitive filter
#include <math.h>     // fmaxf — palette gesture glyphs

// Case-insensitive substring test (defined near PanelAssets); used by the
// content browser's search box too.
static bool ContainsCI(const char *hay, const char *needle);

// ============================================================================
//  Panels: Tools palette (L), Hierarchy (L), Inspector (R), Console (B)
// ============================================================================

// ============================================================================
//  PALETTE — the content browser (Unity-style; editor-ux-review.md §4)
//
//  Replaces the old left-dock TOOLS button-stack. A two-pane bottom-dock panel:
//  a CATEGORY column (Select / Geometry / Spawns / Props / Wallbuys / Perks) on
//  the left, and a wrapping TILE GRID on the right. Asset-backed kinds (mobs,
//  props, wallbuys, perks) show a real GLB thumbnail (edthumb.{c,h}); GEOMETRY
//  primitives show a procedural gesture glyph (no model to preview). Clicking a
//  tile arms the same placeTool state the old palette set — the downstream
//  placement path (edplace.c) is unchanged. A search box filters the grid.
//
//  Thumbnails render off-screen via BeginTextureMode, which honours the active
//  GL scissor — so the grid pre-renders them with the panel scissor LIFTED
//  (EndScissorMode → render → restore), exactly as PanelAssets used to.
// ============================================================================

typedef enum { PAL_GEOMETRY = 0, PAL_SPAWNS, PAL_PROPS, PAL_WALLBUYS, PAL_PERKS, PAL_CATCOUNT } PalCat;
static const char *kPalCatNames[PAL_CATCOUNT] = { "Geometry", "Spawns", "Props", "Wallbuys", "Perks" };

// One resolved tile to draw. `geom` 1..4 = a primitive gesture glyph (no model);
// 0 = an asset/spawn tile (thumbnail `model` if non-empty, else `swatch`).
typedef struct {
    char        label[48];
    const char *model;          // thumbnail path ("" = none)
    Color       swatch;         // fallback square colour
    int         geom;           // 0 none / 1 wall / 2 sector / 3 obstacle / 4 barricade
    EdPlaceTool tool;
    char        id[ED_PROPID_LEN]; // mob/prop/perk/weapon id ("" = n/a)
    bool        active;
} PalTile;

static int  g_palCat = PAL_GEOMETRY;
static float g_palScroll = 0;
static char  g_palFilter[64] = "";
static bool  g_palFilterEdit = false;

// Append one tile (filtered by the search box). A function, not a macro, so the
// (Color){...} compound literals pass as single arguments. Returns the new count.
static int PalEmit(PalTile *out, int n, int max, const char *label,
                   const char *model, Color sw, int geom,
                   EdPlaceTool tool, const char *id, bool active) {
    if (n >= max || !ContainsCI(label, g_palFilter)) return n;
    PalTile *t = &out[n];
    snprintf(t->label, sizeof t->label, "%s", label);
    t->model = model; t->swatch = sw; t->geom = geom;
    t->tool  = tool;  t->active = active;
    snprintf(t->id, sizeof t->id, "%s", id);
    return n + 1;
}

// Build the tile list for the active category, applying the search filter.
static int PaletteBuildTiles(EdScene *s, PalTile out[], int max) {
    int n = 0;
    switch (g_palCat) {
    case PAL_GEOMETRY:
        n = PalEmit(out, n, max, "Wall",      "", (Color){150,150,160,255}, 1, ED_PLACE_WALL,      "", s->placeTool==ED_PLACE_WALL);
        n = PalEmit(out, n, max, "Sector",    "", (Color){ 90,110,150,255}, 2, ED_PLACE_SECTOR,    "", s->placeTool==ED_PLACE_SECTOR);
        n = PalEmit(out, n, max, "Obstacle",  "", (Color){170,140, 90,255}, 3, ED_PLACE_OBSTACLE,  "", s->placeTool==ED_PLACE_OBSTACLE);
        n = PalEmit(out, n, max, "Barricade", "", (Color){200,170, 80,255}, 4, ED_PLACE_BARRICADE, "", s->placeTool==ED_PLACE_BARRICADE);
        break;
    case PAL_SPAWNS:
        n = PalEmit(out, n, max, "Player", "", (Color){80,130,210,255}, 0, ED_PLACE_PLAYER, "", s->placeTool==ED_PLACE_PLAYER);
        for (int i = 0; i < s->mobDefCount; i++) {
            const EdMobDef *m = &s->mobDefs[i];
            n = PalEmit(out, n, max, m->name, m->model, m->tint, 0, ED_PLACE_MOB, m->id,
                        s->placeTool==ED_PLACE_MOB && strcmp(s->placeMobId, m->id)==0);
        }
        break;
    case PAL_PROPS:
        for (int i = 0; i < s->propDefCount; i++) {
            const EdPropDef *pd = &s->propDefs[i];
            n = PalEmit(out, n, max, pd->name, pd->model, (Color){130,130,140,255}, 0, ED_PLACE_PROP, pd->id,
                        s->placeTool==ED_PLACE_PROP && strcmp(s->placePropId, pd->id)==0);
        }
        break;
    case PAL_WALLBUYS:
        for (int i = 0; i < s->weaponDefCount; i++) {
            const EdPropDef *wd = &s->weaponDefs[i];
            n = PalEmit(out, n, max, wd->name, wd->model, (Color){180,150,90,255}, 0, ED_PLACE_WALLBUY, wd->id,
                        s->placeTool==ED_PLACE_WALLBUY && strcmp(s->placeWeaponId, wd->id)==0);
        }
        break;
    case PAL_PERKS:
        for (int i = 0; i < s->perkDefCount; i++) {
            const EdPropDef *kd = &s->perkDefs[i];
            n = PalEmit(out, n, max, kd->name, kd->model, (Color){90,170,120,255}, 0, ED_PLACE_PERK, kd->id,
                        s->placeTool==ED_PLACE_PERK && strcmp(s->placePerkId, kd->id)==0);
        }
        break;
    default: break;
    }
    return n;
}

// Arm the tool a tile represents (mirrors the old DrawPlaceTools click bodies).
static void PaletteArm(EdScene *s, const PalTile *t) {
    s->placeTool = t->tool;
    switch (t->tool) {
    case ED_PLACE_MOB:     snprintf(s->placeMobId,    sizeof s->placeMobId,    "%s", t->id); break;
    case ED_PLACE_PROP:    snprintf(s->placePropId,   sizeof s->placePropId,   "%s", t->id); break;
    case ED_PLACE_PERK:    snprintf(s->placePerkId,   sizeof s->placePerkId,   "%s", t->id); break;
    case ED_PLACE_WALLBUY: snprintf(s->placeWeaponId, sizeof s->placeWeaponId, "%s", t->id); break;
    case ED_PLACE_WALL:    s->wallPending = false; break;   // clear a stale first endpoint
    default: break;
    }
}

// A small procedural glyph for a GEOMETRY primitive (no model to thumbnail).
static void PaletteGeomGlyph(int geom, Rectangle q, Color c) {
    float cx = q.x + q.width * 0.5f, cy = q.y + q.height * 0.5f;
    float r = q.width * 0.32f, th = fmaxf(2.0f, q.width * 0.06f);
    switch (geom) {
    case 1: // Wall: a thick segment between two endpoint dots
        DrawLineEx((Vector2){ cx - r, cy + r*0.5f }, (Vector2){ cx + r, cy - r*0.5f }, th, c);
        DrawCircle((int)(cx - r), (int)(cy + r*0.5f), th, c);
        DrawCircle((int)(cx + r), (int)(cy - r*0.5f), th, c);
        break;
    case 2: // Sector: a footprint outline (drag-rect)
        DrawRectangleLinesEx((Rectangle){ cx - r, cy - r, r*2, r*2 }, th, c);
        break;
    case 3: // Obstacle: a filled box
        DrawRectangleRec((Rectangle){ cx - r, cy - r, r*2, r*2 }, c);
        break;
    case 4: // Barricade: a box with planks
        DrawRectangleLinesEx((Rectangle){ cx - r, cy - r, r*2, r*2 }, th, c);
        DrawLineEx((Vector2){ cx - r, cy - r*0.3f }, (Vector2){ cx + r, cy - r*0.3f }, th, c);
        DrawLineEx((Vector2){ cx - r, cy + r*0.3f }, (Vector2){ cx + r, cy + r*0.3f }, th, c);
        break;
    default: break;
    }
}

static void PanelPalette(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    bool interactive = EdHost_PanelsInteractive(h);

    // ---- layout: category column (left) | search + grid (right) ------------
    float colW = 78 * sc;
    Rectangle colR  = { c.x, c.y, colW, c.height };
    Rectangle rightR= { c.x + colW + 6 * sc, c.y, c.width - colW - 6 * sc, c.height };
    float searchH = 20 * sc;
    Rectangle searchR = { rightR.x, rightR.y, rightR.width, searchH };
    Rectangle gridR = { rightR.x, rightR.y + searchH + 4 * sc,
                        rightR.width, rightR.height - searchH - 4 * sc };

    // ---- build the active category's tiles ---------------------------------
    PalTile tiles[ED_MAX_PROPDEFS + ED_MAX_MOBDEFS + 8];
    int nTiles = PaletteBuildTiles(s, tiles, (int)(sizeof tiles / sizeof tiles[0]));

    // ---- tile grid metrics -------------------------------------------------
    float cellW = 64 * sc, cellH = 78 * sc, pad = 6 * sc;
    int cols = (int)((gridR.width + pad) / cellW); if (cols < 1) cols = 1;
    int nRows = (nTiles + cols - 1) / cols;
    float total = nRows * cellH;

    // ---- pre-render thumbnails with the panel scissor LIFTED ---------------
    // (BeginTextureMode honours the GL scissor; rendering under it yields an
    //  empty texture — same gotcha PanelAssets documented.)
    EndScissorMode();
    for (int i = 0; i < nTiles; i++)
        if (tiles[i].model && tiles[i].model[0]) EdThumb_Model(tiles[i].model, 96);
    BeginScissorMode((int)c.x, (int)c.y, (int)c.width, (int)c.height);

    // ---- category column ---------------------------------------------------
    float by = colR.y, bh = 22 * sc;
    if (Eng_UiToolButton((Rectangle){ colR.x, by, colR.width, bh }, "Select", s->placeTool == ED_PLACE_NONE))
        s->placeTool = ED_PLACE_NONE;
    by += bh + 6 * sc;
    for (int ci = 0; ci < PAL_CATCOUNT; ci++) {
        if (Eng_UiToolButton((Rectangle){ colR.x, by, colR.width, bh }, kPalCatNames[ci], g_palCat == ci))
            g_palCat = ci;
        by += bh + 2 * sc;
    }

    // ---- search box --------------------------------------------------------
    if (GuiTextBox(searchR, g_palFilter, sizeof g_palFilter, g_palFilterEdit))
        g_palFilterEdit = !g_palFilterEdit;

    // ---- tile grid (scrolled + scissored to gridR) -------------------------
    if (CheckCollisionPointRec(GetMousePosition(), gridR))
        g_palScroll -= GetMouseWheelMove() * cellH * 0.5f;
    float maxScroll = total - gridR.height; if (maxScroll < 0) maxScroll = 0;
    if (g_palScroll < 0) g_palScroll = 0;
    if (g_palScroll > maxScroll) g_palScroll = maxScroll;

    BeginScissorMode((int)gridR.x, (int)gridR.y, (int)gridR.width, (int)gridR.height);
    for (int i = 0; i < nTiles; i++) {
        int row = i / cols, col = i % cols;
        float tx = gridR.x + col * cellW;
        float ty = gridR.y - g_palScroll + row * cellH;
        if (ty + cellH < gridR.y || ty > gridR.y + gridR.height) continue;

        Rectangle cell = { tx, ty, cellW - pad, cellH - pad };
        Rectangle q    = { cell.x, cell.y, cell.width, cell.width };   // square image area
        bool hot = CheckCollisionPointRec(GetMousePosition(), cell);

        // background + selection / hover highlight
        DrawRectangleRec(q, (Color){ 30, 34, 42, 255 });
        if (tiles[i].active) DrawRectangleLinesEx(cell, 2 * sc, ENG_UI_GOLD);
        else if (hot)        DrawRectangleLinesEx(cell, 1 * sc, (Color){ 90, 100, 120, 255 });

        // image: thumbnail, gesture glyph, or colour swatch
        Texture2D th = (tiles[i].model && tiles[i].model[0]) ? EdThumb_Model(tiles[i].model, 96) : (Texture2D){0};
        if (th.id) {
            Rectangle src = { 0, 0, (float)th.width, -(float)th.height };  // flip Y (RT)
            DrawTexturePro(th, src, q, (Vector2){0,0}, 0, WHITE);
        } else if (tiles[i].geom) {
            PaletteGeomGlyph(tiles[i].geom, q, tiles[i].swatch);
        } else {
            Rectangle sw = { q.x + q.width*0.28f, q.y + q.height*0.28f, q.width*0.44f, q.height*0.44f };
            DrawRectangleRec(sw, tiles[i].swatch);
        }

        // label
        const char *lbl = tiles[i].label;
        int fw = MeasureText(lbl, (int)(10 * sc));
        Eng_UiText(lbl, cell.x + (cell.width - fw) * 0.5f, q.y + q.height + 2 * sc, 10,
                   tiles[i].active ? ENG_UI_GOLD : ENG_UI_TEXT);

        if (interactive && hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            PaletteArm(s, &tiles[i]);
    }
    EndScissorMode();

    if (nTiles == 0)
        Eng_UiText("(no items)", gridR.x + 4 * sc, gridR.y + 2 * sc, 11, ENG_UI_DIM);
}

// ---- Hierarchy — sector tree (§6 of editor-ux-review.md) ------------------
//
// Two-level tree: each sector is a collapsible parent row; under it sit the
// entities whose sectorId (sector array index) matches the sector's position
// in doc.sectors[].  Collapsed state is tracked per-sector-id in a small flat
// array (g_hierCollapsed / g_hierCollapsedCount).
//
// Descriptor strings per kind:
//   SPAWN   → "PLAYER" or "MOB (tag)"
//   WALL    → "wall" (or "wall [door]" when a door is embedded)
//   WINDOW  → "window dir"
//   OBSTACLE→ "obstacle SxZ"
//   PROP    → "prop name"
//   WALLBUY → "wallbuy weapon"
//   PERK    → "perk name"
//   SECTOR  → rendered as the parent row, not a child
// -------------------------------------------------------------------------

// Collapsed-sector tracking: up to MAPDOC_MAX_SECTORS distinct sector ids.
static int  g_hierCollapsed[MAPDOC_MAX_SECTORS];
static int  g_hierCollapsedCount = 0;

static bool HierIsSectorCollapsed(int sectorId) {
    for (int i = 0; i < g_hierCollapsedCount; i++)
        if (g_hierCollapsed[i] == sectorId) return true;
    return false;
}
static void HierToggleSectorCollapse(int sectorId) {
    for (int i = 0; i < g_hierCollapsedCount; i++) {
        if (g_hierCollapsed[i] == sectorId) {
            // remove by swapping with last
            g_hierCollapsed[i] = g_hierCollapsed[--g_hierCollapsedCount];
            return;
        }
    }
    if (g_hierCollapsedCount < MAPDOC_MAX_SECTORS)
        g_hierCollapsed[g_hierCollapsedCount++] = sectorId;
}

// Build the descriptor label for a non-sector entity proxy.
// Returns a pointer to a TextFormat() buffer (static, valid until next call).
static const char *HierDescriptor(const MapDoc *doc, const EdProxy *p) {
    switch (p->kind) {
    case ENGMAPENT_SPAWN: {
        char mob[MAPDOC_SPAWN_MOB_LEN] = "";
        Eng_GetSpawnMob(doc, p->id, mob, sizeof mob);
        if (strcmp(mob, "PLAYER") == 0) return "PLAYER";
        return TextFormat("MOB (%s)", mob);
    }
    case ENGMAPENT_WALL: {
        // Check if the wall has an embedded door by searching the wall array.
        for (int i = 0; i < doc->wallCount; i++) {
            if (doc->walls[i].id == p->id)
                return doc->walls[i].door.present ? "wall [door]" : "wall";
        }
        return "wall";
    }
    case ENGMAPENT_WINDOW: {
        char dir[8] = "";
        Eng_GetWindowDir(doc, p->id, dir, sizeof dir);
        return TextFormat("window %s", dir);
    }
    case ENGMAPENT_OBSTACLE: {
        float sx = 0, sz = 0, h = 0;
        Eng_GetObstacleSize(doc, p->id, &sx, &sz, &h);
        return TextFormat("obstacle %.0fx%.0f", (double)sx, (double)sz);
    }
    case ENGMAPENT_PROP: {
        for (int i = 0; i < doc->propCount; i++)
            if (doc->props[i].id == p->id) return TextFormat("prop %s", doc->props[i].name);
        return "prop";
    }
    case ENGMAPENT_WALLBUY: {
        for (int i = 0; i < doc->wallbuyCount; i++)
            if (doc->wallbuys[i].id == p->id) return TextFormat("wallbuy %s", doc->wallbuys[i].weapon);
        return "wallbuy";
    }
    case ENGMAPENT_PERK: {
        for (int i = 0; i < doc->perkCount; i++)
            if (doc->perks[i].id == p->id) return TextFormat("perk %s", doc->perks[i].perk);
        return "perk";
    }
    default:
        return EdScene_KindName(p->kind);
    }
}

// Case-insensitive substring search (same logic as ContainsCI).
static bool HierContainsCI(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

static float g_hierScroll = 0;
static char  g_hierFilter[64]  = "";
static bool  g_hierFilterEdit  = false;

static void PanelHierarchy(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    const MapDoc *doc = &s->doc;
    float sc      = EdHost_UiScale(h);
    float rowH    = 16 * sc;
    float filterH = 20 * sc;

    // --- filter text box ---
    Rectangle filterRect = { c.x, c.y, c.width, filterH };
    if (GuiTextBox(filterRect, g_hierFilter, sizeof g_hierFilter, g_hierFilterEdit))
        g_hierFilterEdit = !g_hierFilterEdit;

    // List area below the filter box.
    Rectangle listArea = { c.x, c.y + filterH + 2 * sc,
                           c.width, c.height - filterH - 2 * sc };

    bool hasFilter = (g_hierFilter[0] != '\0');

    // ---- Build the virtual row list ----------------------------------------
    // Each row is one of:
    //   type=0 → sector header row     (sectorIdx = index in doc->sectors[])
    //   type=1 → entity child row      (proxyIdx  = index in s->proxies[])
    // We lay these out ahead of time so we know the total height for scrolling.
    typedef struct { int type; int idx; } HierRow;
    HierRow rows[ED_MAX_ENTS + MAPDOC_MAX_SECTORS];
    int nRows = 0;

    for (int si = 0; si < doc->sectorCount; si++) {
        const MapDocSector *sec = &doc->sectors[si];
        bool collapsed = HierIsSectorCollapsed(sec->id);

        // When filtering: only show a sector row if it has at least one
        // matching child (or if the sector name itself matches).
        bool sectorNameMatch = hasFilter
            ? HierContainsCI(sec->name, g_hierFilter)
            : true;

        // Gather matching children.
        int childProxies[ED_MAX_ENTS];
        int nChildren = 0;
        for (int pi = 0; pi < s->proxyCount; pi++) {
            EdProxy *p = &s->proxies[pi];
            if (p->kind == ENGMAPENT_SECTOR) continue;  // sectors are parent rows
            // sectorId field is an array index matching si.
            int ownerIdx = -1;
            Eng_GetSector(doc, p->id, &ownerIdx);
            if (ownerIdx != si) continue;
            if (hasFilter) {
                // Build search string: "#id descriptor"
                const char *desc = HierDescriptor(doc, p);
                char search[128];
                snprintf(search, sizeof search, "#%d %s", p->id, desc);
                if (!HierContainsCI(search, g_hierFilter)) continue;
            }
            childProxies[nChildren++] = pi;
        }

        // When filtering: skip this sector if neither its name matches nor it
        // has matching children.
        if (hasFilter && !sectorNameMatch && nChildren == 0) continue;

        // Emit sector header row.
        if (nRows < (int)(sizeof rows / sizeof rows[0])) {
            rows[nRows].type = 0;
            rows[nRows].idx  = si;
            nRows++;
        }

        // Emit child rows (only when not collapsed — or when filtering,
        // always expand so matches are visible).
        bool showChildren = (!collapsed || hasFilter);
        if (showChildren) {
            for (int ci = 0; ci < nChildren; ci++) {
                if (nRows < (int)(sizeof rows / sizeof rows[0])) {
                    rows[nRows].type = 1;
                    rows[nRows].idx  = childProxies[ci];
                    nRows++;
                }
            }
        }
    }

    // ---- Scroll ------------------------------------------------------------
    float total = nRows * rowH;
    if (CheckCollisionPointRec(GetMousePosition(), listArea))
        g_hierScroll -= GetMouseWheelMove() * rowH * 3.0f;
    float maxScroll = total - listArea.height; if (maxScroll < 0) maxScroll = 0;
    if (g_hierScroll < 0) g_hierScroll = 0;
    if (g_hierScroll > maxScroll) g_hierScroll = maxScroll;

    // ---- Draw --------------------------------------------------------------
    BeginScissorMode((int)listArea.x, (int)listArea.y,
                     (int)listArea.width, (int)listArea.height);
    bool interactive = EdHost_PanelsInteractive(h);

    // Double-click state (preserved across frames via statics).
    static int    s_lastClickId = -1;
    static double s_lastClickT  = -1.0;

    for (int ri = 0; ri < nRows; ri++) {
        float ry = listArea.y - g_hierScroll + ri * rowH;
        if (ry + rowH < listArea.y || ry > listArea.y + listArea.height) continue;

        Rectangle row = { listArea.x, ry, listArea.width, rowH };
        bool hot = CheckCollisionPointRec(GetMousePosition(), row);

        if (rows[ri].type == 0) {
            // ---- Sector parent row -----------------------------------------
            int si = rows[ri].idx;
            const MapDocSector *sec = &doc->sectors[si];
            bool collapsed = HierIsSectorCollapsed(sec->id) && !hasFilter;

            // Find this sector's proxy id for selection.
            int secProxyId = -1;
            for (int pi = 0; pi < s->proxyCount; pi++) {
                if (s->proxies[pi].kind == ENGMAPENT_SECTOR &&
                    s->proxies[pi].id == sec->id) {
                    secProxyId = sec->id;
                    break;
                }
            }

            bool sel = (secProxyId >= 0) && EdScene_IsSelected(s, secProxyId);
            if (sel)      DrawRectangleRec(row, (Color){ 60, 70, 90, 255 });
            else if (hot) DrawRectangleRec(row, (Color){ 40, 46, 56, 255 });

            // Toggle triangle: ▸/▾ — drawn left of the label.
            float tx = listArea.x + 2 * sc;
            float ty = ry + rowH * 0.5f;
            // Small triangle: 7×7 px scaled.
            float ts = 6 * sc;
            Color triCol = ENG_UI_DIM;
            if (collapsed) {
                // ▸ right-pointing triangle
                Vector2 t0 = { tx,      ty - ts * 0.5f };
                Vector2 t1 = { tx,      ty + ts * 0.5f };
                Vector2 t2 = { tx + ts, ty };
                DrawTriangle(t0, t1, t2, triCol);
            } else {
                // ▾ down-pointing triangle
                Vector2 t0 = { tx,            ty };
                Vector2 t1 = { tx + ts * 0.5f, ty - ts };
                Vector2 t2 = { tx + ts,        ty };
                DrawTriangle(t1, t0, t2, triCol);
            }

            // Sector label: name + id.
            float labelX = tx + ts + 4 * sc;
            const char *label = TextFormat("\"%s\"  (#%d)", sec->name, sec->id);
            Eng_UiText(label, labelX, ry + 1 * sc, 11,
                       sel ? ENG_UI_GOLD : ENG_UI_TEXT);

            if (interactive && hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // Clicking the toggle triangle collapses/expands.
                // Clicking elsewhere selects the sector.
                Rectangle toggleHit = { listArea.x, ry, ts + 6 * sc, rowH };
                bool onToggle = CheckCollisionPointRec(GetMousePosition(), toggleHit);
                if (onToggle) {
                    HierToggleSectorCollapse(sec->id);
                } else {
                    // Select the sector entity.
                    if (secProxyId >= 0) {
                        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                        EdScene_SelectClick(s, secProxyId, shift);
                        // Double-click → frame
                        double now = GetTime();
                        if (secProxyId == s_lastClickId && now - s_lastClickT < 0.35) {
                            EdScene_FrameSelected(s);
                            s_lastClickId = -1;
                        } else {
                            s_lastClickId = secProxyId;
                            s_lastClickT  = now;
                        }
                    }
                }
            }

        } else {
            // ---- Entity child row ------------------------------------------
            EdProxy *p = &s->proxies[rows[ri].idx];
            bool sel = EdScene_IsSelected(s, p->id);
            if (sel)      DrawRectangleRec(row, (Color){ 60, 70, 90, 255 });
            else if (hot) DrawRectangleRec(row, (Color){ 40, 46, 56, 255 });

            // Colour dot (indented by 14 sc to show nesting).
            float indent = 14 * sc;
            Color dot = p->col; dot.a = 255;
            DrawRectangle((int)(listArea.x + indent),
                          (int)(ry + 4 * sc),
                          (int)(7 * sc), (int)(7 * sc), dot);

            // Descriptor label.
            const char *desc = HierDescriptor(doc, p);
            Eng_UiText(desc,
                       listArea.x + indent + 10 * sc, ry + 1 * sc,
                       11, sel ? ENG_UI_GOLD : ENG_UI_TEXT);

            if (interactive && hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                EdScene_SelectClick(s, p->id, shift);
                double now = GetTime();
                if (p->id == s_lastClickId && now - s_lastClickT < 0.35) {
                    EdScene_FrameSelected(s);
                    s_lastClickId = -1;
                } else {
                    s_lastClickId = p->id;
                    s_lastClickT  = now;
                }
            }
        }
    }
    EndScissorMode();

    if (nRows == 0) {
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
    // Left = HIERARCHY only (the placement palette moved to the bottom-dock
    // PALETTE content browser — editor-ux-review.md §4). Right = INSPECTOR +
    // ASSETS (maps). Bottom = PALETTE (primary, flexible) over a short CONSOLE.
    EdHost_AddPanel(h, &(EdPanel){ .id="hierarchy", .title="HIERARCHY", .zone=ED_DOCK_LEFT,   .draw=PanelHierarchy });
    EdHost_AddPanel(h, &(EdPanel){ .id="inspector", .title="INSPECTOR", .zone=ED_DOCK_RIGHT,  .draw=PanelInspector });
    EdHost_AddPanel(h, &(EdPanel){ .id="assets",    .title="ASSETS",    .zone=ED_DOCK_RIGHT,  .draw=PanelAssets });
    EdHost_AddPanel(h, &(EdPanel){ .id="palette",   .title="PALETTE",   .zone=ED_DOCK_BOTTOM, .draw=PanelPalette });
    EdHost_AddPanel(h, &(EdPanel){ .id="console",   .title="CONSOLE",   .zone=ED_DOCK_BOTTOM, .draw=PanelConsole, .prefH=40 });
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
// ---- viewport tool-strip (editor-ux-review.md §5) --------------------------
// A thin band over the top of the viewport carrying the active tool / view /
// gizmo / snap / grid (relocated off the status bar, where they sat far from the
// viewport they describe), plus a "PLACING: …" cue when a tool is armed. The
// view/gizmo/snap/grid chips are clickable; the strip is a viewport overlay
// (EdHost_AddViewportOverlay), so the host reserves its band and the scene never
// gets clicks landing on it.

// One label-sized chip; advances *x. Returns true on a left-click (gated).
static bool StripBtn(float *x, float by, float bh, const char *label,
                     bool active, bool interactive, float sc) {
    float w = MeasureText(label, (int)(12 * sc)) + 16 * sc;
    Rectangle r = { *x, by, w, bh };
    bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = active ? (Color){ 70, 80, 100, 255 }
             : hot    ? (Color){ 48, 54, 66, 255 } : (Color){ 36, 41, 51, 255 };
    DrawRectangleRec(r, bg);
    int fw = MeasureText(label, (int)(12 * sc));
    Eng_UiText(label, r.x + (w - fw) / 2.0f, r.y + (bh - 12 * sc) / 2.0f, 12,
               active ? ENG_UI_GOLD : ENG_UI_TEXT);
    *x += w + 4 * sc;
    return interactive && hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static const char *PlaceToolName(const EdScene *s) {
    switch (s->placeTool) {
    case ED_PLACE_PLAYER:    return "Player spawn";
    case ED_PLACE_MOB:       return TextFormat("spawn %s", s->placeMobId);
    case ED_PLACE_BARRICADE: return "Barricade";
    case ED_PLACE_WALL:      return s->wallPending ? "Wall (click 2 of 2)" : "Wall (click 1 of 2)";
    case ED_PLACE_OBSTACLE:  return "Obstacle";
    case ED_PLACE_PROP:      return TextFormat("prop %s", s->placePropId);
    case ED_PLACE_SECTOR:    return "Sector (drag)";
    case ED_PLACE_WALLBUY:   return TextFormat("wallbuy %s", s->placeWeaponId);
    case ED_PLACE_PERK:      return TextFormat("perk %s", s->placePerkId);
    default:                 return "";
    }
}

static void ToolStrip(EdHost *h, Rectangle strip, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    bool it = EdHost_PanelsInteractive(h);
    float by = strip.y + 3 * sc, bh = strip.height - 6 * sc;
    float x  = strip.x + 8 * sc;

    if (StripBtn(&x, by, bh, "Select", s->placeTool == ED_PLACE_NONE, it, sc))
        s->placeTool = ED_PLACE_NONE;

    const char *vn = s->view == ED_VIEW_FLY ? "view: fly"
                   : s->view == ED_VIEW_ORBIT ? "view: iso" : "view: top";
    if (StripBtn(&x, by, bh, vn, false, it, sc)) {
        EdViewMode nv = s->view == ED_VIEW_ORBIT ? ED_VIEW_TOP
                      : s->view == ED_VIEW_TOP   ? ED_VIEW_FLY : ED_VIEW_ORBIT;
        EdScene_SwitchView(s, nv);
    }

    const char *gn = s->mode == ENG_GIZMO_TRANSLATE ? "move"
                   : s->mode == ENG_GIZMO_ROTATE    ? "rotate" : "scale";
    bool na = (s->selectedId >= 0 && !EdScene_GizmoModeApplies(s));
    if (StripBtn(&x, by, bh, TextFormat("%s%s", gn, na ? " (n/a)" : ""), false, it, sc))
        s->mode = (EngGizmoMode)((s->mode + 1) % 3);

    if (StripBtn(&x, by, bh, "snap", s->snapEnabled, it, sc)) s->snapEnabled = !s->snapEnabled;
    if (StripBtn(&x, by, bh, "grid", s->gridVisible, it, sc)) s->gridVisible = !s->gridVisible;

    // PLACING cue, right-aligned, when a placement tool is armed.
    if (s->placeTool != ED_PLACE_NONE) {
        const char *txt = TextFormat("PLACING: %s", PlaceToolName(s));
        int tw = MeasureText(txt, (int)(12 * sc));
        Eng_UiText(txt, strip.x + strip.width - tw - 10 * sc,
                   strip.y + (strip.height - 12 * sc) / 2.0f, 12, ENG_UI_GOLD);
    }
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
    EdHost_AddStatusItem(h, st_sel,   NULL);
    // view/gizmo/snap/grid moved to the viewport tool-strip (editor-ux-review §5).
    EdHost_AddViewportOverlay(h, ToolStrip, NULL);
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
