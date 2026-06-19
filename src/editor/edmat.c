// ============================================================================
//  edmat.c — material-mode viewport rendering.
//
//  Split out of edscene.c. "Material mode" (toggled with M) swaps the flat proxy
//  boxes for textured geometry — floor/ceiling quads, textured wall/obstacle
//  boxes, and real prop models — lit with the engine's world shader so the
//  editor preview matches the game. edscene.c's draw loop calls DrawMaterialWorld;
//  everything else here is private to this file. Proxy boxes and floor heights
//  come from edscene.c via edscene_internal.h.
// ============================================================================

#include "edscene.h"
#include "edscene_internal.h"

#include "raymath.h"
#include "gfx.h"
#include "content.h"      // Eng_LoadTextureByName / Eng_LoadModel
#include "eng_render.h"   // Eng_RenderSetLighting / Begin/EndWorld + world shader

#include <stdio.h>

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
void DrawMaterialWorld(EdScene *s) {
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
