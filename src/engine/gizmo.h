#ifndef SHOOTER_GIZMO_H
#define SHOOTER_GIZMO_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Engine gizmo — axis-constrained transform-handle interaction math.
//
//  This is a *substrate*, not editor policy. It answers "which handle is the
//  cursor over" and "given mouse motion since grab, what constrained delta
//  results" — it never owns the selection, the MapDoc, or the camera. Same
//  split as pick.h (ray math in the engine, what-it-hits/what-it-edits in
//  the game): the editor decides what's selected and how to apply a delta
//  to a transform; this module only turns mouse movement into a number.
//
//  All functions are pure — no globals, no malloc, no hidden state. Hit
//  testing takes a Camera3D + screen pos and builds its own ray via
//  Eng_PickRayFromScreen (pick.h); everything past that is plain vector
//  math against a caller-supplied gizmo origin, so the geometry helpers
//  (closest point on an axis line, signed angle in a plane) are testable
//  with hand-built Rays and no camera at all.
//
//  DRAG CONVENTION — read this before wiring a caller
//  ----------------------------------------------------
//  Eng_GizmoBeginDrag captures the grab state (active handle, gizmo origin
//  at grab time, initial ray-vs-axis projection). Every subsequent
//  Eng_GizmoUpdateDrag call takes the *current* mouse ray and returns the
//  delta ACCUMULATED SINCE GRAB, not a per-frame increment. The caller is
//  expected to recompute the dragged transform from the ORIGINAL (pre-drag)
//  transform plus this delta every frame:
//
//      newPos = originalPos + delta.translate;
//      newRot = originalRot * RotateByAxisAngle(axis, delta.rotateRadians);
//      newScale = originalScale * delta.scale;
//
//  Re-deriving from the original value each frame (rather than integrating
//  a per-frame delta onto a running value) avoids drift from accumulated
//  floating-point error over a long drag. Only one of the delta's three
//  shapes (translate / rotateRadians / scale) is meaningful per drag,
//  selected by `drag.mode`.
//
//  SPACE
//  -----
//  Everything here is world space. Gizmo orientation is the world axes
//  (X/Y/Z) — there is no per-object local-space gizmo in this primitive;
//  a game wanting local-space handles rotates the ray/origin into local
//  space before calling in, and rotates the resulting delta back out.
// ============================================================================

// ---- Modes & handles --------------------------------------------------

// What kind of transform a drag produces.
typedef enum EngGizmoMode {
    ENG_GIZMO_TRANSLATE,
    ENG_GIZMO_ROTATE,
    ENG_GIZMO_SCALE,
} EngGizmoMode;

// Which handle is hovered/grabbed. The three planar handles (_XY/_YZ/_XZ)
// are translate-only conveniences (drag confined to a plane instead of a
// line); rotate and scale only ever use the single-axis values.
typedef enum EngGizmoAxis {
    ENG_GIZMO_AXIS_NONE,
    ENG_GIZMO_AXIS_X,
    ENG_GIZMO_AXIS_Y,
    ENG_GIZMO_AXIS_Z,
    ENG_GIZMO_AXIS_XY,
    ENG_GIZMO_AXIS_YZ,
    ENG_GIZMO_AXIS_XZ,
} EngGizmoAxis;

// ---- Pure geometry helpers (standalone-testable, no camera needed) ----

// Closest point to `ray` on the infinite line through `linePoint` along
// unit/non-unit `lineDir`. This is the core of axis-handle hit testing and
// of translate dragging: project the mouse ray onto the constraint line.
// Handles near-parallel ray/line pairs (returns the line point closest to
// the ray origin's projection rather than dividing by ~0).
Vector3 Eng_GizmoClosestPointOnAxis(Ray ray, Vector3 linePoint, Vector3 lineDir);

// Signed angle (radians) from `refDir` to the direction of `ray`'s
// intersection with the ray-vs-axis line, both measured in the plane
// through `planePoint` perpendicular to `axis`. Positive = counter-clockwise
// looking down `axis` toward its origin (right-hand rule). Used by rotate
// dragging: the angle of the current cursor projection relative to the
// angle at grab time. Returns 0 if the ray is parallel to the plane.
float Eng_GizmoAngleInPlane(Ray ray, Vector3 planePoint, Vector3 axis, Vector3 refDir, bool *outValid);

