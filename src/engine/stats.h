#ifndef SHOOTER_STATS_H
#define SHOOTER_STATS_H

// ============================================================================
//  Engine instrumentation — per-frame timing, fixed-step count, draw tally.
//
//  app.c drives the bookkeeping: Eng_StatsBeginFrame(dt) at the top of each
//  rendered frame (resets the draw counter), Eng_StatsEndFrame(steps) at the
//  bottom (snapshots the frame). The gfx facade bumps the draw counter per
//  high-level 3D draw via Eng_StatsAddDrawCalls. Read the getters from anywhere
//  (a debug HUD, devtools) — they report the most recently *completed* frame.
//
//  The draw tally counts engine-facade 3D draw *submissions* (Eng_GfxDrawModel,
//  cubes, spheres, etc.), not raw GL draw calls — many primitives are batched
//  by rlgl. It's the right signal for spotting per-entity draw growth (players,
//  props, projectiles) as scenes scale up; it does NOT include 2D HUD draws the
//  game issues through raylib directly.
// ============================================================================

#include <stdint.h>

// ---- Per-frame bookkeeping (called by app.c) -------------------------------

// Top of a rendered frame: record this frame's real dt, reset the draw tally.
void Eng_StatsBeginFrame(float dt);

// Bottom of a rendered frame: record how many fixed sim steps were drained,
// then snapshot the frame so the getters report it.
void Eng_StatsEndFrame(int fixedSteps);

// ---- Draw tally (called by the gfx facade) ---------------------------------

void Eng_StatsAddDrawCalls(int n);

// ---- Getters (report the last completed frame) -----------------------------

float    Eng_StatsFrameTimeMs(void);  // real frame time in milliseconds
float    Eng_StatsFps(void);          // 1/dt, exponentially smoothed
int      Eng_StatsFixedSteps(void);   // fixed sim steps drained last frame
int      Eng_StatsDrawCalls(void);    // engine-facade 3D draws last frame
uint64_t Eng_StatsFrameCount(void);   // total rendered frames since startup

#endif // SHOOTER_STATS_H
