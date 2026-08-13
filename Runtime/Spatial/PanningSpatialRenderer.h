// Reverie/Runtime/Spatial/PanningSpatialRenderer.h - dependency-free positional renderer.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The always-available spatial backend: constant-power amplitude panning by listener-relative
// azimuth, plus linear distance attenuation and a simple occlusion attenuation. No HRTF, no room
// model (that is what the Resonance backend adds behind the same ISpatialRenderer interface), and
// no external dependencies - so Reverie ships spatial audio out of the box.
#pragma once

#include "Spatial/SpatialRenderer.h"

#include <vector>

namespace reverie {

class PanningSpatialRenderer final : public ISpatialRenderer {
public:
    Result Init(u32 sampleRate, u32 maxSources) override;
    void Shutdown() override;
    const char* Name() const override { return "Panning"; }
    u32 Capacity() const override { return static_cast<u32>(sources_.size()); }

    int AcquireSource(SpatialQuality quality) override;
    void ReleaseSource(int slot) override;

    void SetListener(const Float3& position, const Float3& forward, const Float3& up) override;
    void SetSource(int slot, const Float3& position, f32 volume, f32 minDistance,
                   f32 maxDistance) override;
    void SetSourceOcclusion(int slot, f32 occlusion01) override;
    void SetSourceSpread(int slot, f32 spreadDegrees) override;
    void SetEnvironment(const AcousticEnvironment& /*env*/) override {} // no room model

    void BeginBlock(u32 frameCount) override;
    void SubmitSourceAudio(int slot, const f32* mono, u32 frameCount) override;
    void Render(f32* stereoOut, u32 frameCount) override;

private:
    struct Source {
        bool active = false;
        Float3 position;
        f32 volume = 1.0f;
        f32 minDistance = 1.0f;
        f32 maxDistance = 100.0f;
        f32 occlusion = 0.0f;
        f32 spread = 0.0f;
    };
    bool ValidSlot(int slot) const {
        return slot >= 0 && static_cast<usize>(slot) < sources_.size();
    }

    std::vector<Source> sources_;
    Float3 listenerPos_;
    Float3 listenerFwd_{0.0f, 0.0f, -1.0f};
    Float3 listenerUp_{0.0f, 1.0f, 0.0f};
    std::vector<f32> accum_; // interleaved stereo accumulator for the current block
    u32 sampleRate_ = 48000;
};

} // namespace reverie
