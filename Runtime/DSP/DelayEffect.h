// Reverie/Runtime/DSP/DelayEffect.h - a feedback delay effect.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Per-channel feedback delay line. The ring buffers are allocated once in Prepare (sized to the max
// delay); Process only reads/writes them - real-time safe. Params:
//   0 = delay ms (default 250), 1 = feedback 0..0.95 (default 0.3), 2 = wet mix 0..1 (default 0.3).
#pragma once

#include "DSP/AudioEffect.h"

#include <vector>

namespace reverie {

class DelayEffect final : public IAudioEffect {
public:
    static constexpr f32 kMaxDelayMs = 2000.0f;

    void Prepare(u32 sampleRate, u32 channels) override;
    void Reset() override;
    void Process(f32* buffer, u32 frameCount, u32 channels) override;
    void SetParam(u32 index, f32 value) override;
    f32 GetParam(u32 index) const override;
    EffectType Type() const override { return EffectType::Delay; }
    const char* Name() const override { return "Delay"; }

private:
    u32 sampleRate_ = 48000;
    u32 channels_ = 0;
    f32 delayMs_ = 250.0f;
    f32 feedback_ = 0.3f;
    f32 mix_ = 0.3f;

    u32 maxFrames_ = 0;          // ring length in frames
    std::vector<f32> ring_;      // channels_ * maxFrames_ (interleaved by [frame*channels+c])
    u32 writePos_ = 0;
};

} // namespace reverie
