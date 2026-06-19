// ============================================================================
//  edhost.c — implementation of the editor shell (the IDE frame).
//  See edhost.h for the architecture; edscene.{c,h} for the document/viewport.
// ============================================================================

#include "edhost.h"
#include "edscene.h"

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"

#include <dlfcn.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- capacities ------------------------------------------------------------
#define ED_MAX_MENUS        8
#define ED_MAX_MENU_ITEMS  32
#define ED_MAX_PANELS      32
#define ED_MAX_STATUS       8
#define ED_MAX_PLUGINS     32
#define ED_LOG_LINES      256
#define ED_LOG_LEN        180

// ---- base (unscaled) metrics -----------------------------------------------
#define ED_MENUBAR_H   24
#define ED_STATUS_H    20
#define ED_SPLITTER    6
#define ED_PANEL_HDR   20
#define ED_DROPDOWN_W  220
#define ED_ITEM_H      22

typedef struct {
    char         label[24];
    EdMenuItem   items[ED_MAX_MENU_ITEMS];
    int          itemCount;
    EdMenuDynFn  dynFn;      // optional dynamic-item provider (called per frame when open)
    void        *dynUser;
} EdMenu;

struct EdHost {
    EdScene *scene;

    EdMenu   menus[ED_MAX_MENUS];
    int      menuCount;
    int      openMenu;          // -1 = none
    int      openSubmenu;       // item index of the open flyout's parent, -1 = none

    EdPanel  panels[ED_MAX_PANELS];
    int      panelCount;

    struct { EdStatusFn fn; void *user; } status[ED_MAX_STATUS];
    int      statusCount;

    // dock-zone sizes in UNSCALED px (multiplied by uiScale at layout time).
    float    leftW, rightW, bottomH;
    int      splitterDrag;      // 0 none, 1 left, 2 right, 3 bottom
    bool     vpActive;          // a viewport drag is in progress (started in vp)

    EdModalFn modalFn;
    void     *modalUser;

    // console ring buffer
    char       log[ED_LOG_LINES][ED_LOG_LEN];
    EdLogLevel logLvl[ED_LOG_LINES];
    int        logHead, logCount;

    // dynamic plugin handles (for dlclose at teardown)
    void    *dl[ED_MAX_PLUGINS];
    int      dlCount;
};

// ---- registration ----------------------------------------------------------

static EdMenu *FindOrAddMenu(EdHost *h, const char *name) {
    for (int i = 0; i < h->menuCount; i++)
        if (strcmp(h->menus[i].label, name) == 0) return &h->menus[i];
    if (h->menuCount >= ED_MAX_MENUS) return NULL;
    EdMenu *m = &h->menus[h->menuCount++];
    snprintf(m->label, sizeof m->label, "%s", name);
    m->itemCount = 0;
    return m;
}

void EdHost_AddMenuItem(EdHost *h, const EdMenuItem *item) {
    EdMenu *m = FindOrAddMenu(h, item->menu);
    if (!m || m->itemCount >= ED_MAX_MENU_ITEMS) return;
    m->items[m->itemCount++] = *item;
}

void EdHost_AddMenuSeparator(EdHost *h, const char *menu) {
    EdMenuItem sep = { .menu = menu, .label = NULL };
    EdHost_AddMenuItem(h, &sep);
}

void EdHost_SetMenuDynamic(EdHost *h, const char *menu, EdMenuDynFn fn, void *user) {
    EdMenu *m = FindOrAddMenu(h, menu);
    if (!m) return;
    m->dynFn   = fn;
    m->dynUser = user;
}

void EdHost_AddPanel(EdHost *h, const EdPanel *panel) {
    if (h->panelCount >= ED_MAX_PANELS) return;
    h->panels[h->panelCount++] = *panel;
}

void EdHost_AddStatusItem(EdHost *h, EdStatusFn fn, void *user) {
    if (h->statusCount >= ED_MAX_STATUS) return;
    h->status[h->statusCount].fn = fn;
    h->status[h->statusCount].user = user;
    h->statusCount++;
}

void EdHost_SetModal(EdHost *h, EdModalFn fn, void *user) {
    h->modalFn = fn; h->modalUser = user;
}
bool EdHost_HasModal(EdHost *h) { return h->modalFn != NULL; }

// ---- console / log ---------------------------------------------------------

