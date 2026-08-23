# HDS Reverie — Architecture Assessment & Migration Plan

**Date:** 2026-08-22
**Scope:** Ground-truthed audit of the entire Reverie runtime (~4,300 LOC, 40 files) and its
HDS Resonance boundary, followed by a target architecture and a phased migration plan.
**Method:** Every subsystem was read directly (not from memory). Findings are anchored to real
`file:line`. A parallel multi-agent audit (subsystem readers + real-time-safety / API-ABI /
professional-gap / Resonance-boundary lenses) cross-checked against independent first-hand reads.

> **One-line verdict:** Reverie is a *clean, genuinely engine-agnostic Phase-1 runtime skeleton*
> whose bones are worth preserving, but it is **not yet middleware**. Roughly 80% of a
> professional (FMOD/Wwise-class) platform is unbuilt, and the parts that exist have **no
> control↔audio thread discipline** — the single most important thing to fix before anything else
> is layered on top.

---

## 0. How we got here (context)

Reverie was stood up 2026-08-12 as a standalone middleware (its own git repo,
`github.com/typicyl/Reverie`) and reached **Phase 4** (core → events/voices → mixer → spatial +
Resonance backend) over one to two days. It was then **orphaned**: the flagship "Heartbreak
Acoustics" effort wired the rich `hdsr` acoustic model *directly into Heartbreak* against the
Resonance fork, bypassing Reverie. Git history shows five commits and then silence. This mandate
revives Reverie as the product. The original `Docs/Architecture.md` remains a strong north star and
already anticipates most of this mandate (parameters, DSP graph, music-as-events, banks, C ABI as
the binding boundary, Studio, live-link); the gap is that **implementation stopped after Phase 4**.

---

## 1. Current Architecture

Reverie is a single static library, `ReverieRuntime`, plus a C++ facade and a flat C ABI. The
layering is honest and the dependency direction is correct: **everything points down; nothing points
at an engine.**

```
        SDK (public boundary)
   reverie::Engine  ── C++ facade (pimpl)   SDK/Include/Reverie/Reverie.h + SDK/Reverie.cpp
   reverie_*         ── flat C ABI           SDK/CAPI/reverie.h + reverie_c.cpp
        │  (thin wrappers over the runtime)
        ▼
   AudioEngine  ─────────────────────────── Runtime/Audio/AudioEngine.{h,cpp}
   (owns device, mixer, voices, sound table, events, spatial renderer; IS the IAudioRenderer)
        │
   ┌────┼───────────────┬───────────────┬────────────────┬───────────────┐
   ▼    ▼               ▼               ▼                ▼               ▼
 Voices  Mixer        Events          Spatial          Platform        Audio
 pool +  bus tree     layered         ISpatialRenderer  IAudioDevice    format/buffer/
 virtual routing      weighted-pool   ├ Panning (dep-   ├ Miniaudio     decoder
 -ization ducking     RNG events      │  free fallback)  └ Null(offline)  (miniaudio,
          sends       instances       └ Resonance(gated)                   hidden)
          snapshots
        │
        ▼
   Core: Types.h (POD Float3/Quat/Result/handles) + Log   Runtime/Core/
```

**Data flow (per audio block):**
`AudioEngine::RenderAudio` → `mixer.BeginBlock` (clear bus buffers) → `voices.MixToBuses`
(resample/upmix each real voice into its bus buffer; submit spatial voices' mono to the spatial
renderer) → `spatialRenderer.Render` → fold stereo into the Spatial bus →
`mixer.EndBlock` (topological bus evaluation → Master → device output).
([AudioEngine.cpp:137-161](../Runtime/Audio/AudioEngine.cpp))

**Threading (as built):** two threads with **no queue between them**. The control/game thread calls
the SDK directly, which mutates runtime state. The audio thread is the device callback
([MiniaudioDevice.cpp:29-35](../Runtime/Platform/MiniaudioDevice.cpp)), or `RenderOffline` pulled
synchronously for the Null backend. `VoiceManager` guards its voice list with a single `std::mutex`
that **both threads take**; `Mixer` has **no synchronization at all**.

**Subsystem inventory (all verified present and functional):**

