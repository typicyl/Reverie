// Reverie/Runtime/DSP/DynamicsEffect.cpp - see DynamicsEffect.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "DSP/DynamicsEffect.h"

#include <cmath>

namespace reverie {

namespace {
inline f32 DbToLin(f32 db) { return std::pow(10.0f, db / 20.0f); }
// One-pole smoothing coefficient for a `ms` time constant at `sr`.
inline f32 TimeCoef(f32 ms, u32 sr) {
    if (ms <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * static_cast<f32>(sr)));
}
} // namespace

void DynamicsEffect::Prepare(u32 sampleRate, u32 /*channels*/) {
    sampleRate_ = sampleRate == 0 ? 48000 : sampleRate;
    env_ = 0.0f;
    Recompute();
}

void DynamicsEffect::Recompute() {
    threshLin_ = DbToLin(thresholdDb_);
    makeupLin_ = DbToLin(makeupDb_);
    attackCoef_ = TimeCoef(attackMs_, sampleRate_);
    releaseCoef_ = TimeCoef(releaseMs_, sampleRate_);
}

void DynamicsEffect::SetParam(u32 index, f32 value) {
    switch (index) {
    case 0: thresholdDb_ = value; break;
    case 1: ratio_ = value < 1.0f ? 1.0f : value; break;
    case 2: attackMs_ = value; break;
    case 3: releaseMs_ = value; break;
    case 4: makeupDb_ = value; break;
    default: return;
    }
    Recompute();
}

f32 DynamicsEffect::GetParam(u32 index) const {
    switch (index) {
    case 0: return thresholdDb_;
    case 1: return ratio_;
    case 2: return attackMs_;
    case 3: return releaseMs_;
    case 4: return makeupDb_;
    default: return 0.0f;
    }
}

void DynamicsEffect::Process(f32* buffer, u32 frameCount, u32 channels) {
    if (buffer == nullptr || channels == 0) return;
    const f32 invRatio = 1.0f / ratio_;
    for (u32 f = 0; f < frameCount; ++f) {
        // Peak across channels this frame.
        f32 peak = 0.0f;
        for (u32 c = 0; c < channels; ++c) {
            const f32 a = std::fabs(buffer[static_cast<usize>(f) * channels + c]);
            if (a > peak) peak = a;
        }
        // Envelope follower (attack when rising, release when falling).
        const f32 coef = (peak > env_) ? attackCoef_ : releaseCoef_;
        env_ += (peak - env_) * coef;

        // Static gain computer (above threshold only).
        f32 gain = makeupLin_;
        if (env_ > threshLin_ && env_ > 1e-9f) {
            const f32 overDb = 20.0f * std::log10(env_ / threshLin_);
            const f32 grDb = overDb * (1.0f - invRatio); // gain reduction
            gain *= std::pow(10.0f, -grDb / 20.0f);
        }
        for (u32 c = 0; c < channels; ++c) buffer[static_cast<usize>(f) * channels + c] *= gain;
    }
}

} // namespace reverie