void EdHost_Log(EdHost *h, EdLogLevel lvl, const char *fmt, ...) {
    int slot = (h->logHead + h->logCount) % ED_LOG_LINES;
    if (h->logCount < ED_LOG_LINES) h->logCount++;
    else h->logHead = (h->logHead + 1) % ED_LOG_LINES;   // overwrite oldest
    va_list ap; va_start(ap, fmt);
    vsnprintf(h->log[slot], ED_LOG_LEN, fmt, ap);
    va_end(ap);
    h->logLvl[slot] = lvl;
}

int EdHost_LogCount(EdHost *h) { return h->logCount; }
void EdHost_LogClear(EdHost *h) { h->logHead = 0; h->logCount = 0; }

const char *EdHost_LogLine(EdHost *h, int i, EdLogLevel *outLvl) {
    if (i < 0 || i >= h->logCount) return NULL;
    int slot = (h->logHead + i) % ED_LOG_LINES;
    if (outLvl) *outLvl = h->logLvl[slot];
    return h->log[slot];
}

// ---- context accessors -----------------------------------------------------

MapDoc  *EdHost_Doc(EdHost *h)        { return &h->scene->doc; }
int      EdHost_SelectedId(EdHost *h) { return h->scene->selectedId; }
void     EdHost_Select(EdHost *h, int id) { EdScene_SelectClick(h->scene, id, false); }
void     EdHost_CommitEdit(EdHost *h) { EdScene_Commit(h->scene); }
void     EdHost_CommitEditTagged(EdHost *h, uint32_t tag) { EdScene_CommitTagged(h->scene, tag); }
uint32_t EdHost_NewEditTag(EdHost *h) { return EdScene_NextTag(h->scene); }
EdScene *EdHost_Scene(EdHost *h)      { return h->scene; }
float    EdHost_UiScale(EdHost *h)    { return h->scene->uiScale; }
bool     EdHost_PanelsInteractive(EdHost *h) {
    return h->modalFn == NULL && h->openMenu < 0 && h->splitterDrag == 0;
}

// ---- lifecycle / plugin loading --------------------------------------------

EdHost *EdHost_Create(EdScene *scene) {
    EdHost *h = calloc(1, sizeof *h);
    h->scene    = scene;
    h->openMenu = -1;
    h->openSubmenu = -1;
    h->leftW    = 210;
    h->rightW   = 240;
    h->bottomH  = 150;
    return h;
}

void EdHost_Destroy(EdHost *h) {
    if (!h) return;
    for (int i = 0; i < h->dlCount; i++) if (h->dl[i]) dlclose(h->dl[i]);
    free(h);
}

void EdHost_RegisterBuiltin(EdHost *h, const EdPluginDesc *desc) {
    if (!desc || !desc->registerFn) return;
    if (desc->abiVersion != ED_PLUGIN_ABI) {
        EdHost_Log(h, ED_LOG_ERROR, "builtin '%s' ABI %d != %d (skipped)",
                   desc->name ? desc->name : "?", desc->abiVersion, ED_PLUGIN_ABI);
        return;
    }
    desc->registerFn(h);
    EdHost_Log(h, ED_LOG_INFO, "loaded builtin: %s", desc->name ? desc->name : "?");
}

