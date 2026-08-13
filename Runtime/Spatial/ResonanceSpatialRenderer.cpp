// Reverie/Runtime/Spatial/ResonanceSpatialRenderer.cpp - see ResonanceSpatialRenderer.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The ONLY Reverie file that touches vraudio (the HDS Resonance fork). It adapts vraudio's
// single-instance binaural renderer to Reverie's ISpatialRenderer: a fixed pool of sound-object
// sources (fed silence when idle so the re-block stays aligned), listener + per-source params,
// the shoebox room model, and an internal re-block ring buffer that bridges Reverie's variable
// block size to vraudio's fixed frames_per_buffer.
#include "Spatial/ResonanceSpatialRenderer.h"

#include "Core/Log.h"

#if REVERIE_WITH_RESONANCE

#include "resonance_audio/api/resonance_audio_api.h" // vraudio (fork repo root on include path)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace reverie {

namespace {

constexpr size_t kResBlock = 256;   // vraudio fixed frames_per_buffer
constexpr size_t kMaxResSources = 64; // pool cap (all are processed every block)
constexpr size_t kAccumSlack = 8192;  // per-source accumulator headroom

// forward + up -> quaternion (x,y,z,w) using the -Z-forward right-handed convention Resonance
// documents. (Exact front/back/left/right may want a playtest tweak, same caveat as any HRTF
// integration; the panning fallback is unaffected.)
Quat LookRotation(const Float3& forward, const Float3& up) {
    auto norm = [](const Float3& v) {
        const f32 l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return l < 1e-6f ? Float3{0, 0, -1} : Float3{v.x / l, v.y / l, v.z / l};
    };
    auto cross = [](const Float3& a, const Float3& b) {
        return Float3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    const Float3 f = norm(forward);
    const Float3 c2{-f.x, -f.y, -f.z};       // column 2 = -forward
    const Float3 c0 = norm(cross(up, c2));   // column 0 = right
    const Float3 c1 = cross(c2, c0);         // column 1 = up
    // Matrix (columns c0,c1,c2); mNN with row,col.
    const f32 m00 = c0.x, m01 = c1.x, m02 = c2.x;
    const f32 m10 = c0.y, m11 = c1.y, m12 = c2.y;
    const f32 m20 = c0.z, m21 = c1.z, m22 = c2.z;
    const f32 trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        f32 s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m21 - m12) * s;
        q.y = (m02 - m20) * s;
        q.z = (m10 - m01) * s;
    } else if (m00 > m11 && m00 > m22) {
        f32 s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        f32 s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        f32 s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q;
}

} // namespace

class ResonanceSpatialRenderer final : public ISpatialRenderer {
public:
    ~ResonanceSpatialRenderer() override { Shutdown(); }

    Result Init(u32 sampleRate, u32 maxSources) override {
        const size_t n = std::min<size_t>(maxSources != 0 ? maxSources : 32, kMaxResSources);
        api_ = vraudio::CreateResonanceAudioApi(2, kResBlock, static_cast<int>(sampleRate));
        if (api_ == nullptr) {
            LogMessage(LogLevel::Error, "ResonanceSpatialRenderer: CreateResonanceAudioApi failed");
            return Result::DeviceError;
        }
        sources_.resize(n);
        for (Src& s : sources_) {
            s.id = api_->CreateSoundObjectSource(vraudio::kBinauralHighQuality);
            s.active = false;
            api_->SetSourceVolume(s.id, 0.0f);
            s.accum.assign(kResBlock + kAccumSlack, 0.0f);
        }
        stereoTmp_.assign(kResBlock * 2, 0.0f);
        ringCap_ = kResBlock * 8;
        ring_.assign(ringCap_ * 2, 0.0f);
        ringHead_ = ringCount_ = accumFill_ = 0;
        LogFormat(LogLevel::Info, "ResonanceSpatialRenderer: ready (%zu HRTF sources, %u Hz)", n,
                  sampleRate);
        return Result::Ok;
    }

    void Shutdown() override {
        if (api_ != nullptr) {
            delete api_; // frees every pooled source with it
            api_ = nullptr;
        }
        sources_.clear();
    }

    const char* Name() const override { return "Resonance"; }
    u32 Capacity() const override { return static_cast<u32>(sources_.size()); }

