// ============================================================================
//  voice_plugin.c — voice chat as an engine plugin (the reference plugin).
//
//  Builds to build/plugins/voice.so. Drop it next to the binary's ./plugins and
//  the engine runs it alongside the game (see docs/voice-chat.md). It owns all
//  voice POLICY — push-to-talk, the packet schema, the PCM16 codec, self-monitor,
//  the talking overlay — and leans on engine PRIMITIVES for everything hard:
//
//    Eng_AudioCapture*  mic in            Eng_AudioVoice*   per-speaker playback
//    Net_* (+ observer) transport         Eng_Plugin host   the lifecycle hooks
//
//  The seam holds: this file includes NO game header. It depends only on the
//  engine's public surface, so it works for any game on the engine.
//
//  Networking model (star through the host, since Net_Broadcast is host-only):
//    - send:  call BOTH Net_SendTo(0,…) and Net_Broadcast(…) every frame —
//             on a client only the first does anything (→ server); on the host
//             only the second does (→ all clients). One code path, both modes.
//    - recv:  Net_SetReceiveObserver fires during the game's own Net_Poll, so we
//             see voice packets without stealing the poll. The host RELAYS each
//             received voice packet (Net_Broadcast is a no-op on clients), giving
//             every client everyone's voice.
//    - self-echo: each session mints a random token; a receiver ignores packets
//             carrying its own token (so the host's relay doesn't loop back).
//
//  CODEC: v1 is PCM16 passthrough (the `codec` byte = 0). Opus would be the
//  plugin's own dependency (libopus) behind that byte — the engine never links it
//  ([[engine-only-raylib-enet]]). POSITIONAL voice is left flat (pan .5) for v1;
//  it needs a peer→player-position hook the engine deliberately lacks (§7).
//
//  NOTE: built/linked clean and symbol-resolved against the host, but voice
//  itself can't be auto-verified here (needs a mic + two clients). Hold the
//  push-to-talk key with self-monitor on to validate capture→playback locally.
// ============================================================================

#include "plugin.h"
#include "net.h"
#include "audio_capture.h"
#include "audio_voice.h"
#include "raylib.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

// ---- tunables --------------------------------------------------------------
#define VOICE_RATE      16000          // 16 kHz mono — VoIP-typical
#define VOICE_FRAME     320            // 20 ms @ 16 kHz
#define VOICE_PKT_TYPE  100            // distinct from all PKT_* (game ignores it)
#define VOICE_CODEC_PCM16 0
#define PTT_KEY         KEY_V          // hold to talk
#define MONITOR_KEY     KEY_B          // toggle hearing yourself (local test)
#define MAX_REMOTES     4
#define HEADER_BYTES    10             // type,codec,token(4),seq(2),nsamples(2)

// ---- per-remote-speaker playback ------------------------------------------
typedef struct {
    uint32_t        token;             // sender's session token (0 = free slot)
    EngVoiceStream *stream;
    double          lastHeard;         // GetTime() of last packet (for the overlay)
} Remote;

static struct {
    uint32_t        token;             // our random session token
    int16_t         acc[VOICE_FRAME * 4];
    int             accN;              // samples buffered toward a full frame
    uint16_t        seq;
    bool            monitor;           // self-monitor (loopback) on?
    EngVoiceStream *self;              // self-monitor playback
    Remote          remotes[MAX_REMOTES];
    float           micLevel;
} G;

// ---- packet (de)serialisation (manual bytes; same-arch LAN assumption) -----
static int VoicePack(uint8_t *buf, const int16_t *pcm, int n) {
    buf[0] = VOICE_PKT_TYPE;
    buf[1] = VOICE_CODEC_PCM16;
    memcpy(buf + 2, &G.token, 4);
    uint16_t seq = G.seq, ns = (uint16_t)n;
    memcpy(buf + 6, &seq, 2);
    memcpy(buf + 8, &ns, 2);
    memcpy(buf + HEADER_BYTES, pcm, (size_t)n * sizeof(int16_t));
    return HEADER_BYTES + n * (int)sizeof(int16_t);
}

static Remote *RemoteFor(uint32_t token) {
    Remote *freeSlot = NULL;
    for (int i = 0; i < MAX_REMOTES; i++) {
        if (G.remotes[i].token == token && G.remotes[i].stream) return &G.remotes[i];
        if (!G.remotes[i].stream && !freeSlot) freeSlot = &G.remotes[i];
    }
    if (!freeSlot) return NULL;
    freeSlot->stream = Eng_AudioVoiceOpen(VOICE_RATE, 1);
    if (!freeSlot->stream) return NULL;
    freeSlot->token = token;
    return freeSlot;
}

// Fires from inside the game's Net_Poll (game thread) — copy out, don't retain.
static void OnPacket(int peerIdx, const uint8_t *data, size_t len) {
    (void)peerIdx;
    if (len < HEADER_BYTES || data[0] != VOICE_PKT_TYPE) return;   // not voice
    uint32_t token; memcpy(&token, data + 2, 4);
    if (token == G.token) return;                                  // our own echo
    Net_Broadcast(data, len, false);                               // host relays; client no-op
    uint16_t ns; memcpy(&ns, data + 8, 2);
    if (len < HEADER_BYTES + (size_t)ns * sizeof(int16_t)) return; // truncated
    Remote *rm = RemoteFor(token);
    if (!rm) return;
    Eng_AudioVoiceQueue(rm->stream, (const int16_t *)(data + HEADER_BYTES), ns);
    rm->lastHeard = GetTime();
}