| Subsystem | Files | What it does today |
|---|---|---|
| Core types & log | `Core/Types.h`, `Core/Log.{h,cpp}` | POD `Float3`/`Quat`, `Result` enum (explicit `: i32`, C-ABI-mirrored), `u32` handle aliases, C-function-pointer log sink with a stack-buffer formatter |
| Audio path | `Audio/AudioFormat`, `AudioBuffer`, `AudioDecoder`, `AudioEngine` | interleaved-f32 internal format; fully-decoded in-RAM `AudioBuffer`; miniaudio `ma_decoder` behind `AudioDecoder`; central `AudioEngine` owning the graph; shared_ptr sound table off the audio thread |
| Voices | `Voices/Voice.h`, `VoiceManager` | flat `vector<Voice>`, priority+age virtualization, spatial-slot acquire/release, per-voice linear resample/upmix |
| Mixer | `Mixer/Mixer.{h,cpp}` | Master + Music/SFX/Dialogue/Ambience/UI/Spatial; gain/mute/solo, sends, sidechain duck, peak meter, snapshots; Kahn topological `EndBlock` |
| Events | `Events/AudioEvent.h`, `EventSystem` | authored event → layers → weighted sound pool; probability, vol/pitch variance, loop, per-layer bus; instances group voices; per-event `maxInstances` steal-oldest; seeded RNG |
| Spatial | `Spatial/SpatialRenderer.h`, `PanningSpatialRenderer`, `ResonanceSpatialRenderer` | `ISpatialRenderer` seam; dependency-free constant-power panner; gated Resonance/vraudio HRTF adapter with variable→fixed re-block ring |
| Platform | `Platform/AudioDevice.h`, `AudioDeviceManager`, `MiniaudioDevice`, `NullDevice`, `MiniaudioImpl` | `IAudioDevice`/`IAudioRenderer`; miniaudio playback backend + headless Null; single-TU `MINIAUDIO_IMPLEMENTATION` |
| SDK | `SDK/Include/Reverie/Reverie.h`, `SDK/Reverie.cpp`, `SDK/CAPI/reverie.h`, `reverie_c.cpp` | pimpl C++ facade + flat C ABI (opaque handles, result codes, event builder) |
| Tests | `Tests/{Core,Event,Mixer,Spatial,Resonance}Tests.cpp` | deterministic headless suites on the Null backend, wired to ctest |

**Empty scaffold (git-tracked, zero source):** `Runtime/{Assets,DSP,Layers,Music,Parameters,
Routing,Serialization}` all exist but are empty. There is **no** `Tools/`, `Studio/`, or
`Integrations/` directory at all.

---

## 2. Existing Systems to Preserve

These are at or near a professional bar and should be **kept** (evolved internally, not rewritten).
They are the hardest parts to get right and Reverie already got them right.

1. **The engine-agnostic POD core.** `Types.h` pulls only `<cstddef>/<cstdint>`; `Float3`/`Quat`
   are standard-layout + trivially copyable; `Result` is exception-free with an explicit `: i32`
   underlying type mirrored 1:1 by the C ABI. No glm/entt/nlohmann/engine leakage anywhere.
   ([Types.h:11-68](../Runtime/Core/Types.h))
2. **miniaudio containment.** No `ma_*` type crosses any header; `MINIAUDIO_IMPLEMENTATION` is in
   exactly one TU; only the device + decoder see miniaudio. Grep-verified. This is the single most
   important architectural promise and it holds.
   ([MiniaudioImpl.cpp](../Runtime/Platform/MiniaudioImpl.cpp), [MiniaudioDevice.cpp:12](../Runtime/Platform/MiniaudioDevice.cpp))
3. **The pluggable spatial seam + fallback.** `ISpatialRenderer` with a dependency-free panner means
   Reverie has **no hard dependency** on Resonance; the vraudio adapter is the only file that touches
   vraudio and is fully `#if`-gated with a nullptr-factory stub; `AudioEngine::Init` falls back to
   panning cleanly. ([SpatialRenderer.h:38-62](../Runtime/Spatial/SpatialRenderer.h),
   [AudioEngine.cpp:38-49](../Runtime/Audio/AudioEngine.cpp))
4. **The decoupled sound table.** Each `Voice` captures its own `shared_ptr<const AudioBuffer>` at
   `Play`, so the audio thread never locks or looks up the sound map. Correct, professional-grade.
   ([Voice.h:26](../Runtime/Voices/Voice.h), [AudioEngine.cpp:116-120](../Runtime/Audio/AudioEngine.cpp))
5. **The mixer routing *model*.** Kahn topological sort over child→parent, send-source→dest, and
   sidechain→ducked edges with a cycle fallback; ancestor+descendant solo closure; exp-smoothed
   duck envelope. The *model* is correct and should be preserved; only its runtime contract is
   wrong (§3). ([Mixer.cpp:131-208](../Runtime/Mixer/Mixer.cpp))
6. **The voice virtualization *concept*.** Over-budget voices keep advancing their cursor silently so
   they re-materialize in phase — the right mechanism for large scenes.
   ([VoiceManager.cpp:195-260](../Runtime/Voices/VoiceManager.cpp))
7. **The event model's good seams.** `ResolveSound` callback decoupling, O(1) `EventId` lookup,
   instance-groups-voices with steal-oldest, seeded RNG for reproducibility, and *all* heavy work on
   the game thread (the audio thread never touches events).
   ([EventSystem.cpp](../Runtime/Events/EventSystem.cpp))
8. **The dual SDK boundary shape.** Opaque `u32` handles, a mirrored result enum, a genuinely flat C
   ABI (no STL in signatures, dllexport triad, event builder), a clean pimpl facade, and Null-backend
   offline render as a first-class config. The *shape* is right (hardening needed — §3).
9. **The deterministic Null-backend test discipline.** Offline pull + seeded RNG + byte-exact
   comparison is the correct CI-safe foundation to build on.
10. **The CMake consumption/gating pattern** for the optional Resonance fork (EXCLUDE_FROM_ALL,
    PRIVATE link, EXISTS guards, panning fallback with a warning).

---

## 3. Problems & Technical Debt