    int AcquireSource(SpatialQuality /*quality*/) override {
        for (usize i = 0; i < sources_.size(); ++i) {
            if (!sources_[i].active) {
                sources_[i].active = true;
                if (api_ != nullptr) {
                    api_->SetSourceVolume(sources_[i].id, 1.0f);
                    api_->SetSoundObjectOcclusionIntensity(sources_[i].id, 0.0f);
                }
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void ReleaseSource(int slot) override {
        if (InRange(slot)) {
            sources_[slot].active = false;
            if (api_ != nullptr) api_->SetSourceVolume(sources_[slot].id, 0.0f);
        }
    }

    void SetListener(const Float3& position, const Float3& forward, const Float3& up) override {
        if (api_ == nullptr) return;
        api_->SetHeadPosition(position.x, position.y, position.z);
        const Quat q = LookRotation(forward, up);
        api_->SetHeadRotation(q.x, q.y, q.z, q.w);
    }

    void SetSource(int slot, const Float3& position, f32 volume, f32 minDistance,
                   f32 maxDistance) override {
        if (!Active(slot) || api_ == nullptr) return;
        const auto id = sources_[slot].id;
        api_->SetSourcePosition(id, position.x, position.y, position.z);
        api_->SetSourceVolume(id, volume);
        api_->SetSourceDistanceModel(id, vraudio::kLogarithmic, minDistance, maxDistance);
    }

    void SetSourceOcclusion(int slot, f32 occlusion01) override {
        if (Active(slot) && api_ != nullptr)
            api_->SetSoundObjectOcclusionIntensity(sources_[slot].id, occlusion01);
    }
    void SetSourceSpread(int slot, f32 spreadDegrees) override {
        if (Active(slot) && api_ != nullptr)
            api_->SetSoundObjectSpread(sources_[slot].id, spreadDegrees);
    }

    void SetEnvironment(const AcousticEnvironment& env) override {
        if (api_ == nullptr) return;
        api_->EnableRoomEffects(env.enabled);
        if (!env.enabled) return;
        vraudio::ReflectionProperties refl;
        refl.room_position[0] = env.position.x;
        refl.room_position[1] = env.position.y;
        refl.room_position[2] = env.position.z;
        refl.room_rotation[0] = env.rotation.x;
        refl.room_rotation[1] = env.rotation.y;
        refl.room_rotation[2] = env.rotation.z;
        refl.room_rotation[3] = env.rotation.w;
        refl.room_dimensions[0] = env.dimensions.x;
        refl.room_dimensions[1] = env.dimensions.y;
        refl.room_dimensions[2] = env.dimensions.z;
        refl.cutoff_frequency = 800.0f;
        for (int i = 0; i < 6; ++i) refl.coefficients[i] = 0.5f; // mid reflectivity
        refl.gain = 1.0f;
        api_->SetReflectionProperties(refl);

        vraudio::ReverbProperties rev;
        for (int i = 0; i < 9; ++i) rev.rt60_values[i] = 0.5f * env.reverbGain;
        rev.gain = env.reverbGain;
        api_->SetReverbProperties(rev);
    }

    void BeginBlock(u32 frameCount) override {
        const size_t f = frameCount;
        if (accumFill_ + f > kResBlock + kAccumSlack) return; // absurd block size; skip
        for (Src& s : sources_) std::memset(&s.accum[accumFill_], 0, f * sizeof(f32));
        accumFill_ += f;
    }

    void SubmitSourceAudio(int slot, const f32* mono, u32 frameCount) override {
        if (!Active(slot) || mono == nullptr) return;
        if (accumFill_ >= frameCount)
            std::memcpy(&sources_[slot].accum[accumFill_ - frameCount], mono,
                        frameCount * sizeof(f32));
    }

    void Render(f32* stereoOut, u32 frameCount) override {
        if (api_ == nullptr || stereoOut == nullptr) return;
        while (accumFill_ >= kResBlock) {
            for (Src& s : sources_) api_->SetInterleavedBuffer(s.id, s.accum.data(), 1, kResBlock);
            api_->FillInterleavedOutputBuffer(2, kResBlock, stereoTmp_.data());
            RingPush(stereoTmp_.data(), kResBlock);
            const size_t rem = accumFill_ - kResBlock;
            if (rem > 0)
                for (Src& s : sources_)
                    std::memmove(s.accum.data(), s.accum.data() + kResBlock, rem * sizeof(f32));
            accumFill_ = rem;
        }
        const size_t got = RingPop(stereoOut, frameCount);
        if (got < frameCount)
            std::memset(stereoOut + got * 2, 0, (frameCount - got) * 2 * sizeof(f32));
    }

private:
    struct Src {
        bool active = false;
        vraudio::ResonanceAudioApi::SourceId id = -1;
        std::vector<f32> accum;
    };
    bool InRange(int s) const { return s >= 0 && static_cast<usize>(s) < sources_.size(); }
    bool Active(int s) const { return InRange(s) && sources_[s].active; }

    void RingPush(const f32* stereo, size_t frames) {
        for (size_t i = 0; i < frames; ++i) {
            const size_t w = (ringHead_ + ringCount_) % ringCap_;
            ring_[w * 2 + 0] = stereo[i * 2 + 0];
            ring_[w * 2 + 1] = stereo[i * 2 + 1];
            if (ringCount_ < ringCap_) ++ringCount_;
            else ringHead_ = (ringHead_ + 1) % ringCap_;
        }
    }
    size_t RingPop(f32* out, size_t frames) {
        const size_t n = frames < ringCount_ ? frames : ringCount_;
        for (size_t i = 0; i < n; ++i) {
            out[i * 2 + 0] = ring_[ringHead_ * 2 + 0];
            out[i * 2 + 1] = ring_[ringHead_ * 2 + 1];
            ringHead_ = (ringHead_ + 1) % ringCap_;
        }
        ringCount_ -= n;
        return n;
    }

    vraudio::ResonanceAudioApi* api_ = nullptr;
    std::vector<Src> sources_;
    size_t accumFill_ = 0;
    std::vector<f32> stereoTmp_;
    std::vector<f32> ring_;
    size_t ringCap_ = 0, ringHead_ = 0, ringCount_ = 0;
};

std::unique_ptr<ISpatialRenderer> CreateResonanceSpatialRenderer() {
    return std::make_unique<ResonanceSpatialRenderer>();
}
bool ResonanceBackendBuilt() { return true; }

} // namespace reverie

#else // REVERIE_WITH_RESONANCE == 0 : backend not built; the factory yields nothing.

namespace reverie {
std::unique_ptr<ISpatialRenderer> CreateResonanceSpatialRenderer() { return nullptr; }
bool ResonanceBackendBuilt() { return false; }
} // namespace reverie

#endif // REVERIE_WITH_RESONANCE
