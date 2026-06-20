// ============================================================================
//  panel_inspector.c — the Inspector panel (per-entity + map-properties editor).
//
//  Split out of builtins.c — by far its largest single piece. Draws editable
//  fields for the selected entity (a per-kind ladder) or, when nothing is
//  selected, a quiet "Nothing selected" hint. The map-property editing UI is
//  factored into PanelInspector_DrawMapProps so it can be called from the
//  Map Settings… modal in edmenus.c. Registered as a panel by edpanels.c's
//  RegisterPanels via the PanelInspector declaration in builtins_internal.h.
// ============================================================================

#include "builtins.h"
#include "builtins_internal.h"
#include "edscene.h"

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"

#include <stdio.h>
#include <stdlib.h>   // strtod, for inspector number fields
#include <string.h>
#include <ctype.h>    // isspace, validating numeric input
#include <limits.h>   // INT_MAX, for InspIntBox clamp sentinels

// ============================================================================
//  Inspector helpers — tiny field widgets used by PanelInspector
// ============================================================================

// Draw a float field via a GuiSlider.  Returns true and writes *v if changed.
static bool InspSlider(float X, float W, float *y, float sc,
                       const char *label, float *v, float lo, float hi) {
    float oldV = *v;
    Eng_UiText(label, X, *y + 1 * sc, 12, ENG_UI_TEXT);
    char buf[32]; snprintf(buf, sizeof buf, "%.2f", *v);
    GuiSlider((Rectangle){ X + 60 * sc, *y, W - 100 * sc, 16 * sc }, "", buf, v, lo, hi);
    *y += 22 * sc;
    return (*v != oldV);
}

// Coalesce token for the current continuous edit (slider / colour-picker drag).
// Reset to 0 every frame the mouse is up (see the top of PanelInspector), so it
// holds steady for one drag and is fresh for the next. Module-level rather than a
// function static because a slider's release frame produces no commit — the reset
// has to be driven by the per-frame panel update, not by commit timing.
static uint32_t g_inspContTag = 0;

// Commit a continuous edit so the whole drag collapses to ONE undo step instead
// of one-per-frame. The first commit of a drag claims a tag from the shared
// editor allocator (never colliding with a gizmo drag); the rest coalesce into
// it. (TODO item 4: undo spam from inspector sliders / colour pickers.)
static void InspCommitCont(EdHost *h) {
    if (g_inspContTag == 0) g_inspContTag = EdHost_NewEditTag(h);
    EdHost_CommitEditTagged(h, g_inspContTag);
}

// Float text-box edit-state, addressed by a STABLE STRING KEY (the field label)
// rather than a hand-assigned index. The old design was a fixed 4-slot array
// indexed by a caller-passed idx; pos x/z reused slots 0/1 that obstacle/sector
// size x/z ALSO claimed, so editing one silently clobbered the other's buffer,
// and adding any field risked a fresh collision. Keying by label makes every
// distinct field its own slot — no manual bookkeeping, no collisions. Slots are
// reclaimed (key cleared) whenever the selection changes.
typedef struct {
    const char *key;        // field label; NULL = free slot
    bool        editing;    // is this box in text-entry mode?
    char        buf[32];    // live edit buffer
} InspFloatState;
#define INSP_FLOAT_FIELDS 12  // max distinct float fields shown for one entity
static InspFloatState g_inspFloat[INSP_FLOAT_FIELDS];
static int  g_inspLastId = -1;  // reset buffers when selection changes

// Find (or lazily claim) the edit-state slot for `key`. Keys are string literals
// so pointer identity is stable, but we compare by content to be safe. Returns
// NULL only if the table is full (more than INSP_FLOAT_FIELDS live at once).
static InspFloatState *InspFloatSlot(const char *key) {
    for (int i = 0; i < INSP_FLOAT_FIELDS; i++)
        if (g_inspFloat[i].key && strcmp(g_inspFloat[i].key, key) == 0) return &g_inspFloat[i];
    for (int i = 0; i < INSP_FLOAT_FIELDS; i++)
        if (!g_inspFloat[i].key) {
            g_inspFloat[i].key = key; g_inspFloat[i].editing = false; g_inspFloat[i].buf[0] = '\0';
            return &g_inspFloat[i];
        }
    return NULL;
}

// Draw a float field via a GuiTextBox, keyed by its `label`.
// Returns true and writes *v if the user committed a valid change (toggled out of
// edit). Non-numeric input is REJECTED, not silently coerced: the old `atof`
// turned "abc" into 0.0 and zeroed the field with no feedback. We parse with
// strtod and require the whole buffer to be a number (trailing spaces aside); on
// reject the value is left untouched (the buffer refreshes from it next frame)
// and a warning is logged.
static bool InspFloatBox(EdHost *h, float X, float W, float *y, float sc,
                         const char *label, float *v) {
    InspFloatState *st = InspFloatSlot(label);
    if (!st) return false;
    Eng_UiText(label, X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool changed = false;
    bool wasEdit = st->editing;
    if (!wasEdit) snprintf(st->buf, sizeof st->buf, "%.3f", *v);
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc },
                   st->buf, sizeof st->buf, wasEdit)) {
        st->editing = !wasEdit;
        if (wasEdit) {
            char *end = NULL;
            double parsed = strtod(st->buf, &end);
            bool valid = (end != st->buf);             // consumed at least one char
            while (valid && *end) {                    // only trailing spaces are ok
                if (!isspace((unsigned char)*end)) valid = false;
                end++;
            }
            if (valid) {
                float fv = (float)parsed;
                if (fv != *v) { *v = fv; changed = true; }
            } else if (st->buf[0]) {                   // ignore a silently-cleared box
                EdHost_Log(h, ED_LOG_WARN, "%s: ignored non-numeric input \"%s\"", label, st->buf);
            }
        }
    }
    *y += 22 * sc;
    return changed;
}

