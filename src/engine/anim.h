#ifndef SHOOTER_ANIM_H
#define SHOOTER_ANIM_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Skeletal animation pipeline (glTF / .glb via raylib 5.5 GPU skinning).
//
//  An AnimModel is a *shared* asset: one loaded skinned Model plus its array
//  of animation clips. Many instances (e.g. every zombie) reference the same
//  AnimModel; each instance carries its own lightweight AnimState (which clip,
//  how far into it). Because GPU skinning only needs per-instance bone
//  matrices (computed cheaply on the CPU by UpdateModelAnimationBones and
//  uploaded as a uniform in DrawMesh), one AnimModel can be drawn at many
//  different poses in a single frame without touching the vertex buffers.
//
//  Usage:
//    AnimModel zm; Anim_Load(&zm, "zombie.glb");
//    Anim_ApplyShader(&zm, worldSkinnedShader);     // lit + fogged
//    int walk = Anim_FindClip(&zm, "walk");
//    ... per instance: AnimState st; Anim_Play(&st, walk, true, 1.0f);
//    ... each frame: Anim_Update(&zm, &st, dt);
//    ...            Anim_Draw(&zm, &st, pos, yawDeg, scale, WHITE);
// ============================================================================

typedef struct {
    Model           model;
    ModelAnimation *anims;
    int             animCount;
    bool            loaded;
    char            name[64];   // basename, for diagnostics
} AnimModel;

typedef struct {
    int   clip;      // index into AnimModel.anims, or -1 for static bind pose
    float time;      // seconds elapsed in the current clip
    float speed;     // playback rate multiplier
    bool  loop;
    bool  finished;  // set true the frame a non-looping clip reaches its end
} AnimState;

// glTF clips are baked by raylib at ~60 fps (GLTF_ANIMDELAY = 17 ms).
#define ANIM_FPS (1000.0f / 17.0f)

// Loads `file` (a .glb/.gltf) trying the usual data/ path prefixes. Loads the
// model + all its animation clips. Returns true on success; logs a summary
// (mesh/material/bone/clip counts + clip names) to stderr either way.
bool Anim_Load(AnimModel *am, const char *file);
void Anim_Unload(AnimModel *am);

// Point every material at the given shader (use worldSkinnedShader so animated
// models get the same lighting + fog as the rest of the world).
void Anim_ApplyShader(AnimModel *am, Shader sh);

// Returns the clip index whose name matches (case-sensitive), or -1.
int  Anim_FindClip(const AnimModel *am, const char *name);

// Duration of a clip in seconds (0 if invalid).
float Anim_ClipDuration(const AnimModel *am, int clip);

// (Re)start a clip from t=0. clip < 0 selects the static bind pose.
void Anim_Play(AnimState *st, int clip, bool loop, float speed);

// Advance playback time. For non-looping clips, clamps at the end and sets
// `finished`. Cheap — does no posing.
void Anim_Update(const AnimModel *am, AnimState *st, float dt);

// Pose the shared model to this instance's current frame and draw it at the
// given transform. Safe to call many times per frame with different states.
void Anim_Draw(AnimModel *am, AnimState *st, Vector3 pos, float yawDeg,
               float scale, Color tint);

// Pose the shared model to this instance's current frame WITHOUT drawing.
// Use when the caller needs a custom world transform (e.g. a camera-space
// first-person viewmodel): call Anim_Pose, set `am->model.transform`, then
// DrawModel(am->model, ...).
void Anim_Pose(AnimModel *am, AnimState *st);

// Index of the named bone in the model (or -1). Used to attach a separate
// object (e.g. a gun) to a skeleton bone.
int Anim_FindBone(const AnimModel *am, const char *name);

// Model-space transform of bone `boneIdx` at the AnimState's current frame.
// Compose with the model's own transform to get the bone's world matrix, then
// post-multiply a local offset to place an attached object in the bone's space.
Matrix Anim_BoneMatrix(const AnimModel *am, const AnimState *st, int boneIdx);

#endif // SHOOTER_ANIM_H
