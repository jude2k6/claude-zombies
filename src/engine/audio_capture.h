#ifndef SHOOTER_AUDIO_CAPTURE_H
#define SHOOTER_AUDIO_CAPTURE_H

// ============================================================================
//  audio_capture.h — microphone capture primitive (the "deaf pipe", input half).
//
//  raylib's public API has no mic capture, so this is built on miniaudio —
//  raylib's OWN bundled header (external/miniaudio.h), compiled privately here
//  with internal linkage, so it adds NO new linked library: the engine link line
//  stays raylib + enet (see docs/voice-chat.md §4 + [[engine-only-raylib-enet]]).
//
//  Pure, game-clean IO: it knows samples, never players. miniaudio's capture
//  callback runs on its own audio thread; this module hands the data across via a
//  lock-free ring buffer that Eng_AudioCaptureRead drains on the game thread.
// ============================================================================

#include <stdbool.h>
#include <stdint.h>

// Open the default capture device as signed-16 PCM at the given rate/channels
// (e.g. 16000, 1 — VoIP-typical). Returns false if no device is available (a
// headless box, no mic) — the caller degrades gracefully. Idempotent.
bool  Eng_AudioCaptureOpen(int sampleRate, int channels);

// Drain up to maxFrames captured samples into out; returns the count written
// (0 if none ready / not open). Call each frame.
int   Eng_AudioCaptureRead(int16_t *out, int maxFrames);

// Current input level, 0..1 RMS of the last captured block — for a mic meter
// and amplitude-threshold voice-activity detection.
float Eng_AudioCaptureLevel(void);

bool  Eng_AudioCaptureActive(void);
void  Eng_AudioCaptureClose(void);

#endif // SHOOTER_AUDIO_CAPTURE_H
