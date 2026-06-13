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
#include <stdio.h>

// ---- combined per-weapon viewmodel rigs (NEW PATH) ---------------------
// Each weapon can have its own combined glTF (<id>/<id>_vm.glb) that already
// contains arms + gun + mechanical part-bones, with its own per-gun clip set.
// The engine just plays clips on the skinned model — no runtime gun-bolting,
// no vm_grip_*, no IK, no hand-seating math.
//
// Convention: combined rig is authored facing +Y in Blender, exported with
// export_yup=True → in raylib model space: forward = -Z, up = +Y, right = +X.
// (Same convention as zombie.glb / player.glb.)
// So the camera-space root maps: model +X → camera right, +Y → camera up,
//                                 model -Z → camera forward.
//
// --- Framing constants (tuned via --screenshot-viewmodels once real asset lands)
// The combined rig is authored with origin ≈ the eye point, looking down the
// arms; start with near-identity framing and small downward offset.
#define CRIG_SCALE        1.0f   // uniform framing scale for combined-rig VMs
#define CRIG_FWD_OFFSET   0.13f  // metres forward — push the rig off the lens
#define CRIG_RIGHT_OFFSET 0.07f  // metres right from camera.position
#define CRIG_DOWN_OFFSET  -0.14f // metres downward from camera.position (positive = down; negative lifts the gun up)
// Optional base pitch (radians) applied before camera basis; negative tilts
// muzzle down (matching a slight downward author pose). 0 = none.
#define CRIG_BASE_PITCH   0.0f

// Stable lowercase IDs matching the data/weapons/<id>/ directory names.
// Order must match the W_* enum: W_PISTOL=0, W_SMG=1, W_SHOTGUN=2,
// W_RIFLE=3, W_RAYGUN=4.
static const char *WEAPON_DIR_IDS[W_COUNT] = {
    [W_PISTOL]  = "pistol",
    [W_SMG]     = "smg",
    [W_SHOTGUN] = "shotgun",
    [W_RIFLE]   = "rifle",
    [W_RAYGUN]  = "raygun",
};

typedef struct {
    int idle, fire, reload, reloadEmpty, raise, lower, sprint;
} CrigClips;

static AnimModel  crigVM[W_COUNT];
static AnimState  crigVMState[W_COUNT];
static CrigClips  crigClips[W_COUNT];

// Load all per-weapon combined rigs that exist on disk. Missing files are
// silently skipped — the weapon falls through to the shared-arms path. Call
// after Assets_Load so worldSkinnedShader is ready.
void Viewmodel_LoadCombinedRigs(void) {
    for (int wi = 0; wi < W_COUNT; wi++) {
        const char *id = WEAPON_DIR_IDS[wi];
        if (!id) continue;
        char relpath[128];
        snprintf(relpath, sizeof relpath, "weapons/%s/%s_vm.glb", id, id);
        // Anim_Load searches data/models/ and data/ prefixes; weapons/ lives
        // under data/ so passing "weapons/<id>/<id>_vm.glb" hits the
        // "data/" prefix automatically.
        if (!Anim_Load(&crigVM[wi], relpath)) continue;
        if (worldSkinnedShaderLoaded)
            Anim_ApplyShader(&crigVM[wi], worldSkinnedShader);
        CrigClips *c = &crigClips[wi];
        c->idle        = Anim_FindClip(&crigVM[wi], "idle");
        c->fire        = Anim_FindClip(&crigVM[wi], "fire");
        c->reload      = Anim_FindClip(&crigVM[wi], "reload");
        c->reloadEmpty = Anim_FindClip(&crigVM[wi], "reload_empty");
        c->raise       = Anim_FindClip(&crigVM[wi], "raise");
        c->lower       = Anim_FindClip(&crigVM[wi], "lower");
        c->sprint      = Anim_FindClip(&crigVM[wi], "sprint");
        // Start in idle (or bind-pose if no idle clip)
        Anim_Play(&crigVMState[wi], c->idle, true, 1.0f);
        fprintf(stderr, "viewmodel: loaded combined rig for %s\n", id);
    }
}

