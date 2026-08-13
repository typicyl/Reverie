// Reverie/SDK/Include/Reverie/Reverie.h - the public C++ SDK facade.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The stable, ABI-lean C++ surface a host links against. Exposes only Reverie's POD vocabulary
// (Core/Types.h) plus event-authoring structs, and an opaque, pimpl'd Engine; no runtime
// internals, no miniaudio, no backend type leaks through. Language bindings should prefer the
// flat C ABI in <reverie.h>.
//
//   reverie::Engine engine;
//   engine.Init({});
//   reverie::EventDesc fire; /* ...layers... */
//   reverie::EventId id = engine.RegisterEvent(fire);
//   engine.PlayEvent(id);
#pragma once

#include "Core/Types.h"

#include <memory>
#include <vector>

namespace reverie {

enum class Backend : u32 {
    Null = 0,      // headless: driven via RenderOffline (tests / CI / servers / offline render)
    Miniaudio = 1, // real playback device
};

struct Config {
    Backend backend = Backend::Miniaudio;
    u32 sampleRate = 48000;
    u32 channels = 2;   // output channels (Phase 1/2: stereo)
    u32 periodFrames = 0;
    u32 maxVoices = 64; // real-voice budget; the rest virtualize
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
};

struct EventDesc {
    std::vector<EventLayerDesc> layers;
    i32 priority = 0;         // handed to every voice this event spawns
    u32 maxInstances = 0;     // 0 = unlimited; else steal the oldest instance
    u32 concurrencyGroup = 0; // shared-limit key (0 = none)
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
    void CaptureSnapshot(const char* name);
    bool ApplySnapshot(const char* name);

    // -- Sounds ---------------------------------------------------------------------------
    SoundId LoadSoundFile(const char* path);
    SoundId LoadSoundPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate);
    void UnloadSound(SoundId sound);

    // -- Direct one-off voice playback ----------------------------------------------------
    VoiceId Play(SoundId sound, f32 volume = 1.0f, bool loop = false);
    void StopVoice(VoiceId voice);
    void StopAll();

    // -- Events (layered) -----------------------------------------------------------------
    EventId RegisterEvent(const EventDesc& desc);
    void UnregisterEvent(EventId event);
    InstanceId PlayEvent(EventId event, f32 volume = 1.0f);
    void StopEventInstance(InstanceId instance);
    u32 ActiveInstanceCount(EventId event) const;

    // -- Stats ----------------------------------------------------------------------------
    u32 ActiveVoiceCount() const;  // real + virtual
    u32 RealVoiceCount() const;    // currently mixed
    u32 VirtualVoiceCount() const; // over budget, silent but tracked

    // Offline pull of the whole graph (interleaved f32, OutputChannels()). Returns frames.
    u32 RenderOffline(f32* out, u32 frameCount);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverie
