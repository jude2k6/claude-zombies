// ============================================================================
//  audio_capture.c — mic capture on a private, capture-only miniaudio instance.
//
//  MA_API static gives our copy internal linkage so it can't clash with the
//  miniaudio raylib already compiles inside raudio.c; the MA_NO_* trims drop the
//  decode/encode/generation code we don't need. No new linked library results —
//  miniaudio is header-only and ships inside raylib's source tree.
// ============================================================================

#define MA_API static
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"          // from raylib's external/ (engine include path)

#include "audio_capture.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>

// Lock-free SPSC ring: the miniaudio audio thread writes, the game thread reads.
// Indices are free-running unsigned counters; (w - r) is the fill, masked on use.
#define CAP_RING_SIZE 32768u                 // power of two
#define CAP_RING_MASK (CAP_RING_SIZE - 1u)

static ma_device   g_dev;
static bool        g_open  = false;
static int16_t     g_ring[CAP_RING_SIZE];
static atomic_uint g_w, g_r;
static _Atomic float g_level = 0.0f;

static void CaptureCB(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
    (void)dev; (void)out;
    const int16_t *src = (const int16_t *)in;
    if (!src) return;
    unsigned w = atomic_load_explicit(&g_w, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&g_r, memory_order_acquire);
    double acc = 0.0;
    for (ma_uint32 i = 0; i < frames; i++) {
        int16_t s = src[i];
        acc += (double)s * (double)s;
        if ((w - r) < CAP_RING_SIZE) {        // drop on overrun rather than block
            g_ring[w & CAP_RING_MASK] = s;
            w++;
        }
    }
    atomic_store_explicit(&g_w, w, memory_order_release);
    float rms = frames ? (float)(sqrt(acc / (double)frames) / 32768.0) : 0.0f;
    atomic_store_explicit(&g_level, rms, memory_order_relaxed);
}

bool Eng_AudioCaptureOpen(int sampleRate, int channels) {
    if (g_open) return true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format   = ma_format_s16;
    cfg.capture.channels = (ma_uint32)(channels > 0 ? channels : 1);
    cfg.sampleRate       = (ma_uint32)(sampleRate > 0 ? sampleRate : 16000);
    cfg.dataCallback     = CaptureCB;
    if (ma_device_init(NULL, &cfg, &g_dev) != MA_SUCCESS) {
        fprintf(stderr, "audio_capture: no capture device (mic unavailable)\n");
        return false;
    }
    atomic_store(&g_w, 0u);
    atomic_store(&g_r, 0u);
    if (ma_device_start(&g_dev) != MA_SUCCESS) {
        ma_device_uninit(&g_dev);
        fprintf(stderr, "audio_capture: device start failed\n");
        return false;
    }
    g_open = true;
    return true;
}

int Eng_AudioCaptureRead(int16_t *out, int maxFrames) {
    if (!g_open || !out || maxFrames <= 0) return 0;
    unsigned w = atomic_load_explicit(&g_w, memory_order_acquire);
    unsigned r = atomic_load_explicit(&g_r, memory_order_relaxed);
    unsigned avail = w - r;
    unsigned n = ((unsigned)maxFrames < avail) ? (unsigned)maxFrames : avail;
    for (unsigned i = 0; i < n; i++) out[i] = g_ring[(r + i) & CAP_RING_MASK];
    atomic_store_explicit(&g_r, r + n, memory_order_release);
    return (int)n;
}

float Eng_AudioCaptureLevel(void) {
    return atomic_load_explicit(&g_level, memory_order_relaxed);
}

bool Eng_AudioCaptureActive(void) { return g_open; }

void Eng_AudioCaptureClose(void) {
    if (!g_open) return;
    ma_device_uninit(&g_dev);
    g_open = false;
}
