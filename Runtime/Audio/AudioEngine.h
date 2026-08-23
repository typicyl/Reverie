// Reverie/Runtime/Audio/AudioEngine.h - the Reverie runtime engine (internal).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Owns the output device, the mixer, the loaded-sound table, the voice manager, and the event
// system, and IS the IAudioRenderer the device pulls. Internal (Runtime/); the ABI-stable public
// surface is reverie::Engine + the C ABI, which are thin wrappers over this. Pipeline:
//
//   Sound (decoded) -> Event/Layer -> Voice (resample/upmix) -> VoiceManager (budget/priority)
//                                                             -> Mixer (Master) -> Device output
#pragma once

#include "Audio/AudioBuffer.h"
#include "Audio/AudioFormat.h"
#include "Events/AudioEvent.h"
#include "Events/EventSystem.h"
#include "Audio/StreamManager.h"
#include "Mixer/Mixer.h"
#include "Music/MusicSystem.h"
#include "Parameters/ParameterStore.h"
#include "Platform/AudioDevice.h"
#include "Spatial/PanningSpatialRenderer.h"
#include "Spatial/SpatialRenderer.h"
#include "Voices/VoiceManager.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace reverie {

struct EngineConfig {
    DeviceBackend backend = DeviceBackend::Miniaudio;
    u32 sampleRate = 48000;
    u32 channels = 2; // output channels (Phase 1/2: stereo)
    u32 periodFrames = 0;
    u32 maxVoices = 64;      // real-voice budget (the rest virtualize)
    bool useResonance = false; // spatial backend: false = panning, true = HDS Resonance HRTF
                               // (only if built with REVERIE_WITH_RESONANCE; else falls back)
};

class AudioEngine final : public IAudioRenderer {
public:
    AudioEngine();
    ~AudioEngine() override;

    Result Init(const EngineConfig& config);
    void Shutdown();
    bool IsInitialized() const { return inited_; }
    AudioFormat OutputFormat() const { return format_; }

    Result Start();
    Result Stop();

    // Host per-frame control pump: applies deferred/quantized work (music transitions today; async
    // load completion + callbacks later). Call once per game frame on the control thread.
    void Update() { music_.Tick(); }

    void SetMasterVolume(f32 v) { mixer_.SetBusVolume(mixer_.MasterBus(), v); }
    f32 MasterVolume() const { return mixer_.BusVolume(mixer_.MasterBus()); }
    void SetMaxVoices(u32 n) { voices_.SetMaxRealVoices(n); }
    void SetSeed(u64 seed) { events_.SetSeed(seed); }

    // -- Mixer / bus tree -----------------------------------------------------------------
    BusId MasterBus() const { return mixer_.MasterBus(); }
    BusId CreateBus(const char* name, BusId parent) { return mixer_.CreateBus(name, parent); }
    BusId FindBus(const char* name) const { return mixer_.FindBus(name); }
    void SetBusVolume(BusId bus, f32 v) { mixer_.SetBusVolume(bus, v); }
    f32 BusVolume(BusId bus) const { return mixer_.BusVolume(bus); }
    void SetBusMuted(BusId bus, bool m) { mixer_.SetBusMuted(bus, m); }
    bool BusMuted(BusId bus) const { return mixer_.BusMuted(bus); }
    void SetBusSoloed(BusId bus, bool s) { mixer_.SetBusSoloed(bus, s); }
    bool BusSoloed(BusId bus) const { return mixer_.BusSoloed(bus); }
    void AddSend(BusId from, BusId to, f32 level) { mixer_.AddSend(from, to, level); }
    void SetDuck(BusId ducked, BusId sc, f32 thr, f32 amt, f32 atk, f32 rel) {
        mixer_.SetDuck(ducked, sc, thr, amt, atk, rel);
    }
    void ClearDuck(BusId ducked) { mixer_.ClearDuck(ducked); }
    f32 BusMeter(BusId bus) const { return mixer_.Meter(bus); }
    // -- Per-bus DSP inserts ---------------------------------------------------------------
    EffectId AddBusEffect(BusId bus, EffectType type) {
        return mixer_.AddEffect(bus, type, format_.sampleRate, format_.channels);
    }
    void SetEffectParam(EffectId fx, u32 index, f32 value) { mixer_.SetEffectParam(fx, index, value); }
    f32 EffectParam(EffectId fx, u32 index) const { return mixer_.EffectParam(fx, index); }
    void CaptureSnapshot(const char* name) { mixer_.CaptureSnapshot(name); }
    bool ApplySnapshot(const char* name) { return mixer_.ApplySnapshot(name); }

