#include "gizmo.h"
#include "pick.h"
#include "debugdraw.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

// ---- small local helpers ---------------------------------------------------

// How far (in multiples of handleSize) an axis handle's pickable/drawable
// extent reaches from the gizmo origin. Shared by hit-testing and debug-draw
// so the two stay visually consistent.
#define ENG_GIZMO_REACH 4.0f

// Reach of the rotate ring, in multiples of handleSize.
#define ENG_GIZMO_RING_REACH 3.0f

static Vector3 AxisDir(EngGizmoAxis axis) {
    switch (axis) {
        case ENG_GIZMO_AXIS_X: return (Vector3){ 1.0f, 0.0f, 0.0f };
        case ENG_GIZMO_AXIS_Y: return (Vector3){ 0.0f, 1.0f, 0.0f };
        case ENG_GIZMO_AXIS_Z: return (Vector3){ 0.0f, 0.0f, 1.0f };
        default:               return (Vector3){ 0.0f, 0.0f, 0.0f };
    }
}

// Plane normal for a planar (translate) handle — the axis NOT spanned by
// the plane, e.g. XY spans X and Y so its normal is Z.
static Vector3 PlaneNormal(EngGizmoAxis axis) {
    switch (axis) {
        case ENG_GIZMO_AXIS_XY: return (Vector3){ 0.0f, 0.0f, 1.0f };
        case ENG_GIZMO_AXIS_YZ: return (Vector3){ 1.0f, 0.0f, 0.0f };
        case ENG_GIZMO_AXIS_XZ: return (Vector3){ 0.0f, 1.0f, 0.0f };
        default:                return (Vector3){ 0.0f, 0.0f, 0.0f };
    }
}

static bool IsPlanarAxis(EngGizmoAxis axis) {
    return axis == ENG_GIZMO_AXIS_XY || axis == ENG_GIZMO_AXIS_YZ || axis == ENG_GIZMO_AXIS_XZ;
}

// Shortest distance from `ray` to the point `p`, measured as the distance
// from `p` to the closest point on the ray's line (ray treated as infinite
// for hit-testing purposes — handles behind the camera are rejected by the
// distance-along-ray check at the call site instead).
static float DistanceRayToPoint(Ray ray, Vector3 p) {
    Vector3 toP = Vector3Subtract(p, ray.position);
    float t = Vector3DotProduct(toP, ray.direction); // ray.direction is unit length
    Vector3 closest = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    return Vector3Distance(closest, p);
}

// ---- pure geometry helpers --------------------------------------------

Vector3 Eng_GizmoClosestPointOnAxis(Ray ray, Vector3 linePoint, Vector3 lineDir) {
    Vector3 d = Vector3Normalize(lineDir);
    Vector3 r = Vector3Normalize(ray.direction);

    // Closest points between two infinite lines (ray origin+r, linePoint+d).
    // Standard skew-line solve (a == c == 1 since r and d are unit vectors);
    // falls back to projecting the ray origin onto the line if the two are
    // (near-)parallel, rather than dividing by ~0.
    float b = Vector3DotProduct(r, d);
    Vector3 w0 = Vector3Subtract(ray.position, linePoint);
    float e = Vector3DotProduct(r, w0);
    float f = Vector3DotProduct(d, w0);

    float denom = 1.0f - b * b;
    float tLine; // parameter along the line (linePoint + tLine * d)
    if (fabsf(denom) < 1e-8f) {
        // Parallel: any point on the ray projects to the same place on the
        // line. Use the ray origin's projection.
        tLine = f;
    } else {
        tLine = (f - b * e) / denom;
    }

    return Vector3Add(linePoint, Vector3Scale(d, tLine));
}