// Draw an integer field via a GuiTextBox (keyed by label, same slot table as
// InspFloatBox). Parses and rounds to int; rejects non-numeric input; optionally
// clamps to [min, max] when min <= max.  Returns true and writes *v if changed.
static bool InspIntBox(EdHost *h, float X, float W, float *y, float sc,
                       const char *label, int *v, int clampMin, int clampMax) {
    InspFloatState *st = InspFloatSlot(label);
    if (!st) return false;
    Eng_UiText(label, X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool changed = false;
    bool wasEdit = st->editing;
    if (!wasEdit) snprintf(st->buf, sizeof st->buf, "%d", *v);
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc },
                   st->buf, sizeof st->buf, wasEdit)) {
        st->editing = !wasEdit;
        if (wasEdit) {
            char *end = NULL;
            double parsed = strtod(st->buf, &end);
            bool valid = (end != st->buf);
            while (valid && *end) {
                if (!isspace((unsigned char)*end)) valid = false;
                end++;
            }
            if (valid) {
                int iv = (int)(parsed + 0.5);
                if (clampMin <= clampMax) {
                    if (iv < clampMin) iv = clampMin;
                    if (iv > clampMax) iv = clampMax;
                }
                if (iv != *v) { *v = iv; changed = true; }
            } else if (st->buf[0]) {
                EdHost_Log(h, ED_LOG_WARN, "%s: ignored non-numeric input \"%s\"", label, st->buf);
            }
        }
    }
    *y += 22 * sc;
    return changed;
}

// String text-box edit-state, keyed by field label — mirrors InspFloatState /
// InspFloatSlot but for char-array fields (T2-5). Slots are shared across all
// text fields shown for one entity and released on selection change (same point
// as the float slots). INSP_TEX_BUF_LEN must be >= the largest buffer any caller
// passes (MAPDOC_TEX_NAME_LEN is the largest today).
#define INSP_TEX_BUF_LEN   MAPDOC_TEX_NAME_LEN
#define INSP_TEXT_FIELDS    4   // max distinct text fields shown for one entity
typedef struct {
    const char *key;                    // field label; NULL = free slot
    bool        editing;                // in text-entry mode?
    char        buf[INSP_TEX_BUF_LEN]; // live edit buffer
} InspTextState;
static InspTextState g_inspText[INSP_TEXT_FIELDS];

// Find (or lazily claim) the edit-state slot for `key`. Compared by content for
// safety (same as InspFloatSlot). Returns NULL only if the table is full.
static InspTextState *InspTextSlot(const char *key) {
    for (int i = 0; i < INSP_TEXT_FIELDS; i++)
        if (g_inspText[i].key && strcmp(g_inspText[i].key, key) == 0) return &g_inspText[i];
    for (int i = 0; i < INSP_TEXT_FIELDS; i++)
        if (!g_inspText[i].key) {
            g_inspText[i].key     = key;
            g_inspText[i].editing = false;
            g_inspText[i].buf[0]  = '\0';
            return &g_inspText[i];
        }
    return NULL;
}

// Draw a generic char-array text field keyed by `label`.  Reads/writes into
// `buf` (length `bufLen`).  Commits via EdHost_CommitEdit on edit-toggle if the
// value changed.  Returns true if a change was committed this frame.
static bool InspTextField(EdHost *h, float X, float W, float *y, float sc,
                          const char *label, char *buf, int bufLen) {
    InspTextState *st = InspTextSlot(label);
    if (!st) return false;
    if (!st->editing) snprintf(st->buf, sizeof st->buf, "%s", buf);
    Eng_UiText(label, X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool was = st->editing;
    bool changed = false;
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc },
                   st->buf, (int)sizeof(st->buf) < bufLen ? (int)sizeof(st->buf) : bufLen,
                   was)) {
        st->editing = !was;
        if (was && strcmp(st->buf, buf) != 0) {
            // Bounded copy into the (possibly smaller) destination — explicit so
            // the compiler doesn't flag a %s truncation it can't prove is safe.
            size_t n = strnlen(st->buf, (size_t)bufLen - 1);
            memcpy(buf, st->buf, n); buf[n] = '\0';
            changed = true;
        }
    }
    *y += 22 * sc;
    return changed;
}

