// ============================================================================
//  panel_inspector.c — the Inspector panel (per-entity + map-properties editor).
//
//  Split out of builtins.c — by far its largest single piece. Draws editable
//  fields for the selected entity (a per-kind ladder) or, when nothing is
//  selected, whole-map properties (name + atmosphere + textures). Registered as
//  a panel by edpanels.c's RegisterPanels via the PanelInspector declaration in
//  builtins_internal.h.
// ============================================================================

#include "builtins.h"
#include "builtins_internal.h"
#include "edscene.h"

#include "raygui.h"
#include "ui.h"
#include "mapedit.h"

#include <stdio.h>
#include <stdlib.h>   // atof, for inspector number fields
#include <string.h>

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
#define INSP_FLOAT_FIELDS 8   // max distinct float fields shown for one entity
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
// Returns true and writes *v if the user committed a change (toggled out of edit).
static bool InspFloatBox(float X, float W, float *y, float sc,
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
            // toggling out of edit → parse and commit
            float parsed = (float)atof(st->buf);
            if (parsed != *v) { *v = parsed; changed = true; }
        }
    }
    *y += 22 * sc;
    return changed;
}

// Per-surface texture-override field for WALL / OBSTACLE. Reads/writes the
// MapDoc TEX field via the engine accessors; a blank string clears the override
// so the surface falls back to the map's wall_ext slot. Commits on edit-toggle.
static void InspTexField(EdHost *h, EdScene *s, float X, float W, float *y, float sc) {
    static char buf[MAPDOC_TEX_NAME_LEN] = "";
    static bool edit = false;
    static int  lastId = -1;
    char cur[MAPDOC_TEX_NAME_LEN] = "";
    Eng_GetSurfaceTex(&s->doc, s->selectedId, cur, sizeof cur);
    if (lastId != s->selectedId) {
        lastId = s->selectedId;
        snprintf(buf, sizeof buf, "%s", cur);
        edit = false;
    }
    Eng_UiText("tex", X, *y + 1 * sc, 12, ENG_UI_TEXT);
    bool was = edit;
    if (GuiTextBox((Rectangle){ X + 60 * sc, *y, W - 60 * sc, 18 * sc }, buf, sizeof buf, was)) {
        edit = !was;
        if (was && strcmp(buf, cur) != 0) {
            Eng_SetSurfaceTex(&s->doc, s->selectedId, buf);
            EdHost_CommitEdit(h);
        }
    }
    *y += 20 * sc;
    Eng_UiText("(blank = map default)", X, *y, 9, ENG_UI_DIM);
    *y += 14 * sc;
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

// ---- Inspector — Feature 1: editable fields / Feature 3: map metadata ------
void PanelInspector(EdHost *h, Rectangle c, void *u) {
    (void)u; EdScene *s = EdHost_Scene(h);
    float sc = EdHost_UiScale(h);
    float X = c.x, W = c.width, y = c.y;

    // Release every float-field slot when the selection changes, so a new entity
    // (possibly a different kind with different labels) starts from fresh buffers.
    if (s->selectedId != g_inspLastId) {
        g_inspLastId = s->selectedId;
        for (int i = 0; i < INSP_FLOAT_FIELDS; i++) g_inspFloat[i].key = NULL;
    }

    // End any continuous-edit coalescing run once the mouse is released, so the
    // next slider/picker drag starts a fresh undo step (see InspCommitCont).
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_inspContTag = 0;

    // ---- Map properties when nothing is selected (audit P1-A) --------------
    if (s->selectedId < 0) {
        Eng_UiText("MAP PROPERTIES", X, y, 14, ENG_UI_GOLD); y += 16 * sc;
        Eng_UiText("Nothing selected — editing the whole map.", X, y, 10, ENG_UI_DIM); y += 16 * sc;

        // Map name (doc.name)
        Eng_UiText("Name", X, y + 1 * sc, 12, ENG_UI_TEXT);
        static char g_mapNameBuf[MAPDOC_NAME_LEN] = "";
        static bool g_mapNameEdit = false;
        static int  g_mapNameLastId = -2; // use -2 as "uninitialised sentinel"
        // Reset buffer when we come back to the no-selection state and the doc
        // name might have changed from an Open/New.
        if (g_mapNameLastId != s->selectedId) {
            g_mapNameLastId = s->selectedId;
            snprintf(g_mapNameBuf, sizeof g_mapNameBuf, "%s", s->doc.name);
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
        return;
    }

    // ---- Editable inspector for the selected entity ------------------------
    // Affordance to reach the map-properties view without hunting for empty
    // space to click (audit P1-A): deselect → the no-selection branch above.
    if (GuiButton((Rectangle){ X, y, W, 18 * sc }, "#185# Map properties...")) {
        s->selectedId = -1;
        return;
    }
    y += 24 * sc;

    EngMapEntKind k;
    EngMapEnt_Ptr(&s->doc, EngMapEnt_Find(&s->doc, s->selectedId), &k);
    Eng_UiText(TextFormat("#%d  %s", s->selectedId, EdScene_KindName(k)), X, y, 13, ENG_UI_GOLD);
    if (EdScene_SelCount(s) > 1)
        Eng_UiText(TextFormat("%d selected (editing primary)", EdScene_SelCount(s)),
                   X, y + 14 * sc, 10, ENG_UI_DIM);
    y += 22 * sc;

    // Position x, z — all kinds.
    float px = 0, pz = 0;
    if (Eng_GetPos(&s->doc, s->selectedId, &px, &pz)) {
        float newX = px, newZ = pz;
        bool cx = InspFloatBox(X, W, &y, sc, "pos x", &newX);
        bool cz = InspFloatBox(X, W, &y, sc, "pos z", &newZ);
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
        // Walls carry only a per-surface texture override in the inspector;
        // their geometry is edited by dragging the move gizmo / endpoints.
        InspTexField(h, s, X, W, &y, sc);

    } else if (k == ENGMAPENT_OBSTACLE) {
        // Obstacle sx / sz / h.
        float sx = 1, sz = 1, oh = 1;
        Eng_GetObstacleSize(&s->doc, s->selectedId, &sx, &sz, &oh);
        float nsx = sx, nsz = sz, noh = oh;
        bool csx = InspFloatBox(X, W, &y, sc, "size x", &nsx);
        bool csz = InspFloatBox(X, W, &y, sc, "size z", &nsz);
        bool coh = InspFloatBox(X, W, &y, sc, "height", &noh);
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
        bool csx   = InspFloatBox(X, W, &y, sc, "size x", &nsx);
        bool csz   = InspFloatBox(X, W, &y, sc, "size z", &nsz);
        bool cyLow = InspFloatBox(X, W, &y, sc, "y low",  &nyLow);
        bool cyHi  = InspFloatBox(X, W, &y, sc, "y high", &nyHigh);
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
}