Ranked by severity. **Critical = correctness/real-time defect that must be fixed before production
use.** Every item is grounded.

### 3.1 CRITICAL — There is no control↔audio thread discipline (the "spine" defect)

This is one problem with several faces. It is the reason Reverie cannot yet ship, and it is what
every subsystem migration note points back to.

- **The audio callback blocks on a mutex the game thread holds.** `VoiceManager::MixToBuses` locks
  `mutex_` for the whole block, and that same mutex is held by `Play` (which does `push_back` + a
  full `std::sort`), `Stop`, `SetVoicePosition`, `SetMaxRealVoices`, and every count query. A
  game-thread `Play` mid-section stalls the audio callback → dropouts / priority inversion.
  ([VoiceManager.cpp:179](../Runtime/Voices/VoiceManager.cpp), acknowledged at
  [VoiceManager.h:12-14](../Runtime/Voices/VoiceManager.h))
- **The Mixer has *no* synchronization at all.** `CreateBus`/`SetBusVolume`/`AddSend`/`SetDuck`/
  snapshots mutate `buses_` with no lock while `EndBlock` iterates it on the audio thread.
  `CreateBus` does `buses_.push_back`, which can **reallocate the vector mid-`EndBlock`** while the
  audio thread holds `Bus&`/`Bus*` into it → **use-after-free**, not merely a torn read.
  ([Mixer.cpp:47](../Runtime/Mixer/Mixer.cpp), read at
  [Mixer.cpp:235-281](../Runtime/Mixer/Mixer.cpp))
- **Per-block heap allocation + `std::sort` on the audio thread.** `ReprioritizeLocked` allocates a
  `vector<usize>` and sorts it *every* block ([VoiceManager.cpp:162-171](../Runtime/Voices/VoiceManager.cpp),
  called at :265). `EndBlock` allocates a `vector<bool>` every block and can run the whole
  allocating `RebuildTopology` on the audio thread when a config change set `topoDirty_`
  ([Mixer.cpp:214,227](../Runtime/Mixer/Mixer.cpp)).
