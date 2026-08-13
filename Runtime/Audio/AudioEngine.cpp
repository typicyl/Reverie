// Reverie/Runtime/Audio/AudioEngine.cpp - see AudioEngine.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Audio/AudioEngine.h"

#include "Audio/AudioDecoder.h"
#include "Core/Log.h"

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
    voices_.StopAll();
    events_.StopAllInstances();
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
    std::memset(output, 0, static_cast<usize>(frameCount) * channels * sizeof(f32));
    voices_.Mix(output, frameCount, channels, format_.sampleRate);
    const f32 g = mixer_.MasterGain();
    if (g != 1.0f) {
        const usize n = static_cast<usize>(frameCount) * channels;
        for (usize i = 0; i < n; ++i) output[i] *= g;
    }
}

u32 AudioEngine::RenderOffline(f32* out, u32 frameCount) {
    if (!inited_ || out == nullptr || frameCount == 0) return 0;
    RenderAudio(out, frameCount, format_.channels, format_.sampleRate);
    return frameCount;
}

} // namespace reverie
