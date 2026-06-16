#include "stats.h"

// Module-level state. Two draw counters: one accumulating the in-progress
// frame, one holding the last completed frame's total (what getters report).
static float    s_dt           = 0.0f;
static float    s_fps          = 0.0f;
static int      s_fixedSteps   = 0;
static int      s_drawCallsCur = 0;
static int      s_drawCallsLast = 0;
static uint64_t s_frameCount   = 0;

void Eng_StatsBeginFrame(float dt) {
    s_dt           = dt;
    s_drawCallsCur = 0;

    // Exponentially smoothed fps so a debug readout doesn't jitter every frame.
    float inst = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
    if (s_fps <= 0.0f) s_fps = inst;            // seed on the first frame
    else               s_fps += (inst - s_fps) * 0.1f;
}

void Eng_StatsAddDrawCalls(int n) { s_drawCallsCur += n; }

void Eng_StatsEndFrame(int fixedSteps) {
    s_fixedSteps    = fixedSteps;
    s_drawCallsLast = s_drawCallsCur;
    s_frameCount++;
}

float    Eng_StatsFrameTimeMs(void) { return s_dt * 1000.0f; }
float    Eng_StatsFps(void)         { return s_fps; }
int      Eng_StatsFixedSteps(void)  { return s_fixedSteps; }
int      Eng_StatsDrawCalls(void)   { return s_drawCallsLast; }
uint64_t Eng_StatsFrameCount(void)  { return s_frameCount; }