- **A decoded buffer can be freed on the audio thread.** `ReapLocked` runs inside `MixToBuses` and
  erases finished voices; if the reaped voice held the last `shared_ptr` (e.g. the sound was already
  `UnloadSound`'d), the multi-MB `AudioBuffer` free runs on the audio callback.
  ([VoiceManager.cpp:264](../Runtime/Voices/VoiceManager.cpp))
- **The cross-thread Resonance race.** `AcquireSource`/`ReleaseSource`/`SetEnvironment` touch the
  shared vraudio object from the game thread while `Render` runs on the audio thread, sharing no
  lock. ([ResonanceSpatialRenderer.cpp:120-138](../Runtime/Spatial/ResonanceSpatialRenderer.cpp))
- **No command queue exists** despite the Architecture doc promising "a lockless command-queue
  primitive." This is the missing foundation.

### 3.2 CRITICAL / MAJOR — ABI is not yet safe to bind

- **No versioning or forward-compat anywhere.** No `REVERIE_ABI_VERSION`, no runtime version symbol,
  no struct-size/version field. Worse, `reverie_default_config` **returns the config struct by
  value** — the moment a future version adds a field, an old host calling a newer DLL corrupts its
  stack. The config layout is effectively frozen forever.
  ([reverie.h:58-68](../SDK/CAPI/reverie.h))
- **The C ABI (the recommended binding boundary) is a strict *subset* of the C++ facade** — no
  `reverie_unregister_event` (→ permanent event leak on the recommended path), no
  `reverie_is_initialized`, no bus muted/soloed getters.
- **Error detail is collapsed to 0/invalid ids.** The runtime distinguishes FileNotFound vs
  DecodeError vs OutOfMemory, but the boundary discards it; there is no `reverie_last_error()` /
  result-returning load variant / `reverie_result_string()`.
- **The public include tree is not self-contained.** `SDK/Include/Reverie/Reverie.h` includes
  `"Core/Types.h"`, which only resolves because CMake marks the *whole* `Runtime/` directory PUBLIC
  — so a consumer can `#include "Mixer/Mixer.h"` and bypass the facade, and shipping the SDK means
  shipping internal headers. ([CMakeLists.txt:48-51](../CMakeLists.txt))
- **The threading contract is undocumented at the boundary** — integrators will guess and race.
- **No host pump / `reverie_update(dt)`.** There is no per-frame tick to drain a command queue,
  advance parameters/music, and dispatch callbacks on the host thread (the FMOD `System::update`
  pattern). The only pull is `RenderOffline` for the Null backend.
- **No callbacks/notifications** (event-finished, instance-stopped, device-lost, bank-loaded) — so
  gameplay cannot sync to audio without polling.
- **Smaller correctness/contract nits confirmed:** the C and C++ `Result` enums are hand-duplicated
  and bridged by a bare `static_cast` with **no `static_assert`** tying them together (silent
  mis-map risk); `const char*` returns (`spatial_backend_name`) and the `LoadPCM` input buffer have
  **unspecified ownership/lifetime** (P/Invoke free-hazard); `max_voices==0` means 64 via the C ABI
  but 1 via the C++ facade ([reverie_c.cpp:47](../SDK/CAPI/reverie_c.cpp) vs
  [Reverie.cpp:50](../SDK/Reverie.cpp)).

### 3.3 MAJOR — Advertised features that are only scaffolding

- **Concurrency groups do nothing.** `concurrencyGroup` is stamped onto voices but no per-group cap
  is ever enforced. ([EventSystem.cpp:109](../Runtime/Events/EventSystem.cpp),
  [VoiceManager.cpp:38](../Runtime/Voices/VoiceManager.cpp))
- **No total-voice ceiling and no stealing.** `Play` unconditionally `push_back`s; `maxReal_` only
  flips a `virtualized` flag. A runaway `Play` loop grows the vector without bound; despite header
  talk of "steal-oldest", nothing is ever evicted. ([VoiceManager.cpp:52](../Runtime/Voices/VoiceManager.cpp))
- **Streaming is unwired.** `AudioStream` exists but has **no consumer** anywhere; every sound is
  fully decoded into RAM. Music of any length is a full in-RAM decode.
  ([AudioDecoder.h:30](../Runtime/Audio/AudioDecoder.h))
- **No async decode.** `LoadFile` blocks the caller on a full synchronous decode.
- **Handle "generation guard" is documented but not implemented.** `Types.h:57-60` promises stale
  handles can't alias recycled slots; ids are actually bare wrapping counters. Either implement it or
  correct the comment.
- **`SpatialQuality` is a no-op for Resonance** (every source is `kBinauralHighQuality`); backends
  diverge on distance model (linear vs logarithmic) and capacity (128 vs 64).
- **`AcousticEnvironment` wall materials are silently discarded** — coefficients hardcoded to 0.5.

### 3.4 MAJOR — DSP / signal-quality gaps

- **No per-bus DSP insert chain** (no EQ/compressor/limiter/reverb-insert). The `Bus` struct has no
  effect slot; `EndBlock` only applies a scalar gain.
- **No gain smoothing** — bus gain and voice volume are hard block-constant multiplies → zipper
  noise / clicks. (Duck *is* smoothed; user gain is not.)
- **Linear-interpolation-only resampling** → audible aliasing on pitch-shift/downsample.
- **Crude up/downmix** — mono→stereo with no −3 dB compensation; >2ch collapsed to front L/R;
  center/LFE/surround discarded; output effectively stereo-only.
- **No master limiter / clip protection; no denormal (FTZ/DAZ) guard.**
- **Snapshots capture only gain+mute** (not sends/duck/solo/effects) and apply instantly (no blend).

### 3.5 MAJOR — Whole professional pillars are ABSENT

Verified against empty scaffold directories and grep:

| Pillar | Status | Evidence |
|---|---|---|
| Asset pipeline & cooking | **absent** | `Runtime/Assets/` empty; only runtime miniaudio decode |
| Cooked `.HDSRF` container + streaming/seek/chunking | **absent** | `Runtime/Serialization/` empty; no format at all |
| Project format & authoring/runtime split | **absent** | events built imperatively in code; no GUIDs, no load/save |
| Parameters / RTPC | **absent** | `ParameterId` typedef referenced nowhere; `Runtime/Parameters/` empty |
| DSP effects & chains | **absent** | `Runtime/DSP/` empty; no `IAudioEffect` |
| Adaptive/interactive music | **absent** | `Runtime/Music/` empty; only a bus named "Music" |
| Profiling / debug live-link | **absent** | only peak meters + voice counts |
| Format reliability/validation/versioning | **absent** | nothing versioned to validate |
| Standalone authoring app (Studio) | **absent** | no `Studio/`/`Tools/`/`Integrations/` |
| Multichannel / surround output | **absent** | hard stereo everywhere |

### 3.6 MAJOR — The rich acoustics live in the wrong place

The fork's `hdsr` layer (per-band materials, Eyring RT60, per-band transmission, a room→room
best-path propagation graph, multi-environment reverb with coupling/pre-delay/directional pan,
diffraction) is genuinely engine-agnostic POD math — but Reverie **never consumes it**. It was
integrated one layer too high, *directly into Heartbreak*
(`Source/Audio/AcousticWorld`, `SpatializerResonance`). So Reverie, the middleware, is a
generation behind the fork it wraps, and its headline "sounds like a place" capability is trapped in
a single consumer. ([ResonanceSpatialRenderer.cpp:166-190](../Runtime/Spatial/ResonanceSpatialRenderer.cpp);
`HDSResonance-Audio/hdsr/acoustics.h`, `environment_reverb.h`)

### 3.7 MINOR (representative)

O(n) linear scans on every voice control op; no de-click ramps on stop/steal/virtualize; virtual
spatial voices keep occupying scarce source slots; miniaudio init failure yields a "zombie" device
instead of nullptr; no device enumeration / hot-plug / fallback-to-Null; `std::uniform_real_distribution`
is not cross-toolchain deterministic; test harness copy-pasted across 5 files; no CI committed; no
sanitizer/coverage builds; asset-ingestion/streaming/resample/concurrency paths are entirely
untested; `REVERIE_ASSERT` claims to trap in debug but only logs; the default log sink does blocking
stderr I/O.

