// Reverie/Runtime/Audio/AudioEngine.cpp - see AudioEngine.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Audio/AudioEngine.h"

#include "Audio/AudioDecoder.h"
#include "Core/Log.h"
#include "Spatial/ResonanceSpatialRenderer.h"

#include <cstring>

namespace reverie {

AudioEngine::AudioEngine()
    : events_(voices_, [this](SoundId s) { return GetSound(s); }) {}

AudioEngine::~AudioEngine() { Shutdown(); }

Result AudioEngine::Init(const EngineConfig& config) {
    if (inited_) return Result::AlreadyInitialized;
    if (config.channels == 0 || config.sampleRate == 0) return Result::InvalidArgument;

    DeviceDesc desc;
    desc.backend = config.backend;
    desc.format = AudioFormat{config.channels, config.sampleRate, SampleFormat::F32};
    desc.periodFrames = config.periodFrames;

    device_ = deviceManager_.CreateDevice(desc, this);
    if (!device_) {
        LogMessage(LogLevel::Error, "AudioEngine::Init: failed to create output device");
        return Result::DeviceError;
    }
    format_ = device_->Format(); // device is authoritative
    voices_.SetMaxRealVoices(config.maxVoices);
    mixer_.ConfigureDefault(); // Master + Music/SFX/Dialogue/Ambience/UI/Spatial
    // Spatial backend: HDS Resonance (HRTF) when requested and built into this binary, else the
    // dependency-free panning renderer. Both implement ISpatialRenderer.
    if (config.useResonance) {
        spatialRenderer_ = CreateResonanceSpatialRenderer();
        if (spatialRenderer_ && Failed(spatialRenderer_->Init(format_.sampleRate, 128)))
            spatialRenderer_.reset();
        if (!spatialRenderer_)
            LogMessage(LogLevel::Warning,
                       "AudioEngine: Resonance spatial backend unavailable; using panning.");
    }
    if (!spatialRenderer_) {
        spatialRenderer_ = std::make_unique<PanningSpatialRenderer>();
        spatialRenderer_->Init(format_.sampleRate, 128);
    }
    voices_.SetSpatialRenderer(spatialRenderer_.get());
    spatialBus_ = mixer_.FindBus("Spatial");
    inited_ = true;
    LogFormat(LogLevel::Info, "AudioEngine ready: %s, %u ch, %u Hz, %u voice budget",
              device_->Name(), format_.channels, format_.sampleRate, config.maxVoices);
    return Result::Ok;
}

void AudioEngine::Shutdown() {
    if (!inited_) return;
    if (device_) {
        device_->Stop();
        device_.reset();
    }
    voices_.StopAll(); // releases spatial slots while the renderer is still alive
    events_.StopAllInstances();
    voices_.SetSpatialRenderer(nullptr);
    spatialRenderer_.reset();
    spatialBus_ = kInvalidId;
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        sounds_.clear();
    }
    inited_ = false;
}

Result AudioEngine::Start() {
    if (!inited_ || !device_) return Result::NotInitialized;
    return device_->Start();
}

Result AudioEngine::Stop() {
    if (!inited_ || !device_) return Result::NotInitialized;
    return device_->Stop();
}

SoundId AudioEngine::RegisterSound(std::shared_ptr<const AudioBuffer> buffer) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    const SoundId id = nextSound_++;
    if (nextSound_ == kInvalidId) nextSound_ = 1;
    sounds_[id] = std::move(buffer);
    return id;
}

SoundId AudioEngine::LoadPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate) {
    if (interleaved == nullptr || frameCount == 0 || channels == 0 || sampleRate == 0)
        return kInvalidId;
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = channels;
    buffer->sampleRate = sampleRate;
    buffer->samples.assign(interleaved,
                           interleaved + static_cast<usize>(frameCount) * channels);
    return RegisterSound(std::move(buffer));
}

SoundId AudioEngine::LoadFile(const char* path) {
    auto buffer = std::make_shared<AudioBuffer>();
    if (Failed(AudioDecoder::DecodeFile(path, *buffer))) return kInvalidId;
    return RegisterSound(std::move(buffer));
}

void AudioEngine::UnloadSound(SoundId sound) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    sounds_.erase(sound);
}

std::shared_ptr<const AudioBuffer> AudioEngine::GetSound(SoundId sound) const {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(sound);
    return it != sounds_.end() ? it->second : nullptr;
}

VoiceId AudioEngine::PlaySound(SoundId sound, f32 volume, bool loop) {
    std::shared_ptr<const AudioBuffer> buffer = GetSound(sound);
    if (!buffer) return kInvalidId;
    VoiceSpawn spawn;
    spawn.buffer = std::move(buffer);
    spawn.volume = volume;
    spawn.loop = loop;
    return voices_.Play(spawn);
}

void AudioEngine::StopAll() {
    events_.StopAllInstances();
    voices_.StopAll();
}

void AudioEngine::RenderAudio(f32* output, u32 frameCount, u32 channels, u32 /*sampleRate*/) {
    if (output == nullptr || frameCount == 0 || channels == 0) return;
    // voices -> per-bus buffers (+ spatial voices -> renderer) -> bus tree -> Master -> output.
    mixer_.BeginBlock(frameCount, channels);
    if (spatialRenderer_) {
        spatialRenderer_->BeginBlock(frameCount);
        spatialRenderer_->SetListener(listenerPos_, listenerFwd_, listenerUp_);
    }
    voices_.MixToBuses(mixer_, frameCount, channels, format_.sampleRate);
    if (spatialRenderer_ && spatialBus_ != kInvalidId) {
        const usize need = static_cast<usize>(frameCount) * 2;
        if (spatialTmp_.size() < need) spatialTmp_.assign(need, 0.0f);
        spatialRenderer_->Render(spatialTmp_.data(), frameCount);
        if (f32* sbus = mixer_.BusBuffer(spatialBus_)) {
            for (u32 f = 0; f < frameCount; ++f) {
                sbus[static_cast<usize>(f) * channels + 0] +=
                    spatialTmp_[static_cast<usize>(f) * 2 + 0];
                if (channels >= 2)
                    sbus[static_cast<usize>(f) * channels + 1] +=
                        spatialTmp_[static_cast<usize>(f) * 2 + 1];
            }
        }
    }
    mixer_.EndBlock(output, frameCount, channels, format_.sampleRate);
}

void AudioEngine::SetListener(const Float3& position, const Float3& forward, const Float3& up) {
    listenerPos_ = position;
    listenerFwd_ = forward;
    listenerUp_ = up;
}

VoiceId AudioEngine::PlaySpatial(SoundId sound, const Float3& position, f32 volume, bool loop) {
    std::shared_ptr<const AudioBuffer> buffer = GetSound(sound);
    if (!buffer) return kInvalidId;
    VoiceSpawn spawn;
    spawn.buffer = std::move(buffer);
    spawn.volume = volume;
    spawn.loop = loop;
    spawn.spatial = true;
    spawn.position = position;
    spawn.bus = spatialBus_; // fallback bus if the source pool is full
    return voices_.Play(spawn);
}

u32 AudioEngine::RenderOffline(f32* out, u32 frameCount) {
    if (!inited_ || out == nullptr || frameCount == 0) return 0;
    RenderAudio(out, frameCount, format_.channels, format_.sampleRate);
    return frameCount;
}

} // namespace reverie
