#ifndef SHOOTER_COLLIDE_H
#define SHOOTER_COLLIDE_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Engine collision — pure sweep / overlap / penetration math.
//
//  This is a *substrate*, not a character controller. It answers "do these two
//  volumes touch, when, and how deep" — it never decides how a mover responds.
//  Movement *feel* (slide along the normal, bhop, air-accel, step-up, friction)
//  is game-side and pluggable: a game reads these results and applies its own
//  response, or ignores this module and rolls its own collision entirely. Same
//  split as pick.h (ray math in the engine, what-it-hits in the game).
//
//  All functions are pure — no globals, no broadphase, no world. The caller
//  iterates its own candidate geometry (the engine has no scene graph) and
//  combines the per-pair results. Geometry is raylib `BoundingBox` (AABB);
//  movers may be an AABB, a sphere, or a capsule (segment + radius).
//
//  Conventions:
//   - Returned normals are the STATIC box's outward surface normal at contact
//     (the vector to reflect/slide a mover's velocity against).
//   - Push-out vectors move the MOVER out of penetration (add them to its pos).
//   - Swept tests take `vel` as the mover's FULL-FRAME displacement, so the
//     time-of-impact `*outT` is in [0,1] across this frame.
// ============================================================================

// ---- Static overlap --------------------------------------------------------

// Do two AABBs overlap? (Touching faces, zero overlap, count as no overlap.)
bool Eng_CollideAABBOverlap(BoundingBox a, BoundingBox b);

// Minimum translation that separates `a` from `b` (added to `a`'s position).
// Returns false if they don't overlap. The MTV is along the axis of least
// penetration, signed to push `a` away from `b`.
bool Eng_CollideAABBPenetration(BoundingBox a, BoundingBox b, Vector3 *outMTV);

// Sphere vs AABB. If they intersect, returns true and writes the push-out
// vector that moves the sphere centre just clear of the box. Handles a centre
// inside the box (pushes out the nearest face).
bool Eng_CollideSphereAABB(Vector3 center, float radius, BoundingBox box,
                           Vector3 *outPush);

// Capsule (segment p0..p1, radius) vs AABB — the usual character-vs-world test.
// Approximated as: closest point on the segment to the box centre, then a
// sphere test there. Good enough for push-out resolution of an upright player
// capsule against world boxes; not an exact segment-vs-box distance.
bool Eng_CollideCapsuleAABB(Vector3 p0, Vector3 p1, float radius, BoundingBox box,
                            Vector3 *outPush);

// ---- Swept (anti-tunnelling) ----------------------------------------------

// Swept AABB vs static AABB. `moving` is the mover's box at the start of the
// frame; `vel` is its full-frame displacement. If they collide during [0,1],
// returns true with `*outT` the time-of-impact (0 = touching at start) and
// `*outNormal` the static box's surface normal at contact. A typical mover then
// advances to TOI, kills the into-surface velocity component, and re-sweeps the
// remainder (the slide loop) — that loop is the game's to write.
//
// If the boxes already fully overlap at t=0 the impact normal can be ambiguous;
// resolve that case with Eng_CollideAABBPenetration instead.
bool Eng_CollideSweptAABB(BoundingBox moving, Vector3 vel, BoundingBox staticBox,
                          float *outT, Vector3 *outNormal);

#endif // SHOOTER_COLLIDE_H