float Eng_GizmoAngleInPlane(Ray ray, Vector3 planePoint, Vector3 axis, Vector3 refDir, bool *outValid) {
    Vector3 n = Vector3Normalize(axis);

    Vector3 hit;
    if (!Eng_PickRayPlane(ray, planePoint, n, NULL, &hit)) {
        if (outValid) *outValid = false;
        return 0.0f;
    }

    Vector3 toHit = Vector3Subtract(hit, planePoint);
    // Project out any residual component along the axis (numerical safety;
    // toHit should already lie in-plane since hit comes from the plane test).
    toHit = Vector3Subtract(toHit, Vector3Scale(n, Vector3DotProduct(toHit, n)));
    float lenSq = Vector3DotProduct(toHit, toHit);
    if (lenSq < 1e-12f) {
        // Ray hits exactly at the pivot — no defined angle.
        if (outValid) *outValid = false;
        return 0.0f;
    }

    Vector3 refInPlane = Vector3Subtract(refDir, Vector3Scale(n, Vector3DotProduct(refDir, n)));
    if (Vector3DotProduct(refInPlane, refInPlane) < 1e-12f) {
        if (outValid) *outValid = false;
        return 0.0f;
    }
    refInPlane = Vector3Normalize(refInPlane);
    Vector3 toHitNorm = Vector3Normalize(toHit);

    // Signed angle from refInPlane to toHitNorm about n (right-hand rule):
    // atan2(|a x b| signed by n, a . b).
    Vector3 cross = Vector3CrossProduct(refInPlane, toHitNorm);
    float sinPart = Vector3DotProduct(cross, n);
    float cosPart = Vector3DotProduct(refInPlane, toHitNorm);

    if (outValid) *outValid = true;
    return atan2f(sinPart, cosPart);
}

// ---- hit testing --------------------------------------------------------

EngGizmoAxis Eng_GizmoHitTest(Camera3D cam, Vector2 screenPos, int screenW, int screenH,
                              Vector3 origin, EngGizmoMode mode, float handleSize) {
    Ray ray = Eng_PickRayFromScreen(cam, screenPos, screenW, screenH);

    EngGizmoAxis best = ENG_GIZMO_AXIS_NONE;
    float bestDist = -1.0f; // distance along ray to the best candidate's closest point

    float reach = handleSize * ENG_GIZMO_REACH;
    float pickRadius = handleSize * 0.5f;

    if (mode == ENG_GIZMO_TRANSLATE || mode == ENG_GIZMO_SCALE) {
        EngGizmoAxis axes[3] = { ENG_GIZMO_AXIS_X, ENG_GIZMO_AXIS_Y, ENG_GIZMO_AXIS_Z };
        for (int i = 0; i < 3; i++) {
            Vector3 dir = AxisDir(axes[i]);
            Vector3 closest = Eng_GizmoClosestPointOnAxis(ray, origin, dir);

            // Clamp the closest point onto the visible [origin, tip] segment.
            float t = Vector3DotProduct(Vector3Subtract(closest, origin), dir);
            t = (t < 0.0f) ? 0.0f : (t > reach ? reach : t);
            Vector3 onSegment = Vector3Add(origin, Vector3Scale(dir, t));

            float d = DistanceRayToPoint(ray, onSegment);
            if (d <= pickRadius) {
                float alongRay = Vector3DotProduct(Vector3Subtract(onSegment, ray.position), ray.direction);
                if (alongRay >= 0.0f && (bestDist < 0.0f || alongRay < bestDist)) {
                    bestDist = alongRay;
                    best = axes[i];
                }
            }
        }
    }

    if (mode == ENG_GIZMO_TRANSLATE) {
        // Planar handles: a small quad straddling the origin in each plane,
        // offset out along the two in-plane axes by a fraction of reach.
        EngGizmoAxis planes[3] = { ENG_GIZMO_AXIS_XY, ENG_GIZMO_AXIS_YZ, ENG_GIZMO_AXIS_XZ };
        float quadOffset = reach * 0.35f;
        for (int i = 0; i < 3; i++) {
            Vector3 n = PlaneNormal(planes[i]);
            Vector3 hit;
            if (!Eng_PickRayPlane(ray, origin, n, NULL, &hit)) continue;
            Vector3 local = Vector3Subtract(hit, origin);
            float dist = Vector3Length(local);
            if (dist > quadOffset * 1.8f) continue; // outside the little plane swatch

            float alongRay = Vector3Distance(ray.position, hit);
            // Planar handles yield to axis handles: only override a
            // previously-found hit if distinctly closer (0.01 world-unit
            // margin). When a cursor lies exactly on an axis that also lies
            // in a plane, the axis handle wins.
            if (bestDist < 0.0f || alongRay < bestDist - 0.01f) {
                bestDist = alongRay;
                best = planes[i];
            }
        }
    }

    if (mode == ENG_GIZMO_ROTATE) {
        EngGizmoAxis axes[3] = { ENG_GIZMO_AXIS_X, ENG_GIZMO_AXIS_Y, ENG_GIZMO_AXIS_Z };
        float ringRadius = handleSize * ENG_GIZMO_RING_REACH;
        for (int i = 0; i < 3; i++) {
            Vector3 n = AxisDir(axes[i]);
            Vector3 hit;
            if (!Eng_PickRayPlane(ray, origin, n, NULL, &hit)) continue;
            float distFromOrigin = Vector3Distance(hit, origin);
            if (fabsf(distFromOrigin - ringRadius) > pickRadius) continue;

            float alongRay = Vector3Distance(ray.position, hit);
            if (bestDist < 0.0f || alongRay < bestDist) {
                bestDist = alongRay;
                best = axes[i];
            }
        }
    }

    return best;
}