// Per-surface texture-override field for WALL / OBSTACLE. Reads/writes the
// MapDoc TEX field via the engine accessors; a blank string clears the override
// so the surface falls back to the map's wall_ext slot. Commits on edit-toggle.
// Now keyed through InspTextSlot (T2-5) to remove the function-static pattern.
static void InspTexField(EdHost *h, EdScene *s, float X, float W, float *y, float sc) {
    InspTextState *st = InspTextSlot("tex");
    if (!st) return;
    char cur[MAPDOC_TEX_NAME_LEN] = "";
    Eng_GetSurfaceTex(&s->doc, s->selectedId, cur, sizeof cur);
    // Sync buffer from the doc whenever we are not actively editing it.
    if (!st->editing) snprintf(st->buf, sizeof st->buf, "%s", cur);
    Eng_UiText("tex", X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool was = st->editing;
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc },
                   st->buf, sizeof st->buf, was)) {
        st->editing = !was;
        if (was && strcmp(st->buf, cur) != 0) {
            Eng_SetSurfaceTex(&s->doc, s->selectedId, st->buf);
            EdHost_CommitEdit(h);
        }
    }
    *y += 20 * sc;
    Eng_UiText("(blank = map default)", X, *y, 9, ENG_UI_DIM);
    *y += 14 * sc;
}

// Collect distinct non-empty door names from walls that have door.present.
// Fills `out` with up to `maxOut` unique names and returns the count.
// Used by the "Locked by" cycle button for spawns and windows.
#define INSP_MAX_DOOR_NAMES 32
static int CollectDoorNames(const MapDoc *d, char out[][MAPDOC_DOOR_NAME_LEN], int maxOut) {
    int count = 0;
    for (int i = 0; i < d->wallCount && count < maxOut; i++) {
        if (!d->walls[i].door.present) continue;
        if (d->walls[i].door.name[0] == '\0') continue;
        // dedup
        bool dup = false;
        for (int j = 0; j < count; j++)
            if (strcmp(out[j], d->walls[i].door.name) == 0) { dup = true; break; }
        if (!dup) snprintf(out[count++], MAPDOC_DOOR_NAME_LEN, "%s", d->walls[i].door.name);
    }
    return count;
}

// Cycle a lockedBy string to the next door name (or back to "" = none).
// Current == "" means "none"; cycles through the collected door names and wraps.
static void CycleLockedBy(const MapDoc *d, char *lockedBy) {
    char names[INSP_MAX_DOOR_NAMES][MAPDOC_DOOR_NAME_LEN];
    int  n = CollectDoorNames(d, names, INSP_MAX_DOOR_NAMES);
    if (n == 0) { lockedBy[0] = '\0'; return; }
    // Find current position
    int cur = -1;  // -1 = currently "<none>"
    for (int i = 0; i < n; i++)
        if (strcmp(lockedBy, names[i]) == 0) { cur = i; break; }
    int next = cur + 1;
    if (next >= n) { lockedBy[0] = '\0'; }  // wrap to none
    else            snprintf(lockedBy, MAPDOC_DOOR_NAME_LEN, "%s", names[next]);
}

// Display name for a sector array index (for ramp link buttons). -1 / OOB =
// "<none>"; unnamed sectors fall back to "sector N". Uses a static buffer, so
// copy the result before the next call.
static const char *SectorLabel(const MapDoc *d, int idx) {
    static char buf[48];
    if (idx < 0 || idx >= d->sectorCount) return "<none>";
    const char *nm = d->sectors[idx].name;
    if (nm && nm[0]) snprintf(buf, sizeof buf, "%s", nm);
    else             snprintf(buf, sizeof buf, "sector %d", idx);
    return buf;
}

// Advance a ramp link to the next candidate sector index, skipping the ramp's
// own index and wrapping through -1 (= none): none → 0 → 1 → … → none.
static int CycleSectorLink(const MapDoc *d, int selfIdx, int cur) {
    int next = cur + 1;
    while (next < d->sectorCount && next == selfIdx) next++;
    return (next >= d->sectorCount) ? -1 : next;
}

