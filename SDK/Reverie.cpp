// Reverie/SDK/Reverie.cpp - reverie::Engine facade over the runtime.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Reverie/Reverie.h"

#include "Audio/AudioDecoder.h"
#include "Audio/AudioEngine.h"

#include <unordered_map>

namespace reverie {

struct Engine::Impl {
    AudioEngine engine;
    std::unordered_map<SoundId, std::shared_ptr<const AudioBuffer>> sounds;
    SoundId nextSound = 1;

    SoundId Register(std::shared_ptr<const AudioBuffer> buffer) {
        const SoundId id = nextSound++;
        if (nextSound == kInvalidId) nextSound = 1;
        sounds[id] = std::move(buffer);
        return id;
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

Result Engine::Init(const Config& config) {
    EngineConfig ec;
    ec.backend = config.backend == Backend::Null ? DeviceBackend::Null : DeviceBackend::Miniaudio;
    ec.sampleRate = config.sampleRate;
    ec.channels = config.channels;
    ec.periodFrames = config.periodFrames;
    return impl_->engine.Init(ec);
}

void Engine::Shutdown() { impl_->engine.Shutdown(); }
bool Engine::IsInitialized() const { return impl_->engine.IsInitialized(); }
Result Engine::Start() { return impl_->engine.Start(); }
Result Engine::Stop() { return impl_->engine.Stop(); }
void Engine::SetMasterVolume(f32 volume) { impl_->engine.SetMasterVolume(volume); }
f32 Engine::MasterVolume() const { return impl_->engine.MasterVolume(); }
u32 Engine::OutputChannels() const { return impl_->engine.OutputFormat().channels; }
u32 Engine::OutputSampleRate() const { return impl_->engine.OutputFormat().sampleRate; }

SoundId Engine::LoadSoundFile(const char* path) {
    auto buffer = std::make_shared<AudioBuffer>();
    if (Failed(AudioDecoder::DecodeFile(path, *buffer))) return kInvalidId;
    return impl_->Register(std::move(buffer));
}

SoundId Engine::LoadSoundPCM(const f32* interleaved, u32 frameCount, u32 channels,
                             u32 sampleRate) {
    if (interleaved == nullptr || frameCount == 0 || channels == 0 || sampleRate == 0)
        return kInvalidId;
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = channels;
    buffer->sampleRate = sampleRate;
    buffer->samples.assign(interleaved,
                           interleaved + static_cast<usize>(frameCount) * channels);
    return impl_->Register(std::move(buffer));
}

void Engine::UnloadSound(SoundId sound) { impl_->sounds.erase(sound); }

VoiceId Engine::Play(SoundId sound, f32 volume, bool loop) {
    auto it = impl_->sounds.find(sound);
    if (it == impl_->sounds.end()) return kInvalidId;
    return impl_->engine.PlayMemory(it->second, volume, loop);
}

void Engine::StopVoice(VoiceId voice) { impl_->engine.StopVoice(voice); }
void Engine::StopAll() { impl_->engine.StopAll(); }
u32 Engine::ActiveVoiceCount() const { return impl_->engine.ActiveVoiceCount(); }
u32 Engine::RenderOffline(f32* out, u32 frameCount) {
    return impl_->engine.RenderOffline(out, frameCount);
}

} // namespace reverie