---

## 4. Recommended Target Architecture

The original `Docs/Architecture.md` design is sound and largely unchanged here; what follows makes
the **runtime contract**, the **format strategy**, and the **HDS Resonance boundary** concrete.

### 4.1 The non-negotiable invariant: a real-time-safe control↔audio boundary

Everything else is built on this. The audio thread must be **wait-free**: no locks it can block on,
no heap allocation, no unbounded work, no deallocation.

```
   CONTROL / GAME THREAD                         AUDIO THREAD (device callback)
   ─────────────────────                         ──────────────────────────────
   SDK calls (Play/Stop/SetParam/SetBus/…)       RenderAudio(block):
        │ allocate handle (atomic)                  1. drain command ring  (wait-free pop)
        │ build POD command                         2. apply to voices_/buses_ (audio owns them)
        ▼                                           3. mix → DSP → spatial → mixer → output
   command ring  ──(SPSC, lock-free)──►            4. push finished shared_ptrs to retire ring
                                                          │
   retire ring   ◄──(SPSC, lock-free)──────────────────┘
        │ drained on control thread → buffers freed OFF the audio thread
```

- **Command ring** (control→audio): a bounded, lock-free single-producer/single-consumer ring of
  POD commands. The control API is documented **single-threaded** (call from the game thread) — the
  same contract FMOD/Wwise assume. Overflow is bounded and logged, never blocking.
- **Handles allocated synchronously** on the control thread (atomic counter with a generation field)
  so callers get a valid id immediately; the audio thread realizes the object at drain time.
- **Stats published via atomics** the audio thread writes (real/virtual/active counts), so control
  queries never touch `voices_`.
- **Retire ring** (audio→control): finished `AudioBuffer`/voice storage handed back for release on a
  non-audio thread.
- **Scratch preallocated at Init** from `maxVoices` × max block size × max channels: voice order
  buffer, bus buffers, solo-audibility and topology working sets. `BeginBlock`/`EndBlock`/
  `MixToBuses` never allocate.

**Drain points.** On a real device the audio thread drains the ring at the top of its callback; for
the Null backend, `RenderOffline` drains it synchronously (so tests stay deterministic). A future
`reverie_update(dt)` host pump (§4.3) advances params/music and dispatches host-side callbacks, but
never touches the audio-owned state directly.

**Two safe patterns, applied where each fits.** The command ring suits high-frequency, additive
mutations (play/stop/set-param/set-position). For the **bus graph** — low-frequency but
structure-changing (create bus, add send) — an RCU-style *atomically-published immutable snapshot*
is cleaner: build the new graph on the control thread, publish a pointer the audio thread picks up at
block start, retire the old one via the retire ring. The listener pose is published via a seqlock /
double-buffer so the audio thread never reads a torn `{pos, fwd, up}`.

This is a **contained internal change** — the public API (`Play` returns a `VoiceId`, etc.) does not
change shape; the mutex and the races disappear behind it.

### 4.2 Module structure (fills the existing empty scaffold)

```
Runtime/
├── Core/          Types, handles(+generation), Result, Log(RT-safe path), CommandQueue, RetireQueue
├── Platform/      IAudioDevice (+enumeration/hotplug/fallback), miniaudio + Null; decoders
├── Audio/         AudioEngine, block pipeline, FTZ/DAZ guard
├── Voices/        pooled handle voices, budget + stealing + concurrency groups, ramps
├── Mixer/         buses, sends/returns, snapshots(full state + blend), metering(peak/RMS)
├── DSP/           IAudioEffect + per-bus insert chain (EQ/compressor/limiter/delay/reverb/filter)
├── Routing/       signal-graph helpers (precomputed indices for the topo walk)
├── Parameters/    generalized parameter store (RTPC): global + per-instance, smoothing, automation
├── Events/        recursive container tree (random/sequence/switch/blend) over the leaf pool
├── Layers/        triggering rules, conditions, delays, fades (attributes on container nodes)
├── Music/         musical clock (tempo/bars/beats), states, synced layers, quantized transitions
├── Spatial/       ISpatialRenderer(+materials/multi-env/per-band occlusion/diffraction) + panning
│   └── Resonance/ vraudio + hdsr adapter (the ONLY TUs touching either) — see §4.4
├── Assets/        runtime asset + bank loading; async load; streaming voices
└── Serialization/ project (text, GUID-keyed) + bank (.HDSRF binary, versioned) formats
SDK/{Include,CAPI}  hardened, versioned C ABI (§4.3) + C++ facade
Tools/              Baker / BankBuilder / AssetProcessor (offline cook)
Studio/             standalone authoring app over the SDK
Integrations/       Heartbreak / Unity / Unreal — thin adapters over the C ABI only
Tests/              shared harness; decode/stream/resample/concurrency/lifecycle/format tiers
```

### 4.3 The stable v1 C ABI

Freeze a real contract **before any engine binds it** (cheap now, expensive after Heartbreak ships):

- `#define REVERIE_ABI_VERSION 1` + a runtime `unsigned int reverie_abi_version(void)`; validate at
  `reverie_create`/`init`.