// ---- drag -----------------------------------------------------------------

EngGizmoDrag Eng_GizmoBeginDrag(Camera3D cam, Vector2 screenPos, int screenW, int screenH,
                                 Vector3 origin, EngGizmoMode mode, EngGizmoAxis axis) {
    EngGizmoDrag drag = { 0 };
    drag.mode = mode;
    drag.axis = ENG_GIZMO_AXIS_NONE;
    drag.origin = origin;

    if (axis == ENG_GIZMO_AXIS_NONE) return drag;

    Ray ray = Eng_PickRayFromScreen(cam, screenPos, screenW, screenH);

    if (mode == ENG_GIZMO_TRANSLATE && IsPlanarAxis(axis)) {
        Vector3 n = PlaneNormal(axis);
        Vector3 hit;
        if (!Eng_PickRayPlane(ray, origin, n, NULL, &hit)) return drag;
        drag.axis = axis;
        drag.grabPoint = hit;
        return drag;
    }

    if (mode == ENG_GIZMO_TRANSLATE || mode == ENG_GIZMO_SCALE) {
        Vector3 dir = AxisDir(axis);
        Vector3 closest = Eng_GizmoClosestPointOnAxis(ray, origin, dir);
        drag.axis = axis;
        drag.grabPoint = closest;
        drag.grabDistance = Vector3Distance(closest, origin);
        return drag;
    }

    if (mode == ENG_GIZMO_ROTATE) {
        Vector3 n = AxisDir(axis);
        Vector3 hit;
        if (!Eng_PickRayPlane(ray, origin, n, NULL, &hit)) return drag;
        Vector3 refDir = Vector3Subtract(hit, origin);
        if (Vector3DotProduct(refDir, refDir) < 1e-12f) return drag; // grabbed exactly at pivot
        drag.axis = axis;
        drag.grabPoint = hit;
        drag.grabRefDir = Vector3Normalize(refDir);
        return drag;
    }

    return drag;
}

EngGizmoDelta Eng_GizmoUpdateDrag(EngGizmoDrag drag, Camera3D cam, Vector2 screenPos,
                                   int screenW, int screenH) {
    EngGizmoDelta delta = { 0 };
    delta.mode = drag.mode;
    delta.scale = (Vector3){ 1.0f, 1.0f, 1.0f };

    if (drag.axis == ENG_GIZMO_AXIS_NONE) return delta;

    Ray ray = Eng_PickRayFromScreen(cam, screenPos, screenW, screenH);

    if (drag.mode == ENG_GIZMO_TRANSLATE && IsPlanarAxis(drag.axis)) {
        Vector3 n = PlaneNormal(drag.axis);
        Vector3 hit;
        if (!Eng_PickRayPlane(ray, drag.origin, n, NULL, &hit)) return delta;
        delta.translate = Vector3Subtract(hit, drag.grabPoint);
        return delta;
    }

    if (drag.mode == ENG_GIZMO_TRANSLATE) {
        Vector3 dir = AxisDir(drag.axis);
        Vector3 closest = Eng_GizmoClosestPointOnAxis(ray, drag.origin, dir);
        delta.translate = Vector3Subtract(closest, drag.grabPoint);
        return delta;
    }

    if (drag.mode == ENG_GIZMO_SCALE) {
        Vector3 dir = AxisDir(drag.axis);
        Vector3 closest = Eng_GizmoClosestPointOnAxis(ray, drag.origin, dir);
        float dist = Vector3Distance(closest, drag.origin);
        float denom = (drag.grabDistance > 1e-6f) ? drag.grabDistance : 1e-6f;
        float factor = dist / denom;
        // Sign flips if the cursor crossed to the opposite side of the axis.
        float side = Vector3DotProduct(Vector3Subtract(closest, drag.origin), dir);
        float grabSide = Vector3DotProduct(Vector3Subtract(drag.grabPoint, drag.origin), dir);
        if ((side < 0.0f) != (grabSide < 0.0f) && grabSide != 0.0f) factor = -factor;

        delta.scale = (Vector3){ 1.0f, 1.0f, 1.0f };
        if (drag.axis == ENG_GIZMO_AXIS_X) delta.scale.x = factor;
        else if (drag.axis == ENG_GIZMO_AXIS_Y) delta.scale.y = factor;
        else if (drag.axis == ENG_GIZMO_AXIS_Z) delta.scale.z = factor;
        return delta;
    }

    if (drag.mode == ENG_GIZMO_ROTATE) {
        Vector3 n = AxisDir(drag.axis);
        bool valid = false;
        float angle = Eng_GizmoAngleInPlane(ray, drag.origin, n, drag.grabRefDir, &valid);
        delta.rotateRadians = valid ? angle : 0.0f;
        return delta;
    }

    return delta;
}

