# HDS Reverie — Architecture & Phase 1 Plan

Engine-agnostic game-audio middleware. Heartbreak, Unity, and Unreal are clients.
Reverie does not depend on any of them. HDS Resonance Audio is one spatial backend.

This document is grounded in a direct inspection (2026-08-12) of the actual repository:
the empty `Reverie` repo, the `HDSResonance-Audio` fork, and Heartbreak's audio layer.
File:line anchors below are real.

---

## 1. Current state of the Reverie directory

`V:\HBENGINE\HeartbreakEngine\Reverie` is a fresh, effectively empty git repository:
- `origin` = `github.com/typicyl/Reverie.git`, one commit (`Initial commit`), branch `main`.
- Only `.gitattributes` is tracked (LF normalization). No source, no build, no docs yet.
- It is a sibling of `HeartbreakEngine/` and `HDSResonance-Audio/` under the same parent
  folder, but it is its **own** repository (this is correct: Reverie ships independently).

Conclusion: a clean slate. No existing code to preserve inside Reverie itself; all
"preserve existing functionality" obligations are about Heartbreak and the Resonance fork,
which Reverie must not disturb.

---

## 2. Relevant existing Resonance functionality (verified)

The HDS fork at `../HDSResonance-Audio` is stock upstream Google Resonance Audio; per its
`MODIFICATIONS.md` there are **no source/DSP/API changes**, only a README note and a CMake
policy-floor flag. So the public surface is canonical upstream and stable to target.

**The entire spatial surface is one C-ABI-friendly abstract class**,
`vraudio::ResonanceAudioApi` (`resonance_audio/api/resonance_audio_api.h`), created by
`extern "C" CreateResonanceAudioApi(num_channels, frames_per_buffer, sample_rate_hz)` (:133).

Capabilities, all verified present:
- **Rendering modes** (`RenderingMode`, api.h:37-56): stereo panning (no HRTF), binaural
  low/medium/high = 1st/2nd/3rd-order Ambisonics HRTF (8/12/26 virtual speakers), and
  room-effects-only.
- **Source types**: `CreateSoundObjectSource(mode)` (point source), `CreateAmbisonicSource`
  (pre-encoded FOA/2nd/3rd bed), `CreateStereoSource` (non-spatial passthrough); `DestroySource`.
- **I/O**: interleaved + planar, float + int16, both directions (`SetInterleavedBuffer` /
  `FillInterleavedOutputBuffer`, api.h:159-287).
- **Listener**: `SetHeadPosition`, `SetHeadRotation(x,y,z,w)`, `SetMasterVolume`,
  `SetStereoSpeakerMode` (headphones vs speakers).
- **Per-source**: position, rotation, volume, distance model/attenuation, room-effects gain,
  directivity, listener directivity, near-field gain, occlusion intensity, spread (api.h:297-394).
- **Room / acoustic environment**: `EnableRoomEffects`, `SetReflectionProperties` (shoebox:
  position/rotation/dimensions + 6 per-wall coefficients + cutoff + gain), `SetReverbProperties`
  (9-band RT60 + gain) (api.h:398-409). A **24-material octave-band absorption table**
  (`platforms/common/room_properties.h:38-64`, coefficients in `room_effects_utils.cc:49-120`).

The DSP + room modules (`resonance_audio/dsp/*`, `platforms/common/room_effects_utils.cc`)
are **real implementations, not stubs** (shoebox image-source reflections, spectral reverb,
occlusion filter, SH-HRIR from SADIE, near-field, distance attenuation, pffft-backed FFT).

**Hard constraints that shape the abstraction** (from `graph/resonance_audio_api_impl.cc`):
- Single instance = one listener + one stereo binaural mix + one global room. Multi-listener
  or multi-room requires N instances.
- **Output is hard-locked to stereo** (`LOG(FATAL)` otherwise, impl.cc:78-81). No native 5.1/7.1.
- **Fixed `frames_per_buffer` and sample rate at construction**; a mismatched buffer is dropped
  with a warning (impl.cc:540-543). The seam must re-block/resample.