- Every public struct gains a leading `{ uint32_t struct_size; uint32_t abi_version; }`; **pass
  configs by pointer**, never by value, so fields can be added compatibly.
- The C ABI becomes a **superset** of the C++ facade (add `unregister_event`, `is_initialized`,
  bus getters, per-instance/per-voice control, fade variants).
- Add a real error path: result-returning load/create variants with out-params, plus
  `reverie_result_string`.
- Relocate `Types.h` under `SDK/Include/Reverie/Core/`; make **only** `SDK/Include` + `SDK/CAPI`
  public; demote `Runtime/` to PRIVATE.
- Document the threading model explicitly; a notification/callback mechanism marshals to a
  host-pumped queue (never fires on the audio thread).
- Distinct handle types (a one-field `Handle<Tag>` that still lays out as `u32`) so the six id kinds
  stop being silently interchangeable.

### 4.4 The HDS Resonance boundary — move the rich model *down* one layer

Keep the vraudio isolation exactly as-is; additionally consume `hdsr` **beneath** the
`ISpatialRenderer` seam, in the same quarantined adapter (hdsr is engine-agnostic POD math, so this
pulls in no engine/physics dependency). Enrich the abstraction with **backend-neutral** acoustic
types that *map onto* hdsr but don't require it:

- a Reverie `AcousticMaterial` (per-band absorption/scattering/transmission) or registered material
  handles, replacing the opaque `u32 wallMaterials`;
- `SetEnvironments(span)` with per-environment per-band coupling (not a single global room);
- per-band (spectrum) occlusion instead of one scalar; an optional diffraction/apparent-position
  hook.

The panning backend ignores the new fields; the Resonance adapter honors them by calling
`hdsr::ComputeRoomAcoustics` / `hdsr::EnvironmentReverb` / `hdsr::SolvePropagation`. Then Heartbreak
refactors to feed Reverie geometry *results* and becomes just another consumer — every future
Reverie consumer inherits the full acoustics for free.

### 4.5 `.HDSRF` (Hollow Dream Studios Reverie File) direction

A **container around proven encoders**, not a new codec: `magic + version + header + metadata + seek
table + chunk[0..N] + loop/cue info`. Cook picks per-asset strategy (short SFX → preload/low-latency
decode; long music/voice → chunk-streamed). Streaming = background IO worker → decode → ring →
audio thread, never blocking the callback. Versioned from day one, with load-old-version regression
fixtures. This unblocks assets, cooking, and Studio, none of which can exist without it.

---

## 5. Migration Plan (phased, each phase leaves the repo green)

The ordering is by **foundational dependency**, not by visible feature. Every phase ends with a
building repo and passing tests. Baseline verified 2026-08-22: all four suites PASS in Release.

| Phase | Goal | Why here | Validation |
|---|---|---|---|
| **P1. RT-safe boundary** | Lock-free SPSC command + retire rings; preallocated scratch; deferred buffer release; remove the audio-thread mutex and Mixer races; atomic stats | Everything else adds cross-thread traffic on top of a model that is currently racy. Fix the spine first. | new concurrency/RT tests under TSan; existing suites unchanged |
| **P2. ABI hardening** | Version handshake, size/version-stamped structs, by-pointer configs, C-ABI superset, error path, self-contained include tree, documented threading | The binding window closes the moment Heartbreak ships against it — do it pre-1.0 | ABI version test; negative/lifecycle tests |
| **P3. Serialization + `.HDSRF`** | Versioned binary bank + text GUID project format; magic/version/validation | Unblocks assets, cooking, streaming chunks, Studio | round-trip + malformed + old-version fixtures |
| **P4. Parameters (RTPC)** | Generalized parameter store fed by the command queue; smoothing/automation | Substrate every higher pillar (DSP automation, music, distance/occlusion) consumes | parameter-drive tests |
| **P5. DSP effects** | `IAudioEffect` + per-bus insert chain (EQ/comp/limiter/delay/reverb/filter); master limiter; gain ramps; denormal guard | Reuses the existing topological `EndBlock` spine | per-effect + zipper-free tests |
| **P6. Streaming + async assets** | Wire `AudioStream` into streaming voices + background IO ring; async load; banks | The API already exists; needs a consumer + worker | seek/loop/EOF/underrun tests |
| **P7. Resonance boundary uplift** | Consume `hdsr` below the seam; enrich abstraction (materials/multi-env/per-band occlusion/diffraction); reconcile quality/capacity | Recovers the middleware's headline capability from Heartbreak | with/without-fork parity tests |
| **P8. Adaptive music** | Musical clock (tempo/bars/beats), states, synced layers, quantized transitions, stingers | Built on P4 parameters + a clock | sync/transition determinism tests |
| **P9. Event model uplift** | Recursive container tree (random/sequence/switch/blend), conditions, delays, fades, portable RNG, per-group limits, per-layer spatial | Old flat defs upgrade 1:1 as "blend of random leaves"; needs P4 | container-behavior + upgrade tests |
| **P10. Studio + Integrations + Live-Link** | Standalone authoring app over the SDK; Heartbreak/Unity/Unreal adapters; live-edit protocol | Consumes the stable SDK + formats | end-to-end |
| **P11. Reliability/profiling/perf** | Profiler live-link, stress tests, sanitizer CI, optimization | Hardening pass | large-scene stress + CI matrix |

