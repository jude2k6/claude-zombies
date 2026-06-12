// Post-process fragment shader — runs on a fullscreen quad drawn over the
// render target that captured the 3D world pass. Applied before the HUD.
//
// Effects:
//   1. Bloom  — cheap single-pass: samples a ring of 12 taps at ~3 px offset,
//               thresholds bright pixels (luminance > 0.55), adds blurred
//               bright contribution scaled to ~0.32. Targets PaP purple glow,
//               raygun coils, muzzle-flash, tracers. Subtle — dark night game.
//   2. Vignette — gentle radial darkening toward corners, always on.
//   3. Hit-flash — uniform hitFlash (0..1): red tint + slightly stronger
//               vignette when the local player just took damage.
//   4. Low-HP pulse — uniform lowHp (0..1, rises as HP falls below ~40% max):
//               slow heartbeat-period desaturation / red-edge pulse using
//               `time`. While fully downed (lowHp == 1.0) the effect is strong.

#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0;    // raylib RT texture unit
uniform vec2  resolution;      // current render size (pixels)
uniform float time;            // GetTime() seconds
uniform float hitFlash;        // 0..1, spikes on HP drop, decays ~0.35 s
uniform float lowHp;           // 0..1 as HP falls below 40% max; 1 = downed

out vec4 finalColor;

// ----- helpers ---------------------------------------------------------------

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// ----- bloom (single-pass, 12-tap ring + 4-tap cross) -----------------------
vec3 BloomContrib(vec2 uv, vec2 texelSize) {
    // 12 taps on a ring at radius ~3 px, plus 4-tap cross at 2 px
    vec3 acc = vec3(0.0);
    float r3 = 3.0;
    float r2 = 2.0;

    // ring taps (every 30 deg = 12 taps)
    for (int i = 0; i < 12; i++) {
        float a = float(i) * (3.14159265 / 6.0);
        vec2 off = vec2(cos(a), sin(a)) * r3 * texelSize;
        vec3 s = texture(texture0, uv + off).rgb;
        float lum = luminance(s);
        // threshold: only bright pixels contribute
        float bright = max(lum - 0.55, 0.0) / max(1.0 - 0.55, 0.001);
        acc += s * bright;
    }
    acc /= 12.0;

    // 4-tap cross at 2 px for a tight inner glow
    vec3 cross4 = vec3(0.0);
    cross4 += texture(texture0, uv + vec2( r2, 0.0) * texelSize).rgb;
    cross4 += texture(texture0, uv + vec2(-r2, 0.0) * texelSize).rgb;
    cross4 += texture(texture0, uv + vec2(0.0,  r2) * texelSize).rgb;
    cross4 += texture(texture0, uv + vec2(0.0, -r2) * texelSize).rgb;
    cross4 /= 4.0;
    {
        float lum = luminance(cross4);
        float bright = max(lum - 0.55, 0.0) / max(1.0 - 0.55, 0.001);
        cross4 *= bright;
    }

    return acc * 0.28 + cross4 * 0.08;
}

// ----- vignette --------------------------------------------------------------
float Vignette(vec2 uv, float strength) {
    // 0 at centre, 1 at corners. Smooth quintic.
    vec2 d = uv - 0.5;
    float r = dot(d, d) * 2.0;          // 0 centre, 1 at corner (approx)
    r = clamp(r, 0.0, 1.0);
    float v = r * r * (3.0 - 2.0 * r);  // smoothstep(0,1,r)
    return v * strength;
}

// ----- main ------------------------------------------------------------------
void main() {
    vec2 uv = fragTexCoord;
    vec2 texelSize = 1.0 / resolution;

    // Base scene colour
    vec3 col = texture(texture0, uv).rgb;

    // --- Bloom ---
    vec3 bloom = BloomContrib(uv, texelSize);
    col += bloom * 0.32;

    // --- Vignette ---
    // Base vignette strength: subtle (0.38). Bumped by hit-flash and low-HP.
    float vigStr = 0.38
                 + hitFlash  * 0.30
                 + lowHp     * 0.22;
    float vig = Vignette(uv, vigStr);
    col *= (1.0 - vig);

    // --- Hit-flash ---
    // Red tint over the whole frame, strongest at edges.
    if (hitFlash > 0.001) {
        // Edge-biased red overlay: stronger at vignette radius
        vec2 d = uv - 0.5;
        float edgeBias = clamp(dot(d, d) * 4.0, 0.0, 1.0);
        float flashAmt = hitFlash * mix(0.18, 0.55, edgeBias);
        col = mix(col, vec3(1.0, 0.05, 0.05), flashAmt);
    }

    // --- Low-HP heartbeat desaturation + red-edge pulse ---
    if (lowHp > 0.001) {
        // Heartbeat: two peaks per period (~1.6 s). Driven by time.
        // double-bump pattern: sin(t) + 0.45*sin(2t) gives two spikes per cycle
        float period = 1.6;
        float phase = mod(time, period) / period * (2.0 * 3.14159265);
        float hb = sin(phase) + 0.45 * sin(2.0 * phase);
        hb = clamp(hb * 0.5 + 0.5, 0.0, 1.0);   // 0..1

        // Desaturation: lerp toward grey
        float lum = luminance(col);
        vec3 grey = vec3(lum);
        float desat = lowHp * mix(0.30, 0.70, hb);
        col = mix(col, grey, desat);

        // Red-edge pulse: tint corners red proportional to heartbeat * lowHp
        {
            vec2 d2 = uv - 0.5;
            float edgeR = clamp(dot(d2, d2) * 3.5, 0.0, 1.0);
            float pulseAmt = lowHp * hb * edgeR * 0.45;
            col = mix(col, vec3(0.8, 0.0, 0.0), pulseAmt);
        }
    }

    finalColor = vec4(col, 1.0);
}
