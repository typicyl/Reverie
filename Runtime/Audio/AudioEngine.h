// Reverie/Runtime/Audio/AudioEngine.h - the Reverie runtime engine (internal).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The heart of the runtime: owns the output device, the mixer, and the voice pool, and IS the
// IAudioRenderer the device pulls. This is an INTERNAL runtime type (Runtime/); the public,
// ABI-stable surface is reverie::Engine (SDK/Include) + the C ABI (SDK/CAPI), which are thin
// wrappers over this. The pipeline realized here:
//
//   AudioBuffer (decoded) -> Voice (resample/upmix) -> Mixer (Master) -> Device output
#pragma once

#include "Audio/AudioBuffer.h"
#include "Audio/AudioFormat.h"
#include "Mixer/Mixer.h"
#include "Platform/AudioDevice.h"
#include "Voices/Voice.h"

#include <memory>
#include <mutex>
#include <vector>

namespace reverie {

struct EngineConfig {
    DeviceBackend backend = DeviceBackend::Miniaudio;
    u32 sampleRate = 48000;
    u32 channels = 2; // output channels (Phase 1: stereo)
    u32 periodFrames = 0;
};

class AudioEngine final : public IAudioRenderer {
public:
    AudioEngine();
    ~AudioEngine() override;

    Result Init(const EngineConfig& config);
    void Shutdown();
    bool IsInitialized() const { return inited_; }
    AudioFormat OutputFormat() const { return format_; }

    // Starts / stops the real output device (no-op flag on the Null backend, which is pulled
    // offline instead). Safe to call repeatedly.
    Result Start();
    Result Stop();

    void SetMasterVolume(f32 v) { mixer_.SetMasterVolume(v); }
    f32 MasterVolume() const { return mixer_.MasterVolume(); }

    // Plays a decoded in-memory buffer. Returns a voice id (kInvalidId on failure).
    VoiceId PlayMemory(std::shared_ptr<const AudioBuffer> buffer, f32 volume, bool loop);
    void StopVoice(VoiceId id);
    void StopAll();
    u32 ActiveVoiceCount() const;

    // Offline pull of the whole graph into `out` (interleaved f32, device channels). Independent
    // of the backend; intended for the Null backend / tests / the offline renderer. Do NOT call
    // while a real device is actively pulling (that would double-render). Returns frames written.
    u32 RenderOffline(f32* out, u32 frameCount);

    // IAudioRenderer - the device pulls this (on its audio thread for a real backend).
    void RenderAudio(f32* output, u32 frameCount, u32 channels, u32 sampleRate) override;

private:
    void MixVoicesLocked(f32* out, u32 frameCount, u32 channels);

    AudioDeviceManager deviceManager_;
    std::unique_ptr<IAudioDevice> device_;
    Mixer mixer_;
    std::vector<Voice> voices_;
    mutable std::mutex voiceMutex_;
    VoiceId nextVoiceId_ = 1;
    AudioFormat format_;
    bool inited_ = false;
};

} // namespace reverie
