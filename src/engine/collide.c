#include "collide.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

// ---- small local helpers ---------------------------------------------------

static Vector3 BoxCenter(BoundingBox b) {
    return (Vector3){ (b.min.x + b.max.x) * 0.5f,
                      (b.min.y + b.max.y) * 0.5f,
                      (b.min.z + b.max.z) * 0.5f };
}

// Closest point to `p` on the segment a..b.
static Vector3 ClosestOnSegment(Vector3 p, Vector3 a, Vector3 b) {
    Vector3 ab = Vector3Subtract(b, a);
    float denom = Vector3DotProduct(ab, ab);
    if (denom < 1e-12f) return a;                       // degenerate segment
    float t = Vector3DotProduct(Vector3Subtract(p, a), ab) / denom;
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Vector3Add(a, Vector3Scale(ab, t));
}

// ---- static overlap --------------------------------------------------------

bool Eng_CollideAABBOverlap(BoundingBox a, BoundingBox b) {
    return (a.min.x < b.max.x && a.max.x > b.min.x) &&
           (a.min.y < b.max.y && a.max.y > b.min.y) &&
           (a.min.z < b.max.z && a.max.z > b.min.z);
}

bool Eng_CollideAABBPenetration(BoundingBox a, BoundingBox b, Vector3 *outMTV) {
    float ox = fminf(a.max.x, b.max.x) - fmaxf(a.min.x, b.min.x);
    if (ox <= 0.0f) return false;
    float oy = fminf(a.max.y, b.max.y) - fmaxf(a.min.y, b.min.y);
    if (oy <= 0.0f) return false;
    float oz = fminf(a.max.z, b.max.z) - fmaxf(a.min.z, b.min.z);
    if (oz <= 0.0f) return false;

    Vector3 ca = BoxCenter(a), cb = BoxCenter(b);
    Vector3 mtv = { 0, 0, 0 };
    // Resolve along the axis of least penetration.
    if (ox <= oy && ox <= oz)      mtv.x = (ca.x < cb.x) ? -ox : ox;
    else if (oy <= ox && oy <= oz) mtv.y = (ca.y < cb.y) ? -oy : oy;
    else                           mtv.z = (ca.z < cb.z) ? -oz : oz;

    if (outMTV) *outMTV = mtv;
    return true;
}

bool Eng_CollideSphereAABB(Vector3 c, float r, BoundingBox box, Vector3 *outPush) {
    // Closest point on the box to the sphere centre.
    Vector3 q = {
        fmaxf(box.min.x, fminf(c.x, box.max.x)),
        fmaxf(box.min.y, fminf(c.y, box.max.y)),
        fmaxf(box.min.z, fminf(c.z, box.max.z)),
    };
    Vector3 d = Vector3Subtract(c, q);
    float d2 = Vector3DotProduct(d, d);

    if (d2 > r * r) return false;

    if (d2 > 1e-12f) {
        // Centre outside the box: push out along the surface direction.
        float dist = sqrtf(d2);
        if (outPush) *outPush = Vector3Scale(d, (r - dist) / dist);
        return true;
    }

    // Centre inside the box: push out the nearest face, plus the radius.
    float dxMin = c.x - box.min.x, dxMax = box.max.x - c.x;
    float dyMin = c.y - box.min.y, dyMax = box.max.y - c.y;
    float dzMin = c.z - box.min.z, dzMax = box.max.z - c.z;
    float best = dxMin; Vector3 push = { -(dxMin + r), 0, 0 };
    if (dxMax < best) { best = dxMax; push = (Vector3){ dxMax + r, 0, 0 }; }
    if (dyMin < best) { best = dyMin; push = (Vector3){ 0, -(dyMin + r), 0 }; }
    if (dyMax < best) { best = dyMax; push = (Vector3){ 0, dyMax + r, 0 }; }
    if (dzMin < best) { best = dzMin; push = (Vector3){ 0, 0, -(dzMin + r) }; }
    if (dzMax < best) { best = dzMax; push = (Vector3){ 0, 0, dzMax + r }; }
    if (outPush) *outPush = push;
    return true;
}

bool Eng_CollideCapsuleAABB(Vector3 p0, Vector3 p1, float r, BoundingBox box,
                            Vector3 *outPush) {
    Vector3 s = ClosestOnSegment(BoxCenter(box), p0, p1);
    return Eng_CollideSphereAABB(s, r, box, outPush);
}

// ---- swept -----------------------------------------------------------------

bool Eng_CollideSweptAABB(BoundingBox moving, Vector3 vel, BoundingBox staticBox,
                          float *outT, Vector3 *outNormal) {
    // Minkowski: expand the static box by the mover's half-extents, then sweep
    // the mover's centre (a point) through it as a ray with direction `vel`.
    Vector3 half = {
        (moving.max.x - moving.min.x) * 0.5f,
        (moving.max.y - moving.min.y) * 0.5f,
        (moving.max.z - moving.min.z) * 0.5f,
    };
    Vector3 o = BoxCenter(moving);
    BoundingBox e = {
        { staticBox.min.x - half.x, staticBox.min.y - half.y, staticBox.min.z - half.z },
        { staticBox.max.x + half.x, staticBox.max.y + half.y, staticBox.max.z + half.z },
    };

    const float v[3]    = { vel.x, vel.y, vel.z };
    const float op[3]   = { o.x, o.y, o.z };
    const float emin[3] = { e.min.x, e.min.y, e.min.z };
    const float emax[3] = { e.max.x, e.max.y, e.max.z };

    float tnear = -INFINITY, tfar = INFINITY;
    int   hitAxis = -1;
    float hitSign = 0.0f;

    for (int a = 0; a < 3; a++) {
        if (fabsf(v[a]) < 1e-8f) {
            // Parallel to this slab: a miss if the origin is outside it.
            if (op[a] < emin[a] || op[a] > emax[a]) return false;
        } else {
            float inv = 1.0f / v[a];
            float t1 = (emin[a] - op[a]) * inv;
            float t2 = (emax[a] - op[a]) * inv;
            float sign = -1.0f;                  // entered the min face (-axis normal)
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; sign = 1.0f; }
            if (t1 > tnear) { tnear = t1; hitAxis = a; hitSign = sign; }
            if (t2 < tfar)  tfar = t2;
            if (tnear > tfar) return false;
        }
    }

    if (tfar < 0.0f)  return false;   // static box is entirely behind the mover
    if (tnear > 1.0f) return false;   // mover doesn't reach it this frame
    if (tnear < 0.0f) tnear = 0.0f;   // already overlapping at start → TOI 0

    if (outT) *outT = tnear;
    if (outNormal) {
        Vector3 n = { 0, 0, 0 };
        if (hitAxis == 0) n.x = hitSign;
        else if (hitAxis == 1) n.y = hitSign;
        else if (hitAxis == 2) n.z = hitSign;
        *outNormal = n;
    }
    return true;
}