- Practical ceiling ~512 sources (impl.cc:41). Setters are non-blocking (lockless queue),
  applied on the audio thread inside the fill call.
- Coordinate convention: **right-handed** world space (room_properties.h:84-94); quaternions
  enter as (x,y,z,w).

**Platform integrations that exist as code** under `platforms/`: `common`, `unity` (native +
full C# package), `fmod` (DSP plugin), `wwise` (mixer FX + room-effects attachment + authoring),
`vst` (binaural monitor). **Unreal exists only as documentation** (`docs/develop/unreal/*.md`),
there is no `platforms/unreal` code. These are **reference templates** for Reverie's own
adapters, not code to copy.

Build: `BUILD_RESONANCE_AUDIO_API=ON` produces `ResonanceAudioStatic` + `ResonanceAudioShared`
(object libs `ResonanceAudioObj` + `SadieHrtfsObj` + `PffftObj`). Deps: Eigen (header-only,
`-DEIGEN_MPL2_ONLY`), pffft (compiled in), SADIE HRTF (compiled into the lib). Public header
needs only `<cstddef>`/`<cstdint>` — a Reverie TU talking to the object API needs **no Eigen**.

---

## 3. Relevant existing Heartbreak audio architecture (verified)

Heartbreak's audio is already a clean middleware-shaped facade — good news, because it is the
model for what Reverie generalizes, and it shows exactly what must NOT leak into Reverie.

- **`AudioSystem`** (`Source/Audio/AudioSystem.{h,cpp}`) is a `NonCopyable` pimpl whose only
  implementation dependency is miniaudio (`MINIAUDIO_IMPLEMENTATION` lives only in
  AudioSystem.cpp:12-14). The header includes just `Core/Types`, `UI/Subtitles`, `glm`. **This
  is precisely the seam Reverie occupies.**
- **Bus tree**: `ConfigureBuses(vector<AudioBusDesc{name,parent,volume,muted}>)` + set
  volume/mute; `ma_sound_group` per bus, implicit Master, topological parent resolve, unknown
  bus falls back to Master (AudioSystem.cpp:501-586).
- **Events (.hbevent)**: weighted sound pool + volume/pitch variance + loop + spatial min/max
  + bus (`AudioEvent.h:19-38`). `PostEvent` picks weighted-random and routes. (Note: currently
  only editor code posts events; no runtime/schematic caller yet — Reverie can define the
  runtime event path cleanly.)
- **Adaptive music (.hbmusic)**: `MusicState` = synced looping `MusicLayer`s; per-layer gain =
  `volume * smoothstep(paramLo, paramHi, param)`; named runtime `MusicParameter`s; crossfade;
  **beat/bar-quantized transitions** (musicClock + `MusicSync{Immediate,Beat,Bar,TwoBars,FourBars}`,
  outgoing state owns the boundary); **dialogue ducking** (attack/release dB floor)
  (`MusicGraph.h:26-88`, AudioSystem.cpp:773-966). This is a complete interactive-music spec.
- **Occlusion**: a bespoke multi-ray probe (direct ray + spread ring so sound leaks through
  gaps) driving a per-voice LPF + volume floor glided ~150ms; the geometry test is **injected
  as `std::function<bool(vec3,vec3)>`** so AudioSystem has no physics dependency
  (AudioSystem.cpp:175-235).
- **Captions**: `subtitle::Line{speaker,text,kind,priority}` + `subtitle::Stack` — already has
  **zero engine/audio coupling** (Subtitles.h), a clean pattern to mirror.
- **ECS components** (`Scene/Components.h`): `AudioSource`, `DialogueActor`, `MusicZone` — the
  surface a Reverie integration replaces with `ReverieAudioSource`/`ReverieListener`/`ReverieAudioZone`.
- **Gameplay triggers**: a deferred `game::` queue (latest-wins slots + FIFO) drained each
  frame; schematic nodes enqueue `SetMusicState`/`SetMusicParam`/`PlayStinger`/`PlayVoiceline`/
  `PlayDialogue` (GameSystems.h:74-125, Engine.cpp:2168-2216).
- **The existing Resonance bridge** (`SpatializerResonance.{h,cpp}`, built this session) is the
  **consumption template**: a separately-published sibling repo consumed via
  `add_subdirectory(EXCLUDE_FROM_ALL)`, static-lib, PRIVATE-linked behind a `HBE_HAVE_RESONANCE`
  gate, with ALL glue engine-side and nothing Heartbreak-specific in the fork
  (Dependencies.cmake:478-506). **Reverie is consumed the same way.**
- **Per-frame contract** (Engine.cpp:2117-2274): set occlusion config, build a physics-raycast
  `segmentBlocked`, `UpdateScene(listenerPose, sources, gamePlaying, segBlocked, dt)`, `Update()`,
  drain the deferred queues, MusicZone containment, ducking, `UpdateMusic(dt)`, drain captions.

**Heartbreak coupling Reverie must NOT inherit**: entt (ECS walk keys voices by `entt::entity`),
glm (all vectors/quaternions), the VFS + nlohmann JSON asset loaders, the `.uaf`/`.hbevent`/
`.hbmusic` on-disk formats, and **miniaudio ownership itself**.

---

## 4. Proposed Reverie architecture

Reverie is a layered stack. Every arrow points **down**; nothing points at an engine.

- **Reverie Runtime (Core)** — pure, portable C++17, **no glm/entt/nlohmann/VFS/engine types**.
  Uses POD math (`rv_float3`, `rv_quat`), opaque handles, result codes, and GUIDs. Subsystems:
  - **Platform/Device**: `IAudioDevice` abstraction with two backends — a **miniaudio** device
    (default; Reverie owns miniaudio internally) and an **OfflineDevice** that renders to a
    buffer for deterministic headless tests. Decoders (wav/flac/ogg) sit here.
  - **Mixer/Routing**: bus tree (Master + children), gain/mute/solo, sends/returns, snapshots,
    metering, ducking/sidechain, channel limits.
  - **Voices**: pooled non-relocatable voice handles internally (mirroring Heartbreak's pinned-
    node discipline), voice prioritization / virtualization / stealing / concurrency groups /
    distance priority / bus + event limits. Public API exposes only opaque handles.
  - **Events / Layers**: an event is a tree of independently-configurable layers (gain, pitch,
    randomization, looping, spatialization, routing, DSP, sends, parameters, conditions,
    concurrency, triggering rules). Music is the same event/layer machinery, not a separate system.
  - **Parameters**: a generalized continuous+discrete parameter store consumed by events, layers,
    mixer, effects, music, spatialization, automation (the FMOD "RTPC" concept, generalized).
  - **DSP**: a modular effect-graph with an `IAudioEffect` interface; effects (EQ, compressor,
    limiter, delay, reverb, filters, ...) are registered, not hard-wired. Ship the interfaces
    first, a few effects later.
  - **Spatial** (the abstraction the brief asked for): `ISpatialRenderer`, `ISpatialSource`,
    `ISpatialListener`, `IAcousticEnvironment`. **Resonance is one `ISpatialRenderer`
    implementation**; a trivial amplitude-panning renderer is the fallback so Reverie has **no
    hard dependency** on Resonance. Occlusion is fed via an **injected ray callback**
    (`rv_occlusion_ray_fn(user, a, b) -> bool`), exactly like Heartbreak's `segmentBlocked`, so
    Reverie never depends on any physics engine.
  - **Music**: states/layers/parameters/transitions/stingers/ducking/beat-quantization — the
    `.hbmusic` model generalized and engine-agnostic.
  - **Assets / Serialization**: an engine-independent project format (text, versioned, GUID-keyed,
    source-control-friendly) for authoring, compiled into optimized binary **banks** for runtime.
    Runtime consumes banks, not editor assets. No `.uaf`/VFS/nlohmann in the runtime.
- **Reverie SDK** — the stable public boundary:
  - A C++ facade (`reverie::AudioEngine`, `AudioEvent`, `AudioEventInstance`, `AudioSource`,
    `AudioBus`, `AudioParameter`, `AudioListener`, `SpatialSource`, `MusicSystem`), pimpl,
    header-clean.
  - A **flat C ABI** (`reverie.h`, `reverie_*` over opaque handles) — the true stability
    boundary for language bindings (Unity P/Invoke, Unreal, others). The C++ facade is a thin
    wrapper over the C ABI (or vice-versa); the C ABI is what integrations bind to.
- **Reverie Tools** — offline: Baker / BankBuilder / AssetProcessor (import → decode/compress →
  banks; dependency tracking; deterministic cook).
- **Reverie Studio** — a **standalone** desktop authoring app (project browser, asset browser,
  event/layer/mixer/DSP/music/spatial editors, profiler, bank builder, settings). Depends only
  on Reverie SDK + its own UI stack; **never** on Heartbreak.
- **Integrations** — thin adapters over the C ABI: Heartbreak, Unity, Unreal. No core logic.
- **Live Link** — a Studio↔runtime protocol (interface designed now, implemented later) for live
  editing of events/mixer/params in a running game.

---

## 5. Proposed directory structure (adapted)

Adapted from the brief to fit reality. The key adaptation: **`Spatial/Resonance/` holds the
Reverie→Resonance backend adapter, NOT a copy of Resonance** (Resonance stays its own sibling
repo, consumed by CMake). The Heartbreak-specific glue (entt/glm) lives Heartbreak-side; Reverie's
`Integrations/Heartbreak/` holds only code that depends solely on the Reverie C ABI.

```
Reverie/
├── Runtime/          # the engine-agnostic core (static lib: ReverieRuntime)
│   ├── Core/         # types, handles, ids/guids, result codes, log/assert, command queue
│   ├── Platform/     # IAudioDevice + miniaudio backend + OfflineDevice; decoders
│   ├── Audio/        # engine object, tick, device pump
│   ├── Voices/       # pool, prioritization, virtualization, stealing, concurrency
│   ├── Mixer/        # buses, sends/returns, snapshots, metering, ducking
│   ├── Routing/      # signal routing graph
│   ├── DSP/          # IAudioEffect + effect graph (+ effects over time)
│   ├── Events/       # event + instance model
│   ├── Layers/       # layer model + triggering rules
│   ├── Parameters/   # generalized parameter store
│   ├── Spatial/      # ISpatialRenderer/Source/Listener/IAcousticEnvironment + panning fallback
│   ├── Music/        # states/layers/params/transitions/stingers/ducking
│   ├── Assets/       # runtime asset + bank loading
│   └── Serialization/# project (text) + bank (binary) formats, versioned, GUID-keyed
├── SDK/
│   ├── Include/      # reverie/*.h  (C++ facade)
│   └── CAPI/         # reverie.h    (flat C ABI — the binding boundary)
├── Spatial/
│   └── Resonance/    # ReverieResonanceRenderer: ISpatialRenderer backed by HDSResonance-Audio
├── Studio/           # standalone authoring app (App/Editor/EventEditor/MixerEditor/…)
├── Integrations/
│   ├── Heartbreak/   # C-ABI-only helpers/examples (entt/glm glue stays in Heartbreak's repo)
│   ├── Unity/        # C# package + native shim over the C ABI
│   └── Unreal/       # UE plugin (C++/Blueprint) over the C ABI
├── Tools/            # Baker / BankBuilder / AssetProcessor
├── Tests/            # headless deterministic tests (offline device)
├── Examples/
├── Docs/             # this file + design notes + THIRD-PARTY/NOTICE
└── ThirdParty/       # miniaudio (+ any decoders); Resonance is a sibling repo, referenced by CMake
```

---

## 6. Dependency graph (no cycles; nothing depends on an engine)

```
  Reverie Studio ─┐
  Reverie Tools ──┤
  Heartbreak adapter ─┐
  Unity plugin ───────┼──► Reverie SDK (C ABI + C++ facade)
  Unreal plugin ──────┘            │
                                   ▼
                          Reverie Runtime (Core)
                                   │
        ┌──────────────────────────┼───────────────────────────┐
        ▼                          ▼                            ▼
  Spatial abstraction       Platform/Device               Serialization/Bank
        │                     (miniaudio)                       (own format)
        ▼
  Resonance backend ──► HDSResonance-Audio  (optional, gated; panning fallback if absent)
```

- Reverie Runtime depends only on: its own code, miniaudio (internal), optionally the Resonance
  fork (gated), and the C++ std lib. **Never** on Heartbreak/glm/entt/nlohmann.
- Heartbreak/Unity/Unreal depend on Reverie's **C ABI** only.
- Studio depends on the SDK + its own UI; not on any game engine.

---

## 7. What can be reused

- **Resonance, wholesale, as the spatial backend** behind `ISpatialRenderer` — mapping is
  1:1 (source setters → `SetSource*`, listener → `SetHead*`, environment → room properties).
  Adopt its **24-material absorption table** as Reverie's acoustic-material vocabulary.
- **The `SpatializerResonance` re-block technique** (fixed-block ring buffer bridging a variable
  host callback to Resonance's fixed `frames_per_buffer`) — the pattern moves into the Resonance
  backend adapter.
- **Resonance's Unity/FMOD/Wwise/VST trees as design reference** for Reverie's own adapters
  (component field shapes, the reduced flat-C native ABI in `platforms/unity/unity.h`, the Wwise
  mixer-plugin + room-attachment split). Reference, not copy.
- **Heartbreak's audio designs as engine-agnostic prior art** (re-implemented, not copied with
  coupling): the bus-tree model, the weighted-pool event model, the states/layers/params/duck/
  beat-quantized interactive-music model, the injected-callback occlusion probe, the
  coupling-free caption pattern (`subtitle::Line`).
- **The CMake consumption + gating pattern** already proven with the Resonance fork.

## 8. What needs to be built (net-new)

Everything above the backends is new: the Reverie Core runtime (device/mixer/voices/events/
layers/params/DSP/music/spatial abstraction), the C++ SDK + C ABI, the project + bank format and
serialization, the offline tools, Reverie Studio, the three integrations, and the live-link
protocol. None of it exists yet.

---

## 9. Licensing / provenance concerns discovered (verified per-file)

- **Resonance itself**: Apache-2.0, `Copyright 2018 Google Inc.` headers, consistent across
  spot-checked files. **There is NO NOTICE file anywhere in the fork** (verified repo-wide).
- **HDS modifications**: `MODIFICATIONS.md` asserts zero source/DSP/API changes (README +
  MODIFICATIONS + a CMake flag only). That is an assertion, not an independent diff against
  upstream — trust-but-verify before treating the surface as canonical.
- **Eigen**: MPL-2.0 (weak, file-level copyleft), compiled with `-DEIGEN_MPL2_ONLY` (excludes its
  LGPL parts). Linking imposes no copyleft on Reverie; only modifying an Eigen file triggers
  disclosure of that file. Ship `COPYING.MPL2`. **Keep Eigen out of Reverie's own TUs** (build
  `ReflectionProperties`/`ReverbProperties` in Reverie rather than calling the Eigen-dependent
  room helpers) — good for both build hygiene and licensing.
