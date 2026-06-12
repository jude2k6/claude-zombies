#include "viewmodel.h"
#include "types.h"
#include "assets.h"
#include "weapons.h"
#include "player.h"
#include "anim.h"
#include "interact.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

// ---- shared first-person ARMS viewmodel (glTF) -------------------------
// One rigged pair of arms+hands (arms_vm.glb) used by ALL guns. The gun is a
// separate model bolted onto the `hand.R` bone each frame, so a character
// skin only needs to retexture this one arms model (not every gun). Clip set
// mirrors the gun viewmodel clips; the gun rides the right hand, so
// recoil/reload read for free. Per-gun seating comes from weaponGrip[]
// (vm_grip_* keys in the .weapon files).
static AnimModel armsVM;
static AnimState armsVMState;
static int avmIdle = -1, avmFire = -1, avmReload = -1, avmReloadEmpty = -1,
           avmRaise = -1, avmLower = -1, avmSprint = -1, avmHandR = -1;

// Load the shared first-person arms; bind the skinned shader. Call once after
// Assets_Load. No-op-safe: missing .glb leaves the 4 non-pistol guns on their
// gun-only floating OBJ path.
void Viewmodel_LoadArms(void) {
    if (!Anim_Load(&armsVM, "arms_vm.glb")) return;
    if (worldSkinnedShaderLoaded) Anim_ApplyShader(&armsVM, worldSkinnedShader);
    avmIdle        = Anim_FindClip(&armsVM, "idle");
    avmFire        = Anim_FindClip(&armsVM, "fire");
    avmReload      = Anim_FindClip(&armsVM, "reload");
    avmReloadEmpty = Anim_FindClip(&armsVM, "reload_empty");
    avmRaise       = Anim_FindClip(&armsVM, "raise");
    avmLower       = Anim_FindClip(&armsVM, "lower");
    avmSprint      = Anim_FindClip(&armsVM, "sprint");
    avmHandR       = Anim_FindBone(&armsVM, "hand.R");
    Anim_Play(&armsVMState, avmIdle, true, 1.0f);
}

void Viewmodel_UnloadArms(void) {
    Anim_Unload(&armsVM);
}

// Per-weapon grip seating lives in weaponGrip[] (weapons.c), populated from
// the vm_grip_pos / vm_grip_rot / vm_grip_scale keys of each .weapon file.
// The base orientation rotates the gun OBJ (long axis model -Z = muzzle,
// +Y = up) so the muzzle points +Y (arms forward) and up stays +Z. `pos`
// nudges the grip in hand-local metres (after that base rotation: +x=right,
// +y=toward muzzle, +z=up; pos.x positive moves the gun right on screen),
// `rotDeg` is extra fine rotation, `scale` sizes the gun against the arms.
// Tune via --screenshot-viewmodels — edit the .weapon, rerun, no recompile.

// Procedural viewmodel movement: a gentle bob while walking and a stronger
// sink + high-frequency jitter while sprinting. `outPos` is a world-space
// offset built from the camera basis; `outTilt` is an extra muzzle-down tilt
// (radians) for the sprint pose. Driven by the local player's moveBlend /
// sprintBlend; ADS settles it all down.
static void ViewmodelMotion(Player *me, Vector3 fwd, Vector3 right, Vector3 up,
                            Vector3 *outPos, float *outTilt) {
    static float phase = 0.0f;
    float dt     = GetFrameTime();
    float walk   = me->moveBlend;
    float sprint = me->sprintBlend;
    float ads    = me->adsHeld ? 0.25f : 1.0f;

    // Bob frequency rises with sprint; amplitude scales with how much we move.
    float freq = 6.5f + 4.5f * sprint;
    phase += dt * freq;
    float amp  = (0.012f + 0.05f * sprint) * walk * ads;
    float bobV = sinf(phase * 2.0f) * amp;        // vertical (double freq)
    float bobH = sinf(phase)        * amp * 0.7f; // lateral sway

    // Sprint: gun drops, pulls in slightly, and shakes with stacked sines.
    float sink     = sprint * 0.10f * ads;
    float shakeAmp = sprint * 0.020f * ads;
    float sx = (sinf(phase * 13.3f) + 0.5f * sinf(phase * 7.7f)) * shakeAmp;
    float sy = (sinf(phase * 11.1f) + 0.5f * sinf(phase * 5.3f)) * shakeAmp;

    Vector3 o = { 0, 0, 0 };
    o = Vector3Add(o, Vector3Scale(right, bobH + sx));
    o = Vector3Add(o, Vector3Scale(up,    bobV - sink + sy));
    o = Vector3Add(o, Vector3Scale(fwd,  -sprint * 0.05f * ads));
    *outPos  = o;
    *outTilt = sprint * 0.35f * ads;              // muzzle dips while sprinting
}

