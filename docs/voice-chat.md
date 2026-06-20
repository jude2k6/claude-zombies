# Voice Chat — design (engine capture primitive + a voice plugin)

> **Status: DESIGN — not built.** This captures *how* in-game voice chat fits this
> engine, written against the actual audio/net APIs in the tree. It builds on the
> engine **plugin host** ([engine-usage.md](engine-usage.md) §3 `plugin`) — voice
> chat is the motivating example for that seam — plus the existing audio mixer
> (`audio.h`) and net transport (`net.h`). Nothing here ships by writing the doc;
> items become real when they land a header + code and get documented in
> engine-usage.md §3. Cross-refs: [engine-layers.md](engine-layers.md) (primitives
> vs. policy), [engine-roadmap.md](engine-roadmap.md) (the `NET_MAX_PLAYERS` cap that
> gates many-speaker voice).

---

## 1. The idea in one screen

Each client captures its microphone, encodes short audio frames, and sends them over
the existing UDP transport; every other client decodes the frames and plays them —
**positionally**, so a teammate's voice comes from where their player is. Push-to-talk
(or voice-activity detection) decides *when* to capture; a small overlay shows *who* is
talking.

```
  mic ──► Eng_AudioCaptureRead ──► [encode] ──► Net_Broadcast(voice pkt, unreliable)
                                                        │  (per frame, ~20 ms)
                                                        ▼
  speakers ◄── AudioStream (per peer) ◄── [decode] ◄── Net_Poll → voice pkt
                    ▲
              pan/volume from Audio_Positional(peerPlayerPos)   ← positional voice
```

Two engine **primitives** are missing today (capture + a voice-playback helper); the
**feature** (when to talk, the wire packet, positional mixing, the indicator) is
**policy** and lives in a plugin/game. That split is the whole design.

---

## 2. Where each piece lives (primitives vs. policy)

Per the engine's cardinal rule — *the engine ships primitives, the application brings
policy* ([engine-layers.md](engine-layers.md)):

| Concern | Owner | Why |
|---|---|---|
| **Mic capture** (open device, pull PCM frames) | **engine** (`Eng_AudioCapture*`, new) | Substrate, like the mixer/transport. Built on miniaudio (§4). |
| **Voice playback** (a low-latency PCM stream per speaker) | **engine** (`Eng_AudioVoice*`, new, thin) | A reusable wrapper over raylib `AudioStream`; any game wants it. |
| **3D roll-off / pan** | **engine** (`Audio_Positional`, exists) | Already the mixer's job (`audio.h`). |
| **UDP send/receive** | **engine** (`net.h`, exists) | Transport only — never learns it's carrying voice. |
| **Push-to-talk / VAD, codec choice, the voice packet schema, positional mapping, the talking overlay** | **plugin / game** | Policy. Lives in a voice **plugin** (`eng_plugin_main`) or game-side. |

So a different game reuses capture + voice-stream + transport unchanged and writes only
its own voice *policy* — exactly the property the plugin host exists to give.

---

## 3. Reality check: what raylib gives us, what it doesn't

Verified against `build/_deps/raylib-src/src/raylib.h`:

- **Playback — yes, public.** `LoadAudioStream(sampleRate, sampleSize, channels)` +
  `UpdateAudioStream(stream, data, frameCount)` + `PlayAudioStream` stream raw PCM out.
  `SetAudioStreamPan` / `SetAudioStreamVolume` give positional control. This is the
  voice-playback half, free.
- **Capture — NO.** raylib's public API has **no microphone/recording call**. The only
  `*Record*` functions are `StartAutomationEventRecording` (input automation) and GIF
  recording — not audio. There is no `InitAudioCapture` / `GetMicData`.
- **But miniaudio is right there.** raylib is built on **miniaudio**, vendored at
  `build/_deps/raylib-src/src/external/miniaudio.h`, which has full capture support
  (`ma_device_type_capture`, `ma_device_init`, …). raylib compiles it *internally* and
  doesn't re-export it, so capture means **we compile our own capture-only miniaudio
  instance** (§4). This is the one real integration wrinkle in the whole feature.

---

## 4. Engine primitive #1 — `Eng_AudioCapture*` (mic in, via miniaudio)

A new engine module (`src/engine/audio_capture.{c,h}`) that opens the default capture
device and hands the caller mono PCM frames. miniaudio's capture **callback runs on its
own audio thread**, so the module's job is a **lock-free ring buffer**: the callback
writes captured samples in; `Eng_AudioCaptureRead` drains them on the game thread.

