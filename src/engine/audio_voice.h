#ifndef SHOOTER_AUDIO_VOICE_H
#define SHOOTER_AUDIO_VOICE_H

// ============================================================================
//  audio_voice.h — per-speaker voice playback primitive (the "deaf pipe", out half).
//
//  A thin wrapper over raylib's AudioStream: one EngVoiceStream per remote
//  speaker, fed decoded PCM each frame and panned/attenuated for positional
//  voice. Game-clean — it knows samples, never who is talking. Living engine-side
//  (not in the plugin) keeps the raylib AudioStream symbols inside libengine, so a
//  dynamic plugin only ever calls Eng_* (which the host always exports). See
//  docs/voice-chat.md §5.
// ============================================================================

#include <stdint.h>

typedef struct EngVoiceStream EngVoiceStream;

// Open a playback stream for one speaker (e.g. 16000, 1). NULL on failure.
EngVoiceStream *Eng_AudioVoiceOpen(int sampleRate, int channels);

// Append decoded PCM16 samples to the stream's queue (drops on overflow).
void Eng_AudioVoiceQueue(EngVoiceStream *v, const int16_t *pcm, int frames);

// Positional control: vol 0..1, pan 0..1 (0.5 = centred). Pair with the mixer's
// Audio_Positional (audio.h) for world-space voice; pass (1, 0.5) for flat voice.
void Eng_AudioVoiceSetSpatial(EngVoiceStream *v, float vol, float pan);

// Pump the stream once per frame: refill raylib's buffers from the queue (or
// silence on underrun, so playback never stutter-loops).
void Eng_AudioVoiceUpdate(EngVoiceStream *v);

void Eng_AudioVoiceClose(EngVoiceStream *v);

#endif // SHOOTER_AUDIO_VOICE_H