// Shared first-person arms + the equipped gun bolted to the hand.R bone. Used
// for every non-pistol gun. Clip is picked from the local player's weapon state
// (same logic as the pistol VM); the gun rides the right hand so recoil/reload
// motion is inherited from the arms clip for free.
static void DrawArmsViewmodel(Camera camera, int wi) {
    Player *me = &players[localPlayerIdx];
    int cs = me->currentSlot;
    WeaponSlot *slot = &me->inventory[cs];
    float dt = GetFrameTime();

    static int prevSlot = -1, prevWi = -1;
    static float prevFire = 0.0f, prevReload = 0.0f;
    static bool reloadEmpty = false;
    bool swapEdge    = (cs != prevSlot || wi != prevWi);
    bool fireEdge    = (slot->fireTimer  > 0.0f && prevFire   <= 0.0f);
    bool reloadStart = (slot->reloadTimer > 0.0f && prevReload <= 0.0f);
    bool reloading   = (slot->reloadTimer > 0.0f);
    if (reloadStart) reloadEmpty = (slot->ammo == 0);
    prevSlot = cs; prevWi = wi; prevFire = slot->fireTimer; prevReload = slot->reloadTimer;
    bool sprinting = (me->sprintBlend > 0.6f) && !reloading;

    AnimState *st = &armsVMState;
    if (swapEdge && avmRaise >= 0) {
        Anim_Play(st, avmRaise, false, 1.0f);
    } else if (reloadStart) {
        int clip = (reloadEmpty && avmReloadEmpty >= 0) ? avmReloadEmpty : avmReload;
        float dur = Anim_ClipDuration(&armsVM, clip);
        float spd = (slot->reloadTimer > 0.05f) ? dur / slot->reloadTimer : 1.0f;
        Anim_Play(st, clip, false, spd);
    } else if (fireEdge && !reloading && avmFire >= 0) {
        Anim_Play(st, avmFire, false, 1.0f);
    } else {
        bool busy = ((st->clip == avmFire || st->clip == avmRaise) && !st->finished)
                 || (reloading && (st->clip == avmReload || st->clip == avmReloadEmpty));
        if (!busy) {
            int want = (sprinting && avmSprint >= 0) ? avmSprint : avmIdle;
            if (st->clip != want) Anim_Play(st, want, true, 1.0f);
        }
    }
    Anim_Update(&armsVM, st, dt);
    Anim_Pose(&armsVM, st);

    // ---- camera-space root matrix (model X->right, Y->forward, Z->up) ----
    Vector3 camFwd   = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 camRight = Vector3Normalize(Vector3CrossProduct(camFwd, camera.up));
    Vector3 camUp    = Vector3CrossProduct(camRight, camFwd);
    const float PITCH = -0.06f;
    Vector3 fwd = Vector3Normalize(Vector3Add(camFwd, Vector3Scale(camUp, PITCH)));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, camera.up));
    Vector3 up    = Vector3CrossProduct(right, fwd);
    // Arms + gun are both authored at real metric, so one scale sizes the whole
    // assembly; the gun ends up a short-forearm-stub + dominant gun (correct FP
    // framing). Anchor puts the right-hand grip just below-front of the eye.
    const float VM_SCALE = 0.62f;
    Vector3 anchor = camera.position;
    anchor = Vector3Add(anchor, Vector3Scale(camFwd,  0.30f));
    anchor = Vector3Add(anchor, Vector3Scale(camRight, 0.11f));
    anchor = Vector3Add(anchor, Vector3Scale(camUp,  -0.12f));
    // Walk-bob / sprint-sink-shake on top of the sprint clip (offset only — the
    // arms+gun ride the same root, so the gun follows for free).
    Vector3 moveOff; float moveTilt;
    ViewmodelMotion(me, fwd, right, up, &moveOff, &moveTilt);
    (void)moveTilt;
    anchor = Vector3Add(anchor, moveOff);
    Vector3 cx = Vector3Scale(right, VM_SCALE);
    Vector3 cy = Vector3Scale(fwd,   VM_SCALE);
    Vector3 cz = Vector3Scale(up,    VM_SCALE);
    Matrix root;
    root.m0 = cx.x; root.m4 = cy.x; root.m8  = cz.x; root.m12 = anchor.x;
    root.m1 = cx.y; root.m5 = cy.y; root.m9  = cz.y; root.m13 = anchor.y;
    root.m2 = cx.z; root.m6 = cy.z; root.m10 = cz.z; root.m14 = anchor.z;
    root.m3 = 0;    root.m7 = 0;    root.m11 = 0;    root.m15 = 1;

    // gun model.transform = root * handBone * gunLocal (raylib MatrixMultiply
    // applies the LEFT operand first, so the chain reads inner->outer).
    WeaponGrip g = weaponGrip[wi];
    float gs = (g.scale > 0.0f) ? g.scale : 1.0f;
    Matrix gunLocal = MatrixScale(gs, gs, gs);
    gunLocal = MatrixMultiply(gunLocal, MatrixRotateX(PI * 0.5f)); // -Z->+Y, +Y->+Z
    if (g.rotDeg.x || g.rotDeg.y || g.rotDeg.z)
        gunLocal = MatrixMultiply(gunLocal, MatrixRotateXYZ((Vector3){
            g.rotDeg.x * DEG2RAD, g.rotDeg.y * DEG2RAD, g.rotDeg.z * DEG2RAD }));
    gunLocal = MatrixMultiply(gunLocal, MatrixTranslate(g.pos.x, g.pos.y, g.pos.z));
    Matrix bone = Anim_BoneMatrix(&armsVM, st, avmHandR);
    Matrix gunTx = MatrixMultiply(MatrixMultiply(gunLocal, bone), root);

    armsVM.model.transform = root;
    Model gm = weaponModels[wi];
    gm.transform = gunTx;

    // Arms (skinned shader) under flat lighting so they don't colour-swing.
    if (worldSkinnedShaderLoaded) {
        Vector3 flatSun = { 0.25f, 0.26f, 0.30f };
        Vector3 flatAmb = { 1.30f, 1.31f, 1.36f };
        rlDrawRenderBatchActive();
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunColorLoc,     &flatSun, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_ambientColorLoc, &flatAmb, SHADER_UNIFORM_VEC3);
        DrawModel(armsVM.model, (Vector3){0,0,0}, 1.0f, WHITE);
        rlDrawRenderBatchActive();
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunColorLoc,     &sunColor,     SHADER_UNIFORM_VEC3);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_ambientColorLoc, &ambientColor, SHADER_UNIFORM_VEC3);
    } else {
        DrawModel(armsVM.model, (Vector3){0,0,0}, 1.0f, WHITE);
    }
    // Gun (OBJ world shader) under flat lighting.
    if (worldShaderLoaded) {
        Vector3 flatSun = { 0.12f, 0.13f, 0.16f };
        Vector3 flatAmb = { 0.90f, 0.91f, 0.96f };
        rlDrawRenderBatchActive();
        SetShaderValue(worldShader, worldShader_sunColorLoc,     &flatSun, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, worldShader_ambientColorLoc, &flatAmb, SHADER_UNIFORM_VEC3);
        DrawModel(gm, (Vector3){0,0,0}, 1.0f, WHITE);
        rlDrawRenderBatchActive();
        SetShaderValue(worldShader, worldShader_sunColorLoc,     &sunColor,     SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, worldShader_ambientColorLoc, &ambientColor, SHADER_UNIFORM_VEC3);
    } else {
        DrawModel(gm, (Vector3){0,0,0}, 1.0f, WHITE);
    }
}

