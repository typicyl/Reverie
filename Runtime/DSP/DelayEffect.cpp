// Reverie/Runtime/DSP/DelayEffect.cpp - see DelayEffect.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "DSP/DelayEffect.h"

#include <algorithm>

namespace reverie {

void DelayEffect::Prepare(u32 sampleRate, u32 channels) {
    sampleRate_ = sampleRate == 0 ? 48000 : sampleRate;
    channels_ = channels == 0 ? 1 : channels;
    maxFrames_ = static_cast<u32>(kMaxDelayMs * 0.001f * static_cast<f32>(sampleRate_)) + 1;
    ring_.assign(static_cast<usize>(maxFrames_) * channels_, 0.0f); // allocation happens HERE, not in Process
    writePos_ = 0;
}

void DelayEffect::Reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    writePos_ = 0;
}

void DelayEffect::SetParam(u32 index, f32 value) {
    switch (index) {
    case 0: delayMs_ = value < 0.0f ? 0.0f : (value > kMaxDelayMs ? kMaxDelayMs : value); break;
    case 1: feedback_ = value < 0.0f ? 0.0f : (value > 0.95f ? 0.95f : value); break;
    case 2: mix_ = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); break;
    default: return;
    }
}

f32 DelayEffect::GetParam(u32 index) const {
    switch (index) {
    case 0: return delayMs_;
    case 1: return feedback_;
    case 2: return mix_;
    default: return 0.0f;
    }
}

void DelayEffect::Process(f32* buffer, u32 frameCount, u32 channels) {
    if (buffer == nullptr || channels == 0 || maxFrames_ == 0 || ring_.empty()) return;
    const u32 ch = channels < channels_ ? channels : channels_;
    u32 delayFrames = static_cast<u32>(delayMs_ * 0.001f * static_cast<f32>(sampleRate_));
    if (delayFrames == 0) delayFrames = 1;
    if (delayFrames >= maxFrames_) delayFrames = maxFrames_ - 1;

    for (u32 f = 0; f < frameCount; ++f) {
        const u32 readPos = (writePos_ + maxFrames_ - delayFrames) % maxFrames_;
        for (u32 c = 0; c < ch; ++c) {
            const usize wi = static_cast<usize>(writePos_) * channels_ + c;
            const usize ri = static_cast<usize>(readPos) * channels_ + c;
            const f32 dry = buffer[static_cast<usize>(f) * channels + c];
            const f32 delayed = ring_[ri];
            ring_[wi] = dry + delayed * feedback_;                 // write dry + feedback
            buffer[static_cast<usize>(f) * channels + c] = dry * (1.0f - mix_) + delayed * mix_;
        }
        writePos_ = (writePos_ + 1) % maxFrames_;
    }
}

} // namespace reverie