**Cross-cutting, done alongside every phase:** shared test harness + committed CI (matrix incl.
`REVERIE_WITH_RESONANCE=ON` and ASan/UBSan/TSan legs); versioned formats from introduction;
the two invariants that already hold today held forever — nothing above the backends includes an
engine/glm/entt/nlohmann type, and miniaudio/vraudio/hdsr stay confined to their adapter TUs.

---

## 6. Implementation status (living log)

Per the mandate, implementation began with **Phase 1** (the RT-safe boundary) and the safe/additive
parts of **Phase 2** (ABI hardening). Every item below landed against a green build (`ctest` all
suites passing); the baseline and each step were verified on 2026-08-22.

**Phase 1 — real-time-safe control↔audio boundary:**
- `Runtime/Core/SpscRing.h` — bounded lock-free wait-free SPSC ring (substrate for command/retire
  queues), with `ReverieRingTests` (FIFO, wraparound, move-only no-leak, 2M-item two-thread stress).
- `VoiceManager` — **removed the audio-thread mutex**: replaced the mutex-guarded `std::vector<Voice>`
  with a **fixed lock-free voice pool** (`unique_ptr<Voice[]>`), a `Free→Playing→Stopping` atomic
  state machine, synchronous counts preserved, spatial-source teardown + `Free` published on the
  audio thread, and buffer frees deferred to the control thread (no heap free in the callback). Also
  removed the per-block allocation + sort. `ReverieConcurrencyTests` stresses it (one control thread
  vs one render thread, 300k ops; 80+ clean runs). Total voices are now bounded (pool cap).
  **Reviewed by three independent agents** (atomics/memory-ordering, state-machine/teardown,
  behavior-parity): the pool itself is confirmed correct. They found one real bug I had introduced —
  the spatial source pool was touched from both threads (`AcquireSource` on control vs the audio
  thread's calls) — now **fixed** by moving source-slot acquisition onto the audio thread (lazy, in
  `MixToBuses`), deferring `SetEnvironment` to the audio thread (fixes a pre-existing race too), and
  documenting the `ISpatialRenderer` "audio-thread-only" contract.
- `Mixer` — eliminated the crash-class **use-after-free**: `buses_`, the id/name maps, and per-bus
  `sends` are reserved to hard caps so they never reallocate/rehash under the audio thread; removed
  the per-block `vector<bool>` allocation. *(Remaining: scalar gain/mute/solo torn-reads are benign
  today; full lock-free mixer config is a later increment.)*

**Phase 2 — ABI hardening (done):**
- Version handshake (`REVERIE_ABI_VERSION` + `reverie_abi_version()`); `static_assert` pinning the C
  and C++ result enums together.
- Growable, size-stamped `reverie_config` passed **by pointer** (`reverie_default_config(out)`) — no
  more stack-corruption risk when the struct grows.
- C ABI is now a **superset** of the C++ facade (`reverie_is_initialized`, `reverie_unregister_event`,
  `reverie_get_bus_muted/soloed`, `reverie_result_string`).
- Unified the `max_voices==0` default (one place); documented the threading contract in both public
  headers; made the **SDK self-contained** (`Types.h` moved to `SDK/Include`, `Runtime/` is private).

**Phase 4 — parameters (RTPC):**
- `Runtime/Parameters/ParameterStore` — a **lock-free named-parameter store** (fixed pool, atomic
  current/target, per-block smoothing advanced on the audio thread), exposed through the SDK/C ABI.
- **Wired as a real consumer:** voices are now **parameter-modulatable** (`Voice.volumeParam` →
  `volume *= smoothstep(lo,hi, param)` on the audio thread), which powers music-layer gains and
  general volume automation. Resolves the audit's "`ParameterId` referenced nowhere."

