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
#include "Mixer/Mixer.h"
#include "Platform/AudioDevice.h"
#include "Voices/VoiceManager.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace reverie {

struct EngineConfig {
    DeviceBackend backend = DeviceBackend::Miniaudio;
    u32 sampleRate = 48000;
    u32 channels = 2; // output channels (Phase 1/2: stereo)
    u32 periodFrames = 0;
    u32 maxVoices = 64; // real-voice budget (the rest virtualize)
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
    void CaptureSnapshot(const char* name) { mixer_.CaptureSnapshot(name); }
    bool ApplySnapshot(const char* name) { return mixer_.ApplySnapshot(name); }

    // -- Sounds ---------------------------------------------------------------------------
    SoundId LoadPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate);
    SoundId LoadFile(const char* path);
    void UnloadSound(SoundId sound);
    std::shared_ptr<const AudioBuffer> GetSound(SoundId sound) const;

    // -- Direct voice playback ------------------------------------------------------------
    VoiceId PlaySound(SoundId sound, f32 volume, bool loop);
    void StopVoice(VoiceId voice) { voices_.Stop(voice); }
    void StopAll();

    // -- Events ---------------------------------------------------------------------------
    EventId RegisterEvent(const AudioEventDef& def) { return events_.RegisterEvent(def); }
    void UnregisterEvent(EventId event) { events_.UnregisterEvent(event); }
    InstanceId PlayEvent(EventId event, f32 volume) { return events_.PlayEvent(event, volume); }
    void StopInstance(InstanceId instance) { events_.StopInstance(instance); }
    u32 ActiveInstanceCount(EventId event) const { return events_.ActiveInstanceCount(event); }

    // -- Stats ----------------------------------------------------------------------------
    u32 ActiveVoiceCount() const { return voices_.ActiveVoiceCount(); }
    u32 RealVoiceCount() const { return voices_.RealVoiceCount(); }
    u32 VirtualVoiceCount() const { return voices_.VirtualVoiceCount(); }

    // Offline pull of the whole graph (interleaved f32, device channels). Null backend / tests /
    // offline renderer. Returns frames written.
    u32 RenderOffline(f32* out, u32 frameCount);

    // IAudioRenderer - the device pulls this (audio thread on a real backend).
    void RenderAudio(f32* output, u32 frameCount, u32 channels, u32 sampleRate) override;

private:
    SoundId RegisterSound(std::shared_ptr<const AudioBuffer> buffer);

    AudioDeviceManager deviceManager_;
    std::unique_ptr<IAudioDevice> device_;
    Mixer mixer_;
    VoiceManager voices_;
    mutable std::mutex soundMutex_;
    std::unordered_map<SoundId, std::shared_ptr<const AudioBuffer>> sounds_;
    SoundId nextSound_ = 1;
    EventSystem events_; // constructed with voices_ + a GetSound resolver (see ctor)
    AudioFormat format_;
    bool inited_ = false;
};

} // namespace reverie
