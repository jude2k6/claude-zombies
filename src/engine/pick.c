#include "pick.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

Ray Eng_PickRayFromScreen(Camera3D cam, Vector2 screenPos, int screenW, int screenH)
{
    return GetScreenToWorldRayEx(screenPos, cam, screenW, screenH);
}

bool Eng_PickRayAABB(Ray ray, BoundingBox box, float *outDist)
{
    RayCollision hit = GetRayCollisionBox(ray, box);
    if (!hit.hit) return false;
    if (outDist) *outDist = hit.distance;
    return true;
}

bool Eng_PickRaySphere(Ray ray, Vector3 center, float radius, float *outDist)
{
    RayCollision hit = GetRayCollisionSphere(ray, center, radius);
    if (!hit.hit) return false;
    if (outDist) *outDist = hit.distance;
    return true;
}

bool Eng_PickRayPlane(Ray ray, Vector3 planePoint, Vector3 planeNormal, float *outDist, Vector3 *outHit)
{
    Vector3 n = Vector3Normalize(planeNormal);
    float denom = Vector3DotProduct(n, ray.direction);

    // Parallel (or near enough that the intersection is numerically useless).
    if (fabsf(denom) < 1e-6f) return false;

    float dist = Vector3DotProduct(Vector3Subtract(planePoint, ray.position), n) / denom;

    // Plane is behind the ray origin.
    if (dist < 0.0f) return false;

    if (outDist) *outDist = dist;
    if (outHit)  *outHit  = Vector3Add(ray.position, Vector3Scale(ray.direction, dist));
    return true;
}

bool Eng_PickRayGroundY(Ray ray, float y, Vector3 *outHit)
{
    Vector3 planePoint = { 0.0f, y, 0.0f };
    Vector3 normal     = { 0.0f, 1.0f, 0.0f };
    return Eng_PickRayPlane(ray, planePoint, normal, NULL, outHit);
}
