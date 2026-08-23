// Reverie/SDK/Include/Reverie/Reverie.h - the public C++ SDK facade.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The C++ surface a host links against. Exposes only Reverie's POD vocabulary (Core/Types.h) plus
// event-authoring structs, and an opaque, pimpl'd Engine; no runtime internals, no miniaudio, no
// backend type leaks through.
//
// This is a SOURCE-compatible convenience facade (it uses std::vector / std::unique_ptr by value),
// so it is only binary-stable when the host and Reverie are built with the same toolchain/STL.
// The binary-stable boundary for language bindings and cross-toolchain use is the flat C ABI in
// <reverie.h> - integrations should prefer it.
//
// Threading: call this API from a SINGLE control thread; it is not safe to call concurrently on
// one Engine. Audio is produced on an internal audio thread (or synchronously via RenderOffline on
// the Null backend), which you must never block. (Control<->audio hardening: see
// Docs/ArchitectureAssessment.md, Phase 1.)
//
//   reverie::Engine engine;
//   engine.Init({});
//   reverie::EventDesc fire; /* ...layers... */
//   reverie::EventId id = engine.RegisterEvent(fire);
//   engine.PlayEvent(id);
#pragma once

#include "Core/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace reverie {

enum class Backend : u32 {
    Null = 0,      // headless: driven via RenderOffline (tests / CI / servers / offline render)
    Miniaudio = 1, // real playback device
};

struct Config {
    Backend backend = Backend::Miniaudio;
    u32 sampleRate = 48000;
    u32 channels = 2;    // output channels (Phase 1/2: stereo)
    u32 periodFrames = 0;
    u32 maxVoices = 64;  // real-voice budget; the rest virtualize
    // Spatial backend: false = built-in panning; true = HDS Resonance HRTF (used only if Reverie
    // was built with the Resonance backend, else it transparently falls back to panning).
    bool useResonance = false;
};

// --- Event authoring (public mirror of the runtime layered-event model) ------------------
struct EventPoolEntry {
    SoundId sound = kInvalidId;
    f32 weight = 1.0f; // relative weighted-random selection weight (>0)
};

struct EventLayerDesc {
    std::vector<EventPoolEntry> pool; // weighted pick per trigger (empty = silent layer)
    f32 volume = 1.0f;
    f32 volumeVariance = 0.0f; // +/- linear gain applied randomly per trigger
    f32 pitch = 1.0f;
    f32 pitchVariance = 0.0f;
    bool loop = false;
    f32 probability = 1.0f;   // 0..1 chance the layer triggers
    BusId bus = kInvalidId;   // routing target bus (kInvalidId = Master)
    // Optional volume modulation by a parameter (RTPC): gain *= smoothstep(paramLo,paramHi,param).
    ParameterId gainParam = kInvalidId;
    f32 paramLo = 0.0f;
    f32 paramHi = 1.0f;
};

struct EventDesc {
    std::vector<EventLayerDesc> layers;
    i32 priority = 0;         // handed to every voice this event spawns
    u32 maxInstances = 0;     // 0 = unlimited; else steal the oldest instance
    u32 concurrencyGroup = 0; // shared-limit key (0 = none)
};

// --- Adaptive music (public mirror of the runtime music model) ---------------------------
struct MusicLayerDesc {
    SoundId sound = kInvalidId;
    f32 gain = 1.0f;                    // base gain
    ParameterId gainParam = kInvalidId; // optional: gain *= smoothstep(paramLo,paramHi, param)
    f32 paramLo = 0.0f;
    f32 paramHi = 1.0f;
};

struct MusicStateDesc {
    std::string name;
    f32 bpm = 120.0f;
    u32 beatsPerBar = 4;
    std::vector<MusicLayerDesc> layers; // synchronized looping layers
};

