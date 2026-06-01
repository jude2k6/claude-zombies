// Procedural night sky: horizon-to-zenith gradient + hash-based star
// field. No texture asset required. Direction comes in unnormalised
// from the skybox cube vertex; we normalise per-pixel so the gradient
// follows the actual view ray.

#version 330

in vec3 fragDir;

out vec4 finalColor;

// 3D hash → [0,1).  Cheap, stable, good enough for sparse stars.
float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

void main()
{
    vec3 dir = normalize(fragDir);

    // Vertical gradient: -1 (down) → 0 (horizon) → +1 (zenith).
    float h = dir.y;

    // Sky colours: tuned to feel like the inside of a Nacht map at
    // night without competing with the spotlit gameplay.
    vec3 cZenith  = vec3(0.020, 0.030, 0.075);  // deep indigo
    vec3 cHorizon = vec3(0.060, 0.055, 0.095);  // faint purple band
    vec3 cGround  = vec3(0.010, 0.010, 0.018);  // near-black below
    // Mix up: horizon → zenith over [0, 1]; below 0 we crossfade into
    // the ground colour over [-0.15, 0] so the seam doesn't pop.
    float tUp   = clamp(h * 1.4, 0.0, 1.0);
    float tDown = clamp(-h / 0.15, 0.0, 1.0);
    vec3 sky = mix(cHorizon, cZenith, tUp);
    sky = mix(sky, cGround, tDown);

    // A subtle horizon glow so the band reads as light leaking from
    // somewhere off-map (matches the moon-from-the-side vibe).
    float glow = exp(-abs(h) * 24.0) * 0.10;
    sky += vec3(0.20, 0.14, 0.08) * glow;

    // Stars: sparse, only above the horizon.  Snap direction to a grid
    // so each "star" is a stable point in space rather than crawling
    // when the camera moves.
    if (h > 0.02) {
        vec3 grid = floor(dir * 320.0);
        float r = hash13(grid);
        // 0.997 cutoff → ~0.3% of cells become stars.  Brightness varies
        // slightly per-star.
        float star = smoothstep(0.997, 0.999, r);
        float bright = mix(0.5, 1.0, hash13(grid + 17.0));
        sky += vec3(star * bright);
    }

    finalColor = vec4(sky, 1.0);
}
