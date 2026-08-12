// Reverie/Runtime/Audio/AudioEngine.cpp - see AudioEngine.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Audio/AudioEngine.h"

#include "Core/Log.h"

#include <algorithm>
#include <cstring>

namespace reverie {

AudioEngine::AudioEngine() = default;

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
    // The device is authoritative for the final format (a real backend may negotiate).
    format_ = device_->Format();
    inited_ = true;
    LogFormat(LogLevel::Info, "AudioEngine ready: %s, %u ch, %u Hz", device_->Name(),
              format_.channels, format_.sampleRate);
    return Result::Ok;
}

void AudioEngine::Shutdown() {
    if (!inited_) return;
    if (device_) {
        device_->Stop();
        device_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(voiceMutex_);
        voices_.clear();
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

VoiceId AudioEngine::PlayMemory(std::shared_ptr<const AudioBuffer> buffer, f32 volume, bool loop) {
    if (!inited_ || !buffer || buffer->Empty()) return kInvalidId;
    std::lock_guard<std::mutex> lock(voiceMutex_);
    Voice v;
    v.buffer = std::move(buffer);
    v.cursor = 0.0;
    v.volume = volume;
    v.loop = loop;
    v.state = VoiceState::Playing;
    v.id = nextVoiceId_++;
    if (nextVoiceId_ == kInvalidId) nextVoiceId_ = 1; // never issue 0
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void AudioEngine::StopVoice(VoiceId id) {
    if (id == kInvalidId) return;
    std::lock_guard<std::mutex> lock(voiceMutex_);
    for (Voice& v : voices_) {
        if (v.id == id) {
            v.state = VoiceState::Free; // reaped on the next render
            break;
        }
    }
}

void AudioEngine::StopAll() {
    std::lock_guard<std::mutex> lock(voiceMutex_);
    voices_.clear();
}

u32 AudioEngine::ActiveVoiceCount() const {
    std::lock_guard<std::mutex> lock(voiceMutex_);
    u32 n = 0;
    for (const Voice& v : voices_)
        if (v.state == VoiceState::Playing) ++n;
    return n;
}

void AudioEngine::MixVoicesLocked(f32* out, u32 frameCount, u32 channels) {
    for (Voice& v : voices_) {
        if (v.state != VoiceState::Playing || !v.buffer) continue;
        const AudioBuffer& b = *v.buffer;
        const u32 srcCh = b.channels;
        const u32 srcFrames = b.FrameCount();
        if (srcCh == 0 || srcFrames == 0) {
            v.state = VoiceState::Free;
            continue;
        }
        const f64 step = static_cast<f64>(b.sampleRate) / static_cast<f64>(format_.sampleRate);

        for (u32 f = 0; f < frameCount; ++f) {
            if (v.cursor >= static_cast<f64>(srcFrames)) {
                if (v.loop) {
                    while (v.cursor >= static_cast<f64>(srcFrames))
                        v.cursor -= static_cast<f64>(srcFrames);
                } else {
                    v.state = VoiceState::Free;
                    break;
                }
            }
            const u32 i0 = static_cast<u32>(v.cursor);
            const u32 i1 = (i0 + 1 < srcFrames) ? i0 + 1 : (v.loop ? 0u : i0);
            const f32 frac = static_cast<f32>(v.cursor - static_cast<f64>(i0));

            f32 sl, sr;
            if (srcCh == 1) {
                const f32 s0 = b.samples[i0];
                const f32 s1 = b.samples[i1];
                const f32 s = (s0 + (s1 - s0) * frac) * v.volume;
                sl = s;
                sr = s;
            } else {
                const f32 l0 = b.samples[static_cast<usize>(i0) * srcCh + 0];
                const f32 l1 = b.samples[static_cast<usize>(i1) * srcCh + 0];
                const f32 r0 = b.samples[static_cast<usize>(i0) * srcCh + 1];
                const f32 r1 = b.samples[static_cast<usize>(i1) * srcCh + 1];
                sl = (l0 + (l1 - l0) * frac) * v.volume;
                sr = (r0 + (r1 - r0) * frac) * v.volume;
            }
            out[static_cast<usize>(f) * channels + 0] += sl;
            if (channels >= 2) out[static_cast<usize>(f) * channels + 1] += sr;
            v.cursor += step;
        }
    }

    // Reap finished / stopped voices.
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.state == VoiceState::Free; }),
                  voices_.end());
}

void AudioEngine::RenderAudio(f32* output, u32 frameCount, u32 channels, u32 /*sampleRate*/) {
    if (output == nullptr || frameCount == 0 || channels == 0) return;
    std::memset(output, 0, static_cast<usize>(frameCount) * channels * sizeof(f32));
    {
        std::lock_guard<std::mutex> lock(voiceMutex_);
        MixVoicesLocked(output, frameCount, channels);
    }
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
