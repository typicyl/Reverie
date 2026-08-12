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
#include "Voices/Voice.h"

#include <memory>
#include <mutex>
#include <vector>

namespace reverie {

struct VoiceSpawn {
    std::shared_ptr<const AudioBuffer> buffer;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    bool loop = false;
    i32 priority = 0;
    InstanceId eventInstance = 0;
    u32 concurrencyGroup = 0;
};

class VoiceManager {
public:
    void SetMaxRealVoices(u32 count); // 0 is treated as 1 (never zero the budget)
    u32 MaxRealVoices() const;

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

    // Mixes all real voices into `out` (interleaved f32, `channels`) at the device rate,
    // advancing every voice (virtual ones silently), reaping finished voices, and
    // re-evaluating the real/virtual split afterwards.
    void Mix(f32* out, u32 frameCount, u32 channels, u32 dstSampleRate);

private:
    void ReprioritizeLocked();

    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    u32 maxReal_ = 64;
    VoiceId nextId_ = 1;
    u64 nextAge_ = 1;
};

} // namespace reverie
