// Reverie/Runtime/Voices/VoiceManager.cpp - see VoiceManager.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Voices/VoiceManager.h"

#include "Mixer/Mixer.h"

#include <algorithm>
#include <cstring>

namespace reverie {

void VoiceManager::SetMaxRealVoices(u32 count) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxReal_ = count == 0 ? 1 : count;
    ReprioritizeLocked();
}

u32 VoiceManager::MaxRealVoices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return maxReal_;
}

VoiceId VoiceManager::Play(const VoiceSpawn& spawn) {
    if (!spawn.buffer || spawn.buffer->Empty()) return kInvalidId;
    std::lock_guard<std::mutex> lock(mutex_);
    Voice v;
    v.buffer = spawn.buffer;
    v.cursor = 0.0;
    v.volume = spawn.volume < 0.0f ? 0.0f : spawn.volume;
    v.pitch = spawn.pitch < 0.01f ? 0.01f : spawn.pitch;
    v.loop = spawn.loop;
    v.state = VoiceState::Playing;
    v.id = nextId_++;
    if (nextId_ == kInvalidId) nextId_ = 1;
    v.priority = spawn.priority;
    v.eventInstance = spawn.eventInstance;
    v.concurrencyGroup = spawn.concurrencyGroup;
    v.bus = spawn.bus;
    v.age = nextAge_++;
    voices_.push_back(std::move(v));
    const VoiceId id = voices_.back().id;
    ReprioritizeLocked();
    return id;
}

void VoiceManager::Stop(VoiceId id) {
    if (id == kInvalidId) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_) {
        if (v.id == id) {
            v.state = VoiceState::Free;
            break;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.state == VoiceState::Free; }),
                  voices_.end());
    ReprioritizeLocked();
}

void VoiceManager::StopInstance(InstanceId instance) {
    if (instance == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [instance](const Voice& v) { return v.eventInstance == instance; }),
                  voices_.end());
    ReprioritizeLocked();
}

void VoiceManager::StopGroup(u32 group) {
    if (group == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [group](const Voice& v) { return v.concurrencyGroup == group; }),
                  voices_.end());
    ReprioritizeLocked();
}

void VoiceManager::StopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

u32 VoiceManager::ActiveVoiceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<u32>(voices_.size());
}

u32 VoiceManager::RealVoiceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    u32 n = 0;
    for (const Voice& v : voices_)
        if (!v.virtualized) ++n;
    return n;
}

u32 VoiceManager::VirtualVoiceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    u32 n = 0;
    for (const Voice& v : voices_)
        if (v.virtualized) ++n;
    return n;
}

u32 VoiceManager::GroupVoiceCount(u32 group) const {
    std::lock_guard<std::mutex> lock(mutex_);
    u32 n = 0;
    for (const Voice& v : voices_)
        if (v.concurrencyGroup == group) ++n;
    return n;
}

u32 VoiceManager::InstanceVoiceCount(InstanceId instance) const {
    std::lock_guard<std::mutex> lock(mutex_);
    u32 n = 0;
    for (const Voice& v : voices_)
        if (v.eventInstance == instance) ++n;
    return n;
}

void VoiceManager::ReprioritizeLocked() {
    // Order playing voices by priority (desc), then age (asc): among equal priority the older
    // voice is kept audible and the newer one is virtualized. The top `maxReal_` are real.
    std::vector<usize> order;
    order.reserve(voices_.size());
    for (usize i = 0; i < voices_.size(); ++i)
        if (voices_[i].state == VoiceState::Playing) order.push_back(i);

    std::sort(order.begin(), order.end(), [this](usize a, usize b) {
        if (voices_[a].priority != voices_[b].priority)
            return voices_[a].priority > voices_[b].priority;
        return voices_[a].age < voices_[b].age;
    });

    for (usize rank = 0; rank < order.size(); ++rank)
        voices_[order[rank]].virtualized = (rank >= maxReal_);
}

void VoiceManager::MixToBuses(Mixer& mixer, u32 frameCount, u32 channels, u32 dstSampleRate) {
    if (frameCount == 0 || channels == 0 || dstSampleRate == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);

    for (Voice& v : voices_) {
        if (v.state != VoiceState::Playing || !v.buffer) continue;
        const AudioBuffer& b = *v.buffer;
        const u32 srcCh = b.channels;
        const u32 srcFrames = b.FrameCount();
        if (srcCh == 0 || srcFrames == 0) {
            v.state = VoiceState::Free;
            continue;
        }
        // Real voices mix into their target bus's block buffer; virtual voices still advance.
        f32* out = v.virtualized ? nullptr : mixer.BusBuffer(v.bus);
        const bool audible = out != nullptr;
        const f64 step =
            (static_cast<f64>(b.sampleRate) / static_cast<f64>(dstSampleRate)) * v.pitch;

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
            if (audible) {
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
            }
            v.cursor += step;
        }
    }

    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.state == VoiceState::Free; }),
                  voices_.end());
    ReprioritizeLocked();
}

} // namespace reverie
