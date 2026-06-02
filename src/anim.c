#include "anim.h"
#include "raymath.h"
#include <stdio.h>
#include <string.h>

bool Anim_Load(AnimModel *am, const char *file) {
    memset(am, 0, sizeof *am);
    am->anims = NULL;

    // Same resolution strategy as the rest of the asset loaders: try the
    // project root and the build/ layout so it works from either CWD.
    static const char *prefixes[] = {
        "", "data/models/", "../data/models/", "./data/models/",
        "data/", "../data/",
    };
    char path[512] = {0};
    bool found = false;
    for (size_t p = 0; p < sizeof prefixes / sizeof prefixes[0]; p++) {
        snprintf(path, sizeof path, "%s%s", prefixes[p], file);
        if (FileExists(path)) { found = true; break; }
    }
    if (!found) {
        fprintf(stderr, "anim: '%s' not found in any data path\n", file);
        return false;
    }

    am->model = LoadModel(path);
    if (am->model.meshCount == 0) {
        fprintf(stderr, "anim: '%s' loaded no meshes\n", path);
        return false;
    }
    am->anims = LoadModelAnimations(path, &am->animCount);
    am->loaded = true;
    snprintf(am->name, sizeof am->name, "%s", GetFileName(path));

    int bones = (am->model.meshCount > 0) ? am->model.meshes[0].boneCount : 0;
    fprintf(stderr, "anim: loaded %s (%d mesh, %d mat, %d bones, %d clips)\n",
            path, am->model.meshCount, am->model.materialCount, bones, am->animCount);
    for (int i = 0; i < am->animCount; i++) {
        fprintf(stderr, "  clip[%d] '%s'  %d frames (%.2fs)\n",
                i, am->anims[i].name, am->anims[i].frameCount,
                am->anims[i].frameCount / ANIM_FPS);
        if (!IsModelAnimationValid(am->model, am->anims[i]))
            fprintf(stderr, "  WARNING: clip[%d] skeleton does not match model\n", i);
    }
    return true;
}

void Anim_Unload(AnimModel *am) {
    if (!am->loaded) return;
    if (am->anims) UnloadModelAnimations(am->anims, am->animCount);
    UnloadModel(am->model);
    memset(am, 0, sizeof *am);
}

void Anim_ApplyShader(AnimModel *am, Shader sh) {
    if (!am->loaded) return;
    for (int m = 0; m < am->model.materialCount; m++)
        am->model.materials[m].shader = sh;
}

int Anim_FindClip(const AnimModel *am, const char *name) {
    if (!am->loaded || !name) return -1;
    for (int i = 0; i < am->animCount; i++)
        if (strcmp(am->anims[i].name, name) == 0) return i;
    return -1;
}

float Anim_ClipDuration(const AnimModel *am, int clip) {
    if (!am->loaded || clip < 0 || clip >= am->animCount) return 0.0f;
    return am->anims[clip].frameCount / ANIM_FPS;
}

void Anim_Play(AnimState *st, int clip, bool loop, float speed) {
    st->clip     = clip;
    st->time     = 0.0f;
    st->speed    = speed;
    st->loop     = loop;
    st->finished = false;
}

void Anim_Update(const AnimModel *am, AnimState *st, float dt) {
    if (!am->loaded || st->clip < 0 || st->clip >= am->animCount) return;
    float dur = Anim_ClipDuration(am, st->clip);
    if (dur <= 0.0f) return;
    st->time += dt * st->speed;
    if (st->loop) {
        st->time = fmodf(st->time, dur);
        if (st->time < 0.0f) st->time += dur;
    } else if (st->time >= dur) {
        st->time = dur;
        st->finished = true;
    }
}

void Anim_Draw(AnimModel *am, AnimState *st, Vector3 pos, float yawDeg,
               float scale, Color tint) {
    if (!am->loaded) return;
    if (st->clip >= 0 && st->clip < am->animCount) {
        ModelAnimation a = am->anims[st->clip];
        if (a.frameCount > 0) {
            int frame = (int)(st->time * ANIM_FPS);
            if (frame >= a.frameCount) frame = st->loop ? (frame % a.frameCount)
                                                        : (a.frameCount - 1);
            if (frame < 0) frame = 0;
            // GPU skinning: writes per-instance bone matrices into the mesh,
            // which DrawMesh uploads to the boneMatrices uniform. Does NOT
            // re-upload vertex buffers, so it's cheap per instance.
            UpdateModelAnimationBones(am->model, a, frame);
        }
    }
    DrawModelEx(am->model, pos, (Vector3){ 0, 1, 0 }, yawDeg,
                (Vector3){ scale, scale, scale }, tint);
}
