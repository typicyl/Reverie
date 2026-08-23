// Reverie/Runtime/DSP/DynamicsEffect.h - a feed-forward compressor / limiter effect.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A broadband peak compressor: one shared envelope follower across channels drives one gain applied
// to all channels (preserves the stereo image). With a high ratio + fast attack it acts as a
// limiter - e.g. on the Master bus to stop the summed mix clipping (the audit's "no master limiting"
// gap). Real-time safe: fixed state, no allocation in Process. Params:
//   0 = threshold dB (default -6), 1 = ratio (default 4), 2 = attack ms (10),
//   3 = release ms (100), 4 = makeup gain dB (0).
#pragma once

#include "DSP/AudioEffect.h"

namespace reverie {

class DynamicsEffect final : public IAudioEffect {
public:
    void Prepare(u32 sampleRate, u32 channels) override;
    void Reset() override { env_ = 0.0f; }
    void Process(f32* buffer, u32 frameCount, u32 channels) override;
    void SetParam(u32 index, f32 value) override;
    f32 GetParam(u32 index) const override;
    EffectType Type() const override { return EffectType::Compressor; }
    const char* Name() const override { return "Compressor"; }

private:
    void Recompute();

    u32 sampleRate_ = 48000;
    f32 thresholdDb_ = -6.0f;
    f32 ratio_ = 4.0f;
    f32 attackMs_ = 10.0f;
    f32 releaseMs_ = 100.0f;
    f32 makeupDb_ = 0.0f;

    f32 threshLin_ = 0.5f;
    f32 makeupLin_ = 1.0f;
    f32 attackCoef_ = 0.5f;
    f32 releaseCoef_ = 0.1f;
    f32 env_ = 0.0f; // peak envelope
};

} // namespace reverie