// Draw the first-person viewmodel for the local player. Must be called inside
// an active BeginMode3D scope (Render_World3D handles that).
//
// Transform stack: vertex → (per-weapon yaw around model Y) → camera basis
// (model +X → world right, +Y → up, -Z → forward) → translate to anchor.
// weaponTune.yawDeg lets each weapon's authored "forward" axis be aligned
// to the camera's forward without modifying the OBJ.
void Viewmodel_DrawFirstPerson(Camera camera) {
    Player *me = &players[localPlayerIdx];
    // Hidden when dead/downed, and while noclipping — in noclip the camera
    // detaches and flies free, so the arms+gun must NOT trail it; the body is
    // drawn in third person at its frozen spot instead (see DrawOtherPlayer).
    if (!me->alive || me->downed || noclipMode) return;
    // Held weapon is physically in the PaP machine — show empty hands.
    if (PaP_SlotLocked(localPlayerIdx, me->currentSlot)) return;
    int wi = me->inventory[me->currentSlot].weaponIdx;
    if (wi < 0 || wi >= W_COUNT) return;
    if (!weaponModelLoaded[wi]) return;
    // All guns use the shared arms + gun bolted to hand.R (skinnable arms),
    // falling through to the legacy gun-only floating OBJ path below only if
    // arms_vm.glb isn't loaded (graceful degradation, never crashes).
    if (armsVM.loaded && avmHandR >= 0) { DrawArmsViewmodel(camera, wi); return; }

    // ---- viewmodel anim state (local to the player's render) ----
    // Swap: when the displayed weapon changes (currentSlot flip OR a
    // weapon swap into the same slot), start a 0.22s "raise from below"
    // animation. State is render-local since the viewmodel is local-only.
    static int   prev_slot = -1;
    static int   prev_wi   = -1;
    static float swap_t    = 0.0f;
    int cur_slot = me->currentSlot;
    if (cur_slot != prev_slot || wi != prev_wi) {
        swap_t = 1.0f;
        prev_slot = cur_slot;
        prev_wi   = wi;
    }
    if (swap_t > 0.0f) {
        swap_t -= GetFrameTime() / 0.22f;
        if (swap_t < 0.0f) swap_t = 0.0f;
    }
    // Reload: parabolic dip + lateral tilt over the weapon's reload time.
    float reload_dip = 0.0f;
    {
        WeaponSlot *cs = &me->inventory[cur_slot];
        const WeaponDef *cw = &WEAPONS[cs->weaponIdx];
        if (cs->reloadTimer > 0.0f && cw->reloadTime > 0.0f) {
            float t = 1.0f - cs->reloadTimer / cw->reloadTime;  // 0 -> 1
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            reload_dip = sinf(t * PI);   // 0 -> 1 -> 0
        }
    }
    // Swap raise: gun starts 0.32m below + 0.12m back, eases in
    float swap_down = swap_t * 0.32f;
    float swap_back = swap_t * 0.12f;
    // Reload dip: drops 0.18m, pushes 0.10m forward (away from screen)
    float reload_down = reload_dip * 0.18f;
    float reload_fwd  = reload_dip * 0.10f;
    // Tilt the gun (right-axis rotation) during reload — muzzle drops
    float tilt = reload_dip * 0.55f;  // radians (~31 deg peak)

    Vector3 fwd   = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, camera.up));
    Vector3 up    = Vector3CrossProduct(right, fwd);

    // Procedural walk-bob / sprint-sink-shake (M1911 + any gun on this path).
    Vector3 moveOff; float moveTilt;
    ViewmodelMotion(me, fwd, right, up, &moveOff, &moveTilt);
    tilt += moveTilt;

    // Viewmodel framing scale. The per-weapon `weaponTune.scale` knob is the
    // relative size between guns; this base sets the overall in-hand size.
    // (World pickups/wall-buys are sized separately in DrawWeaponDisplay.)
    float s = 0.05f * weaponTune[wi].scale;
    Vector3 anchor = camera.position;
    anchor = Vector3Add(anchor, Vector3Scale(fwd,    0.55f - swap_back + reload_fwd));
    anchor = Vector3Add(anchor, Vector3Scale(right,  0.35f));
    anchor = Vector3Add(anchor, Vector3Scale(up,    -0.28f - reload_down - swap_down));
    anchor = Vector3Add(anchor, Vector3Scale(right,  weaponTune[wi].offset.x));
    anchor = Vector3Add(anchor, Vector3Scale(up,     weaponTune[wi].offset.y));
    anchor = Vector3Add(anchor, Vector3Scale(fwd,    weaponTune[wi].offset.z));
    anchor = Vector3Add(anchor, moveOff);

    // R_y(yaw) applied in model space, then camera basis applied: the column
    // for model +X = cos(y)*right + sin(y)*fwd, for model +Z = sin(y)*right
    // - cos(y)*fwd. yaw=0 leaves model -Z pointing world-forward.
    float yawRad = weaponTune[wi].yawDeg * DEG2RAD;
    float cy = cosf(yawRad), sy = sinf(yawRad);
    Vector3 colX = { cy*right.x + sy*fwd.x, cy*right.y + sy*fwd.y, cy*right.z + sy*fwd.z };
    Vector3 colY = up;
    Vector3 colZ = { sy*right.x - cy*fwd.x, sy*right.y - cy*fwd.y, sy*right.z - cy*fwd.z };

    // Pitch tilt around the model's right axis (colX) for the reload dip:
    // rotates colY -> colY*cos(tilt) + colZ*sin(tilt) and colZ similarly.
    // Positive `tilt` drops the muzzle (model -Z) downward.
    if (tilt > 0.0f) {
        float ct = cosf(tilt), st = sinf(tilt);
        Vector3 newY = {
            colY.x * ct + colZ.x * st,
            colY.y * ct + colZ.y * st,
            colY.z * ct + colZ.z * st,
        };
        Vector3 newZ = {
            colZ.x * ct - colY.x * st,
            colZ.y * ct - colY.y * st,
            colZ.z * ct - colY.z * st,
        };
        colY = newY;
        colZ = newZ;
    }

    Matrix tx;
    tx.m0 = colX.x * s; tx.m4 = colY.x * s; tx.m8  = colZ.x * s; tx.m12 = anchor.x;
    tx.m1 = colX.y * s; tx.m5 = colY.y * s; tx.m9  = colZ.y * s; tx.m13 = anchor.y;
    tx.m2 = colX.z * s; tx.m6 = colY.z * s; tx.m10 = colZ.z * s; tx.m14 = anchor.z;
    tx.m3 = 0;          tx.m7 = 0;          tx.m11 = 0;          tx.m15 = 1;

    Model m = weaponModels[wi];
    m.transform = tx;

    // The world shader lights every fragment from a fixed *world* direction
    // using a screen-space-derivative normal. The viewmodel rotates with the
    // camera, so that makes its facets swing between lit and near-black as the
    // player looks around — "colour all over the place". Draw it under near-
    // flat lighting (tiny directional term for form, high ambient) so the gun
    // shows its authored material colours consistently, then restore the world
    // lighting for everything drawn afterwards.
    if (worldShaderLoaded) {
        Vector3 flatSun = { 0.12f, 0.13f, 0.16f };
        Vector3 flatAmb = { 0.90f, 0.91f, 0.96f };
        rlDrawRenderBatchActive();
        SetShaderValue(worldShader, worldShader_sunColorLoc,     &flatSun, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, worldShader_ambientColorLoc, &flatAmb, SHADER_UNIFORM_VEC3);
        DrawModel(m, (Vector3){0,0,0}, 1.0f, WHITE);
        rlDrawRenderBatchActive();
        SetShaderValue(worldShader, worldShader_sunColorLoc,     &sunColor,     SHADER_UNIFORM_VEC3);
        SetShaderValue(worldShader, worldShader_ambientColorLoc, &ambientColor, SHADER_UNIFORM_VEC3);
    } else {
        DrawModel(m, (Vector3){0,0,0}, 1.0f, WHITE);
    }
}
