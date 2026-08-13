// Reverie/Runtime/Voices/VoiceManager.h - the voice pool + mixer front-end.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Owns every active voice and does the actual mixing. Designed for large scenes: a budget caps
// how many voices are REAL (mixed) at once; the rest are VIRTUALIZED - they keep advancing
// their playback position so they stay in sync, but produce no audio and cost almost nothing.
// Priority (with age as a tie-break) decides who is real; a new important voice steals a real
// slot from a less important one (which becomes virtual). This is the mechanism behind
// distance-based priority, concurrency limits, and CPU-aware culling in later phases.
//
// Thread-safety: voice operations (Play/Stop) come from the game thread; Mix runs on the audio
// thread. All access is guarded by an internal mutex (a lock-free command queue is a later
// optimization; correctness first).
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"
#include "Spatial/SpatialRenderer.h"
#include "Voices/Voice.h"

#include <memory>
#include <mutex>
#include <vector>

namespace reverie {

class Mixer; // voices mix into their target bus's buffer (Mixer::BusBuffer)

struct VoiceSpawn {
    std::shared_ptr<const AudioBuffer> buffer;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    bool loop = false;
    i32 priority = 0;
    InstanceId eventInstance = 0;
    u32 concurrencyGroup = 0;
    BusId bus = kInvalidId; // routing target (kInvalidId = Master)

    // Spatial (3D): when `spatial`, the voice is rendered by the spatial renderer at `position`
    // instead of the flat upmix, and its output goes to the mixer's Spatial bus.
    bool spatial = false;
    Float3 position;
    SpatialQuality quality = SpatialQuality::Panning;
    f32 minDistance = 1.0f;
    f32 maxDistance = 100.0f;
};

class VoiceManager {
public:
    // Optional spatial renderer. When set, spatial voices claim a source slot from it, submit
    // their mono signal each block, and its stereo mix is added to the mixer's Spatial bus.
    void SetSpatialRenderer(ISpatialRenderer* renderer) { spatial_ = renderer; }

    void SetMaxRealVoices(u32 count); // 0 is treated as 1 (never zero the budget)
    u32 MaxRealVoices() const;

    // Updates a live spatial voice's world position (no-op if the voice is gone / not spatial).
    void SetVoicePosition(VoiceId id, const Float3& position);

    VoiceId Play(const VoiceSpawn& spawn); // kInvalidId if buffer is empty
    void Stop(VoiceId id);
    void StopInstance(InstanceId instance);
    void StopGroup(u32 group);
    void StopAll();

    u32 ActiveVoiceCount() const;  // total playing (real + virtual)
    u32 RealVoiceCount() const;    // currently mixed
    u32 VirtualVoiceCount() const; // currently virtual
    u32 GroupVoiceCount(u32 group) const;
    u32 InstanceVoiceCount(InstanceId instance) const;

    // Mixes each real voice into its TARGET BUS's block buffer (via Mixer::BusBuffer),
    // advancing every voice (virtual ones silently), reaping finished voices, and
    // re-evaluating the real/virtual split afterwards. The mixer must be inside a
    // BeginBlock/EndBlock pair sized to `frameCount`/`channels`.
    void MixToBuses(Mixer& mixer, u32 frameCount, u32 channels, u32 dstSampleRate);

private:
    void ReprioritizeLocked();
    void ReapLocked(); // releases spatial slots of finished voices, then erases them

    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    ISpatialRenderer* spatial_ = nullptr;
    std::vector<f32> monoTmp_; // scratch for downmixing a spatial voice to mono
    u32 maxReal_ = 64;
    VoiceId nextId_ = 1;
    u64 nextAge_ = 1;
};

} // namespace reverie
