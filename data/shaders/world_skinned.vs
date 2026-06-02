// Skinned world vertex shader. GPU skeletal skinning that produces the exact
// same varyings as world.vs, so it pairs with the unchanged world.fs lighting
// + fog fragment shader. raylib binds `vertexBoneIds`/`vertexBoneWeights` to
// the right attribute slots and uploads `boneMatrices` automatically in
// DrawMesh when the mesh carries bone data (set via UpdateModelAnimationBones).
//
// The fragment shader derives its normal from screen-space derivatives of
// fragWorldPos, so we don't need to skin normals here — only position.

#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
in vec4 vertexBoneIds;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 matModel;

#define MAX_BONE_NUM 128
uniform mat4 boneMatrices[MAX_BONE_NUM];

out vec2  fragTexCoord;
out vec4  fragColor;
out vec3  fragWorldPos;
out float fragViewDist;

void main()
{
    int b0 = int(vertexBoneIds.x);
    int b1 = int(vertexBoneIds.y);
    int b2 = int(vertexBoneIds.z);
    int b3 = int(vertexBoneIds.w);

    // Weighted blend of the four influencing bones (bind-pose-relative
    // matrices produced by UpdateModelAnimationBones, in model-local space).
    mat4 skin = boneMatrices[b0] * vertexBoneWeights.x +
                boneMatrices[b1] * vertexBoneWeights.y +
                boneMatrices[b2] * vertexBoneWeights.z +
                boneMatrices[b3] * vertexBoneWeights.w;

    vec4 localPos = skin * vec4(vertexPosition, 1.0);

    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragWorldPos = (matModel * localPos).xyz;

    vec4 clip    = mvp * localPos;
    gl_Position  = clip;
    fragViewDist = clip.w;
}
