// ============================================================================
//  audio_voice.c — per-speaker playback over raylib AudioStream (see header).
// ============================================================================

#include "audio_voice.h"
#include "raylib.h"
#include <stdlib.h>
#include <string.h>

#define VQ_SIZE  32768u            // queued-sample ring (power of two)
#define VQ_MASK  (VQ_SIZE - 1u)
#define VCHUNK   1024              // samples per AudioStream refill

struct EngVoiceStream {
    AudioStream stream;
    int16_t     q[VQ_SIZE];
    unsigned    w, r;              // single-threaded (game thread): no atomics
};

EngVoiceStream *Eng_AudioVoiceOpen(int sampleRate, int channels) {
    EngVoiceStream *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    SetAudioStreamBufferSizeDefault(VCHUNK);
    v->stream = LoadAudioStream((unsigned)(sampleRate > 0 ? sampleRate : 16000),
                                16, (unsigned)(channels > 0 ? channels : 1));
    if (!IsAudioStreamValid(v->stream)) { free(v); return NULL; }
    SetAudioStreamVolume(v->stream, 1.0f);
    PlayAudioStream(v->stream);
    return v;
}

void Eng_AudioVoiceQueue(EngVoiceStream *v, const int16_t *pcm, int frames) {
    if (!v || !pcm || frames <= 0) return;
    for (int i = 0; i < frames; i++) {
        if ((v->w - v->r) >= VQ_SIZE) break;   // overflow: drop oldest-by-skipping
        v->q[v->w & VQ_MASK] = pcm[i];
        v->w++;
    }
}

void Eng_AudioVoiceSetSpatial(EngVoiceStream *v, float vol, float pan) {
    if (!v) return;
    SetAudioStreamVolume(v->stream, vol);
    SetAudioStreamPan(v->stream, pan);
}

void Eng_AudioVoiceUpdate(EngVoiceStream *v) {
    if (!v) return;
    static int16_t chunk[VCHUNK];
    while (IsAudioStreamProcessed(v->stream)) {
        if ((v->w - v->r) >= VCHUNK) {
            for (unsigned i = 0; i < VCHUNK; i++) chunk[i] = v->q[(v->r + i) & VQ_MASK];
            v->r += VCHUNK;
        } else {
            memset(chunk, 0, sizeof chunk);    // underrun → silence (no stutter)
        }
        UpdateAudioStream(v->stream, chunk, VCHUNK);
    }
}

void Eng_AudioVoiceClose(EngVoiceStream *v) {
    if (!v) return;
    StopAudioStream(v->stream);
    UnloadAudioStream(v->stream);
    free(v);
}