// ============================================================================
//  PanelInspector_DrawMapProps — Fix #3: the map-property editor body.
//
//  Called both by the Map Settings… modal in edmenus.c. `area` is the
//  available content rectangle; draws name / atmosphere / textures advancing
//  top-to-bottom. Declared in builtins_internal.h.
// ============================================================================
void PanelInspector_DrawMapProps(EdHost *h, Rectangle area) {
    EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    float X = area.x, W = area.width, y = area.y;

    // Map name (doc.name) — keyed on selectedId == -1 sentinel so the buffer
    // resets after an Open/New (same logic as the old inline block).
    Eng_UiText("Name", X, y + 1 * sc, 12, ENG_UI_TEXT);
    static char g_mapNameBuf[MAPDOC_NAME_LEN] = "";
    static bool g_mapNameEdit = false;
    static int  g_mapNameLastId = -2; // use -2 as "uninitialised sentinel"
    EdScene *ms = EdHost_Scene(h);
    if (g_mapNameLastId != ms->selectedId) {
        g_mapNameLastId = ms->selectedId;
        snprintf(g_mapNameBuf, sizeof g_mapNameBuf, "%s", ms->doc.name);
        g_mapNameEdit = false;
    }
    bool wasNameEdit = g_mapNameEdit;
    if (GuiTextBox((Rectangle){ X + 60 * sc, y, W - 60 * sc, 18 * sc },
                   g_mapNameBuf, sizeof g_mapNameBuf, wasNameEdit)) {
        g_mapNameEdit = !wasNameEdit;
        if (wasNameEdit && strcmp(g_mapNameBuf, s->doc.name) != 0) {
            snprintf(s->doc.name, sizeof s->doc.name, "%s", g_mapNameBuf);
            EdHost_CommitEdit(h);
        }
    }
    y += 22 * sc;

    // ---- Atmosphere -------------------------------------------------------
    MapDocAtmosphere *atm = &s->doc.atmosphere;
    y += 4 * sc;
    Eng_UiText("ATMOSPHERE", X, y, 13, (Color){ 200, 205, 215, 255 }); y += 18 * sc;

    // Fog color — stored as float 0-255 components.
    Color fogCol = { (unsigned char)atm->fogR, (unsigned char)atm->fogG,
                     (unsigned char)atm->fogB, 255 };
    Color newFogCol = fogCol;
    // GuiColorPicker needs a square-ish area; give it a compact block.
    float cpSz = 80 * sc;
    Eng_UiText("Fog color", X, y + 1 * sc, 12, ENG_UI_TEXT);
    GuiColorPicker((Rectangle){ X + 80 * sc, y, cpSz, cpSz }, NULL, &newFogCol);
    if (newFogCol.r != fogCol.r || newFogCol.g != fogCol.g || newFogCol.b != fogCol.b) {
        atm->fogR = (float)newFogCol.r;
        atm->fogG = (float)newFogCol.g;
        atm->fogB = (float)newFogCol.b;
        atm->present = true;
        InspCommitCont(h);
    }
    y += cpSz + 4 * sc;

    // Fog range sliders.
    float oldFogStart = atm->fogStart, oldFogEnd = atm->fogEnd;
    Eng_UiText("Fog start", X, y + 1 * sc, 12, ENG_UI_TEXT);
    char fsBuf[32]; snprintf(fsBuf, sizeof fsBuf, "%.1f", atm->fogStart);
    GuiSlider((Rectangle){ X + 80 * sc, y, W - 120 * sc, 16 * sc }, "", fsBuf, &atm->fogStart, 0, 500);
    y += 22 * sc;
    Eng_UiText("Fog end", X, y + 1 * sc, 12, ENG_UI_TEXT);
    char feBuf[32]; snprintf(feBuf, sizeof feBuf, "%.1f", atm->fogEnd);
    GuiSlider((Rectangle){ X + 80 * sc, y, W - 120 * sc, 16 * sc }, "", feBuf, &atm->fogEnd, 0, 2000);
    y += 22 * sc;
    if (atm->fogStart != oldFogStart || atm->fogEnd != oldFogEnd) {
        atm->present = true;
        InspCommitCont(h);
    }

    // Sky tint (only shown when hasSkyTint or to enable it).
    y += 4 * sc;
    Eng_UiText("Sky tint", X, y + 1 * sc, 12, ENG_UI_TEXT);
    GuiCheckBox((Rectangle){ X + 80 * sc, y, 14 * sc, 14 * sc }, " enable", &atm->hasSkyTint);
    y += 20 * sc;
    if (atm->hasSkyTint) {
        Color skyCol    = { (unsigned char)atm->skyR, (unsigned char)atm->skyG,
                            (unsigned char)atm->skyB, 255 };
        Color newSkyCol = skyCol;
        GuiColorPicker((Rectangle){ X + 80 * sc, y, cpSz, cpSz }, NULL, &newSkyCol);
        if (newSkyCol.r != skyCol.r || newSkyCol.g != skyCol.g || newSkyCol.b != skyCol.b) {
            atm->skyR = (float)newSkyCol.r;
            atm->skyG = (float)newSkyCol.g;
            atm->skyB = (float)newSkyCol.b;
            atm->present = true;
            InspCommitCont(h);
        }
        y += cpSz + 4 * sc;
    }

    // ---- Textures (read-only display) ------------------------------------
    MapDocTextures *tex = &s->doc.textures;
    if (tex->present) {
        y += 4 * sc;
        Eng_UiText("TEXTURES", X, y, 13, (Color){ 200, 205, 215, 255 }); y += 18 * sc;
        const struct { const char *lbl; const char *val; } slots[] = {
            { "floor",    tex->floor    },
            { "ground",   tex->ground   },
            { "wall_ext", tex->wall_ext },
            { "wall_int", tex->wall_int },
            { "ceiling",  tex->ceiling  },
        };
        for (int i = 0; i < 5; i++) {
            Eng_UiText(TextFormat("%s: %s", slots[i].lbl,
                       slots[i].val[0] ? slots[i].val : "(default)"),
                       X, y, 11, ENG_UI_DIM);
            y += 15 * sc;
        }
    }
    (void)y;   // final y not used by caller; suppress unused-variable warning
}

// ---- Inspector — Fix #3: quiet empty state; Fix #4: scrollable fields ------

// Scroll state for the per-entity inspector field list (Fix #4).
static float g_inspScroll = 0;
// Last frame's measured content height (T2-2): used to clamp scroll BEFORE drawing
// so we never clip the last rows. Initialised to a generous estimate; updated every
// frame after drawing so it converges on the real height within one frame.
static float g_inspContentH = 600.0f;