// ---- lifecycle -------------------------------------------------------------
static void Init(void) {
    memset(&G, 0, sizeof G);
    G.token = ((uint32_t)GetRandomValue(1, 0x7fff) << 16) ^ (uint32_t)GetRandomValue(1, 0xffff);
    if (!G.token) G.token = 1;
    Eng_AudioCaptureOpen(VOICE_RATE, 1);     // graceful if no mic
    G.self = Eng_AudioVoiceOpen(VOICE_RATE, 1);
    Net_SetReceiveObserver(OnPacket);
}

static void Frame(float dt, int w, int h) {
    (void)dt; (void)w; (void)h;
    if (IsKeyPressed(MONITOR_KEY)) G.monitor = !G.monitor;
    G.micLevel = Eng_AudioCaptureLevel();
    bool talking = IsKeyDown(PTT_KEY) && Eng_AudioCaptureActive();

    // Drain the mic into the frame accumulator, emit whole 20 ms frames.
    int got;
    while ((got = Eng_AudioCaptureRead(G.acc + G.accN,
                                       (int)(sizeof G.acc / sizeof G.acc[0]) - G.accN)) > 0) {
        G.accN += got;
        while (G.accN >= VOICE_FRAME) {
            if (talking) {
                uint8_t pkt[HEADER_BYTES + VOICE_FRAME * sizeof(int16_t)];
                int n = VoicePack(pkt, G.acc, VOICE_FRAME);
                Net_SendTo(0, pkt, (size_t)n, false);    // → server (client only)
                Net_Broadcast(pkt, (size_t)n, false);    // → clients (host only)
                if (G.monitor) Eng_AudioVoiceQueue(G.self, G.acc, VOICE_FRAME);
                G.seq++;
            }
            G.accN -= VOICE_FRAME;
            memmove(G.acc, G.acc + VOICE_FRAME, (size_t)G.accN * sizeof(int16_t));
        }
        if (G.accN >= VOICE_FRAME) continue;
        break;
    }

    // Pump playback (flat / non-positional for v1).
    if (G.self) Eng_AudioVoiceUpdate(G.self);
    for (int i = 0; i < MAX_REMOTES; i++)
        if (G.remotes[i].stream) {
            Eng_AudioVoiceSetSpatial(G.remotes[i].stream, 1.0f, 0.5f);
            Eng_AudioVoiceUpdate(G.remotes[i].stream);
        }
}

static void Draw(int w, int h) {
    (void)h;
    const int pad = 10, bw = 190, bh = 92;
    int x = w - bw - pad, y = pad;
    DrawRectangle(x, y, bw, bh, Fade(BLACK, 0.55f));
    DrawText("VOICE", x + 10, y + 8, 18, RAYWHITE);

    // mic meter
    bool talking = IsKeyDown(PTT_KEY) && Eng_AudioCaptureActive();
    int barX = x + 10, barY = y + 34, barW = bw - 20;
    DrawRectangle(barX, barY, barW, 8, Fade(WHITE, 0.20f));
    int fill = (int)(G.micLevel * 6.0f * barW); if (fill > barW) fill = barW;
    DrawRectangle(barX, barY, fill, 8, talking ? GREEN : GRAY);
    DrawText(Eng_AudioCaptureActive() ? (talking ? "TALKING [V]" : "hold [V] to talk")
                                      : "no mic", x + 10, y + 48, 12,
             talking ? GREEN : Fade(RAYWHITE, 0.8f));
    DrawText(G.monitor ? "monitor [B]: on" : "monitor [B]: off",
             x + 10, y + 64, 12, Fade(RAYWHITE, 0.7f));

    // speaking remotes
    double now = GetTime();
    int sx = x + 110, sy = y + 30, shown = 0;
    for (int i = 0; i < MAX_REMOTES && shown < 4; i++) {
        if (!G.remotes[i].stream) continue;
        bool live = (now - G.remotes[i].lastHeard) < 0.4;
        DrawCircle(sx, sy + shown * 14 + 4, 4, live ? GREEN : Fade(GRAY, 0.6f));
        shown++;
    }
}

static void Shutdown(void) {
    Net_SetReceiveObserver(NULL);
    Eng_AudioCaptureClose();
    if (G.self) Eng_AudioVoiceClose(G.self);
    for (int i = 0; i < MAX_REMOTES; i++)
        if (G.remotes[i].stream) Eng_AudioVoiceClose(G.remotes[i].stream);
    memset(&G, 0, sizeof G);
}

// ---- plugin entry ----------------------------------------------------------
static const EngPluginDesc DESC = {
    .name = "voice", .abiVersion = ENG_PLUGIN_ABI,
    .init = Init, .frame = Frame, .draw = Draw, .shutdown = Shutdown,
};

const EngPluginDesc *eng_plugin_main(void) { return &DESC; }
