// World vertex shader. Matches raylib's default attribute layout so it
// drops in as a replacement on every loaded Model and on rlgl's default
// shader. Forwards world-space position so the fragment shader can derive
// a flat normal via screen-space derivatives — uniform across loaded
// Models (which carry per-vertex normals) and rlgl immediate-mode quads
// (which don't).

#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2  fragTexCoord;
out vec4  fragColor;
out vec3  fragWorldPos;
out float fragViewDist;

uniform mat4 mvp;
uniform mat4 matModel;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;

    // matModel is identity for rlgl immediate-mode batches (verts are
    // already world-space) and is the loaded Model's transform for
    // DrawModel calls — so this gives world-space pos in both cases.
    fragWorldPos = (matModel * vec4(vertexPosition, 1.0)).xyz;

    vec4 clip = mvp * vec4(vertexPosition, 1.0);
    gl_Position  = clip;
    fragViewDist = clip.w;
}