    // -- Sounds ---------------------------------------------------------------------------
    SoundId LoadPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate);
    SoundId LoadFile(const char* path);
    SoundId LoadHdsrf(const u8* data, usize size); // decode a cooked .HDSRF blob into a sound
    void UnloadSound(SoundId sound);

    // Asynchronous loading: decode off the control thread on a background worker. Returns a request
    // id; poll it (or, later, get a callback via Update) for the resulting SoundId. The audio thread
    // is never involved. PollLoad returns true once complete and yields the SoundId (kInvalidId on
    // decode failure), consuming the result.
    u32 LoadFileAsync(const char* path);
    u32 LoadHdsrfAsync(const u8* data, usize size);
    bool PollLoad(u32 requestId, SoundId& outSound);

    // Streaming playback: decode an .HDSRF blob in the background (bounded memory), no full load.
    VoiceId PlayStream(const u8* hdsrf, usize size, f32 volume, bool loop, BusId bus);
    std::shared_ptr<const AudioBuffer> GetSound(SoundId sound) const;

    // -- Direct voice playback ------------------------------------------------------------
    VoiceId PlaySound(SoundId sound, f32 volume, bool loop);
    void StopVoice(VoiceId voice) { voices_.Stop(voice); }
    void StopAll();

    // -- Parameters (RTPC) ----------------------------------------------------------------
    ParameterId RegisterParameter(const char* name, f32 def, f32 mn, f32 mx, f32 smoothMs) {
        return params_.Register(name, def, mn, mx, smoothMs);
    }
    ParameterId FindParameter(const char* name) const { return params_.Find(name); }
    void SetParameter(ParameterId id, f32 value) { params_.SetTarget(id, value); }
    f32 ParameterValue(ParameterId id) const { return params_.Value(id); }
    f32 ParameterTarget(ParameterId id) const { return params_.Target(id); }
    // Drives a bus's gain from a parameter each block (mixer automation). Returns false if full.
    bool BindParameterToBusGain(ParameterId param, BusId bus);

    // -- Events ---------------------------------------------------------------------------
    EventId RegisterEvent(const AudioEventDef& def) { return events_.RegisterEvent(def); }
    void UnregisterEvent(EventId event) { events_.UnregisterEvent(event); }
    InstanceId PlayEvent(EventId event, f32 volume) { return events_.PlayEvent(event, volume); }
    void StopInstance(InstanceId instance) { events_.StopInstance(instance); }
    u32 ActiveInstanceCount(EventId event) const { return events_.ActiveInstanceCount(event); }

    // -- Adaptive music -------------------------------------------------------------------
    MusicStateId RegisterMusicState(const MusicStateDef& def) { return music_.RegisterState(def); }
    MusicStateId FindMusicState(const char* name) const { return music_.FindState(name); }
    void SetMusicState(MusicStateId id, MusicTransition t = MusicTransition::Immediate) {
        music_.SetState(id, t);
    }
    void StopMusic() { music_.Stop(); }
    MusicStateId CurrentMusicState() const { return music_.CurrentState(); }
    f64 MusicBeat() const { return music_.CurrentBeat(); }
    u64 MusicBar() const { return music_.CurrentBar(); }
    f32 MusicBpm() const { return music_.Bpm(); }

    // -- Spatial (3D) ---------------------------------------------------------------------
    void SetListener(const Float3& position, const Float3& forward, const Float3& up);
    VoiceId PlaySpatial(SoundId sound, const Float3& position, f32 volume, bool loop);
    void SetVoicePosition(VoiceId voice, const Float3& position) {
        voices_.SetVoicePosition(voice, position);
    }
    InstanceId PlayEventAt(EventId event, const Float3& position, f32 volume) {
        return events_.PlayEvent(event, volume, true, position);
    }
    // Stores the environment; it is applied to the spatial renderer on the AUDIO thread (in
    // RenderAudio) so it never mutates renderer/vraudio DSP state concurrently with Render.
    void SetEnvironment(const AcousticEnvironment& env) {
        pendingEnv_ = env;
        envDirty_.store(true, std::memory_order_release);
    }
    const char* SpatialBackendName() const {
        return spatialRenderer_ ? spatialRenderer_->Name() : "none";
    }
    BusId SpatialBus() const { return spatialBus_; }

    // -- Stats ----------------------------------------------------------------------------
    u32 ActiveVoiceCount() const { return voices_.ActiveVoiceCount(); }
    u32 RealVoiceCount() const { return voices_.RealVoiceCount(); }
    u32 VirtualVoiceCount() const { return voices_.VirtualVoiceCount(); }
    EngineStats GetStats() const;

    // -- Serialization (versioned bank: mixer bus tree + parameters) ----------------------
    Result SaveBank(std::vector<u8>& out) const;
    Result LoadBank(const u8* data, usize size);

    // Offline pull of the whole graph (interleaved f32, device channels). Null backend / tests /
    // offline renderer. Returns frames written.
    u32 RenderOffline(f32* out, u32 frameCount);

    // IAudioRenderer - the device pulls this (audio thread on a real backend).
    void RenderAudio(f32* output, u32 frameCount, u32 channels, u32 sampleRate) override;