- **pffft**: FFTPACK/BSD-like (Pommier 2013 + UCAR/NCAR 2004) — retain notice + disclaimer,
  no-endorsement clause. Compiled into the shipping lib → attribution required in binaries.
- **SADIE HRTF**: `LICENSE` is pure Apache-2.0 and the assets are compiled into the lib. **But the
  upstream SADIE dataset (Univ. of York) has its own academic/citation provenance not captured in
  that file** → flag for legal diligence before shipping.
- **googletest / ios-cmake**: BSD-3, tools/tests only (not shipped).

**Actions for Reverie**: (1) author a `Docs/THIRD-PARTY.md` / `NOTICE` aggregating Google
Resonance (Apache-2.0), Eigen (MPL-2.0), pffft (BSD/FFTPACK), SADIE (Apache-2.0 + citation);
(2) keep the three provenance classes clearly separated — **(a)** Resonance-derived Apache-2.0
(Google), **(b)** HDS modifications to Resonance, **(c)** new HDS Reverie code (its own copyright
+ header); (3) decide Reverie's own license (it may be proprietary while linking Apache-2.0/MPL-2.0/
BSD deps, provided their attribution terms are honored). **Reverie's license is a decision for you.**

---

## 10. Recommended Phase 1 implementation plan (smallest viable, provable)

