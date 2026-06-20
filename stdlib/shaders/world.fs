// World fragment shader: texture * tint * vertexColor, plus a directional
// light (moon) + ambient sky term, plus linear-distance fog. Normal is
// taken from screen-space derivatives of the world-space position — flat
// shading that works regardless of whether the source mesh has per-vertex
// normals, so the same program lights both authored Models and rlgl
// immediate-mode walls/floor.
//
// When `tileVariation > 0.0` we sample the diffuse texture via Iñigo
// Quilez's hash-perturbed multi-sample blend (method 1 from
// https://iquilezles.org/articles/texturerepetition/) so a single 1k
// texture stops looking like a 10×10 grid of identical copies across the
// arena floor. Costs 4 texture samples per fragment but only on the
// tiled wall/floor draws.

#version 330

in vec2  fragTexCoord;
in vec4  fragColor;
in vec3  fragWorldPos;
in float fragViewDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec4  fogColor;
uniform float fogStart;
uniform float fogEnd;

uniform vec3 sunDir;        // direction the light travels (unit)
uniform vec3 sunColor;      // moon colour
uniform vec3 ambientColor;  // ambient/sky-bounce floor

uniform float tileVariation;

// IQ's recommended 2-d → 4-d hash (cheap, decent quality for this purpose).
vec4 hash4(vec2 p) {
    return fract(sin(vec4(1.0 + dot(p, vec2(37.0, 17.0)),
                          2.0 + dot(p, vec2(11.0, 47.0)),
                          3.0 + dot(p, vec2(41.0, 29.0)),
                          4.0 + dot(p, vec2(23.0, 31.0)))) * 103.0);
}

// IQ "textureNoTile" method 1: at each integer UV cell, derive a random
// offset + ±1 mirror; sample the four neighbouring cells at their offset
// UVs (via textureGrad so mipmaps stay correct); blend with smoothstep.
// The mirrored sampling kills shape-level repetition; the smoothstep
// blends out the boundaries.
vec4 textureNoTile(sampler2D samp, vec2 uv) {
    vec2 iuv = floor(uv);
    vec2 fuv = fract(uv);

    vec4 ofa = hash4(iuv + vec2(0.0, 0.0));
    vec4 ofb = hash4(iuv + vec2(1.0, 0.0));
    vec4 ofc = hash4(iuv + vec2(0.0, 1.0));
    vec4 ofd = hash4(iuv + vec2(1.0, 1.0));

    vec2 ddx = dFdx(uv);
    vec2 ddy = dFdy(uv);

    ofa.zw = sign(ofa.zw - 0.5);
    ofb.zw = sign(ofb.zw - 0.5);
    ofc.zw = sign(ofc.zw - 0.5);
    ofd.zw = sign(ofd.zw - 0.5);

    vec2 uva = uv*ofa.zw + ofa.xy, ddxa = ddx*ofa.zw, ddya = ddy*ofa.zw;
    vec2 uvb = uv*ofb.zw + ofb.xy, ddxb = ddx*ofb.zw, ddyb = ddy*ofb.zw;
    vec2 uvc = uv*ofc.zw + ofc.xy, ddxc = ddx*ofc.zw, ddyc = ddy*ofc.zw;
    vec2 uvd = uv*ofd.zw + ofd.xy, ddxd = ddx*ofd.zw, ddyd = ddy*ofd.zw;

    vec2 b = smoothstep(0.25, 0.75, fuv);
    return mix(mix(textureGrad(samp, uva, ddxa, ddya),
                   textureGrad(samp, uvb, ddxb, ddyb), b.x),
               mix(textureGrad(samp, uvc, ddxc, ddyc),
                   textureGrad(samp, uvd, ddxd, ddyd), b.x),
               b.y);
}

out vec4 finalColor;

void main()
{
    vec4 tex;
    if (tileVariation > 0.0) {
        tex = textureNoTile(texture0, fragTexCoord);
    } else {
        tex = texture(texture0, fragTexCoord);
    }
    vec4 base = tex * colDiffuse * fragColor;

    // Flat per-face normal from world-space derivatives. For degenerate
    // pixels (e.g. razor-thin triangles) fall back to straight-up.
    vec3 dx = dFdx(fragWorldPos);
    vec3 dy = dFdy(fragWorldPos);
    vec3 N  = cross(dx, dy);
    float Nlen = length(N);
    N = (Nlen > 1e-6) ? (N / Nlen) : vec3(0.0, 1.0, 0.0);

    // Lambertian against the moon. -sunDir = direction toward the light.
    float lambert = max(dot(N, -normalize(sunDir)), 0.0);
    vec3 lit = ambientColor + sunColor * lambert;

    vec3 rgb = base.rgb * lit;

    float t = clamp((fragViewDist - fogStart) / max(fogEnd - fogStart, 0.001),
                    0.0, 1.0);
    rgb = mix(rgb, fogColor.rgb, t);

    finalColor = vec4(rgb, base.a);
}
