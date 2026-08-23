// Reverie/Runtime/DSP/BiquadFilter.cpp - see BiquadFilter.h. RBJ audio-EQ cookbook coefficients.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "DSP/BiquadFilter.h"

#include <cmath>

namespace reverie {

namespace {
constexpr f32 kPi = 3.14159265358979323846f;
inline f32 ClampMin(f32 v, f32 lo) { return v < lo ? lo : v; }
} // namespace

void BiquadFilter::Prepare(u32 sampleRate, u32 /*channels*/) {
    sampleRate_ = sampleRate == 0 ? 48000 : sampleRate;
    Reset();
    Recompute();
}

void BiquadFilter::Reset() {
    z1_.fill(0.0f);
    z2_.fill(0.0f);
}

void BiquadFilter::SetParam(u32 index, f32 value) {
    switch (index) {
    case 0: {
        u32 t = static_cast<u32>(value + 0.5f);
        if (t > static_cast<u32>(FilterType::HighShelf)) t = static_cast<u32>(FilterType::HighShelf);
        type_ = static_cast<FilterType>(t);
        break;
    }
    case 1: cutoff_ = value; break;
    case 2: q_ = value; break;
    case 3: gainDb_ = value; break;
    default: return;
    }
    Recompute();
}

f32 BiquadFilter::GetParam(u32 index) const {
    switch (index) {
    case 0: return static_cast<f32>(type_);
    case 1: return cutoff_;
    case 2: return q_;
    case 3: return gainDb_;
    default: return 0.0f;
    }
}

void BiquadFilter::Recompute() {
    // Clamp to a sane range: [1 Hz, ~0.49*fs], Q>0.
    const f32 nyq = 0.5f * static_cast<f32>(sampleRate_);
    const f32 fc = cutoff_ < 1.0f ? 1.0f : (cutoff_ > 0.98f * nyq ? 0.98f * nyq : cutoff_);
    const f32 q = ClampMin(q_, 1e-3f);
    const f32 w0 = 2.0f * kPi * fc / static_cast<f32>(sampleRate_);
    const f32 cw = std::cos(w0);
    const f32 sw = std::sin(w0);
    const f32 alpha = sw / (2.0f * q);
    const f32 A = std::pow(10.0f, gainDb_ / 40.0f); // for shelf/peak

    f32 b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;
    switch (type_) {
    case FilterType::Lowpass:
        b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = (1.0f - cw) * 0.5f;
        a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        break;
    case FilterType::Highpass:
        b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = (1.0f + cw) * 0.5f;
        a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        break;
    case FilterType::Bandpass: // constant 0 dB peak gain
        b0 = alpha; b1 = 0.0f; b2 = -alpha;
        a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        break;
    case FilterType::Notch:
        b0 = 1.0f; b1 = -2.0f * cw; b2 = 1.0f;
        a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        break;
    case FilterType::Peak:
        b0 = 1.0f + alpha * A; b1 = -2.0f * cw; b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A; a1 = -2.0f * cw; a2 = 1.0f - alpha / A;
        break;
    case FilterType::LowShelf: {
        const f32 tsa = 2.0f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + tsa);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - tsa);
        a0 = (A + 1.0f) + (A - 1.0f) * cw + tsa;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
        a2 = (A + 1.0f) + (A - 1.0f) * cw - tsa;
        break;
    }
    case FilterType::HighShelf: {
        const f32 tsa = 2.0f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cw + tsa);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cw - tsa);
        a0 = (A + 1.0f) - (A - 1.0f) * cw + tsa;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
        a2 = (A + 1.0f) - (A - 1.0f) * cw - tsa;
        break;
    }
    }

    const f32 inv = a0 != 0.0f ? 1.0f / a0 : 1.0f;
    b0_ = b0 * inv; b1_ = b1 * inv; b2_ = b2 * inv; a1_ = a1 * inv; a2_ = a2 * inv;
}

void BiquadFilter::Process(f32* buffer, u32 frameCount, u32 channels) {
    if (buffer == nullptr || channels == 0) return;
    const u32 ch = channels > kMaxChannels ? kMaxChannels : channels;
    // Snapshot coefficients once (they may be updated between blocks by SetParam).
    const f32 b0 = b0_, b1 = b1_, b2 = b2_, a1 = a1_, a2 = a2_;
    for (u32 f = 0; f < frameCount; ++f) {
        for (u32 c = 0; c < ch; ++c) {
            const f32 x = buffer[static_cast<usize>(f) * channels + c];
            // Transposed Direct Form II.
            const f32 y = b0 * x + z1_[c];
            z1_[c] = b1 * x - a1 * y + z2_[c];
            z2_[c] = b2 * x - a2 * y;
            buffer[static_cast<usize>(f) * channels + c] = y;
        }
    }
}

} // namespace reverie