void PanelInspector(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    float X = c.x, W = c.width;

    // Release every float-field slot when the selection changes, so a new entity
    // (possibly a different kind with different labels) starts from fresh buffers.
    if (s->selectedId != g_inspLastId) {
        g_inspLastId = s->selectedId;
        for (int i = 0; i < INSP_FLOAT_FIELDS; i++) g_inspFloat[i].key = NULL;
        for (int i = 0; i < INSP_TEXT_FIELDS;  i++) g_inspText[i].key  = NULL; // T2-5
        g_inspScroll = 0;        // reset scroll when selection changes
        g_inspContentH = 600.0f; // reset height estimate; will re-measure this frame
    }

    // End any continuous-edit coalescing run once the mouse is released, so the
    // next slider/picker drag starts a fresh undo step (see InspCommitCont).
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_inspContTag = 0;

    // ---- Fix #3: nothing selected → quiet hint only -------------------------
    if (s->selectedId < 0) {
        Eng_UiText("Nothing selected", X + 4 * sc, c.y + 8 * sc, 12, ENG_UI_DIM);
        Eng_UiText("Use Tools > Map Settings... to edit map properties.",
                   X + 4 * sc, c.y + 24 * sc, 10, ENG_UI_DIM);
        return;
    }

    // ---- Fix #4: scrollable per-entity field list --------------------------
    // Mouse-wheel scroll when the cursor is inside the panel.
    if (CheckCollisionPointRec(GetMousePosition(), c))
        g_inspScroll -= GetMouseWheelMove() * 22 * sc * 2.0f;

    EngMapEntKind k;
    EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);

    // Clamp scroll against last frame's measured content height (T2-2).
    // g_inspContentH is updated AFTER drawing each frame so the clamp here is
    // exact for every frame after the first (first frame falls back to the 600px
    // estimate stored in the static initialiser). This eliminates the one-frame
    // clip that the old 600*sc hard-code caused for tall entities.
    float maxScroll = g_inspContentH > c.height ? g_inspContentH - c.height : 0.0f;
    if (g_inspScroll < 0) g_inspScroll = 0;
    if (g_inspScroll > maxScroll) g_inspScroll = maxScroll;

    // Scissor to the panel rect so content below the fold is invisible.
    BeginScissorMode((int)c.x, (int)c.y, (int)c.width, (int)c.height);

    float y = c.y - g_inspScroll;

    Eng_UiText(TextFormat("#%d  %s", s->selectedId, EdScene_KindName(k)), X, y, 13, ENG_UI_GOLD);
    if (EdScene_SelCount(s) > 1)
        Eng_UiText(TextFormat("%d selected (editing primary)", EdScene_SelCount(s)),
                   X, y + 14 * sc, 10, ENG_UI_DIM);
    y += 22 * sc;

    // Position x, z — all kinds.
    float px = 0, pz = 0;
    if (Eng_GetPos(&s->doc, s->selectedId, &px, &pz)) {
        float newX = px, newZ = pz;
        bool cx = InspFloatBox(h, X, W, &y, sc, "pos x", &newX);
        bool cz = InspFloatBox(h, X, W, &y, sc, "pos z", &newZ);
        if (cx || cz) {
            Eng_SetPos(&s->doc, s->selectedId, newX, newZ);
            EdHost_CommitEdit(h);
        }
        y += 2 * sc;
    }

    // Kind-specific fields.
    if (k == ENGMAPENT_SPAWN) {
        // Spawn mob text field.
        char mob[MAPDOC_SPAWN_MOB_LEN];
        Eng_GetSpawnMob(&s->doc, s->selectedId, mob, sizeof mob);

        Eng_UiText("mob", X, y + 1 * sc, 12, ENG_UI_TEXT);
        static char g_spawnMobBuf[MAPDOC_SPAWN_MOB_LEN] = "";
        static bool g_spawnMobEdit = false;
        static int  g_spawnMobLastId = -1;
        if (g_spawnMobLastId != s->selectedId) {
            g_spawnMobLastId  = s->selectedId;
            snprintf(g_spawnMobBuf, sizeof g_spawnMobBuf, "%s", mob);
            g_spawnMobEdit = false;
        }
        bool wasMobEdit = g_spawnMobEdit;
        if (GuiTextBox((Rectangle){ X + 60 * sc, y, W - 60 * sc, 18 * sc },
                       g_spawnMobBuf, sizeof g_spawnMobBuf, wasMobEdit)) {
            g_spawnMobEdit = !wasMobEdit;
            if (wasMobEdit && strcmp(g_spawnMobBuf, mob) != 0) {
                Eng_SetSpawnMob(&s->doc, s->selectedId, g_spawnMobBuf);
                EdHost_CommitEdit(h);
            }
        }
        y += 22 * sc;

        // Quick-toggle PLAYER / ZOMBIE buttons.
        float bw = (W - 8 * sc) / 2.0f;
        bool isPlayer = (strcmp(mob, "PLAYER") == 0);
        bool isZombie = (strcmp(mob, "ZOMBIE") == 0);
        if (GuiButton((Rectangle){ X, y, bw, 20 * sc }, isPlayer ? "[PLAYER]" : "PLAYER")) {
            if (!isPlayer) {
                Eng_SetSpawnMob(&s->doc, s->selectedId, "PLAYER");
                snprintf(g_spawnMobBuf, sizeof g_spawnMobBuf, "PLAYER");
                EdHost_CommitEdit(h);
            }
        }
        if (GuiButton((Rectangle){ X + bw + 8 * sc, y, bw, 20 * sc }, isZombie ? "[ZOMBIE]" : "ZOMBIE")) {
            if (!isZombie) {
                Eng_SetSpawnMob(&s->doc, s->selectedId, "ZOMBIE");
                snprintf(g_spawnMobBuf, sizeof g_spawnMobBuf, "ZOMBIE");
                EdHost_CommitEdit(h);
            }
        }
        y += 26 * sc;

        // "Locked by" cycle button — gates this spawn behind a named door.
        // Only relevant for mob spawns (PLAYER spawns never gate), but shown for
        // all spawns so a mis-tagged spawn can still be cleared.
        {
            MapDocSpawn *sp = (MapDocSpawn *)EngMapEnt_Ptr(&s->doc,
                                  EngMapEnt_Find(&s->doc, s->selectedId), NULL);
            if (sp) {
                const char *lb = sp->lockedBy[0] ? sp->lockedBy : "<none>";
                Eng_UiText("locked by", X, y + 1 * sc, 12, ENG_UI_TEXT);
                char lbLbl[MAPDOC_DOOR_NAME_LEN + 8];
                snprintf(lbLbl, sizeof lbLbl, "%s", lb);
                if (GuiButton((Rectangle){ X + 60 * sc, y, W - 60 * sc, 20 * sc }, lbLbl)) {
                    CycleLockedBy(&s->doc, sp->lockedBy);
                    EdHost_CommitEdit(h);
                }
                y += 24 * sc;
                Eng_UiText("(click to cycle named doors)", X, y, 9, ENG_UI_DIM);
                y += 14 * sc;
            }
        }

    } else if (k == ENGMAPENT_WINDOW) {
        // Window facing toggle group.
        char dir[4]; Eng_GetWindowDir(&s->doc, s->selectedId, dir, sizeof dir);
        const char *dirs[4] = { "+x", "+z", "-x", "-z" };
        int di = 0;
        for (int i = 0; i < 4; i++) if (strcmp(dir, dirs[i]) == 0) { di = i; break; }
        Eng_UiText("facing", X, y + 1 * sc, 12, ENG_UI_TEXT); y += 16 * sc;
        int newDi = di;
        GuiToggleGroup((Rectangle){ X, y, (W - 12 * sc) / 4.0f, 20 * sc }, "+x;+z;-x;-z", &newDi);
        if (newDi != di) {
            Eng_SetWindowDir(&s->doc, s->selectedId, dirs[newDi]);
            EdHost_CommitEdit(h);
        }
        y += 26 * sc;

        // "Locked by" cycle button — gates this window behind a named door.
        {
            MapDocWindow *win = (MapDocWindow *)EngMapEnt_Ptr(&s->doc,
                                    EngMapEnt_Find(&s->doc, s->selectedId), NULL);
            if (win) {
                const char *lb = win->lockedBy[0] ? win->lockedBy : "<none>";
                Eng_UiText("locked by", X, y + 1 * sc, 12, ENG_UI_TEXT);
                char lbLbl[MAPDOC_DOOR_NAME_LEN + 8];
                snprintf(lbLbl, sizeof lbLbl, "%s", lb);
                if (GuiButton((Rectangle){ X + 60 * sc, y, W - 60 * sc, 20 * sc }, lbLbl)) {
                    CycleLockedBy(&s->doc, win->lockedBy);
                    EdHost_CommitEdit(h);
                }
                y += 24 * sc;
                Eng_UiText("(click to cycle named doors)", X, y, 9, ENG_UI_DIM);
                y += 14 * sc;
            }
        }

    } else if (k == ENGMAPENT_PROP) {
        // Prop yaw + uniform scale sliders.
        float yawDeg = 0, scale = 1;
        Eng_GetYaw(&s->doc, s->selectedId, &yawDeg);
        Eng_GetScale(&s->doc, s->selectedId, &scale);
        float newYaw = yawDeg, newScale = scale;
        if (InspSlider(X, W, &y, sc, "yaw",   &newYaw,   0,   360)) {
            Eng_SetYaw(&s->doc, s->selectedId, newYaw);
            InspCommitCont(h);
        }
        if (InspSlider(X, W, &y, sc, "scale", &newScale, 0.1f, 5)) {
            Eng_SetScale(&s->doc, s->selectedId, newScale);
            InspCommitCont(h);
        }

    } else if (k == ENGMAPENT_WALL) {
        // Wall endpoints (x1,z1)–(x2,z2): editable here AND via the two viewport
        // endpoint handles. The generic pos x/z block above moves the whole wall
        // (its midpoint); these set one end. Written through the typed pointer.
        MapDocWall *w = (MapDocWall *)EngMapEnt_Ptr(&s->doc,
                            EngMapEnt_Find(&s->doc, s->selectedId), NULL);
        if (w) {
            float x1 = w->x1, z1 = w->z1, x2 = w->x2, z2 = w->z2;
            bool c1 = InspFloatBox(h, X, W, &y, sc, "x1", &x1);
            bool d1 = InspFloatBox(h, X, W, &y, sc, "z1", &z1);
            bool c2 = InspFloatBox(h, X, W, &y, sc, "x2", &x2);
            bool d2 = InspFloatBox(h, X, W, &y, sc, "z2", &z2);
            if (c1 || d1 || c2 || d2) {
                w->x1 = x1; w->z1 = z1; w->x2 = x2; w->z2 = z2;
                EdHost_CommitEdit(h);
            }
            y += 4 * sc;

            // ---- Door sub-inspector -------------------------------------------
            y += 2 * sc;
            Eng_UiText("DOOR", X, y, 13, (Color){ 200, 205, 215, 255 }); y += 18 * sc;

            // door.present toggle
            bool wasDoor = w->door.present;
            GuiCheckBox((Rectangle){ X, y, 14 * sc, 14 * sc }, " door", &w->door.present);
            if (w->door.present != wasDoor) EdHost_CommitEdit(h);
            y += 20 * sc;

            if (w->door.present) {
                // center: position along the wall's running axis (any value ok)
                float dCenter = w->door.center;
                if (InspFloatBox(h, X, W, &y, sc, "center", &dCenter)) {
                    w->door.center = dCenter;
                    EdHost_CommitEdit(h);
                }

                // width: must be >= 0
                float dWidth = w->door.width;
                if (InspFloatBox(h, X, W, &y, sc, "width", &dWidth)) {
                    if (dWidth < 0.0f) dWidth = 0.0f;
                    w->door.width = dWidth;
                    EdHost_CommitEdit(h);
                }

                // cost: int >= 0; no hard upper bound
                int dCost = w->door.cost;
                if (InspIntBox(h, X, W, &y, sc, "cost", &dCost, 0, INT_MAX)) {
                    w->door.cost = dCost;
                    EdHost_CommitEdit(h);
                }

                // name (AS clause)
                if (InspTextField(h, X, W, &y, sc, "name", w->door.name,
                                  (int)sizeof(w->door.name))) {
                    EdHost_CommitEdit(h);
                }
            }
        }
        InspTexField(h, s, X, W, &y, sc);

    } else if (k == ENGMAPENT_OBSTACLE) {
        // Obstacle sx / sz / h.
        float sx = 1, sz = 1, oh = 1;
        Eng_GetObstacleSize(&s->doc, s->selectedId, &sx, &sz, &oh);
        float nsx = sx, nsz = sz, noh = oh;
        bool csx = InspFloatBox(h, X, W, &y, sc, "size x", &nsx);
        bool csz = InspFloatBox(h, X, W, &y, sc, "size z", &nsz);
        bool coh = InspFloatBox(h, X, W, &y, sc, "height", &noh);
        if (csx || csz || coh) {
            Eng_SetObstacleSize(&s->doc, s->selectedId, nsx, nsz, noh);
            EdHost_CommitEdit(h);
        }
        y += 4 * sc;
        InspTexField(h, s, X, W, &y, sc);

    } else if (k == ENGMAPENT_SECTOR) {
        // Sector sx / sz + yLow / yHigh.
        float sx = 10, sz = 10, yLow = 0, yHigh = 0;
        Eng_GetSectorSize(&s->doc, s->selectedId, &sx, &sz);
        Eng_GetSectorHeights(&s->doc, s->selectedId, &yLow, &yHigh);
        float nsx = sx, nsz = sz, nyLow = yLow, nyHigh = yHigh;
        bool csx   = InspFloatBox(h, X, W, &y, sc, "size x", &nsx);
        bool csz   = InspFloatBox(h, X, W, &y, sc, "size z", &nsz);
        bool cyLow = InspFloatBox(h, X, W, &y, sc, "y low",  &nyLow);
        bool cyHi  = InspFloatBox(h, X, W, &y, sc, "y high", &nyHigh);
        if (csx || csz) {
            Eng_SetSectorSize(&s->doc, s->selectedId, nsx, nsz);
            EdHost_CommitEdit(h);
        }
        if (cyLow || cyHi) {
            Eng_SetSectorHeights(&s->doc, s->selectedId, nyLow, nyHigh);
            EdHost_CommitEdit(h);
        }
        y += 4 * sc;

        // Kind: FLAT vs RAMP (SECTOR_FLAT = 0, SECTOR_RAMP = 1).
        int kind = SECTOR_FLAT; Eng_GetSectorKind(&s->doc, s->selectedId, &kind);
        Eng_UiText("kind", X, y + 1 * sc, 12, ENG_UI_TEXT); y += 16 * sc;
        int newKind = kind;
        GuiToggleGroup((Rectangle){ X, y, (W - 12 * sc) / 2.0f, 20 * sc }, "FLAT;RAMP", &newKind);
        if (newKind != kind) {
            Eng_SetSectorKind(&s->doc, s->selectedId, newKind);
            if (newKind == SECTOR_RAMP) {
                // Seed a valid ramp: give it a rise + a default axis if it has none,
                // so it passes MapDoc_Validate straight away.
                float lo, hi; Eng_GetSectorHeights(&s->doc, s->selectedId, &lo, &hi);
                if (hi - lo < 0.01f) Eng_SetSectorHeights(&s->doc, s->selectedId, lo, lo + 4.0f);
                int ax, la, lb; Eng_GetSectorRamp(&s->doc, s->selectedId, &ax, &la, &lb);
                if (ax != 1 && ax != 2) Eng_SetSectorRamp(&s->doc, s->selectedId, 1, la, lb);
            }
            EdHost_CommitEdit(h);
        }
        y += 26 * sc;

        // RAMP-only: rise axis + the two linked sectors (AI nav edge).
        if (newKind == SECTOR_RAMP) {
            int axis = 1, la = -1, lb = -1;
            Eng_GetSectorRamp(&s->doc, s->selectedId, &axis, &la, &lb);
            int ai = (axis == 2) ? 1 : 0;   // 0 = +X, 1 = +Z
            Eng_UiText("rise axis", X, y + 1 * sc, 12, ENG_UI_TEXT); y += 16 * sc;
            int newAi = ai;
            GuiToggleGroup((Rectangle){ X, y, (W - 12 * sc) / 2.0f, 20 * sc }, "+X;+Z", &newAi);
            if (newAi != ai) {
                axis = (newAi == 1) ? 2 : 1;
                Eng_SetSectorRamp(&s->doc, s->selectedId, axis, la, lb);
                EdHost_CommitEdit(h);
            }
            y += 26 * sc;

            int selfIdx = EngMapEnt_Find(&s->doc, s->selectedId).index;
            Eng_UiText("links (cycle, or Pick in viewport)", X, y, 9, ENG_UI_DIM); y += 14 * sc;
            // Each row: a cycle button (most of the width) + a Pick button that
            // arms viewport click-to-link (s->linkPick: 1 = A, 2 = B).
            float full = W - 12 * sc, cycW = full * 0.62f, pkW = full * 0.34f, pkX = X + full - pkW;
            char lbl[64];
            snprintf(lbl, sizeof lbl, "A: %s", SectorLabel(&s->doc, la));
            if (GuiButton((Rectangle){ X, y, cycW, 20 * sc }, lbl)) {
                Eng_SetSectorRamp(&s->doc, s->selectedId, axis,
                                  CycleSectorLink(&s->doc, selfIdx, la), lb);
                EdHost_CommitEdit(h);
            }
            if (GuiButton((Rectangle){ pkX, y, pkW, 20 * sc }, s->linkPick == 1 ? "click..." : "Pick")) {
                s->linkPick = (s->linkPick == 1) ? 0 : 1;
                if (s->linkPick == 1) EdHost_Log(h, ED_LOG_INFO, "link A: click a sector in the viewport (Esc cancels)");
            }
            y += 24 * sc;
            snprintf(lbl, sizeof lbl, "B: %s", SectorLabel(&s->doc, lb));
            if (GuiButton((Rectangle){ X, y, cycW, 20 * sc }, lbl)) {
                Eng_SetSectorRamp(&s->doc, s->selectedId, axis, la,
                                  CycleSectorLink(&s->doc, selfIdx, lb));
                EdHost_CommitEdit(h);
            }
            if (GuiButton((Rectangle){ pkX, y, pkW, 20 * sc }, s->linkPick == 2 ? "click..." : "Pick")) {
                s->linkPick = (s->linkPick == 2) ? 0 : 2;
                if (s->linkPick == 2) EdHost_Log(h, ED_LOG_INFO, "link B: click a sector in the viewport (Esc cancels)");
            }
            y += 24 * sc;
        }
    }

    // ---- Inline validation for the selected entity (TODO item 7) -----------
    // MapDoc_Validate already powers the red viewport outline, but the reason
    // only appeared as a console line that scrolls away. Echo this entity's
    // issues right here so the fix is visible next to the field that caused it.
    MapDocIssue issues[64];
    int nIssue = MapDoc_Validate(&s->doc, issues, 64);
    bool issueHeader = false;
    for (int i = 0; i < nIssue && i < 64; i++) {
        if (issues[i].entId != s->selectedId) continue;
        if (!issueHeader) {
            y += 6 * sc;
            Eng_UiText("ISSUES", X, y, 12, (Color){ 235, 110, 110, 255 }); y += 16 * sc;
            issueHeader = true;
        }
        Color col = (issues[i].severity == MAPDOC_ERROR)
                    ? (Color){ 235, 110, 110, 255 } : ENG_UI_GOLD;
        Eng_UiText(issues[i].msg, X, y, 10, col); y += 13 * sc;
    }

    EndScissorMode();

    // Measure the true content height from this frame's final y position (T2-2).
    // Store it in g_inspContentH so the NEXT frame's pre-draw clamp is exact.
    // Also apply it immediately to snap out any over-scroll that the pre-draw
    // estimate allowed (e.g. on the very first frame with a new selection).
    float contentH = (y + g_inspScroll) - c.y;
    g_inspContentH = contentH > 0.0f ? contentH : 0.0f;
    float realMax = g_inspContentH > c.height ? g_inspContentH - c.height : 0.0f;
    if (g_inspScroll > realMax) g_inspScroll = realMax;
}
