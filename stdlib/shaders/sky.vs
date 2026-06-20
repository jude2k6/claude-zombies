// Skybox vertex shader.  The skybox is a large cube centered on the
// camera; the fragment shader paints the sky from the view direction.
// We pass the cube's local-space vertex position through unchanged so
// the fragment side reads it as the per-pixel direction vector.

#version 330

in vec3 vertexPosition;

out vec3 fragDir;

uniform mat4 mvp;

void main()
{
    fragDir = vertexPosition;
    // Push z to the far plane so the skybox always draws *behind*
    // everything regardless of cube size.
    vec4 pos = mvp * vec4(vertexPosition, 1.0);
    gl_Position = pos.xyww;
}
