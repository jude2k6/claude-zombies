#ifndef SHOOTER_PICK_H
#define SHOOTER_PICK_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Engine picking — ray construction + ray/primitive intersection helpers.
//
//  This is the core primitive the 3D map editor is built on: turn a screen
//  pixel (mouse cursor) into a world-space Ray, then test that ray against
//  boxes, spheres, and planes to find out what the cursor is pointing at or
//  where it should place something.
//
//  All functions here are pure — no global state, no engine-wide camera or
//  cursor cached anywhere. Callers own the Camera3D and screen size and pass
//  them in explicitly every time.
// ============================================================================

// Build a world-space pick ray from a screen-space pixel (e.g. mouse
// position) under the given camera and viewport size. This is the starting
// point for almost every editor pick: cursor position in, world ray out.
Ray Eng_PickRayFromScreen(Camera3D cam, Vector2 screenPos, int screenW, int screenH);

// Ray vs axis-aligned bounding box. Returns true on hit and writes the hit
// distance to *outDist (skipped if outDist is NULL).
bool Eng_PickRayAABB(Ray ray, BoundingBox box, float *outDist);

// Ray vs sphere. Returns true on hit and writes the hit distance to
// *outDist (skipped if outDist is NULL).
bool Eng_PickRaySphere(Ray ray, Vector3 center, float radius, float *outDist);

// Ray vs infinite plane, given any point on the plane and its normal.
// Returns false if the ray is parallel to the plane or the plane is behind
// the ray origin (negative distance). On hit, writes the hit distance to
// *outDist and the world-space hit point to *outHit (either may be NULL).
bool Eng_PickRayPlane(Ray ray, Vector3 planePoint, Vector3 planeNormal, float *outDist, Vector3 *outHit);

// Convenience: intersect with the horizontal plane at height y (normal
// (0,1,0)). The common "where on the floor did the user click" case for
// entity placement in the editor. Returns false if the ray is parallel to
// the ground or the ground is behind the ray origin.
bool Eng_PickRayGroundY(Ray ray, float y, Vector3 *outHit);

#endif // SHOOTER_PICK_H