void Viewmodel_UnloadCombinedRigs(void) {
    for (int wi = 0; wi < W_COUNT; wi++) Anim_Unload(&crigVM[wi]);
}

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
           avmRaise = -1, avmLower = -1, avmSprint = -1, avmHandR = -1,
           avmHandL = -1, avmIdlePistol = -1;

// Grip-tuning aid: draw markers at the hand bones (red sphere = hand.R
// origin + RGB axis ticks for its local X/Y/Z, blue sphere = hand.L).
// Enabled by --screenshot-viewmodels so vm_grip_* values can be dialled
// against the actual bone, not eyeballed mesh overlap.
bool vmDebugMarkers = false;

// Load the shared first-person arms; bind the skinned shader. Call once after
// Assets_Load. No-op-safe: missing .glb leaves the 4 non-pistol guns on their
// gun-only floating OBJ path.
void Viewmodel_LoadArms(void) {
    if (!Anim_Load(&armsVM, "arms_vm.glb")) return;
    if (worldSkinnedShaderLoaded) Anim_ApplyShader(&armsVM, worldSkinnedShader);
    avmIdle        = Anim_FindClip(&armsVM, "idle");
    avmIdlePistol  = Anim_FindClip(&armsVM, "idle_pistol");  // optional clip
    avmFire        = Anim_FindClip(&armsVM, "fire");
    avmReload      = Anim_FindClip(&armsVM, "reload");
    avmReloadEmpty = Anim_FindClip(&armsVM, "reload_empty");
    avmRaise       = Anim_FindClip(&armsVM, "raise");
    avmLower       = Anim_FindClip(&armsVM, "lower");
    avmSprint      = Anim_FindClip(&armsVM, "sprint");
    avmHandR       = Anim_FindBone(&armsVM, "hand.R");
    avmHandL       = Anim_FindBone(&armsVM, "hand.L");
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

// ---- combined-rig first-person draw ----------------------------------------
// Draws the combined per-weapon glTF (arms + gun + mechanical parts) using the
// same player-state → clip state machine as DrawArmsViewmodel. Called only when
// crigVM[wi].loaded is true. No separate gun model, no weaponGrip seating.
static void DrawCombinedRigViewmodel(Camera camera, int wi) {
    Player *me = &players[localPlayerIdx];
    int cs = me->currentSlot;
    WeaponSlot *slot = &me->inventory[cs];
    float dt = GetFrameTime();

    // State tracking (persistent across frames, per-weapon-slot edge detect).
    static int   cr_prevSlot   = -1;
    static int   cr_prevWi     = -1;
    static float cr_prevFire   = 0.0f;
    static float cr_prevReload = 0.0f;
    static bool  cr_reloadEmpty = false;

    bool swapEdge    = (cs != cr_prevSlot || wi != cr_prevWi);
    bool fireEdge    = (slot->fireTimer  > 0.0f && cr_prevFire   <= 0.0f);
    bool reloadStart = (slot->reloadTimer > 0.0f && cr_prevReload <= 0.0f);
    bool reloading   = (slot->reloadTimer > 0.0f);
    if (reloadStart) cr_reloadEmpty = (slot->ammo == 0);
    cr_prevSlot   = cs;
    cr_prevWi     = wi;
    cr_prevFire   = slot->fireTimer;
    cr_prevReload = slot->reloadTimer;
    bool sprinting = (me->sprintBlend > 0.6f) && !reloading;

    AnimModel *am = &crigVM[wi];
    AnimState *st = &crigVMState[wi];
    CrigClips *c  = &crigClips[wi];

    // --- Clip state machine (mirrors DrawArmsViewmodel) ---
    if (swapEdge && c->raise >= 0) {
        Anim_Play(st, c->raise, false, 1.0f);
    } else if (reloadStart) {
        int clip = (cr_reloadEmpty && c->reloadEmpty >= 0) ? c->reloadEmpty : c->reload;
        float dur = Anim_ClipDuration(am, clip);
        float spd = (slot->reloadTimer > 0.05f) ? dur / slot->reloadTimer : 1.0f;
        Anim_Play(st, clip, false, spd);
    } else if (fireEdge && !reloading && c->fire >= 0) {
        Anim_Play(st, c->fire, false, 1.0f);
    } else {
        bool busy = ((st->clip == c->fire || st->clip == c->raise) && !st->finished)
                 || (reloading && (st->clip == c->reload || st->clip == c->reloadEmpty));
        if (!busy) {
            int want = (sprinting && c->sprint >= 0) ? c->sprint : c->idle;
            if (st->clip != want) Anim_Play(st, want, true, 1.0f);
        }
    }
    Anim_Update(am, st, dt);
    Anim_Pose(am, st);

    // --- Camera-space root matrix ---
    // Combined rig authored: +Y in Blender, export_yup=True → in raylib:
    //   forward = -Z, up = +Y, right = +X  (same convention as zombie.glb).
    // Map: model +X → camera right, model +Y → camera up, model -Z → camera fwd.
    Vector3 camFwd   = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 camRight = Vector3Normalize(Vector3CrossProduct(camFwd, camera.up));
    Vector3 camUp    = Vector3CrossProduct(camRight, camFwd);

    // Optional base pitch rotates camFwd / camUp around camRight (negative = nose down).
    Vector3 fwd = camFwd, up = camUp;
    if (CRIG_BASE_PITCH != 0.0f) {
        float cp = cosf(CRIG_BASE_PITCH), sp = sinf(CRIG_BASE_PITCH);
        fwd = (Vector3){
            camFwd.x * cp + camUp.x * sp,
            camFwd.y * cp + camUp.y * sp,
            camFwd.z * cp + camUp.z * sp,
        };
        up = (Vector3){
            camUp.x * cp - camFwd.x * sp,
            camUp.y * cp - camFwd.y * sp,
            camUp.z * cp - camFwd.z * sp,
        };
        fwd = Vector3Normalize(fwd);
        up  = Vector3Normalize(up);
    }
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, camera.up));

    // Anchor: origin near camera.position + small static offsets.
    // CRIG_FWD_OFFSET, CRIG_RIGHT_OFFSET, CRIG_DOWN_OFFSET are the tuning
    // constants at the top of this file.
    Vector3 anchor = camera.position;
    anchor = Vector3Add(anchor, Vector3Scale(camFwd,   CRIG_FWD_OFFSET));
    anchor = Vector3Add(anchor, Vector3Scale(camRight,  CRIG_RIGHT_OFFSET));
    anchor = Vector3Add(anchor, Vector3Scale(camUp,    -CRIG_DOWN_OFFSET));

    // Walk-bob / sprint-sink-shake applied as a world-space offset (same as
    // the arms path — arms+gun ride the same root so the gun follows for free).
    Vector3 moveOff; float moveTilt;
    ViewmodelMotion(me, fwd, right, up, &moveOff, &moveTilt);
    (void)moveTilt;  // no extra tilt on combined rig (it's in the clip)
    anchor = Vector3Add(anchor, moveOff);

    // Build the 4×4 column-major root matrix (raylib convention):
    //   col 0 = model +X → world right  (scaled by CRIG_SCALE)
    //   col 1 = model +Y → world up     (scaled)
    //   col 2 = model -Z → world fwd    (negated, scaled)
    //   col 3 = translation
    float s = CRIG_SCALE;
    Vector3 cx = Vector3Scale(right, s);     // model +X → camera right
    Vector3 cy = Vector3Scale(up,    s);     // model +Y → camera up
    Vector3 cz = Vector3Scale(fwd,  -s);     // model -Z → camera fwd (negate)
    Matrix root;
    root.m0 = cx.x; root.m4 = cy.x; root.m8  = cz.x; root.m12 = anchor.x;
    root.m1 = cx.y; root.m5 = cy.y; root.m9  = cz.y; root.m13 = anchor.y;
    root.m2 = cx.z; root.m6 = cy.z; root.m10 = cz.z; root.m14 = anchor.z;
    root.m3 = 0;    root.m7 = 0;    root.m11 = 0;    root.m15 = 1;

    am->model.transform = root;

    // Draw under flat lighting (same treatment as the arms path) so the rig
    // doesn't colour-swing with the world directional light as the player
    // looks around.
    if (worldSkinnedShaderLoaded) {
        Vector3 flatSun = { 0.45f, 0.46f, 0.50f };
        Vector3 flatAmb = { 2.40f, 2.42f, 2.50f };
        rlDrawRenderBatchActive();
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunColorLoc,
                       &flatSun, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_ambientColorLoc,
                       &flatAmb, SHADER_UNIFORM_VEC3);
        DrawModel(am->model, (Vector3){0,0,0}, 1.0f, WHITE);
        rlDrawRenderBatchActive();
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_sunColorLoc,
                       &sunColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(worldSkinnedShader, worldSkinnedShader_ambientColorLoc,
                       &ambientColor, SHADER_UNIFORM_VEC3);
    } else {
        DrawModel(am->model, (Vector3){0,0,0}, 1.0f, WHITE);
    }
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
            // Idle hold is per-weapon (`vm_pose` key): pistols cup the gun
            // hand (`idle_pistol`), long guns use the foregrip `idle`.
            int idle = (WEAPONS[wi].vmPose == VMPOSE_PISTOL && avmIdlePistol >= 0)
                       ? avmIdlePistol : avmIdle;
            int want = (sprinting && avmSprint >= 0) ? avmSprint : idle;
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

    // Grip-tuning markers, drawn LAST with depth testing off so they show
    // through the hand/gun meshes (the bone origin is inside both).
    // Red sphere + 3 cm axis ticks (orange=X, green=Y, sky=Z) = hand.R;
    // blue sphere = hand.L.
    if (vmDebugMarkers) {
        // One-shot dump of the hand bones in ARMS-MODEL space so vm_grip_*
        // values can be solved numerically (hand.L relative to hand.R's
        // local frame = where the gun's forend must reach).
        static bool dumped = false;
        if (!dumped && avmHandL >= 0) {
            dumped = true;
            Matrix bL = Anim_BoneMatrix(&armsVM, st, avmHandL);
            fprintf(stderr, "vmdbg handR rot rows: [%+.3f %+.3f %+.3f] [%+.3f %+.3f %+.3f] [%+.3f %+.3f %+.3f]\n",
                    bone.m0, bone.m4, bone.m8,
                    bone.m1, bone.m5, bone.m9,
                    bone.m2, bone.m6, bone.m10);
            fprintf(stderr, "vmdbg handR pos: %+.4f %+.4f %+.4f\n", bone.m12, bone.m13, bone.m14);
            fprintf(stderr, "vmdbg handL pos: %+.4f %+.4f %+.4f\n", bL.m12, bL.m13, bL.m14);
        }
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        Matrix mR = MatrixMultiply(bone, root);
        Vector3 pR = { mR.m12, mR.m13, mR.m14 };
        DrawSphere(pR, 0.010f, RED);
        Vector3 ax = Vector3Add(pR, Vector3Scale((Vector3){ mR.m0, mR.m1, mR.m2  }, 0.03f));
        Vector3 ay = Vector3Add(pR, Vector3Scale((Vector3){ mR.m4, mR.m5, mR.m6  }, 0.03f));
        Vector3 az = Vector3Add(pR, Vector3Scale((Vector3){ mR.m8, mR.m9, mR.m10 }, 0.03f));
        DrawLine3D(pR, ax, ORANGE);
        DrawLine3D(pR, ay, GREEN);
        DrawLine3D(pR, az, SKYBLUE);
        if (avmHandL >= 0) {
            Matrix mL = MatrixMultiply(Anim_BoneMatrix(&armsVM, st, avmHandL), root);
            DrawSphere((Vector3){ mL.m12, mL.m13, mL.m14 }, 0.010f, BLUE);
        }
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
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
    // --- Fallback priority: combined rig → shared arms → gun-only OBJ ---
    // 1. Combined per-weapon rig (arms + gun + mechanical parts in one glTF).
    //    Used if data/weapons/<id>/<id>_vm.glb was loaded successfully.
    if (wi >= 0 && wi < W_COUNT && crigVM[wi].loaded) {
        DrawCombinedRigViewmodel(camera, wi);
        return;
    }
    // 2. Shared arms + gun bolted to hand.R bone (arms_vm.glb path).
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