Phase 1 is the **engine-agnostic runtime skeleton + public API boundary + deterministic test
harness** — no events/spatial/music yet. It proves the architecture (agnostic core, SDK boundary,
offline-testable, standalone build) before any feature breadth.

Deliverables:
1. `Reverie/CMakeLists.txt` — standalone project building `ReverieRuntime` (static lib) +
   `reverie_tests` (exe). Vendors miniaudio under `ThirdParty/`. No Heartbreak, no glm, no entt.
2. `Runtime/Core/` — POD math (`rv_float3`/`rv_quat`), opaque handle + id/GUID types, `rv_result`
   codes, a dependency-free log/assert, and a lockless command-queue primitive.
3. `Runtime/Platform/` — `IAudioDevice` + a **miniaudio** device backend (Reverie owns
   `MINIAUDIO_IMPLEMENTATION`) + an **OfflineDevice** that renders to a caller buffer; a PCM/wav
   decoder.
4. `Runtime/Mixer/` + `Runtime/Voices/` — a minimal Master bus and a voice that plays a decoded
   PCM buffer, summed to the device output with per-voice + bus gain.
5. `SDK/` — `reverie::AudioEngine` C++ facade (create/destroy, init with device config, play a
   PCM buffer, set master volume, tick) **and** the matching flat C ABI (`reverie.h`). Opaque
   handles only; no internal types leak.