```c
// audio_capture.h (proposed)
bool Eng_AudioCaptureOpen(int sampleRate, int channels);   // e.g. 16000, 1 (VoIP-typical)
int  Eng_AudioCaptureRead(int16_t *out, int maxFrames);    // drain ring → out; returns frames
float Eng_AudioCaptureLevel(void);                          // 0..1 RMS, for VAD + a mic meter
void Eng_AudioCaptureClose(void);
bool Eng_AudioCaptureActive(void);
```

16 kHz mono 16-bit is the VoIP norm — intelligible speech at a fraction of music
bandwidth. The module is engine-clean (it knows samples, never players).

### The miniaudio integration caveat (read this before coding)

raylib already compiles miniaudio with `MINIAUDIO_IMPLEMENTATION` inside its `raudio.c`
translation unit. A **second** `MINIAUDIO_IMPLEMENTATION` in `audio_capture.c` risks
**duplicate symbols at link**. The clean fix is to give our copy **internal linkage**
and trim it to capture only:

```c
// audio_capture.c — our private, capture-only miniaudio instance.
#define MA_API static                 // internal linkage → no clash with raylib's copy
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"                // from raylib's external/ (add to include path)
```

Two independent `ma_context`/`ma_device` instances (raylib's playback + ours for
capture) coexist fine — miniaudio is built for that. If `MA_API static` proves awkward,
the fallbacks are: a thin C++/C shim around a separately-built miniaudio, or a different
capture lib (SDL audio, PortAudio) — but `MA_API static` keeps zero new dependencies and
is the recommended path. **This caveat is the main thing to get right; everything else
is ordinary code.**

---

## 5. Engine primitive #2 — `Eng_AudioVoice*` (per-speaker playback)

A thin wrapper over raylib `AudioStream`, one stream per remote speaker, fed decoded PCM
each frame and panned/attenuated for positional voice.

```c
// (proposed) part of audio.h or a new voice header
typedef struct EngVoiceStream EngVoiceStream;
EngVoiceStream *Eng_AudioVoiceOpen(int sampleRate, int channels);
void Eng_AudioVoiceQueue(EngVoiceStream *v, const int16_t *pcm, int frames);
void Eng_AudioVoiceSpatial(EngVoiceStream *v, float vol, float pan); // from Audio_Positional
void Eng_AudioVoiceClose(EngVoiceStream *v);
```

Positional voice falls out of pieces that already exist: the game calls
`Audio_SetListener(camPos, camYaw)` each frame (`audio.h`); the plugin calls
`Audio_Positional(peerPlayerPos, maxDist, &vol, &pan)` and forwards the result into
`Eng_AudioVoiceSpatial`. Non-positional (lobby/party) voice just pins `vol=1, pan=0.5`.

> *Smaller alternative:* skip this wrapper for v1 and let the plugin drive raylib
> `AudioStream` directly — it's a handful of calls. The wrapper earns its keep once a
> second consumer (or jitter-buffer logic) appears. Either way it's engine-side.

---

## 6. Transport — reuse `net.h`, schema is policy

Voice rides the **existing** UDP transport unchanged:

- `Net_Broadcast(pkt, len, /*reliable*/false)` — voice is loss-tolerant; a dropped 20 ms
  frame is inaudible, and head-of-line blocking from reliability would add latency. Send
  **unreliable**.
- The **packet schema is game/plugin policy**, exactly like `protocol.c` owns the
  snapshot schema (`net.h` never learns what a player is). A minimal voice packet:

  ```
  [u8 type=VOICE][u8 peerId][u16 seq][u8 codec][u16 nbytes][payload …]
  ```

  `seq` lets the receiver drop late/duplicate frames and size a tiny **jitter buffer**
  (queue ~2–3 frames before playback to absorb network jitter). `peerId` selects the
  receiver's `EngVoiceStream`.

> **Scale constraint.** `net.h` is raw transport with `NET_MAX_PLAYERS == 4`
> ([engine-roadmap.md](engine-roadmap.md) Phase D). Voice for a 4-player co-op session
> works on today's transport; **many-speaker** voice (16+) waits on the Phase D netcode
> (raised cap, broadcast loop). Voice doesn't *need* delta-compression, but it does need
> the higher peer cap.

---

## 7. The voice plugin (policy, on the plugin host)

The feature is an engine **plugin** (`eng_plugin_main`) — or game-side code — mapping
cleanly onto the lifecycle hooks ([engine-usage.md](engine-usage.md) §3 `plugin`):