// The audio engine. One per game/app instance. Not copyable.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Result Init(const Config& config);
    void Shutdown();
    bool IsInitialized() const;
    Result Start();
    Result Stop();
    // Host per-frame control pump (applies quantized music transitions; more deferred work later).
    void Update();

    void SetMasterVolume(f32 volume);
    f32 MasterVolume() const;
    u32 OutputChannels() const;
    u32 OutputSampleRate() const;

    // Real-voice budget (the rest virtualize) and the RNG seed for weighted pools / variance.
    void SetMaxVoices(u32 count);
    void SetSeed(u64 seed);

    // -- Mixer / bus tree -----------------------------------------------------------------
    // After Init the default tree exists: Master + Music, SFX, Dialogue, Ambience, UI.
    BusId MasterBus() const;
    BusId CreateBus(const char* name, BusId parent); // parent kInvalidId = Master
    BusId FindBus(const char* name) const;
    void SetBusVolume(BusId bus, f32 volume);
    f32 BusVolume(BusId bus) const;
    void SetBusMuted(BusId bus, bool muted);
    bool BusMuted(BusId bus) const;
    void SetBusSoloed(BusId bus, bool soloed);
    bool BusSoloed(BusId bus) const;
    // Routes a copy of `from`'s gained signal into `to` at `level` (e.g. a reverb send).
    void AddSend(BusId from, BusId to, f32 level);
    // Ducks `ducked` while `sidechain` exceeds `threshold`, by `amount` (0..1), attack/release ms.
    void SetDuck(BusId ducked, BusId sidechain, f32 threshold, f32 amount, f32 attackMs,
                 f32 releaseMs);
    void ClearDuck(BusId ducked);
    f32 BusMeter(BusId bus) const; // last block's peak level
    // Per-bus DSP insert chain. AddBusEffect inserts a built-in effect (e.g. EffectType::Filter)
    // and returns an EffectId; SetEffectParam drives it by index (Filter: 0=FilterType, 1=cutoffHz,
    // 2=Q, 3=gainDb). Effects are pre-fader, processed in insertion order.
    EffectId AddBusEffect(BusId bus, EffectType type);
    void SetEffectParam(EffectId effect, u32 index, f32 value);
    f32 EffectParam(EffectId effect, u32 index) const;
    void CaptureSnapshot(const char* name);
    bool ApplySnapshot(const char* name);

    // -- Spatial (3D) ---------------------------------------------------------------------
    // A default panning spatializer is active after Init (positions in world space,
    // right-handed, listener facing -Z by default). Spatial voices/events mix through the
    // Spatial bus.
    void SetListener(const Float3& position, const Float3& forward, const Float3& up);
    VoiceId PlaySpatial(SoundId sound, const Float3& position, f32 volume = 1.0f, bool loop = false);
    void SetVoicePosition(VoiceId voice, const Float3& position);
    InstanceId PlayEventAt(EventId event, const Float3& position, f32 volume = 1.0f);
    BusId SpatialBus() const;
    // "Panning" or "Resonance" - the spatial backend actually in use after Init.
    const char* SpatialBackendName() const;

    // -- Sounds ---------------------------------------------------------------------------
    SoundId LoadSoundFile(const char* path);
    SoundId LoadSoundPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate);
    SoundId LoadSoundHdsrf(const u8* data, usize size); // decode a cooked .HDSRF blob into a sound
    void UnloadSound(SoundId sound);

    // Asynchronous loading (decoded on a background worker, never the audio thread). Returns a
    // request id; PollLoad returns true once done and yields the SoundId (kInvalidId on failure).
    u32 LoadSoundFileAsync(const char* path);
    u32 LoadSoundHdsrfAsync(const u8* data, usize size);
    bool PollLoad(u32 requestId, SoundId& outSound);

    // Streaming playback: decode an .HDSRF blob in the background (bounded memory - no full load).
    // The blob is copied internally; returns a voice id (kInvalidId if the stream pool is full).
    VoiceId PlayStream(const u8* hdsrf, usize size, f32 volume = 1.0f, bool loop = false,
                       BusId bus = kInvalidId);

    // -- Direct one-off voice playback ----------------------------------------------------
    VoiceId Play(SoundId sound, f32 volume = 1.0f, bool loop = false);
    void StopVoice(VoiceId voice);
    void StopAll();

    // -- Parameters (RTPC) ----------------------------------------------------------------
    // A named, ranged, smoothed float the game drives (e.g. "CombatIntensity"). The current value
    // eases toward the set target over `smoothMs`. The substrate for adaptive music and DSP
    // automation in later phases. Register returns a stable id (or the existing id for that name).
    ParameterId RegisterParameter(const char* name, f32 defaultValue, f32 minValue, f32 maxValue,
                                  f32 smoothMs = 0.0f);
    ParameterId FindParameter(const char* name) const;
    void SetParameter(ParameterId param, f32 value); // clamped to [min,max]
    f32 ParameterValue(ParameterId param) const;     // current (smoothed) value
    f32 ParameterTarget(ParameterId param) const;    // the value it is easing toward
    // Drive a bus's gain from a parameter each block (mixer automation). Returns false if full.
    bool BindParameterToBusGain(ParameterId param, BusId bus);

    // -- Events (layered) -----------------------------------------------------------------
    EventId RegisterEvent(const EventDesc& desc);
    void UnregisterEvent(EventId event);
    InstanceId PlayEvent(EventId event, f32 volume = 1.0f);
    void StopEventInstance(InstanceId instance);
    u32 ActiveInstanceCount(EventId event) const;

    // -- Adaptive music -------------------------------------------------------------------
    // A music state is a set of synchronized looping layers whose gains can be parameter-driven.
    // SetMusicState switches states; the musical clock (beat/bar) is queryable for sync/UI.
    MusicStateId RegisterMusicState(const MusicStateDesc& desc);
    MusicStateId FindMusicState(const char* name) const;
    void SetMusicState(MusicStateId state, MusicTransition transition = MusicTransition::Immediate);
    void StopMusic();
    MusicStateId CurrentMusicState() const;
    f64 MusicBeat() const; // elapsed musical time in beats
    u64 MusicBar() const;
    f32 MusicBpm() const;

    // -- Stats ----------------------------------------------------------------------------
    u32 ActiveVoiceCount() const;  // real + virtual
    u32 RealVoiceCount() const;    // currently mixed
    u32 VirtualVoiceCount() const; // over budget, silent but tracked
    EngineStats GetStats() const;  // a profiling/debug snapshot (voices, CPU load, music, meter)

    // -- Serialization (versioned bank: mixer bus tree + parameters) ----------------------
    // SaveBank appends the cooked, versioned runtime data to `out`. LoadBank validates + applies it
    // (rejects a corrupt/newer bank with an error). The authoring->runtime seam.
    Result SaveBank(std::vector<u8>& out) const;
    Result LoadBank(const u8* data, usize size);

    // Offline pull of the whole graph (interleaved f32, OutputChannels()). Returns frames.
    u32 RenderOffline(f32* out, u32 frameCount);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Cook interleaved f32 PCM into an .HDSRF blob (engine-independent; for tools/build steps). The
// blob loads via Engine::LoadSoundHdsrf / reverie_load_sound_hdsrf. chunkFrames 0 -> a sensible default.
Result CookHdsrf(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate,
                 u32 chunkFrames, std::vector<u8>& out);

} // namespace reverie
