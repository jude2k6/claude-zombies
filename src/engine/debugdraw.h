#ifndef SHOOTER_DEBUGDRAW_H
#define SHOOTER_DEBUGDRAW_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================================
//  Engine debug-draw — toggleable, deferred 3D overlay channel.
//
//  Both the FPS (AI/collision visualization) and the planned map editor
//  (selection highlights, sector bounds, gizmo state) need a way to drop
//  debug shapes from anywhere in the frame and have them rendered once,
//  on top of everything else, without each call site managing its own
//  immediate-mode draw. This module is that channel.
//
//  DEFERRED-QUEUE MODEL
//  ---------------------
//  Eng_Debug{Line,Box,Cube,Sphere,Text3D} do not draw anything — they push
//  a small struct onto a fixed-size, compile-time-capped pool (see the
//  ENG_DEBUG_MAX_* defines in debugdraw.c). Submission is O(1) and safe to
//  call from `frame`, `fixed`, or any game-logic pass, in any order, any
//  number of times per frame.
//
//  Once per frame, after all submissions for that frame are in, the owner
//  of the render loop calls:
//    Eng_DebugDraw3D(cam)     — inside an active BeginMode3D/EndMode3D scope,
//                               draws all queued lines/boxes/spheres in world
//                               space.
//    Eng_DebugDrawLabels(cam) — OUTSIDE Mode3D (2D/screen space), projects
//                               queued Text3D labels via GetWorldToScreen and
//                               draws them with DrawText. Labels behind the
//                               camera are skipped.
//  Both calls clear the queues they drain, so submit -> draw -> clear is the
//  cycle every frame. Draw order requirement: call Eng_DebugDraw3D() while
//  still inside Mode3D, then EndMode3D(), then Eng_DebugDrawLabels() — text
//  is plain 2D DrawText and will be drawn-over / depth-tested incorrectly if
//  attempted inside Mode3D.
//
//  If a frame's submissions exceed a pool's cap, further pushes for that
//  shape type are silently dropped (no crash, no resize) — debug overlays
//  degrade, they never destabilize the game.
//
//  ENABLE FLAG
//  -----------
//  Eng_DebugSetEnabled(false) (the default) makes every Eng_Debug* submission
//  function a no-op. This means call sites can leave debug-draw calls in
//  permanently (e.g. "draw the AI nav path every tick") with zero runtime
//  cost beyond the branch, and zero visual effect, until a dev flips the
//  flag (devtools menu, console command, hotkey). Eng_DebugDraw3D/Labels
//  also no-op when disabled, and still clear any stale queue contents.
// ============================================================================

// ---- Enable flag -------------------------------------------------------

void Eng_DebugSetEnabled(bool on);
bool Eng_DebugEnabled(void);

// ---- Submission (safe to call every frame; no-op while disabled) ------

void Eng_DebugLine(Vector3 a, Vector3 b, Color c);
void Eng_DebugBox(BoundingBox box, Color c);              // wireframe
void Eng_DebugCube(Vector3 center, Vector3 size, Color c); // wireframe
void Eng_DebugSphere(Vector3 center, float radius, Color c); // wireframe
void Eng_DebugText3D(Vector3 worldPos, const char *text, Color c);

// ---- Flush (call once per frame; see ordering note above) -------------

// Draw queued lines/boxes/spheres in world space. Call inside an active
// Eng_GfxBeginMode3D/EndMode3D (or raylib BeginMode3D/EndMode3D) scope.
// Clears the line/box/sphere queues.
void Eng_DebugDraw3D(Camera3D cam);

// Project and draw queued Text3D labels as 2D screen-space text. Call
// AFTER EndMode3D (screen space, not world space). Clears the label queue.
void Eng_DebugDrawLabels(Camera3D cam);

#endif // SHOOTER_DEBUGDRAW_H