6. `Tests/` — a **deterministic headless test**: create the engine with the OfflineDevice, play a
   synthetic sine, render N frames, assert non-silence + correct summed gain (the same self-test
   discipline used for meshopt/Resonance). No audio device required to pass.
7. `Docs/THIRD-PARTY.md` + a Reverie `LICENSE`/`NOTICE` stub (pending the license decision).

Explicitly **out of Phase 1**: events/layers, the Resonance backend (Phase 4), the Heartbreak
integration (Phase 5), Studio (Phase 7). Heartbreak is untouched.

Suggested subsequent order (dependency-driven, matches the brief with one change — spatial can
come earlier since Resonance is ready): Core → Voices/Events/Layers → Mixer/Routing → Parameters →
Resonance spatial backend → Asset/Bank format → Heartbreak integration → Studio → Music → Unity →
Unreal → Profiler/Live-Link.

---

## Key decisions needed before/within Phase 1

1. **Audio device ownership.** Recommended: **Reverie owns miniaudio internally** (its own device
   + decoders), so it is a true standalone runtime and the Studio/Unity/Unreal all work without a
   host engine. Consequence: eventually Heartbreak's `AudioSystem` becomes a thin Reverie client
   and stops compiling its own `MINIAUDIO_IMPLEMENTATION` (only one is allowed per linked image).
   Alternative: Reverie is device-agnostic (host feeds/pulls buffers) — more flexible for consoles
   but pushes device/decoding onto every integration.
2. **Reverie's own license** (proprietary / open-source / decide later) — determines the file
   headers + NOTICE I write.
3. **Studio UI stack** (Phase 7, not blocking) — e.g. Dear ImGui (permissive, already in the
   ecosystem) vs Qt vs other. Flagged early because it affects Studio's dependency footprint.