| Hook | Voice plugin does |
|---|---|
| `init` | `Eng_AudioCaptureOpen(16000,1)`; bind a push-to-talk action (`Eng_InputBind`); alloc per-peer `EngVoiceStream`s + jitter buffers. |
| `frame` | If PTT held (or VAD over `Eng_AudioCaptureLevel`), `Eng_AudioCaptureRead` → encode → `Net_Broadcast` unreliable. Drain received packets (the plugin/game already polls `Net_Poll`; voice packets route here) → decode → jitter-buffer → `Eng_AudioVoiceQueue`, with `Audio_Positional` → `Eng_AudioVoiceSpatial`. |
| `draw` | Talking indicators (a mic icon over speaking players, a party list) via `Eng_Ui*` — drawn **after** the game, so it overlays the HUD. |
| `shutdown` | Close capture + voice streams. |

`fixed` is unused (voice is wall-clock paced, not sim-locked). Because the plugin is a
`.so`, voice chat is added to any game on the engine **without recompiling the engine or
the game** — the point of the host.

### The one missing seam: peer → player identity/position

Positional voice needs to map a **net peer** to **that peer's player position** — and
the engine deliberately doesn't model "player." Today a plugin can read peer indices
from `net.h` but not where each peer's avatar is. Options (smallest first):

1. **Game pushes a context table** — the game, each frame, hands the plugin an array of
   `{peerId → Vector3 pos}` through a tiny registered callback. Cheapest; keeps the seam.
2. **A generic plugin-context API on the engine** — `Eng_Plugin*` gains a key→value
   context bag the game fills and plugins read. More general, more surface.

This is the "shared-context hook" already flagged in the plugin-host docs as the natural
next step. **For non-positional (party/lobby) voice it isn't needed at all** — that's the
sensible v1 milestone before positional.

---

## 8. Codec — raw first, Opus for real use

| Option | Bitrate (16 kHz mono) | Verdict |
|---|---|---|
| **Raw PCM16** | ~256 kbit/s per speaker | Fine on LAN for a 2–4 player proof; the zero-dependency v1. |
| **ADPCM / simple DPCM** | ~64 kbit/s | Tiny code, 4× smaller, no dep — a reasonable middle step. |
| **Opus** | ~16–32 kbit/s, excellent quality | **The right answer for shipping voice** — purpose-built for VoIP (built-in VAD, PLC, jitter resilience). Adds `libopus` as a dependency. |

Recommended path: **PCM16 for the first end-to-end loop** (prove capture→net→playback),
then **swap in Opus** behind the `codec` byte in the packet (so old/new can't be
confused) once the pipeline works. The `codec` field in §6 exists precisely to make that
swap non-breaking.

---

## 9. Phases (each independently demoable)

| Phase | Work | Proof |
|---|---|---|
| **1** | `Eng_AudioCapture*` (miniaudio, §4) | a `--mic-meter` dev mode prints `Eng_AudioCaptureLevel` — mic in works, headless of net. |
| **2** | `Eng_AudioVoice*` (§5) + loopback | capture → own speakers (monitor yourself), PTT-gated. |
| **3** | Voice packet + transport (§6) over `net.h`, PCM16 | **2-player non-positional voice** — the milestone. |
| **4** | Positional voice (§7 identity seam + `Audio_Positional`) | teammates' voices come from their location. |
| **5** | Opus codec (§8) + jitter buffer tuning | ship-quality bandwidth/quality. |
| **6** | Talking-indicator overlay + settings (device pick, mic gain, PTT bind) | polish. |

Phases 1–3 need **no** engine seam changes beyond the two new audio primitives; Phase 4
introduces the peer-context hook; many-speaker voice waits on Phase D netcode (§6).

---

## 10. Open decisions (defaults in **bold**)

1. **Capture backend:** **miniaudio with `MA_API static`** (zero new deps, §4) vs. SDL/
   PortAudio. Only switch if the static-linkage approach hits a real wall.
2. **Voice playback wrapper:** **`Eng_AudioVoice*` engine helper** (reusable, jitter
   buffer has a home) vs. plugin drives raylib `AudioStream` directly (less code for v1).
3. **Identity/position seam (Phase 4):** **game-pushes-context callback** (smallest, keeps
   the seam) vs. a generic engine `Eng_Plugin*` context bag (more general).
4. **Trigger:** **push-to-talk default** (predictable bandwidth, no hot-mic) with
   **VAD optional** (amplitude threshold over `Eng_AudioCaptureLevel`, later Opus VAD).
5. **Topology:** **mesh broadcast** for ≤4 players (each client sends to all — fine at the
   current cap) vs. server-mixed (host decodes+remixes+rebroadcasts one stream) once
   player counts rise with Phase D. Mesh first; revisit at scale.
6. **Codec:** **PCM16 v1 → Opus for ship** (§8), gated by the packet `codec` byte.
