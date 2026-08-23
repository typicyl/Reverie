// Reverie/Runtime/DSP/BiquadFilter.h - a second-order (biquad) filter effect.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// RBJ-cookbook biquad, one section, per-channel state (transposed Direct-Form II). Real-time safe:
// coefficients are recomputed on SetParam (control thread) and the per-sample loop only does mults
// and adds. Params: 0 = type (FilterType), 1 = cutoff Hz, 2 = Q, 3 = gain dB (shelf/peak only).
#pragma once

#include "DSP/AudioEffect.h" // FilterType lives in Core/Types.h (pulled in here)

#include <array>

namespace reverie {

class BiquadFilter final : public IAudioEffect {
public:
    static constexpr u32 kMaxChannels = 8;

    void Prepare(u32 sampleRate, u32 channels) override;
    void Reset() override;
    void Process(f32* buffer, u32 frameCount, u32 channels) override;
    void SetParam(u32 index, f32 value) override;
    f32 GetParam(u32 index) const override;
    EffectType Type() const override { return EffectType::Filter; }
    const char* Name() const override { return "Filter"; }

private:
    void Recompute();

    u32 sampleRate_ = 48000;
    FilterType type_ = FilterType::Lowpass;
    f32 cutoff_ = 1000.0f;
    f32 q_ = 0.707f;
    f32 gainDb_ = 0.0f;

    // Normalized coefficients (a0 folded in).
    f32 b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    // Per-channel transposed-DF2 state.
    std::array<f32, kMaxChannels> z1_{};
    std::array<f32, kMaxChannels> z2_{};
};

} // namespace reverie