// ---- optional convenience: debug-draw --------------------------------------

void Eng_GizmoDebugDraw(Vector3 origin, EngGizmoMode mode, float handleSize, EngGizmoAxis activeAxis) {
    float reach = handleSize * ENG_GIZMO_REACH;
    Color axisColor[3] = { RED, GREEN, BLUE };
    EngGizmoAxis axes[3] = { ENG_GIZMO_AXIS_X, ENG_GIZMO_AXIS_Y, ENG_GIZMO_AXIS_Z };

    if (mode == ENG_GIZMO_TRANSLATE || mode == ENG_GIZMO_SCALE) {
        for (int i = 0; i < 3; i++) {
            Color c = (axes[i] == activeAxis) ? YELLOW : axisColor[i];
            Vector3 tip = Vector3Add(origin, Vector3Scale(AxisDir(axes[i]), reach));
            Eng_DebugLine(origin, tip, c);
        }
    }

    if (mode == ENG_GIZMO_TRANSLATE) {
        EngGizmoAxis planes[3] = { ENG_GIZMO_AXIS_XY, ENG_GIZMO_AXIS_YZ, ENG_GIZMO_AXIS_XZ };
        float off = reach * 0.35f;
        for (int i = 0; i < 3; i++) {
            Color c = (planes[i] == activeAxis) ? YELLOW : axisColor[i];
            // Build the two in-plane axes from the plane normal.
            Vector3 n = PlaneNormal(planes[i]);
            Vector3 u = (fabsf(n.x) < 0.9f) ? Vector3CrossProduct(n, (Vector3){ 1, 0, 0 })
                                             : Vector3CrossProduct(n, (Vector3){ 0, 1, 0 });
            u = Vector3Normalize(u);
            Vector3 v = Vector3Normalize(Vector3CrossProduct(n, u));

            Vector3 p0 = Vector3Add(origin, Vector3Scale(u, off));
            Vector3 p1 = Vector3Add(origin, Vector3Scale(v, off));
            Vector3 p2 = Vector3Add(origin, Vector3Add(Vector3Scale(u, off), Vector3Scale(v, off)));
            Eng_DebugLine(p0, p2, c);
            Eng_DebugLine(p1, p2, c);
        }
    }

    if (mode == ENG_GIZMO_ROTATE) {
        float ringRadius = handleSize * ENG_GIZMO_RING_REACH;
        const int segs = 32;
        for (int i = 0; i < 3; i++) {
            Color c = (axes[i] == activeAxis) ? YELLOW : axisColor[i];
            Vector3 n = AxisDir(axes[i]);
            Vector3 u = (fabsf(n.x) < 0.9f) ? Vector3CrossProduct(n, (Vector3){ 1, 0, 0 })
                                             : Vector3CrossProduct(n, (Vector3){ 0, 1, 0 });
            u = Vector3Normalize(u);
            Vector3 v = Vector3Normalize(Vector3CrossProduct(n, u));

            Vector3 prev = Vector3Add(origin, Vector3Scale(u, ringRadius));
            for (int s = 1; s <= segs; s++) {
                float t = (float)s / (float)segs * 2.0f * PI;
                Vector3 p = Vector3Add(origin, Vector3Add(Vector3Scale(u, cosf(t) * ringRadius),
                                                            Vector3Scale(v, sinf(t) * ringRadius)));
                Eng_DebugLine(prev, p, c);
                prev = p;
            }
        }
    }
}