// ---- Hit testing --------------------------------------------------------

// Which handle (if any) is under the cursor. Builds the pick ray itself
// (Eng_PickRayFromScreen) from `screenPos`/`screenW`/`screenH` under `cam`,
// then tests every handle implied by `mode` against a cylinder of
// `handleSize` world units around each axis line / arc, both extending
// `handleSize` * a fixed reach factor from `origin`. Nearest hit (by
// distance along the ray) wins. Returns ENG_GIZMO_AXIS_NONE on a miss.
EngGizmoAxis Eng_GizmoHitTest(Camera3D cam, Vector2 screenPos, int screenW, int screenH,
                              Vector3 origin, EngGizmoMode mode, float handleSize);

// ---- Drag state -----------------------------------------------------------

// Result of a drag, accumulated since Eng_GizmoBeginDrag (see the file
// banner's drag convention). Only the field matching `mode` is meaningful.
typedef struct EngGizmoDelta {
    EngGizmoMode mode;
    Vector3 translate;     // TRANSLATE: world-space offset since grab.
    float   rotateRadians; // ROTATE: signed angle since grab, about drag.axis.
    Vector3 scale;         // SCALE: per-axis multiplier since grab (1.0 = unchanged).
} EngGizmoDelta;

// Caller-owned drag state, produced by Eng_GizmoBeginDrag and threaded
// through Eng_GizmoUpdateDrag for the lifetime of one mouse-down drag.
// Every field is captured at grab time and never mutated by UpdateDrag —
// deltas are always recomputed relative to this fixed grab snapshot, which
// is what makes the "absolute since grab" convention drift-free.
typedef struct EngGizmoDrag {
    EngGizmoMode mode;
    EngGizmoAxis axis;       // Active handle; ENG_GIZMO_AXIS_NONE if grab missed.
    Vector3 origin;          // Gizmo origin at grab time.
    Vector3 grabPoint;       // Initial ray-vs-constraint projection (translate/rotate).
    float   grabDistance;    // |grabPoint - origin| at grab time (scale denominator).
    Vector3 grabRefDir;      // Unit direction from origin to grabPoint, in-plane (rotate reference).
} EngGizmoDrag;

// Begin a drag on `axis` (typically the result of Eng_GizmoHitTest) for a
// gizmo at `origin` in `mode`. Builds the initial ray from `screenPos` and
// records grab state. Returns a drag with axis == ENG_GIZMO_AXIS_NONE if
// `axis` is NONE or the grab ray fails to hit the constraint (e.g. exactly
// parallel) — callers should check `.axis` before using the drag.
EngGizmoDrag Eng_GizmoBeginDrag(Camera3D cam, Vector2 screenPos, int screenW, int screenH,
                                 Vector3 origin, EngGizmoMode mode, EngGizmoAxis axis);

// Given the current mouse position, return the constrained delta
// accumulated since `drag` was begun (see the file banner's drag
// convention — this is NOT a per-frame increment). `drag` is read-only;
// the caller keeps the same EngGizmoDrag value for the whole interaction
// and just calls this every frame with a fresh screen position.
EngGizmoDelta Eng_GizmoUpdateDrag(EngGizmoDrag drag, Camera3D cam, Vector2 screenPos,
                                   int screenW, int screenH);

// ---- Optional convenience: debug-draw the handles -------------------------
//
// Submits the gizmo's handle geometry (3 axis lines, 3 plane-corner quads
// for translate, a rotation ring per axis) to debugdraw.h so a game gets a
// visualization for free. Entirely optional — a game can ignore this and
// draw its own handle meshes/sprites; nothing else in this file depends on
// debugdraw.h. `activeAxis` (e.g. the current hover or drag axis, or NONE)
// is highlighted; pass ENG_GIZMO_AXIS_NONE to draw all handles in their
// neutral colour.
void Eng_GizmoDebugDraw(Vector3 origin, EngGizmoMode mode, float handleSize, EngGizmoAxis activeAxis);

#endif // SHOOTER_GIZMO_H