int EdHost_LoadDynamicPlugins(EdHost *h, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcmp(dot, ".so") != 0) continue;
        if (h->dlCount >= ED_MAX_PLUGINS) break;

        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!lib) { EdHost_Log(h, ED_LOG_ERROR, "plugin %s: %s", e->d_name, dlerror()); continue; }

        EdPluginMainFn entry = (EdPluginMainFn)(uintptr_t)dlsym(lib, ED_PLUGIN_ENTRY);
        if (!entry) {
            EdHost_Log(h, ED_LOG_ERROR, "plugin %s: missing %s()", e->d_name, ED_PLUGIN_ENTRY);
            dlclose(lib); continue;
        }
        const EdPluginDesc *desc = entry();
        if (!desc || desc->abiVersion != ED_PLUGIN_ABI || !desc->registerFn) {
            EdHost_Log(h, ED_LOG_ERROR, "plugin %s: bad descriptor / ABI", e->d_name);
            dlclose(lib); continue;
        }
        h->dl[h->dlCount++] = lib;
        desc->registerFn(h);
        EdHost_Log(h, ED_LOG_INFO, "loaded plugin: %s (%s)", desc->name, e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

// ---- layout helpers --------------------------------------------------------

typedef struct {
    Rectangle menuBar, statusBar, left, right, bottom, viewport;
    float     s;   // ui scale
} EdLayout;

static EdLayout ComputeLayout(EdHost *h, int W, int H) {
    float s = h->scene->uiScale;
    EdLayout L = { .s = s };
    float mh = ED_MENUBAR_H * s, sh = ED_STATUS_H * s;
    float lw = h->leftW * s, rw = h->rightW * s, bh = h->bottomH * s;

    // clamp zone sizes to sane fractions of the window
    float maxW = W * 0.4f, maxB = (H - mh - sh) * 0.6f;
    if (lw < 120 * s) lw = 120 * s;
    if (lw > maxW)    lw = maxW;
    if (rw < 120 * s) rw = 120 * s;
    if (rw > maxW)    rw = maxW;
    if (bh < 70 * s)  bh = 70 * s;
    if (bh > maxB)    bh = maxB;

    float midTop = mh, midBot = H - sh;
    L.menuBar   = (Rectangle){ 0, 0, (float)W, mh };
    L.statusBar = (Rectangle){ 0, (float)H - sh, (float)W, sh };
    L.left      = (Rectangle){ 0, midTop, lw, midBot - midTop };
    L.right     = (Rectangle){ (float)W - rw, midTop, rw, midBot - midTop };
    float cx = lw, cw = (float)W - lw - rw;
    L.bottom    = (Rectangle){ cx, midBot - bh, cw, bh };
    L.viewport  = (Rectangle){ cx, midTop, cw, (midBot - bh) - midTop };
    return L;
}

// Splitter rects sit just inside each zone boundary, over the viewport edge.
static void SplitterRects(const EdLayout *L, Rectangle *sl, Rectangle *sr, Rectangle *sb) {
    float w = ED_SPLITTER * L->s;
    *sl = (Rectangle){ L->left.x + L->left.width - w * 0.5f, L->viewport.y, w, L->viewport.height + L->bottom.height };
    *sr = (Rectangle){ L->right.x - w * 0.5f, L->viewport.y, w, L->viewport.height + L->bottom.height };
    *sb = (Rectangle){ L->viewport.x, L->bottom.y - w * 0.5f, L->viewport.width, w };
}

static void UpdateSplitters(EdHost *h, const EdLayout *L, int W, int H) {
    Rectangle sl, sr, sb; SplitterRects(L, &sl, &sr, &sb);
    Vector2 m = GetMousePosition();
    float s = L->s;

    if (h->splitterDrag == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if      (CheckCollisionPointRec(m, sl)) h->splitterDrag = 1;
        else if (CheckCollisionPointRec(m, sr)) h->splitterDrag = 2;
        else if (CheckCollisionPointRec(m, sb)) h->splitterDrag = 3;
    }
    if (h->splitterDrag && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) h->splitterDrag = 0;

    if (h->splitterDrag == 1) h->leftW   = m.x / s;
    if (h->splitterDrag == 2) h->rightW  = (W - m.x) / s;
    if (h->splitterDrag == 3) h->bottomH = (H - (ED_STATUS_H * s) - m.y) / s;

    // Cursor hint: EW on vertical splitters (left/right), NS on the bottom one.
    // Checked against drag state first so the hint persists while dragging even
    // if the cursor drifts slightly off the handle rect.
    if (h->splitterDrag == 1 || h->splitterDrag == 2 ||
        (h->splitterDrag == 0 && (CheckCollisionPointRec(m, sl) || CheckCollisionPointRec(m, sr))))
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
    else if (h->splitterDrag == 3 ||
             (h->splitterDrag == 0 && CheckCollisionPointRec(m, sb)))
        SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
    else
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
}

// ---- menu bar (interaction is folded into the draw; runs first, on top) ----

// Draw one dropdown row at [dx,iy,dw,ih]; handles separators (NULL label).
// Returns true when the row is clicked this frame (caller fires onClick +
// closes the menu). *outHot (optional) reports hover. When `arrowLabel` is
// non-NULL the row is a submenu parent: it shows that text + a ▸, ignores the
// item's own fields, and never reports a click.
static bool DrawMenuRow(EdHost *h, const EdMenuItem *it, float dx, float iy,
                        float dw, float ih, float s, Vector2 m, bool click,
                        const char *arrowLabel, bool *outHot) {
    if (outHot) *outHot = false;
    if (!arrowLabel && !it->label) {  // separator
        DrawRectangle((int)(dx + 8 * s), (int)(iy + ih * 0.5f),
                      (int)(dw - 16 * s), 1, (Color){ 70, 76, 90, 255 });
        return false;
    }
    Rectangle ir = { dx, iy, dw, ih };
    bool en  = arrowLabel ? true  : (it->enabled ? it->enabled(h, it->user) : true);
    bool chk = arrowLabel ? false : (it->checked ? it->checked(h, it->user) : false);
    bool hot = en && CheckCollisionPointRec(m, ir);
    if (hot) DrawRectangleRec(ir, (Color){ 48, 54, 66, 255 });
    Color tc = !en ? ENG_UI_DIM : (hot ? ENG_UI_GOLD : ENG_UI_TEXT);
    if (chk) Eng_UiText("\xE2\x9C\x93", dx + 6 * s, iy + (ih - 13 * s) / 2.0f, 13, tc);
    Eng_UiText(arrowLabel ? arrowLabel : it->label, dx + 22 * s, iy + (ih - 13 * s) / 2.0f, 13, tc);
    if (arrowLabel) {
        Eng_UiText("\xE2\x96\xB8", dx + dw - 14 * s, iy + (ih - 13 * s) / 2.0f, 13, tc);  // ▸
    } else if (it->shortcut) {
        float sw = MeasureText(it->shortcut, (int)(12 * s));
        Eng_UiText(it->shortcut, dx + dw - sw - 10 * s, iy + (ih - 12 * s) / 2.0f, 12, ENG_UI_DIM);
    }
    if (outHot) *outHot = hot;
    return !arrowLabel && hot && click;
}

// Returns true if the menu system consumed this frame's click (so the viewport
// must ignore it). Draws the bar + the open dropdown.
static bool DrawMenuBar(EdHost *h, const EdLayout *L) {
    float s = L->s;
    bool consumed = false;
    Vector2 m = GetMousePosition();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangleRec(L->menuBar, (Color){ 30, 34, 42, 255 });
    DrawRectangle(0, (int)L->menuBar.height, (int)L->menuBar.width, 1, (Color){ 60, 66, 80, 255 });

    // top-level titles
    float x = 8 * s;
    Rectangle titleR[ED_MAX_MENUS];
    for (int i = 0; i < h->menuCount; i++) {
        float tw = MeasureText(h->menus[i].label, (int)(14 * s)) + 16 * s;
        titleR[i] = (Rectangle){ x, 0, tw, L->menuBar.height };
        bool hot = CheckCollisionPointRec(m, titleR[i]);
        bool open = (h->openMenu == i);
        if (hot || open)
            DrawRectangleRec(titleR[i], (Color){ 48, 54, 66, 255 });
        Eng_UiText(h->menus[i].label, x + 8 * s, (L->menuBar.height - 14 * s) / 2.0f, 14,
                   open ? ENG_UI_GOLD : ENG_UI_TEXT);
        if (hot && click) { h->openMenu = open ? -1 : i; h->openSubmenu = -1; consumed = true; }
        else if (hot && h->openMenu >= 0 && !open) { h->openMenu = i; h->openSubmenu = -1; }  // hover-switch
        x += tw;
    }

    // open dropdown
    if (h->openMenu >= 0 && h->openMenu < h->menuCount) {
        EdMenu *mn = &h->menus[h->openMenu];
        float dw = ED_DROPDOWN_W * s, ih = ED_ITEM_H * s;
        float dx = titleR[h->openMenu].x, dy = L->menuBar.height;

        // Collect dynamic items (up to 16) from the optional provider.
        EdMenuItem dynItems[16]; int dynCount = 0;
        if (mn->dynFn) dynCount = mn->dynFn(h, dynItems, 16, mn->dynUser);
        if (dynCount < 0) dynCount = 0;
        if (dynCount > 16) dynCount = 16;

        // Count top-level rows: every non-submenu item is one row; each distinct
        // submenu name collapses to a single "<name> ▸" parent row.
        const char *seen[ED_MAX_MENU_ITEMS]; int nSeen = 0;
        int topRows = 0;
        for (int i = 0; i < mn->itemCount; i++) {
            const char *sub = mn->items[i].submenu;
            if (!sub) { topRows++; continue; }
            bool dup = false;
            for (int k = 0; k < nSeen; k++) if (strcmp(seen[k], sub) == 0) { dup = true; break; }
            if (!dup) { seen[nSeen++] = sub; topRows++; }
        }

        int sepRow = (topRows > 0 && dynCount > 0) ? 1 : 0;
        int totalRows = topRows + sepRow + dynCount;
        float dh = totalRows * ih + 8 * s;

        Rectangle box = { dx, dy, dw, dh };
        Eng_UiPanelBg((Rectangle){ dx - 1, dy - 1, dw + 2, dh + 2 }, (Color){ 60, 66, 80, 255 });
        Eng_UiPanelBg(box, (Color){ 28, 32, 40, 255 });

        float iy = dy + 4 * s;

        // --- static top-level rows (submenu children are hidden into flyouts) ---
        const char *openSubName = NULL; float openSubY = 0;
        const char *emitted[ED_MAX_MENU_ITEMS]; int nEmit = 0;
        for (int i = 0; i < mn->itemCount; i++) {
            EdMenuItem *it = &mn->items[i];
            if (it->submenu) {
                bool dup = false;
                for (int k = 0; k < nEmit; k++) if (strcmp(emitted[k], it->submenu) == 0) { dup = true; break; }
                if (dup) continue;            // only the first child emits the parent row
                emitted[nEmit++] = it->submenu;
                bool hot;
                DrawMenuRow(h, it, dx, iy, dw, ih, s, m, click, it->submenu, &hot);
                if (hot) h->openSubmenu = i;  // hovering the parent opens its flyout
                if (h->openSubmenu == i) { openSubName = it->submenu; openSubY = iy; }
                iy += ih;
                continue;
            }
            bool hot;
            if (DrawMenuRow(h, it, dx, iy, dw, ih, s, m, click, NULL, &hot)) {
                if (it->onClick) it->onClick(h, it->user);
                h->openMenu = -1; h->openSubmenu = -1; consumed = true;
            }
            if (hot) h->openSubmenu = -1;     // hovering a normal row closes any flyout
            iy += ih;
        }

        // --- separator + dynamic items ---
        if (sepRow) { DrawMenuRow(h, &(EdMenuItem){0}, dx, iy, dw, ih, s, m, click, NULL, NULL); iy += ih; }
        for (int i = 0; i < dynCount; i++) {
            bool hot;
            if (DrawMenuRow(h, &dynItems[i], dx, iy, dw, ih, s, m, click, NULL, &hot)) {
                if (dynItems[i].onClick) dynItems[i].onClick(h, dynItems[i].user);
                h->openMenu = -1; h->openSubmenu = -1; consumed = true;
            }
            if (hot) h->openSubmenu = -1;
            iy += ih;
        }

        // --- submenu flyout, to the right of its parent row ---
        Rectangle flyBox = { 0, 0, 0, 0 };
        if (openSubName) {
            int childRows = 0;
            for (int i = 0; i < mn->itemCount; i++)
                if (mn->items[i].submenu && strcmp(mn->items[i].submenu, openSubName) == 0) childRows++;
            float fx = dx + dw + 1 * s, fdh = childRows * ih + 8 * s;
            flyBox = (Rectangle){ fx, openSubY, dw, fdh };
            Eng_UiPanelBg((Rectangle){ fx - 1, openSubY - 1, dw + 2, fdh + 2 }, (Color){ 60, 66, 80, 255 });
            Eng_UiPanelBg(flyBox, (Color){ 28, 32, 40, 255 });
            float fy = openSubY + 4 * s;
            for (int i = 0; i < mn->itemCount; i++) {
                EdMenuItem *it = &mn->items[i];
                if (!it->submenu || strcmp(it->submenu, openSubName) != 0) continue;
                if (DrawMenuRow(h, it, fx, fy, dw, ih, s, m, click, NULL, NULL)) {
                    if (it->onClick) it->onClick(h, it->user);
                    h->openMenu = -1; h->openSubmenu = -1; consumed = true;
                }
                fy += ih;
            }
        }

        // click outside the open menu (and any flyout) closes it (consumed)
        if (click && !consumed && !CheckCollisionPointRec(m, box) &&
            (!openSubName || !CheckCollisionPointRec(m, flyBox)) &&
            !CheckCollisionPointRec(m, L->menuBar)) {
            h->openMenu = -1; h->openSubmenu = -1; consumed = true;
        }
    }
    return consumed;
}

// ---- panels ----------------------------------------------------------------

static void DrawZone(EdHost *h, EdDockZone zone, Rectangle area) {
    if (area.width < 2 || area.height < 2) return;
    DrawRectangleRec(area, (Color){ 24, 27, 34, 255 });

    // count panels in this zone
    int idx[ED_MAX_PANELS], n = 0;
    for (int i = 0; i < h->panelCount; i++)
        if (h->panels[i].zone == zone) idx[n++] = i;
    if (n == 0) return;

    float s = h->scene->uiScale;
    float hdr = ED_PANEL_HDR * s;

    // Height allocation: fixed-height panels (prefH>0) keep prefH*scale+header;
    // the rest of the zone is split evenly among the flexible panels. If fixed
    // requests overflow the zone they're scaled down proportionally so nothing
    // bleeds past the zone bounds.
    float panelH[ED_MAX_PANELS];
    float fixedSum = 0; int flexN = 0;
    for (int k = 0; k < n; k++) {
        EdPanel *p = &h->panels[idx[k]];
        if (p->prefH > 0) { panelH[k] = p->prefH * s + hdr; fixedSum += panelH[k]; }
        else              { panelH[k] = -1; flexN++; }
    }
    if (fixedSum > area.height && fixedSum > 0) {
        float shrink = area.height / fixedSum;
        for (int k = 0; k < n; k++) if (panelH[k] > 0) panelH[k] *= shrink;
        fixedSum = area.height;
    }
    float flexH = flexN ? (area.height - fixedSum) / flexN : 0;
    if (flexH < hdr + 8 * s) flexH = hdr + 8 * s;
    for (int k = 0; k < n; k++) if (panelH[k] < 0) panelH[k] = flexH;

    float y = area.y;
    for (int k = 0; k < n; k++) {
        EdPanel *p = &h->panels[idx[k]];
        Rectangle pr = { area.x, y, area.width, panelH[k] };
        y += panelH[k];
        DrawRectangle((int)pr.x, (int)pr.y, (int)pr.width, (int)hdr, (Color){ 34, 39, 49, 255 });
        Eng_UiText(p->title, pr.x + 8 * s, pr.y + (hdr - 12 * s) / 2.0f, 12, ENG_UI_GOLD);
        DrawRectangle((int)pr.x, (int)(pr.y + hdr), (int)pr.width, 1, (Color){ 50, 56, 68, 255 });
        if (k > 0) DrawRectangle((int)pr.x, (int)pr.y, (int)pr.width, 1, (Color){ 50, 56, 68, 255 });
        Rectangle content = { pr.x + 6 * s, pr.y + hdr + 4 * s, pr.width - 12 * s, pr.height - hdr - 8 * s };
        if (content.height > 4 && p->draw) {
            BeginScissorMode((int)content.x, (int)content.y, (int)content.width, (int)content.height);
            p->draw(h, content, p->user);
            EndScissorMode();
        }
    }
}

// ---- status bar ------------------------------------------------------------

static void DrawStatusBar(EdHost *h, const EdLayout *L) {
    float s = L->s;
    DrawRectangleRec(L->statusBar, (Color){ 30, 34, 42, 255 });
    DrawRectangle((int)L->statusBar.x, (int)L->statusBar.y, (int)L->statusBar.width, 1,
                  (Color){ 60, 66, 80, 255 });
    float x = 10 * s;
    for (int i = 0; i < h->statusCount; i++) {
        char buf[160] = {0};
        h->status[i].fn(h, buf, sizeof buf, h->status[i].user);
        if (!buf[0]) continue;
        Eng_UiText(buf, x, L->statusBar.y + (L->statusBar.height - 12 * s) / 2.0f, 12, ENG_UI_TEXT);
        x += MeasureText(buf, (int)(12 * s)) + 12 * s;
        Eng_UiText("|", x - 8 * s, L->statusBar.y + (L->statusBar.height - 12 * s) / 2.0f, 12, ENG_UI_DIM);
    }
}

// ---- frame -----------------------------------------------------------------

void EdHost_Frame(EdHost *h, int W, int H) {
    float s = h->scene->uiScale;
    Eng_UiSetScale(s);
    Eng_UiApplyFont(13);

    EdLayout L = ComputeLayout(h, W, H);

    // Input routing: modal owns everything; otherwise menus, then splitters,
    // then the viewport. Panels are immediate-mode and run during draw, locked
    // out while a modal or menu is up.
    bool modal = h->modalFn != NULL;
    bool menuConsumed = false;

    if (!modal) {
        UpdateSplitters(h, &L, W, H);
        if (h->splitterDrag) L = ComputeLayout(h, W, H);  // reflect new sizes now
    }

    // viewport input gating
    Vector2 m = GetMousePosition();
    bool ptIn = CheckCollisionPointRec(m, L.viewport);
    if (!(IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
          IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)))
        h->vpActive = false;
    bool dropdownOpen = (h->openMenu >= 0);
    bool vpBase = !modal && !h->splitterDrag && !dropdownOpen;
    bool vpInput = vpBase && (ptIn || h->vpActive || h->scene->looking);
    if (vpBase && ptIn && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)))
        h->vpActive = true;

    // global shortcuts (work regardless of focus): undo/redo + save + UI scale
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_Z)) EdScene_Undo(h->scene);
    if (ctrl && IsKeyPressed(KEY_Y)) EdScene_Redo(h->scene);
    // Selection + clipboard verbs (suppressed while a modal owns input).
    if (!modal && ctrl && IsKeyPressed(KEY_A)) EdScene_SelectAll(h->scene);
    if (!modal && ctrl && IsKeyPressed(KEY_C)) EdScene_CopySelection(h->scene);
    if (!modal && ctrl && IsKeyPressed(KEY_X)) EdScene_Cut(h->scene);
    if (!modal && ctrl && IsKeyPressed(KEY_V)) EdScene_Paste(h->scene);
    if (!modal && ctrl && IsKeyPressed(KEY_D)) EdScene_DuplicateSelected(h->scene);
    // Ctrl+S: quick-save (skips dialog if path is known; "untitled" is handled
    //         in builtins.c a_save but we can do a direct save here too — this
    //         only fires when a real path is already set).
    if (ctrl && IsKeyPressed(KEY_S)) {
        if (EdScene_Save(h->scene))
            EdHost_Log(h, ED_LOG_INFO, "saved %s", h->scene->path);
        else
            EdHost_Log(h, ED_LOG_ERROR, "save FAILED: %s", h->scene->path);
    }
    // Ctrl+R: play test — save + launch the game on the current map (detached).
    if (ctrl && IsKeyPressed(KEY_R)) {
        char m[256];
        bool ok = EdScene_PlayTest(h->scene, m, sizeof m);
        EdHost_Log(h, ok ? ED_LOG_INFO : ED_LOG_ERROR, "%s", m);
    }
    if (ctrl && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)))
        h->scene->uiScale = (h->scene->uiScale + 0.1f > 3.0f) ? 3.0f : h->scene->uiScale + 0.1f;
    if (ctrl && (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)))
        h->scene->uiScale = (h->scene->uiScale - 0.1f < 0.7f) ? 0.7f : h->scene->uiScale - 0.1f;
    if (modal && IsKeyPressed(KEY_ESCAPE)) { h->modalFn = NULL; }

    // update + draw the scene viewport (after gating, before chrome)
    EdScene_UpdateViewport(h->scene, L.viewport, vpInput);
    EdScene_DrawViewport(h->scene, L.viewport);

    // panels (locked out while modal / a menu is open so widgets don't fire)
    bool lock = modal || dropdownOpen;
    if (lock) GuiLock();
    DrawZone(h, ED_DOCK_LEFT,   L.left);
    DrawZone(h, ED_DOCK_RIGHT,  L.right);
    DrawZone(h, ED_DOCK_BOTTOM, L.bottom);
    if (lock) GuiUnlock();

    // splitter handles (subtle line; brighter while dragging)
    Rectangle sl, sr, sb; SplitterRects(&L, &sl, &sr, &sb);
    Color spc = (Color){ 50, 56, 68, 255 };
    DrawRectangleRec(sl, h->splitterDrag == 1 ? ENG_UI_GOLD : spc);
    DrawRectangleRec(sr, h->splitterDrag == 2 ? ENG_UI_GOLD : spc);
    DrawRectangleRec(sb, h->splitterDrag == 3 ? ENG_UI_GOLD : spc);

    DrawStatusBar(h, &L);

    // menu bar last (its dropdown floats over panels + viewport)
    menuConsumed = DrawMenuBar(h, &L);
    (void)menuConsumed;

    // modal overlay on top of all
    if (h->modalFn) {
        DrawRectangle(0, 0, W, H, (Color){ 0, 0, 0, 120 });
        h->modalFn(h, (Rectangle){ 0, 0, (float)W, (float)H }, h->modalUser);
    }
}
