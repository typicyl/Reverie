// Reverie/Runtime/Spatial/PanningSpatialRenderer.cpp - see PanningSpatialRenderer.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Spatial/PanningSpatialRenderer.h"

#include <cmath>
#include <cstring>

namespace reverie {

namespace {

constexpr f32 kHalfPi = 1.57079632679489661923f;

f32 Dot(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Float3 Cross(const Float3& a, const Float3& b) {
    return Float3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Float3 Normalized(const Float3& v) {
    const f32 len = std::sqrt(Dot(v, v));
    if (len < 1e-6f) return Float3{0.0f, 0.0f, 0.0f};
    return Float3{v.x / len, v.y / len, v.z / len};
}
f32 Clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

Result PanningSpatialRenderer::Init(u32 sampleRate, u32 maxSources) {
    sampleRate_ = sampleRate != 0 ? sampleRate : 48000;
    sources_.assign(maxSources != 0 ? maxSources : 64, Source{});
    accum_.clear();
    return Result::Ok;
}

void PanningSpatialRenderer::Shutdown() {
    sources_.clear();
    accum_.clear();
}

int PanningSpatialRenderer::AcquireSource(SpatialQuality /*quality*/) {
    for (usize i = 0; i < sources_.size(); ++i) {
        if (!sources_[i].active) {
            sources_[i] = Source{};
            sources_[i].active = true;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PanningSpatialRenderer::ReleaseSource(int slot) {
    if (ValidSlot(slot)) sources_[slot].active = false;
}

void PanningSpatialRenderer::SetListener(const Float3& position, const Float3& forward,
                                         const Float3& up) {
    listenerPos_ = position;
    listenerFwd_ = Normalized(forward);
    if (Dot(listenerFwd_, listenerFwd_) < 0.5f) listenerFwd_ = Float3{0.0f, 0.0f, -1.0f};
    listenerUp_ = Normalized(up);
    if (Dot(listenerUp_, listenerUp_) < 0.5f) listenerUp_ = Float3{0.0f, 1.0f, 0.0f};
}

void PanningSpatialRenderer::SetSource(int slot, const Float3& position, f32 volume,
                                       f32 minDistance, f32 maxDistance) {
    if (!ValidSlot(slot)) return;
    Source& s = sources_[slot];
    s.position = position;
    s.volume = volume < 0.0f ? 0.0f : volume;
    s.minDistance = minDistance < 0.01f ? 0.01f : minDistance;
    s.maxDistance = maxDistance > s.minDistance ? maxDistance : s.minDistance + 0.01f;
}

void PanningSpatialRenderer::SetSourceOcclusion(int slot, f32 occlusion01) {
    if (ValidSlot(slot)) sources_[slot].occlusion = Clamp(occlusion01, 0.0f, 1.0f);
}
void PanningSpatialRenderer::SetSourceSpread(int slot, f32 spreadDegrees) {
    if (ValidSlot(slot)) sources_[slot].spread = Clamp(spreadDegrees, 0.0f, 180.0f);
}

void PanningSpatialRenderer::BeginBlock(u32 frameCount) {
    accum_.assign(static_cast<usize>(frameCount) * 2, 0.0f);
}

void PanningSpatialRenderer::SubmitSourceAudio(int slot, const f32* mono, u32 frameCount) {
    if (!ValidSlot(slot) || mono == nullptr || frameCount == 0) return;
    const Source& s = sources_[slot];
    if (!s.active) return;
    if (accum_.size() < static_cast<usize>(frameCount) * 2) accum_.assign(frameCount * 2, 0.0f);

    const Float3 d{s.position.x - listenerPos_.x, s.position.y - listenerPos_.y,
                   s.position.z - listenerPos_.z};
    const f32 dist = std::sqrt(Dot(d, d));

    f32 atten;
    if (dist <= s.minDistance) atten = 1.0f;
    else if (dist >= s.maxDistance) atten = 0.0f;
    else atten = (s.maxDistance - dist) / (s.maxDistance - s.minDistance);

    f32 pan = 0.0f; // -1 left .. +1 right
    if (dist > 1e-4f) {
        const Float3 right = Normalized(Cross(listenerFwd_, listenerUp_));
        pan = Clamp(Dot(Float3{d.x / dist, d.y / dist, d.z / dist}, right), -1.0f, 1.0f);
    }
    if (s.spread > 0.0f) pan *= (1.0f - Clamp(s.spread / 180.0f, 0.0f, 1.0f)); // widen toward centre

    const f32 angle = (pan * 0.5f + 0.5f) * kHalfPi; // constant-power
    const f32 occAtten = 1.0f - s.occlusion * 0.7f;
    const f32 g = s.volume * atten * occAtten;
    const f32 gainL = std::cos(angle) * g;
    const f32 gainR = std::sin(angle) * g;

    for (u32 i = 0; i < frameCount; ++i) {
        accum_[static_cast<usize>(i) * 2 + 0] += mono[i] * gainL;
        accum_[static_cast<usize>(i) * 2 + 1] += mono[i] * gainR;
    }
}

void PanningSpatialRenderer::Render(f32* stereoOut, u32 frameCount) {
    if (stereoOut == nullptr || frameCount == 0) return;
    const usize want = static_cast<usize>(frameCount) * 2;
    const usize have = accum_.size() < want ? accum_.size() : want;
    if (have > 0) std::memcpy(stereoOut, accum_.data(), have * sizeof(f32));
    if (have < want) std::memset(stereoOut + have, 0, (want - have) * sizeof(f32));
}

} // namespace reverie