**Phase 5 — DSP effect chain:**
- `IAudioEffect` + a per-bus **pre-fader insert chain** processed in the topological `EndBlock`,
  with an `EffectId` registry (stable heap pointers). Built-ins: **biquad filter** (7 types),
  **compressor/limiter** (fixes the audit's "no master limiting" gap), and **feedback delay**.
  All real-time-safe (state allocated in `Prepare`, none in `Process`). SDK/C-ABI:
  `AddBusEffect`/`SetEffectParam`/`EffectParam`. `ReverieDspTests` verifies filter response,
  limiting, and the delay tail.

**Phase 3 — serialization & the `.HDSRF` cooked audio format:**
- `Serialization/BinaryStream` (bounds-checked LE) + `Bank` — a **versioned bank** (magic/version/
  validation) of the mixer bus tree + parameters, round-tripped into a fresh engine; corrupt/newer/
  truncated banks are rejected. SDK/C-ABI `SaveBank`/`LoadBank` (2-call size query).
- `Serialization/Hdsrf` — the **`.HDSRF` container**: chunked 16-bit PCM with a byte-offset **seek
  table** (the structure streaming needs), versioned + validated, ~half the size of f32. Cook +
  full-decode + single-chunk-decode; `Engine::LoadSoundHdsrf` / free `CookHdsrf`; SDK/C-ABI.
  `ReverieHdsrfTests` covers round-trip tolerance, chunk decode, size, and rejection of bad blobs.

**Phase 8 — adaptive music (first slice):**
- `Music/MusicClock` (tempo/bars/beats + `BeatsUntil` quantization math) + `Music/MusicSystem` —
  states of synchronized looping layers whose gains are parameter-driven; `SetMusicState` switches
  (immediate for now; beat/bar-quantized crossfade builds on the clock next). Clock advances on the
  audio thread and is published for control queries. SDK/C-ABI (music-state builder + transport +
  beat/bar/bpm queries). `ReverieMusicTests` verifies the clock tempo math, param-driven layer
  gating, and teardown.

**Also landed since:** bar/beat **quantized music transitions** + a `reverie_update` **host pump**;
**async asset loading** (background loader thread + poll); DSP **compressor/limiter + delay** and
per-sample **gain smoothing** (de-zippers volume/mute/solo/duck); **event-layer parameter
modulation**; and a **`reverie-cook` tool** (source audio → `.HDSRF`, with a CI self-test).

**Phase 10 — Reverie Studio authoring application:**
- **Authoring core (headless, SDK-only):** `Studio/StudioProject` is the editable, source-control-
  friendly document — assets, the bus tree, parameters, events, and music states, all keyed by
  **name** (not runtime ids) so it round-trips through a versioned human-readable text file
  (`RVPROJ`). `Studio/StudioApp` **builds** a project into a live `reverie::Engine` (loads assets,
  creates buses, registers params/events/music, resolves every name→id) for preview, and is the
  seam a future cooker reuses. This layer speaks **only** the public SDK — no runtime internals, no
  ImGui. `reverie-studio` CLI (new/info/`--selftest`); `ReverieStudioTests` + `reverie_studio_selftest`.
- **GUI editor (`reverie-studio-gui`, Dear ImGui docking):** a strict layered presentation over that
  model — **User input → editor command → authoring model → runtime/preview**; no business logic in
  ImGui callbacks. `Studio/App/`:
  - **Platform/renderer are replaceable:** the editor talks only to `IStudioBackend`
    (`StudioBackend.h`); the concrete **Win32 + DX11** implementation (`Win32Dx11Backend.cpp`) is the
    *only* TU that includes Win32/DX11/ImGui-backend headers. Swapping in GLFW+Vulkan/Metal for
    Linux/macOS is a new backend TU, no editor changes. Reverie Studio is **not** Win32/DX11-bound.
  - **Real undo/redo** (`EditorCommand.h`): a transaction stack of redo/undo closures (not snapshots);
    every model mutation (add/delete/edit) routes through it. Sliders record **one** command per drag.
  - **Panels** (`Panels.cpp`, dockable): Asset Browser, Inspector (context-sensitive per selection),
    **Mixer** with **custom draw-list VU meters** (live `BusMeter` while previewing), Events, **Music**
    with a **custom-rendered bars/beats timeline** + transport, **Profiler** (real `GetStats()`:
    voices/CPU/peak/clock + a CPU history plot), Console (filterable log), Project Settings.
  - **Preview** drives the live engine (a real device when available, else the Null backend, reported
    honestly in the status bar); Build (Ctrl+B) rebuilds the model into the engine; Play/Stop.
  - Reverie **dark theme** (`Theme.cpp`, no stock ImGui look, no emoji), docking + multi-viewport,
    menu/toolbar, keyboard shortcuts, layout persisted to `reverie_studio_layout.ini`.
  - Every visible control either works or is explicitly labelled **planned** (native file dialog,
    clip/region timeline editing, project-global settings). **Compiles + links clean**; runtime/SDK/
    cooker/`.HDSRF`/model/HDS-Resonance remain 100% ImGui-free (ImGui is fetched only for the GUI exe).

**Test suite now (15, all green, clean build + concurrency stress):** core, ring, events, mixer,
params, dsp, bank, music, hdsrf, spatial, resonance, concurrency, cook-selftest, **studio**,
**studio-selftest**.

**Delivered this program:** P1 (RT-safety) · P2 (ABI hardening) · P3 (serialization + `.HDSRF`) ·
P4 (parameters, wired into voices/buses/events) · P5 (DSP) · P6-partial (async load + cooker) ·
P8 (adaptive music + quantized transitions) · **P10 (Studio authoring core + Dear ImGui GUI editor)** ·
the host pump.
**Still to do (large / separate):** true chunk-**streaming** voices during playback (background
reader over the `.HDSRF` seek table); a project **cooker** (Studio project → runtime banks, reusing
`StudioApp::Build`); engine **integrations** (Heartbreak/Unity/Unreal thin C-ABI clients); snapshot
blend/full-state; profiling/live-link; event containers (random/sequence/switch) + conditions/delays/
fades; callbacks via the host pump; Studio polish (native file dialogs, waveform/spectrogram cache,
spatial editor, clip-level music timeline editing, a non-Win32 backend). Remaining Phase-1 minors:
full lock-free mixer config; per-block scratch preallocation.