private:
    SoundId RegisterSound(std::shared_ptr<const AudioBuffer> buffer);

    // Background async-loader worker (control<->worker only; never touches the audio thread).
    struct LoadRequest {
        u32 id = 0;
        bool isHdsrf = false;
        std::string path;
        std::vector<u8> blob;
    };
    void StartLoader();
    void StopLoader();
    void LoaderMain();
    std::thread loaderThread_;
    std::mutex loadMutex_;
    std::condition_variable loadCv_;
    std::queue<LoadRequest> loadQueue_;
    std::unordered_map<u32, SoundId> loadDone_;
    std::atomic<u32> nextLoadId_{1};
    bool loaderQuit_ = false;    // guarded by loadMutex_
    bool loaderStarted_ = false; // control-thread only

    AudioDeviceManager deviceManager_;
    std::unique_ptr<IAudioDevice> device_;
    Mixer mixer_;
    VoiceManager voices_;
    ParameterStore params_;
    StreamManager streams_;
    mutable std::mutex soundMutex_;
    std::unordered_map<SoundId, std::shared_ptr<const AudioBuffer>> sounds_;
    SoundId nextSound_ = 1;
    EventSystem events_; // constructed with voices_ + a GetSound resolver (see ctor)
    MusicSystem music_;  // constructed with voices_ + a GetSound resolver (see ctor)
    std::unique_ptr<ISpatialRenderer> spatialRenderer_;
    BusId spatialBus_ = kInvalidId;
    Float3 listenerPos_;
    Float3 listenerFwd_{0.0f, 0.0f, -1.0f};
    Float3 listenerUp_{0.0f, 1.0f, 0.0f};
    AcousticEnvironment pendingEnv_;      // set on the control thread, applied on the audio thread
    std::atomic<bool> envDirty_{false};   // pendingEnv_ has an update to apply
    // Parameter->bus-gain automation bindings. Written on the control thread (append + publish
    // count), read on the audio thread; the fixed array never reallocates, so it is a clean SPSC
    // publish like the voice pool. Applied each block in RenderAudio (same thread as EndBlock).
    struct GainBinding { ParameterId param = kInvalidId; BusId bus = kInvalidId; };
    std::array<GainBinding, 64> gainBindings_{};
    std::atomic<u32> gainBindingCount_{0};
    std::vector<f32> spatialTmp_; // stereo scratch for the spatial mix
    AudioFormat format_;
    std::atomic<f32> cpuLoad_{0.0f}; // last block: render time / block duration (audio thread writes)
    bool inited_ = false;
};

} // namespace reverie
